// synth.h — the instrument: pitch, modulation, per-channel envelopes, ornaments, drums.
//
// Everything musical lives on this side of the link. The Workshop streams seven raw sources and
// a switch position; what they DO is decided here, which is why the whole editor costs no
// protocol and a patch is a struct rather than a conversation.
#pragma once

#include <stdint.h>

// ---- modulation sources, in MAP-page order (matches g_in[] then the knobs) ----
#define SRC_CV1   0
#define SRC_CV2   1
#define SRC_AUD1  2
#define SRC_AUD2  3
#define SRC_MAIN  4
#define SRC_X     5
#define SRC_Y     6
// The Workshop's three-way switch. DOWN is already a trigger source (the SW column of the TRIG
// grid), so this reads as "how far UP the switch is" rather than as a bipolar three-way: UP is
// full, MIDDLE and DOWN are zero. Giving DOWN a modulation value too would mean every trigger
// also yanked whatever the switch was mapped to.
#define SRC_SW    7
#define SRC_COUNT 8

extern const char *src_name[SRC_COUNT];

// ---- modulation destinations ----
// Those marked per-voice are routed through the mod slot's channel mask, so one CV can raise the
// level of channels 1 and 3 and leave 2 and 4 alone. The rest address hardware that exists once
// (channel 1's sweep, channel 2's detune partner, the noise generator) or a global musical
// setting, and ignore the mask.
#define DEST_NONE     0
#define DEST_PITCH    1    // per-voice
#define DEST_LEVEL    2    // per-voice
#define DEST_DUTY     3    // per-voice (channels 1 and 2 only)
#define DEST_DETUNE   4    // channel 2
#define DEST_GLIDE    5    // per-voice
#define DEST_DECAY    6    // per-voice
#define DEST_SWEEP    7    // channel 1
#define DEST_NPITCH   8    // channel 4
#define DEST_ORN      9    // per-voice: picks the slot for channels set to ORN_CV
#define DEST_ORNRATE 10
#define DEST_SCALE   11    // global: walks the scale list
#define DEST_KEY     12    // global: transposes the root
#define DEST_ATTACK  13    // per-voice
#define DEST_RELEASE 14    // per-voice
#define DEST_COUNT   15

extern const char *dest_name[DEST_COUNT];

// ---- trigger sources: the columns of the TRIG pin grid ----
#define TRIG_PU2   0
#define TRIG_SW    1
#define TRIG_BTN   2
#define TRIG_COUNT 3

extern const char *trig_name[TRIG_COUNT];

// ---- GBA button actions (BTN page) ----
#define ACT_NONE      0
#define ACT_TRIGGER   1
#define ACT_HOLD      2
#define ACT_OCT_UP    3
#define ACT_OCT_DN    4
#define ACT_DETUNE_UP 5
#define ACT_DETUNE_DN 6
#define ACT_DUTY1     7
#define ACT_DUTY2     8
#define ACT_CH1       9
#define ACT_CH2      10
#define ACT_CH3      11
#define ACT_CH4      12
#define ACT_ORN_UP   13
#define ACT_ORN_DN   14
#define ACT_SEMI_UP  15
#define ACT_SEMI_DN  16
// Appended rather than inserted, so a stored patch's button indices keep their meaning.
#define ACT_DUTY_BOTH 17   // cycles both squares together
#define ACT_SWEEP     18   // cycles channel 1's sweep time
#define ACT_ORN1      19   // select ornament slot n on the melodic pair (channels 1 and 2)
#define ACT_ORN2      20
#define ACT_ORN3      21
#define ACT_ORN4      22
#define ACT_ORN5      23
#define ACT_ORN6      24
#define ACT_COUNT     25

extern const char *act_name[ACT_COUNT];

#define BTN_SLOTS 8        // A, B, L, R, Up, Down, Left, Right
extern const char *btn_name[BTN_SLOTS];

// ---- ornaments ----
// Six slots of sixteen semitone offsets. Six rather than eight so the whole patch stays inside
// one 256-byte storage slot — see the size assertion in synth.c.
#define ORN_SLOTS 6
#define ORN_STEPS 16
#define ORN_OFF   0
#define ORN_CV    (ORN_SLOTS + 1)   // slot chosen live by a DEST_ORN modulation

typedef struct {
    uint8_t len;                 // steps in use, 0 = slot empty
    uint8_t rate;                // 0..7
    uint8_t mode;                // 0 = loop while held, 1 = one-shot then hold last
    uint8_t pad;
    int8_t  step[ORN_STEPS];
} Ornament;

// ---- channel pan / enable ----
#define PAN_OFF  0
#define PAN_L    1
#define PAN_R    2
#define PAN_BOTH 3
extern const char *pan_name[4];

typedef struct {
    uint8_t pan;
    uint8_t level;               // 0..15 mixer level, scales the envelope output
    int8_t  semi;
    uint8_t atk, dec, sus, rel;
    uint8_t glide;               // per-voice portamento
    uint8_t trig;                // bitmask over TRIG_*
    uint8_t orn;                 // ORN_OFF, 1..ORN_SLOTS, or ORN_CV
} Channel;

typedef struct {
    uint8_t dest;
    int8_t  depth;               // -64..+63. For DEST_PITCH, +32 is unity 1V/oct.
    uint8_t chMask;              // which voices this slot reaches, bit per channel
} ModSlot;

// ---- scales ----
// Fifteen built in, then four the user builds note by note on the SET page.
#define SCALE_BUILTIN 15
#define SCALE_USER    4
#define SCALE_COUNT   (SCALE_BUILTIN + SCALE_USER)
extern const char *scale_name[SCALE_COUNT];
extern const char *key_name[12];

// ---- drums ----
// A drum fires when an input goes HIGH, so any jack carrying a trigger, a gate, or simply a loud
// enough signal becomes a drum pad. The sounds are synthesised on the PSG rather than sampled:
// the GBA's Direct Sound channels would need DMA, a timer and sample data in an already large
// payload, and a synthesised kick is editable in a way a sample is not.
//
// Pitched drums live on CHANNEL 3, the wavetable channel, and noise drums on channel 4. The wave
// channel is the right home for them: it plays an arbitrary waveform, so a kick has a body
// instead of being a square, and its period sweeps like the squares do. Its one weakness — four
// volume steps — is sidestepped by scaling the wavetable itself (see psg_wave_load_scaled).
//
// That leaves BOTH squares free for melody while drums play, which is the better split anyway.
#define DRUM_SRC_AUD1  0
#define DRUM_SRC_AUD2  1
#define DRUM_SRC_CV1   2
#define DRUM_SRC_CV2   3
#define DRUM_SRC_PU2   4
#define DRUM_SRC_SW    5
#define DRUM_SRC_COUNT 6
extern const char *drum_src_name[DRUM_SRC_COUNT];

#define DRUM_PRESETS 11    // index 0 is OFF
extern const char *drum_name[DRUM_PRESETS];

// ---- the patch ----
// Stored verbatim in one 256-byte flash slot on the Workshop. The magic and version come first
// so an erased slot (all 0xFF) can never be mistaken for a patch.
#define PATCH_MAGIC   0x4742u      // 'GB'
// BUMP THIS WHENEVER A STORED FIELD CHANGES ITS LAYOUT *OR* ITS MEANING. Either way an old slot
// read as a current Patch is not merely wrong, it is silently wrong, so the version is checked
// rather than assumed.
//
//   v2  added SRC_SW, which grew mod[] and shifted every field after it. A LAYOUT change, and
//       those cannot be recovered from the bytes alone - synth_patch_valid() rejects them.
//   v3  cvScale went from counts per semitone in 1/16ths to 1/256ths. Same layout, different
//       meaning, so synth_patch_migrate() brings it forward instead of throwing it away.
#define PATCH_VERSION 3

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  pad0;

    Channel  ch[4];
    uint8_t  duty[2];
    int8_t   detune;
    uint8_t  waveSel;
    uint8_t  noiseDiv, noiseShift, noiseWidth, retrig;
    uint8_t  sweepTime, sweepDir, sweepShift;
    int8_t   octave;
    uint8_t  masterL, masterR, ratio, baseNote;
    // cvScale is COUNTS PER SEMITONE IN 1/256ths (Q8) - see synth_default_patch() for why that
    // precision, and refresh_cv_recip() for the reciprocal it feeds. cvOffset is plain counts.
    int16_t  cvScale, cvOffset;

    ModSlot  mod[SRC_COUNT];
    uint8_t  pad1;
    uint8_t  btnAct[BTN_SLOTS];

    int8_t   tuneCents;
    uint8_t  key;
    uint8_t  scale;
    uint8_t  drumMode;
    uint8_t  drumThresh;                 // 0..15, how high "high" has to be
    uint8_t  drumMap[DRUM_SRC_COUNT];    // preset per input, 0 = off
    uint8_t  pad2;

    uint16_t userScale[SCALE_USER];      // 12-bit degree masks
    Ornament orn[ORN_SLOTS];
} Patch;

extern Patch g_patch;

// ---- live voice state, for the UI ----
extern int32_t  g_pitchQ8;
extern int32_t  g_chPitch[4];
extern uint8_t  g_note;
extern uint8_t  g_hold;
extern uint16_t g_chEnv[4];
extern uint8_t  g_chLevel[4];
extern uint8_t  g_chNoteOn[4];
extern uint8_t  g_chOrnStep[4];
extern uint8_t  g_anyNoteOn;
extern uint8_t  g_btnTrigHeld;
extern uint8_t  g_drumHit[DRUM_SRC_COUNT];   // counts down after a hit; lights the DRUM page

extern const uint16_t ENV_MS[16];

void synth_init(void);
void synth_update(void);
void synth_default_patch(void);       // reset to the factory patch
int  synth_patch_bytes(void);         // sizeof(Patch), for the transfer
int  synth_patch_valid(const Patch *p);
void synth_patch_migrate(Patch *p);   // bring an older-but-compatible patch up to date, in place
void synth_patch_applied(void);       // re-derive anything cached from the patch

// Button state, sampled at the control rate; the UI drains the latches at frame rate.
extern uint16_t g_btn;
extern volatile uint16_t g_btnEdgeLatch;
extern volatile uint16_t g_btnStepLatch;
uint16_t synth_take_edges(void);
uint16_t synth_take_steps(void);

extern uint8_t g_editMode;
