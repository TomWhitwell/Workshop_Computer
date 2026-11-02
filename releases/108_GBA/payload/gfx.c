// gfx.c — mode-3 bitmap drawing.
//
// THE PRIMITIVES RUN FROM IWRAM. They used to run from EWRAM like the rest of the image, one pixel
// per loop iteration, and glyph() called rect() for every lit pixel. EWRAM's 16-bit bus makes each
// 32-bit ARM instruction cost about six cycles to fetch, so a full-screen clear was ~1M cycles
// (~60 ms) and text was worse per pixel - the whole editor felt like it was wading through mud.
// Now rect() fills two pixels per 32-bit store, unrolled, and glyph() and line() write VRAM
// directly, all from zero-wait IWRAM. Everything that CALLS them (text, rect_clear, the UI) stays
// in EWRAM: the inner loops are where the time went.

#include "gfx.h"
#include "gba.h"
#include "link.h"
#include "font5x7.h"

#define REG_BASE    0x04000000
#define REG_DISPCNT (*(volatile uint16_t *)(REG_BASE + 0x0000))
#define MODE3       0x0003
#define BG2_ON      0x0400

void gfx_init(void)
{
    REG_DISPCNT = MODE3 | BG2_ON;
}

// Two pixels per 32-bit store, eight stores per pass. VRAM takes 32-bit writes (two 16-bit
// accesses, one instruction). The pointers stay volatile: without it GCC is free to turn the fill
// into a call to memset, which -nostdlib cannot resolve.
IWRAM_CODE void rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    uint32_t c2 = (uint32_t)color | ((uint32_t)color << 16);
    volatile uint16_t *row = VRAM + y * SCREEN_W + x;
    for (int j = 0; j < h; j++, row += SCREEN_W) {
        volatile uint16_t *p = row;
        int n = w;
        if (((uint32_t)p & 2u) != 0) { *p++ = color; n--; }       // to a word boundary
        volatile uint32_t *q = (volatile uint32_t *)p;
        while (n >= 16) {
            q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2;
            q[4] = c2; q[5] = c2; q[6] = c2; q[7] = c2;
            q += 8;
            n -= 16;
        }
        while (n >= 2) { *q++ = c2; n -= 2; }
        if (n) *(volatile uint16_t *)q = color;
    }
}

// Bresenham. Deliberately division-free — see the note in gfx.h. Pixels go straight to VRAM.
IWRAM_CODE void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sx =  (x0 < x1) ? 1 : -1;
    int sy =  (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if ((unsigned)x0 < SCREEN_W && (unsigned)y0 < SCREEN_H) VRAM[y0 * SCREEN_W + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void srect(int x, int y, int w, int h, uint16_t color)
{
    rect(x, y, w, h, color);
    gfx_idle();
}

void rect_clear(int x, int y, int w, int h, uint16_t color)
{
    enum { BAND = 8 };   // 8 rows x 240 cols is a small, cheap fill; plenty of bands even for
                         // a full 160-row screen (20 idle calls), rare enough to matter and
                         // small enough that no one band is itself a noticeable stall.
    for (int y0 = 0; y0 < h; y0 += BAND) {
        int bh = (h - y0 < BAND) ? (h - y0) : BAND;
        rect(x, y + y0, w, bh, color);
        gfx_idle();
    }
}

// Draw one glyph at `scale`. Column-major font: bit r of column c is the pixel at (c, r).
//
// Scale 1 - nearly all the text there is - writes its pixels straight into VRAM, clipping at the
// right edge as it always has (a too-long hint line loses its tail rather than wrapping). Larger
// scales, and glyphs partly off the top, bottom or left, take the old rect()-per-pixel path.
IWRAM_CODE void glyph(int x, int y, char ch, uint16_t color, int scale)
{
    if ((unsigned char)ch < FONT_FIRST || (unsigned char)ch > FONT_LAST) ch = '?';
    const uint8_t *g = font5x7[(unsigned char)ch - FONT_FIRST];
    if (scale == 1 && x >= 0 && y >= 0 && y + FONT_H <= SCREEN_H) {
        volatile uint16_t *base = VRAM + y * SCREEN_W + x;
        for (int c = 0; c < FONT_W && x + c < SCREEN_W; c++) {
            uint32_t col = g[c] & ((1u << FONT_H) - 1u);
            volatile uint16_t *p = base + c;
            for (; col; col >>= 1, p += SCREEN_W)
                if (col & 1u) *p = color;
        }
        return;
    }
    for (int c = 0; c < FONT_W; c++) {
        uint8_t col = g[c];
        for (int r = 0; r < FONT_H; r++) {
            if (col & (1u << r)) rect(x + c * scale, y + r * scale, scale, scale, color);
        }
    }
}

int text_width(const char *s, int scale)
{
    int n = 0;
    while (s[n]) n++;
    return n * FONT_ADV * scale;
}

// IWRAM too: it runs once per character. The gfx_idle() it calls stays in EWRAM.
IWRAM_CODE void text(int x, int y, const char *s, uint16_t color, int scale)
{
    for (int i = 0; s[i]; i++) {
        glyph(x + i * FONT_ADV * scale, y, s[i], color, scale);
        gfx_idle();
    }
}

void text_centre(int y, const char *s, uint16_t color, int scale)
{
    text((SCREEN_W - text_width(s, scale)) / 2, y, s, color, scale);
}

// Freestanding build: no libc, so render numbers by hand.
int dec_at(char *out, int at, uint32_t v)
{
    char tmp[12];
    int  n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }   // constant divisor
    while (n) out[at++] = tmp[--n];
    out[at] = 0;
    return at;
}

void hex32(char *out, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) out[i] = H[(v >> (28 - 4 * i)) & 0xF];
    out[8] = 0;
}

// Right-aligned decimal into a fixed field. Counts are far easier to read as decimal than
// hex, and these screens exist to be read at a glance from the bench.
//
// The divisions here are by the CONSTANT 10, which GCC turns into a multiply-and-shift. That
// matters: we link -nostdlib, so libgcc is absent and a divide by a VARIABLE would be an
// undefined reference to __aeabi_uidiv rather than merely slow code.
void dec32(char *out, uint32_t v, int width)
{
    for (int i = 0; i < width; i++) out[i] = ' ';
    out[width] = 0;
    int i = width - 1;
    if (v == 0) { out[i] = '0'; return; }
    while (v && i >= 0) { out[i--] = (char)('0' + (v % 10)); v /= 10; }
}

void sdec32(char *out, int32_t v, int width)
{
    int neg = v < 0;
    uint32_t m = (uint32_t)(neg ? -v : v);
    dec32(out, m, width);
    // Place the sign immediately left of the first digit.
    for (int i = width - 1; i >= 0; i--) {
        if (out[i] == ' ') { out[i] = neg ? '-' : '+'; break; }
        if (i == 0) out[0] = neg ? '-' : '+';
    }
}
