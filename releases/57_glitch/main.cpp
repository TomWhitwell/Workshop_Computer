// Glitch — clock-synced beat-repeater for the Workshop Computer
// Inspired by the Phazerville/O&C Glitch applet by Andy Jenkinson
//
// === GLITCH MODE (default — power on with switch in any position except Down) ===
//   Pulse In 1          : Clock input (rising edge = new beat, max 2.33s)
//   CV In 1 > ~0V       : Freeze (stops recording, keeps looping)
//   CV In 2             : Bipolar mod — added to Knob X and Knob Y values
//   Pulse In 2          : External gate (used in Switch MID mode)
//   Main Knob           : 5 zones → ratchet division {1,2,3,4,6}
//                         Remainder within zone → reverse probability threshold
//   Knob X              : Degradation amount — first half: decimation only (1→16x);
//                         second half: decimation stays max, adds bitcrush (0→7 bits)
//   Knob Y              : Degradation probability (how often degradation applies)
//   Switch UP (latch)   : Probabilistic mode — glitch if rng < prob_thresh
//   Switch MID          : External gate mode — glitch while Pulse In 2 HIGH
//   Switch DOWN (moment): Force mode — always glitch
//   Audio In 1          : Input signal
//   Audio Out 1         : Glitched (or pass-through) output
//   Audio Out 2         : Always dry — live pass-through of Audio In 1
//   Pulse Out 1         : Sub-slice clock — fires at every ratchet slice boundary
//   CV Out 1            : Glitch gate — high (+5V) while glitching, low (0V) otherwise
//   CV Out 2            : Descending ramp across each slice (high→low), resets at boundary
//   Pulse Out 2         : Mirror of Pulse In 1 (clock through)
//   LEDs 0..4           : One lit to show current ratchet zone
//   LED 5               : Brightness = reverse probability remainder
//
// === STUTTER MODE (power on with switch held DOWN) ===
//   Pulse In 1          : Clock input (quarter note, max 2.33s)
//   CV In 1 > ~0V       : Freeze (stops recording, loops current shuffle)
//   CV In 2             : Bipolar mod — added to Knob X and Knob Y values
//   Pulse In 2          : External gate (Switch MID mode: HIGH = shuffle this slice)
//   Main Knob           : 5 zones → slice count {1,2,3,4,8}
//                         Remainder → drift probability (LED 5) — how often beat is drawn
//                         from an earlier beat instead of the canonical one
//   Knob X              : Shuffle probability (Switch UP) — 0=always pass-through,
//                         4095=always shuffle (same as Switch DOWN)
//   Knob Y              : Repeat probability — how often a slice stutters before advancing
//   Switch UP (latch)   : Probabilistic — shuffle this slice if rng < Knob X
//   Switch MID          : CV gate — shuffle while Pulse In 2 HIGH, pass-through when LOW
//   Switch DOWN (moment): Force — always shuffle, never pass-through
//   Audio In 1          : Input signal
//   Audio Out 1         : Shuffled (or pass-through) output
//   Audio Out 2         : Always dry — live pass-through of Audio In 1
//   Pulse Out 1         : Sub-slice clock — fires at every slice boundary
//   CV Out 1            : High (+5V) while shuffling this slice, low (0V) during pass-through
//   CV Out 2            : Descending ramp across each slice (high→low), resets at boundary
//   Pulse Out 2         : Mirror of Pulse In 1 (clock through)
//   LEDs 0..4           : One lit to show current slice count zone
//   LED 5               : Brightness = drift probability (how far back beats can reach)
//
// Boot animation identifies which mode loaded:
//   Glitch: LEDs 0,2,4 cycle in order × 3
//   Stutter: LEDs 5,3,1 cycle in reverse order × 3
//
// Integer math only — no floats anywhere.

#include "ComputerCard.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

// 2.33 s at 48kHz = 112000 samples = 224KB.
static constexpr int32_t MAX_BUFFER_SIZE = 112000;

static int16_t  g_buf[MAX_BUFFER_SIZE];  // circular audio buffer
static int32_t  g_write_pos = 0;         // next write position

// ---------------------------------------------------------------------------
// Mode flag — set on first ProcessSample call from switch position at boot
// ---------------------------------------------------------------------------
static bool g_stutter_mode = false;

// ---------------------------------------------------------------------------
// RNG — 32-bit LCG, use high bits only
// ---------------------------------------------------------------------------

static uint32_t g_rng = 1;
static inline uint32_t __not_in_flash_func(rng_next)() {
    g_rng = 1664525u * g_rng + 1013904223u;
    return g_rng;
}

// ---------------------------------------------------------------------------
// Micro-fade — 64-sample linear ramp at loop boundaries to suppress clicks.
//
// amp ranges 0..63; scale factor is >>6 (divide by 64).
// Skipped for very short slices (<128 samples) where ramps would overlap.
// ---------------------------------------------------------------------------

static inline int16_t __not_in_flash_func(apply_fade)(int16_t sample,
                                                       int32_t pos,
                                                       int32_t len)
{
    if (len < 128) return sample;
    int32_t amp;
    if (pos < 64)
        amp = pos;
    else if (pos >= len - 64)
        amp = (len - pos) - 1;
    else
        amp = 63;
    return (int16_t)((sample * amp) >> 6);
}

// ---------------------------------------------------------------------------
// Glitch card
// ---------------------------------------------------------------------------

class Glitch : public ComputerCard
{
    // --- mode detection ---
    bool     mode_locked       = false;  // false until ADC has settled
    int32_t  mode_settle       = 4800;   // wait ~100ms for ADC smoother to stabilise

    // --- boot animation ---
    int32_t  boot_counter      = 0;     // starts after mode is locked (set to 12000 then)

    // --- clock tracking ---
    int32_t  clock_counter    = 0;  // samples since last clock rising edge
    int32_t  master_loop_len  = 0;  // measured beat length (capped at MAX_BUFFER_SIZE)

    // --- slice state ---
    int32_t  slice_start      = 0;  // absolute buffer index where slice began
    int32_t  slice_len        = 1;  // length of one ratchet sub-slice in samples
    int32_t  slice_pos        = 0;  // playback position within current slice

    // --- glitch state (Glitch mode) ---
    bool     do_glitch        = false;  // held for full slice duration
    bool     reverse_flag     = false;  // held for full slice duration
    bool     degrade_active   = false;  // held for full slice duration

    // --- stutter state (Stutter mode) ---
    uint8_t  st_order[8]      = {0,1,2,3,4,5,6,7};  // shuffled slice indices (always fully shuffled)
    int32_t  st_slice_idx     = 0;    // which entry in st_order we're playing
    bool     do_shuffle       = false;// held for full slice: true=read buffer, false=pass-through

    // --- frozen state ---
    bool     frozen           = false;

    // --- output state ---
    bool     slice_boundary   = false;  // true for one sample at each slice reset
    int32_t  ramp_scale       = 0;      // 2047*65536 / (slice_len-1), 16.16 fixed-point

public:
    Glitch() {
        memset(g_buf, 0, sizeof(g_buf));
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        // ----------------------------------------------------------------
        // 0. One-time mode detection — wait for ADC smoother to settle
        //    (knobs[3] starts at 0 which reads as Switch::Down; give it
        //     ~100ms before sampling the true switch position)
        // ----------------------------------------------------------------
        if (!mode_locked) {
            if (mode_settle > 0) {
                mode_settle--;
            } else {
                g_stutter_mode = (SwitchVal() == Switch::Down);
                mode_locked = true;
                boot_counter = 12000;  // start animation now that mode is known
            }
        }

        // ----------------------------------------------------------------
        // 1. Read raw knob values; CV In 2 modulation is mode-dependent:
        //    Glitch:  CV2 added to both X and Y (degradation amount + probability)
        //    Stutter: CV2 added to X only (shuffle probability)
        // ----------------------------------------------------------------
        const int32_t knob_main = KnobVal(Knob::Main);  // 0..4095
        const int32_t cv2       = CVIn2();               // -2048..2047

        auto clamp = [](int32_t v) -> int32_t {
            return v < 0 ? 0 : (v > 4095 ? 4095 : v);
        };
        const int32_t knob_x = clamp(KnobVal(Knob::X) + cv2);
        const int32_t knob_y = clamp(KnobVal(Knob::Y) + (g_stutter_mode ? 0 : cv2));

        // ----------------------------------------------------------------
        // 2. Big Knob zone decode
        //    5 zones of width 819. Zone selects ratchet; remainder is
        //    the probability threshold (scaled 0..4095).
        //    Glitch: remainder = reverse probability
        //    Stutter: remainder = drift probability (how often to reach back)
        // ----------------------------------------------------------------
        static constexpr int32_t kRatchetGlitch[5]  = {1, 2, 3, 4, 6};
        static constexpr int32_t kRatchetStutter[5] = {1, 2, 3, 4, 8};
        static constexpr int32_t kZoneWidth = 819;  // floor(4095/5)

        int32_t zone = knob_main / kZoneWidth;
        if (zone > 4) zone = 4;
        const int32_t remainder    = knob_main % kZoneWidth;
        const int32_t prob_thresh  = (remainder * 4095) / 818;  // 0..4095
        const int32_t ratchet      = g_stutter_mode
                                     ? kRatchetStutter[zone]
                                     : kRatchetGlitch[zone];

        // ----------------------------------------------------------------
        // 3. Degradation parameters from Knob X (Glitch mode only)
        //    First half  (0–2047): decimation only, dec_factor 1→16, crush_bits=0
        //    Second half (2048–4095): dec_factor fixed at 16, crush_bits 0→7
        // ----------------------------------------------------------------
        int32_t crush_bits = 0, dec_factor = 1;
        if (!g_stutter_mode) {
            if (knob_x < 2048) {
                dec_factor  = 1 + ((knob_x * 15) >> 11);  // 1..16
                crush_bits  = 0;
            } else {
                dec_factor  = 16;
                crush_bits  = (knob_x - 2048) >> 8;       // 0..7
            }
        }

        // ----------------------------------------------------------------
        // 4. Freeze: CV In 1 as comparator (> 0 ≈ above 0V)
        // ----------------------------------------------------------------
        frozen = (CVIn1() > 0);

        // ----------------------------------------------------------------
        // 5. Clock edge — measure beat, compute slice geometry
        // ----------------------------------------------------------------
        clock_counter++;

        if (PulseIn1RisingEdge()) {
            // Measure and cap
            master_loop_len = clock_counter;
            if (master_loop_len > MAX_BUFFER_SIZE)
                master_loop_len = MAX_BUFFER_SIZE;
            clock_counter = 0;

            // Slice length for this ratchet setting.
            slice_len = master_loop_len / ratchet;
            if (slice_len < 1) slice_len = 1;

            if (!g_stutter_mode) {
                // --- Glitch: canonical slice_start (one beat back) ---
                slice_start = (g_write_pos - master_loop_len + MAX_BUFFER_SIZE)
                              % MAX_BUFFER_SIZE;

                // Roll glitch/reverse/degrade for first sub-slice
                slice_pos       = 0;
                slice_boundary  = true;
                ramp_scale      = (slice_len > 1) ? (2047 * 65536) / (slice_len - 1) : 0;
                do_glitch       = eval_glitch(prob_thresh, knob_y);
                reverse_flag    = ((rng_next() >> 20) < (uint32_t)prob_thresh);
                degrade_active  = eval_degrade(knob_y);
            } else {
                // --- Stutter: choose slice_start (drift or canonical) ---
                int32_t max_extra = (MAX_BUFFER_SIZE / master_loop_len) - 1;
                if (max_extra < 0) max_extra = 0;

                int32_t beats_back = 1;  // default: canonical
                bool drift = (prob_thresh > 0) && (max_extra > 0)
                             && ((rng_next() >> 20) < (uint32_t)prob_thresh);
                if (drift) {
                    // Reach back a random whole-beat count beyond the canonical one
                    beats_back = 1 + (int32_t)((rng_next() >> 16) % (uint32_t)max_extra);
                }
                slice_start = (g_write_pos
                               - beats_back * master_loop_len
                               + (int32_t)MAX_BUFFER_SIZE * 4)
                              % MAX_BUFFER_SIZE;

                // Always fully reshuffle; pass-through vs. buffer decided per-slice
                reshuffle(ratchet);

                slice_pos      = 0;
                slice_boundary = true;
                ramp_scale     = (slice_len > 1) ? (2047 * 65536) / (slice_len - 1) : 0;
                do_shuffle     = eval_stutter(knob_x);
            }
        }

        // ----------------------------------------------------------------
        // 6. Write to buffer (unless frozen)
        // ----------------------------------------------------------------
        if (!frozen) {
            g_buf[g_write_pos] = AudioIn1();
            g_write_pos = (g_write_pos + 1) % MAX_BUFFER_SIZE;
        }

        // ----------------------------------------------------------------
        // 7. Advance slice position; re-roll at sub-slice boundary
        // ----------------------------------------------------------------
        if (master_loop_len > 0) {
            slice_pos++;
            if (slice_pos >= slice_len) {
                slice_pos      = 0;
                slice_boundary = true;
                ramp_scale     = (slice_len > 1) ? (2047 * 65536) / (slice_len - 1) : 0;

                if (!g_stutter_mode) {
                    do_glitch      = eval_glitch(prob_thresh, knob_y);
                    reverse_flag   = ((rng_next() >> 20) < (uint32_t)prob_thresh);
                    degrade_active = eval_degrade(knob_y);
                } else {
                    // Stutter: repeat or advance, then decide shuffle for new slice
                    bool repeat = ((rng_next() >> 20) < (uint32_t)knob_y);
                    if (!repeat) {
                        st_slice_idx = (st_slice_idx + 1) % ratchet;
                    }
                    do_shuffle = eval_stutter(knob_x);
                }
            }
        }

        // ----------------------------------------------------------------
        // 8. Compute output sample
        // ----------------------------------------------------------------
        int16_t out;

        if (!g_stutter_mode) {
            // --- Glitch mode ---
            if (do_glitch && master_loop_len > 0) {
                int32_t eff_p = slice_pos;
                if (dec_factor > 1 && degrade_active)
                    eff_p = (eff_p / dec_factor) * dec_factor;
                if (reverse_flag)
                    eff_p = (slice_len - 1) - eff_p;
                if (eff_p < 0) eff_p = 0;

                const int32_t read_ptr = (slice_start + eff_p) % MAX_BUFFER_SIZE;
                int16_t sample = g_buf[read_ptr];
                if (crush_bits > 0 && degrade_active)
                    sample = (int16_t)((sample >> crush_bits) << crush_bits);
                sample = apply_fade(sample, slice_pos, slice_len);
                out = sample;
            } else {
                out = AudioIn1();
            }
        } else {
            // --- Stutter mode ---
            if (do_shuffle && master_loop_len > 0) {
                const int32_t phys      = st_order[st_slice_idx % ratchet];
                const int32_t abs_start = (slice_start + phys * slice_len)
                                          % MAX_BUFFER_SIZE;
                const int32_t read_ptr  = (abs_start + slice_pos) % MAX_BUFFER_SIZE;
                out = apply_fade(g_buf[read_ptr], slice_pos, slice_len);
            } else {
                out = AudioIn1();  // pass-through when not shuffling
            }
        }

        // ----------------------------------------------------------------
        // 9. Outputs
        // ----------------------------------------------------------------
        AudioOut1(out);
        AudioOut2(AudioIn1());                        // always dry
        PulseOut1(slice_boundary);                    // sub-slice clock, one sample wide
        PulseOut2(PulseIn1());                        // clock mirror
        slice_boundary = false;

        if (!g_stutter_mode) {
            CVOut1(do_glitch ? 2047 : -2048);         // glitch gate
        } else {
            CVOut1(do_shuffle ? 2047 : -2048);        // shuffle gate
        }

        const int32_t ramp = (slice_len > 1)
            ? 2047 - ((slice_pos * ramp_scale) >> 16)
            : 2047;
        CVOut2((int16_t)ramp);                        // descending slice ramp

        // ----------------------------------------------------------------
        // 10. LEDs
        //     Boot animation: plays for ~0.25 s, then normal behaviour.
        //     Glitch: LEDs 0,2,4 cycle × 3
        //     Stutter: LEDs 5,3,1 cycle in reverse × 3
        // ----------------------------------------------------------------
        if (boot_counter > 0) {
            boot_counter--;
            const int32_t cycle_pos = boot_counter % 2000;
            const int32_t flash_idx = cycle_pos / 666;   // 0, 1, or 2
            const bool led_on       = (cycle_pos % 666) < 333;
            for (int i = 0; i < 6; i++) LedOn(i, false);
            if (led_on) {
                const int led = g_stutter_mode ? (5 - flash_idx * 2)
                                               : (flash_idx * 2);
                LedOn(led, true);
            }
            return;
        }

        // Normal LED behaviour
        for (int i = 0; i < 5; i++)
            LedOn(i, i == zone);
        LedBrightness(5, (uint16_t)prob_thresh);
    }

private:
    // Evaluate whether to glitch this slice (Glitch mode only).
    bool __not_in_flash_func(eval_glitch)(int32_t prob_thresh, int32_t /*knob_y*/) {
        switch (SwitchVal()) {
            case Switch::Up:
                return ((rng_next() >> 20) < (uint32_t)prob_thresh);
            case Switch::Middle:
                return PulseIn2();
            case Switch::Down:
                return true;
            default:
                return false;
        }
    }

    // Evaluate whether degradation applies this slice (Glitch mode only).
    bool __not_in_flash_func(eval_degrade)(int32_t knob_y) {
        return ((rng_next() >> 20) < (uint32_t)knob_y);
    }

    // Evaluate whether to shuffle this slice (Stutter mode). Mirrors eval_glitch().
    // Knob X is the probability threshold for Switch::Up.
    bool __not_in_flash_func(eval_stutter)(int32_t knob_x) {
        switch (SwitchVal()) {
            case Switch::Up:
                return ((rng_next() >> 20) < (uint32_t)knob_x);
            case Switch::Middle:
                return PulseIn2();
            case Switch::Down:
                return true;
            default:
                return false;
        }
    }

    // Fisher-Yates full shuffle of st_order[0..ratchet-1]. Always unconditional.
    // The per-slice pass-through/shuffle decision is made separately by eval_stutter().
    void __not_in_flash_func(reshuffle)(int32_t ratchet) {
        for (int i = 0; i < ratchet; i++) st_order[i] = (uint8_t)i;
        for (int i = ratchet - 1; i > 0; i--) {
            int j = (int)((rng_next() >> 16) % (uint32_t)(i + 1));
            uint8_t tmp = st_order[i];
            st_order[i] = st_order[j];
            st_order[j] = tmp;
        }
        st_slice_idx = 0;
    }
};

// ---------------------------------------------------------------------------
// Global instance — must be global (not in main) to keep large buffer out of
// the 4KB core-0 stack. See ComputerCard NOTES.md §Heap, stack and overflow.
// ---------------------------------------------------------------------------
Glitch glitch_card;

int main()
{
    // 144MHz = 3 × 48kHz — alias-free CV PWM (ComputerCard 0.3.x requirement)
    set_sys_clock_khz(144000, true);
    glitch_card.Run();
}
