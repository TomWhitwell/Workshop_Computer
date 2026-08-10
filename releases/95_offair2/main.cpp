// offair — shortwave radio simulator, v1.0.0 (behavioural tuning model)
//
// Tune between two Stations the way you tune a shortwave radio. Instead of a literal
// AM encode/decode round-trip (which can't give clean selectivity in cheap integer
// DSP), we synthesise the audible RESULT of detuning each Station directly from
// detune = (tunePos - stationPos):
//   - a heterodyne whistle whose pitch slides with |detune| (the signature SW sound)
//   - the audio single-sideband FREQUENCY-SHIFTED by detune (harmonics break →
//     the "wrong pitch"/metallic sound of a near-tuned SW station). This is the
//     genuine product-detector behaviour: a detuned LO shifts the recovered audio.
//   - the whole Station attenuated as |detune| grows, fading at the band edge.
//   - at detune = 0: clean, correctly-pitched audio; whistle vanishes (it's at DC).
// All frequencies stay in the audio band → no aliasing, no instability.
//
// Two Stations + three interference clips (morse/data) are scattered across the dial,
// re-randomised on each band tap. Sometimes a Station and an interferer overlap —
// like a crowded SW band. A separate "Insta-ference" bank of short one-shot events
// can be fired in via Pulse In 2.
//
// BAKED-IN AUDIO MODE (hold Switch Down at power-on until all LEDs flash):
//   Live Audio In 1/2 (Station 1/2) replaced by baked recordings.
//
//   Main Knob       : Tuning
//   Knob X (Bright) : IF bandwidth — capture width + audio brightness
//                     (CCW narrow/muffled/selective, CW wide/bright/easy)
//   Knob Y (Noise)  : Noise / static level (slow random swell + swish)
//   CV In 1 (Tuner) : Tuning offset (1:1, added to Main knob — ±5V = ±half dial)
//   CV In 2 (Noise) : Noise level (adds to Knob Y — voltage-controlled static)
//   Pulse In 1      : Shuffle Signals — rising edge re-randomises the Station /
//                     interference layout. In audio-input mode + Switch Up it is the
//                     Morse In key instead (see below).
//   Pulse In 2      : Insta-ference — rising edge fires a one-shot from the curated
//                     bank (played once-through; falls back to loop bank if empty)
//   Audio Out 1     : Output — full mix (tuned audio + whistles + noise + Insta-ference)
//   Audio Out 2     : Just Noise — static only
//   CV Out 1        : Signal Strength (envelope — rises as you tune in)
//   CV Out 2        : Station 1 CV Offset (slew → CV In 1 tunes onto Station 1)
//   Pulse Out 1     : Station 1 Tuned Gate — HIGH while tuned to Station 1
//   Pulse Out 2     : Station 2 Tuned Gate — HIGH while tuned to Station 2
//   Switch Down tap : Cycle band AM → SW → LW (re-randomises dial layout)
//                     AM = correct-pitch audio (envelope detect); SW/LW = directional
//                     pitch-shift (SSB). Both add a heterodyne whistle off-tune.
//   Switch Up hold  : BAKED-IN  → mute Station 1 & 2 (interference + CV/pulse stay)
//                     AUDIO-IN   → Station 2 becomes a ~600Hz morse tone keyed by
//                                  Pulse In 1 (PU1 stops shuffling while held)
//   LED 0/1         : Station 1/2 signal strength
//   LED 2/3         : Band — both off = AM, LED2 = SW, LED3 = LW
//   LED 5           : Tuning position
//
//   5 stations per band: 2 broadcast + 3 interference.

#include "ComputerCard.h"
#include "clips.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "pico/multicore.h"

// ---------------------------------------------------------------------------
// PWM globals — written by ProcessSample(), read by ISR (RF out, not a focus)
// ---------------------------------------------------------------------------

volatile int32_t amValue = 0, fqValue = 1024000;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int32_t kNumStations = 5;   // 0/1 broadcast, 2/3/4 interference
static constexpr int32_t kNumClips    = 3;   // continuous interference streams

// One-shot interference burst (Pulse In 2): random clip from kAllClips, ~1.5s,
// ducked under the current audio, retrigger restarts.
static constexpr int32_t kOsFadeSamples = 400;   // fade-out over last ~50ms (clip samples @8k)
static constexpr int32_t kOsDuckShift   = 3;     // ducked under broadcast (>>3 ≈ −18dB)

// Pulse-out gate threshold: tuned-to-station when strength exceeds this.
static constexpr int32_t kOnThresh   = 2000;

// Morse tone (Normal boot + Switch Up): Broadcast 2's audio becomes a ~600Hz CW
// sidetone keyed by the Pulse In 1 input — a rhythmic signal that's also musical.
static constexpr int32_t kMorseInc   = 53687091; // 600Hz: 600 * 2^32 / 48000
static constexpr int32_t kMorseLevel = 900;      // tone peak (~75% of broadcast level)
static constexpr int32_t kMorseRamp  = 6;        // key attack/release smoothing (no click)

// detune → pitch: whistle/shift = |detune| * kWhistleK Hz.
// kWhistleK=8 gives a gentle slide: you can detune further before the pitch sounds
// wrong (wider artefact-free spot), and the whistle reaches ~2kHz at the band edge.
// Per-sample phase increment for one dial-count of detune = kWhistleK * 2^32/48000.
static constexpr int32_t kWhistleK_phaseInc = 715828;  // = 8 * 2^32/48000

// Capture width (kBandwidth) is now set live by Knob X (IF bandwidth). These bound it.
static constexpr int32_t kBwMin       = 90;   // narrowest capture (CCW): selective, fiddly
static constexpr int32_t kBwMax       = 420;  // widest capture (CW): broad, easy to find
static constexpr int32_t kBwInnerFrac = 40;   // flat (clean) zone = bandwidth * frac/100
// Knob X also sweeps audio brightness (the clearly-audible part of "IF bandwidth").
// Tone LPF alpha (Q12): CCW ~600Hz (muffled/boxy), CW ~5kHz (bright/open).
// alpha = round(4096*(1-exp(-2*pi*fc/48000))): 600Hz=309, 5kHz=1967
static constexpr int32_t kToneMin     = 309;  // narrow IF → dull
static constexpr int32_t kToneMax     = 1967; // wide IF → bright

// Whistle amplitude (no longer ducked by audio strength — it has its own envelope).
static constexpr int32_t kWhistleA    = 70;   // SW/LW heterodyne (quiet)
static constexpr int32_t kWhistleA_AM = 200;  // AM whistle (no pitch-shift competing)
// Whistle gate: silent within kWhistleLo counts of tune (no low rumble — clean tune-
// through), ramps in to full by kWhistleFull once the beat is a high-pitched whistle.
static constexpr int32_t kWhistleLo   = 50;   // ~400Hz beat: below this, no whistle
static constexpr int32_t kWhistleFull = 110;  // ~880Hz beat: full whistle
static constexpr int32_t kStDcShift   = 5;    // per-station HP ~239Hz (small-speaker roll-off:
                                              // thins the low pitch-slide on SW/LW tune-in)
static constexpr int32_t kDetuneShift = 4;    // detune smoother (~16 samples, anti-zipper)
// Audio gain into the shifter (×scale >>11). Broadcast louder; interference subtler.
static constexpr int32_t kBcastScale = 2400;  // broadcast stations (lpfState ±2047)
static constexpr int32_t kIntfScale  = 1000;  // interference clips (stepIntf ±1020)

// Per-band demod: 0=AM (envelope, correct pitch), 1=SW (pitch-shift), 2=LW (pitch-shift)
// band order is AM, SW, LW (matches kLpfAlpha / kNoiseLpf indexing)
static constexpr bool kBandPitchShift[3] = { false, true, true };

// SSB (pitch-shift) audio window: the shifted audio fades out within this much detune,
// well inside the full capture width. Keeps the intelligible/audible audio near the
// centre so you don't hear it slide all the way down through the bass; the whistle
// still extends to the full band edge. (Counts; ~ shift = counts*8 Hz.)
static constexpr int32_t kSsbAudioHalf = 90;   // audio audible over a wider approach window
                                               // (broadcast heard sooner; still fades before deep bass)

// IIR Hilbert quadrature pair (Q14 a2 coeffs). Two 4-section all-pass chains
// (in z^-2); path B has a +1 sample delay. Their outputs are ~90° apart across the
// audio band, giving I/Q for the single-sideband frequency shift.
static constexpr int32_t kHilbA[4] = { 11344, 15336, 16191, 16364 };
static constexpr int32_t kHilbB[4] = {  6590, 14027, 15930, 16307 };

// Drift (broadcast stations) — depth now set by CV In 2 (was Knob X). Q8 units.
static constexpr int32_t  kMaxDrift_q8   = 256;        // applied as >>2 → ±64 counts
static constexpr uint32_t kDriftProbMask = 0xFE000000u;

// Noise swell/swish — slow random walks modulating noise level and filter cutoff.
// Per band: {level-walk step, cutoff-walk range}. Probability mask sets walk speed.
static constexpr uint32_t kNoiseWalkMask  = 0xFFE00000u;  // ~1/2048 chance/sample (slow)
static constexpr int32_t  kSwellDepth[3]  = { 1000, 1800, 3000 }; // AM steady, LW slow heavy fade
static constexpr int32_t  kSwishRange[3]  = {  120,  700,   12 }; // SW swishes a lot, LW barely
// Noise ducking when tuned to a station (Q12 = how much of full strength removes noise).
// AM ducks fully; SW/LW now duck strongly too so the broadcast is clearly heard.
static constexpr int32_t  kNoiseDuck[3]   = { 4096, 3600, 3400 }; // AM full / SW,LW strong

// Pre-input band LPF (Q12) — gentle band-limiting / tone shaping of the source.
// alpha = round(4096*(1-exp(-2*pi*fc/48000))): 3.5kHz=1505, 2.5kHz=1143, 1.8kHz=860
static constexpr int32_t kLpfAlpha[3] = { 1505, 1505, 860 };  // AM/SW bright, LW darker

// Noise LPF character per band (AM mellow / SW bright crackle / LW deep rumble).
static constexpr int32_t kNoiseLpf[3] = { 200, 900, 28 };  // AM mid / SW bright / LW deep
// Gain compensation (Q8): low-pass filtering white noise loses energy, so heavily
// filtered bands (low alpha) come out far quieter. These restore similar loudness
// across bands. ~ 256 / lpf_gain(alpha): AM x6.3, SW x3.7, LW x12.2.
static constexpr int32_t kNoiseGain[3] = { 1613, 752, 4400 };  // Q8 — matches new kNoiseLpf

// Clip playback sample-rate fractions
static constexpr int32_t kClipNum  = 8000;
static constexpr int32_t kClipDen  = 48000;
static constexpr int32_t kBcastNum = 13340;
static constexpr int32_t kBcastDen = 48000;

// ---------------------------------------------------------------------------
// Sine table — 256-entry full cycle, int8 -127..127
// ---------------------------------------------------------------------------

static const int8_t kSinTable[256] = {
       0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
      49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
      90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
     117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
     127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
     117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
      90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
      49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
       0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
     -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
     -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
    -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
    -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
    -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
     -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
     -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3,
};

static inline int32_t __not_in_flash_func(isin)(uint32_t phase)
{
    return kSinTable[phase >> 24];
}

static inline int32_t __not_in_flash_func(icos)(uint32_t phase)
{
    return kSinTable[(phase + 0x40000000u) >> 24];  // +64 entries = +90 degrees
}

static inline int32_t __not_in_flash_func(softClip)(int32_t s, int32_t threshold)
{
    if (s >  threshold) return  threshold + ((s - threshold) >> 2);
    if (s < -threshold) return -threshold + ((s + threshold) >> 2);
    return s;
}

// ---------------------------------------------------------------------------
// OffAir
// ---------------------------------------------------------------------------

class OffAir : public ComputerCard
{
    // --- Startup ---
    bool    mode_locked = false;
    int32_t mode_settle = 4800;
    bool    altbootMode = false;
    int32_t fadeGain    = 0;

    // --- Band: 0=AM, 1=SW, 2=LW ---
    int32_t band = 0;

    // --- Switch tap ---
    int32_t switchTimer = 0;
    bool    downArmed   = true;

    // --- Morse tone (Normal boot + Switch Up): B2 = PU1-keyed CW tone ---
    uint32_t morsePhase = 0;
    int32_t  morseKey   = 0;   // Q12 key envelope, smoothed from PU1

    // --- Knob smoothers ---
    int32_t smMain = 2048;
    int32_t smX    = 2048;
    int32_t smY    = 0;

    // --- Tuning ---
    int32_t tuneLocked = 2048;

    // --- Per-input DC block + band LPF (sources 0/1 = broadcast/live) ---
    int32_t hpState1 = 0, hpState2 = 0;
    int32_t lpfState1 = 0, lpfState2 = 0;

    // --- 5 station dial positions (0..4095) ---
    // [0/1] = broadcast, [2/3/4] = interference. Randomised by randomiseDial().
    int32_t  dialPos[5]    = { 600, 2000, 1200, 2800, 3500 };
    uint32_t shiftPhase[5] = {};   // detune oscillator (whistle + SSB shift)
    int32_t  detuneSm[5]   = {};   // smoothed detune
    int32_t  stDc[5]       = {};   // per-station output DC block
    int32_t  stStrength[5] = {};   // signal strength for LED/CV/pulse

    // --- Hilbert all-pass state: [station][section], two paths A/B ---
    int32_t hilAx1[5][4]={}, hilAx2[5][4]={}, hilAy1[5][4]={}, hilAy2[5][4]={};
    int32_t hilBx1[5][4]={}, hilBx2[5][4]={}, hilBy1[5][4]={}, hilBy2[5][4]={};
    int32_t hilBdelay[5]={};  // path-B 1-sample delay (gives quadrature)

    // --- Clip assignment shuffle (3 distinct from 6-clip bank per band) ---
    uint8_t  clipOrder[3] = { 0, 1, 2 };

    bool    pu1Prev = false;

    // --- One-shot (Pulse In 2): plays a curated clip once-through ---
    bool     pu2Prev   = false;
    int32_t  osClip    = -1;       // -1 = idle, else index into the one-shot bank
    uint32_t osPos     = 0;
    int32_t  osFrac    = 0;
    int32_t  osSample  = 0;

    // --- Altboot flash ---
    int32_t altbootFlash = 24000;

    // --- Noise + slow swell/swish random walks ---
    int32_t bpY1 = 0;
    int32_t swellLevel  = 4096, swellTarget  = 4096;  // Q12 level multiplier walk
    int32_t swishCut    = 0,    swishTarget  = 0;      // cutoff offset walk

    // --- Knob X audio-brightness (IF bandwidth) tone LPF state ---
    int32_t toneState = 0;

    // --- Clip playback ---
    uint32_t intfPos[3]   = {};
    int32_t  intfFrac[3]  = {};
    int32_t  intfSamp[3]  = {};
    uint32_t bcastPos[2]    = {};
    int32_t  bcastFrac[2]   = {};
    int32_t  bcastSample[2] = {};

    // --- RNG ---
    uint32_t rng = 1;

    // -----------------------------------------------------------------------

    inline uint32_t __not_in_flash_func(rng_next)()
    {
        rng = 1664525u * rng + 1013904223u;
        return rng;
    }

    int32_t __not_in_flash_func(stepBcast)(int ci)
    {
        bcastFrac[ci] += kBcastNum;
        if (bcastFrac[ci] >= kBcastDen) {
            bcastFrac[ci] -= kBcastDen;
            if (++bcastPos[ci] >= kBcastClips[ci].len) bcastPos[ci] = 0;
            uint32_t p = bcastPos[ci];
            const uint8_t* d = kBcastClips[ci].data;
            int32_t s;
            if ((p & 1) == 0) {
                uint32_t b = (p >> 1) * 3;
                s = ((int32_t)d[b] << 4) | (d[b+1] >> 4);
            } else {
                uint32_t b = (p >> 1) * 3;
                s = (((int32_t)d[b+1] & 0xF) << 8) | d[b+2];
            }
            if (s >= 2048) s -= 4096;
            bcastSample[ci] = s;
        }
        return bcastSample[ci];
    }

    int32_t __not_in_flash_func(stepIntf)(int ci)
    {
        const ClipDesc& c = kAllClips[clipOrder[ci]];
        intfFrac[ci] += kClipNum;
        if (intfFrac[ci] >= kClipDen) {
            intfFrac[ci] -= kClipDen;
            if (++intfPos[ci] >= c.len) intfPos[ci] = 0;
            intfSamp[ci] = ((int32_t)c.data[intfPos[ci]] - 128) << 3;  // ±1020
        }
        return intfSamp[ci];
    }

    // One-shot reader: plays the curated clip ONCE through at 8kHz, then stops.
    // (Loop bank used as fallback when the one-shot bank is empty.)
    // A short fade-out over the last samples avoids a click at the clip end.
    int32_t __not_in_flash_func(stepOneShot)()
    {
        if (osClip < 0) return 0;
        const ClipDesc& c = (kNumOneShots > 0) ? kOneShotClips[osClip] : kAllClips[osClip];
        osFrac += kClipNum;
        if (osFrac >= kClipDen) {
            osFrac -= kClipDen;
            if (++osPos >= c.len) { osClip = -1; return 0; }   // played once → stop
            osSample = ((int32_t)c.data[osPos] - 128) << 3;    // ±1020
        }
        // Linear fade-out over the last kOsFadeSamples of the clip.
        int32_t remaining = (int32_t)c.len - (int32_t)osPos;
        int32_t fade = 4096;
        if (remaining < kOsFadeSamples) fade = (remaining * 4096) / kOsFadeSamples;
        return ((osSample * fade) >> 12) >> kOsDuckShift;
    }

    // One 4-section all-pass chain (in z^-2). y = (a2*(x+y2)>>14) - x2 per section.
    int32_t __not_in_flash_func(hilbertChain)(
        const int32_t* coeff, int32_t x,
        int32_t* x1, int32_t* x2, int32_t* y1, int32_t* y2)
    {
        for (int k = 0; k < 4; k++) {
            int32_t a2 = coeff[k];
            int32_t y  = ((a2 * (x + y2[k])) >> 14) - x2[k];
            x2[k] = x1[k]; x1[k] = x;
            y2[k] = y1[k]; y1[k] = y;
            x = y;
        }
        return x;
    }

    // Assign random dial positions + clip selection. Called at startup + each band tap.
    void randomiseDial()
    {
        // One station per equal slot, jittered, so spacing is guaranteed.
        const int32_t lo = 300, hi = 3795;
        int32_t span  = hi - lo;
        int32_t slotW = span / kNumStations;          // ~699
        int32_t jit   = slotW - 200; if (jit < 1) jit = 1;
        int32_t pos[5];
        for (int i = 0; i < kNumStations; i++)
            pos[i] = lo + i * slotW + (int32_t)(rng_next() % (uint32_t)jit);

        // Shuffle which slot is which role, so broadcast/interference positions vary.
        int32_t idx[5] = { 0, 1, 2, 3, 4 };
        for (int i = kNumStations - 1; i > 0; i--) {
            int j = (int)(rng_next() % (uint32_t)(i + 1));
            int32_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
        }
        for (int i = 0; i < kNumStations; i++) dialPos[i] = pos[idx[i]];

        // Pick kNumClips distinct interference clips from the 6 available.
        uint8_t pool[6] = { 0, 1, 2, 3, 4, 5 };
        for (int i = 5; i > 0; i--) {
            int j = (int)(rng_next() % (uint32_t)(i + 1));
            uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        }
        for (int i = 0; i < kNumClips; i++) clipOrder[i] = pool[i];

        // Clear per-station DSP state.
        for (int i = 0; i < kNumStations; i++) {
            shiftPhase[i] = 0; detuneSm[i] = 0; stDc[i] = 0; stStrength[i] = 0;
            hilBdelay[i] = 0;
            for (int k = 0; k < 4; k++) {
                hilAx1[i][k]=hilAx2[i][k]=hilAy1[i][k]=hilAy2[i][k]=0;
                hilBx1[i][k]=hilBx2[i][k]=hilBy1[i][k]=hilBy2[i][k]=0;
            }
        }
        for (int i = 0; i < kNumClips; i++) {
            intfPos[i] = 0; intfFrac[i] = 0; intfSamp[i] = 0;
        }
    }

public:
    OffAir() {}

    void Init()
    {
        gpio_init(DEBUG_2);
        gpio_set_dir(DEBUG_2, GPIO_OUT);
        gpio_put(DEBUG_2, false);
    }

    void StageLed(int n) { LedBrightness(n, 4095); }

    void __not_in_flash_func(ProcessSample)() override
    {
        smMain += (KnobVal(Knob::Main) - smMain) >> 6;
        smX    += (KnobVal(Knob::X)    - smX)    >> 6;
        smY    += (KnobVal(Knob::Y)    - smY)    >> 6;

        // -------------------------------------------------------------------
        // Startup
        // -------------------------------------------------------------------
        if (!mode_locked) {
            if (mode_settle > 0) {
                mode_settle--;
                AudioOut1(0); AudioOut2(0);
                LedBrightness(5, 4095);
                return;
            }
            altbootMode = (SwitchVal() == Switch::Down);
            mode_locked = true;
            downArmed   = false;
            bcastPos[0] = 0;
            bcastPos[1] = kBcastClips[1].len / 2;
            randomiseDial();
            if (altbootMode) {
                for (int i = 0; i < 6; i++) LedBrightness(i, 4095);
            }
        }

        // -------------------------------------------------------------------
        // Fade-in
        // -------------------------------------------------------------------
        if (fadeGain < 4095) {
            fadeGain += 4095 / 480;
            if (fadeGain > 4095) fadeGain = 4095;
        }

        // -------------------------------------------------------------------
        // Switch: tap Down = cycle band (+ re-randomise). Hold Up is mode-dependent:
        //   ALTBOOT  → mute broadcasts 1 & 2 (stations still exist for CV/pulse)
        //   NORMAL   → Broadcast 2 becomes a PU1-keyed morse tone (PU1 stops shuffling)
        // -------------------------------------------------------------------
        Switch sw = SwitchVal();
        if (sw == Switch::Down) {
            switchTimer++;
        } else {
            if (downArmed && switchTimer > 0 && switchTimer < 48000) {
                band = (band + 1) % 3;
                randomiseDial();
            }
            downArmed   = (switchTimer == 0);
            switchTimer = 0;
        }
        bool switchUp  = (sw == Switch::Up);
        bool morseMode = switchUp && !altbootMode;   // normal boot + Up = morse on B2

        // -------------------------------------------------------------------
        // Tuning position — Main knob (coarse) + CV In 1 as a 1:1 offset.
        // 1:1 scaling means CV Out 2 (= Broadcast 1's offset from the knob) fed back
        // into CV In 1 lands exactly on Broadcast 1, regardless of knob position.
        // -------------------------------------------------------------------
        int32_t cv1 = CVIn1();                       // ±2048 (≈ ±5V)
        int32_t tunePos = smMain + cv1;              // 1:1 offset, no gain
        if (tunePos < 0)    tunePos = 0;
        if (tunePos > 4095) tunePos = 4095;

        // -------------------------------------------------------------------
        // IF bandwidth / capture width — Knob X.
        // CCW = narrow & selective (fiddly), CW = wide & easy to find.
        // -------------------------------------------------------------------
        int32_t bandwidth = kBwMin + (smX * (kBwMax - kBwMin)) / 4095;
        int32_t bwInnerFlat = (bandwidth * kBwInnerFrac) / 100;
        // IF bandwidth also sets audio brightness (the audible part): narrow IF (CCW) =
        // muffled/boxy, wide IF (CW) = bright. Maps Knob X to a tone LPF alpha (Q12).
        int32_t toneAlpha = kToneMin + (smX * (kToneMax - kToneMin)) / 4095;

        // -------------------------------------------------------------------
        // Pulse In 1: normally re-randomises the layout on a rising edge. In morse
        // mode (Normal boot + Switch Up) it is instead the morse key (see below).
        // -------------------------------------------------------------------
        bool pu1Now = PulseIn1();
        if (!morseMode && pu1Now && !pu1Prev) randomiseDial();
        pu1Prev = pu1Now;

        // Morse oscillator + key envelope (used only in morse mode).
        morsePhase += (uint32_t)kMorseInc;
        int32_t keyTarget = (morseMode && pu1Now) ? 4096 : 0;
        morseKey += (keyTarget - morseKey) >> kMorseRamp;     // smooth attack/release
        int32_t morseTone = (((isin(morsePhase) * kMorseLevel) >> 7) * morseKey) >> 12;

        // -------------------------------------------------------------------
        // Pulse In 2 rising edge → trigger a one-shot from the curated bank.
        // Plays the whole clip once-through, fairly present. Retrigger restarts.
        // Falls back to the loop bank if the one-shot bank is empty.
        // -------------------------------------------------------------------
        bool pu2Now = PulseIn2();
        if (pu2Now && !pu2Prev) {
            osClip   = (int32_t)(rng_next() % (uint32_t)(kNumOneShots > 0 ? kNumOneShots : 6));
            osPos    = 0;
            osFrac   = 0;
            osSample = 0;
        }
        pu2Prev = pu2Now;

        // -------------------------------------------------------------------
        // CV In 2 → noise level (adds to Knob Y). Read here, applied at the noise stage.
        // -------------------------------------------------------------------
        int32_t cv2 = CVIn2();                         // -2048..2047, 0V≈0
        int32_t cvNoise = cv2 > 0 ? cv2 * 2 : 0;       // +5V → ~+4095 of extra noise

        // (Station drift removed — broadcast stations stay put.)

        // -------------------------------------------------------------------
        // Audio sources for the two broadcast stations.
        // Band-limited; scaled to ~±kStAudioScale for the shifter.
        // -------------------------------------------------------------------
        int32_t raw1 = altbootMode ? stepBcast(0) : (int32_t)AudioIn1();
        int32_t raw2 = altbootMode ? stepBcast(1) : (int32_t)AudioIn2();

        hpState1 += (raw1 - hpState1) >> 11;
        hpState2 += (raw2 - hpState2) >> 11;
        int32_t lpfA = kLpfAlpha[band];
        lpfState1 += (((raw1 - hpState1) - lpfState1) * lpfA) >> 12;
        lpfState2 += (((raw2 - hpState2) - lpfState2) * lpfA) >> 12;

        // Per-station audio: 0/1 broadcast (louder), 2/3/4 interference (subtler).
        int32_t aud[5];
        aud[0] = (lpfState1 * kBcastScale) >> 11;
        aud[1] = (lpfState2 * kBcastScale) >> 11;
        aud[2] = (stepIntf(0) * kIntfScale) >> 11;
        aud[3] = (stepIntf(1) * kIntfScale) >> 11;
        aud[4] = (stepIntf(2) * kIntfScale) >> 11;

        // Switch Up overrides (broadcast audio only — strengths/CV/pulse unaffected):
        if (morseMode) {
            aud[1] = morseTone;              // Normal boot: B2 = PU1-keyed morse tone
        } else if (switchUp) {               // Altboot: mute both broadcasts
            aud[0] = 0;
            aud[1] = 0;
        }

        // -------------------------------------------------------------------
        // Per-station behavioural demod:
        //   detune → strength fade, sliding whistle, and audio demod that depends
        //   on band: SW/LW = SSB directional pitch-shift; AM = symmetric garble.
        // -------------------------------------------------------------------
        bool pitchShift = kBandPitchShift[band];
        int32_t mix = 0;
        int32_t maxStrength = 0;       // strongest station (for noise ducking)
        for (int i = 0; i < kNumStations; i++) {
            int32_t eff = dialPos[i];

            int32_t d = tunePos - eff;
            detuneSm[i] += (d - detuneSm[i]) >> kDetuneShift;
            int32_t ds = detuneSm[i];
            int32_t ad = ds < 0 ? -ds : ds;

            int32_t strength;
            if      (ad >= bandwidth)   strength = 0;
            else if (ad <= bwInnerFlat) strength = 4096;
            else strength = ((bandwidth - ad) * 4096) / (bandwidth - bwInnerFlat);
            stStrength[i] = strength;

            // Skip the heavy DSP entirely when out of range (keeps state cleared).
            if (strength == 0) {
                shiftPhase[i] = 0;
                continue;
            }

            // Whistle/shift oscillator. SW/LW use SIGNED detune (direction matters →
            // pitch shifts up one side, down the other). AM uses |detune| (symmetric).
            int32_t phaseStep = pitchShift ? ds : ad;
            shiftPhase[i] += (uint32_t)(phaseStep * kWhistleK_phaseInc);
            int32_t c = icos(shiftPhase[i]);   // ±127, =127 at phase 0 (detune→0)
            int32_t s = isin(shiftPhase[i]);   // ±127, =0   at phase 0

            int32_t shifted;
            if (pitchShift) {
                // SSB single-sideband shift: I/Q from Hilbert pair, complex mix.
                // Audio is genuinely frequency-shifted by detune → "wrong pitch".
                int32_t I = hilbertChain(kHilbA, aud[i],
                                         hilAx1[i], hilAx2[i], hilAy1[i], hilAy2[i]);
                int32_t Braw = hilbertChain(kHilbB, aud[i],
                                            hilBx1[i], hilBx2[i], hilBy1[i], hilBy2[i]);
                int32_t Q = hilBdelay[i]; hilBdelay[i] = Braw;   // +1 sample → quadrature
                shifted = (I * c - Q * s) >> 7;
            } else {
                // AM envelope detection: audio stays at CORRECT PITCH. Off-tune, a real
                // envelope detector distorts (sideband imbalance) — emulate with a
                // detune-dependent soft overdrive: clean on-tune, gritty as you detune.
                int32_t a = aud[i];
                int32_t od = ad - bwInnerFlat;            // 0 at clean centre
                if (od > 0) {
                    // drive scales with how far off-tune (0..~Q12), boosts then clips
                    int32_t drive = (od * 4096) / (bandwidth - bwInnerFlat);  // 0..4096
                    a += (a * drive) >> 12;               // up to ~2x gain off-tune
                    a = softClip(a, 1600);                // asymmetric grit on peaks
                }
                shifted = a;
            }

            // For pitch-shift bands, fade the AUDIO faster than the capture width so it
            // is already quiet by the time the slide reaches deep bass. Whistle keeps
            // the full-width capture. AM audio uses full strength (no slide problem).
            int32_t audGain = strength;
            if (pitchShift) {
                if (ad >= kSsbAudioHalf) audGain = 0;
                else audGain = (kSsbAudioHalf - ad) * 4096 / kSsbAudioHalf;  // Q12
                if (audGain > strength) audGain = strength;
            }
            int32_t stAudio = (shifted * audGain) >> 12;

            // Audio path: per-station HP (~119Hz, removes DC + sub-bass slide).
            stDc[i] += (stAudio - stDc[i]) >> kStDcShift;
            int32_t stOut = stAudio - stDc[i];
            mix += (stOut * strength) >> 12;             // overall capture fade

            // Heterodyne whistle — separate path, its OWN envelope (NOT ducked by the
            // audio strength). Full across the capture, fading only at zero-beat via the
            // low-pitch gate (real speakers can't reproduce the sub-200Hz beat).
            // ad in counts; whistle_hz = ad*8, so 25 counts ≈ 200Hz.
            // Whistle gate: SILENT within kWhistleLo counts of tune (no low rumble —
            // you can tune cleanly through the centre), then ramps in only once the
            // beat is a proper high-pitched whistle, reaching full by kWhistleFull.
            int32_t wgate;
            if      (ad <= kWhistleLo)   wgate = 0;
            else if (ad >= kWhistleFull) wgate = 4096;
            else wgate = ((ad - kWhistleLo) * 4096) / (kWhistleFull - kWhistleLo);
            int32_t wAmp = pitchShift ? kWhistleA : kWhistleA_AM;
            int32_t whistle = (((wAmp * c) >> 7) * wgate) >> 12;
            mix += (whistle * strength) >> 12;           // off near tune, whistle when hunting

            if (strength > maxStrength) maxStrength = strength;
        }

        // -------------------------------------------------------------------
        // Noise — filtered white noise with slow random-walk swell (level) and
        // swish (filter cutoff). Knob Y scales level. Ducks when tuned to a station.
        // -------------------------------------------------------------------
        // Swell: random walk of a Q12 level multiplier within [4096-depth, 4096].
        uint32_t nrv = rng_next();
        if ((nrv & kNoiseWalkMask) == 0) {
            int32_t depth = kSwellDepth[band];
            swellTarget += (nrv & 1) ? 96 : -96;
            if (swellTarget > 4096)         swellTarget = 4096;
            if (swellTarget < 4096 - depth) swellTarget = 4096 - depth;
        }
        swellLevel += (swellTarget - swellLevel) >> 9;   // smooth glide (~512 samp)

        // Swish: random walk of the noise LPF cutoff around its band centre.
        nrv = rng_next();
        if ((nrv & kNoiseWalkMask) == 0) {
            int32_t range = kSwishRange[band];
            swishTarget += (nrv & 1) ? 16 : -16;
            if (swishTarget >  range) swishTarget =  range;
            if (swishTarget < -range) swishTarget = -range;
        }
        swishCut += (swishTarget - swishCut) >> 9;
        int32_t nAlpha = kNoiseLpf[band] + swishCut;
        if (nAlpha < 20)   nAlpha = 20;
        if (nAlpha > 2000) nAlpha = 2000;

        int32_t white = (int32_t)(rng_next() >> 21) - 1024;  // ±1024 white noise
        bpY1 += ((white - bpY1) * nAlpha) >> 12;
        if (bpY1 >  2000) bpY1 =  2000;
        if (bpY1 < -2000) bpY1 = -2000;
        // Per-band gain compensation (filtering loses energy → equalise band loudness).
        int32_t nb = (bpY1 * kNoiseGain[band]) >> 8;
        if (nb >  2047) nb =  2047;
        if (nb < -2047) nb = -2047;
        // Noise level = Knob Y + CV In 2 (silent at CCW), modulated by the slow swell.
        // Ducked when tuned to a station — a strong carrier quiets the hiss (AM most).
        int32_t noiseLvl = smY + cvNoise;
        if (noiseLvl > 4095) noiseLvl = 4095;
        // duck factor: 4096 (full noise) down toward 0 as the strongest station rises.
        int32_t duck = 4096 - ((maxStrength * kNoiseDuck[band]) >> 12);
        if (duck < 0) duck = 0;
        int32_t noiseOut = (((nb * noiseLvl) >> 13) * swellLevel) >> 12;  // halved again
        noiseOut = (noiseOut * duck) >> 12;

        // One-shot interference burst — ducked, sits under the current audio.
        int32_t osOut = stepOneShot();

        // -------------------------------------------------------------------
        // Output
        // -------------------------------------------------------------------
        mix = mix >> 1;                                  // headroom; softClip catches overlap
        mix = softClip(mix, 1800);                       // graceful crowded-band sat
        mix += noiseOut;                                 // Knob Y / CV2 static
        mix += osOut;                                    // one-shot burst, under the audio

        // IF-bandwidth tone shaping (Knob X) — LAST in the chain, shapes everything:
        // narrow IF (CCW) = muffled, wide IF (CW) = bright/open.
        toneState += ((mix - toneState) * toneAlpha) >> 12;
        mix = toneState;

        int32_t out1 = softClip(mix, 1800);
        if (out1 >  2047) out1 =  2047;
        if (out1 < -2048) out1 = -2048;
        if (fadeGain < 4095) out1 = (out1 * fadeGain) >> 12;
        AudioOut1((int16_t)out1);

        int32_t out2 = noiseOut;
        if (out2 >  2047) out2 =  2047;
        if (out2 < -2048) out2 = -2048;
        if (fadeGain < 4095) out2 = (out2 * fadeGain) >> 12;
        AudioOut2((int16_t)out2);

        // -------------------------------------------------------------------
        // LED / CV / Pulse from station strengths
        // -------------------------------------------------------------------
        int32_t env1 = stStrength[0]; if (env1 > 4095) env1 = 4095;
        int32_t env2 = stStrength[1]; if (env2 > 4095) env2 = 4095;

        // CV Out 1: overall signal strength (envelope — rises as you tune in).
        int32_t sigStr = env1 + env2;
        if (sigStr > 4095) sigStr = 4095;
        CVOut1((int16_t)(sigStr - 2048));
        // CV Out 2: Broadcast 1's offset from the current knob position. Patch via an
        // external slew into CV In 1 (a 1:1 offset) and the dial tunes exactly onto
        // Broadcast 1, whatever the knob is set to; tap Pulse In 1 to re-randomise and
        // it hunts to the new position. (Clamps at ±5V if B1 is >½ dial from the knob —
        // nudge the Main knob nearer if so.)
        int32_t b1off = dialPos[0] - smMain;
        if (b1off >  2047) b1off =  2047;
        if (b1off < -2048) b1off = -2048;
        CVOut2((int16_t)b1off);

        // Pulse Out 1 / 2: HIGH while tuned to Broadcast 1 / Broadcast 2 respectively.
        PulseOut1(env1 > kOnThresh);
        PulseOut2(env2 > kOnThresh);

        if (altbootMode && altbootFlash > 0) {
            altbootFlash--;
            for (int i = 0; i < 6; i++) LedBrightness(i, 4095);
        } else {
            LedBrightness(0, (uint16_t)env1);
            LedBrightness(1, (uint16_t)env2);
            // Band: both off = AM, LED2 = SW, LED3 = LW
            LedBrightness(2, band == 1 ? 4095 : 0);
            LedBrightness(3, band == 2 ? 4095 : 0);
            LedBrightness(4, 0);
            LedBrightness(5, (uint16_t)tunePos);
        }

        // -------------------------------------------------------------------
        // PWM RF transmitter — transmit the final audio (RF out not a focus)
        // -------------------------------------------------------------------
        int32_t txOut = out1;
        if (txOut >  2047) txOut =  2047;
        if (txOut < -2047) txOut = -2047;
        fqValue = 563200 + (4095 - tunePos) * 280;
        amValue = (txOut + 2048) * (fqValue >> 13);
    }
};

// ---------------------------------------------------------------------------
// RF PWM ISR — core 0
// ---------------------------------------------------------------------------

static void __not_in_flash_func(OnRFPWMWrap)()
{
    static int32_t amError = 0, fqError = 0;
    int32_t amv = amValue, fqv = fqValue;

    uint32_t amTruncated = (uint32_t)(amv - amError) & 0xFFFFF000u;
    uint32_t fqTruncated = (uint32_t)(fqv - fqError) & 0xFFFFF000u;

    amError += (int32_t)amTruncated - amv;
    fqError += (int32_t)fqTruncated - fqv;

    pwm_hw->slice[0].cc  = amTruncated >> 12;
    pwm_hw->slice[0].top = fqTruncated >> 12;

    pwm_hw->intr = 1;
}

static void SetupRFPWM()
{
    gpio_set_function(DEBUG_1, GPIO_FUNC_PWM);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, 220);
    pwm_init(pwm_gpio_to_slice_num(DEBUG_1), &config, true);
    pwm_set_gpio_level(DEBUG_1, 0);

    uint slice_num = pwm_gpio_to_slice_num(DEBUG_1);
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, OnRFPWMWrap);
    irq_set_priority(PWM_IRQ_WRAP, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static OffAir card;

void core1_entry() { card.Run(); }

int main()
{
    set_sys_clock_khz(144000, true);

    card.Init();
    card.StageLed(0);

    multicore_launch_core1(core1_entry);
    card.StageLed(1);

    sleep_ms(500);
    card.StageLed(2);

    SetupRFPWM();
    card.StageLed(3);

    while (true) __wfi();
}
