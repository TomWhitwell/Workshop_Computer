// synth.c — the instrument.

#include "synth.h"
#include "psg.h"
#include "link.h"
#include "notes.h"

// One patch must fit one flash slot on the Workshop, because the flash sector is the erase unit
// and sixteen slots are one sector. If this ever fires, drop an ornament slot rather than
// widening the slot: the transfer time is a byte per link word.
typedef char patch_fits_a_slot[(sizeof(Patch) <= GBA_PATCH_SLOT_BYTES) ? 1 : -1];

#define REG_BASE     0x04000000
#define REG_KEYINPUT (*(volatile uint16_t *)(REG_BASE + 0x0130))
#define REG_TM0CNT_L (*(volatile uint16_t *)(REG_BASE + 0x0100))
#define REG_TM0CNT_H (*(volatile uint16_t *)(REG_BASE + 0x0102))

#define KEY_A      (1u << 0)
#define KEY_B      (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START  (1u << 3)
#define KEY_RIGHT  (1u << 4)
#define KEY_LEFT   (1u << 5)
#define KEY_UP     (1u << 6)
#define KEY_DOWN   (1u << 7)
#define KEY_R      (1u << 8)
#define KEY_L      (1u << 9)

static const uint16_t BTN_BIT[BTN_SLOTS] = {
    KEY_A, KEY_B, KEY_L, KEY_R, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT
};
const char *btn_name[BTN_SLOTS] = { "A", "B", "L", "R", "UP", "DOWN", "LEFT", "RIGHT" };

const char *dest_name[DEST_COUNT] = {
    "---", "PITCH", "LEVEL", "DUTY", "DETUNE", "GLIDE", "DECAY",
    "SWEEP", "N PITCH", "ORNMNT", "ORNRATE", "SCALE", "KEY", "ATTACK", "RELEASE"
};
const char *src_name[SRC_COUNT] = { "CV 1", "CV 2", "AUD 1", "AUD 2", "MAIN", "KNOB X", "KNOB Y",
                                    "SWITCH" };
const char *trig_name[TRIG_COUNT] = { "PU2", "SW", "BTN" };
const char *pan_name[4] = { "OFF", "L", "R", "BOTH" };
const char *key_name[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char *scale_name[SCALE_COUNT] = {
    "CHROMATIC", "MAJOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD", "MINOR", "LOCRIAN",
    "HARM MIN", "PENTA MAJ", "PENTA MIN", "BLUES", "HIRAJOSHI", "IN SEN", "WHOLE",
    "USER 1", "USER 2", "USER 3", "USER 4"
};

// Scale membership as a 12-bit mask: bit n set if semitone n above the key is in the scale.
static const uint16_t SCALE_MASK[SCALE_BUILTIN] = {
    0x0FFF,   // chromatic    everything
    0x0AB5,   // major        0 2 4 5 7 9 11
    0x06AD,   // dorian       0 2 3 5 7 9 10
    0x05AB,   // phrygian     0 1 3 5 7 8 10
    0x0AD5,   // lydian       0 2 4 6 7 9 11
    0x06B5,   // mixolydian   0 2 4 5 7 9 10
    0x05AD,   // aeolian      0 2 3 5 7 8 10
    0x056B,   // locrian      0 1 3 5 6 8 10
    0x09AD,   // harm minor   0 2 3 5 7 8 11
    0x0295,   // penta major  0 2 4 7 9
    0x04A9,   // penta minor  0 3 5 7 10
    0x04E9,   // blues        0 3 5 6 7 10
    0x018D,   // hirajoshi    0 2 3 7 8
    0x04A3,   // in sen       0 1 5 7 10
    0x0555,   // whole tone   0 2 4 6 8 10
};

const char *act_name[ACT_COUNT] = {
    "---", "TRIGGER", "HOLD", "OCT +", "OCT -", "DETUNE +", "DETUNE -",
    "DUTY 1", "DUTY 2", "CH1 ON/OFF", "CH2 ON/OFF", "CH3 ON/OFF", "CH4 ON/OFF",
    "ORNMNT +", "ORNMNT -", "SEMI +", "SEMI -",
    "DUTY BOTH", "SWEEP TIME",
    "ORN SLOT 1", "ORN SLOT 2", "ORN SLOT 3", "ORN SLOT 4", "ORN SLOT 5", "ORN SLOT 6"
};

const char *drum_src_name[DRUM_SRC_COUNT] = { "AUD 1", "AUD 2", "CV 1", "CV 2", "PU 2", "SWITCH" };
const char *drum_name[DRUM_PRESETS] = {
    "OFF", "KICK", "SNARE", "CL HAT", "OP HAT", "TOM HI", "TOM LO",
    "RIM", "CLAP", "COWBELL", "ZAP"
};

// A drum voice. `noise` picks which PSG channel it lands on: the noise generator for anything
// with a hiss, the WAVE channel for anything with a pitch. Pitched drums fall by `sweep`
// semitones over their decay, which is the whole trick behind a PSG kick, and `wave` chooses the
// body they fall with — a sine for a kick is a completely different sound from a square.
typedef struct {
    uint8_t noise;      // 1 = channel 4 (noise), 0 = channel 3 (wave)
    uint8_t note;       // starting MIDI note (pitched) or noise shift (noise)
    uint8_t sweep;      // semitones of downward pitch sweep across the decay
    uint8_t dec;        // ENV_MS index
    uint8_t level;      // 0..15
    uint8_t width;      // noise width: 1 = 7-bit, metallic
    uint8_t wave;       // index into psg_wave_preset, pitched voices only
} DrumVoice;

static const DrumVoice DRUM[DRUM_PRESETS] = {
    { 0,  0,  0,  0,  0, 0,  0 },   // OFF
    { 0, 45, 24,  5, 15, 0,  0 },   // KICK     sine body, fast deep fall
    { 1,  4,  0,  6, 13, 0,  0 },   // SNARE    mid noise
    { 1,  2,  0,  2, 10, 1,  0 },   // CL HAT   short metallic
    { 1,  2,  0,  7, 10, 1,  0 },   // OP HAT   same, long
    { 0, 62, 10,  7, 13, 0,  1 },   // TOM HI   triangle body
    { 0, 50, 10,  8, 13, 0,  1 },   // TOM LO
    { 1,  0,  0,  1, 12, 1,  0 },   // RIM      very short, very bright
    { 1,  5,  0,  4, 12, 0,  0 },   // CLAP
    { 0, 74,  0,  6, 12, 0,  5 },   // COWBELL  narrow pulse, no sweep
    { 0, 80, 36,  4, 13, 0,  2 },   // ZAP      saw, huge fall
};

Patch g_patch;

int32_t  g_pitchQ8 = 60 << 8;
int32_t  g_chPitch[4] = { 60 << 8, 60 << 8, 60 << 8, 60 << 8 };
uint8_t  g_note    = 60;
uint8_t  g_hold    = 0;
uint16_t g_chEnv[4]     = { 0, 0, 0, 0 };
uint8_t  g_chLevel[4]   = { 0, 0, 0, 0 };
uint8_t  g_chNoteOn[4]  = { 0, 0, 0, 0 };
uint8_t  g_chOrnStep[4] = { 0, 0, 0, 0 };
uint8_t  g_anyNoteOn   = 0;
uint8_t  g_btnTrigHeld = 0;
uint8_t  g_drumHit[DRUM_SRC_COUNT] = { 0, 0, 0, 0, 0, 0 };

uint16_t g_btn = 0;
static uint16_t g_btnEdge = 0, g_btnRep = 0, g_btnStep = 0;
volatile uint16_t g_btnEdgeLatch = 0;
volatile uint16_t g_btnStepLatch = 0;
uint8_t g_editMode = 0;

uint16_t synth_take_edges(void) { uint16_t v = g_btnEdgeLatch; g_btnEdgeLatch = 0; return v; }
uint16_t synth_take_steps(void) { uint16_t v = g_btnStepLatch; g_btnStepLatch = 0; return v; }

// ---- envelope ----
#define ENV_IDLE 0
#define ENV_ATK  1
#define ENV_DEC  2
#define ENV_SUS  3
#define ENV_REL  4
static uint8_t g_envState[4] = { ENV_IDLE, ENV_IDLE, ENV_IDLE, ENV_IDLE };

// Envelope stage times, and the per-tick increment that realises them at the 1 kHz control rate
// (inc = 65535 / milliseconds). Spread over what you would actually dial rather than the plain
// power-of-two ladder this started as, which crammed everything useful into four indices.
const uint16_t ENV_MS[16] = {
    0, 5, 10, 20, 35, 60, 100, 160, 250, 400, 650, 1000, 1600, 2500, 4000, 6000
};
static const uint16_t ENV_INC[16] = {
    65535, 13107, 6554, 3277, 1872, 1092, 655, 410,
      262,   164,  101,   66,   41,   26,  16,  11
};

// Ornament step period in control ticks. Slow to fast.
static const uint16_t ORN_RATE[8] = { 250, 167, 125, 84, 63, 42, 31, 21 };

// ---- integer divide ---------------------------------------------------------------------
// We link -nostdlib, so libgcc is absent: a divide by a VARIABLE is an undefined reference at
// LINK time rather than merely slow code. Divides by compile-time constants are fine.
static uint32_t udiv32(uint32_t n, uint32_t d)
{
    if (d == 0) return 0;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (1u << i); }
    }
    return q;
}

static int32_t g_cvRecip = 0;
static int16_t g_cvRecipFor = 0;

static void refresh_cv_recip(void)
{
    if (g_patch.cvScale == g_cvRecipFor) return;
    g_cvRecipFor = g_patch.cvScale;
    int32_t s = g_patch.cvScale;
    // cvScale is counts per semitone in 1/256ths (Q8), so semiQ8 = raw * 256 * 256 / cvScale,
    // and the reciprocal that ">> 12" then divides by is (65536 << 12) / cvScale = 2^28 / s.
    //
    // Floor of 1024 is an OVERFLOW guard: the pitch maths multiplies the reciprocal by up to
    // 4095 counts, and at 1024 that peaks near 1.07e9, inside int32.
    if (s < 1024) s = 1024;
    g_cvRecip = (int32_t)udiv32(65536u << 12, (uint32_t)s);
}

void synth_patch_applied(void) { g_cvRecipFor = 0; refresh_cv_recip(); }

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int synth_patch_bytes(void) { return (int)sizeof(Patch); }

// ACCEPT OLDER VERSIONS THAT CAN BE BROUGHT FORWARD, not just the current one. See
// synth_patch_migrate for what "can be" means: same layout, different meaning.
int synth_patch_valid(const Patch *p)
{
    return p->magic == PATCH_MAGIC && p->version >= 2 && p->version <= PATCH_VERSION;
}

// Bring a loaded patch up to the current version IN PLACE. This only ever handles changes that
// leave the STRUCT LAYOUT alone and alter what a field means - a layout change cannot be
// migrated from the bytes alone, and synth_patch_valid rejects those instead.
//
// v2 -> v3: cvScale went from counts per semitone in 1/16ths to 1/256ths, for the resolution.
void synth_patch_migrate(Patch *p)
{
    if (p->version == 2) {
        // Clamp on the way up: the v2 range topped out at 4000, and 4000 * 16 is 64000, which
        // wraps negative in an int16_t. refresh_cv_recip() would then floor it and the patch
        // would load quietly mistuned instead of loudly broken.
        int32_t q8 = (int32_t)p->cvScale * 16;
        p->cvScale = (int16_t)clampi(q8, 1024, 32767);
    }
    p->version = PATCH_VERSION;
}

// ---- the factory patch ----------------------------------------------------------------------
void synth_default_patch(void)
{
    Patch *p = &g_patch;

    for (unsigned i = 0; i < sizeof(Patch); i++) ((uint8_t *)p)[i] = 0;
    p->magic   = PATCH_MAGIC;
    p->version = PATCH_VERSION;

    // ---- the showcase patch --------------------------------------------------------------------
    // A hard-panned square pair with a shimmer over the top, playable from the module's own
    // switch with nothing patched. It is deliberately a demonstration rather than a blank slate:
    // every jack, knob and button on the panel does something audible from the first note.

    for (int c = 0; c < 4; c++) {
        Channel *ch = &p->ch[c];
        ch->level = 15;
        ch->semi  = 0;
        ch->atk   = 2;
        ch->dec   = 6;
        ch->sus   = 13;
        ch->rel   = 6;
        ch->glide = 0;
        // The switch and a mapped button. Pulse In 2 is deliberately NOT armed: the point of the
        // default is that it plays with nothing patched at all.
        ch->trig  = (1u << TRIG_SW) | (1u << TRIG_BTN);
        ch->orn   = ORN_OFF;
        ch->pan   = PAN_OFF;
    }

    // Channel 1: left, 50% duty, quick attack, quick portamento.
    p->ch[0].pan   = PAN_L;
    p->ch[0].atk   = 2;      // 10 ms
    p->ch[0].glide = 3;      // a fast slide between notes

    // Channel 2: right, 25% duty, slow attack, no portamento. The pair arrives at different
    // times and from different sides, which is most of why it sounds wide.
    p->ch[1].pan   = PAN_R;
    p->ch[1].atk   = 8;      // 250 ms
    p->ch[1].glide = 0;

    // Channel 3: the shimmer. A sine on a permanent octave trill, sitting under the Main knob.
    p->ch[2].pan   = PAN_BOTH;
    p->ch[2].level = 7;      // mid, so the Main knob can take it either way
    p->ch[2].orn   = 5;      // slot 5, the octave trill
    p->ch[2].atk   = 3;
    p->ch[2].rel   = 7;

    p->ch[3].pan   = PAN_OFF;   // noise off; the DRUM page is where it earns its place

    p->duty[0]    = PSG_DUTY_50;
    p->duty[1]    = PSG_DUTY_25;
    p->detune     = 2;          // 1/16 semitone units: a touch over ten cents, so the pair beats
    p->waveSel    = 0;          // SINE
    p->noiseDiv   = 3;
    p->noiseShift = 4;
    p->noiseWidth = 0;
    p->retrig     = 1;
    p->sweepTime  = 0;          // off until the R button brings it in
    p->sweepDir   = 1;          // downward
    p->sweepShift = 3;          // enough depth to hear when it is switched on
    p->octave     = 0;
    p->masterL    = 7;
    p->masterR    = 7;
    p->ratio      = 2;          // 100%
    p->baseNote   = 36;         // 0 V = C2
    p->tuneCents  = 0;
    p->key        = 0;
    p->scale      = 0;          // chromatic: quantiser off
    p->drumMode   = 0;
    p->drumThresh = 6;

    // COUNTS PER SEMITONE, Q8. MEASURED, NOT CALCULATED.
    //
    // 3312 is 207 in the old Q4 unit, which is the bench trim: an octave measured about 2.2 V
    // under the original 455, and 455 / 2.2 is 207. That 455 came from taking the CV inputs to
    // span +-6 V over the full 4096 counts - 341 counts/V, 28.44 per semitone - and the hardware
    // says otherwise, so the ADC evidently keeps a good deal of over-range headroom either side
    // of the nominal input range.
    //
    // Q8 RATHER THAN Q4 BECAUSE PITCH ERROR ACCUMULATES WITH DISTANCE FROM THE CALIBRATION
    // POINT. One Q4 step is 0.48%, which is 17 cents three octaves up - so 206 and 207 straddled
    // correct with nothing between them. A Q8 step is 1.1 cents at the same distance.
    //
    // It is still only as good as one bench reading, which is why CV SCALE is trimmable and why
    // the CAL page shows the live input count: two readings a known interval apart give the
    // exact figure as 256 * (high - low) / semitones. There is no factory calibration for the CV
    // INPUTS, so this can never be more than a good starting point.
    p->cvScale  = 3312;
    p->cvOffset = 0;

    // ---- the modulation matrix -------------------------------------------------------------
    // Unused sources are left unassigned on purpose: nothing should move that you did not patch.
    for (int s = 0; s < SRC_COUNT; s++) { p->mod[s].dest = DEST_NONE; p->mod[s].depth = 0;
                                          p->mod[s].chMask = 0xF; }

    p->mod[SRC_CV2].dest   = DEST_PITCH;   p->mod[SRC_CV2].depth  = 32;  // unity 1V/oct
    p->mod[SRC_CV2].chMask = 0xF;
    p->mod[SRC_MAIN].dest  = DEST_LEVEL;   p->mod[SRC_MAIN].depth = 64;  // Main knob rides ch3
    p->mod[SRC_MAIN].chMask = 0x4;
    p->mod[SRC_X].dest     = DEST_ATTACK;  p->mod[SRC_X].depth    = 64;  // both envelopes at once
    p->mod[SRC_X].chMask   = 0xF;
    p->mod[SRC_Y].dest     = DEST_RELEASE; p->mod[SRC_Y].depth    = 64;
    p->mod[SRC_Y].chMask   = 0xF;

    // ---- ornaments ---------------------------------------------------------------------------
    // Slots 1-4 are what the D-pad selects; slot 5 is channel 3's permanent trill.
    static const int8_t seed[ORN_SLOTS][ORN_STEPS] = {
        {   0,  4,  7,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // 1 major chord, looping
        {   0,  3,  7,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // 2 minor chord, looping
        { -12, 12,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // 3 octave drop and leap, once
        {  -3, -2, -1,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // 4 chromatic run up, once
        {   0, 12,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // 5 octave trill, looping
        {   0,  7, 12, 19, 24, 19, 12, 7, 0, 0, 0, 0, 0, 0, 0, 0 },// 6 fifths and octaves
    };
    static const uint8_t seedLen[ORN_SLOTS]  = { 3, 3, 3, 4, 2, 8 };
    static const uint8_t seedRate[ORN_SLOTS] = { 4, 4, 5, 6, 6, 3 };
    static const uint8_t seedMode[ORN_SLOTS] = { 0, 0, 1, 1, 0, 0 };   // 1 = one-shot
    for (int o = 0; o < ORN_SLOTS; o++) {
        p->orn[o].len  = seedLen[o];
        p->orn[o].rate = seedRate[o];
        p->orn[o].mode = seedMode[o];
        for (int i = 0; i < ORN_STEPS; i++) p->orn[o].step[i] = seed[o][i];
    }

    // ---- the panel ----------------------------------------------------------------------------
    p->btnAct[0] = ACT_TRIGGER;    // A
    p->btnAct[1] = ACT_HOLD;       // B
    p->btnAct[2] = ACT_DUTY_BOTH;  // L
    p->btnAct[3] = ACT_SWEEP;      // R
    p->btnAct[4] = ACT_ORN1;       // Up     major chord
    p->btnAct[5] = ACT_ORN2;       // Down   minor chord
    p->btnAct[6] = ACT_ORN3;       // Left   octave drop and leap
    p->btnAct[7] = ACT_ORN4;       // Right  chromatic run

    p->userScale[0] = 0x0AB5;      // major
    p->userScale[1] = 0x05AD;      // minor
    p->userScale[2] = 0x0295;      // pentatonic
    p->userScale[3] = 0x0FFF;      // chromatic

    p->drumMap[DRUM_SRC_AUD1] = 1;   // kick
    p->drumMap[DRUM_SRC_AUD2] = 2;   // snare
    p->drumMap[DRUM_SRC_CV1]  = 3;   // closed hat
    p->drumMap[DRUM_SRC_CV2]  = 5;   // tom hi
    p->drumMap[DRUM_SRC_PU2]  = 1;   // kick
    p->drumMap[DRUM_SRC_SW]   = 2;   // snare
}

void synth_init(void)
{
    synth_default_patch();
    synth_patch_applied();
    psg_init();

    // Free-running control clock: 16.78 MHz / 1024 = 16384 Hz, so 16 ticks is 1.024 kHz. Paced
    // off a timer rather than VBlank because 60 Hz modulation on a CV-driven voice is steppy.
    REG_TM0CNT_L = 0;
    REG_TM0CNT_H = 0x0083;
}

// ---- buttons ----
static uint16_t g_btnPrev = 0;
static uint16_t g_repTimer[16];

static void scan_buttons(void)
{
    uint16_t now = (uint16_t)((~REG_KEYINPUT) & 0x03FFu);
    g_btnEdge = (uint16_t)(now & ~g_btnPrev);
    g_btnRep  = 0;

    for (int b = 0; b < 10; b++) {
        uint16_t m = (uint16_t)(1u << b);
        if (!(now & m)) { g_repTimer[b] = 0; continue; }
        if (g_btnEdge & m) { g_repTimer[b] = 300; continue; }
        if (g_repTimer[b]) {
            if (--g_repTimer[b] == 0) { g_btnRep |= m; g_repTimer[b] = 40; }
        }
    }

    g_btn     = now;
    g_btnPrev = now;
    g_btnStep = (uint16_t)(g_btnEdge | g_btnRep);
    g_btnEdgeLatch |= g_btnEdge;
    g_btnStepLatch |= g_btnStep;
    link_set_buttons(now);
}

// ---- pitch ----------------------------------------------------------------------------------
static uint16_t period_for(int32_t pitchQ8, int semitoneShift)
{
    int32_t p = pitchQ8 + ((int32_t)semitoneShift << 8);
    p = clampi(p, (int32_t)NOTE_MIN << 8, ((int32_t)NOTE_MAX - 1) << 8);

    int32_t n    = p >> 8;
    int32_t frac = p & 0xFF;
    int32_t a = NOTE_PERIOD[n];
    int32_t b = NOTE_PERIOD[n + 1];
    return (uint16_t)(a + (((b - a) * frac) >> 8));
}

static uint16_t scale_mask_for(int idx)
{
    if (idx < SCALE_BUILTIN) return SCALE_MASK[idx];
    uint16_t m = g_patch.userScale[(idx - SCALE_BUILTIN) & 3];
    return m ? (uint16_t)(m & 0x0FFF) : 0x0FFF;   // an empty user scale would silence everything
}

// Snap a note to the NEAREST degree of the current key and scale, not the one below it. Rounding
// down makes a slow upward CV sweep hang on each degree until it is a full step past — the note
// you hear lags the voltage you can see. Nearest splits the difference and tracks properly.
static int32_t quantise(int32_t note, int scaleIdx, int key)
{
    if (scaleIdx == 0) return note;

    uint16_t mask = scale_mask_for(scaleIdx);
    int32_t  rel  = note - key;
    int32_t  oct  = rel / 12;                  // constant divisor
    int32_t  deg  = rel - oct * 12;
    if (deg < 0) { deg += 12; oct--; }

    for (int d = 0; d < 12; d++) {
        int32_t up = deg + d, dn = deg - d;
        int32_t k, adj;
        // Down first on a tie, so a note exactly between two degrees resolves consistently.
        k = dn; adj = 0;
        if (k < 0) { k += 12; adj = -12; }
        if (mask & (1u << k)) return key + oct * 12 + k + adj;
        k = up; adj = 0;
        if (k > 11) { k -= 12; adj = 12; }
        if (mask & (1u << k)) return key + oct * 12 + k + adj;
    }
    return note;
}

// ---- drums ----------------------------------------------------------------------------------
// State for the two channels the drum engine can borrow.
static int32_t  g_drumPitch = 0;     // Q8, channel 1
static int32_t  g_drumFall  = 0;     // Q8 per tick
static uint16_t g_drumEnv[2] = { 0, 0 };
static uint8_t  g_drumDec[2] = { 0, 0 };
static uint8_t  g_drumLvl[2] = { 0, 0 };
static uint8_t  g_drumShift  = 0;
static uint8_t  g_drumWidth  = 0;
static uint8_t  g_drumWave   = 0;
static uint8_t  g_drumAmp    = 0xFF;   // last amplitude written to wave RAM
static uint8_t  g_waveAmp    = 0xFF;   // same, for the melodic wave channel
static uint8_t  g_waveSel    = 0xFF;

static void drum_fire(int preset)
{
    const DrumVoice *d = &DRUM[preset % DRUM_PRESETS];
    int slot = d->noise ? 1 : 0;

    g_drumEnv[slot] = 65535;
    g_drumDec[slot] = d->dec;
    g_drumLvl[slot] = d->level;

    if (d->noise) {
        g_drumShift = d->note;
        g_drumWidth = d->width;
    } else {
        g_drumWave  = d->wave;
        g_drumAmp   = 0xFF;              // force the wavetable to reload at the new amplitude
        g_drumPitch = (int32_t)d->note << 8;
        psg_wave_trigger(0);             // restart the waveform phase for a clean attack
        // Fall the whole sweep over roughly the decay time, in control ticks.
        uint16_t ms = ENV_MS[d->dec & 15];
        if (ms < 5) ms = 5;
        // udiv32, not '/': ms is a variable, and -nostdlib turns a variable divide into an
        // undefined __aeabi_idiv at link time. Fires once per drum hit, so the cost is nothing.
        g_drumFall = (int32_t)udiv32((uint32_t)d->sweep << 8, ms);
        if (g_drumFall < 1) g_drumFall = 1;
    }
}

// ---- the control tick -------------------------------------------------------------------------
static void synth_tick(void)
{
    Patch *p = &g_patch;
    scan_buttons();
    refresh_cv_recip();

    // ---- mapped button actions --------------------------------------------------------------
    // IN THE EDITOR NO BUTTON CARRIES ITS PERFORMANCE MEANING. Every key belongs to the editor
    // while a menu is open. Performance meanings belong to the performance screen.
    int btnTrigLevel = 0, btnTrigEdge = 0;
    for (int b = 0; b < BTN_SLOTS && !g_editMode; b++) {
        uint8_t  act = p->btnAct[b];
        uint16_t bit = BTN_BIT[b];
        int      lvl = (g_btn & bit) != 0;
        int      edg = (g_btnEdge & bit) != 0;
        int      stp = (g_btnStep & bit) != 0;

        if (act == ACT_TRIGGER) { btnTrigLevel |= lvl; btnTrigEdge |= edg; continue; }
        if (act == ACT_HOLD)    { if (edg) g_hold = (uint8_t)!g_hold; continue; }

        switch (act) {
        case ACT_OCT_UP:    if (stp) p->octave = (int8_t)clampi(p->octave + 1, -3, 3); break;
        case ACT_OCT_DN:    if (stp) p->octave = (int8_t)clampi(p->octave - 1, -3, 3); break;
        case ACT_DETUNE_UP: if (stp) p->detune = (int8_t)clampi(p->detune + 1, -64, 63); break;
        case ACT_DETUNE_DN: if (stp) p->detune = (int8_t)clampi(p->detune - 1, -64, 63); break;
        case ACT_DUTY1:     if (edg) p->duty[0] = (uint8_t)((p->duty[0] + 1) & 3); break;
        case ACT_DUTY2:     if (edg) p->duty[1] = (uint8_t)((p->duty[1] + 1) & 3); break;
        case ACT_CH1: case ACT_CH2: case ACT_CH3: case ACT_CH4: {
            int c = act - ACT_CH1;
            if (edg) p->ch[c].pan = (uint8_t)(p->ch[c].pan ? PAN_OFF : PAN_BOTH);
            break;
        }
        case ACT_ORN_UP: case ACT_ORN_DN: {
            if (!stp) break;
            int d = (act == ACT_ORN_UP) ? 1 : -1;
            for (int c = 0; c < 4; c++)
                p->ch[c].orn = (uint8_t)clampi(p->ch[c].orn + d, ORN_OFF, ORN_CV);
            break;
        }
        case ACT_SEMI_UP: case ACT_SEMI_DN: {
            if (!stp) break;
            int d = (act == ACT_SEMI_UP) ? 1 : -1;
            for (int c = 0; c < 4; c++)
                p->ch[c].semi = (int8_t)clampi(p->ch[c].semi + d, -24, 24);
            break;
        }
        case ACT_DUTY_BOTH:
            if (edg) { p->duty[0] = (uint8_t)((p->duty[0] + 1) & 3);
                       p->duty[1] = (uint8_t)((p->duty[1] + 1) & 3); }
            break;
        case ACT_SWEEP:
            if (edg) p->sweepTime = (uint8_t)((p->sweepTime + 1) & 7);
            break;
        case ACT_ORN1: case ACT_ORN2: case ACT_ORN3:
        case ACT_ORN4: case ACT_ORN5: case ACT_ORN6: {
            // The MELODIC PAIR only. Channel 3 keeps whatever it was given, which is what lets
            // it hold a steady shimmer while the lead switches figures underneath it.
            if (!edg) break;
            uint8_t slot = (uint8_t)(act - ACT_ORN1 + 1);
            p->ch[0].orn = slot;
            p->ch[1].orn = slot;
            break;
        }
        default: break;
        }
    }
    g_btnTrigHeld = (uint8_t)(btnTrigLevel != 0);

    // ---- modulation matrix ------------------------------------------------------------------
    // Per-voice destinations accumulate into an array indexed by channel and are gated by the
    // slot's channel mask; the rest address single pieces of hardware and stay global.
    int32_t modPitch[4] = { 0, 0, 0, 0 };
    int32_t modLevel[4] = { 0, 0, 0, 0 };
    int32_t modDuty[4]  = { 0, 0, 0, 0 };
    int32_t modGlide[4] = { 0, 0, 0, 0 };
    int32_t modDecay[4] = { 0, 0, 0, 0 };
    int32_t modAtk[4]   = { 0, 0, 0, 0 };
    int32_t modRel[4]   = { 0, 0, 0, 0 };
    uint8_t ornForce[4] = { 0, 0, 0, 0 };     // ornament slot forced on by a mapping
    int32_t modDetune = 0, modSweep = 0, modNPitch = 0, modOrnRate = 0;
    int     pitchToNoise = 0;      // does any PITCH mapping actually reach channel 4?
    int32_t modScale = 0, modKey = 0;

    for (int s = 0; s < SRC_COUNT; s++) {
        int dest = p->mod[s].dest;
        if (dest == DEST_NONE) continue;
        int32_t depth = p->mod[s].depth;
        uint8_t mask  = p->mod[s].chMask;

        // The four jacks are bipolar around 0 V; the three knobs are unipolar and are centred
        // here so one depth control means the same thing for both. The switch is neither: see
        // SRC_SW in synth.h for why UP is the only position that carries a value.
        int32_t raw = (s < 4) ? ((int32_t)g_in[s] - 2048)
                    : (s < 7) ? ((int32_t)g_knob[s - 4] - 2048)
                              : ((g_switch == 2) ? 2047 : 0);

        if (dest == DEST_PITCH) {
            int32_t semiQ8 = ((raw - p->cvOffset) * g_cvRecip) >> 12;
            int32_t v = (semiQ8 * depth) >> 5;      // depth 32 is unity 1V/oct
            for (int c = 0; c < 4; c++) if (mask & (1u << c)) modPitch[c] += v;
            if (mask & 0x8) pitchToNoise = 1;
            continue;
        }

        if (dest == DEST_ORN) {
            // DEPTH IS NOT A DEPTH HERE: it names the ornament SLOT, and the source is a plain
            // switch — above halfway it is on. There is no "forty per cent of an arpeggio", so a
            // continuous depth would have been a control with nothing to say.
            if (raw > 0) {
                uint8_t slot = (uint8_t)clampi(depth, 1, ORN_SLOTS);
                for (int c = 0; c < 4; c++) if (mask & (1u << c)) ornForce[c] = slot;
            }
            continue;
        }

        int32_t v = (raw * depth) >> 11;
        switch (dest) {
        case DEST_LEVEL: for (int c = 0; c < 4; c++) if (mask & (1u << c)) modLevel[c] += v; break;
        case DEST_DUTY:  for (int c = 0; c < 4; c++) if (mask & (1u << c)) modDuty[c]  += v; break;
        case DEST_GLIDE: for (int c = 0; c < 4; c++) if (mask & (1u << c)) modGlide[c] += v; break;
        case DEST_DECAY: for (int c = 0; c < 4; c++) if (mask & (1u << c)) modDecay[c] += v; break;
        case DEST_ATTACK:  for (int c = 0; c < 4; c++) if (mask & (1u << c)) modAtk[c] += v; break;
        case DEST_RELEASE: for (int c = 0; c < 4; c++) if (mask & (1u << c)) modRel[c] += v; break;
        case DEST_DETUNE:  modDetune  += v; break;
        case DEST_SWEEP:   modSweep   += v; break;
        case DEST_NPITCH:  modNPitch  += v; break;
        case DEST_ORNRATE: modOrnRate += v; break;
        case DEST_SCALE:   modScale   += v; break;
        case DEST_KEY:     modKey     += v; break;
        default: break;
        }
    }

    // Scale and key are musical settings a knob can sweep, so they are resolved here rather than
    // read straight from the patch.
    int scaleIdx = (int)clampi((int32_t)p->scale + (modScale >> 4), 0, SCALE_COUNT - 1);
    int keyIdx   = (int)clampi((int32_t)p->key   + (modKey   >> 4), 0, 11);

    // ---- pitch ---------------------------------------------------------------------------------
    int32_t base = ((int32_t)p->baseNote << 8)
                 + ((int32_t)p->octave * 12 << 8)
                 + (((int32_t)p->tuneCents * 256) / 100);    // master tuning, constant divisor
    g_pitchQ8 = clampi(base + modPitch[0], (int32_t)NOTE_MIN << 8, (int32_t)NOTE_MAX << 8);
    uint8_t note = (uint8_t)clampi((g_pitchQ8 + 128) >> 8, 0, 127);
    if (scaleIdx != 0) note = (uint8_t)clampi(quantise(note, scaleIdx, keyIdx), 0, 127);
    g_note = note;

    // ---- trigger sources ------------------------------------------------------------------------
    static uint32_t seenEdges = 0;
    static uint8_t  swPrev = 1;

    int trigLevel[TRIG_COUNT], trigEdge[TRIG_COUNT];
    trigLevel[TRIG_PU2] = g_gate;
    trigEdge[TRIG_PU2]  = 0;
    if (g_gateEdges != seenEdges) { seenEdges++; trigEdge[TRIG_PU2] = 1; }

    trigLevel[TRIG_SW] = (g_switch == 0);
    trigEdge[TRIG_SW]  = (g_switch == 0 && swPrev != 0);
    swPrev = g_switch;

    trigLevel[TRIG_BTN] = btnTrigLevel;
    trigEdge[TRIG_BTN]  = btnTrigEdge;

    // HOLD is a LATCHING trigger source: switching it on fires once and sustains, switching it
    // off releases. It never re-fires on its own.
    static uint8_t holdPrev = 0;
    int holdEdge = (g_hold && !holdPrev);
    holdPrev = g_hold;

    // ---- drums ------------------------------------------------------------------------------
    // Any input going HIGH is a pad. The threshold is deliberately generous: these are meant to
    // be driven by triggers, gates and loud audio, not by a precise voltage.
    if (p->drumMode) {
        static uint8_t hi[DRUM_SRC_COUNT] = { 0, 0, 0, 0, 0, 0 };
        int32_t thr = 2048 + (int32_t)p->drumThresh * 96;

        int lvl[DRUM_SRC_COUNT];
        lvl[DRUM_SRC_AUD1] = (int32_t)g_in[SRC_AUD1] > thr;
        lvl[DRUM_SRC_AUD2] = (int32_t)g_in[SRC_AUD2] > thr;
        lvl[DRUM_SRC_CV1]  = (int32_t)g_in[SRC_CV1]  > thr;
        lvl[DRUM_SRC_CV2]  = (int32_t)g_in[SRC_CV2]  > thr;
        lvl[DRUM_SRC_PU2]  = g_gate;
        lvl[DRUM_SRC_SW]   = (g_switch == 0);

        for (int i = 0; i < DRUM_SRC_COUNT; i++) {
            if (lvl[i] && !hi[i] && p->drumMap[i]) {
                drum_fire(p->drumMap[i]);
                g_drumHit[i] = 60;                    // ~60 ms of indicator
            }
            hi[i] = (uint8_t)lvl[i];
            if (g_drumHit[i]) g_drumHit[i]--;
        }

        for (int d = 0; d < 2; d++) {
            if (!g_drumEnv[d]) continue;
            int32_t e = (int32_t)g_drumEnv[d] - ENV_INC[g_drumDec[d] & 15];
            g_drumEnv[d] = (uint16_t)(e < 0 ? 0 : e);
        }
        if (g_drumFall) {
            g_drumPitch -= g_drumFall;
            if (g_drumPitch < ((int32_t)NOTE_MIN << 8)) g_drumPitch = (int32_t)NOTE_MIN << 8;
        }
    }

    // ---- per-channel envelopes ------------------------------------------------------------------
    int chEdge[4] = { 0, 0, 0, 0 };
    g_anyNoteOn = 0;

    for (int c = 0; c < 4; c++) {
        Channel *ch = &p->ch[c];

        int gateOn = ch->trig ? g_hold : 0;
        int edge   = ch->trig ? holdEdge : 0;
        for (int t = 0; t < TRIG_COUNT; t++) {
            if (!(ch->trig & (1u << t))) continue;
            gateOn |= trigLevel[t];
            edge   |= trigEdge[t];
        }

        // Attack and release are modulated as INDICES into the time table, so a knob sweeps the
        // stage time the same way the editor's own control does.
        int32_t atkIx = clampi((int32_t)ch->atk + (modAtk[c] >> 3), 0, 15);
        int32_t relIx = clampi((int32_t)ch->rel + (modRel[c] >> 3), 0, 15);

        if (edge) {
            g_envState[c] = (atkIx == 0) ? ENV_DEC : ENV_ATK;
            if (atkIx == 0)     g_chEnv[c] = 65535;
            else if (p->retrig) g_chEnv[c] = 0;
            g_chNoteOn[c]  = 1;
            g_chOrnStep[c] = 0;
            chEdge[c] = 1;
        } else if (!gateOn && g_chNoteOn[c] && g_envState[c] != ENV_REL) {
            g_envState[c] = ENV_REL;
        }

        int32_t susLevel = (int32_t)ch->sus * 4369;
        int32_t decIx    = clampi((int32_t)ch->dec + (modDecay[c] >> 5), 0, 15);

        switch (g_envState[c]) {
        case ENV_ATK: {
            int32_t e = (int32_t)g_chEnv[c] + ENV_INC[atkIx];
            if (e >= 65535) { e = 65535; g_envState[c] = ENV_DEC; }
            g_chEnv[c] = (uint16_t)e;
            break;
        }
        case ENV_DEC: {
            int32_t e = (int32_t)g_chEnv[c] - ENV_INC[decIx];
            if (e <= susLevel) { e = susLevel; g_envState[c] = ENV_SUS; }
            g_chEnv[c] = (uint16_t)e;
            break;
        }
        case ENV_SUS: g_chEnv[c] = (uint16_t)susLevel; break;
        case ENV_REL: {
            int32_t e = (int32_t)g_chEnv[c] - ENV_INC[relIx];
            if (e <= 0) { e = 0; g_envState[c] = ENV_IDLE; g_chNoteOn[c] = 0; }
            g_chEnv[c] = (uint16_t)e;
            break;
        }
        default: g_chEnv[c] = 0; break;
        }

        if (g_chNoteOn[c]) g_anyNoteOn = 1;
    }

    // ---- ornaments ------------------------------------------------------------------------------
    static uint16_t ornCount[4] = { 0, 0, 0, 0 };
    int32_t ornOffset[4] = { 0, 0, 0, 0 };

    for (int c = 0; c < 4; c++) {
        Channel *ch = &p->ch[c];

        // A mapping wins over the channel's own setting, which is what makes a knob or a gate
        // able to switch an ornament in on top of whatever the patch says. ORN_CV means "only
        // when something maps one to me".
        int slot = ornForce[c];
        if (!slot) {
            if (ch->orn == ORN_OFF || ch->orn == ORN_CV) {
                ornCount[c] = 0; g_chOrnStep[c] = 0; continue;
            }
            slot = ch->orn;
        }

        Ornament *o = &p->orn[(slot - 1) % ORN_SLOTS];
        if (o->len == 0 || !g_chNoteOn[c]) { ornCount[c] = 0; g_chOrnStep[c] = 0; continue; }

        int32_t rate = clampi((int32_t)o->rate + (modOrnRate >> 5), 0, 7);
        if (++ornCount[c] >= ORN_RATE[rate]) {
            ornCount[c] = 0;
            uint8_t next = (uint8_t)(g_chOrnStep[c] + 1);
            if (next >= o->len) next = o->mode ? (uint8_t)(o->len - 1) : 0;
            g_chOrnStep[c] = next;
        }
        ornOffset[c] = o->step[g_chOrnStep[c] % ORN_STEPS];
    }

    // ---- per-voice pitch and portamento -----------------------------------------------------------
    // The quantiser snaps the TARGET; portamento then slides to it, so a scale still glides.
    int detune = (int)clampi((int32_t)p->detune + (modDetune >> 2), -128, 127);

    for (int c = 0; c < 4; c++) {
        int32_t t = base + modPitch[c];
        if (scaleIdx != 0) t = quantise((t + 128) >> 8, scaleIdx, keyIdx) << 8;
        t += (int32_t)p->ch[c].semi << 8;
        if (c == 1) t += (detune << 4);
        t = clampi(t, (int32_t)NOTE_MIN << 8, (int32_t)NOTE_MAX << 8);

        int32_t g = clampi((int32_t)p->ch[c].glide + (modGlide[c] >> 4), 0, 15);
        if (g == 0) {
            g_chPitch[c] = t;
        } else {
            int32_t step = (t - g_chPitch[c]) >> g;
            if (step == 0) g_chPitch[c] = t;      // without this the one-pole stalls short for ever
            else           g_chPitch[c] += step;
        }
    }

    // ---- levels -----------------------------------------------------------------------------------
    uint8_t maskL = 0, maskR = 0;
    for (int c = 0; c < 4; c++) {
        if (p->ch[c].pan & 1) maskL |= (uint8_t)(1u << c);
        if (p->ch[c].pan & 2) maskR |= (uint8_t)(1u << c);
    }

    for (int c = 0; c < 4; c++) {
        uint8_t vol = (uint8_t)(g_chEnv[c] >> 12);
        // NO FLOOR. Volume 0 means volume 0, in every envelope state.
        //
        // There used to be one, because a bare zero written to NRx2 switches the channel's DAC
        // off and the trigger block below was skipped at level 0, so an attack starting from
        // zero could never fire its trigger and stayed silent for ever. Both halves of that are
        // gone: psg_sq_voice and psg_noise_voice keep the DAC alive at volume 0 (see the note
        // over psg_sq_voice), and the trigger now fires on the note edge whatever the level is.
        //
        // The floor was not free. It began every attack with a plateau at volume 1 lasting a
        // sixteenth of the attack time - up to a third of a second - which on the noise channel
        // is inaudible, so a retriggered note seemed not to sound at all for seconds; and on the
        // wave channel it unmuted a near-flat, DC-offset wavetable, so silence ended in a click.

        // >>3, not >>4: at full depth that is the whole 0..15 span, so a knob mapped to LEVEL
        // really does run a channel from silent to full rather than nudging it by a quarter.
        int32_t lv  = clampi((int32_t)p->ch[c].level + (modLevel[c] >> 3), 0, 15);
        int32_t out = ((int32_t)vol * lv) / 15;              // constant divisor
        if (vol && lv && out == 0) out = 1;
        if (p->ch[c].pan == PAN_OFF || lv == 0) out = 0;
        g_chLevel[c] = (uint8_t)out;
    }

    // ---- write the voice ----------------------------------------------------------------------------
    int32_t pitch1 = g_chPitch[0] + ((int32_t)ornOffset[0] << 8);
    int32_t pitch2 = g_chPitch[1] + ((int32_t)ornOffset[1] << 8);
    int32_t pitch3 = g_chPitch[2] + ((int32_t)ornOffset[2] << 8);

    int duty0 = (int)clampi((int32_t)p->duty[0] + (modDuty[0] >> 6), 0, 3);
    int duty1 = (int)clampi((int32_t)p->duty[1] + (modDuty[1] >> 6), 0, 3);

    // Drum mode borrows channels 3 and 4 — the wavetable and the noise generator. BOTH SQUARES
    // STAY MELODIC, which is the better half of the machine to keep: two squares is a lead and a
    // bass, where a square and a wavetable is an awkward pair.
    int drumCh3 = p->drumMode && g_drumEnv[0];
    int drumCh4 = p->drumMode && g_drumEnv[1];

    psg_sq_voice(PSG_CH1, (uint8_t)duty0, g_chLevel[0]);
    psg_sq_period(PSG_CH1, period_for(pitch1, 0));

    psg_sq_voice(PSG_CH2, (uint8_t)duty1, g_chLevel[1]);
    psg_sq_period(PSG_CH2, period_for(pitch2, 0));

    // The wave channel, and the reason drums moved here: an arbitrary waveform means a kick has a
    // body rather than being a square with a fast decay.
    //
    // Its volume register only has four steps, which is far too coarse for percussion, so the
    // amplitude is applied by SCALING THE WAVETABLE instead — sixteen steps, eight halfword
    // writes, and only when the level actually changes. That is the trick every Game Boy tracker
    // uses on this channel.
    if (drumCh3) {
        uint8_t amp = (uint8_t)(((g_drumEnv[0] >> 12) * g_drumLvl[0]) / 15);
        if (amp != g_drumAmp) {
            g_drumAmp = amp;
            if (amp == 0) {
                psg_wave_voice(PSG_WAVE_MUTE);
            } else {
                psg_wave_load_scaled(psg_wave_preset[g_drumWave % PSG_WAVE_PRESETS], amp);
                psg_wave_voice(PSG_WAVE_100);
            }
        }
        psg_wave_period(period_for(g_drumPitch, 12));
    } else if (p->drumMode) {
        psg_wave_voice(PSG_WAVE_MUTE);
        g_drumAmp = 0xFF;
    } else {
        // SIXTEEN AMPLITUDE STEPS, NOT FOUR.
        //
        // SOUND3CNT_H offers only mute / 25 / 50 / 100 %, and mapping a 0-15 envelope onto that
        // makes most of the envelope invisible: a sustain of 13 never leaves the 100% band, so a
        // decay from full does nothing audible, and sustain 11 through 15 are the same value. The
        // release did move, but through three coarse jumps, which reads as stopping rather than
        // decaying.
        //
        // So the amplitude is applied by scaling the wavetable itself, exactly as the drum engine
        // already does. The channel sits at 100% of a scaled waveform instead of at one of four
        // volumes of a fixed one. Only reloaded when the level or the waveform actually changes.
        if (g_chLevel[2] != g_waveAmp || p->waveSel != g_waveSel) {
            g_waveAmp = g_chLevel[2];
            g_waveSel = p->waveSel;
            if (g_waveAmp == 0) {
                psg_wave_voice(PSG_WAVE_MUTE);
            } else {
                psg_wave_load_scaled(psg_wave_preset[p->waveSel % PSG_WAVE_PRESETS], g_waveAmp);
                psg_wave_voice(PSG_WAVE_100);
            }
        }
        psg_wave_period(period_for(pitch3, 12));       // wave is an octave down for the same n
    }

    // Leaving drum mode hands the wave channel back, so its melodic waveform has to be restored:
    // the drum engine has been overwriting wave RAM with scaled copies of a drum body.
    {
        static uint8_t lastDrumMode = 0;
        if (lastDrumMode && !p->drumMode) {
            psg_wave_load(psg_wave_preset[p->waveSel % PSG_WAVE_PRESETS]);
            g_drumAmp = 0xFF;
            g_waveAmp = 0xFF;      // the melodic side must reload too: wave RAM holds a drum body
        }
        lastDrumMode = p->drumMode;
    }

    if (drumCh4) {
        uint8_t v = (uint8_t)(((g_drumEnv[1] >> 12) * g_drumLvl[1]) / 15);
        psg_noise_set(p->noiseDiv, g_drumShift, g_drumWidth);
        psg_noise_voice(v);
    } else {
        int shift = (int)clampi((int32_t)p->noiseShift + (modNPitch >> 5), 0, 13);

        // THE NOISE CHANNEL CAN BE PITCHED, BUT ONLY IN OCTAVES.
        //
        // Its frequency is 524288 / r / 2^(s+1), so the shift field steps by a factor of two and
        // nothing finer: the eight divider ratios do subdivide an octave, but unevenly (roughly
        // 0, -3.9, -7.0, -9.7 semitones), so there is no honest chromatic mapping to be had.
        // Octave tracking is the whole of what this hardware offers, and it is what tuned noise
        // percussion has always meant on a Game Boy.
        //
        // Applied ONLY when a PITCH mapping actually ticks channel 4. Before this, g_chPitch[3]
        // was computed every tick and then never used, so ticking that box did nothing at all
        // and did it silently.
        if (pitchToNoise) {
            int32_t rel = (((g_chPitch[3] + 128) >> 8) - (int32_t)p->baseNote);
            int32_t oct = rel / 12;                       // constant divisor
            if (rel < 0 && (rel % 12)) oct--;             // floor, so octaves are even either side
            shift = (int)clampi(shift - oct, 0, 13);      // higher note, lower shift
        }

        psg_noise_set(p->noiseDiv, (uint8_t)shift, p->noiseWidth);
        psg_noise_voice(p->drumMode ? 0 : g_chLevel[3]);
    }

    // The sweep register is NOT written here. See the trigger block: it is armed on a note start
    // and zeroed for volume-only retriggers, because a trigger re-runs the sweep's overflow check
    // and an overflow disables the channel.
    int sweepShift = (int)clampi((int32_t)p->sweepShift + (modSweep >> 6), 0, 7);

    psg_master(p->masterL, p->masterR, p->ratio);

    // 0xC = channels 3 and 4: while drums are armed those two are audible whatever the mixer
    // says about them, because they are no longer the mixer's to silence.
    uint8_t enL = (uint8_t)(maskL | (p->drumMode ? 0xC : 0));
    uint8_t enR = (uint8_t)(maskR | (p->drumMode ? 0xC : 0));

    // MASTER VOLUME 0 IS NOT SILENCE ON THIS HARDWARE. SOUNDCNT_L scales by (vol+1)/8, so 0 is
    // one eighth rather than off — a volume control that cannot reach zero, which is not what
    // anyone means by turning it down. Drop the per-channel enables for that side instead, which
    // genuinely mutes it.
    if (p->masterL == 0) enL = 0;
    if (p->masterR == 0) enR = 0;

    psg_enable(enL, enR);

    // ---- trigger, AFTER the volume writes, AND on every volume CHANGE -----------------------------
    // THIS HARDWARE REALLY DOES NEED IT. Measured, not assumed: with the retriggers taken out
    // the envelope stopped moving altogether and notes never dropped to silence, because the
    // channel keeps playing at whatever volume was latched by its last trigger. So the DMG rule
    // holds on this console - an NRx2 volume write is only loaded into the channel BY A TRIGGER -
    // and software envelopes have to retrigger on every step. Only on a CHANGE, though: doing it
    // every tick would be a buzz rather than a note.
    //
    // What that costs, and what is done about it, because the cost is audible:
    //
    //   - a trigger resets the duty phase, so every envelope step is a small waveform
    //     discontinuity. Unavoidable while the step is real; 16 levels means at most a few dozen
    //     per note.
    //   - a trigger on CHANNEL 1 also re-arms the sweep AND RUNS ITS OVERFLOW CHECK, and an
    //     overflow DISABLES the channel on the spot. Retriggering at the control rate therefore
    //     had channel 1 switching itself off and back on continuously - crackle, not a click,
    //     and far louder than the phase reset. The sweep is now programmed ONLY on a note start
    //     and held at zero for volume-only retriggers, which is also where a sweep belongs
    //     musically. That is why channel 1 was the worst of the four.
    //   - a trigger on CHANNEL 4 reloads the noise LFSR. Retriggering makes the hiss repeat at
    //     the step rate, which is the price of a software envelope on that channel.
    //
    // Channel 3's level lives in wave RAM and applies immediately, so it is excluded.
    #define PSG_VOL_NEEDS_RETRIGGER 1
    {
        static uint8_t lastOn = 0;
        static uint8_t swArmed = 0;      // channel 1's sweep is armed once per note, see below
        static uint8_t lastVol[4] = { 0, 0, 0, 0 };
        static uint8_t lastDrum[2] = { 0, 0 };
        uint8_t on    = (uint8_t)(maskL | maskR);
        uint8_t fresh = (uint8_t)(on & ~lastOn);
        lastOn = on;

        for (int c = 0; c < 4; c++) {
            uint8_t bit = (uint8_t)(1u << c);
            if (p->drumMode && (c == 2 || c == 3)) continue;   // the drum engine triggers those
            int volMoved = PSG_VOL_NEEDS_RETRIGGER && (c != 2)
                        && (g_chLevel[c] != lastVol[c]);
            lastVol[c] = g_chLevel[c];

            int start = chEdge[c] || (fresh & bit);
            if (!(start || volMoved)) continue;
            // A note start triggers whatever the level is: with the DAC held alive at volume 0
            // the channel sits silent until the envelope lifts it, and the phase reset lands at
            // the start of the note, where it belongs. A volume-driven retrigger, if one is ever
            // re-enabled above, still needs something audible to be worth doing.
            if (!start && !g_chLevel[c]) continue;

            switch (c) {
            case 0:
                // Arm the configured sweep at the START of a note and nowhere else. Every other
                // trigger here is a volume step, and a trigger re-runs the sweep's overflow
                // check, which DISABLES the channel when it overflows - so leaving the sweep
                // programmed had channel 1 cutting in and out at the control rate.
                //
                // "Start" means the first trigger that can actually be heard, not the note edge:
                // with a slow attack the edge arrives while the level is still 0 and the DAC is
                // off, and a sweep armed there would be thrown away by the next volume step.
                //
                // A moving envelope still curtails a sweep, because the next volume retrigger
                // disarms it. Sweep and a software envelope are not fully compatible on this
                // hardware; a sustained level is where a sweep gets to run.
                if (start) swArmed = 0;
                if (!swArmed && g_chLevel[0]) {
                    psg_sq_sweep((uint8_t)sweepShift, p->sweepDir, p->sweepTime);
                    swArmed = 1;
                } else {
                    // SHIFT AND TIME TO ZERO, BUT NEVER THE DIRECTION BIT.
                    //
                    // Clearing the sweep's negate bit after even one negate-mode calculation has
                    // been made since the last trigger DISABLES THE CHANNEL IMMEDIATELY. It is a
                    // real, documented quirk and it is not a quiet one: the patch defaults to a
                    // downward sweep, a trigger with a non-zero shift performs a calculation on
                    // the spot, and the very next volume step wrote a bare zero here - so
                    // channel 1 would go silent mid-note while every screen still showed it
                    // playing. Setting the depth to zero cured it, which is the tell: with no
                    // shift no calculation is made and there is nothing to clear.
                    psg_sq_sweep(0, p->sweepDir, 0);
                }
                psg_sq_trigger(PSG_CH1, period_for(pitch1, 0));
                break;
            case 1: psg_sq_trigger(PSG_CH2, period_for(pitch2, 0)); break;
            case 2: psg_wave_trigger(period_for(pitch3, 12)); break;
            default: psg_noise_trigger(); break;
            }
        }

        if (p->drumMode) {
            // The wave channel needs a trigger only when a drum STARTS: its level lives in
            // wave RAM and in SOUND3CNT_H, both of which apply immediately, so unlike the
            // squares it does not need retriggering on every volume step. The NOISE drum does -
            // same rule as the melodic channels, or its decay never happens.
            uint8_t v1 = (uint8_t)(((g_drumEnv[1] >> 12) * g_drumLvl[1]) / 15);
            if (v1 && v1 != lastDrum[1]) psg_noise_trigger();
            lastDrum[1] = v1;
        }
    }

    // ---- publish upstream ---------------------------------------------------------------------------
    {
        static uint8_t lastNote = 0xFF, lastGate = 0xFF;
        uint8_t loudest = 0;
        for (int c = 0; c < 4; c++) if (g_chLevel[c] > loudest) loudest = g_chLevel[c];
        uint8_t g = (uint8_t)(g_anyNoteOn ? (loudest ? loudest : 1) : 0);
        if (note != lastNote || g != lastGate) {
            lastNote = note; lastGate = g;
            link_post_note(note, g);
        }
    }
}

void synth_update(void)
{
    static uint16_t last = 0;
    static int primed = 0;
    if (!primed) { last = REG_TM0CNT_L; primed = 1; }

    uint16_t now = REG_TM0CNT_L;
    // Bounded catch-up: after a long stall we do NOT run every owed tick, or the envelope lurches
    // and the glide jumps.
    int budget = 8;
    while ((uint16_t)(now - last) >= 16 && budget--) {
        last = (uint16_t)(last + 16);
        synth_tick();
    }
    if ((uint16_t)(now - last) >= 16) last = now;
}
