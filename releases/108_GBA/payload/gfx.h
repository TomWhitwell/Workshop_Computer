// gfx.h — mode-3 bitmap drawing for the GBA payload.
//
// Split out of main.c unchanged. Every routine keeps the link serviced while it works: a
// single line of scale-2 text is hundreds of rect() calls, and with the host streaming words
// that was long enough to lose thousands of them, which the host then read as a dead link.
#pragma once

#include <stdint.h>

#define SCREEN_W 240
#define SCREEN_H 160

#define VRAM ((volatile uint16_t *)0x06000000)

static inline uint16_t rgb15(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
}

// ---- palette ----
// COL_DIM and COL_MID carry most of the text on screen. They started at (8,8,12) and (16,16,20)
// and were simply too dark to read against the background on real hardware — the GBA's original
// non-backlit screen is much less forgiving than an emulator. Lifted well up; the selection
// highlight still separates cleanly because it uses COL_SEL/COL_TITLE against COL_PANEL.
#define COL_BG     rgb15( 2,  3,  8)
#define COL_TITLE  rgb15(31, 28, 10)
#define COL_DIM    rgb15(20, 21, 25)
#define COL_MID    rgb15(27, 28, 31)
#define COL_OK     rgb15( 6, 31, 10)
#define COL_WAIT   rgb15(31, 14,  4)
#define COL_BAR    rgb15(10, 22, 31)
#define COL_HEX    rgb15(31, 31, 31)
#define COL_FAIL   rgb15(31,  6,  6)
#define COL_SEL    rgb15(31, 31, 31)
#define COL_PANEL  rgb15( 5,  6, 14)

// Implemented in main.c. Called from inside every drawing routine, and from the frame wait.
//
// It services the link AND runs the synth's control tick. Servicing alone was not enough: the
// control tick used to advance only once per main-loop iteration, so the effective control rate
// was the FRAME rate, and a page with a heavier redraw ran the envelopes and the portamento
// slower than a cheap one. That is why the same patch glided on an edit page and stepped on the
// performance page — the notes were fine, the clock driving them was not.
void gfx_idle(void);

void gfx_init(void);

void rect(int x, int y, int w, int h, uint16_t color);
// Bresenham, so no division: the payload links -nostdlib and a divide by a variable is a link
// error. Used by the envelope graph.
void line(int x0, int y0, int x1, int y1, uint16_t color);
void srect(int x, int y, int w, int h, uint16_t color);   // rect() + link_service()
void glyph(int x, int y, char ch, uint16_t color, int scale);
void text(int x, int y, const char *s, uint16_t color, int scale);
void text_centre(int y, const char *s, uint16_t color, int scale);
int  text_width(const char *s, int scale);

// Append v to out at offset `at`, left-aligned and un-padded, returning the new offset.
//
// Prefer this to dec32() for anything whose digit count varies. dec32 right-aligns into a FIXED
// width and, when the value needs more digits than the width allows, keeps only the LAST ones:
// dec32(buf, 10, 1) yields "0". That is what printed every envelope time above 5 ms as "0 MS".
int  dec_at(char *out, int at, uint32_t v);

void hex32(char *out, uint32_t v);
void dec32(char *out, uint32_t v, int width);
void sdec32(char *out, int32_t v, int width);   // signed, with a leading + or -
