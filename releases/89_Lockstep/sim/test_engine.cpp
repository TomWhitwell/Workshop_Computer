// Host self-check for the Lockstep movement engine. No hardware, no Pico SDK.
//   ./build.sh   (compiles and runs)
#include <cassert>
#include <cstdio>
#include "lockstep_engine.h"

static bool chromaInScale(uint8_t note, uint8_t scaleIdx) {
    int c = note % 12;
    const Scale& s = generative_scales[scaleIdx];
    for (int i = 0; i < s.num_notes; i++) if (s.notes[i] == c) return true;
    return false;
}

int main() {
    // 1) Every output note is a scale tone AND positive-CV safe (>= MINNOTE),
    //    and the walk never escapes its range, across all scales and Y settings.
    for (uint8_t sc = 0; sc < NUM_SCALES; sc++) {
        Walk w; w.reseed(0xC0FFEEu + sc);
        for (int i = 0; i < 20000; i++) {
            int32_t range = i % (LOCKSTEP_HALFMAX + 1);          // sweep Y: 0..12
            w.step(range);
            assert(w.pos >= -LOCKSTEP_HALFMAX && w.pos <= LOCKSTEP_HALFMAX);
            uint8_t n = LockstepNote(w.pos, sc);
            assert(n >= LOCKSTEP_MINNOTE);                       // never negative CV
            assert(chromaInScale(n, sc));                        // always on the scale
        }
    }

    // 2) range == 0 pins to the center note (Y fully CCW = single held note).
    {
        Walk w; w.reseed(1);
        for (int i = 0; i < 100; i++) { w.step(0); assert(w.pos == 0); }
    }

    // 3) range > 0 actually moves (not frozen).
    {
        Walk w; w.reseed(42);
        int moved = 0;
        for (int i = 0; i < 200; i++) { w.step(7); if (w.pos != 0) moved++; }
        assert(moved > 0);
    }

    // 4) Ornament (voice B passing tone) stays in range, on-scale, and actually
    //    differs from the base at least sometimes (it's a *passing* tone).
    for (uint8_t sc = 0; sc < NUM_SCALES; sc++) {
        Walk w; w.reseed(0xBEEF + sc);
        int differed = 0;
        for (int i = 0; i < 5000; i++) {
            int32_t range = 1 + (i % LOCKSTEP_HALFMAX);          // 1..12 (range 0 is the frozen case)
            w.step(range);
            int32_t pb = LockstepOrnament(w.pos, range, w.next());
            assert(pb >= -range && pb <= range);
            uint8_t nb = LockstepNote(pb, sc);
            assert(nb >= LOCKSTEP_MINNOTE);
            assert(chromaInScale(nb, sc));
            if (pb != w.pos) differed++;
        }
        assert(differed > 0);
    }

    printf("lockstep engine: all checks passed\n");
    return 0;
}
