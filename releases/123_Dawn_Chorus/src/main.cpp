/*
  Dawn Chorus - test card v0.14
  ----------------------------
  Receives parameter frames from the host companion over USB serial
  and renders a simple sound set so that changes over time can be heard.

  Core 0: ComputerCard audio (ProcessSample at 48 kHz, integer only)
  Core 1: USB serial reading, frame parsing, slewing (float is fine here)
  The whole program runs from RAM (copy_to_ram) to avoid flash stalls in the audio interrupt.

  Outputs
    Audio 1  drone (3 phase-modulated voices) + rain crackle
    Audio 2  chimes -> wobbling feedback delay, + birds, + rain crackle
             (birds skip the delay and get a light reverb of their own)
    CV 1     daylight, 0 to +5 V (patch to a filter's FM input to open sounds with the dawn)
    CV 2     pitch of the latest chime, 1 V/oct (MIDI note - 24)
    Pulse 1  trigger on each chime
    Pulse 2  trigger on each bird phrase

  Inputs
    CV 1     adds to X (which local bird leads)
    CV 2     adds to Y (chime instrument)
    Pulse 1  strike a chime now
    Pulse 2  start a bird phrase now

  Controls
    Main     key: 12 o'clock = D, each step is a semitone (drone, chimes and CV 2 follow).
             Turning it to a new key plays a chime.
    X        birds: five positions. Fully anticlockwise, the four local birds for this place,
             season and time of day (from the host's GBIF data) share the singing. Each step
             clockwise picks one of them to lead (most likely first): it sings about 85% of
             phrases, the others quietly fill in. Turning X to a new position plays that bird.
    Y        chime instrument: marimba, harp, xylophone, bell, vibraphone, glockenspiel.
             Turning it to a new instrument plays a chime.
    Switch   Middle = normal (v0.5's sparse), Up = sparse (0.4x of normal),
             tap Down = preview full daylight for 10 s, then return

  LEDs
    0 daylight      1 link (solid = receiving, slow blink = waiting/lost)
    2 dawn chorus   3 cloud
    4 chime flash   5 bird flash

  Host frame format (one line, ASCII, newline-terminated):
    F el=-4.21 li=0.1830 ch=0.4200 ri=1 cl=0.40 wi=0.25 te=0.55 ra=0.00 hu=0.70 n=4 t0=0 s0=41234 w0=1.000 ... t3=5 s3=2210 w3=0.800
  n = how many local birds are in the cast (0-4). For each bird k (most likely first): tk = bird type (0 screamer, 1 flute,
  2 warbler, 3 trill, 4 two-note, 5 coo, 6 cuckoo, 7 harsh, 8 owl, 9 skylark), sk = personality
  seed 0-65535 (low bit = variant: blackbird/song thrush, wren/chaffinch, great tit/chiffchaff,
  wood pigeon/collared dove, crow/gull), wk = how present it is now (0 = absent).
  All values 0..1 except el (solar elevation, degrees).
  "HELLO" from the host gets a "HELLO dawn-chorus-test 0.14" reply (0.14-125mhz for the 125 MHz test build).
  The card sends "S fr=<frames> ch=<chimes> bi=<birds> li=<light 0-1000> x=<X position 0-4>
  ld=<average audio load %> pk=<peak load %> lt=<late interrupts>" twice a second.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/systick.h"
#include "ComputerCard.h"

#if SYS_CLK_HZ == 125000000
#define BUILD_TAG "0.14-125mhz"
#else
#define BUILD_TAG "0.14"
#endif

// ------------------------------------------------------------------
// Shared state (core 1 writes, core 0 reads). 32-bit writes are atomic.
// ------------------------------------------------------------------
enum { P_LI, P_CH, P_CL, P_WI, P_TE, P_RA, P_HU, P_RI, P_EL,
       P_W0, P_W1, P_W2, P_W3, NPARAM };
static const char *paramKeys[NPARAM] = {"li", "ch", "cl", "wi", "te", "ra", "hu", "ri", "el",
                                        "w0", "w1", "w2", "w3"};

static volatile int32_t g_param[NPARAM];   // Q16 (65536 = 1.0); el is degrees * 100
static volatile int32_t g_link = 0;        // 0 = never connected, 1 = receiving, 2 = lost
// The four local birds for this place, season and time of day, most likely first:
// each one's bird type (0-9) and personality seed (low bit = variant within the type)
static volatile int32_t g_birdType[4] = {0, 0, 0, 0};
static volatile int32_t g_birdSeed[4] = {0, 0, 0, 0};
static volatile int32_t g_castN = 0;        // how many of the four slots hold a bird
static volatile int32_t g_birdPick = 0;     // X position: 0 = all four, 1-4 = that bird leads
// Audio load, measured on core 0 with SysTick (CPU cycles); read and reset by core 1
static constexpr uint32_t CYCLES_PER_SAMPLE = SYS_CLK_HZ / 48000;   // 4166 at 200 MHz, 2604 at 125 MHz
static_assert(SYS_CLK_HZ == 200000000 || SYS_CLK_HZ == 125000000, "Build at 200 MHz (normal) or 125 MHz (test)");
static volatile uint32_t g_loadSum = 0, g_loadN = 0, g_loadMax = 0, g_late = 0;
static volatile uint32_t g_loadReset = 0;
static volatile bool g_usbOk = true;        // false if USB failed to start: all LEDs flash
static void PlanService();                  // core 1: prepares bird phrases (defined after the card)
static volatile uint32_t g_chimes = 0, g_birds = 0;

// ------------------------------------------------------------------
// Core 1: USB serial, parsing, slewing
// ------------------------------------------------------------------
static float tgt[NPARAM], cur[NPARAM];
// Slew time constants (seconds). Weather is smoothed more heavily than light.
static const float tau[NPARAM] = {1.0f, 1.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 0.2f, 1.0f,
                                  3.0f, 3.0f, 3.0f, 3.0f};
static uint64_t lastFrameUs = 0;
static uint32_t frameCount = 0;

static void handleLine(char *line)
{
	if (line[0] == 'F' && line[1] == ' ')
	{
		char *save = nullptr;
		for (char *tok = strtok_r(line + 2, " ", &save); tok; tok = strtok_r(nullptr, " ", &save))
		{
			char *eq = strchr(tok, '=');
			if (!eq) continue;
			*eq = 0;
			if (tok[0] == 'n' && tok[1] == 0)
			{
				long v = strtol(eq + 1, nullptr, 10);
				g_castN = v < 0 ? 0 : (v > 4 ? 4 : (int32_t)v);
				continue;
			}
			if ((tok[0] == 't' || tok[0] == 's') && tok[1] >= '0' && tok[1] <= '3' && tok[2] == 0)
			{
				long v = strtol(eq + 1, nullptr, 10);
				int k = tok[1] - '0';
				if (tok[0] == 't') g_birdType[k] = (v < 0 || v > 9) ? 0 : (int32_t)v;
				else g_birdSeed[k] = (int32_t)(v & 0xFFFF);
				continue;
			}
			for (int i = 0; i < NPARAM; i++)
			{
				if (!strcmp(tok, paramKeys[i]))
				{
					float v = strtof(eq + 1, nullptr);
					if (i == P_EL) { if (v < -90) v = -90; if (v > 90) v = 90; }
					else { if (v < 0) v = 0; if (v > 1) v = 1; }
					tgt[i] = v;
					break;
				}
			}
		}
		lastFrameUs = time_us_64();
		frameCount++;
	}
	else if (!strncmp(line, "HELLO", 5))
	{
		printf("HELLO dawn-chorus-test " BUILD_TAG "\n");
	}
}

static void core1_main()
{
	char buf[256];
	int n = 0;
	uint64_t lastSlew = time_us_64(), lastStatus = lastSlew;

	// Defaults before any host has spoken: dark, mild weather
	tgt[P_CL] = cur[P_CL] = 0.5f;
	tgt[P_WI] = cur[P_WI] = 0.2f;
	tgt[P_TE] = cur[P_TE] = 0.5f;
	tgt[P_HU] = cur[P_HU] = 0.6f;
	tgt[P_EL] = cur[P_EL] = -20.0f;

	while (true)
	{
		int c;
		while ((c = getchar_timeout_us(0)) >= 0)
		{
			if (c == '\n' || c == '\r')
			{
				if (n) { buf[n] = 0; handleLine(buf); n = 0; }
			}
			else if (n < (int)sizeof(buf) - 1)
			{
				buf[n++] = (char)c;
			}
			else
			{
				n = 0; // overlong line: drop it
			}
		}

		PlanService();

		uint64_t now = time_us_64();
		if (now - lastSlew >= 2000)
		{
			float dt = (now - lastSlew) * 1e-6f;
			lastSlew = now;
			for (int i = 0; i < NPARAM; i++)
			{
				float k = dt / tau[i];
				if (k > 1) k = 1;
				cur[i] += (tgt[i] - cur[i]) * k;
				if (i == P_EL) g_param[i] = (int32_t)(cur[i] * 100.0f);
				else g_param[i] = (int32_t)(cur[i] * 65536.0f);
			}
			// When the link drops we simply hold the last values.
			if (frameCount == 0) g_link = 0;
			else g_link = (now - lastFrameUs < 3000000) ? 1 : 2;
		}

		if (now - lastStatus >= 500000)
		{
			lastStatus = now;
			// Take the load figures and restart them every half second, connected or not,
			// so a report never includes anything from before the page connected
			uint32_t n = g_loadN, sum = g_loadSum, mx = g_loadMax, late = g_late;
			g_loadReset = 1;
			if (stdio_usb_connected())
			{
				uint32_t avg = n ? (uint32_t)(((uint64_t)sum * 100) / ((uint64_t)n * CYCLES_PER_SAMPLE)) : 0;
				uint32_t pk = (uint32_t)(((uint64_t)mx * 100) / CYCLES_PER_SAMPLE);
				printf("S fr=%lu ch=%lu bi=%lu li=%ld x=%ld ld=%lu pk=%lu lt=%lu\n",
					   (unsigned long)frameCount, (unsigned long)g_chimes,
					   (unsigned long)g_birds, (long)((g_param[P_LI] * 1000) >> 16), (long)g_birdPick,
					   (unsigned long)avg, (unsigned long)pk, (unsigned long)late);
			}
		}
	}
}

// ------------------------------------------------------------------
// Core 0: audio
// ------------------------------------------------------------------
static int16_t sineTab[4096];     // in RAM
static uint32_t noteInc[128];     // phase increment per MIDI note at 48 kHz
static constexpr int DLEN = 18000;
static int16_t delayBuf[DLEN];    // 375 ms

// Bird ambience: pre-delay, two diffusers, and a damped four-line feedback network (~1 s)
static constexpr int AM_PRE = 960;                       // 20 ms: the trees are a little way off
static constexpr int AM_AP1 = 211, AM_AP2 = 563;
static constexpr int AM_L0 = 1601, AM_L1 = 1867, AM_L2 = 2053, AM_L3 = 2251;
static int16_t amPre[AM_PRE], amAp1[AM_AP1], amAp2[AM_AP2];
static int16_t amL0[AM_L0], amL1[AM_L1], amL2[AM_L2], amL3[AM_L3];

static constexpr int TICK = 16;   // control rate = 48000 / 16 = 3 kHz
static constexpr uint32_t HZ_TO_INC = 89478;      // 2^32 / 48000 (audio rate)
static constexpr uint32_t HZ_TO_TICKINC = 1431655; // 2^32 / 3000 (control rate)

static inline int32_t __not_in_flash_func(sinLU)(uint32_t ph) { return sineTab[ph >> 20]; }

static uint32_t rngState = 0x9E3779B9u;
static inline uint32_t __not_in_flash_func(rnd)()
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

// Pick a zone from a knob, with hysteresis so it doesn't flicker at the edges
static int __not_in_flash_func(zoneSel)(int32_t v, int n, int cur)
{
	int width = 4096 / n;
	if (v > cur * width - 60 && v < (cur + 1) * width + 60) return cur;
	int z = (v * n) >> 12;
	return z >= n ? n - 1 : z;
}

// ---------- Chime instruments (Main knob, anticlockwise to clockwise) ----------
enum Engine : uint8_t { FM = 0, PD_TRI = 1, PD_SAW = 2 };
struct Preset
{
	Engine engine;
	int8_t octave;       // relative to the scale's home octave
	uint16_t ratioQ8;    // FM modulator ratio (Q8)
	int32_t idxMax;      // FM: peak index; PD: peak resonance above 1.0 (Q8)
	int32_t dec;         // amplitude decay: time ~= 21.8 / dec seconds
	int32_t tdec;        // timbre decay (index / resonance sweep)
	bool trem;           // vibraphone tremolo
	int32_t level;
};
static Preset presets[6] = {
	// engine  oct ratio  idx    dec tdec trem  level
	{PD_TRI,   -1,    0,  768,   36, 145, false, 14000}, // marimba: resonance near the 4th harmonic
	{PD_SAW,   -1,    0, 1280,   22,  73, false, 11000}, // harp: brighter, saw-windowed
	{PD_TRI,    0,    0,  512,   55, 110, false, 14000}, // xylophone: resonance near the 3rd harmonic
	{FM,        0,  896, 45000,  12,  18, false,  8000}, // bell: ratio 3.5 (the v0.1 sound)
	{FM,        0, 1024,  9000,   9,  30, true,   8000}, // vibraphone: ratio 4, gentle, tremolo
	{FM,        1,  706, 22000,  14,  50, false,  7000}, // glockenspiel: ratio ~2.76
};

struct Chime
{
	bool active, trem;
	Engine engine;
	uint32_t cph, cinc, mph, minc;   // base increments
	uint32_t cincNow, mincNow;       // after wind wobble
	int32_t env, dec, tenv, tdec, idx, idxEff, envEff; // envelopes Q15
};

// ---------- Birds ----------
// Ten types, chosen per phrase from weights the host sends (b0..b9):
enum BirdType : uint8_t { SCREAMER, FLUTE, WARBLER, TRILL, TWONOTE, COO, CUCKOO, HARSH, OWL, SKYLARK, NBIRDTYPES };

struct Bird
{
	uint8_t state;                 // 0 idle, 1 note, 2 gap
	uint8_t type;
	int8_t slot;                   // which of the four local birds (-1 = any bird)
	// sound (audio rate)
	uint32_t ph, curInc, ph2, inc2, amPh, amInc;
	int32_t env, v2Lvl, fmIdx, amDepth, noiseMix, svfF, svfLow, svfBand;
	// note (control rate)
	uint32_t inc, vibPh, vibInc;
	int32_t dinc, vibDepth, envUp, envDown, attackTicks, ticks, nextGap;
	// phrase plan: a motif played reps times, then an optional tail; or free notes
	int32_t mF0[8], mF1[8], mLen[8], mGap[8];
	uint8_t mAm[8], mN, mIdx;
	int32_t tF0[6], tF1[6], tLen[6], tGap[6];
	uint8_t tN, tIdx;
	uint32_t noteRng;              // the bird's own source for improvised notes
	int32_t reps, freeLeft, mulQ12, mulStepQ12, gapQ12, gapStepQ12;
	// phrase timbre
	int32_t peakNow, attackShift, ratio2Q8, amDepthSet, svfHz;
};

// Bird phrases are planned on core 1 (planning is too slow for the audio interrupt) and
// kept ready here: one per local bird (0-3) and one general garden bird (4). The audio
// interrupt copies a ready plan into a voice and marks it used; core 1 then plans the next.
static Bird planBuf[5];
static uint32_t planRng = 1;      // core 1's generator for plans (seeded per bird and song)
static uint32_t c1Rng = 0x2545F491u; // core 1's free generator
static volatile uint8_t planReady[5] = {0, 0, 0, 0, 0};
static volatile int32_t planType[5], planSeed[5];

class DawnTest;
static DawnTest *g_card = nullptr;

// Copy a bird a word at a time (much faster than a byte-wise memcpy in the interrupt)
static_assert(sizeof(Bird) % 4 == 0, "Bird must be whole words");
static inline void __not_in_flash_func(CopyBird)(Bird &dst, const Bird &src)
{
	const uint32_t *s = reinterpret_cast<const uint32_t *>(&src);
	uint32_t *d = reinterpret_cast<uint32_t *>(&dst);
	constexpr unsigned n = sizeof(Bird) / 4;
	unsigned i = 0;
	for (; i + 4 <= n; i += 4) { uint32_t a = s[i], b = s[i + 1], c = s[i + 2], e = s[i + 3]; d[i] = a; d[i + 1] = b; d[i + 2] = c; d[i + 3] = e; }
	for (; i < n; i++) d[i] = s[i];
}

class DawnTest : public ComputerCard
{
public:
	DawnTest()
	{
		for (int i = 0; i < 4096; i++)
			sineTab[i] = (int16_t)lrintf(32767.0f * sinf(6.2831853f * i / 4096.0f));
		for (int n = 0; n < 128; n++)
			noteInc[n] = (uint32_t)(440.0f * powf(2.0f, (n - 69) / 12.0f) * 89478.485f);
		memset(delayBuf, 0, sizeof(delayBuf));
		memset(chimes, 0, sizeof(chimes));
		memset(birds, 0, sizeof(birds));
		for (int v = 0; v < 3; v++) { dPhA[v] = rnd(); dPhB[v] = rnd(); dBase[v] = noteInc[droneNotes[v]]; }
	}

protected:
	virtual void __not_in_flash_func(ProcessSample)()
	{
		uint32_t t0 = systick_hw->cvr;
		if (g_loadReset) { g_loadSum = 0; g_loadN = 0; g_loadMax = 0; g_late = 0; g_loadReset = 0; }
		// A late start means something delayed the audio interrupt. The first second is
		// ignored: the very first "gap" is really the start-up time (launching core 1,
		// ComputerCard setting up its ADC and DMA), which v0.10-0.13 counted as late.
		uint32_t gap = (lastStart - t0) & 0xFFFFFF;
		lastStart = t0;
		if (warmup) warmup--;
		else if (gap > CYCLES_PER_SAMPLE + 400 && gap < 0x800000) g_late = g_late + 1;

		// Control work runs at 3 kHz, spread over four of every 16 samples so no single
		// sample carries all of it
		if (++tickCount >= TICK) tickCount = 0;
		switch (tickCount)
		{
		case 0: ControlBirdNotes(); break;
		case 2: if (flockIdx >= 0)
		        {
		            if (!BeginFromPlan(flockIdx, flockSlot, ms(R(100, 700)), R(3900, 4300), false)) planReady[flockIdx] = 0;
		            flockIdx = -1;
		        }
		        else if (pendingBird) { pendingBird = false; TriggerBird(); }
		        else if (pendingAudition) { pendingAudition = false; AuditionBird(); } break;
		case 4: ControlInputs(); break;
		case 6: if (pendingChime) { pendingChime = false; TriggerChime(pendingChimeLi, ctlTe); } break;
		case 8: ControlSlow(); break;
		case 10: ControlEvents(); break;
		case 12: ControlOutputs(); break;
		default: break;
		}

		// ---------- Audio 1: drone + rain ----------
		int32_t d = 0;
		for (int v = 0; v < 3; v++)
		{
			int32_t a = sinLU(dPhA[v] + (uint32_t)(sinLU(dPhA[v]) * droneIdx));
			int32_t b = sinLU(dPhB[v] + (uint32_t)(sinLU(dPhB[v]) * droneIdx));
			dPhA[v] += dIncA[v];
			dPhB[v] += dIncB[v];
			d += ((a + b) * droneLevel[v]) >> 15;
		}
		d = (d * droneAmp) >> 15;

		d += Crackle(0);

		// ---------- Audio 2: chimes + birds ----------
		int32_t e = 0, bird = 0;
		for (int i = 0; i < 4; i++)
		{
			Chime &c = chimes[i];
			if (!c.active) continue;
			int32_t s;
			if (c.engine == FM)
			{
				int32_t m = sinLU(c.mph);
				c.mph += c.mincNow;
				s = sinLU(c.cph + (uint32_t)(m * c.idxEff));
			}
			else
			{
				// Casio CZ-style resonance: a sine at k x the note frequency, restarted
				// every cycle and shaped by a window. k sweeps down as the note decays.
				uint32_t p = c.cph;
				int32_t r = sinLU((p >> 8) * (uint32_t)c.idxEff);
				int32_t w = (c.engine == PD_TRI)
					? (int32_t)(((p & 0x80000000u) ? ~p : p) >> 16)   // 0..32767 triangle
					: (int32_t)((0xFFFFFFFFu - p) >> 17);            // 32767..0 falling saw
				s = (r * w) >> 15;
			}
			c.cph += c.cincNow;
			e += (s * c.envEff) >> 15;
		}
		for (int i = 0; i < 3; i++)
		{
			Bird &b = birds[i];
			if (b.state != 1) continue;
			int32_t s;
			if (b.fmIdx) s = sinLU(b.ph + (uint32_t)(sinLU(b.ph2) * b.fmIdx));   // rasp
			else s = sinLU(b.ph);
			if (b.v2Lvl) s += (sinLU(b.ph2) * b.v2Lvl) >> 15;                     // second voice
			if (b.noiseMix)                                                       // breath / grit
			{
				int32_t n = (int32_t)(rnd() >> 17) - 16384;
				b.svfLow += (b.svfF * b.svfBand) >> 15;
				int32_t hp = n - b.svfLow - ((b.svfBand * 11000) >> 15);
				b.svfBand += (b.svfF * hp) >> 15;
				if (b.svfBand > 60000) b.svfBand = 60000;
				if (b.svfBand < -60000) b.svfBand = -60000;
				s += ((b.svfBand * b.noiseMix) >> 15) - ((s * b.noiseMix) >> 16);
			}
			if (b.amDepth)                                                        // buzz / tremolo
			{
				int32_t a = 32767 - (((32767 - sinLU(b.amPh)) * b.amDepth) >> 16);
				s = (s * a) >> 15;
				b.amPh += b.amInc;
			}
			bird += (s * b.env) >> 15;
			b.ph += b.curInc;
			b.ph2 += b.inc2;
		}


		// Feedback delay with wind-driven wobble (interpolated, tape-wow style)
		int32_t rpos = (delayPos << 8) - delayTimeQ8;
		if (rpos < 0) rpos += DLEN << 8;
		int ri = rpos >> 8, rf = rpos & 255;
		int ri2 = (ri + 1 >= DLEN) ? 0 : ri + 1;
		int32_t dl = delayBuf[ri] + (((delayBuf[ri2] - delayBuf[ri]) * rf) >> 8);
		int32_t w = e + ((dl * delayFb) >> 8);
		if (w > 32767) w = 32767;
		if (w < -32768) w = -32768;
		delayBuf[delayPos] = (int16_t)w;
		if (++delayPos >= DLEN) delayPos = 0;
		e += (dl * delayMix) >> 8;

		// DC blocker (the resonance voices are not perfectly zero-mean)
		int32_t dcy = e - dcX + ((dcY * 32604) >> 15);
		dcX = e;
		dcY = dcy;
		e = dcy + Crackle(1);
		// Birds stay dry, outside the chime delay, with a hint of their own outdoor ambience
		e += bird + ((BirdAmbience(bird) * ambMix) >> 15);

		// ---------- Output ----------
		int32_t o1 = (d * 5) >> 6;                   // drone sits a little hotter
		int32_t o2 = (e * 3) >> 6;                   // headroom for busy moments
		if (o1 > 2047) o1 = 2047;
		if (o1 < -2048) o1 = -2048;
		if (o2 > 2047) o2 = 2047;
		if (o2 < -2048) o2 = -2048;
		AudioOut1((int16_t)o1);
		AudioOut2((int16_t)o2);

		// Pulse inputs: 1 strikes a chime, 2 starts a bird phrase, immediately
		if (PulseIn1RisingEdge()) { pendingChime = true; pendingChimeLi = lastLi; }
		if (PulseIn2RisingEdge()) pendingBird = true;

		if (pulse1) { pulse1--; PulseOut1(pulse1 > 0); }
		if (pulse2) { pulse2--; PulseOut2(pulse2 > 0); }

		uint32_t dt = (t0 - systick_hw->cvr) & 0xFFFFFF;
		g_loadSum = g_loadSum + dt;
		g_loadN = g_loadN + 1;
		if (dt > g_loadMax) g_loadMax = dt;
	}

private:
	int tickCount = 0, ledCount = 0;
	uint32_t lastStart = 0;
	int32_t warmup = 48000;         // samples (1 s) before lateness is counted
	uint32_t blinkCount = 0;

	// knobs
	int presetZone = 3, rootZone = 6;
	int32_t root = 0;
	int birdZone = 0;                             // X + CV 1: 0 = all four, 1-4 = that bird leads
	int knobRoot = 6, knobChime = 3, knobBird = 0; // knob-only zones, for auditioning on a turn
	int32_t settleTicks = 3000;                   // ignore knob changes for the first second
	Switch lastSw = Middle;
	int32_t auditionTicks = 0, auditionAmt = 0;

	// drone
	uint8_t droneNotes[3] = {38, 45, 50}; // D2, A2, D3 before transposition
	uint32_t dPhA[3], dPhB[3], dIncA[3], dIncB[3], dBase[3];
	int32_t droneLevel[3] = {7000, 4500, 3000};
	int32_t droneIdx = 0, droneAmp = 0;
	uint32_t lfoPh = 0, tremPh = 0, wowPh = 0;
	int32_t chimePitchQ31 = 0;
	int32_t ambMix = 5000;   // bird reverb level (Q15): about -13 dB under the dry birds
	int32_t gust = 0, gustTarget = 0, gustTimer = 0;     // slow random 0..32767
	int32_t delayTimeQ8 = 17000 << 8;                     // samples, Q8

	// Rain: vinyl-style crackle and a faint warm hiss, one independent stream per output
	uint32_t crackleThresh = 0;
	int32_t hissAmp = 0;
	int32_t ckImp[2] = {0, 0}, ckLP[2] = {0, 0}, ckHP[2] = {0, 0}, hissLP[2] = {0, 0};
	int32_t lastLi = 0, lastTe = 0;

	// ---- Bird ambience ----
	int amPrePos = 0, amAp1Pos = 0, amAp2Pos = 0, amP0 = 0, amP1 = 0, amP2 = 0, amP3 = 0;
	int32_t amLp0 = 0, amLp1 = 0, amLp2 = 0, amLp3 = 0;

	static inline int16_t __not_in_flash_func(sat16)(int32_t v)
	{
		return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
	}

	static inline int32_t __not_in_flash_func(Allpass)(int16_t *buf, int len, int &pos, int32_t x)
	{
		int32_t z = buf[pos];
		int32_t v = x - ((z * 19661) >> 15);           // g = 0.6
		buf[pos] = sat16(v);
		if (++pos >= len) pos = 0;
		return z + ((v * 19661) >> 15);
	}

	int32_t __not_in_flash_func(BirdAmbience)(int32_t in)
	{
		int32_t x = amPre[amPrePos];
		amPre[amPrePos] = sat16(in);
		if (++amPrePos >= AM_PRE) amPrePos = 0;
		x = Allpass(amAp1, AM_AP1, amAp1Pos, x);
		x = Allpass(amAp2, AM_AP2, amAp2Pos, x);

		int32_t a = amL0[amP0], b = amL1[amP1], c = amL2[amP2], d = amL3[amP3];
		// Orthogonal 4x4 Hadamard mix
		int32_t s1 = a + b, s2 = a - b, s3 = c + d, s4 = c - d;
		int32_t h0 = (s1 + s3) >> 1, h1 = (s2 + s4) >> 1, h2 = (s1 - s3) >> 1, h3 = (s2 - s4) >> 1;
		// Damping: highs die away faster, like sound among leaves
		amLp0 += ((h0 - amLp0) * 24000) >> 15;
		amLp1 += ((h1 - amLp1) * 24000) >> 15;
		amLp2 += ((h2 - amLp2) * 24000) >> 15;
		amLp3 += ((h3 - amLp3) * 24000) >> 15;
		amL0[amP0] = sat16(((amLp0 * 28500) >> 15) + x);  // feedback ~0.87 -> ~1 s at bird pitches
		amL1[amP1] = sat16(((amLp1 * 28500) >> 15) + x);
		amL2[amP2] = sat16(((amLp2 * 28500) >> 15) + x);
		amL3[amP3] = sat16(((amLp3 * 28500) >> 15) + x);
		if (++amP0 >= AM_L0) amP0 = 0;
		if (++amP1 >= AM_L1) amP1 = 0;
		if (++amP2 >= AM_L2) amP2 = 0;
		if (++amP3 >= AM_L3) amP3 = 0;
		return (a - b + c - d) >> 1;
	}

	int32_t __not_in_flash_func(Crackle)(int o)
	{
		if (rnd() < crackleThresh)
		{
			uint32_t r = rnd();
			int32_t a = 300 + (int32_t)(r & 0x7FF);           // mostly small ticks
			if (((r >> 11) & 31) == 0) a *= 3;                // occasional pop
			a = (a * 3) >> 3;                                 // v0.4: crackle ~6 dB quieter
			ckImp[o] = (r & 0x80000000u) ? a : -a;
		}
		// short decaying impulse -> soft low-pass -> remove thump
		ckLP[o] += (ckImp[o] - ckLP[o]) >> 1;
		ckImp[o] = (ckImp[o] * 11) >> 4;
		ckHP[o] += (ckLP[o] - ckHP[o]) >> 6;
		int32_t c = ckLP[o] - ckHP[o];
		int32_t n = (int32_t)(rnd() >> 16) - 32768;
		hissLP[o] += (n - hissLP[o]) >> 3;                    // warm, tape-like
		return c + ((hissLP[o] * hissAmp) >> 15);
	}

	// events
	Chime chimes[4];
	Bird birds[3];
	int nextChime = 0;
	int32_t lastChimeNote = 62;

	// output processing
	int32_t dcX = 0, dcY = 0;
	int delayPos = 0;
	int32_t delayFb = 0, delayMix = 0;

	int pulse1 = 0, pulse2 = 0;
	int32_t chimeFlash = 0, birdFlash = 0;

	// Runs at 3 kHz inside the audio interrupt: integer only
	// ---------- control state shared between the phases ----------
	int32_t ctlLi = 0, ctlCh = 0, ctlTe = 0, ctlDensity = 410, trem = 32767;
	int downTicks = 0, auditionCooldown = 0;
	bool chimeOwed = false, birdOwed = false;   // a knob turn still waiting to be played
	bool pendingBird = false, pendingChime = false, pendingAudition = false;
	int flockIdx = -1, flockSlot = -1;
	bool pickedBackground = false;   // PickBird chose a bird other than the lead
	int32_t pendingChimeLi = 0;

	// Knob plus CV. With a cable in, the knob becomes an offset around 12 o'clock and
	// 0 to +5 V spans the whole range, so 12 o'clock plus a 0-5 V slope sweeps everything.
	int32_t __not_in_flash_func(KnobCV)(Knob k, Input in, int16_t cv)
	{
		int32_t v = KnobVal(k);
		if (Connected(in)) v = v - 2048 + ((cv * 12) / 5);
		return v < 0 ? 0 : (v > 4095 ? 4095 : v);
	}

	// Phase 10: chime envelopes, and the dice for new chimes and birds
	void __not_in_flash_func(ControlEvents)()
	{
		int32_t wi = g_param[P_WI];
		int32_t pq19 = chimePitchQ31 >> 12;              // wind pitch bend, Q19
		for (int i = 0; i < 4; i++)
		{
			Chime &c = chimes[i];
			if (!c.active) continue;
			c.cincNow = c.cinc + (((int32_t)(c.cinc >> 12) * pq19) >> 7);
			c.mincNow = c.minc + (((int32_t)(c.minc >> 12) * pq19) >> 7);
			c.env = (c.env * c.dec) >> 16;
			c.tenv = (c.tenv * c.tdec) >> 16;
			c.idxEff = (c.engine == FM) ? (c.idx * c.tenv) >> 15 : 256 + ((c.idx * c.tenv) >> 15);
			c.envEff = c.trem ? (c.env * trem) >> 15 : c.env;
			if (c.env < 4) c.active = false;
		}
		// Squared daylight keeps twilight sparse, then fills in
		int32_t li2 = ((ctlLi >> 1) * (ctlLi >> 1)) >> 14;
		int32_t chimeRate = 15 + ((li2 * (1300 + ((wi * 1400) >> 16))) >> 16);
		chimeRate = (chimeRate * ctlDensity * 4 / 5) >> 10;
		if (rnd() < (uint32_t)chimeRate * 1431u) { pendingChime = true; pendingChimeLi = ctlLi; }

		// The host's birdsong curve already shapes twilight, day and dusk, so this is linear
		int32_t birdRate = (ctlCh * 2400) >> 16;
		birdRate = (birdRate * ctlDensity * 4 / 5) >> 10;
		if (rnd() < (uint32_t)birdRate * 1431u) pendingBird = true;
	}

	// Phase 0: bird notes (timing-critical)
	void __not_in_flash_func(ControlBirdNotes)()
	{
		for (int i = 0; i < 3; i++) TickBird(birds[i]);
		if (chimeFlash > 0) chimeFlash -= 2;
		if (birdFlash > 0) birdFlash -= 2;
		if (auditionCooldown > 0) auditionCooldown--;
	}

	// Phase 4: switch, knobs and CV
	void __not_in_flash_func(ControlInputs)()
	{
		int32_t li = g_param[P_LI], ch = g_param[P_CH];

		// Switch: Middle = normal, Up = sparse, tap Down = 10 s full-daylight preview.
		// Down must hold for 20 ms, so a stray reading can't start a preview.
		Switch sw = SwitchVal();
		downTicks = (sw == Down) ? downTicks + 1 : 0;
		if (downTicks == 60) auditionTicks = 30000;   // 10 s at 3 kHz
		if (auditionTicks > 0) auditionTicks--;
		// Fade the preview in over 0.5 s and out over the last 2 s
		int32_t target = 0;
		if (auditionTicks > 6000) target = 32767;
		else if (auditionTicks > 0) target = (auditionTicks * 32767) / 6000;
		if (auditionAmt < target) { auditionAmt += 22; if (auditionAmt > target) auditionAmt = target; }
		else auditionAmt = target;
		if (auditionAmt > 0)
		{
			li += (((65536 - li) >> 1) * auditionAmt) >> 14;
			ch += (((65536 - ch) >> 1) * auditionAmt) >> 14;
		}
		ctlLi = li; ctlCh = ch; ctlTe = g_param[P_TE];
		ctlDensity = (sw == Up) ? 164 : 410;          // Up = sparse, 0.4x

		// Main = key; X + CV 1 = which bird leads; Y + CV 2 = chime instrument
		rootZone = zoneSel(KnobVal(Main), 12, rootZone);
		root = rootZone - 6;                          // noon = D
		presetZone = zoneSel(KnobCV(Y, Input::CV2, CVIn2()), 6, presetZone);
		birdZone = zoneSel(KnobCV(X, Input::CV1, CVIn1()), 5, birdZone);
		g_birdPick = birdZone;
		lastLi = li;
		lastTe = ctlTe;

		// Turning a knob to a new position plays it straight away, so changes are heard
		// at any hour. CV changes don't, so a slope doesn't add extra notes. At most one
		// audition every 0.25 s; a turn made during that time is played once it's over.
		// (v0.10-0.11 forgot the knob's new position whenever an audition fired, so a
		// single turn of Main repeated its chime four times a second, forever.)
		int kr = zoneSel(KnobVal(Main), 12, knobRoot);
		int kc = zoneSel(KnobVal(Y), 6, knobChime);
		int kb = zoneSel(KnobVal(X), 5, knobBird);
		bool chimeMoved = (kr != knobRoot || kc != knobChime), birdMoved = (kb != knobBird);
		knobRoot = kr; knobChime = kc; knobBird = kb;   // always remember where the knobs are
		if (settleTicks > 0) { settleTicks--; chimeOwed = birdOwed = false; }
		else
		{
			if (chimeMoved) chimeOwed = true;
			if (birdMoved) birdOwed = true;
			if (auditionCooldown == 0)
			{
				if (chimeOwed) { chimeOwed = false; pendingChime = true; pendingChimeLi = li > 26214 ? li : 26214; auditionCooldown = 750; }
				else if (birdOwed) { birdOwed = false; pendingAudition = true; auditionCooldown = 750; }
			}
		}
	}

	// Phase 8: wind, delay and drone
	void __not_in_flash_func(ControlSlow)()
	{
		int32_t li = ctlLi, cl = g_param[P_CL], wi = g_param[P_WI], ra = g_param[P_RA], hu = g_param[P_HU];

		// Gusts: a slow random value that wanders to a new target every 1-4 s
		if (--gustTimer <= 0) { gustTarget = rnd() & 32767; gustTimer = 3000 + (int32_t)((rnd() >> 16) * 9000 >> 16); }
		gust += (gustTarget - gust) >> 11;
		// Wind wobbles the delay time: up to ~3 ms of slow wow, deeper in gusts
		wowPh += (uint32_t)(0.25f * HZ_TO_TICKINC) + (uint32_t)((wi >> 4) * 125);   // ~0.25 to ~0.6 Hz
		int32_t wowDepth = ((wi >> 4) * (19000 + (gust >> 1))) >> 12;
		int32_t wow = sinLU(wowPh);
		delayTimeQ8 = (17000 << 8) + ((wow * wowDepth) >> 15);
		// Stronger wind also bends the chime notes themselves. Below ~14 km/h only the
		// echoes move; above that the notes join in, up to about 9 cents in strong gusts.
		int32_t windOver = wi - 22938;                      // 0.35
		if (windOver < 0) windOver = 0;
		int32_t noteDepth = (windOver * (220 + (gust >> 8)) * 25) >> 20;   // 0..~350
		chimePitchQ31 = wow * noteDepth;
		delayFb = 60 + ((hu >> 8) * 140 >> 8);        // humidity lengthens the echoes
		delayMix = 50 + ((hu >> 8) * 50 >> 8);

		// Drone. Gusts swell and sink it by up to 25% in strong wind.
		droneAmp = ((li >> 1) * (32768 - ((((wi >> 4) * (gust >> 3)) >> 12) << 1))) >> 15;
		int32_t bright = ((li >> 4) * (4096 - (((cl >> 4) * 154) >> 8))) >> 12; // 0..4096
		droneIdx = bright * 11;                       // ~0..45000
		lfoPh += 214000 + (uint32_t)wi * 15;          // ~0.15 Hz still air, ~0.8 Hz windy
		int32_t lw = (sinLU(lfoPh) * (wi >> 8)) >> 8; // vibrato, scaled by wind
		for (int v = 0; v < 3; v++)
		{
			uint32_t tgt = noteInc[droneNotes[v] + root];
			dBase[v] += (int32_t)(tgt - dBase[v]) >> 7;   // short glide on key change
			uint32_t base = dBase[v];
			int32_t vib = ((int32_t)(base >> 10) * lw) >> 12;                  // up to ~13 cents
			int32_t det = (int32_t)(base >> 12) + (int32_t)(((base >> 12) * (wi >> 8)) >> 5); // beating
			dIncA[v] = base + vib + det;
			dIncB[v] = base - vib - det;
		}
		// Rain: up to ~25 crackles a second per output, with a faint hiss underneath
		crackleThresh = (uint32_t)((ra * 25) >> 8) * 349u;
		hissAmp = (ra * 1000) >> 16;
	}

	// Phase 12: tremolo, CV outputs and LEDs
	void __not_in_flash_func(ControlOutputs)()
	{
		tremPh += 5 * HZ_TO_TICKINC;
		trem = 32767 - (((sinLU(tremPh) + 32768) * 8192) >> 16); // 75..100%
		CVOut1Millivolts((ctlLi * 5000) >> 16);
		CVOut2MIDINote((uint8_t)(lastChimeNote - 24));
		if (++ledCount >= 8)
		{
			ledCount = 0;
			blinkCount++;
			if (!g_usbOk)
			{
				// USB didn't start, so the page can never connect: make that obvious
				bool on = (blinkCount / 94) & 1;   // ~2 Hz at 375 updates a second
				for (int i = 0; i < 6; i++) LedOn(i, on);
				return;
			}
			LedBrightness(0, ctlLi >> 4);
			if (g_link == 1) LedOn(1);
			else LedOn(1, (blinkCount / 200) & 1);
			LedBrightness(2, ctlCh >> 4);
			LedBrightness(3, g_param[P_CL] >> 4);
			LedBrightness(4, chimeFlash > 0 ? chimeFlash : 0);
			LedBrightness(5, birdFlash > 0 ? birdFlash : 0);
		}
	}

	void __not_in_flash_func(TriggerChime)(int32_t li, int32_t te)
	{
		static uint8_t scale[5] = {0, 2, 4, 7, 9};   // major pentatonic
		const Preset &p = presets[presetZone];
		int span = 5 + ((li * 10) >> 16);             // brighter light reaches higher notes
		int step = rnd() % span;
		int note = 62 + root + 12 * p.octave + 12 * (step / 5) + scale[step % 5];
		if (note > 108) note = 108;
		if (note < 24) note = 24;

		Chime &c = chimes[nextChime];
		nextChime = (nextChime + 1) & 3;
		c.engine = p.engine;
		c.trem = p.trem;
		c.cinc = noteInc[note];
		c.minc = (c.cinc >> 8) * p.ratioQ8;
		c.cincNow = c.cinc;
		c.mincNow = c.minc;
		c.cph = 0;
		c.mph = 0;
		// Quieter and duller in the dark; warmth adds brightness
		int32_t lightScale = 14000 + ((li >> 4) * 18767 >> 12);   // ~0.43..1.0 (Q15)
		int32_t warmScale = 20000 + ((te >> 4) * 12767 >> 12);   // ~0.61..1.0 (Q15)
		c.env = (p.level * lightScale) >> 15;
		c.dec = 65536 - p.dec - (rnd() & 3);
		c.tenv = 32767;
		c.tdec = 65536 - p.tdec;
		c.idx = (((p.idxMax * lightScale) >> 15) * warmScale) >> 15;
		c.idxEff = (c.engine == FM) ? c.idx : 256 + c.idx;
		c.envEff = c.env;
		c.active = true;

		lastChimeNote = note;
		pulse1 = 480;                                 // 10 ms
		PulseOut1(true);
		chimeFlash = 4095;
		g_chimes = g_chimes + 1;
	}

	// ================= Bird engine =================
	static inline int32_t __not_in_flash_func(R)(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }
	static inline int32_t ms(int32_t m) { return m * 3; }   // control ticks

	static void __not_in_flash_func(Add)(Bird &b, int32_t f0, int32_t f1, int32_t len, int32_t gap, uint8_t am = 0)
	{
		if (b.mN >= 8) return;
		b.mF0[b.mN] = f0; b.mF1[b.mN] = f1; b.mLen[b.mN] = len; b.mGap[b.mN] = gap; b.mAm[b.mN] = am;
		b.mN++;
	}
	static void __not_in_flash_func(AddTail)(Bird &b, int32_t f0, int32_t f1, int32_t len, int32_t gap)
	{
		if (b.tN >= 6) return;
		b.tF0[b.tN] = f0; b.tF1[b.tN] = f1; b.tLen[b.tN] = len; b.tGap[b.tN] = gap;
		b.tN++;
	}

	// Choose who sings next. X picks: fully anticlockwise, all four local birds share the
	// singing; then each step picks one bird (most likely first) to lead, singing about
	// 85% of phrases while the others sing quietly in the background. A bird that isn't
	// awake at this hour can't lead, so the others carry on. If the chosen bird is
	// mid-song, the turn passes to a free bird (lead positions) or is skipped ("all four"):
	// a bird never sings over itself.
	bool __not_in_flash_func(Singing)(int slot)
	{
		for (int i = 0; i < 3; i++) if (birds[i].state && birds[i].slot == slot) return true;
		return false;
	}
	int __not_in_flash_func(FreeVoices)()
	{
		int f = 0;
		for (int i = 0; i < 3; i++) if (birds[i].state == 0) f++;
		return f;
	}
	bool __not_in_flash_func(Awake)(int k) { return k < g_castN && g_param[P_W0 + k] > 1311; }   // above 2%

	void __not_in_flash_func(PickBird)(int &type, uint32_t &pers, int &slot)
	{
		int n = g_castN;
		bool any = false;
		for (int k = 0; k < n; k++) if (Awake(k)) any = true;
		if (any)
		{
			int lead = birdZone - 1;
			if (lead >= n) lead = n - 1;
			int j = -1;
			// The lead takes about 85% of turns. If it's mid-song, its turn passes to
			// another bird that is awake and free, so the lead still dominates but there
			// are fewer gaps (v0.13; before, the turn was simply skipped). A background bird
			// may only start if it leaves a voice free, so the lead is never crowded out.
			bool leadTurn = lead >= 0 && Awake(lead) && (int32_t)(rnd() & 1023) < 870;
			if (leadTurn && !Singing(lead)) j = lead;
			else if (lead >= 0 && Awake(lead) && FreeVoices() < 2) { slot = -2; return; }
			else
			{
				int32_t w[4], total = 0;
				for (int k = 0; k < n; k++)
				{
					bool skip = (k == lead && lead >= 0) || !Awake(k) || (lead >= 0 && Singing(k));
					w[k] = skip ? 0 : g_param[P_W0 + k];
					total += w[k];
				}
				if (leadTurn && total <= 0) { slot = -2; return; }   // everyone else busy too
				if (total <= 0) { if (lead >= 0 && Awake(lead)) j = lead; else { slot = -2; return; } }
				else
				{
					int32_t r = (int32_t)(rnd() % (uint32_t)total);
					j = 0;
					while (j < n - 1 && r >= w[j]) { r -= w[j]; j++; }
				}
			}
			if (Singing(j)) { slot = -2; return; }
			slot = j;
			pickedBackground = (lead >= 0 && j != lead);
			type = g_birdType[j];
			pers = (uint32_t)g_birdSeed[j];
			return;
		}
		// No local birds (no data from the page, or none awake): a general garden mix
		static int32_t generalMix[NBIRDTYPES] = {0, 20, 25, 15, 20, 10, 0, 5, 0, 5};
		int32_t total = 0;
		for (int i = 0; i < NBIRDTYPES; i++) total += generalMix[i];
		int32_t r = (int32_t)(rnd() % (uint32_t)total);
		type = 0;
		while (type < NBIRDTYPES - 1 && r >= generalMix[type]) { r -= generalMix[type]; type++; }
		pers = rnd() & 0xFFFF;
		slot = -1;
	}

	// X turned to a new position: the bird it picks sings now (if awake and not mid-song)
	void __not_in_flash_func(AuditionBird)()
	{
		int lead = birdZone - 1;
		if (lead >= g_castN) lead = g_castN - 1;
		if (lead < 0 || !Awake(lead) || Singing(lead)) return;
		if (BeginFromPlan(lead, lead, 0, 4096, false))
		{
			pulse2 = 480;
			PulseOut2(true);
			birdFlash = 4095;
			g_birds = g_birds + 1;
		}
	}

	// ---- Planning (core 1 only) ----
	static inline uint32_t prnd()
	{
		planRng ^= planRng << 13; planRng ^= planRng >> 17; planRng ^= planRng << 5;
		return planRng;
	}
	static inline int32_t PR(int32_t lo, int32_t hi) { return lo + (int32_t)(prnd() % (uint32_t)(hi - lo + 1)); }
	static inline uint32_t c1rnd()
	{
		c1Rng ^= c1Rng << 13; c1Rng ^= c1Rng >> 17; c1Rng ^= c1Rng << 5;
		return c1Rng;
	}

	// Each bird has a small repertoire. Its personality seed and the song number fix the
	// plan, so the same bird sings recognisably the same songs.
	void PlanSong(Bird &b, int t, uint32_t pers, int song)
	{
		planRng = (pers * 2654435761u) ^ ((uint32_t)(song + 1) * 0x9E3779B9u) ^ 0xA5A5A5A5u;
		if (!planRng) planRng = 1;
		Plan(b, t, pers);
		b.noteRng = prnd() | 1;    // this song's improvisation is always the same
	}

public:
	// Core 1: keep a plan ready for each local bird and for one general garden bird
	static void PlanService()
	{
		if (!g_card) return;
		static int32_t generalMix[NBIRDTYPES] = {0, 20, 25, 15, 20, 10, 0, 5, 0, 5};
		for (int k = 0; k < 5; k++)
		{
			if (planReady[k]) continue;
			int t;
			uint32_t pers;
			if (k < 4)
			{
				if (k >= g_castN) continue;
				t = g_birdType[k];
				pers = (uint32_t)g_birdSeed[k];
			}
			else
			{
				int32_t r = (int32_t)(c1rnd() % 95u);
				t = 0;
				while (t < NBIRDTYPES - 1 && r >= generalMix[t]) { r -= generalMix[t]; t++; }
				pers = c1rnd() & 0xFFFF;
			}
			Bird &b = planBuf[k];
			g_card->PlanSong(b, t, pers, (int)(c1rnd() % 3));
			b.peakNow = (b.peakNow * (int32_t)(26000 + c1rnd() % 6768)) >> 15;   // a little natural variation
			b.state = 0;
			planType[k] = t;
			planSeed[k] = (int32_t)pers;
			__dmb();
			planReady[k] = 1;
		}
	}
private:

	void __not_in_flash_func(Plan)(Bird &b, int t, uint32_t pers)
	{
		int variant = pers & 1;
		int32_t pq = (int32_t)((pers >> 1) & 0x7FFF);   // the bird's own pitch, 0..32767
		b.type = t;
		b.mN = b.mIdx = b.tN = b.tIdx = 0;
		b.reps = 1; b.freeLeft = 0;
		b.mulQ12 = 3850 + ((pq * 490) >> 15);         // each bird a little different, always the same
		b.mulStepQ12 = b.gapQ12 = b.gapStepQ12 = 4096;
		b.v2Lvl = 0; b.ratio2Q8 = 0; b.fmIdx = 0; b.amDepthSet = 0; b.amInc = 0;
		b.noiseMix = 0; b.svfHz = 0; b.vibDepth = 0; b.vibInc = 0; b.attackShift = 2;
		b.svfLow = b.svfBand = 0;
		int32_t peak = 6000;
		switch (t)
		{
		case SCREAMER:   // swifts: piercing buzzy screams that dip in pitch as they pass
		{
			int n = PR(1, 3);
			for (int k = 0; k < n; k++) { int32_t f = PR(5200, 7200); Add(b, f, f - PR(400, 1000), ms(PR(250, 600)), ms(PR(120, 400)), 1); }
			b.amDepthSet = 26000; b.amInc = (uint32_t)PR(40, 70) * HZ_TO_INC;
			b.noiseMix = 9000; b.attackShift = 3; peak = 8500;
			break;
		}
		case FLUTE:      // blackbird: mellow two-voiced phrase ending in a twitter; song thrush: repeated motifs
			if (variant == 0)
			{
				int n = PR(3, 5), base = PR(1400, 2400);
				for (int k = 0; k < n; k++) { int32_t f = base + PR(-300, 500); Add(b, f, f + PR(-500, 500), ms(PR(90, 280)), ms(PR(20, 70))); }
				int m = PR(2, 5);
				for (int k = 0; k < m; k++) { int32_t f = PR(4000, 6500); AddTail(b, f, f + PR(-800, 800), ms(PR(20, 45)), ms(15)); }
			}
			else
			{
				int n = PR(1, 3);
				for (int k = 0; k < n; k++) { int32_t f = PR(1800, 3500); Add(b, f, f + PR(-600, 600), ms(PR(50, 160)), ms(PR(30, 70))); }
				b.mGap[b.mN - 1] = ms(PR(150, 300));
				b.reps = PR(2, 4);
			}
			b.v2Lvl = 9000; b.ratio2Q8 = PR(300, 400);
			b.vibDepth = 330; b.vibInc = (uint32_t)PR(5, 8) * HZ_TO_TICKINC; peak = 7500;
			break;
		case WARBLER:    // robin and warblers: thin, high, wide leaps, never quite repeating
			b.freeLeft = PR(5, 12); peak = 5500;
			break;
		case TRILL:
			if (variant == 0)   // wren: a loud, fast trill that drifts in pitch
			{
				int32_t f = PR(3500, 6000);
				Add(b, f, f + PR(-300, 300), ms(PR(25, 40)), ms(PR(15, 30)));
				b.reps = PR(15, 35); b.mulStepQ12 = 4096 + PR(-60, 60); b.attackShift = 1; peak = 7000;
			}
			else             // chaffinch: speeds up and falls, then a flourish
			{
				int32_t f = PR(4500, 5500);
				Add(b, f, f - 200, ms(45), ms(60));
				b.reps = PR(10, 16); b.mulStepQ12 = 4096 - PR(40, 80); b.gapStepQ12 = 4096 - PR(100, 200);
				AddTail(b, PR(3200, 3800), PR(5200, 6000), ms(PR(150, 250)), ms(300));
				peak = 6500;
			}
			break;
		case TWONOTE:
			if (variant == 0)   // great tit: tea-cher, tea-cher
			{
				int32_t hi = PR(5000, 6500), lo = hi * 7 / 10;
				Add(b, hi, hi - 200, ms(PR(60, 90)), ms(30));
				Add(b, lo, lo - 100, ms(PR(80, 120)), ms(PR(100, 160)));
				b.reps = PR(3, 6); peak = 6500;
			}
			else             // chiffchaff: chiff, chaff, chiff, chaff
			{
				int32_t a = PR(4200, 5200), c = PR(3500, 4300);
				Add(b, a, a - 300, ms(PR(50, 80)), ms(PR(200, 350)));
				Add(b, c, c - 300, ms(PR(50, 80)), ms(PR(200, 350)));
				b.reps = PR(3, 7); peak = 6000;
			}
			break;
		case COO:
		{
			if (variant == 0) // wood pigeon: coo-COO-coo, coo-coo
			{
				static int16_t len[5] = {300, 500, 250, 300, 250}, gap[5] = {80, 80, 350, 80, 600};
				int32_t f = PR(450, 550);
				for (int k = 0; k < 5; k++) Add(b, f, f - 30, ms(len[k]), ms(gap[k]));
				b.reps = PR(2, 4);
			}
			else             // collared dove: coo-COOO-coo
			{
				static int16_t len[3] = {250, 600, 300}, gap[3] = {100, 100, 700};
				int32_t f = PR(400, 480);
				for (int k = 0; k < 3; k++) Add(b, f, f - 25, ms(len[k]), ms(gap[k]));
				b.reps = PR(2, 5);
			}
			b.mulQ12 = 3970 + ((pq * 250) >> 15);
			b.noiseMix = 4000; b.svfHz = 700; b.attackShift = 1; peak = 9000;
			break;
		}
		case CUCKOO:     // two notes falling a minor third
		{
			int32_t hi = PR(700, 780), lo = hi * 27 / 32;
			Add(b, hi, hi - 20, ms(200), ms(120));
			Add(b, lo, lo - 20, ms(300), ms(PR(600, 900)));
			b.reps = PR(3, 8); b.mulQ12 = 4096;
			b.noiseMix = 2500; b.svfHz = 800; peak = 7500;
			break;
		}
		case HARSH:
			if (variant == 0) // crow: rough caws
			{
				int n = PR(1, 4);
				for (int k = 0; k < n; k++) { int32_t f = PR(600, 900); Add(b, f, f - 150, ms(PR(250, 400)), ms(PR(300, 600)), 1); }
				b.fmIdx = 40000; b.ratio2Q8 = 362; b.noiseMix = 14000; b.svfHz = 1200;
				b.amDepthSet = 12000; b.amInc = (uint32_t)PR(60, 90) * HZ_TO_INC; b.attackShift = 3; peak = 5000;
			}
			else             // gull: falling kyow calls
			{
				int n = PR(3, 6);
				for (int k = 0; k < n; k++) Add(b, PR(1100, 1300), PR(650, 800), ms(PR(200, 350)), ms(PR(100, 160)), 1);
				b.fmIdx = 15000; b.ratio2Q8 = 362; b.noiseMix = 6000; b.svfHz = 1500;
				b.amDepthSet = 6000; b.amInc = 50 * HZ_TO_INC; peak = 5500;
			}
			break;
		case OWL:
			if (PR(0, 9) < 6) // tawny owl: hoo... hu, hu-hoooo
			{
				Add(b, 420, 400, ms(600), ms(PR(1200, 2000)));
				Add(b, 400, 400, ms(150), ms(200));
				Add(b, 410, 380, ms(900), ms(1500), 1);
				b.mulQ12 = 3970 + ((pq * 250) >> 15);
				b.amDepthSet = 18000; b.amInc = 12 * HZ_TO_INC;
				b.noiseMix = 3000; b.svfHz = 450; b.attackShift = 1; peak = 9000;
			}
			else             // ke-wick
			{
				int n = PR(1, 3);
				for (int k = 0; k < n; k++) Add(b, PR(1700, 1900), PR(2400, 2700), ms(PR(150, 220)), ms(PR(700, 1200)));
				b.noiseMix = 8000; b.svfHz = 2200; peak = 6000;
			}
			break;
		case SKYLARK:    // a long, high, endlessly varied warble from above
			b.freeLeft = PR(25, 60);
			b.vibDepth = 1000; b.vibInc = 25 * HZ_TO_TICKINC; peak = 4500;
			break;
		}
		b.peakNow = (peak * PR(22000, 32767)) >> 15;
	}

	// Next note of the phrase; false when the phrase is over
	bool __not_in_flash_func(NextNote)(Bird &b, int32_t &f0, int32_t &f1, int32_t &len, int32_t &gap, uint8_t &am)
	{
		am = 0;
		if (b.freeLeft > 0)
		{
			b.freeLeft--;
			uint32_t saved = rngState;
			rngState = b.noteRng;
			if (b.type == SKYLARK) { f0 = R(2500, 6000); f1 = f0 + R(-1000, 1000); len = ms(R(30, 120)); gap = ms(R(5, 30)); }
			else { f0 = R(3000, 7500); f1 = f0 + R(-1500, 1500); len = ms(R(40, 200)); gap = ms(R(10, 60)); }
			b.noteRng = rngState;
			rngState = saved;
			if (f1 < 1500) f1 = 1500;
			f0 = (f0 * b.mulQ12) >> 12; f1 = (f1 * b.mulQ12) >> 12;
			return true;
		}
		if (b.mIdx >= b.mN && b.mN > 0 && --b.reps > 0)
		{
			b.mIdx = 0;
			b.mulQ12 = (b.mulQ12 * b.mulStepQ12) >> 12;
			b.gapQ12 = (b.gapQ12 * b.gapStepQ12) >> 12;
		}
		if (b.mIdx < b.mN)
		{
			int k = b.mIdx++;
			f0 = (b.mF0[k] * b.mulQ12) >> 12; f1 = (b.mF1[k] * b.mulQ12) >> 12;
			len = b.mLen[k]; gap = (b.mGap[k] * b.gapQ12) >> 12; am = b.mAm[k];
			return true;
		}
		if (b.tIdx < b.tN)
		{
			int k = b.tIdx++;
			f0 = b.tF0[k]; f1 = b.tF1[k]; len = b.tLen[k]; gap = b.tGap[k];
			return true;
		}
		return false;
	}

	void __not_in_flash_func(StartNote)(Bird &b)
	{
		int32_t f0, f1, len, gap;
		uint8_t am;
		if (!NextNote(b, f0, f1, len, gap, am)) { b.state = 0; b.env = 0; return; }
		if (len < 3) len = 3;
		b.inc = (uint32_t)f0 * HZ_TO_INC;
		b.curInc = b.inc;
		b.dinc = ((f1 - f0) * (int32_t)HZ_TO_INC) / len;
		b.inc2 = b.ratio2Q8 ? (b.inc >> 8) * (uint32_t)b.ratio2Q8 : 0;
		b.amDepth = am ? b.amDepthSet : 0;
		int32_t fc = b.svfHz ? b.svfHz : f0;
		b.svfF = (fc * 4289) >> 10;                  // ~2*pi*fc/48000, Q15
		if (b.svfF > 28000) b.svfF = 28000;
		if (b.svfF < 200) b.svfF = 200;
		b.attackTicks = len >> b.attackShift;
		b.envUp = b.peakNow / (b.attackTicks + 1);
		b.envDown = b.peakNow / (len - b.attackTicks + 1);
		b.env = 0;
		b.ticks = len;
		b.nextGap = gap;
		b.state = 1;
	}

	// Copy a ready plan into a free voice. A plan made for a bird that has since left the
	// cast is thrown away (core 1 plans afresh). keep = leave the plan for a second voice.
	bool __not_in_flash_func(BeginFromPlan)(int idx, int slot, int32_t delayTicks, int32_t pitchQ12, bool keep)
	{
		if (!planReady[idx]) return false;
		__dmb();
		if (slot >= 0 && (planType[idx] != g_birdType[slot] || planSeed[idx] != g_birdSeed[slot]))
		{
			planReady[idx] = 0;
			return false;
		}
		for (int i = 0; i < 3; i++)
		{
			Bird &b = birds[i];
			if (b.state != 0) continue;
			CopyBird(b, planBuf[idx]);
			if (!keep) { __dmb(); planReady[idx] = 0; }
			b.slot = (int8_t)slot;
			if (pitchQ12 != 4096) b.mulQ12 = (b.mulQ12 * pitchQ12) >> 12;
			if (pickedBackground)
			{
				// While another bird leads, the rest sing short phrases: a call, not a full song
				if (b.type == TRILL) { if (b.reps > 12) b.reps = (b.reps + 1) >> 1; }
				else if (b.reps > 1) b.reps = (b.reps + 1) >> 1;
				if (b.freeLeft > 4) b.freeLeft = (b.freeLeft + 1) >> 1;
				b.tN = b.tN > 1 ? 1 : b.tN;
			}
			// The first note starts on the next bird-note tick (a third of a millisecond
			// later), so copying the plan and starting the note never share a sample
			b.state = 2;
			b.ticks = delayTicks > 0 ? delayTicks : 1;
			b.env = 0;
			return true;
		}
		return false;
	}

	void __not_in_flash_func(TriggerBird)()
	{
		int t = WARBLER, slot = -1;
		uint32_t pers = 0;
		pickedBackground = false;
		PickBird(t, pers, slot);
		if (slot == -2) return;
		int idx = slot >= 0 ? slot : 4;
		bool flock = planReady[idx] && planType[idx] == SCREAMER && (rnd() & 1);   // swifts fly in groups
		if (!BeginFromPlan(idx, slot, 0, 4096, flock)) return;
		if (flock) { flockIdx = idx; flockSlot = slot; }   // the second swift starts on a later sample
		pulse2 = 480;
		PulseOut2(true);
		birdFlash = 4095;
		g_birds = g_birds + 1;
	}

	void __not_in_flash_func(TickBird)(Bird &b)
	{
		if (b.state == 0) return;
		if (b.state == 1)
		{
			b.inc += b.dinc;
			uint32_t ci = b.inc;
			if (b.vibDepth)
			{
				b.vibPh += b.vibInc;
				ci += (((sinLU(b.vibPh) * b.vibDepth) >> 12) * (int32_t)(b.inc >> 12)) >> 6;
			}
			b.curInc = ci;
			if (b.ratio2Q8) b.inc2 = (ci >> 8) * (uint32_t)b.ratio2Q8;
			if (b.attackTicks > 0) { b.env += b.envUp; b.attackTicks--; }
			else { b.env -= b.envDown; if (b.env < 0) b.env = 0; }
			if (--b.ticks <= 0)
			{
				b.env = 0;
				b.state = 2;
				b.ticks = b.nextGap > 0 ? b.nextGap : 1;
			}
		}
		else if (--b.ticks <= 0)
		{
			StartNote(b);
		}
	}
};

static void PlanService() { DawnTest::PlanService(); }

int main()
{
	// USB serial (opened by the host page at 115200; never use 1200). It must start on
	// core 0: the Pico SDK refuses to start USB on any core but the one that owns its
	// default timer pool (core 0), and fails silently if you try. v0.10 tried core 1.
	g_usbOk = stdio_init_all();
	static DawnTest card;
	g_card = &card;
	card.EnableNormalisationProbe(); // unpatched CV inputs read as exactly zero
	// Nothing else on this core may delay the audio interrupt: a late interrupt can make
	// ComputerCard's ADC readings slip channels, scrambling the knobs, switch and CV.
	// USB's interrupts also run on this core, at the default priority (0x80), so the
	// audio interrupt at 0 (highest) pre-empts them.
	irq_set_priority(DMA_IRQ_0, 0);
	// SysTick counts CPU cycles on this core, for the load report
	systick_hw->rvr = 0xFFFFFF;
	systick_hw->cvr = 0;
	systick_hw->csr = 0x5;
	multicore_launch_core1(core1_main);   // frames, smoothing and bird planning
	card.Run();
}
