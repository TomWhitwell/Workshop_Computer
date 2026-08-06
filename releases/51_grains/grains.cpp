#define COMPUTERCARD_SAMPLE_RATE_DIV 1
#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 48000
#define GRAIN_BUF_SAMPLES 72000
#define SWITCH_TAP_MAX_SAMPLES 6000u
#define DENSITY_DEADZONE_Q15 2400
#define MAX_GRAINS 48
volatile bool tapeMode = false;

uint32_t fast_rand_seed0 = 12345, fast_rand_seed1 = 67890;
uint32_t __not_in_flash_func(fast_rand)(int core) {
  uint32_t &s = (core == 0) ? fast_rand_seed0 : fast_rand_seed1;
  s = (1103515245 * s + 12345);
  return s;
}
int32_t __not_in_flash_func(fast_rand_q15)(int core) {
  return (int32_t)((fast_rand(core) >> 16) & 0x7FFF);
}
int32_t __not_in_flash_func(fast_rand_q15_sym)(int core) {
  return (int32_t)((fast_rand(core) >> 16) & 0xFFFF) - 32768;
}

#define HANN_LUT_SIZE 1024
int32_t hannLUT[HANN_LUT_SIZE];
void initHannLUT() {
  int64_t y_m1 = 1073721663, y_0 = 1073741824, coef = 2147443326;
  for (int i = 0; i < HANN_LUT_SIZE; i++) {
    hannLUT[i] = (int32_t)((1073741824LL - y_0) * 16384LL >> 30);
    int64_t y_1 = (coef * y_0 >> 30) - y_m1;
    y_m1 = y_0;
    y_0 = y_1;
  }
}
inline int32_t __not_in_flash_func(hannEnvelope)(uint32_t phase_q16) {
  uint32_t i0 = (phase_q16 >> 6) & 1023;
  uint32_t i1 = (i0 + 1) & 1023;
  int32_t f = (phase_q16 & 0x3F) << 10;
  int32_t v0 = hannLUT[i0], v1 = hannLUT[i1];
  return v0 + (((v1 - v0) * f) >> 16);
}
uint32_t __not_in_flash_func(exp2_q16)(int32_t x_q16) {
  int32_t i = x_q16 >> 16, f = x_q16 & 0xFFFF,
          f2 = (int32_t)(((int64_t)f * f) >> 16);
  int32_t fp1 = (int32_t)(((int64_t)f * 45426) >> 16),
          fp2 = (int32_t)(((int64_t)f2 * 15746) >> 16);
  uint32_t res = 65536 + fp1 + fp2;
  if (i >= 15)
    return 0x7FFFFFFF;
  if (i >= 0)
    return res << i;
  else if (i < -31)
    return 0;
  else
    return res >> (-i);
}

struct Diffuser {
  int16_t b1[157], b2[211], b3[353], b4[401];
  int p1 = 0, p2 = 0, p3 = 0, p4 = 0;
  void init() {
    memset(b1, 0, sizeof(b1));
    memset(b2, 0, sizeof(b2));
    memset(b3, 0, sizeof(b3));
    memset(b4, 0, sizeof(b4));
  }
  int32_t __not_in_flash_func(process)(int32_t in) {
    auto ap = [](int32_t in, int16_t *b, int size, int &p, int32_t g) {
      int32_t del = b[p], out = del - ((in * g) >> 15),
              fb = in + ((del * g) >> 15);
      if (fb > 32767)
        fb = 32767;
      else if (fb < -32768)
        fb = -32768;
      b[p] = (int16_t)fb;
      if (++p >= size)
        p = 0;
      return out;
    };
    in = ap(in, b1, 157, p1, 16384);
    in = ap(in, b2, 211, p2, 16384);
    in = ap(in, b3, 353, p3, 16384);
    in = ap(in, b4, 401, p4, 16384);
    return in;
  }
};
struct NoiseGate {
  int32_t lpf = 0;
  int32_t gain = 0;
  int32_t process(int32_t in) {
    in <<= 6;
    lpf += ((in - lpf) * 100) >> 16;
    int32_t hpf = in - lpf;
    int32_t ahpf = hpf > 0 ? hpf : -hpf;

    // Threshold: ~32 units in 12-bit range (32 * 1024 = 32768 in Q22)
    // This is aggressive enough to kill processor whine during silence.
    if (ahpf > 32768) {
      gain += (32768 - gain) >> 7; // Fast attack
    } else if (ahpf < 16384) {
      gain += (0 - gain) >> 11; // Slow release
    }

    return (int32_t)(((int64_t)hpf * gain) >> 21);
  }
};
class PlateReverb {
  struct AP {
    int16_t *bufIn, *bufOut;
    uint16_t mask, ptr, len;
    int16_t g;
    void init(int16_t *b, uint16_t m, uint16_t l, int16_t gain) {
      bufIn = b;
      bufOut = b + m + 1;
      mask = m;
      len = l;
      g = gain;
      ptr = 0;
    }
    int16_t process(int16_t in) {
      uint16_t rd = (ptr - len) & mask;
      int16_t dIn = bufIn[rd], dOut = bufOut[rd];
      int32_t interm =
          ((int32_t)(g * in) >> 15) + dIn - ((int32_t)(dOut * g) >> 15);
      if (interm > 32767)
        interm = 32767;
      else if (interm < -32768)
        interm = -32768;
      int16_t out = (int16_t)interm;
      bufIn[ptr] = in;
      bufOut[ptr] = out;
      ptr = (ptr + 1) & mask;
      return out;
    }
  };
  struct Delay {
    int16_t *buf;
    uint16_t mask, ptr, len;
    void init(int16_t *b, uint16_t m, uint16_t l) {
      buf = b;
      mask = m;
      len = l;
      ptr = 0;
    }
    void write(int16_t in) {
      buf[ptr] = in;
      ptr = (ptr + 1) & mask;
    }
    int16_t read() { return buf[(ptr - len) & mask]; }
  };
  int16_t mem[35840];
  AP apIn[4], apTankL, apTankR;
  Delay modL, d1L, d2L, modR, d1R, d2R;
  int32_t lpL = 0, lpR = 0, lpIn = 0;
  uint32_t lfo = 0;

public:
  PlateReverb() {
    memset(mem, 0, sizeof(mem));
    int16_t *p = mem;
    apIn[0].init(p, 127, 229, 24576);
    p += 256;
    apIn[1].init(p, 127, 172, 24576);
    p += 256;
    apIn[2].init(p, 511, 611, 20480);
    p += 1024;
    apIn[3].init(p, 255, 447, 20480);
    p += 512;
    modL.init(p, 1023, 1083);
    p += 1024;
    d1L.init(p, 4095, 6000);
    p += 4096;
    apTankL.init(p, 2047, 2903, 16384);
    p += 4096;
    d2L.init(p, 4095, 5800);
    p += 4096;
    modR.init(p, 1023, 1464);
    p += 1024;
    d1R.init(p, 4095, 6200);
    p += 4096;
    apTankR.init(p, 2047, 3850, 16384);
    p += 4096;
    d2R.init(p, 4095, 5500);
    p += 4096;
  }
  void __not_in_flash_func(process)(int32_t &outL, int32_t &outR, int32_t mix,
                                    int32_t decay, int32_t damp) {
    if (mix < 10)
      return;
    int16_t mono = (int16_t)((outL + outR) >> 1);
    lpIn += (((int32_t)mono - lpIn) * 16000) >> 15;
    mono = (int16_t)(lpIn >> 1);
    for (int i = 0; i < 4; i++)
      mono = apIn[i].process(mono);
    lfo += 4096;
    int16_t mod = (lfo >> 16) & 0x7FFF;
    if (lfo & 0x80000000)
      mod = 32767 - mod;
    int16_t tOutL = d2L.read(), tOutR = d2R.read();
    int32_t iL = (int32_t)mono + (((int32_t)decay * tOutR) >> 15);
    int16_t sL = (int16_t)iL + (int16_t)((16384 * modL.read()) >> 15);
    modL.write((int16_t)iL - (int16_t)((16384 * sL) >> 15));
    d1L.write(sL);
    sL = d1L.read();
    lpL += (((int32_t)sL - lpL) * damp) >> 15;
    sL = (int16_t)lpL;
    sL = apTankL.process(sL);
    d2L.write(sL);
    int32_t iR = (int32_t)mono + (((int32_t)decay * tOutL) >> 15);
    int16_t sR = (int16_t)iR + (int16_t)((16384 * modR.read()) >> 15);
    modR.write((int16_t)iR - (int16_t)((16384 * sR) >> 15));
    d1R.write(sR);
    sR = d1R.read();
    lpR += (((int32_t)sR - lpR) * damp) >> 15;
    sR = (int16_t)lpR;
    sR = apTankR.process(sR);
    d2R.write(sR);
    outL = (outL * (32767 - mix) + (int32_t)sL * mix) >> 15;
    outR = (outR * (32767 - mix) + (int32_t)sR * mix) >> 15;
  }
};

struct Grain {
  bool active = false;
  uint32_t posInt = 0;
  uint32_t posFrac = 0;
  uint32_t phaseInc = 0, envPhase = 0, envPhaseInc = 0;
  int32_t amp = 0, panL = 16384, panR = 16384;
  int8_t envSIdx = 0;
  int16_t envSFrac = 0;
  int32_t envShape = 0;
  bool reverse = false;
};
int16_t grainBuffer[GRAIN_BUF_SAMPLES];
uint32_t grainWritePos = 0;
Grain grains_pool[MAX_GRAINS];
volatile int32_t activeGrainsCount = 0;
volatile bool freezeMode = false;
volatile int currentPage = 0;
struct PageParams {
  int32_t pMain, pX, pY;
};
PageParams params[4];
struct KnobLock {
  bool locked = true;
  int32_t ref = 0;
  void engage(int32_t v) {
    locked = true;
    ref = v;
  }
  bool update(int32_t v) {
    if (locked) {
      int32_t d = v - ref;
      if (d < 0)
        d = -d;
      if (d > 1638)
        locked = false;
    }
    return !locked;
  }
};
int32_t dc_bxL = 0, dc_bxR = 0, dc_byL = 0, dc_byR = 0, dc_oxL = 0, dc_oxR = 0,
        dc_oyL = 0, dc_oyR = 0, g_dc_xL = 0, g_dc_xR = 0, g_dc_yL = 0,
        g_dc_yR = 0;
inline int32_t dc_block(int32_t x, int32_t &px, int32_t &py) {
  int32_t y = x - px + py - (py >> 8);
  px = x;
  py = y;
  return y;
}

struct FlashHeader {
  uint32_t magic;
  uint32_t count;
};
struct FlashSampleEntry {
  uint32_t offset;
  uint32_t length;
  char name[24];
};
#define FLASH_SAMPLE_ADDR 0x10100000
#define SAMPLES_MAGIC 0x6772616E

volatile bool synthMode = false;
uint32_t numFlashSamples = 0;
int32_t currentSampleIdx = 0;
const int16_t *activeBuffer = grainBuffer;
uint32_t activeBufferSize = GRAIN_BUF_SAMPLES;
volatile int32_t densityCV = 0;
volatile int16_t nextCV2 = 0;
volatile bool pulse2Trig = false;

volatile bool core1_paused = false;
volatile bool core1_is_paused = false;

#define SAVED_BUFFERS_FLASH_OFFSET 0x60000 // 384KB into flash
#define SAVED_BUFFER_SLOT_SIZE                                                 \
  147456 // 36 sectors * 4096 bytes = 144KB + padding

void saveBufferToFlash(int slot) {
  if (slot < 0 || slot > 3)
    return;
  uint32_t offset =
      SAVED_BUFFERS_FLASH_OFFSET + (slot * SAVED_BUFFER_SLOT_SIZE);
  flash_range_erase(offset, SAVED_BUFFER_SLOT_SIZE);
  flash_range_program(offset, (const uint8_t *)grainBuffer,
                      GRAIN_BUF_SAMPLES * 2);
}

void loadBufferFromFlash(int slot) {
  if (slot < 0 || slot > 3)
    return;
  uint32_t offset =
      SAVED_BUFFERS_FLASH_OFFSET + (slot * SAVED_BUFFER_SLOT_SIZE);
  const int16_t *flashPtr = (const int16_t *)(XIP_BASE + offset);
  memcpy(grainBuffer, flashPtr, GRAIN_BUF_SAMPLES * 2);
  activeBuffer = grainBuffer;
  activeBufferSize = GRAIN_BUF_SAMPLES;
}

const int16_t *getFlashSamplePtr(int idx, uint32_t &size) {
  if (numFlashSamples == 0 || idx < 0 || idx >= (int)numFlashSamples) {
    size = GRAIN_BUF_SAMPLES;
    return grainBuffer;
  }
  FlashSampleEntry *entries = (FlashSampleEntry *)(FLASH_SAMPLE_ADDR + 8);
  size = entries[idx].length;
  return (const int16_t *)(FLASH_SAMPLE_ADDR + entries[idx].offset);
}

void checkFlashSamples() {
  FlashHeader *header = (FlashHeader *)FLASH_SAMPLE_ADDR;
  if ((uint32_t)header < 0x10000000 || (uint32_t)header > 0x11000000) {
    numFlashSamples = 0;
    return;
  }
  if (header->magic == SAMPLES_MAGIC) {
    numFlashSamples = header->count;
    numFlashSamples = header->count;
    if (numFlashSamples > 28)
      numFlashSamples = 28;
  } else {
    numFlashSamples = 0;
  }
}

void loadFlashSampleToRam(int idx) {
  if (numFlashSamples == 0 || idx < 0 || idx >= (int)numFlashSamples)
    return;
  uint32_t sz;
  const int16_t *flashPtr = getFlashSamplePtr(idx, sz);
  if (!flashPtr)
    return;
  activeBuffer = flashPtr;
  activeBufferSize = sz;
}

volatile uint32_t densityCounter = 0, currentDensityThreshold = 0;
volatile uint32_t spawnCounter = 0;
static const int8_t SCALES[4][6] = {
    {0, 7, 12, -12, -5, 0}, // 5ths & Octs
    {0, 2, 4, 7, 9, 12},    // Major
    {0, 2, 3, 7, 8, 12},    // Minor
    {0, 3, 5, 7, 10, 12}    // Pentatonic
};

class Grains : public ComputerCard {
public:
  uint32_t swDT = 0;
  bool swDH = 0, lastSwU = 0, firstAR = 1, swEI = 0;
  KnobLock lM, lX, lY;
  int32_t sM = 0, sX = 0, sY = 0, CV1_acc = 0;
  int32_t fb_dc_x = 0, fb_dc_y = 0;
  uint32_t pOT = 0, p2OT = 0, tFT = 0, bSC = 48000;
  NoiseGate ngL, ngR;
  PlateReverb plate;
  Diffuser fDL, fDR;
  int16_t getCV2() { return CVIn2(); }
  Switch getSwitch() { return SwitchVal(); }
  int32_t getKnob(Knob k) { return KnobVal(k); }

  volatile int flashAction = -1;
  volatile Switch cachedSwitch = Switch::Middle;
  bool inFlashMenu = false;
  volatile bool trigger1Buffered = false;
  volatile bool trigger2Buffered = false;
  volatile double tapePlayhead = 0.0;
  volatile int32_t currentTapeSample = 0;
  volatile bool tapeAutoPlay = true;
  volatile bool tapePlaying = true;
  volatile int32_t menuSelectedSlot = 0;
  volatile int16_t cachedCV2 = 0;
  volatile int16_t cachedKnobMain = 0;
  volatile int16_t cachedKnobX = 0;
  volatile int16_t cachedKnobY = 0;

  void __not_in_flash_func(spawnGrain)() {
    if (activeGrainsCount >= MAX_GRAINS)
      return;
    int32_t pP = params[0].pMain + (CVIn2() << 4), sP = params[0].pY,
            shP = params[1].pMain, smP = params[1].pX, piP = params[1].pY;
    pOT = 480;
    tFT = 2400;
    uint32_t gS = 120 + ((sP * 11880) >> 15);
    pP &= 32767; // Infinite wrap-around for position

    int32_t posJitter = 0, pitchJitter = 0;
    bool rev = false;
    if (smP < 10922) {
      // Left Third: Position Jitter (Smear) with quadratic curve
      uint32_t relX = (smP * 32767) / 10922;
      uint32_t curvedX = (relX >> 1) + (((uint64_t)relX * relX) >> 16);
      posJitter = curvedX;
    } else if (smP < 21845) {
      // Middle Third: Harmonic Cloud (Quantized Pitch Jitter)
      int mode = (smP - 10922) * 4 / 10923;
      if (mode > 3)
        mode = 3;
      int note = SCALES[mode][fast_rand(1) % 6];
      pitchJitter = note * 5461; // 5461 = 65536 / 12 (one semitone in Q16)
      posJitter = 2000;          // Small fixed smear in harmonic mode
    } else {
      // Right Third: Reverse Probability
      int32_t prob = (smP - 21845) * 32767 / 10922;
      if (fast_rand_q15(1) < prob)
        rev = true;
      posJitter = 2000;
    }

    int32_t jit = pP + ((fast_rand_q15_sym(1) * posJitter) >> 15);
    if (jit < 0)
      jit = 0;
    if (jit > 32767)
      jit = 32767;
    uint32_t pJ = (uint32_t)(((uint64_t)jit * (activeBufferSize - 1)) >> 15);
    if (!synthMode) {
      // Live mode: Relative to write head. pJ=0 is now (newest), pJ=max is
      // oldest.
      pJ = (grainWritePos + activeBufferSize - 1 - pJ) % activeBufferSize;
    }
    int slot = -1;
    for (int i = 0; i < MAX_GRAINS; i++) {
      if (!grains_pool[i].active) {
        slot = i;
        break;
      }
    }
    if (slot < 0)
      return;

    int32_t pR = ((piP - 16384) << 3) + pitchJitter, cvP = CV1_acc >> 4;
    uint32_t mM = exp2_q16(pR + cvP);
    uint32_t phaseInc = (uint32_t)(((uint64_t)65536 * mM) >> 16);

    // Nudge grains to ensure they can play to the end in Synth/Freeze mode
    if (synthMode || freezeMode) {
      uint32_t needed = (uint32_t)(((uint64_t)gS * phaseInc) >> 16) + 2;
      if (rev) {
        if (pJ < needed)
          pJ = needed;
      } else {
        if (pJ + needed >= activeBufferSize) {
          if (activeBufferSize > needed)
            pJ = activeBufferSize - needed - 1;
          else
            pJ = 0;
        }
      }
    }

    Grain &g = grains_pool[slot];
    g.posInt = pJ;
    g.posFrac =
        fast_rand(1) &
        0xFFFF; // Start with random fractional phase to prevent comb filtering
    g.phaseInc = phaseInc;
    g.envPhase = 0;
    g.envPhaseInc =
        (65536u << 8) /
        (gS); // Use higher precision for internal calc? No, envPhase is 16-bit
    g.envPhaseInc = 65536u / gS;
    if (g.envPhaseInc == 0)
      g.envPhaseInc = 1;
    g.amp = 32767;
    g.envShape = shP;
    g.reverse = rev;
    g.envSIdx = (shP * 3) >> 15;
    g.envSFrac = (shP * 3) & 0x7FFF;

    // Normal Random Panning
    int32_t panP = params[2].pY;
    g.panR = 16384 + ((fast_rand_q15_sym(1) * panP) >> 15);

    // Generative CV2 and Pulse2 Logic
    if (smP < 16384) {
      // Smear Mode: Pure Random CV
      nextCV2 = (int16_t)(fast_rand(1) >> 20); // 0 to 4095
    } else {
      // Harmonic Mode: Quantized Scale CV (approx 1V/Oct)
      int mode = (smP - 16384) >> 12;
      if (mode > 3)
        mode = 3;
      int note = SCALES[mode][fast_rand(1) % 6];
      int octave = (fast_rand(1) % 3); // 0, 1, 2
      nextCV2 = (int16_t)(512 + (octave * 1024) +
                          (note * 85)); // 85 units per semitone
    }

    // Sub-sampled Clock on Pulse 2
    int32_t bipD = (params[0].pX + densityCV) - 16384;
    if (bipD < -400) { // Spraying
      if ((fast_rand(1) % 8) == 0)
        pulse2Trig = true;
    } else { // Rhythmic
      if ((spawnCounter % 4) == 0)
        pulse2Trig = true;
    }

    if (g.panR < 0)
      g.panR = 0;
    if (g.panR > 32767)
      g.panR = 32767;
    g.panL = 32768 - g.panR;
    g.active = true;
    activeGrainsCount++;
    spawnCounter++;
  }
  Grains() {
    initHannLUT();
    fDL.init();
    fDR.init();
    params[0] = {0, 26214, 16384};
    params[1] = {0, 0, 16384};
    params[2] = {16384, 0, 0};
    params[3] = {0, 16384, 16384};
  }
  void BackgroundLoop() override {
    if (flashAction >= 0) {
      uint32_t ints = save_and_disable_interrupts();
      core1_paused = true;
      while (!core1_is_paused) {
        if (multicore_fifo_rvalid())
          multicore_fifo_pop_blocking();
        if (multicore_fifo_wready())
          multicore_fifo_push_blocking(0);
      }
      while (multicore_fifo_rvalid())
        multicore_fifo_pop_blocking();

      if (flashAction >= 10) {
        int slot = flashAction - 10;
        if (slot < 4)
          loadBufferFromFlash(slot);
        else
          loadFlashSampleToRam(slot - 4);
      } else
        saveBufferToFlash(flashAction);

      core1_paused = false;
      restore_interrupts(ints);
      flashAction = -1;
    }
    static uint32_t lt = 0;
    if (++lt > 5000) {
      lt = 0;
      if (tapeMode && !inFlashMenu) {
        LedOn(5);
        if (SwitchVal() == Switch::Down) {
          int slot = currentTapeSample;
          for (int i = 0; i < 5; i++)
            LedOff(i);
          if (slot < 4) {
            LedOn(slot);
          } else {
            LedOn(4);
            int flashSlot = slot - 4;
            for (int i = 0; i < 4; i++)
              if ((flashSlot + 1) & (1 << i))
                LedOn(i);
          }
        } else {
          // Filling circle progress on LEDs 0, 1, 3, 2 with fading
          uint32_t bS = 0;
          if (currentTapeSample < 4) {
            bS = GRAIN_BUF_SAMPLES;
          } else {
            getFlashSamplePtr(currentTapeSample - 4, bS);
          }

          static const uint8_t circleSeq[] = {0, 1, 3, 2};
          for (int i = 0; i < 5; i++)
            LedOff(i);
          if (bS > 0) {
            float progress = (float)tapePlayhead / (float)bS;
            if (progress < 0)
              progress = 0;
            if (progress > 1)
              progress = 1;

            float fLeds = progress * 4.0f;
            int numFull = (int)fLeds;
            float frac = fLeds - (float)numFull;

            for (int i = 0; i < numFull && i < 4; i++)
              LedBrightness(circleSeq[i], 4095);
            if (numFull < 4)
              LedBrightness(circleSeq[numFull], (uint16_t)(frac * 4095.0f));

            // LED 4: Activity indicator (blinks 8 times per buffer)
            if (((int)(tapePlayhead * 8.0 / (double)bS)) & 1)
              LedOn(4);
          }
        }
      } else if (!inFlashMenu) {
        LedOn(0, currentPage == 0);
        LedOn(1, currentPage == 1);
        LedOn(2, currentPage == 2);
        LedOn(3, currentPage == 3);
        LedOn(4, tFT > 0);
        LedOn(5, freezeMode);
      } else {
        // Flash menu feedback
        static uint32_t pulse = 0;
        pulse += 10;
        bool isSave = freezeMode && !synthMode;
        if (isSave) {
          // Pulse the indicators in save mode
          uint32_t val = (pulse >> 4) & 0xFF;
          if (val > 127)
            val = 255 - val;
          if (val > 64)
            LedOn(5);
          else
            LedOff(5);
        } else {
          LedOn(5); // Load mode: Static ON
        }
      }
    }
  }
  void ProcessSample() override {
    static uint32_t uiBlinkTimer = 0;
    uiBlinkTimer++;
    static int32_t gL48 = 0, gR48 = 0;
    static int gPh = 0;

    // Poll pulse inputs at full 48kHz rate to ensure no triggers are missed
    if (PulseIn1RisingEdge())
      trigger1Buffered = true;
    if (PulseIn2RisingEdge())
      trigger2Buffered = true;

    Switch sw = SwitchVal();
    cachedSwitch = sw;
    if (bSC > 0) {
      bSC--;
      AudioOut1(0);
      AudioOut2(0);

      // Original chasing animation:
      int chaseIdx = (48000 - bSC) / 4000; // 0 to 11
      for (int i = 0; i < 6; i++)
        LedOn(i, (i == (chaseIdx % 6)) || tapeMode || synthMode);

      // Trigger detection window (between 100ms and 900ms)
      if (bSC < 43200 && bSC > 4800) {
        if (SwitchVal() == Switch::Down)
          tapeMode = true;
      }

      if (bSC == 0) {
        for (int i = 0; i < 6; i++)
          LedOff(i);
        if (tapeMode) {
          currentTapeSample = 4;
          if (currentTapeSample >= (int)(4 + numFlashSamples))
            currentTapeSample = 0;
        } else if (synthMode) {
          checkFlashSamples();
          if (numFlashSamples > 0) {
            loadFlashSampleToRam(0);
          } else
            synthMode = false;
        }
      }
      return;
    }
    auto scaleK = [](int32_t v) {
      int32_t res = (v - 50) * 32767 / (4095 - 100);
      if (res < 0)
        res = 0;
      if (res > 32767)
        res = 32767;
      return res;
    };
    CV1_acc = (CV1_acc * 127 + (CVIn1() * 160 * 16)) >> 7;
    cachedCV2 = CVIn2();
    cachedKnobMain = KnobVal(Knob::Main);
    cachedKnobX = KnobVal(Knob::X);
    cachedKnobY = KnobVal(Knob::Y);

    int32_t kM = scaleK(cachedKnobMain), kX = scaleK(cachedKnobX),
            kY = scaleK(cachedKnobY);
    sM += (kM - sM) >> 2;
    sX += (kX - sX) >> 2;
    sY += (kY - sY) >> 2;

    static Switch lSw = Switch::Middle;
    static uint32_t dbT = 0;
    Switch eSw = sw;
    if (sw != lSw) {
      if (++dbT < 500)
        eSw = lSw;
      else {
        lSw = sw;
        dbT = 0;
      }
    } else
      dbT = 0;

    if (tapeMode) {
      bool swD = (sw == Switch::Down);
      bool swU = (sw == Switch::Up);
      if (swD) {
        swDT++;
        int totalSlots = 4 + numFlashSamples;
        int slot = (KnobVal(Knob::Main) * totalSlots) >> 12;
        if (slot >= totalSlots)
          slot = totalSlots - 1;
        currentTapeSample = slot;
        swDH = true;
      } else {
        swDT = 0;
        swDH = false;
      }
      // Pulse 1: Play/Pause Gate/Toggle
      if (Connected(ComputerCard::Input::Pulse1)) {
        tapeAutoPlay = PulseIn1() && !swU;
      } else {
        if (trigger1Buffered) {
          trigger1Buffered = false;
          tapePlaying = !tapePlaying;
        }
        tapeAutoPlay = tapePlaying && !swU;
      }

      // Pulse 2: Reset Playhead
      if (trigger2Buffered) {
        trigger2Buffered = false;
        tapePlayhead = 0;
      }

      if (++gPh >= 2) {
        gPh = 0;
        multicore_fifo_push_blocking(0x80000000); // Special bit for Tape Mode
        int32_t sL = (int32_t)multicore_fifo_pop_blocking();
        int32_t sR = (int32_t)multicore_fifo_pop_blocking();
        int32_t speedCV = ((int32_t)multicore_fifo_pop_blocking()) / 2;

        // Tape Saturation (Refined Soft Clipping)
        auto saturate = [](int32_t x) {
          if (x > 12000)
            x = 12000 + ((x - 12000) >> 2);
          if (x < -12000)
            x = -12000 + ((x + 12000) >> 2);
          return x;
        };
        gL48 = saturate(sL);
        gR48 = saturate(sR);

        // CV2 Out: Movement/Speed CV (-2048 to 2047)
        if (speedCV > 2047)
          speedCV = 2047;
        if (speedCV < -2048)
          speedCV = -2048;
        CVOut2((int16_t)speedCV);
      }

      // CV1 Out: Playhead Position Ramp (-2048 to 2047)
      uint32_t bS = (currentTapeSample < 4) ? GRAIN_BUF_SAMPLES : 0;
      if (bS == 0)
        getFlashSamplePtr(currentTapeSample - 4, bS);
      if (bS > 0) {
        int16_t cv1Val = (int16_t)((tapePlayhead / (double)bS) * 4095.0 - 2048.0);
        CVOut1(cv1Val);

        // Pulse Out 1: End of Cycle (EOC) Trigger
        static double lastPh = 0;
        bool eoc = false, mid = false;
        if (tapePlayhead < lastPh && (lastPh - tapePlayhead) > (bS * 0.5))
          eoc = true;
        if (tapePlayhead > lastPh && (tapePlayhead - lastPh) > (bS * 0.5))
          eoc = true;

        // Pulse Out 2: Midpoint Trigger
        double midPoint = bS * 0.5;
        if (lastPh < midPoint && tapePlayhead >= midPoint)
          mid = true;
        if (lastPh > midPoint && tapePlayhead <= midPoint)
          mid = true;

        PulseOut1(eoc);
        PulseOut2(mid);
        lastPh = tapePlayhead;
      }

      AudioOut1(gL48 >> 4);
      AudioOut2(gR48 >> 4);
      return;
    }
    if (eSw == Switch::Down) {
      if (swDT == 0)
        swDT = 1;
      else
        swDT++;

      if (swDT > 72000) { // 1.5 seconds at 48kHz
        inFlashMenu = true;
      }

      if (inFlashMenu) {
        bool isSave = freezeMode && !synthMode;
        int totalSlots = isSave ? 4 : (4 + numFlashSamples);
        int slot = (sM * totalSlots) >> 15;
        if (slot >= totalSlots)
          slot = totalSlots - 1;
        menuSelectedSlot = slot;

        // Update LEDs continuously while in menu
        int displayVal = (slot + 1);
        for (int i = 0; i < 5; i++) {
          if (displayVal & (1 << i))
            LedOn(i);
          else
            LedOff(i);
        }

        swDH = true; // Mark as handled for normal tap/long press
      }
    } else {
      if (swDT > 500) { // Minimum 10ms to count as a press
        if (inFlashMenu) {
          bool isSave = freezeMode && !synthMode;
          int totalSlots = isSave ? 4 : (4 + numFlashSamples);
          int slot = (sM * totalSlots) >> 15;
          if (slot >= totalSlots)
            slot = totalSlots - 1;

          if (isSave) {
            flashAction = slot;
          } else {
            flashAction = slot + 10;
            synthMode = (slot >= 4);
            freezeMode = true;
          }
          inFlashMenu = false;
        } else if (!swDH) {
          if (swDT < 15000)
            currentPage = (currentPage + 1) % 4; // Tap (up to 312ms)
          else
            currentPage = (currentPage + 3) % 4; // Long press
          lM.engage(sM);
          lX.engage(sX);
          lY.engage(sY);
        }
        swDH = true; // Mark as handled
      }
      if (eSw == Switch::Middle) {
        swDT = 0;
        swDH = 0;
        inFlashMenu = false;
      }
    }
    // Triggers already polled at start of ProcessSample
    bool swU = (eSw == Switch::Up);
    if (!swEI) {
      lastSwU = swU;
      swEI = 1;
    } else if (swU && !lastSwU) {
      freezeMode = !freezeMode;
      if (!freezeMode) {
        synthMode = false;
        activeBuffer = grainBuffer;
        activeBufferSize = GRAIN_BUF_SAMPLES;
      }
    }
    lastSwU = swU;
    if (swEI && trigger2Buffered) {
      trigger2Buffered = false;
      freezeMode = !freezeMode;
      if (!freezeMode) {
        synthMode = false;
        activeBuffer = grainBuffer;
        activeBufferSize = GRAIN_BUF_SAMPLES;
      }
    }
    if (pulse2Trig) {
      p2OT = 480;
      pulse2Trig = false;
    }
    if (p2OT > 0) {
      p2OT--;
      PulseOut2(true);
    } else
      PulseOut2(freezeMode);
    if (pOT > 0) {
      pOT--;
      PulseOut1(true);
    } else
      PulseOut1(false);
    if (tFT > 0)
      tFT--;
    if (firstAR) {
      firstAR = 0;
      sM = kM;
      sX = kX;
      sY = kY;
      lM.engage(sM);
      lX.engage(sX);
      lY.engage(sY);
    }

    if (inFlashMenu) {
      lM.engage(sM);
      lX.engage(sX);
      lY.engage(sY);
    }
    if (!inFlashMenu && lM.update(sM)) {
      params[currentPage].pMain = sM;
    }
    if (lX.update(sX))
      params[currentPage].pX = sX;
    if (lY.update(sY))
      params[currentPage].pY = sY;
    int32_t rawA2 = AudioIn2();
    densityCV = rawA2 << 4; // Use Audio 2 as CV (scaled to parameter range)
    int32_t xL = AudioIn1() * 16,
            xR = xL; // Discard Audio 2 in the audio path, use Mono
    int32_t gL = ngL.process(xL), gR = ngR.process(xR), atr = (gL + gR) >> 1;
    int32_t aiL = dc_block(gL, dc_bxL, dc_byL),
            aiR = dc_block(gR, dc_bxR, dc_byR);
    if (atr > 32767)
      atr = 32767;
    if (atr < -32768)
      atr = -32768;

    if (++gPh >= 2) {
      gPh = 0;
      uint32_t trig = 1;
      if (trigger1Buffered) {
        trig |= 2;
        trigger1Buffered = false;
      }
      multicore_fifo_push_blocking(trig);
      int32_t wetDry = synthMode ? 32767 : params[2].pMain;
      int32_t oL = ((aiL * (32768 - wetDry)) >> 15) + ((gL48 * wetDry) >> 15),
              oR = ((aiR * (32768 - wetDry)) >> 15) + ((gR48 * wetDry) >> 15);
      plate.process(oL, oR, params[3].pMain, params[3].pX, params[3].pY);
      oL = dc_block(oL, dc_oxL, dc_oyL);
      oR = dc_block(oR, dc_oxR, dc_oyR);
      int32_t fL = oL >> 4, fR = oR >> 4;
      if (fL > 2047)
        fL = 2047;
      if (fL < -2048)
        fL = -2048;
      if (fR > 2047)
        fR = 2047;
      if (fR < -2048)
        fR = -2048;
      AudioOut1(fL);
      AudioOut2(fR);
      CVOut2(nextCV2);
      int32_t sL = (int32_t)multicore_fifo_pop_blocking(),
              sR = (int32_t)multicore_fifo_pop_blocking(),
              eS = (int32_t)multicore_fifo_pop_blocking();
      int32_t fA = params[2].pX, smL = fDL.process(sL), smR = fDR.process(sR);
      if (freezeMode || synthMode) {
        // Freeze: Knob X is ONLY Diffusion (Smear). NO buffer writing.
        gL48 =
            (int32_t)(((int64_t)sL * (32767 - fA) + (int64_t)smL * fA) >> 15);
        gR48 =
            (int32_t)(((int64_t)sR * (32767 - fA) + (int64_t)smR * fA) >> 15);
      } else {
        // Live mode: Knob X is ONLY Feedback.
        gL48 = sL;
        gR48 = sR;
        int32_t fM = (gL48 + gR48) >> 1;
        int32_t mix = atr + ((fM * fA) >> 15);
        // DC block the feedback to prevent drift
        mix = dc_block(mix, fb_dc_x, fb_dc_y);
        if (mix > 32767)
          mix = 32767;
        if (mix < -32768)
          mix = -32768;
        grainBuffer[grainWritePos] = (int16_t)mix;
        if (++grainWritePos >= GRAIN_BUF_SAMPLES)
          grainWritePos = 0;
      }
      static uint32_t cv1Phase = 0;
      int32_t mixKnob = params[2].pMain;
      // Calculate base phase increment for exactly 1x playback speed
      uint64_t baseInc = 4294967295ULL / activeBufferSize;
      uint32_t inc = 0;
      if (mixKnob < 16384) {
        inc = (baseInc * mixKnob) >> 14; // 0x to 1x speed
      } else {
        uint32_t extra = mixKnob - 16384;
        inc = baseInc + ((baseInc * 3 * extra) >> 14); // 1x to 4x speed
      }
      cv1Phase += inc;
      // Output a sawtooth ramp (0 to 2047) corresponding to 0V to +6V.
      // Note: CVOut1 expects -2048 to 2047; values > 2047 were previously
      // clipping to a flat line.
      CVOut1((int16_t)(cv1Phase >> 21));
    }
  }
};
void __not_in_flash_func(core1_worker)() {
  while (multicore_fifo_rvalid())
    multicore_fifo_pop_blocking();
  while (1) {
    if (core1_paused) {
      core1_is_paused = true;
      while (core1_paused) {
      }
      core1_is_paused = false;
      while (multicore_fifo_rvalid())
        multicore_fifo_pop_blocking();
    }
    uint32_t trig = multicore_fifo_pop_blocking();
    bool isTape = (trig & 0x80000000);
    Grains *gptr = (Grains *)ComputerCard::ThisPtr();
    if (!gptr)
      continue;

    // --- Secret Tape Mode Player (Retimable Granular Engine) ---
    if (isTape) {
      static uint32_t tapePhaseA = 0, tapePhaseB = (2048 << 16);
      static double spawnPosA = 0, spawnPosB = 0;
      int32_t sM = gptr->cachedKnobMain, sX = gptr->cachedKnobX,
              sY = gptr->cachedKnobY;
      sM = (sM * 32767) >> 12; // Scale 0-4095 to 0-32767
      sX = (sX * 32767) >> 12;
      sY = (sY * 32767) >> 12;

      // Deadzone for 1x playback at center (16384)
      if (sX > 16184 && sX < 16584)
        sX = 16384;
      if (sY > 16184 && sY < 16584)
        sY = 16384;

      // Pitch Mod and Speed are matched mappings: linear from 0.0x to 2.0x, center is 1.0x
      int32_t effY = sY + (gptr->CV1_acc >> 5); // Allow CV1 to modulate pitch
      if (effY < 0) effY = 0;
      if (effY > 32767) effY = 32767;
      
      // Mapping: center (16384) results in 2.0 (normal speed for 48kHz output at 24kHz rate)
      double pitchModBase = (double)effY / 8192.0;
      if (pitchModBase < 0.01)
        pitchModBase = 0.01;

      // Wow & Flutter: Subtle random fluctuations for analog character
      static int32_t wfSeed = 123;
      wfSeed = (wfSeed * 1103515245 + 12345);
      double flutter = ((double)((wfSeed >> 16) & 0xFFFF) / 65535.0 - 0.5) * 0.0005;
      
      static double smoothedSpeed = 2.0;
      double speed = ((double)sX / 8192.0) + flutter;
      smoothedSpeed += (speed - smoothedSpeed) * 0.02; // More inertia on speed changes

      // Manual position for scrubbing / nudging with heavy noise filtering
      static float sCV2 = 0;
      sCV2 += ((float)gptr->cachedCV2 - sCV2) * 0.05f;

      static double lastManualPos = -1.0;
      static double smoothedDelta = 0.0;
      double currentManualPos = (double)sM / 32768.0;
      currentManualPos += (double)sCV2 / 4096.0;
      // Wrap currentManualPos to 0..1
      while(currentManualPos >= 1.0) currentManualPos -= 1.0;
      while(currentManualPos < 0) currentManualPos += 1.0;

      if (lastManualPos < 0)
        lastManualPos = currentManualPos;
      double manualDelta = currentManualPos - lastManualPos;
      if (manualDelta > 0.5)
        manualDelta -= 1.0;
      if (manualDelta < -0.5)
        manualDelta += 1.0;

      if (gptr->cachedSwitch == ComputerCard::Switch::Down) {
        manualDelta = 0;
        lastManualPos = currentManualPos;
      } else {
        lastManualPos = currentManualPos;
      }
      
      uint32_t bS = 0;
      const int16_t *buf = nullptr;
      if (gptr->currentTapeSample < 4) {
        buf = grainBuffer;
        bS = GRAIN_BUF_SAMPLES;
      } else {
        buf = getFlashSamplePtr(gptr->currentTapeSample - 4, bS);
      }
      
      if (buf && bS > 0) {
        double delta = 0;
        if (gptr->tapeAutoPlay) {
          delta = smoothedSpeed + manualDelta * (double)bS;
        } else {
          delta = manualDelta * (double)bS;
        }
        
        smoothedDelta += (delta - smoothedDelta) * 0.01; // Heavy tape inertia for scrubbing
        delta = smoothedDelta;
        gptr->tapePlayhead += delta;
        
        while (gptr->tapePlayhead >= bS) gptr->tapePlayhead -= bS;
        while (gptr->tapePlayhead < 0) gptr->tapePlayhead += bS;

        // Support reverse playback when moving backwards
        bool reverseGrains = (delta < 0);
        uint32_t pitchInc = (uint32_t)(pitchModBase * 65536.0);

        auto renderGrain = [&](uint32_t &ph, double &spawnPos) {
          float totalPh = (float)ph * (1.0f / 65536.0f);
          float p = reverseGrains ? ((float)spawnPos - totalPh) : ((float)spawnPos + totalPh);

          float fbS = (float)bS;
          while (p < 0)
            p += fbS;
          while (p >= fbS)
            p -= fbS;

          // Safety clamp for index
          if (p < 0) p = 0;
          if (p >= fbS) p = fbS - 1.001f;

          uint32_t i0 = (uint32_t)p;
          uint32_t i1 = i0 + 1;
          if (i1 >= bS) i1 = 0;
          float frac = p - (float)i0;

          int32_t v = (int32_t)((float)buf[i0] + (float)(buf[i1] - buf[i0]) * frac);

          uint32_t envPhase = (ph >> 12) & 0xFFFF;
          int32_t env = hannEnvelope(envPhase);

          ph += pitchInc;

          if ((ph >> 16) >= 4096) {
            ph -= (4096 << 16);
            spawnPos = gptr->tapePlayhead;
          }
          return (v * env) >> 15;
        };

        // Volume envelope: speed-dependent volume for realistic tape behavior
        static float tapeVol = 1.0f;
        // Volume depends on total movement speed (delta)
        // 1.0x speed = full volume. 0 speed = silent.
        float targetVol = (float)fabs(delta) * 0.5f;
        if (targetVol > 1.0f)
          targetVol = 1.0f;
        if (targetVol < 0.01f)
          targetVol = 0.0f;
        tapeVol += (targetVol - tapeVol) * 0.1f;

        int32_t out = renderGrain(tapePhaseA, spawnPosA) + renderGrain(tapePhaseB, spawnPosB);
        out = (int32_t)((float)out * tapeVol);

        multicore_fifo_push_blocking((uint32_t)out);
        multicore_fifo_push_blocking((uint32_t)out);
        multicore_fifo_push_blocking((uint32_t)((int32_t)(smoothedDelta * 1024.0))); // Pass scaled speed for CV2 output
      } else {
        multicore_fifo_push_blocking(0);
        multicore_fifo_push_blocking(0);
        multicore_fifo_push_blocking(0);
      }
      continue;
    }

    if (gptr->inFlashMenu) {
      static uint32_t previewPhase = 0;
      static int lastSlot = -1;

      bool isSave = freezeMode && !synthMode;
      int32_t sL = 0, sR = 0;

      if (!isSave) {
        int totalSlots = 4 + numFlashSamples;
        int slot = gptr->menuSelectedSlot;

        if (slot != lastSlot) {
          previewPhase = 0;
          lastSlot = slot;
        }

        uint32_t bS = 0;
        const int16_t *buf = nullptr;
        if (slot < 4) {
          buf = grainBuffer;
          bS = GRAIN_BUF_SAMPLES;
        } else {
          buf = getFlashSamplePtr(slot - 4, bS);
        }

        if (buf && bS > 0) {
          sL = sR = buf[previewPhase >> 16];
          previewPhase += (1 << 16); // 1.0x speed playback
          if ((previewPhase >> 16) >= bS)
            previewPhase = 0;
        }
      }

      multicore_fifo_push_blocking((uint32_t)sL);
      multicore_fifo_push_blocking((uint32_t)sR);
      multicore_fifo_push_blocking(0);
      continue;
    }
    if (trig & 2)
      gptr->spawnGrain();

    int32_t rawDP = params[0].pX + densityCV;
    if (rawDP < 0)
      rawDP = 0;
    if (rawDP > 32767)
      rawDP = 32767;

    int32_t bip = rawDP - 16384;
    bool spraying = (bip < -400);
    bool rhythmic = (bip > 400);

    uint32_t dPr = 0;
    if (spraying || rhythmic) {
      int32_t amt = (bip < 0) ? -bip : bip;
      uint32_t x = ((amt - 400) * 32767) / 15983;

      // Quadratic blend for x: provides more control in the mid-range densities
      // by slowing down the curve at the start, while keeping exponential precision 
      // at the extreme high densities.
      uint32_t x_curved = (x >> 1) + (((uint64_t)x * x) >> 16);

      dPr = exp2_q16(871550 - (int32_t)x_curved * 13 - ((int32_t)x_curved * 55 >> 7)) >> 16;
    }

    int32_t smP = params[1].pX;
    int32_t jitterAmt = 2000;
    if (smP < 10922) {
      uint32_t relX = (smP * 32767) / 10922;
      jitterAmt = (relX >> 1) + (((uint64_t)relX * relX) >> 16);
    }
    if (dPr > 0) {
      if (currentDensityThreshold == 0) {
        int32_t jit = (fast_rand_q15_sym(1) * jitterAmt) >> 15;
        currentDensityThreshold = dPr + (int32_t)(((int64_t)jit * dPr) >> 15);
      }
      if (++densityCounter >= currentDensityThreshold) {
        densityCounter = 0;
        gptr->spawnGrain();
        if (spraying) {
          // Left Side: Spraying (Stochastic timing)
          currentDensityThreshold =
              dPr + ((int32_t)fast_rand_q15(1) * dPr >> 14); // 0.5 to 1.5x dPr
        } else {
          // Right Side: Rhythmic (Periodic timing + Page2-X Jitter)
          int32_t jit = (fast_rand_q15_sym(1) * jitterAmt) >> 15;
          currentDensityThreshold = dPr + (int32_t)(((int64_t)jit * dPr) >> 15);
        }
        if (currentDensityThreshold < 10)
          currentDensityThreshold = 10;
      }
    } else
      densityCounter = 0;
    int32_t sL = 0, sR = 0, eS = 0;
    uint32_t bS = activeBufferSize;
    const int16_t *buf = activeBuffer;
    for (int i = 0; i < MAX_GRAINS; i++) {
      Grain &g = grains_pool[i];
      if (!g.active)
        continue;

      // --- Optimized Branchless Envelope Math ---
      int32_t env = 0;
      uint32_t ep = g.envPhase;
      int32_t sFrac = g.envSFrac;

      int32_t decayEnv = (65535 - ep) >> 2;
      int32_t attackEnv = ep >> 2;
      int32_t att = ep << 4;
      int32_t rel = (65535 - ep) << 4;
      int32_t sqEnv = att < rel ? att : rel;
      if (sqEnv > 16384)
        sqEnv = 16384;

      if (g.envSIdx == 0) {
        int32_t hann = hannEnvelope(ep);
        env = (hann * (32768 - sFrac) + decayEnv * sFrac) >> 15;
      } else if (g.envSIdx == 1) {
        env = (decayEnv * (32768 - sFrac) + sqEnv * sFrac) >> 15;
      } else {
        env = (sqEnv * (32768 - sFrac) + attackEnv * sFrac) >> 15;
      }

      // --- 32-bit Position Processing ---
      if (synthMode || freezeMode) {
        if (g.reverse) {
          if (g.posInt == 0) {
            g.active = false;
            activeGrainsCount--;
            continue;
          }
        } else {
          if (g.posInt >= bS - 1) {
            g.active = false;
            activeGrainsCount--;
            continue;
          }
        }
      } else {
        while (g.posInt >= bS)
          g.posInt -= bS;
      }

      uint32_t p0 = g.posInt;
      uint32_t p1;
      if (g.reverse) {
        if (p0 == 0)
          p1 = (synthMode || freezeMode) ? 0 : (bS - 1);
        else
          p1 = p0 - 1;
      } else {
        p1 = p0 + 1;
        if (p1 >= bS)
          p1 = (synthMode || freezeMode) ? p0 : (p1 % bS);
      }

      int32_t f = g.posFrac;
      int32_t x0 = buf[p0], x1 = buf[p1];
      int32_t v = x0 + (int32_t)(((int64_t)(x1 - x0) * f) >> 16);

      int32_t o = (v * env) >> 15;
      sL += (o * g.panL) >> 15;
      sR += (o * g.panR) >> 15;
      eS += env;

      // --- Fast 32-bit Phase Accumulation ---
      g.posFrac += g.phaseInc;
      uint32_t shift = (g.posFrac >> 16);
      g.posFrac &= 0xFFFF;

      if (g.reverse) {
        if (shift > 0) {
          while (shift >= bS)
            shift -= bS;
          if (g.posInt < shift)
            g.posInt = g.posInt + bS - shift;
          else
            g.posInt -= shift;
        }
      } else {
        g.posInt += shift;
      }

      g.envPhase += g.envPhaseInc;
      if (g.envPhase >= 65535) {
        g.active = false;
        activeGrainsCount--;
      }
    }
    int ac = activeGrainsCount;
    static int32_t smoothComp = 32768;
    int32_t targetComp = 32768;
    if (ac > 0) {
      if (ac <= 1)
        targetComp = 52000;
      else if (ac <= 4)
        targetComp = 38000;
      else if (ac <= 12)
        targetComp = 24000;
      else if (ac <= 24)
        targetComp = 16000;
      else if (ac <= 48)
        targetComp = 11000;
      else
        targetComp = 8000;
    }
    smoothComp += (targetComp - smoothComp) >>
                  10; // Slower smoothing to prevent audio-rate gain flutter
    sL = (int32_t)(((int64_t)sL * smoothComp) >> 15);
    sR = (int32_t)(((int64_t)sR * smoothComp) >> 15);

    // Soft Saturation for Grains (Higher threshold for more headroom)
    auto sat = [](int32_t x) {
      if (x > 24000)
        x = 24000 + ((x - 24000) >> 3);
      if (x < -24000)
        x = -24000 + ((x + 24000) >> 3);
      return x;
    };
    sL = sat(sL);
    sR = sat(sR);

    // Stronger DC block for grains to prevent drift
    auto dcb_s = [](int32_t x, int32_t &px, int32_t &py) {
      int32_t y = x - px + py - (py >> 7);
      px = x;
      py = y;
      return y;
    };
    sL = dcb_s(sL, g_dc_xL, g_dc_yL);
    sR = dcb_s(sR, g_dc_xR, g_dc_yR);

    multicore_fifo_push_blocking((uint32_t)sL);
    multicore_fifo_push_blocking((uint32_t)sR);
    multicore_fifo_push_blocking((uint32_t)eS);
  }
}
int main() {
  vreg_set_voltage(VREG_VOLTAGE_1_25);
  sleep_ms(10);
  set_sys_clock_khz(240000, true);
  stdio_init_all();
  checkFlashSamples();
  initHannLUT();
  static Grains grains;
  grains.EnableNormalisationProbe();
  multicore_launch_core1(core1_worker);
  grains.Run();
}
