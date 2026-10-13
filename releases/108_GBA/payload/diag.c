// diag.c — link-characterisation screens. Behaviour unchanged from the original main.c.

#include "diag.h"
#include "gfx.h"
#include "link.h"

#define BAR_X 12
#define BAR_Y 56
#define BAR_W 216
#define BAR_H 22

int diag_active(void)
{
    return g_bench || g_linkTest;
}

// ── LINK SPEED TEST: one full 0 -> 0FFF ramp per pass, screen frozen ───────────────────────
// Draw the LAST pass, then freeze the display and service tightly for a whole ramp. Drawing
// and measuring cannot overlap: a ramp running across a redraw breaks once per frame however
// good the link is. Separating them is what makes "did a full sweep complete cleanly"
// answerable at all.
static void ramp_frame(void)
{
    char buf[12];
    g_ltMeasuring = 0;                       // drawing: judge nothing that arrives now

    rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    text_centre(6, "LINK 0-0FFF RAMP", COL_TITLE, 1);

    dec32(buf, g_ltRate, 5);
    text(24, 22, "RATE", COL_DIM, 1);
    text(64, 20, buf, COL_HEX, 2);
    text(174, 28, "wds/s", COL_DIM, 1);

    rect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, COL_DIM);
    rect(BAR_X, BAR_Y, BAR_W, BAR_H, COL_BG);
    if (g_ltHave) {
        int w = g_ltFailed ? (int)(((uint32_t)g_ltFailAt * BAR_W) / 0x0FFFu) : BAR_W;
        if (w > 0) rect(BAR_X, BAR_Y, w, BAR_H, g_ltFailed ? COL_WAIT : COL_OK);
        if (g_ltFailed) rect(BAR_X + w, BAR_Y - 4, 2, BAR_H + 8, COL_FAIL);
    }

    if (g_ltHave && g_ltFailed) {
        hex32(buf, g_ltFailAt);
        text(BAR_X, BAR_Y + BAR_H + 6, "BROKE AT", COL_DIM, 1);
        text(BAR_X + 70, BAR_Y + BAR_H + 6, buf + 4, COL_FAIL, 1);
    } else if (g_ltHave) {
        text(BAR_X, BAR_Y + BAR_H + 6, "FULL RAMP 000-FFF OK", COL_OK, 1);
    }

    // What kind of discontinuity: a small gap is dropped words, a wild jump is a bit-slip.
    dec32(buf, g_ltSkips, 4);
    text(14, 104, "DROPPED", COL_DIM, 1); text(76, 104, buf, g_ltSkips ? COL_WAIT : COL_OK, 1);
    dec32(buf, g_ltWild, 4);
    text(120, 104, "SLIP", COL_DIM, 1);   text(158, 104, buf, g_ltWild ? COL_FAIL : COL_OK, 1);
    dec32(buf, g_ltBadMagic, 5);
    text(14, 116, "CORRUPT", COL_DIM, 1); text(76, 116, buf, g_ltBadMagic ? COL_FAIL : COL_OK, 1);

    dec32(buf, g_ltPassOk, 3);
    text(14, 130, "CLEAN PASSES", COL_DIM, 1); text(110, 130, buf, COL_OK, 1);
    dec32(buf, g_ltPassBad, 3);
    text(140, 130, "BAD", COL_DIM, 1);         text(172, 130, buf, COL_FAIL, 1);

    if (g_ltPassOk && !g_ltPassBad)  text_centre(144, "RELIABLE AT THIS RATE", COL_OK, 1);
    else if (g_ltPassBad)            text_centre(144, "NOT RELIABLE", COL_FAIL, 1);
    else                             text_centre(144, "MEASURING...", COL_DIM, 1);

    // ---- now freeze and measure one full ramp ----
    g_ltFailed = 0; g_ltFailAt = 0; g_ltHave = 0;
    g_ltSkips = 0; g_ltWild = 0; g_ltBadMagic = 0;
    g_ltPassDone = 0;
    g_ltArmed = 0;              // discard everything until the ramp wraps
    g_ltMeasuring = 1;

    // Nothing is drawn until the ramp wraps. The screen holding still IS the measurement.
    uint32_t guard = 0;
    while (!g_ltPassDone && ++guard < 40000000u) link_service();

    g_ltMeasuring = 0;
    if (g_ltArmed) { if (g_ltFailed) g_ltPassBad++; else g_ltPassOk++; }
}

// BENCH: service as tightly as possible and draw nothing. This measures the transport ceiling
// itself; comparing it against the normal path shows what the UI costs.
static void bench_frame(void)
{
    if (g_benchDirty) {                 // paint the banner ONCE, then never again
        g_benchDirty = 0;
        rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
        link_service();
        text_centre(60, "BENCH MODE", COL_WAIT, 2);
        link_service();
        text_centre(90, "measuring link", COL_DIM, 1);
    }
    link_spin(2000);
}

void diag_frame(void)
{
    if (g_bench) bench_frame();
    else         ramp_frame();
}
