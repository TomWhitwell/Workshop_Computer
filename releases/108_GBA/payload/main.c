// payload/main.c — GBA-side multiboot payload for the Workshop Computer link.
//
// Runs from EWRAM after the RP2040 uploads it via BIOS Multiboot. It is a PSG synth voice
// played by the modular: the Workshop streams its five inputs down, this side owns pitch
// tracking, the modulation matrix, the envelope, the sound and the UI.
//
// Order of business, and the order matters:
//   1. LCD up and the title drawn IMMEDIATELY, before anything else. A blank screen therefore
//      means the image never ran — a completely different fault from "ran but the link is
//      quiet", and before this the two were indistinguishable from the bench.
//   2. PSG up (psg_init sets SOUNDCNT_X bit 7 first, without which every other sound register
//      write is silently discarded).
//   3. Serial slave up, armed, and the IRQ pointed at it.
//   4. Loop: run the control tick, draw a frame, keep the link serviced throughout.
//
// Dependency-free (no libgba, no libc) so it builds with a bare arm-none-eabi toolchain
// targeting armv4t. See build.sh.

#include <stdint.h>

#include "gfx.h"
#include "link.h"
#include "psg.h"
#include "synth.h"
#include "ui.h"
#include "diag.h"

#define REG_BASE   0x04000000
#define REG_VCOUNT (*(volatile uint16_t *)(REG_BASE + 0x0006))

// Called from inside every drawing routine and from the frame wait. Servicing the link was
// never the whole job: the synth's control tick has to keep running while the screen is being
// drawn, or its rate becomes the frame rate and every envelope and portamento time changes
// depending on which page you happen to be looking at.
void gfx_idle(void)
{
    link_service();
    synth_update();
}

int main(void)
{
    // ---- 1. LCD up ----
    gfx_init();

    // ---- 2. Sound, 3. link ----
    synth_init();      // calls psg_init(), which sets the master enable first
    link_init();
    ui_init();

    // NO SPLASH IN THE NORMAL CASE.
    //
    // There used to be a title card here. It served as proof the payload had booted, but that
    // job now belongs to the green screen crt0.s paints before the C runtime even exists — and
    // the host starts talking within a few milliseconds of the link coming up, so the card was
    // on screen for about as long as it took to clock one word. Not long enough to read, and
    // long enough to look like a glitch.
    //
    // The one case where a message earns its place is the host NOT talking, so that is the only
    // case that gets one. Until then the green boot proof stays up, which is itself the useful
    // reading: the image ran.
    {
        uint32_t guard = 0;
        while (g_rx == 0 && ++guard < 200000u) gfx_idle();
        if (g_rx == 0) {
            rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
            text_centre(56, "MTM WORKSHOP COMPUTER", COL_TITLE, 1);
            text_centre(72, "GBA PSG VOICE", COL_OK, 1);
            rect(40, 88, SCREEN_W - 80, 1, COL_DIM);
            text_centre(100, "WAITING FOR HOST", COL_WAIT, 1);
            while (g_rx == 0) gfx_idle();
        }
    }

    for (;;) {
        synth_update();

        // The diagnostics take over the whole screen when their magic words arrive. Their
        // magics live under the protocol tag reserved for them, so a live stream word can
        // never fall into this by accident.
        if (diag_active()) {
            diag_frame();
            continue;
        }

        ui_frame();

        // Pace to roughly one frame without ever blocking the link or stalling the synth.
        while (REG_VCOUNT <  SCREEN_H) gfx_idle();
        while (REG_VCOUNT >= SCREEN_H) gfx_idle();
    }
}
