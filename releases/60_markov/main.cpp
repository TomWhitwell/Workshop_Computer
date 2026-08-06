// Markov — dual generative Markov chain firmware for the Workshop Computer
//
// Left side: MarkoV — melodic Markov chain over 10 scale degrees, 5 profiles.
// Right side: MarkovPerc — rhythmic Markov chain over 8 hit types, 4 profiles.
//
// === I/O ===
//   Pulse In 1     : Master clock (rising edge advances both chains).
//                    Falls back to CV 2 internal clock if no pulse for >3 s.
//   CV In 1        : Post-scale transpose ±12 semitones (after scale quantisation).
//   CV In 2        : Internal tempo (0 V = 120 BPM, ±full = 20..220 BPM).
//                    In Dual Melody mode: voice B post-scale transpose.
//   Pulse Out 1    : Gate pulse for MarkoV melody (10 ms, only on pitch change)
//   CV Out 1       : Pitch CV for MarkoV melody (V/Oct, calibrated)
//   Pulse Out 2    : Trigger for MarkovPerc hits (incl. ratchets/flams)
//   CV Out 2       : Accent CV for MarkovPerc (0 V..+5 V, 5 levels)
//   Audio Out 1    : Internal synth voice (Dual Melody: voice A)
//   Audio Out 2    : Dual Melody mode: voice B synth
//
// === UI ===
//   Knob X         : MarkoV melody profile (0-4)
//   Knob Y         : MarkovPerc rhythm profile (0-3); Dual Melody: voice B profile
//
//   Switch + Main Knob:
//     UP   — Transpose (-12..+12 semitones, before scale quantisation)
//     MID  — Loop control (symmetric around centre):
//              Centre      = free running, no LEDs
//              CW  → 16/8/4/3/2 beats  = locked replay (clean)
//              CCW → 16/8/4/3/2 beats  = locked replay + 1/8 mutation per step
//              LED: 16=LED0, 8=LED2, 4=LED4, 3=LED5, 2=LED3 (half brightness)
//     DOWN — Scale select (0..11); all LEDs show scale index (half brightness)
//
// === SECONDARY BOOT MODE (hold Switch DOWN at power-on) ===
//   Dual Melody: both engines MarkoV. CV Out 2 = voice B (V/Oct).
//   CV In 2 = voice B post-scale transpose. Audio Out 1/2 = voice A/B.
//
// Integer math only in ProcessSample(). Float used only in constructor LUT init.

#include "ComputerCard.h"
#include "markov_data.h"
#include "markov_scales.h"
#include <cmath>
#include <cstring>

static constexpr int LOOP_BUF_LEN = 16; // max loop length

// Rolling history — always written, used to fill loop buffer on lock engage.
// Must be global (RP2040 flash-placement rule).
static uint8_t g_hist_mel [LOOP_BUF_LEN];
static uint8_t g_hist_perc[LOOP_BUF_LEN];
static uint8_t g_hist_mel2[LOOP_BUF_LEN];

// Working loop buffers — copied from history on lock, mutated in place (CCW mode).
static uint8_t g_loop_mel [LOOP_BUF_LEN];
static uint8_t g_loop_perc[LOOP_BUF_LEN];
static uint8_t g_loop_mel2[LOOP_BUF_LEN];

// MIDI note → 32-bit phase increment LUT (float computed at boot, integer at runtime).
static uint32_t g_midi_phase_inc[128];

// Latched once at boot: true if Switch DOWN held when boot mute expired.
static bool g_dual_melody_mode = false;

// ---------------------------------------------------------------------------
class Markov : public ComputerCard
{
    // --- boot ---
    int32_t  boot_mute = 7200;

    // --- RNG (32-bit LCG) ---
    uint32_t rng_state = 1;

    // --- clocking ---
    int32_t  clock_counter         = 0;
    int32_t  clock_period          = 12000;
    int32_t  no_clock_counter      = 0;
    int32_t  internal_clock_period = 12000;
    bool     use_internal_clock    = false;
    static constexpr int32_t NO_CLOCK_TIMEOUT = 144000;
    static constexpr int32_t PULSE_DURATION   = 480;

    // --- Markov engines ---
    uint8_t  mel_state    = 0;
    uint8_t  mel_profile  = 0;
    uint8_t  perc_state   = MarkovPercData::STATE_REST;
    uint8_t  perc_profile = 0;
    uint8_t  mel2_state   = 0;
    uint8_t  mel2_profile = 0;

    // --- pitch ---
    uint8_t  mel_midi_note      = 60;
    uint8_t  prev_mel_midi_note = 255;
    uint8_t  mel2_midi_note      = 60;
    uint8_t  prev_mel2_midi_note = 255;
    uint8_t  scale_index         = 3;
    int32_t  transpose_manual    = 0;
    int32_t  main_knob_last      = -1;
    static constexpr uint8_t BASE_NOTE = 48;

    // --- ratchet ---
    int32_t  ratchet_times[4] = {};
    int32_t  ratchet_count    = 0;
    int32_t  ratchet_index    = 0;

    // --- pulse timers ---
    int32_t  pulse1_timer  = 0;
    int32_t  pulse2_timer  = 0;
    int32_t  pulse2b_timer = 0;

    // --- synth voice A ---
    uint32_t osc_phase     = 0;
    uint32_t osc_phase_inc = 0;
    int32_t  env_level     = 0;
    int32_t  env_peak      = 0;
    bool     env_releasing = false;
    static constexpr int32_t ENV_ATTACK_RATE   = 131072;
    static constexpr int32_t ENV_RELEASE_COEFF = 65527;
    static constexpr int32_t ENV_PEAK_FULL     = (4095 << 8);
    static constexpr int32_t ENV_PEAK_GRACE    = (1800 << 8);

    // --- synth voice B (dual melody only) ---
    uint32_t osc2_phase     = 0;
    uint32_t osc2_phase_inc = 0;
    int32_t  env2_level     = 0;
    int32_t  env2_peak      = 0;
    bool     env2_releasing = false;

    // --- loop ---
    // Knob zones (11 total):  0-4 = CCW mutating, 5 = centre free, 6-10 = CW clean.
    // Lengths by zone:  CCW 16,8,4,3,2 | free | CW 16,8,4,3,2
    // (zone 0 = CCW-16, zone 4 = CCW-2, zone 5 = free, zone 6 = CW-16, zone 10 = CW-2)
    static constexpr int32_t kZoneLengths[11] = {2,3,4,8,16, 0, 16,8,4,3,2};
    // LED for each active length: 16→0, 8→2, 4→4, 3→5, 2→3
    static constexpr uint8_t kLengthLED[17]  = { // index by length, 0-16
        255,255,3,5,4,255,255,255,2,255,255,255,255,255,255,255,0
    };
    int32_t  loop_zone         = 5;    // default = centre (free)
    int32_t  loop_zone_prev    = -1;
    bool     loop_active       = false;
    bool     loop_mutate       = false;
    int32_t  loop_len          = 0;
    int32_t  loop_step         = 0;    // current playback position within loop
    int32_t  hist_write_pos    = 0;    // next write position in rolling history (mod 16)

    // --- LED timers ---
    int32_t  knob_x_timer      = 0;
    int32_t  knob_y_timer      = 0;
    int32_t  scale_timer       = 0;
    uint8_t  mel_profile_prev  = 255;
    uint8_t  perc_profile_prev = 255;
    static constexpr uint16_t LED_HALF = 2048;

    // --- sample counter ---
    uint32_t sample_counter = 0;

public:
    Markov() {
        for (int n = 0; n < 128; n++) {
            float freq = 440.0f * powf(2.0f, (n - 69) / 12.0f);
            g_midi_phase_inc[n] = (uint32_t)(freq / 48000.0f * 4294967296.0f);
        }
        memset(g_hist_mel,  0, sizeof(g_hist_mel));
        memset(g_hist_perc, MarkovPercData::STATE_REST, sizeof(g_hist_perc));
        memset(g_hist_mel2, 0, sizeof(g_hist_mel2));
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        sample_counter++;

        // 0. Boot mute.
        if (boot_mute > 0) {
            --boot_mute;
            if (boot_mute == 0)
                g_dual_melody_mode = (SwitchVal() == Switch::Down);
            AudioOut1(0);
            PulseOut1(false);
            PulseOut2(false);
            return;
        }

        // 1. UI.
        ProcessUI();

        // 2. External clock.
        bool clock_tick = false;
        if (PulseIn1RisingEdge()) {
            if (clock_counter > 0) clock_period = clock_counter;
            clock_counter      = 0;
            no_clock_counter   = 0;
            use_internal_clock = false;
            clock_tick         = true;
        }
        clock_counter++;
        no_clock_counter++;
        if (no_clock_counter >= NO_CLOCK_TIMEOUT) use_internal_clock = true;

        // 3. Internal clock (normal mode only).
        if (use_internal_clock) {
            if (!g_dual_melody_mode && (sample_counter & 1023u) == 0u) {
                int32_t cv2 = CVIn2();
                int32_t bpm = 120 + (cv2 * 100 / 2048);
                if (bpm < 20)  bpm = 20;
                if (bpm > 220) bpm = 220;
                internal_clock_period = 2880000 / bpm;
            }
            if (clock_counter >= internal_clock_period) {
                clock_counter = 0;
                clock_tick    = true;
            }
        }

        // 4. Clock tick.
        if (clock_tick) {
            if (loop_active) {
                // Load states from working loop buffer.
                mel_state  = g_loop_mel [loop_step];
                perc_state = g_loop_perc[loop_step];
                mel2_state = g_loop_mel2[loop_step];

                // CCW mutate: 1/8 chance per step — recalculate from previous step.
                if (loop_mutate && (rng_next() & 7u) == 0u) {
                    int32_t prev = (loop_step + loop_len - 1) % loop_len;
                    g_loop_mel [loop_step] = NextMelState (g_loop_mel [prev]);
                    g_loop_perc[loop_step] = NextPercState(g_loop_perc[prev]);
                    g_loop_mel2[loop_step] = NextMelState2(g_loop_mel2[prev]);
                    // Play the mutated version.
                    mel_state  = g_loop_mel [loop_step];
                    perc_state = g_loop_perc[loop_step];
                    mel2_state = g_loop_mel2[loop_step];
                }

                loop_step = (loop_step + 1) % loop_len;
            } else {
                // Free running — advance engines.
                AdvanceMelody();
                if (!g_dual_melody_mode) AdvancePerc();
                else                     AdvanceMelody2();
            }

            // Always record to rolling history (free or locked).
            g_hist_mel [hist_write_pos] = mel_state;
            g_hist_perc[hist_write_pos] = perc_state;
            g_hist_mel2[hist_write_pos] = mel2_state;
            hist_write_pos = (hist_write_pos + 1) & (LOOP_BUF_LEN - 1);

            // Pitch + gate.
            if (g_dual_melody_mode) {
                UpdateMelodyPitch();
                UpdateMelodyPitch2();
                if (mel_midi_note != prev_mel_midi_note) {
                    prev_mel_midi_note = mel_midi_note;
                    TriggerEnvelope(false);
                    pulse1_timer = PULSE_DURATION;
                }
                if (mel2_midi_note != prev_mel2_midi_note) {
                    prev_mel2_midi_note = mel2_midi_note;
                    TriggerEnvelope2();
                    pulse2b_timer = PULSE_DURATION;
                }
            } else {
                UpdateMelodyPitch();
                if (mel_midi_note != prev_mel_midi_note) {
                    prev_mel_midi_note = mel_midi_note;
                    pulse1_timer = PULSE_DURATION;
                }
                SchedulePercussion();
                UpdateAccentCV();
            }
        }

        // Timer decrements.
        if (SwitchVal() == Switch::Down) scale_timer = 48000;
        else if (scale_timer > 0)        --scale_timer;
        if (knob_x_timer > 0) --knob_x_timer;
        if (knob_y_timer > 0) --knob_y_timer;

        UpdateLEDs();

        // 5. Ratchet firing (normal mode).
        if (!g_dual_melody_mode) {
            if (ratchet_index < ratchet_count &&
                clock_counter >= ratchet_times[ratchet_index]) {
                bool is_grace = (ratchet_index == 0 &&
                                 (perc_state == MarkovPercData::STATE_FLAM ||
                                  perc_state == MarkovPercData::STATE_ACC_FLAM));
                TriggerEnvelope(is_grace);
                pulse2_timer = PULSE_DURATION;
                ratchet_index++;
            }
        }

        // 6. Pulse outputs.
        PulseOut1(pulse1_timer > 0); if (pulse1_timer  > 0) --pulse1_timer;
        if (g_dual_melody_mode) {
            PulseOut2(pulse2b_timer > 0); if (pulse2b_timer > 0) --pulse2b_timer;
        } else {
            PulseOut2(pulse2_timer  > 0); if (pulse2_timer  > 0) --pulse2_timer;
        }

        // 7. Oscillator + envelope A.
        osc_phase += osc_phase_inc;
        int32_t osc_out = (osc_phase & 0x80000000u) ? 1024 : -1024;
        if (!env_releasing) {
            env_level += ENV_ATTACK_RATE;
            if (env_level >= env_peak) { env_level = env_peak; env_releasing = true; }
        } else {
            env_level = (int32_t)(((int64_t)env_level * ENV_RELEASE_COEFF) >> 16);
        }
        AudioOut1((int16_t)((osc_out * (env_level >> 8)) >> 12));

        // 8. Oscillator + envelope B (dual melody only).
        if (g_dual_melody_mode) {
            osc2_phase += osc2_phase_inc;
            int32_t osc2_out = (osc2_phase & 0x80000000u) ? 1024 : -1024;
            if (!env2_releasing) {
                env2_level += ENV_ATTACK_RATE;
                if (env2_level >= env2_peak) { env2_level = env2_peak; env2_releasing = true; }
            } else {
                env2_level = (int32_t)(((int64_t)env2_level * ENV_RELEASE_COEFF) >> 16);
            }
            AudioOut2((int16_t)((osc2_out * (env2_level >> 8)) >> 12));
            CVOut2MIDINote(mel2_midi_note);
        }

        // 9. Pitch CV every sample.
        CVOut1MIDINote(mel_midi_note);
    }

private:
    // -----------------------------------------------------------------------
    uint32_t __not_in_flash_func(rng_next)() {
        rng_state = 1664525u * rng_state + 1013904223u;
        return rng_state;
    }

    uint8_t __not_in_flash_func(NextMelState)(uint8_t cur) {
        const uint8_t (*row)[MarkovMelody::NUM_STATES] =
            MarkovMelody::profiles[mel_profile];
        int32_t total = 0, weights[MarkovMelody::NUM_STATES];
        for (int j = 0; j < MarkovMelody::NUM_STATES; j++) {
            weights[j] = (int32_t)row[cur][j];
            if (weights[j] < 1) weights[j] = 1;
            total += weights[j];
        }
        int32_t r = (int32_t)(rng_next() % (uint32_t)total);
        for (int j = 0; j < MarkovMelody::NUM_STATES; j++) {
            r -= weights[j]; if (r < 0) return (uint8_t)j;
        }
        return (uint8_t)(MarkovMelody::NUM_STATES - 1);
    }

    // mel2 variant — uses mel2_profile.
    uint8_t __not_in_flash_func(NextMelState2)(uint8_t cur) {
        const uint8_t (*row)[MarkovMelody::NUM_STATES] =
            MarkovMelody::profiles[mel2_profile];
        int32_t total = 0, weights[MarkovMelody::NUM_STATES];
        for (int j = 0; j < MarkovMelody::NUM_STATES; j++) {
            weights[j] = (int32_t)row[cur][j];
            if (weights[j] < 1) weights[j] = 1;
            total += weights[j];
        }
        int32_t r = (int32_t)(rng_next() % (uint32_t)total);
        for (int j = 0; j < MarkovMelody::NUM_STATES; j++) {
            r -= weights[j]; if (r < 0) return (uint8_t)j;
        }
        return (uint8_t)(MarkovMelody::NUM_STATES - 1);
    }

    uint8_t __not_in_flash_func(NextPercState)(uint8_t cur) {
        const uint8_t (*row)[MarkovPercData::NUM_STATES] =
            MarkovPercData::profiles[perc_profile];
        int32_t total = 0, weights[MarkovPercData::NUM_STATES];
        for (int j = 0; j < MarkovPercData::NUM_STATES; j++) {
            weights[j] = (int32_t)row[cur][j];
            if (weights[j] < 1) weights[j] = 1;
            total += weights[j];
        }
        int32_t r = (int32_t)(rng_next() % (uint32_t)total);
        for (int j = 0; j < MarkovPercData::NUM_STATES; j++) {
            r -= weights[j]; if (r < 0) return (uint8_t)j;
        }
        return (uint8_t)(MarkovPercData::NUM_STATES - 1);
    }

    void __not_in_flash_func(AdvanceMelody)()  { mel_state  = NextMelState (mel_state);  }
    void __not_in_flash_func(AdvanceMelody2)() { mel2_state = NextMelState2(mel2_state); }
    void __not_in_flash_func(AdvancePerc)()    { perc_state = NextPercState(perc_state); }

    void __not_in_flash_func(UpdateMelodyPitch)() {
        int32_t pre = (int32_t)BASE_NOTE + (mel_state * 12) / 7 + transpose_manual;
        if (pre < 0) pre = 0; if (pre > 127) pre = 127;
        uint8_t q = QuantizeToScale((uint8_t)pre, scale_index);
        int32_t cv1 = (SwitchVal() != Switch::Down) ? (CVIn1() * 12) / 2048 : 0;
        int32_t n = (int32_t)q + cv1;
        if (n < 0) n = 0; if (n > 127) n = 127;
        mel_midi_note = (uint8_t)n;
        osc_phase_inc = g_midi_phase_inc[mel_midi_note];
    }

    void __not_in_flash_func(UpdateMelodyPitch2)() {
        int32_t pre = (int32_t)BASE_NOTE + (mel2_state * 12) / 7 + transpose_manual;
        if (pre < 0) pre = 0; if (pre > 127) pre = 127;
        uint8_t q = QuantizeToScale((uint8_t)pre, scale_index);
        int32_t cv2 = (SwitchVal() != Switch::Down) ? (CVIn2() * 12) / 2048 : 0;
        int32_t n = (int32_t)q + cv2;
        if (n < 0) n = 0; if (n > 127) n = 127;
        mel2_midi_note = (uint8_t)n;
        osc2_phase_inc = g_midi_phase_inc[mel2_midi_note];
    }

    void __not_in_flash_func(UpdateAccentCV)() {
        using namespace MarkovPercData;
        uint8_t acc;
        switch (perc_state) {
            case STATE_REST:      acc = ACCENT_NONE; break;
            case STATE_HIT:       acc = ACCENT_SOFT; break;
            case STATE_ACC_HIT:   acc = ACCENT_FULL; break;
            case STATE_FLAM:      acc = ACCENT_MED;  break;
            case STATE_ACC_FLAM:  acc = ACCENT_FULL; break;
            case STATE_RATCHET_2: acc = ACCENT_HARD; break;
            case STATE_RATCHET_3: acc = ACCENT_MED;  break;
            case STATE_RATCHET_4: acc = ACCENT_SOFT; break;
            default:              acc = ACCENT_NONE; break;
        }
        CVOut2((int16_t)(((int32_t)acc * 1706) / 5));
    }

    void __not_in_flash_func(SchedulePercussion)() {
        using namespace MarkovPercData;
        ratchet_count = 0; ratchet_index = 0;
        int32_t p = clock_period, p8 = (p/8 > 5) ? p/8 : 5;
        switch (perc_state) {
            case STATE_REST:    break;
            case STATE_HIT:
            case STATE_ACC_HIT:
                ratchet_times[0]=0; ratchet_count=1; break;
            case STATE_FLAM:
            case STATE_ACC_FLAM:
                ratchet_times[0]=0; ratchet_times[1]=p8; ratchet_count=2; break;
            case STATE_RATCHET_2:
                ratchet_times[0]=0; ratchet_times[1]=p/2; ratchet_count=2; break;
            case STATE_RATCHET_3:
                ratchet_times[0]=0; ratchet_times[1]=p/3;
                ratchet_times[2]=(p*2)/3; ratchet_count=3; break;
            case STATE_RATCHET_4:
                ratchet_times[0]=0; ratchet_times[1]=p/4;
                ratchet_times[2]=p/2; ratchet_times[3]=(p*3)/4;
                ratchet_count=4; break;
            default: break;
        }
    }

    void __not_in_flash_func(TriggerEnvelope)(bool grace) {
        env_peak = grace ? ENV_PEAK_GRACE : ENV_PEAK_FULL;
        if (grace) env_level = 0;
        env_releasing = false;
    }

    void __not_in_flash_func(TriggerEnvelope2)() {
        env2_peak = ENV_PEAK_FULL; env2_releasing = false;
    }

    // -----------------------------------------------------------------------
    // ProcessUI
    // -----------------------------------------------------------------------
    void __not_in_flash_func(ProcessUI)() {
        mel_profile = (uint8_t)(KnobVal(Knob::X) * MarkovMelody::NUM_PROFILES / 4096);
        if (mel_profile >= MarkovMelody::NUM_PROFILES) mel_profile = MarkovMelody::NUM_PROFILES - 1;
        if (mel_profile != mel_profile_prev) { mel_profile_prev = mel_profile; knob_x_timer = 48000; }

        if (g_dual_melody_mode) {
            mel2_profile = (uint8_t)(KnobVal(Knob::Y) * MarkovMelody::NUM_PROFILES / 4096);
            if (mel2_profile >= MarkovMelody::NUM_PROFILES) mel2_profile = MarkovMelody::NUM_PROFILES - 1;
            if (mel2_profile != perc_profile_prev) { perc_profile_prev = mel2_profile; knob_y_timer = 48000; }
        } else {
            perc_profile = (uint8_t)(KnobVal(Knob::Y) * MarkovPercData::NUM_PROFILES / 4096);
            if (perc_profile >= MarkovPercData::NUM_PROFILES) perc_profile = MarkovPercData::NUM_PROFILES - 1;
            if (perc_profile != perc_profile_prev) { perc_profile_prev = perc_profile; knob_y_timer = 48000; }
        }

        int32_t mk = KnobVal(Knob::Main);
        switch (SwitchVal()) {
            case Switch::Up: {
                int32_t d = mk - main_knob_last; if (d < 0) d = -d;
                if (main_knob_last < 0 || d > 32) {
                    main_knob_last   = mk;
                    transpose_manual = ((mk - 2048) * 24) / 4096;
                }
                break;
            }
            case Switch::Middle: DecodeLoopKnob(mk); break;
            case Switch::Down: {
                uint8_t si = (uint8_t)(mk * NUM_SCALES / 4096);
                if (si >= NUM_SCALES) si = NUM_SCALES - 1;
                scale_index = si;
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // DecodeLoopKnob — 11 zones: CCW 0-4 (mutating), centre 5 (free), CW 6-10 (clean).
    // On transition to locked: copy last N entries from rolling history to working buffer.
    // -----------------------------------------------------------------------
    void __not_in_flash_func(DecodeLoopKnob)(int32_t knob_val) {
        int32_t zone = knob_val * 11 / 4096;
        if (zone > 10) zone = 10;

        if (zone == loop_zone_prev) return;
        loop_zone_prev = zone;

        int32_t new_len  = kZoneLengths[zone];
        bool    new_free = (new_len == 0);
        bool    now_mutate = (zone < 5);

        if (!new_free) {
            // Fill working buffer from history.
            // hist_write_pos points to the *next* slot to write, so the most recent
            // entry is at (hist_write_pos - 1) & 15, and N entries back starts at
            // (hist_write_pos - new_len) & 15.
            for (int32_t i = 0; i < new_len; i++) {
                int32_t src = (hist_write_pos - new_len + i) & (LOOP_BUF_LEN - 1);
                g_loop_mel [i] = g_hist_mel [src];
                g_loop_perc[i] = g_hist_perc[src];
                g_loop_mel2[i] = g_hist_mel2[src];
            }
            loop_step   = 0;
            loop_len    = new_len;
            loop_mutate = now_mutate;
            loop_active = true;
        } else {
            loop_active = false;
            loop_mutate = false;
        }
        loop_zone = zone;
    }

    // -----------------------------------------------------------------------
    // UpdateLEDs
    // -----------------------------------------------------------------------
    void __not_in_flash_func(UpdateLEDs)() {
        // Priority 1: X/Y knob recently turned — column display, half brightness.
        if (knob_x_timer > 0 || knob_y_timer > 0) {
            uint8_t rp = g_dual_melody_mode ? mel2_profile : perc_profile;
            LedBrightness(0, (knob_x_timer>0 && ((mel_profile>>0)&1)) ? LED_HALF : 0);
            LedBrightness(2, (knob_x_timer>0 && ((mel_profile>>1)&1)) ? LED_HALF : 0);
            LedBrightness(4, (knob_x_timer>0 && ((mel_profile>>2)&1)) ? LED_HALF : 0);
            LedBrightness(1, (knob_y_timer>0 && ((rp>>0)&1)) ? LED_HALF : 0);
            LedBrightness(3, (knob_y_timer>0 && ((rp>>1)&1)) ? LED_HALF : 0);
            LedBrightness(5, (knob_y_timer>0 && ((rp>>2)&1)) ? LED_HALF : 0);
            return;
        }
        // Priority 2: scale knob — half brightness.
        if (scale_timer > 0) {
            for (int i = 0; i < 6; i++)
                LedBrightness(i, ((scale_index >> i) & 1) ? LED_HALF : 0);
            return;
        }
        // Priority 3: loop locked — single LED half brightness for active length.
        if (loop_active) {
            for (int i = 0; i < 6; i++) LedBrightness(i, 0);
            uint8_t led = kLengthLED[loop_len]; // LED for current length
            if (led < 6) LedBrightness(led, LED_HALF);
            return;
        }
        // Priority 4: free (centre) — all LEDs off.
        for (int i = 0; i < 6; i++) LedBrightness(i, 0);
    }
};

// ---------------------------------------------------------------------------
Markov markov_card;

int main()
{
    set_sys_clock_khz(144000, true);
    markov_card.Run();
}
