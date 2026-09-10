// ui.c — the screen.
//
// ─────────────────────────────────────────────────────────────────────────────────────────────
// THE FLICKER RULE: NEVER ERASE THEN DRAW.
//
// Every flickering region in this UI had the same shape — clear a box to the background, then
// paint the content into it, once per frame. The eye sees the gap. Two rules fix it everywhere:
//
//   1. Anything that changes continuously (meters, faders, the envelope level tick) is painted
//      in ONE pass, overwriting: background segment, then value segment. Same pixels touched, no
//      moment where the area is blank.
//   2. Anything textual is compared against what is already on screen and skipped when it has
//      not changed. A rare erase-and-redraw is invisible; a 60 Hz one is a strobe.
//
// Static legends are drawn ONCE in edit_static(), never per frame.
// ─────────────────────────────────────────────────────────────────────────────────────────────
//
// THE EDITOR OWNS EVERY BUTTON. Inside a menu no key carries its performance meaning — not
// trigger, not hold, not the mapped D-pad actions. See the button loop in synth.c.
//
//   D-pad alone           move the cursor (row, and on some pages a column)
//   A + Up/Down           change the value
//   A + Left/Right        change the value coarsely, or the second field on two-field rows
//   SELECT + Left/Right   change page
// TRIG is the exception: a cell there is one bit, so A on its own toggles it.

#include "ui.h"
#include "gfx.h"
#include "link.h"
#include "synth.h"
#include "psg.h"

#define KEY_A      (1u << 0)
#define KEY_B      (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START  (1u << 3)
#define KEY_RIGHT  (1u << 4)
#define KEY_LEFT   (1u << 5)
#define KEY_UP     (1u << 6)
#define KEY_DOWN   (1u << 7)

uint8_t g_page = PAGE_MEM;

static uint8_t g_row     = 0;
static uint8_t g_scroll  = 0;
static uint8_t g_selCh   = 0;      // channel cursor, shared by VOICE / MIX / ENV / TRIG
static uint8_t g_ornSlot = 0;
static uint8_t g_ornStep = 0;
static uint8_t g_trigCol = 0;
static uint8_t g_mapCol  = 0;      // MAP column cursor: 0 dest, 1 depth, 2..5 channel ticks
static uint8_t g_degCol  = 0;      // degree cursor for the user-scale editor
static uint8_t g_slot    = 0;      // patch slot
static uint8_t g_repaint = 1;
static uint8_t g_lastMode = 0xFF;
static uint8_t g_navBlank = 0;      // stepping pages with SELECT held: body blanked, tabs live
static int     g_tabPage  = -1;

// Eleven tabs across 240 px. "CHAN" rather than "VOICE" because the page is per-channel and the
// shorter word is what makes the row fit without abbreviating the rest into noise.
static const char *page_tab[PAGE_COUNT] = {
    "MEM", "CHAN", "TRIG", "ENV", "MIX", "BTN", "MAP", "ORN", "DRUM", "CAL", "SET"
};

static const char *NOTE_NAME[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char *DUTY_NAME[4]  = { "12.5%", "25%", "50%", "75%" };
static const char *RATIO_NAME[4] = { "25%", "50%", "100%", "100%" };
static const char *ONOFF[2]      = { "OFF", "ON" };
static const char *ORNMODE[2]    = { "LOOP", "ONCE" };

// ---- CHAN page: all four channels side by side ------------------------------------------------
// A table, not a list with a channel selector. The question you actually ask while building a
// sound is almost always "what are the other three doing", and answering it by stepping a
// selector and re-reading the same rows is the slow way round. Rows are parameters, columns are
// channels, and a cell the channel does not have simply reads "-".
//
// Channel 1 is the only one with a hardware frequency sweep, channel 2 the only one with a detune
// partner, channel 3 the only one with a wavetable, and channel 4 is not pitched at all — which
// is why the table is sparse rather than square.
#define CF_OUTPUT   0
#define CF_SEMI     1
#define CF_ORN      2
#define CF_TIMBRE   3
#define CF_DETUNE   4
#define CF_NPITCH   5
#define CF_NRATIO   6
#define CF_SWTIME   7
#define CF_SWDIR    8
#define CF_SWDEPTH  9
#define CF_ROWS    10

static const char *CF_LABEL[CF_ROWS] = {
    "OUTPUT", "SEMITONE", "ORNAMENT", "TIMBRE", "DETUNE",
    "N PITCH", "N RATIO", "SWP TIME", "SWP DIR", "SWP DEPTH"
};

// Which channels each row applies to, one bit per channel.
static const uint8_t CF_CHANS[CF_ROWS] = {
    0xF,   // OUTPUT     every channel
    0x7,   // SEMITONE   the pitched three
    0x7,   // ORNAMENT   the pitched three
    0xF,   // TIMBRE     duty, duty, waveform, noise type: one idea, four spellings
    0x2,   // DETUNE     channel 2 only
    0x8,   // N PITCH    channel 4 only
    0x8,   // N RATIO    channel 4 only
    0x1,   // SWP TIME   channel 1 only
    0x1,   // SWP DIR    channel 1 only
    0x1,   // SWP DEPTH  channel 1 only
};

// ---- helpers ---------------------------------------------------------------------------------
static int scopy(char *dst, int at, const char *src)
{
    while (*src) dst[at++] = *src++;
    dst[at] = 0;
    return at;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void note_text(char *buf, int note)
{
    int n   = (int)clampi(note, 0, 127);
    int oct = (n / 12) - 1;                  // constant divisors: no libcall
    int at  = scopy(buf, 0, NOTE_NAME[n % 12]);
    if (oct < 0) { buf[at++] = '-'; oct = -oct; }
    dec_at(buf, at, (uint32_t)oct);
}

static void ch_text(char *buf, int c)
{
    dec_at(buf, scopy(buf, 0, "CH "), (uint32_t)c + 1);
}

static void env_time_text(char *buf, int idx)
{
    uint16_t ms = ENV_MS[idx & 15];
    if (ms == 0) { scopy(buf, 0, "INSTANT"); return; }
    if (ms < 1000) {
        scopy(buf, dec_at(buf, 0, ms), " MS");
    } else {
        int at = dec_at(buf, 0, ms / 1000);            // 2500 ms -> "2.5 S"
        at = scopy(buf, at, ".");
        at = dec_at(buf, at, (ms / 100) % 10);
        scopy(buf, at, " S");
    }
}

// ---- layout ----------------------------------------------------------------------------------
#define TAB_Y     3
#define LIST_TOP  22
#define LIST_ROW  11
#define LIST_MAX  10
#define VAL_X    150
#define HINT_Y   150

// MAP carries column headings above its rows, which need somewhere to live that is not on top
// of the tab bar. It is the only page with that problem, so it is the only page that moves.
static int list_top(int page)
{
    return (page == PAGE_MAP) ? LIST_TOP + 8 : LIST_TOP;
}

static int page_rows(int page)
{
    switch (page) {
    case PAGE_CHAN: return CF_ROWS;
    case PAGE_MIX:   return 4;
    case PAGE_ENV:   return 7;
    case PAGE_TRIG:  return 4;
    case PAGE_MAP:   return SRC_COUNT;
    case PAGE_BTN:   return BTN_SLOTS;
    case PAGE_ORN:   return 5;                    // SLOT, LENGTH, RATE, MODE, STEPS
    case PAGE_DRUM:  return 2 + DRUM_SRC_COUNT;   // MODE, THRESHOLD, then one row per input
    case PAGE_MEM:   return 1;                    // the grid is the whole page
    case PAGE_CAL:   return 8;
    default:         return 5;                    // SET
    }
}

static int visible_rows(int page)
{
    int r = page_rows(page);
    if (page == PAGE_ENV) return r;
    if (page == PAGE_ORN) return r;
    if (page == PAGE_SET) return r;
    if (page == PAGE_MEM) return r;
    return r < LIST_MAX ? r : LIST_MAX;
}

// ---- labels ------------------------------------------------------------------------------------
static const char *field_label(int page, int row)
{
    switch (page) {

    case PAGE_ENV:
        switch (row) {
        case 0: return "CHANNEL";
        case 1: return "ATTACK";
        case 2: return "DECAY";
        case 3: return "SUSTAIN";
        case 4: return "RELEASE";
        case 5: return "PORTAMENTO";
        default: return "RETRIGGER";
        }
    case PAGE_MAP: return src_name[row % SRC_COUNT];
    case PAGE_BTN: return btn_name[row % BTN_SLOTS];
    case PAGE_ORN:
        switch (row) {
        case 0: return "SLOT";
        case 1: return "LENGTH";
        case 2: return "RATE";
        case 3: return "MODE";
        default: return "STEPS";
        }
    case PAGE_DRUM:
        switch (row) {
        case 0: return "DRUM MODE";
        case 1: return "THRESHOLD";
        default: return drum_src_name[(row - 2) % DRUM_SRC_COUNT];
        }
    case PAGE_CAL:
        switch (row) {
        case 0: return "CV SCALE";
        case 1: return "CV OFFSET";
        case 2: return "BASE NOTE";
        case 3: return "MASTER L";
        case 4: return "MASTER R";
        case 5: return "PSG LEVEL";
        case 6: return "CV 2 IN";
        default: return "LINK";
        }
    case PAGE_MEM:
        switch (row) {
        case 0: return "SLOT";
        case 1: return "SAVE TO SLOT";
        default: return "LOAD FROM SLOT";
        }
    default:      // SET
        switch (row) {
        case 0: return "TUNING";
        case 1: return "KEY";
        case 2: return "SCALE";
        case 3: return "OCTAVE";
        default: return "SCALE NOTES";
        }
    }
}

// ---- values -------------------------------------------------------------------------------------
// Cell text, kept short: four columns across 240 px leaves about seven characters each.
static void chan_cell(int row, int c, char *buf)
{
    Patch   *p  = &g_patch;
    Channel *ch = &p->ch[c & 3];
    buf[0] = 0;

    // [X] rather than a dash: a dash reads as "zero" or "not set yet", where the truth is that
    // this channel has no such control at all and never will.
    if (!(CF_CHANS[row] & (1u << c))) { scopy(buf, 0, "[X]"); return; }

    switch (row) {
    case CF_OUTPUT: scopy(buf, 0, pan_name[ch->pan & 3]); return;
    case CF_SEMI:   sdec32(buf, ch->semi, 3); return;
    case CF_ORN:
        if (ch->orn == ORN_OFF) { scopy(buf, 0, "OFF"); return; }
        if (ch->orn == ORN_CV)  { scopy(buf, 0, "MAP"); return; }
        dec_at(buf, scopy(buf, 0, "SL"), (uint32_t)ch->orn);
        return;
    case CF_TIMBRE:
        if (c < 2)       scopy(buf, 0, DUTY_NAME[p->duty[c] & 3]);
        else if (c == 2) scopy(buf, 0, psg_wave_name[p->waveSel % PSG_WAVE_PRESETS]);
        else             scopy(buf, 0, p->noiseWidth ? "7 BIT" : "15 BIT");
        return;
    case CF_DETUNE:  sdec32(buf, p->detune, 3); return;
    case CF_NPITCH:  dec_at(buf, 0, p->noiseShift); return;
    case CF_NRATIO:  dec_at(buf, 0, p->noiseDiv); return;
    case CF_SWTIME:  dec_at(buf, 0, p->sweepTime); return;
    case CF_SWDIR:   scopy(buf, 0, p->sweepDir ? "DOWN" : "UP"); return;
    default:         dec_at(buf, 0, p->sweepShift); return;
    }
}

static void chan_adjust(int row, int c, int delta)
{
    Patch   *p  = &g_patch;
    Channel *ch = &p->ch[c & 3];

    if (!(CF_CHANS[row] & (1u << c))) return;      // this channel has no such control

    switch (row) {
    case CF_OUTPUT: ch->pan  = (uint8_t)clampi(ch->pan + delta, 0, 3); return;
    case CF_SEMI:   ch->semi = (int8_t)clampi(ch->semi + delta, -24, 24); return;
    case CF_ORN:    ch->orn  = (uint8_t)clampi(ch->orn + delta, ORN_OFF, ORN_CV); return;
    case CF_TIMBRE:
        if (c < 2) p->duty[c] = (uint8_t)clampi(p->duty[c] + delta, 0, 3);
        else if (c == 2) {
            p->waveSel = (uint8_t)clampi(p->waveSel + delta, 0, PSG_WAVE_PRESETS - 1);
            psg_wave_load(psg_wave_preset[p->waveSel]);
        } else p->noiseWidth = (uint8_t)(delta > 0);
        return;
    case CF_DETUNE:  p->detune     = (int8_t)clampi(p->detune + delta, -64, 63); return;
    case CF_NPITCH:  p->noiseShift = (uint8_t)clampi(p->noiseShift + delta, 0, 13); return;
    case CF_NRATIO:  p->noiseDiv   = (uint8_t)clampi(p->noiseDiv + delta, 0, 7); return;
    case CF_SWTIME:  p->sweepTime  = (uint8_t)clampi(p->sweepTime + delta, 0, 7); return;
    case CF_SWDIR:   p->sweepDir   = (uint8_t)(delta > 0); return;
    default:         p->sweepShift = (uint8_t)clampi(p->sweepShift + delta, 0, 7); return;
    }
}

// The mapping depth column shows a SLOT for an ornament mapping, because there the depth field
// names the ornament rather than scaling anything — an ornament is on or it is off.
static void map_depth_text(char *buf, const ModSlot *m)
{
    if (m->dest == DEST_ORN) dec_at(buf, scopy(buf, 0, "SL"), (uint32_t)clampi(m->depth, 1, ORN_SLOTS));
    else                     sdec32(buf, m->depth, 4);
}

static void field_value(int page, int row, char *buf)
{
    Patch   *p  = &g_patch;
    Channel *ch = &p->ch[g_selCh & 3];
    buf[0] = 0;

    switch (page) {

    case PAGE_ENV:
        switch (row) {
        case 0: ch_text(buf, g_selCh); return;
        case 1: env_time_text(buf, ch->atk); return;
        case 2: env_time_text(buf, ch->dec); return;
        case 3: dec32(buf, ch->sus, 3); return;
        case 4: env_time_text(buf, ch->rel); return;
        case 5: dec32(buf, ch->glide, 3); return;
        default: scopy(buf, 0, ONOFF[p->retrig ? 1 : 0]); return;
        }
    case PAGE_MAP: {
        // Comparison string only; edit_list paints the four columns itself.
        ModSlot *m  = &p->mod[row % SRC_COUNT];
        int      at = scopy(buf, 0, dest_name[m->dest % DEST_COUNT]);
        char d[10];
        map_depth_text(d, m);
        at = scopy(buf, at, d);
        at = dec_at(buf, at, m->chMask);
        // The column cursor only changes how the SELECTED row looks. Folding it into every row's
        // comparison string made a left/right press redraw all seven — the whole table flickering
        // for a one-cell move.
        if (row == g_row) dec_at(buf, at, g_mapCol);
        return;
    }
    case PAGE_BTN: scopy(buf, 0, act_name[p->btnAct[row % BTN_SLOTS] % ACT_COUNT]); return;
    case PAGE_ORN: {
        Ornament *o = &p->orn[g_ornSlot % ORN_SLOTS];
        switch (row) {
        case 0: dec_at(buf, 0, (uint32_t)g_ornSlot + 1); return;
        case 1: dec32(buf, o->len, 2); return;
        case 2: dec32(buf, o->rate, 2); return;
        case 3: scopy(buf, 0, ORNMODE[o->mode ? 1 : 0]); return;
        default: {
            int at = dec_at(buf, 0, (uint32_t)g_ornStep + 1);
            at = scopy(buf, at, ":");
            char v[8];
            sdec32(v, o->step[g_ornStep % ORN_STEPS], 4);
            scopy(buf, at, v);
            return;
        }
        }
    }
    case PAGE_DRUM:
        switch (row) {
        case 0: scopy(buf, 0, ONOFF[p->drumMode ? 1 : 0]); return;
        case 1: dec32(buf, p->drumThresh, 3); return;
        default: scopy(buf, 0, drum_name[p->drumMap[(row - 2) % DRUM_SRC_COUNT] % DRUM_PRESETS]);
                 return;
        }
    case PAGE_CAL:
        switch (row) {
        case 0: dec32(buf, (uint32_t)p->cvScale, 5); return;
        case 1: sdec32(buf, p->cvOffset, 5); return;
        case 2: note_text(buf, p->baseNote); return;
        case 3: dec32(buf, p->masterL, 3); return;
        case 4: dec32(buf, p->masterR, 3); return;
        case 5: scopy(buf, 0, RATIO_NAME[p->ratio & 3]); return;
        // THE LIVE COUNT, WHICH IS WHAT MAKES THIS PAGE A CALIBRATION PAGE RATHER THAN A GUESS.
        // It is exactly the `raw` that the pitch maths works on, so two readings a known interval
        // apart give CV SCALE directly: scale = 16 * (high - low) / semitones between them.
        // Without it the only way to trim 1V/oct was by ear, one press at a time.
        case 6: sdec32(buf, (int32_t)g_in[SRC_CV2] - 2048, 6); return;
        default: scopy(buf, 0, link_irq_alive() ? "IRQ" : "POLLED"); return;
        }
    case PAGE_MEM:
        switch (row) {
        case 0: {
            int at = dec_at(buf, 0, (uint32_t)g_slot + 1);
            scopy(buf, at, (g_slotMask >> g_slot) & 1 ? "  USED" : "  FREE");
            return;
        }
        default:
            if (g_xferState == LINK_XFER_SAVE) { scopy(buf, 0, "SAVING..."); return; }
            if (g_xferState == LINK_XFER_LOAD) { scopy(buf, 0, "LOADING..."); return; }
            scopy(buf, 0, "A+UP");
            return;
        }
    default:      // SET
        switch (row) {
        case 0: sdec32(buf, p->tuneCents, 4); return;
        case 1: scopy(buf, 0, key_name[p->key % 12]); return;
        case 2: scopy(buf, 0, scale_name[p->scale % SCALE_COUNT]); return;
        case 3: sdec32(buf, p->octave, 3); return;
        default:
            if (p->scale < SCALE_BUILTIN) { scopy(buf, 0, "BUILT IN"); return; }
            dec_at(buf, scopy(buf, 0, "DEGREE "), (uint32_t)g_degCol);
            return;
        }
    }
}

static void field_adjust(int page, int row, int delta)
{
    Patch   *p  = &g_patch;
    Channel *ch = &p->ch[g_selCh & 3];

    switch (page) {

    case PAGE_ENV:
        switch (row) {
        case 0: g_selCh = (uint8_t)clampi(g_selCh + delta, 0, 3); return;
        case 1: ch->atk = (uint8_t)clampi(ch->atk + delta, 0, 15); return;
        case 2: ch->dec = (uint8_t)clampi(ch->dec + delta, 0, 15); return;
        case 3: ch->sus = (uint8_t)clampi(ch->sus + delta, 0, 15); return;
        case 4: ch->rel = (uint8_t)clampi(ch->rel + delta, 0, 15); return;
        case 5: ch->glide = (uint8_t)clampi(ch->glide + delta, 0, 15); return;
        default: p->retrig = (uint8_t)(delta > 0); return;
        }
    case PAGE_BTN: {
        int b = row % BTN_SLOTS;
        p->btnAct[b] = (uint8_t)clampi(p->btnAct[b] + delta, 0, ACT_COUNT - 1);
        return;
    }
    case PAGE_ORN: {
        Ornament *o = &p->orn[g_ornSlot % ORN_SLOTS];
        switch (row) {
        case 0: g_ornSlot = (uint8_t)clampi(g_ornSlot + delta, 0, ORN_SLOTS - 1); return;
        case 1: o->len  = (uint8_t)clampi(o->len + delta, 0, ORN_STEPS); return;
        case 2: o->rate = (uint8_t)clampi(o->rate + delta, 0, 7); return;
        case 3: o->mode = (uint8_t)(delta > 0); return;
        default: return;
        }
    }
    case PAGE_DRUM:
        switch (row) {
        case 0: p->drumMode = (uint8_t)(delta > 0); return;
        case 1: p->drumThresh = (uint8_t)clampi(p->drumThresh + delta, 0, 15); return;
        default: {
            int i = (row - 2) % DRUM_SRC_COUNT;
            p->drumMap[i] = (uint8_t)clampi(p->drumMap[i] + delta, 0, DRUM_PRESETS - 1);
            return;
        }
        }
    case PAGE_CAL:
        switch (row) {
        case 0: {
            // COARSE IS x8 HERE AND NOWHERE ELSE. cvScale is Q8, so A+Up/Down's step of 1 is
            // 1/256 of a count per semitone - which is the whole point of the precision, but it
            // would also make a first calibration hundreds of presses. A+Left/Right therefore
            // moves by 64, a little under one step of the old Q4 unit.
            int d = (delta == 8 || delta == -8) ? delta * 8 : delta;
            p->cvScale = (int16_t)clampi(p->cvScale + d, 1024, 32767);
            return;
        }
        case 1: p->cvOffset = (int16_t)clampi(p->cvOffset + delta, -2048, 2047); return;
        case 2: p->baseNote = (uint8_t)clampi(p->baseNote + delta, 0, 120); return;
        case 3: p->masterL  = (uint8_t)clampi(p->masterL + delta, 0, 7); return;
        case 4: p->masterR  = (uint8_t)clampi(p->masterR + delta, 0, 7); return;
        case 5: p->ratio    = (uint8_t)clampi(p->ratio + delta, 0, 2); return;
        default: return;
        }
    case PAGE_MEM:
        switch (row) {
        case 0: g_slot = (uint8_t)clampi(g_slot + delta, 0, GBA_PATCH_SLOTS - 1); return;
        case 1: if (delta > 0 && g_xferState == LINK_XFER_NONE)
                    link_begin_save(g_slot, (const uint8_t *)&g_patch, synth_patch_bytes());
                return;
        default: if (delta > 0 && g_xferState == LINK_XFER_NONE) link_begin_load(g_slot);
                return;
        }
    case PAGE_SET:
        switch (row) {
        case 0: p->tuneCents = (int8_t)clampi(p->tuneCents + delta, -50, 50); return;
        case 1: p->key   = (uint8_t)clampi(p->key + delta, 0, 11); return;
        case 2: p->scale = (uint8_t)clampi(p->scale + delta, 0, SCALE_COUNT - 1); return;
        case 3: p->octave = (int8_t)clampi(p->octave + delta, -3, 3); return;
        default: return;
        }
    default: return;
    }
}

// ---- PLAY view -----------------------------------------------------------------------------------
#define PLAY_NOTE_Y 26
#define PLAY_CH_Y   72
#define PLAY_IN_Y   112

static void play_static(void)
{
    rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    text_centre(4, "MTM WORKSHOP COMPUTER  //  GBA PSG VOICE", COL_TITLE, 1);
    rect(14, 16, SCREEN_W - 28, 1, COL_DIM);

    static const char *chn[4] = { "SQ1", "SQ2", "WAV", "NSE" };
    for (int i = 0; i < 4; i++) text(14, PLAY_CH_Y + i * 9, chn[i], COL_DIM, 1);

    static const char *inn[7] = { "CV1", "CV2", "AU1", "AU2", "MAIN", "X", "Y" };
    for (int i = 0; i < 7; i++) text(10 + i * 33, PLAY_IN_Y, inn[i], COL_DIM, 1);

    text(14, HINT_Y, "START EDIT", COL_DIM, 1);
}

static void play_frame(void)
{
    char buf[16];

    static uint8_t lastNote = 0xFF, lastOn = 0xFF;
    if (g_note != lastNote || g_anyNoteOn != lastOn) {
        lastNote = g_note; lastOn = g_anyNoteOn;
        note_text(buf, g_note);
        srect(12, PLAY_NOTE_Y, 120, 24, COL_BG);
        text(14, PLAY_NOTE_Y, buf, g_anyNoteOn ? COL_TITLE : COL_MID, 3);
    }

    {
        static uint8_t lastFlags = 0xFF;
        uint8_t f = (uint8_t)((g_gate ? 1 : 0) | (g_switch == 0 ? 2 : 0)
                            | (g_btnTrigHeld ? 4 : 0) | (g_hold ? 8 : 0)
                            | (g_patch.drumMode ? 16 : 0));
        if (f != lastFlags) {
            lastFlags = f;
            srect(134, PLAY_NOTE_Y, 102, 9, COL_BG);
            if (f & 1)  text(134, PLAY_NOTE_Y, "GATE", COL_OK, 1);
            if (f & 2)  text(166, PLAY_NOTE_Y, "SW", COL_OK, 1);
            if (f & 4)  text(184, PLAY_NOTE_Y, "BTN", COL_OK, 1);
            if (f & 8)  text(206, PLAY_NOTE_Y, "HOLD", COL_WAIT, 1);
            if (f & 16) text(134, PLAY_NOTE_Y + 9, "DRUMS", COL_WAIT, 1);
        }
    }

    for (int i = 0; i < 4; i++) {
        int y = PLAY_CH_Y + i * 9;
        int w = (g_chLevel[i] * 150) / 15;
        uint16_t c = g_patch.ch[i].pan ? COL_BAR : COL_MID;
        if (w > 0) rect(45, y + 1, w, 5, c);
        rect(45 + w, y + 1, 150 - w, 5, COL_PANEL);
        gfx_idle();
    }

    for (int i = 0; i < 7; i++) {
        int v = (i < 4) ? (int)g_in[i] : (int)g_knob[i - 4];
        int w = (v * 28) / 4095;
        int x = 10 + i * 33;
        if (w > 0) rect(x, PLAY_IN_Y + 10, w, 8, COL_BAR);
        rect(x + w, PLAY_IN_Y + 10, 28 - w, 8, COL_PANEL);
        gfx_idle();
    }

    static uint32_t lastBad = 0xFFFFFFFFu;
    if (g_streamBad != lastBad) {
        lastBad = g_streamBad;
        srect(150, HINT_Y, 84, 8, COL_BG);
        dec32(buf, g_streamBad, 5);
        text(150, HINT_Y, "REJ", COL_DIM, 1);
        text(176, HINT_Y, buf, g_streamBad ? COL_WAIT : COL_DIM, 1);
    }
}

// ---- EDIT chrome ---------------------------------------------------------------------------------
static void draw_tabs(void)
{
    int x = 2;
    for (int i = 0; i < PAGE_COUNT; i++) {
        int w = text_width(page_tab[i], 1);
        rect(x - 2, TAB_Y - 2, w + 3, 11, (i == g_page) ? COL_PANEL : COL_BG);
        text(x, TAB_Y, page_tab[i], (i == g_page) ? COL_TITLE : COL_DIM, 1);
        x += w + 2;
    }
    rect(0, 15, SCREEN_W, 1, COL_DIM);
}

static void arrow(int x, int y, int up, uint16_t c)
{
    for (int i = 0; i < 4; i++) {
        int w = up ? (i * 2 + 1) : (7 - i * 2);
        rect(x + 3 - (w >> 1), y + i, w, 1, c);
    }
}

static void edit_static(void)
{
    rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    draw_tabs();
    text(4, HINT_Y, "SEL+L/R PAGE", COL_DIM, 1);
    text(96, HINT_Y, (g_page == PAGE_TRIG) ? "A TOGGLES" : "A+PAD EDIT", COL_DIM, 1);
    text(174, HINT_Y, "START PLAY", COL_DIM, 1);

    // Page legends, drawn ONCE. Repainting these every frame is what made them strobe.
    if (g_page == PAGE_MAP) {
        // Column headings sit ABOVE the rows, which is where a table's headings belong, and
        // clear of the list's clear region.
        int hy = list_top(PAGE_MAP) - 9;
        text(14, hy, "SRC", COL_DIM, 1);
        text(58, hy, "DESTINATION", COL_DIM, 1);
        text(130, hy, "AMT", COL_DIM, 1);
        // Each digit over the middle of its own box. Drawn as one padded string, the spacing was
        // the font's advance rather than the box pitch, so the numbers drifted off their columns.
        for (int c = 0; c < 4; c++) {
            char d[4];
            dec_at(d, 0, (uint32_t)c + 1);
            text(158 + c * 18 + 3, hy, d, COL_DIM, 1);
        }
        text(0, 124, "L/R COLUMN  A+UP/DN VALUE  A=TOGGLE TICK", COL_DIM, 1);
        text(6, 136, "ORNAMENT: AMOUNT IS THE ORNAMENT SLOT", COL_DIM, 1);
    }
    // HINT LINES ARE HARD-LIMITED TO 40 CHARACTERS. FONT_ADV is 6 px into a 240 px screen, and
    // glyph() clips silently at the right edge rather than wrapping - so an over-long hint simply
    // loses its tail with nothing to say it has. 40 fits only from x=0; from the usual x=6 it is
    // 39. Both of the lines below that start at 0 do so because they need the fortieth column.
    if (g_page == PAGE_MIX)  text(6, 140, "L/R CHANNEL   A+UP/DN LEVEL  A+L/R PAN", COL_DIM, 1);
    if (g_page == PAGE_ORN)  text(0, 140, "L/R STEP  A+UP/DN SEMITONE  A+L/R OCTAVE", COL_DIM, 1);
    if (g_page == PAGE_DRUM) {
        // Two short lines rather than one that runs off the right edge.
        text(6, 124, "ANY INPUT GOING HIGH FIRES ITS SOUND.", COL_DIM, 1);
        text(6, 136, "DRUMS BORROW CH3 AND CH4. SQUARES FREE.", COL_DIM, 1);
    }
    if (g_page == PAGE_CAL)  text(6, 140, "GBA PSG VOICE   BY ANDY JENKINSON 2026", COL_DIM, 1);
    if (g_page == PAGE_MEM)  text(0, 140, "D-PAD PICKS SLOT. A+UP=SAVE. A+DOWN=LOAD", COL_DIM, 1);
    if (g_page == PAGE_CHAN) text(6, 138, "D-PAD MOVES   A+UP/DN VALUE   A+L/R x8", COL_DIM, 1);
}

// ---- graphs --------------------------------------------------------------------------------------
static int g_envShape = -1;

static void draw_env_graph(int x, int y, int w, int h)
{
    static const uint8_t SEGW[16] = { 1, 3, 5, 7, 10, 13, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52 };
    Channel *ch = &g_patch.ch[g_selCh & 3];

    int base = y + h - 2;
    int top  = y + 2;
    int span = base - top;
    int lane = x + w - 6;

    int shape = (g_selCh << 16) | (ch->atk << 12) | (ch->dec << 8) | (ch->sus << 4) | ch->rel;
    if (shape != g_envShape) {
        g_envShape = shape;
        rect(x - 1, y - 1, w + 2, h + 2, COL_DIM);
        rect(x, y, w, h, COL_BG);

        int sus = base - (span * ch->sus) / 15;
        int wa = SEGW[ch->atk & 15], wd = SEGW[ch->dec & 15], wr = SEGW[ch->rel & 15];
        int ws = w - 10 - wa - wd - wr;
        if (ws < 6) ws = 6;

        int x0 = x + 2;
        int x1 = (int)clampi(x0 + wa, x0, lane - 2);
        int x2 = (int)clampi(x1 + wd, x1, lane - 2);
        int x3 = (int)clampi(x2 + ws, x2, lane - 2);
        int x4 = (int)clampi(x3 + wr, x3, lane - 2);

        line(x0, base, x1, top, COL_OK);
        line(x1, top,  x2, sus, COL_OK);
        line(x2, sus,  x3, sus, COL_OK);
        line(x3, sus,  x4, base, COL_OK);
        if (x4 < lane - 2) line(x4, base, lane - 2, base, COL_DIM);
    }

    int lvl = base - (span * (g_chEnv[g_selCh & 3] >> 8)) / 255;
    if (lvl < y) lvl = y;
    if (lvl > y + h - 3) lvl = y + h - 3;
    rect(lane, y, 5, lvl - y, COL_BG);
    rect(lane, lvl, 5, 3, COL_WAIT);
    rect(lane, lvl + 3, 5, (y + h) - (lvl + 3), COL_BG);
}

static int g_ornShape = -1;

static void draw_orn_editor(int x, int y, int w, int h)
{
    Ornament *o = &g_patch.orn[g_ornSlot % ORN_SLOTS];

    int live  = (g_patch.ch[g_selCh & 3].orn == g_ornSlot + 1) ? g_chOrnStep[g_selCh & 3] : 31;
    int shape = ((g_ornSlot * 5 + o->len) * 3 + o->mode) * 37 + g_ornStep * 41 + live * 43;
    for (int i = 0; i < ORN_STEPS; i++) shape = shape * 31 + o->step[i];
    if (shape == g_ornShape) return;
    g_ornShape = shape;

    rect(x - 1, y - 1, w + 2, h + 2, COL_DIM);
    rect(x, y, w, h, COL_BG);

    int cw   = w / ORN_STEPS;
    int mid  = y + h / 2;
    int half = h / 2 - 4;

    for (int i = 0; i < ORN_STEPS; i++) {
        int bx = x + i * cw;
        if (i == g_ornStep) rect(bx, y, cw, h, COL_PANEL);

        int used = (i < o->len);
        int v    = (int)clampi(o->step[i], -24, 24);
        int hh   = (v * half) / 24;

        uint16_t col = !used ? COL_DIM : (i == live ? COL_TITLE : COL_BAR);
        if (hh >= 0) rect(bx + 1, mid - hh, cw - 2, hh + 1, col);
        else         rect(bx + 1, mid, cw - 2, -hh + 1, col);
    }

    rect(x, mid, w, 1, COL_MID);

    // The loop/end marker, kept INSIDE the box: text drawn outside is never erased by the box
    // repaint, so shortening the length used to strand letters on the background.
    if (o->len > 0 && o->len < ORN_STEPS) {
        int lx = x + o->len * cw;
        if (lx > x + w - 2) lx = x + w - 2;
        rect(lx - 1, y, 2, h, COL_WAIT);
        const char *lbl = o->mode ? "END" : "LOOP";
        int lw = text_width(lbl, 1);
        int tx = lx + 3;
        if (tx + lw > x + w - 2) tx = lx - 3 - lw;
        if (tx < x + 1) tx = x + 1;
        text(tx, y + 2, lbl, COL_WAIT, 1);
    }
}

// ---- MIX -------------------------------------------------------------------------------------------
static int g_mixCache[4] = { -1, -1, -1, -1 };

static void draw_mixer(void)
{
    char buf[8];

    for (int i = 0; i < 4; i++) {
        Channel *ch = &g_patch.ch[i];
        int x   = 20 + i * 54;
        int sel = (i == g_selCh);

        int c = (sel << 12) | (ch->level << 4) | ch->pan;
        if (c != g_mixCache[i]) {
            g_mixCache[i] = c;
            rect(x - 4, 20, 48, 112, sel ? COL_PANEL : COL_BG);
            ch_text(buf, i);
            text(x + 2, 22, buf, sel ? COL_SEL : COL_DIM, 1);
            rect(x + 5, 33, 18, 82, COL_DIM);
            dec32(buf, ch->level, 2);
            text(x + 6, 117, buf, sel ? COL_TITLE : COL_MID, 1);
            // Pan reads as position: L sits to the LEFT of the fader and R to the right, each
            // lit when that side is carrying the channel. OFF is simply neither lit.
            text(x + 1,  125, "L", (ch->pan & 1) ? COL_OK : COL_DIM, 1);
            text(x + 19, 125, "R", (ch->pan & 2) ? COL_OK : COL_DIM, 1);
        }

        int hset  = (ch->level * 78) / 15;
        int hlive = (g_chLevel[i] * 78) / 15;
        if (hlive > hset) hlive = hset;
        const int floorY = 114;
        rect(x + 6, 34, 16, 80 - hset, COL_BG);
        if (hset > hlive) rect(x + 6, floorY - hset, 16, hset - hlive, COL_MID);
        if (hlive > 0)    rect(x + 6, floorY - hlive, 16, hlive, ch->pan ? COL_OK : COL_DIM);
        gfx_idle();
    }
}

// ---- CHAN --------------------------------------------------------------------------------------
// Ten rows by four columns, redrawn cell by cell. Painting the whole table on every cursor move
// would be about 1600 rect() calls a frame, which is both slow and visibly flickery.
#define CHAN_TOP   34
#define CHAN_ROW   10      // one pixel tighter than the list pages, to free a legend line
#define CHAN_X0    58
#define CHAN_W     45

static int  g_chanInit = 0;
static char g_chanCache[CF_ROWS][4][10];

static void draw_chan_grid(void)
{
    char buf[10];

    if (!g_chanInit) {
        g_chanInit = 1;
        rect(0, LIST_TOP - 2, SCREEN_W, 134 - LIST_TOP, COL_BG);
        for (int c = 0; c < 4; c++) {
            ch_text(buf, c);
            text(CHAN_X0 + c * CHAN_W, LIST_TOP, buf, COL_DIM, 1);
        }
        for (int r = 0; r < CF_ROWS; r++) {
            text(4, CHAN_TOP + r * CHAN_ROW, CF_LABEL[r], COL_DIM, 1);
            for (int c = 0; c < 4; c++) g_chanCache[r][c][0] = 1;   // impossible: forces a draw
        }
    }

    for (int r = 0; r < CF_ROWS; r++) {
        int y = CHAN_TOP + r * CHAN_ROW;
        for (int c = 0; c < 4; c++) {
            int sel = (r == g_row && c == g_selCh);
            chan_cell(r, c, buf);

            // The selection marker rides in the cache string, so a cell repaints when its value
            // changes OR when the cursor arrives at or leaves it, and at no other time.
            char key[12];
            int at = scopy(key, 0, buf);
            key[at] = sel ? '*' : '.';
            key[at + 1] = 0;
            if (streq(key, g_chanCache[r][c])) continue;
            scopy(g_chanCache[r][c], 0, key);

            int x = CHAN_X0 + c * CHAN_W;
            rect(x - 2, y - 1, CHAN_W - 2, CHAN_ROW - 1, sel ? COL_PANEL : COL_BG);
            int applies = (CF_CHANS[r] >> c) & 1;
            text(x, y, buf, sel ? COL_TITLE : (applies ? COL_MID : COL_DIM), 1);
        }
        gfx_idle();
    }
}

// ---- TRIG ------------------------------------------------------------------------------------------
static int     g_trigInit = 0;
static uint8_t g_trigCell[4][TRIG_COUNT];
static uint8_t g_trigRow[4];

static void draw_trig_grid(void)
{
    char buf[8];
    const int x0 = 70, cw = 44, y0 = LIST_TOP + 14, rh = 20;

    if (!g_trigInit) {
        g_trigInit = 1;
        rect(0, LIST_TOP - 2, SCREEN_W, 126, COL_BG);
        text(14, LIST_TOP, "TRIGGERS", COL_DIM, 1);
        for (int t = 0; t < TRIG_COUNT; t++)
            text(x0 + t * cw + 8, LIST_TOP, trig_name[t], COL_DIM, 1);
        for (int c = 0; c < 4; c++) {
            g_trigRow[c] = 0xFF;
            for (int t = 0; t < TRIG_COUNT; t++) g_trigCell[c][t] = 0xFF;
        }
    }

    for (int c = 0; c < 4; c++) {
        int y   = y0 + c * rh;
        int sel = (c == g_selCh);

        if (g_trigRow[c] != (uint8_t)sel) {
            g_trigRow[c] = (uint8_t)sel;
            rect(10, y - 2, 52, rh - 2, sel ? COL_PANEL : COL_BG);
            ch_text(buf, c);
            text(14, y + 3, buf, sel ? COL_SEL : COL_DIM, 1);
        }

        for (int t = 0; t < TRIG_COUNT; t++) {
            uint8_t st = (uint8_t)(((g_patch.ch[c].trig >> t) & 1)
                                 | ((sel && t == g_trigCol) ? 2 : 0));
            if (g_trigCell[c][t] == st) continue;
            g_trigCell[c][t] = st;
            int x = x0 + t * cw;
            rect(x, y, 34, 12, (st & 2) ? COL_SEL : COL_DIM);
            rect(x + 1, y + 1, 32, 10, (st & 1) ? COL_OK : COL_BG);
        }
        gfx_idle();
    }
}

// ---- the list --------------------------------------------------------------------------------------
static int g_editCacheValid = 0;

static void edit_list(void)
{
    static char cacheVal[LIST_MAX][24];
    static int  cacheSel = -1;
    static int  cacheTop = -1;
    static int  cacheKey = -1;
    char buf[24];

    int rows = page_rows(g_page);
    int vis  = visible_rows(g_page);
    int key  = (g_page << 4) | g_selCh;
    int top  = list_top(g_page);

    // The channel is part of the cache key because the VOICE page shows different LABELS per
    // channel and only values are compared below. The clear covers the FULL list area, not just
    // the rows this page uses: switching to a channel with fewer parameters would otherwise
    // leave the previous channel's extra rows sitting underneath.
    static int cacheVis = 0;
    if (!g_editCacheValid || cacheTop != g_scroll || cacheKey != key) {
        g_editCacheValid = 1;
        cacheTop = g_scroll;
        cacheKey = key;
        cacheSel = -1;
        for (int r = 0; r < LIST_MAX; r++) cacheVal[r][0] = 1;
        // Clear the taller of the old and new layouts, and NOTHING MORE. Clearing the full
        // LIST_MAX height was over-reaching: it erased the page legends that edit_static() had
        // just drawn below the list, so MAP came up with no column headings at all.
        int clearRows = (vis > cacheVis) ? vis : cacheVis;
        rect(0, top - 2, SCREEN_W, clearRows * LIST_ROW + 4, COL_BG);
        cacheVis = vis;
    }

    for (int i = 0; i < vis; i++) {
        int r = g_scroll + i;
        if (r >= rows) break;
        int y   = top + i * LIST_ROW;
        int sel = (r == g_row);

        field_value(g_page, r, buf);
        if (sel == (i == cacheSel) && streq(buf, cacheVal[i])) continue;

        srect(8, y - 1, SCREEN_W - 16, LIST_ROW - 1, sel ? COL_PANEL : COL_BG);
        text(14, y, field_label(g_page, r), sel ? COL_SEL : COL_DIM, 1);

        if (g_page == PAGE_MAP) {
            ModSlot *m = &g_patch.mod[r % SRC_COUNT];
            char d[10];
            uint16_t base = sel ? COL_TITLE : COL_MID;
            text(58, y, dest_name[m->dest % DEST_COUNT],
                 (sel && g_mapCol == 0) ? COL_SEL : base, 1);
            map_depth_text(d, m);
            text(124, y, d, (sel && g_mapCol == 1) ? COL_SEL : base, 1);

            // The tickboxes: which voices this slot reaches.
            for (int c = 0; c < 4; c++) {
                int bx  = 158 + c * 18;
                int on  = (m->chMask >> c) & 1;
                int cur = (sel && g_mapCol == 2 + c);
                rect(bx, y, 11, 9, cur ? COL_SEL : COL_DIM);
                rect(bx + 1, y + 1, 9, 7, on ? COL_OK : COL_BG);
            }
        } else if (g_page == PAGE_DRUM && r >= 2) {
            uint16_t c = g_drumHit[(r - 2) % DRUM_SRC_COUNT] ? COL_OK : COL_MID;
            text(VAL_X, y, buf, sel ? COL_TITLE : c, 1);
        } else {
            text(VAL_X, y, buf, sel ? COL_TITLE : COL_MID, 1);
        }
        scopy(cacheVal[i], 0, buf);
    }
    cacheSel = g_row - g_scroll;

    {
        static int lastArrows = -1;
        int a = ((g_scroll > 0) ? 1 : 0) | ((g_scroll + vis < rows) ? 2 : 0);
        if (a != lastArrows) {
            lastArrows = a;
            rect(230, top, 7, 4, COL_BG);
            rect(230, top + vis * LIST_ROW - 6, 7, 4, COL_BG);
            if (a & 1) arrow(230, top, 1, COL_TITLE);
            if (a & 2) arrow(230, top + vis * LIST_ROW - 6, 0, COL_TITLE);
        }
    }
}

// ---- per-page extras ---------------------------------------------------------------------------------
static int g_scaleShape = -1;

static void draw_user_scale(int x, int y)
{
    Patch *p = &g_patch;
    int isUser = (p->scale >= SCALE_BUILTIN);

    // -2 is the "no grid" state. Returning early on a built-in scale merely stopped DRAWING the
    // grid, which left the previous USER scale's boxes sitting on screen after you stepped past.
    int shape = isUser ? ((p->userScale[(p->scale - SCALE_BUILTIN) & 3] << 8)
                          | (g_degCol << 2) | (p->scale & 3))
                       : -2;
    if (shape == g_scaleShape) return;
    g_scaleShape = shape;

    rect(x - 2, y - 2, 196, 22, COL_BG);
    if (!isUser) return;

    uint16_t m = p->userScale[(p->scale - SCALE_BUILTIN) & 3];
    for (int d = 0; d < 12; d++) {
        int bx = x + d * 16;
        rect(bx, y, 14, 11, (d == g_degCol) ? COL_SEL : COL_DIM);
        rect(bx + 1, y + 1, 12, 9, (m >> d) & 1 ? COL_OK : COL_BG);
        text(bx + 1, y + 13, NOTE_NAME[d], COL_DIM, 1);
    }
}

// The sixteen slots, and how far a transfer has got. A save takes about a quarter of a second
// and a load the same, which is long enough that a bare "WORKING" leaves you wondering whether
// anything is happening at all — hence a bar rather than a word.
static int     g_memInit = 0;
static uint8_t g_memCell[GBA_PATCH_SLOTS];
static int     g_memProg = -1;

// The whole page IS the grid. There is no list: a slot is a position, and picking a position is
// what a D-pad is for. A+UP writes the current patch to the highlighted slot, B+DOWN reads it
// back — two gestures that cannot be confused with each other or triggered by a stray press.
static void draw_mem_page(void)
{
    char buf[8];
    const int x0 = 14, y0 = 40, bw = 26, bh = 22, by = 100;

    if (!g_memInit) {
        g_memInit = 1;
        // Stops at 138, clear of the legend edit_static() draws at 140. Clearing to 146 took
        // the top half of it away every time the page was entered.
        rect(0, LIST_TOP - 2, SCREEN_W, 138 - LIST_TOP, COL_BG);
        text(14, LIST_TOP, "PATCH SLOTS", COL_DIM, 1);
        for (int i = 0; i < GBA_PATCH_SLOTS; i++) g_memCell[i] = 0xFF;
        g_memProg = -1;
        rect(x0, by, 208, 10, COL_DIM);          // the bar's frame, drawn once
    }

    // Per-box caching: moving the cursor repaints two boxes, not the whole grid.
    for (int i = 0; i < GBA_PATCH_SLOTS; i++) {
        uint8_t st = (uint8_t)(((g_slotMask >> i) & 1) | ((i == g_slot) ? 2 : 0));
        if (g_memCell[i] == st) continue;
        g_memCell[i] = st;

        int col = i & 7, row = i >> 3;           // constant divisors
        int x = x0 + col * bw, y = y0 + row * (bh + 4);
        rect(x, y, bw - 4, bh, (st & 2) ? COL_SEL : COL_DIM);
        rect(x + 1, y + 1, bw - 6, bh - 2, (st & 1) ? COL_BAR : COL_BG);
        dec_at(buf, 0, (uint32_t)i + 1);
        text(x + 5, y + 7, buf, (st & 1) ? COL_TITLE : COL_MID, 1);
        gfx_idle();
    }

    int busy = (g_xferState != LINK_XFER_NONE);
    int prog = link_xfer_progress();
    int key  = (prog << 4) | (busy << 2) | (g_xferResult & 3);
    if (key != g_memProg) {
        g_memProg = key;
        int w = (prog * 206) / 100;              // constant divisor
        rect(x0 + 1, by + 1, w, 8, busy ? COL_OK : COL_PANEL);
        rect(x0 + 1 + w, by + 1, 206 - w, 8, COL_PANEL);

        rect(x0, by + 13, 208, 9, COL_BG);
        if (busy) {
            text(x0, by + 14, (g_xferState == LINK_XFER_SAVE) ? "SAVING" : "LOADING", COL_WAIT, 1);
        } else {
            if (g_xferResult == LINK_RESULT_OK)         text(x0, by + 14, "DONE", COL_OK, 1);
            else if (g_xferResult == LINK_RESULT_EMPTY) text(x0, by + 14, "SLOT IS EMPTY", COL_WAIT, 1);
            else if (g_xferResult == LINK_RESULT_BAD)   text(x0, by + 14, "BAD DATA - NOT LOADED", COL_WAIT, 1);
        }
    }
}

static void edit_extras(void)
{
    char buf[48];

    switch (g_page) {
    case PAGE_ENV: draw_env_graph(14, 104, 212, 38); break;
    case PAGE_ORN: draw_orn_editor(8, 84, 224, 52); break;
    case PAGE_SET: draw_user_scale(14, 92); break;

    case PAGE_CAL: {
        // Only redrawn when the RENDERED text changes, and the raw count is masked to the top
        // bits so ADC dither alone cannot repaint it every frame.
        static char lastLine[48] = { 1, 0 };
        int at = scopy(buf, 0, "READS ");
        char n[8];
        note_text(n, g_note);
        at = scopy(buf, at, n);
        at = scopy(buf, at, "   RAW ");
        char r[8];
        dec32(r, (uint32_t)(g_in[LINK_IN_CV2] & 0xFF0u), 5);
        at = scopy(buf, at, r);
        scopy(buf, at, (g_hostCaps & GBA_CAP_CVOUT_CAL) ? "  CAL" : "  UNCAL");

        // y=114, not 104: the list is eight rows now (CV 2 IN was added for the 1V/oct trim)
        // and ends at y=110, so the old position sat on top of the LINK row - and the clear
        // rect took a bite out of it every time the reading changed.
        if (!streq(buf, lastLine)) {
            scopy(lastLine, 0, buf);
            srect(10, 114, SCREEN_W - 20, 10, COL_BG);
            text(14, 114, buf, COL_MID, 1);
        }

        break;
    }

    default: break;
    }
}

// ---- dispatch -----------------------------------------------------------------------------------------
void ui_init(void)
{
    g_page = PAGE_MEM;
    g_row = 0;
    g_scroll = 0;
    g_repaint = 1;
}

static void invalidate(void)
{
    g_editCacheValid = 0;
    g_envShape   = -1;
    g_ornShape   = -1;
    g_scaleShape = -1;
    g_memInit    = 0;
    g_chanInit   = 0;
    g_trigInit   = 0;
    for (int i = 0; i < 4; i++) g_mixCache[i] = -1;
}

static void change_page(int delta)
{
    int n = (int)g_page + delta;
    if (n < 0) n = PAGE_COUNT - 1;
    if (n >= PAGE_COUNT) n = 0;
    g_page = (uint8_t)n;
    g_row = 0;
    g_scroll = 0;
    g_trigCol = 0;
    g_mapCol = 0;
    // Deliberately does NOT set g_repaint. change_page is only ever reached with SELECT held,
    // and while SELECT is held the body is blanked and only the tab bar is redrawn — see the
    // dispatch at the end of ui_frame. Repainting the whole page per step is what made holding
    // SELECT+RIGHT crawl.
}

void ui_frame(void)
{
    uint16_t edges = synth_take_edges();
    uint16_t steps = synth_take_steps();

    if (edges & KEY_START) {
        g_editMode = (uint8_t)!g_editMode;
        // Entering the editor always lands on the first page: START is a way in, not a bookmark.
        // That page is now MEM, so START is one gesture from recalling a patch.
        if (g_editMode) { g_page = PAGE_MEM; g_row = 0; g_scroll = 0; g_trigCol = 0; g_mapCol = 0; }
        g_repaint = 1;
    }

    if (g_editMode) {
        int rows = page_rows(g_page);
        int vis  = visible_rows(g_page);
        int adj  = (g_btn & KEY_A) != 0;

        if (g_btn & KEY_SELECT) {
            if (steps & KEY_RIGHT) change_page(+1);
            if (steps & KEY_LEFT)  change_page(-1);

        } else if (g_page == PAGE_TRIG) {
            if (steps & KEY_DOWN)  g_selCh = (uint8_t)((g_selCh + 1 >= 4) ? 0 : g_selCh + 1);
            if (steps & KEY_UP)    g_selCh = (uint8_t)((g_selCh == 0) ? 3 : g_selCh - 1);
            if (steps & KEY_RIGHT) g_trigCol = (uint8_t)((g_trigCol + 1 >= TRIG_COUNT) ? 0 : g_trigCol + 1);
            if (steps & KEY_LEFT)  g_trigCol = (uint8_t)((g_trigCol == 0) ? TRIG_COUNT - 1 : g_trigCol - 1);
            if (edges & KEY_A)     g_patch.ch[g_selCh].trig ^= (uint8_t)(1u << g_trigCol);

        } else if (g_page == PAGE_CHAN) {
            if (!adj) {
                if (steps & KEY_DOWN)  g_row = (uint8_t)((g_row + 1 >= CF_ROWS) ? 0 : g_row + 1);
                if (steps & KEY_UP)    g_row = (uint8_t)((g_row == 0) ? CF_ROWS - 1 : g_row - 1);
                if (steps & KEY_RIGHT) g_selCh = (uint8_t)((g_selCh + 1 >= 4) ? 0 : g_selCh + 1);
                if (steps & KEY_LEFT)  g_selCh = (uint8_t)((g_selCh == 0) ? 3 : g_selCh - 1);
            } else {
                if (steps & KEY_UP)    chan_adjust(g_row, g_selCh, +1);
                if (steps & KEY_DOWN)  chan_adjust(g_row, g_selCh, -1);
                if (steps & KEY_RIGHT) chan_adjust(g_row, g_selCh, +8);
                if (steps & KEY_LEFT)  chan_adjust(g_row, g_selCh, -8);
            }

        } else if (g_page == PAGE_MEM) {
            // Bare D-pad walks the grid the way it looks: one box sideways, a row of eight
            // vertically. Save and load are deliberately different gestures on different
            // buttons, so neither can happen by accident while you are just looking around.
            if (!adj) {
                if (steps & KEY_RIGHT) g_slot = (uint8_t)clampi(g_slot + 1, 0, GBA_PATCH_SLOTS - 1);
                if (steps & KEY_LEFT)  g_slot = (uint8_t)clampi(g_slot - 1, 0, GBA_PATCH_SLOTS - 1);
                if (steps & KEY_DOWN)  g_slot = (uint8_t)clampi(g_slot + 8, 0, GBA_PATCH_SLOTS - 1);
                if (steps & KEY_UP)    g_slot = (uint8_t)clampi(g_slot - 8, 0, GBA_PATCH_SLOTS - 1);
            }
            if (adj && (steps & KEY_UP) && g_xferState == LINK_XFER_NONE)
                link_begin_save(g_slot, (const uint8_t *)&g_patch, synth_patch_bytes());
            if (adj && (steps & KEY_DOWN) && g_xferState == LINK_XFER_NONE)
                link_begin_load(g_slot);

        } else if (g_page == PAGE_MIX) {
            if (!adj) {
                if (steps & KEY_RIGHT) g_selCh = (uint8_t)((g_selCh + 1 >= 4) ? 0 : g_selCh + 1);
                if (steps & KEY_LEFT)  g_selCh = (uint8_t)((g_selCh == 0) ? 3 : g_selCh - 1);
            } else {
                Channel *ch = &g_patch.ch[g_selCh];
                if (steps & KEY_UP)    ch->level = (uint8_t)clampi(ch->level + 1, 0, 15);
                if (steps & KEY_DOWN)  ch->level = (uint8_t)clampi(ch->level - 1, 0, 15);
                if (steps & KEY_RIGHT) ch->pan   = (uint8_t)clampi(ch->pan + 1, 0, 3);
                if (steps & KEY_LEFT)  ch->pan   = (uint8_t)clampi(ch->pan - 1, 0, 3);
            }

        } else if (g_page == PAGE_MAP) {
            ModSlot *m = &g_patch.mod[g_row % SRC_COUNT];
            if (!adj) {
                if (steps & KEY_DOWN)  g_row = (uint8_t)((g_row + 1 >= rows) ? 0 : g_row + 1);
                if (steps & KEY_UP)    g_row = (uint8_t)((g_row == 0) ? rows - 1 : g_row - 1);
                if (steps & KEY_RIGHT) g_mapCol = (uint8_t)((g_mapCol + 1 >= 6) ? 0 : g_mapCol + 1);
                if (steps & KEY_LEFT)  g_mapCol = (uint8_t)((g_mapCol == 0) ? 5 : g_mapCol - 1);
            } else if (g_mapCol == 0) {
                if (steps & KEY_UP)   m->dest = (uint8_t)clampi(m->dest + 1, 0, DEST_COUNT - 1);
                if (steps & KEY_DOWN) m->dest = (uint8_t)clampi(m->dest - 1, 0, DEST_COUNT - 1);
            } else if (g_mapCol == 1) {
                int lo = (m->dest == DEST_ORN) ? 1 : -64;
                int hi = (m->dest == DEST_ORN) ? ORN_SLOTS : 63;
                if (steps & KEY_UP)    m->depth = (int8_t)clampi(m->depth + 1, lo, hi);
                if (steps & KEY_DOWN)  m->depth = (int8_t)clampi(m->depth - 1, lo, hi);
                if (steps & KEY_RIGHT) m->depth = (int8_t)clampi(m->depth + 8, lo, hi);
                if (steps & KEY_LEFT)  m->depth = (int8_t)clampi(m->depth - 8, lo, hi);
            } else {
                uint8_t bit = (uint8_t)(1u << (g_mapCol - 2));
                if (steps & KEY_UP)   m->chMask |= bit;
                if (steps & KEY_DOWN) m->chMask &= (uint8_t)~bit;
            }
            // A tick is one bit, so a bare A press flips it — the same reasoning as the TRIG
            // grid. Holding A and hunting for a direction to express "on" is busywork.
            if ((edges & KEY_A) && g_mapCol >= 2)
                m->chMask ^= (uint8_t)(1u << (g_mapCol - 2));

        } else if (g_page == PAGE_ORN && g_row == 4) {
            Ornament *o = &g_patch.orn[g_ornSlot % ORN_SLOTS];
            if (!adj) {
                if (steps & KEY_RIGHT) g_ornStep = (uint8_t)((g_ornStep + 1 >= ORN_STEPS) ? 0 : g_ornStep + 1);
                if (steps & KEY_LEFT)  g_ornStep = (uint8_t)((g_ornStep == 0) ? ORN_STEPS - 1 : g_ornStep - 1);
                if (steps & KEY_DOWN)  g_row = 0;
                if (steps & KEY_UP)    g_row = 3;
            } else {
                // A step is a semitone offset, so the coarse increment is an OCTAVE. Eight
                // semitones is not a musical quantity.
                int i = g_ornStep % ORN_STEPS;
                if (steps & KEY_UP)    o->step[i] = (int8_t)clampi(o->step[i] + 1,  -24, 24);
                if (steps & KEY_DOWN)  o->step[i] = (int8_t)clampi(o->step[i] - 1,  -24, 24);
                if (steps & KEY_RIGHT) o->step[i] = (int8_t)clampi(o->step[i] + 12, -24, 24);
                if (steps & KEY_LEFT)  o->step[i] = (int8_t)clampi(o->step[i] - 12, -24, 24);
            }

        } else if (g_page == PAGE_SET && g_row == 4 && g_patch.scale >= SCALE_BUILTIN) {
            uint16_t *m = &g_patch.userScale[(g_patch.scale - SCALE_BUILTIN) & 3];
            if (!adj) {
                if (steps & KEY_RIGHT) g_degCol = (uint8_t)((g_degCol + 1 >= 12) ? 0 : g_degCol + 1);
                if (steps & KEY_LEFT)  g_degCol = (uint8_t)((g_degCol == 0) ? 11 : g_degCol - 1);
                if (steps & KEY_DOWN)  g_row = 0;
                if (steps & KEY_UP)    g_row = 3;
            } else {
                if (steps & KEY_UP)   *m |= (uint16_t)(1u << g_degCol);
                if (steps & KEY_DOWN) *m &= (uint16_t)~(1u << g_degCol);
            }

        } else if (g_page == PAGE_MEM && g_row == 0 && adj) {
            // The slot display is a grid eight wide, so the D-pad should move around it the way
            // it looks: left and right step one box, up and down jump a whole row.
            if (steps & KEY_RIGHT) field_adjust(g_page, 0, +1);
            if (steps & KEY_LEFT)  field_adjust(g_page, 0, -1);
            if (steps & KEY_DOWN)  field_adjust(g_page, 0, +8);
            if (steps & KEY_UP)    field_adjust(g_page, 0, -8);

        } else if (adj) {
            if (steps & KEY_UP)    field_adjust(g_page, g_row, +1);
            if (steps & KEY_DOWN)  field_adjust(g_page, g_row, -1);
            if (steps & KEY_RIGHT) field_adjust(g_page, g_row, +8);
            if (steps & KEY_LEFT)  field_adjust(g_page, g_row, -8);

        } else {
            // Wrap by comparison, not by %. The divisor is a variable and we link -nostdlib.
            if (steps & KEY_DOWN) g_row = (uint8_t)((g_row + 1 >= rows) ? 0 : g_row + 1);
            if (steps & KEY_UP)   g_row = (uint8_t)((g_row == 0) ? rows - 1 : g_row - 1);
        }

        // The VOICE page's row count depends on the selected channel, so changing channel can
        // leave the cursor past the end.
        rows = page_rows(g_page);
        if (g_row >= rows) g_row = (uint8_t)(rows - 1);
        vis = visible_rows(g_page);
        if (g_row < g_scroll)        g_scroll = g_row;
        if (g_row >= g_scroll + vis) g_scroll = (uint8_t)(g_row - vis + 1);
        if (g_scroll + vis > rows)   g_scroll = (uint8_t)((rows > vis) ? rows - vis : 0);
    }

    // A completed LOAD replaces the whole patch, so everything cached from it has to go.
    if (g_loadReady) {
        g_loadReady = 0;
        const Patch *in = (const Patch *)g_patchBuf;
        if (!synth_patch_valid(in)) {
            g_xferResult = LINK_RESULT_BAD;
        } else {
            // Byte loop, not a struct assignment: GCC turns `g_patch = *in` into a call to
            // memcpy, which -nostdlib cannot resolve. The tripwire caught it on the first build.
            uint8_t *dst = (uint8_t *)&g_patch;
            const uint8_t *src = (const uint8_t *)in;
            for (int i = 0; i < synth_patch_bytes(); i++) dst[i] = src[i];
            // Migrate BEFORE applying: synth_patch_applied() derives the CV reciprocal from
            // cvScale, so migrating afterwards would leave it computed from the old units.
            synth_patch_migrate(&g_patch);
            synth_patch_applied();
            psg_wave_load(psg_wave_preset[g_patch.waveSel % PSG_WAVE_PRESETS]);
            g_repaint = 1;
        }
    }

    // WHILE SELECT IS HELD YOU ARE NAVIGATING, NOT READING.
    //
    // Drawing a full page for every step made holding SELECT+RIGHT crawl: each one was a
    // full-screen clear plus a page body, and the button repeat outran it badly. So a step blanks
    // the body ONCE and then redraws nothing but the tab bar, which is a few dozen glyphs. The
    // page itself is drawn when SELECT comes back up and you have arrived somewhere.
    //
    // Input keeps up regardless — buttons are scanned at 1 kHz from the control tick, which runs
    // inside the drawing routines — so this is purely about not making the screen the bottleneck.
    int selecting = g_editMode && (g_btn & KEY_SELECT);
    if (!selecting && g_navBlank) { g_navBlank = 0; g_repaint = 1; }

    if (g_editMode != g_lastMode) { g_lastMode = g_editMode; g_repaint = 1; }

    if (g_repaint) {
        g_repaint = 0;
        invalidate();
        g_tabPage = -1;
        if (g_editMode) edit_static(); else play_static();
    }

    if (g_editMode) {
        if (selecting) {
            if (!g_navBlank) {
                g_navBlank = 1;
                rect(0, 17, SCREEN_W, HINT_Y - 19, COL_BG);
            }
            if (g_tabPage != g_page) { g_tabPage = g_page; draw_tabs(); }
        }
        else if (g_page == PAGE_TRIG) draw_trig_grid();
        else if (g_page == PAGE_MIX)  draw_mixer();
        else if (g_page == PAGE_CHAN) draw_chan_grid();
        else if (g_page == PAGE_MEM)  draw_mem_page();
        else                        { edit_list(); edit_extras(); }
    } else {
        play_frame();
    }

    link_post_status(g_page, g_editMode ? GBA_MODE_EDIT : GBA_MODE_PLAY,
                     (uint8_t)((g_anyNoteOn ? GBA_FLAG_NOTE_ON : 0) |
                               (g_gate      ? GBA_FLAG_GATE    : 0) |
                               (g_hold      ? GBA_FLAG_LATCH   : 0)));
}
