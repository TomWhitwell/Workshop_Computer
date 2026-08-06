// drumdrum-specific Monome Grid layout for a 16x8 device.
//
//   LEFT half (cols 0–7) — sequence overview
//     Row 0           length selector  (cols 0..7 = "set length to N+1")
//     Rows 1–7        per-step bar — height = pitch, brightness = velocity
//                       (current/edit step override the brightness so they
//                        stand out; out-of-length columns are off)
//
//   RIGHT half (cols 8–15) — selected-step editor
//     (col 15, row 0) play/pause toggle
//     Rows 1–5        chromatic pitch grid (40 notes from MIDI 36)
//                     bottom-left = lowest pitch, top-right = highest
//     Rows 6–7        16-step velocity bar
//                     bottom-left = lowest, top-right = highest

#include "grid_ui.h"
#include "monome_mext.h"
#include "shared_state.h"

#include "pico/time.h"

#include <string.h>
#include <stdint.h>

// Local snapshot of last-rendered state — lets us skip redundant LED
// writes without touching gState mid-frame.
struct RenderSnap {
    uint8_t  pitch[8];
    uint8_t  velocity[8];
    uint8_t  seqLength;
    uint8_t  editStep;
    uint8_t  currentStep;
    uint8_t  playing;
    uint32_t tickEpoch;
    bool     valid;
};
static RenderSnap snap = {};

// ── Layout helpers ────────────────────────────────────────────

// Pitch → bar height (1..7 cells), scaled across the full 0..127
// MIDI range so the bar tops out only at the highest possible pitches.
static inline uint8_t pitch_to_bar_height(uint8_t pitch)
{
    int32_t h = ((int32_t)pitch * 7) / 127;     // 0..7
    if (h > 6) h = 6;
    return (uint8_t)(h + 1);                    // 1..7
}

// Brightness for a step's column on the left-half overview.
// "other" steps show velocity-as-brightness so the bar carries both
// pieces of info at once. Current playback overrides to full bright.
// The edit step gets a brightness floor so it's always clearly visible.
static inline uint8_t left_brightness(uint8_t step, const RenderSnap &s)
{
    if (step >= s.seqLength) return 0;
    if (step == s.currentStep && s.playing) return 15;

    uint8_t vb = (uint8_t)(s.velocity[step] >> 4);  // 0..15
    if (vb < 1) vb = 1;                             // never invisible

    if (step == s.editStep) {
        if (vb < 6) vb = 6;                         // edit floor
    }
    return vb;
}

// Right-half pitch grid: full-range MIDI note → (col, row). Each cell
// is a 3-or-4-pitch bin so every MIDI pitch lands somewhere on the
// grid. Row 5 (bottom) holds the lowest pitch, row 1 (top) the highest.
static inline uint8_t pitch_to_cell(uint8_t note)
{
    int32_t off = ((int32_t)note * PITCH_GRID_CELLS) / 128;
    if (off >= PITCH_GRID_CELLS) off = PITCH_GRID_CELLS - 1;
    if (off < 0) off = 0;
    return (uint8_t)off;
}

static void note_to_right_cell(uint8_t note, uint8_t *col_out, uint8_t *row_out)
{
    uint8_t off = pitch_to_cell(note);
    *row_out = (uint8_t)(5 - (off >> 3));
    *col_out = (uint8_t)(8 + (off & 7));
}

// Cell N → representative pitch (centre of the cell's bin).
static inline uint8_t cell_to_pitch(uint8_t off)
{
    int32_t p = ((int32_t)off * 128 + 64) / PITCH_GRID_CELLS;
    if (p > 127) p = 127;
    if (p < 0)   p = 0;
    return (uint8_t)p;
}

// Right-half velocity bar: idx 0..15 → (col, row). idx 0 is at the
// bottom-left (least velocity), idx 15 at the top-right (most).
static inline void vel_idx_to_cell(uint8_t idx, uint8_t *col_out, uint8_t *row_out)
{
    *row_out = (uint8_t)(7 - (idx >> 3));     // row 7 (bottom) for idx 0..7, row 6 for 8..15
    *col_out = (uint8_t)(8 + (idx & 7));
}

// ── public API ────────────────────────────────────────────────
void grid_ui_init(void)
{
    snap.valid = false;
    mext_grid_led_intensity(15);
    mext_grid_led_all(0);
}

void grid_ui_process_input(void)
{
    mext_event_t ev;
    while (mext_event_pop(&ev)) {
        if (ev.type != MEXT_EVENT_GRID_KEY) continue;
        if (ev.grid.z == 0) continue;          // act on key-down only

        uint8_t x = ev.grid.x, y = ev.grid.y;

        if (x < 8) {
            // ── LEFT half ─────────────────────────────────
            if (y == 0) {
                // length selector: tap col N → length N+1 (1–8 inclusive)
                uint8_t newLen = (uint8_t)(x + 1);
                gState.seqLength = newLen;
                if (gState.editStep    >= newLen) gState.editStep    = 0;
                if (gState.currentStep >= newLen) gState.currentStep = 0;
            } else {
                // any other left cell selects the edit step for that column
                if (x < gState.seqLength) {
                    gState.editStep = x;
                }
            }
        } else {
            // ── RIGHT half ────────────────────────────────
            if (y == 0) {
                // only col 15 row 0 is a button; cols 8..14 are reserved
                if (x == 15) {
                    gState.playing = gState.playing ? 0 : 1;
                }
            } else if (y >= 1 && y <= 5) {
                // pitch picker — 40 cells covering all 128 MIDI pitches
                // in 3-or-4-pitch bins. Tapping a cell snaps the pitch
                // to the bin centre; the panel knob can dial anything
                // in between for fine-grained editing.
                uint8_t off = (uint8_t)(((5 - y) << 3) + (x - 8));
                if (off < PITCH_GRID_CELLS) {
                    gState.pitch[gState.editStep] = cell_to_pitch(off);
                }
            } else {
                // y == 6 or 7 — velocity bar (16 cells, bottom-left = low)
                uint8_t idx = (uint8_t)(((7 - y) << 3) + (x - 8));   // 0..15
                uint16_t v = (uint16_t)((idx + 1) << 4);             // 16..256
                if (v > 255) v = 255;
                gState.velocity[gState.editStep] = (uint8_t)v;
            }
        }
    }
}

static bool snap_matches(const RenderSnap &a)
{
    if (!a.valid) return false;
    if (a.seqLength   != gState.seqLength)   return false;
    if (a.editStep    != gState.editStep)    return false;
    if (a.currentStep != gState.currentStep) return false;
    if (a.playing     != gState.playing)     return false;
    if (a.tickEpoch   != gState.tickEpoch)   return false;
    if (memcmp((const void*)a.pitch,    (const void*)gState.pitch,    8) != 0) return false;
    if (memcmp((const void*)a.velocity, (const void*)gState.velocity, 8) != 0) return false;
    return true;
}

static void take_snapshot(RenderSnap &out)
{
    memcpy(out.pitch,    (const void*)gState.pitch,    8);
    memcpy(out.velocity, (const void*)gState.velocity, 8);
    out.seqLength   = gState.seqLength;
    out.editStep    = gState.editStep;
    out.currentStep = gState.currentStep;
    out.playing     = gState.playing;
    out.tickEpoch   = gState.tickEpoch;
    out.valid       = true;
}

void grid_ui_render(void)
{
    if (!mext_grid_ready()) return;
    if (snap_matches(snap)) return;

    take_snapshot(snap);
    const RenderSnap &s = snap;

    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));

    // ── LEFT half — sequence overview ───────────────────
    // Row 0: length indicator
    for (uint8_t c = 0; c < 8; c++) {
        buf[0 * 16 + c] = (c < s.seqLength) ? 8 : 0;
    }
    // Rows 1–7: per-step bar (height = pitch, brightness = velocity)
    for (uint8_t step = 0; step < 8; step++) {
        uint8_t b = left_brightness(step, s);
        if (b == 0) continue;
        uint8_t h = pitch_to_bar_height(s.pitch[step]);   // 1..7
        for (uint8_t k = 0; k < h; k++) {
            uint8_t r = (uint8_t)(7 - k);                 // fill from bottom up
            buf[r * 16 + step] = b;
        }
    }

    // ── RIGHT half — selected-step editor ───────────────
    // (col 15, row 0): play/pause
    buf[0 * 16 + 15] = s.playing ? 15 : 3;

    // Rows 1–5: pitch picker (40 cells, every MIDI pitch reachable).
    // Only the current pitch is lit — keeps the picker uncluttered.
    {
        uint8_t c, r;
        note_to_right_cell(s.pitch[s.editStep], &c, &r);
        buf[r * 16 + c] = 15;
    }

    // Rows 6–7: velocity bar (16 cells, bottom-left = low)
    {
        uint8_t v = s.velocity[s.editStep];
        // Number of cells lit, 0..16. ceil(v/16) so any non-zero
        // velocity lights at least one cell, and v=255 lights all 16.
        uint8_t lit = (uint8_t)((v + 15) >> 4);
        if (lit > 16) lit = 16;
        for (uint8_t idx = 0; idx < lit; idx++) {
            uint8_t c, r;
            vel_idx_to_cell(idx, &c, &r);
            buf[r * 16 + c] = 10;
        }
    }

    // Push the frame. mext clones into its own buffer, so `buf` can go
    // out of scope immediately. Falls back to per-cell writes if a frame
    // is already in flight (rare with 60 fps refresh).
    if (!mext_grid_frame_submit(buf)) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 16; x++) {
                mext_grid_led_set(x, y, buf[y * 16 + x]);
            }
        }
    }
}
