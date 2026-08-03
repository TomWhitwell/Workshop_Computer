#include "ComputerCard.h"
#include <stdint.h>

/*
 * Cellular Automata Sequencer for Music Thing Workshop Computer
 *
 * 4x4 grid (16 cells) using 2D rules derived from CA 90 & 150.
 *
 * I/O MAPPING
 * -----------
 * Pulse In 1  = External clock
 * Pulse In 2  = Seed trigger (injects energy into corners)
 * CV In 1     = Seed probability / chaos modulation (0..5V scales chance)
 * CV In 2     = Rule morph (0V = Rule 90, ~5V = Rule 150; unpatched = 50/50 mix)
 *
 * CV Out 1    = Quantized 1V/oct melody (root set by X, scale by Y)
 * CV Out 2    = Left/Right imbalance CV
 * Pulse Out 1 = Master gate (any cell active)
 * Pulse Out 2 = Accent gate (center 4 cells)
 * Audio Out 1 = Bitmask of cells 0-7 (for external decoding if needed)
 *
 * Main Knob   = Clock divider (ext) or rate (free run)
 * X Knob      = Root note (C .. B, wraps within a fixed octave)
 * Y Knob      = Scale select
 * Switch Up   = Free run (internal clock)
 * Switch Mid  = External clock
 * Switch Down = Reset grid to single seed
 *
 * LEDs show top-left 3x2 window of the 4x4 grid.
 */

class CA_Sequencer : public ComputerCard
{
public:
    void ProcessSample() override
    {
        // --- Read controls ---
        int32_t mainK = KnobVal(Main);
        int32_t xK = KnobVal(X);
        int32_t yK = KnobVal(Y);
        Switch sw = SwitchVal();

        // --- Clock handling ---
        bool tick = false;

        if (sw == Up)
        {
            // Free run: Main knob sets period in samples (~10ms to ~1s)
            int rate = 480 + (mainK * 12); // 480 .. ~48k samples
            if (++freeRunCounter >= rate)
            {
                freeRunCounter = 0;
                tick = true;
            }
        }
        else
        {
            // External clock: Main knob sets division (1..128)
            int division = (mainK >> 5) + 1;
            if (PulseIn1RisingEdge())
            {
                if (++clockCounter >= division)
                {
                    clockCounter = 0;
                    tick = true;
                }
            }
        }

        // --- Reset ---
        if (sw == Down && SwitchChanged())
        {
            grid = 0x0001; // single seed
            tick = false;  // don't step on reset frame
        }

        // --- Seeding ---
        // Pulse In 2 seeds corners on demand
        // CV In 1 sets probability of spontaneous seeding per step
        bool seedTrigger = PulseIn2RisingEdge();
        int seedProb = CVIn1() >> 4; // 0..255 probability
        if (seedProb > 255) seedProb = 255;

        if (seedTrigger)
        {
            SeedCorners();
        }
        else if (tick && (LCG() & 0xFF) < (uint32_t)seedProb)
        {
            SeedCorners();
        }

        // --- Rule morph ---
        // CV In 2 drives rule morph; if unpatched/low, default to 50/50 mix
        int32_t morph = CVIn2();
        if (morph < 32) morph = 2048;     // default 50/50
        if (morph > 4095) morph = 4095;

        // --- Step the CA ---
        if (tick)
        {
            ComputeNextGeneration(morph);
        }

        // --- OUTPUTS ---

        int active = CountActive();

        // CV Out 1: quantized melody from active-cell count
        int rootNote = 48 + ((xK * 12) >> 12); // C2..B2
        int scaleIdx = (yK * numScales) >> 12;
        uint8_t note = QuantizeNote(rootNote, scaleIdx, active);
        CVOut1MIDINote(note);

        // CV Out 2: left/right imbalance (-4..+4 mapped to full range)
        int left = CountRegion(0, 0, 4, 2);
        int right = CountRegion(0, 2, 4, 2);
        CVOut2((left - right) * 512);

        // Pulse Out 1: master gate
        PulseOut1(active > 0);

        // Pulse Out 2: accent on center cells (1,1), (1,2), (2,1), (2,2)
        bool centerHit = ReadCell(1,1) || ReadCell(1,2) || ReadCell(2,1) || ReadCell(2,2);
        PulseOut2(centerHit);

        // Audio Out 1: bit-mask of cells 0-7 as audio pulses
        int16_t bits = 0;
        for (int i = 0; i < 8; i++)
        {
            if (grid & (1 << i)) bits += 512;
        }
        AudioOut1(bits - 2048);

        // --- LED display (top-left 3x2 window) ---
        LedOn(0, ReadCell(0,0));
        LedOn(1, ReadCell(0,1));
        LedOn(2, ReadCell(1,0));
        LedOn(3, ReadCell(1,1));
        LedOn(4, ReadCell(2,0));
        LedOn(5, ReadCell(2,1));
    }

private:
    uint16_t grid = 0x0001; // initial single seed
    int freeRunCounter = 0;
    int clockCounter = 0;
    uint32_t lcgState = 12345;

    // Scale tables
    static constexpr uint8_t scaleChromatic[] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static constexpr uint8_t scaleMajor[]     = {0,2,4,5,7,9,11};
    static constexpr uint8_t scaleMinor[]     = {0,2,3,5,7,8,10};
    static constexpr uint8_t scalePentMinor[] = {0,3,5,7,10};
    static constexpr uint8_t scaleDorian[]    = {0,2,3,5,7,9,10};

    struct Scale
    {
        const uint8_t* steps;
        uint8_t len;
    };

    static constexpr Scale scales[] = {
        {scaleChromatic, 12},
        {scaleMajor,      7},
        {scaleMinor,      7},
        {scalePentMinor,  5},
        {scaleDorian,     7},
    };
    static constexpr int numScales = sizeof(scales) / sizeof(scales[0]);

    // Fast pseudo-random (LCG) -- stays in RAM, no libc calls
    uint32_t __not_in_flash_func(LCG)()
    {
        lcgState = 1664525 * lcgState + 1013904223;
        return lcgState;
    }

    inline bool __not_in_flash_func(ReadCell)(int r, int c)
    {
        if (r < 0 || r > 3 || c < 0 || c > 3) return false;
        return grid & (1 << (r * 4 + c));
    }

    inline void __not_in_flash_func(WriteCell)(uint16_t &g, int r, int c, bool val)
    {
        int bit = r * 4 + c;
        if (val) g |= (1 << bit);
        else g &= ~(1 << bit);
    }

    int __not_in_flash_func(CountActive)()
    {
        // Brian Kernighan's bit counter
        int count = 0;
        uint16_t g = grid;
        while (g) { count++; g &= g - 1; }
        return count;
    }

    int __not_in_flash_func(CountRegion)(int r0, int c0, int h, int w)
    {
        int count = 0;
        for (int r = r0; r < r0 + h; r++)
            for (int c = c0; c < c0 + w; c++)
                if (ReadCell(r, c)) count++;
        return count;
    }

    int __not_in_flash_func(VonNeumannNeighbors)(int r, int c)
    {
        int n = 0;
        if (r > 0 && ReadCell(r - 1, c)) n++;
        if (r < 3 && ReadCell(r + 1, c)) n++;
        if (c > 0 && ReadCell(r, c - 1)) n++;
        if (c < 3 && ReadCell(r, c + 1)) n++;
        return n;
    }

    uint8_t __not_in_flash_func(QuantizeNote)(int root, int scaleIdx, int degree)
    {
        const Scale& s = scales[scaleIdx];
        int octave = degree / s.len;
        int note = root + octave * 12 + s.steps[degree % s.len];
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        return (uint8_t)note;
    }

    void __not_in_flash_func(SeedCorners)()
    {
        // NLC-style: inject into 4 corners
        // XOR with existing state so repeated seeds evolve the pattern
        grid ^= 0x9009; // bits 0, 3, 12, 15
    }

    void __not_in_flash_func(ComputeNextGeneration)(int32_t morph)
    {
        uint16_t next = 0;

        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                int n = VonNeumannNeighbors(r, c);
                bool center = ReadCell(r, c);

                // 2D interpretation of CA rules:
                // Rule 90:  XOR of neighbors (parity)
                // Rule 150: center XOR XOR of neighbors
                bool r90 = (n & 1);
                bool r150 = center ^ (n & 1);

                bool newState;
                if (morph < 1365)
                    newState = r90;                    // pure 90
                else if (morph > 2730)
                    newState = r150;                   // pure 150
                else
                    newState = (LCG() & 0xFFF) < (uint32_t)morph ? r150 : r90; // probabilistic morph

                WriteCell(next, r, c, newState);
            }
        }

        grid = next;
    }
};

int main()
{
    CA_Sequencer card;
    card.Run(); // never returns
    return 0;
}
