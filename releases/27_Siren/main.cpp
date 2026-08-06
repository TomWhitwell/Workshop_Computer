#include "pico/stdlib.h"
#include "ComputerCard.h"
#include "wavetables.h"
#include "dsp.h"
#include "oscillators.h"

// Siren: A drone oscillator for Music Thing Modular Workshop System Computer
// Ported from the vhikk-drone norns engine (inspired by Forge TME Vhikk X)
//
// Controls:
//   Switch UP:     Knobs = WARP / SPAN / MORPH
//   Switch MIDDLE: Knobs = SEED / SCAN / BASIS (pitch)
//   Switch DOWN:   Momentary tap cycles oscillator bank (LED shows which)
//
//   CV1: Pitch (added to BASIS)
//   CV2: WARP modulation
//   Pulse1: Gate (drone on/off)
//   Pulse2: Short pulse = randomize SEED, long hold (>=500ms) = cycle bank
//
//   Audio In 1/2: Ring-modulated with drone output
//   Audio Out 1/2: Stereo drone output

// Bank crossfade sample buffer (250ms at 48kHz = 12000 stereo samples)
// File-scope to avoid putting 48KB on the stack
static constexpr int XFADE_BUF_LEN = 12000;
static int16_t xfade_buf_l[XFADE_BUF_LEN];
static int16_t xfade_buf_r[XFADE_BUF_LEN];

// Knob pickup: ignores the knob until it crosses near the current parameter
// value, then tracks it. Prevents jumps when switching pages.
struct KnobPickup
{
    int32_t value = 0;       // Current parameter value
    bool picked_up = true;   // Whether the knob is actively tracking
    static constexpr int32_t THRESHOLD = 80; // ~2% of 4095 range

    // Call every sample with the raw knob reading.
    // Returns the (possibly unchanged) parameter value.
    int32_t update(int32_t knob_val)
    {
        if (picked_up)
        {
            value = knob_val;
        }
        else
        {
            // Check if knob has crossed near the stored value
            int32_t diff = knob_val - value;
            if (diff < 0) diff = -diff;
            if (diff < THRESHOLD)
            {
                picked_up = true;
                value = knob_val;
            }
        }
        return value;
    }

    // Call when switching pages to release the knob
    void release() { picked_up = false; }
};

class Siren : public ComputerCard
{
public:
    Siren()
    {
        init_wavetables();

        // Default parameter values (midpoint)
        warp_raw = 0;
        span_raw = 1200;   // moderate spread
        morph_raw = 2048;  // center waveform
        seed_raw = 0;
        scan_raw = 0;
        basis_raw = 2048;  // ~110 Hz

        gate_open = true;  // start droning

        // Initialize pickup values
        knob_warp.value = warp_raw;
        knob_span.value = span_raw;
        knob_morph.value = morph_raw;
        knob_seed.value = seed_raw;
        knob_scan.value = scan_raw;
        knob_basis.value = basis_raw;
    }

    virtual void __not_in_flash_func(ProcessSample)()
    {
        // ─── Read controls (smoothed) ───
        update_controls();

        // ─── Build oscillator params ───
        OscParams params;
        params.warp = warp_smooth.process(warp_raw);
        params.span = span_smooth.process(span_raw);
        params.morph = morph_smooth.process(morph_raw);
        params.seed = seed_val; // seed changes discretely, no smooth
        params.scan = scan_smooth.process(scan_raw);

        // BASIS: convert to phase increment
        // Range: ~55 Hz to ~440 Hz (2 octaves below to 2 octaves above 110)
        // basis_raw 0-4095 maps exponentially
        int32_t basis = basis_smooth.process(basis_raw);

        // Add CV1 pitch modulation
        if (Connected(Input::CV1))
        {
            basis += CVIn1(); // ±2048 added to 0-4095 range
            if (basis < 0) basis = 0;
            if (basis > 8191) basis = 8191;
        }

        // Exponential frequency mapping: 55 Hz at 0, 110 at 2048, 220 at 4095
        // Approximate 2^(basis/2048) * 55 Hz using piecewise linear
        int32_t freq_hz = exp_freq(basis);
        params.basis_freq = freq_hz_to_phase_inc(freq_hz);

        // CV2 modulates WARP
        if (Connected(Input::CV2))
        {
            int32_t cv_warp = params.warp + (CVIn2() + 2048); // shift CV to 0-4095ish
            if (cv_warp < 0) cv_warp = 0;
            if (cv_warp > 4095) cv_warp = 4095;
            params.warp = cv_warp;
        }

        // ─── Audio In 2: SPAN modulation ───
        // External CV/audio modulates SPAN (detuning/spread)
        if (Connected(Input::Audio2))
        {
            int32_t cv_span = params.span + (AudioIn2() + 2048); // shift to 0-4095ish
            if (cv_span < 0) cv_span = 0;
            if (cv_span > 4095) cv_span = 4095;
            params.span = cv_span;
        }

        // ─── Generate audio ───
        int16_t out_l = 0, out_r = 0;

        if (gate_open)
        {
            // Envelope: simple attack/release
            if (env_level < 32767)
            {
                env_level += 8; // ~85ms attack at 48kHz
                if (env_level > 32767) env_level = 32767;
            }
        }
        else
        {
            if (env_level > 0)
            {
                env_level -= 3; // ~225ms release
                if (env_level < 0) env_level = 0;
            }
        }

        if (env_level > 0)
        {
            // Run only the current bank (never two at once — RP2040 can't handle it)
            process_bank(current_bank, params, out_l, out_r);

            // Apply main envelope
            out_l = q15_mul(out_l, (int16_t)env_level);
            out_r = q15_mul(out_r, (int16_t)env_level);

            if (xfade_active)
            {
                // Equal-power crossfade: blend buffered old bank output with new bank
                // Read old bank audio from buffer (written before the switch)
                int16_t buf_l = xfade_buf_l[xfade_buf_pos];
                int16_t buf_r = xfade_buf_r[xfade_buf_pos];

                int idx = xfade_phase >> 16;
                if (idx > 255) idx = 255;
                int16_t gain_in = xfade_curve[idx];
                int16_t gain_out = xfade_curve[255 - idx];

                out_l = q15_clip((int32_t)q15_mul(out_l, gain_in) + q15_mul(buf_l, gain_out));
                out_r = q15_clip((int32_t)q15_mul(out_r, gain_in) + q15_mul(buf_r, gain_out));

                xfade_phase += XFADE_PHASE_INC;
                xfade_samples_left--;
                if (xfade_samples_left <= 0) xfade_active = false;
            }

            // Always record output to crossfade buffer.
            // During crossfade, read happens before write at the same position,
            // so old data is consumed before being overwritten.
            xfade_buf_l[xfade_buf_pos] = out_l;
            xfade_buf_r[xfade_buf_pos] = out_r;
            xfade_buf_pos++;
            if (xfade_buf_pos >= XFADE_BUF_LEN) xfade_buf_pos = 0;
        }

        // ─── Audio In 1: Processor input ───
        // Runs external audio through waveshaping controlled by current params.
        // Creates stereo from mono by applying slightly different processing
        // to L and R channels. Summed with drone output.
        if (Connected(Input::Audio1))
        {
            int16_t ain = bit12_to_q15(AudioIn1());

            int16_t proc_l = process_input(ain, params, 0);
            int16_t proc_r = process_input(ain, params, 1);

            out_l = q15_clip((int32_t)out_l + proc_l);
            out_r = q15_clip((int32_t)out_r + proc_r);
        }

        // Output (Q15 -> 12-bit)
        AudioOut1(q15_to_12bit(out_l));
        AudioOut2(q15_to_12bit(out_r));

        // ─── CV/Pulse outputs ───

        // CV Out 1: Pitch CV (basis value mapped to ±2048 range)
        // basis is 0-4095 from knob (0-8191 with CV), center at 2048
        CVOut1((int16_t)(basis - 2048));

        // CV Out 2: Envelope level (0-32767 → 0-2047)
        CVOut2((int16_t)(env_level >> 4));

        // Pulse outputs: derived from oscillator phase[0]
        uint32_t osc_phase = get_phase0();
        bool phase_msb = osc_phase >> 31;
        // Pulse Out 1: sub-oscillator clock (high when MSB of phase is set)
        PulseOut1(phase_msb);
        // Pulse Out 2: 1/2 divider — toggle on each rising edge of MSB
        if (phase_msb && !last_phase_msb) half_div_state = !half_div_state;
        last_phase_msb = phase_msb;
        PulseOut2(half_div_state);

        // ─── LEDs: show current bank ───
        update_leds();
    }

private:
    // Oscillator bank instances
    BankSine bank_sine;
    BankCluster bank_cluster;
    BankDiatonic bank_diatonic;
    BankAnalogue bank_analogue;
    BankWaveshape bank_waveshape;
    BankWavetable bank_wavetable;

    // Current bank selection (0-5)
    int current_bank = 0;

    // Buffer crossfade state: old bank's last 250ms is buffered, new bank crossfades in
    int xfade_buf_pos = 0;              // circular buffer write/read position
    bool xfade_active = false;          // true during crossfade
    int32_t xfade_phase = 0;            // Q16 phase for equal-power curve lookup
    int xfade_samples_left = 0;         // countdown during crossfade
    int xfade_old_bank = 0;             // previous bank (for LED display during crossfade)
    // Phase increment: traverse 256-entry table over 12000 samples
    // (255 << 16) / 12000 ≈ 1393
    static constexpr int32_t XFADE_PHASE_INC = 1393;

    // Pulse In 2: dual-purpose timing (short = SEED, long hold = bank cycle)
    bool pulse2_was_high = false;
    uint32_t pulse2_high_counter = 0;
    bool pulse2_bank_cycled = false;
    static constexpr uint32_t PULSE2_LONG_THRESHOLD = 24000; // 500ms at 48kHz

    // Raw parameter values (0-4095)
    int32_t warp_raw, span_raw, morph_raw, seed_raw, scan_raw, basis_raw;
    int32_t seed_val = 0; // actual seed value (changes on trigger)

    // Knob pickup instances (one per parameter)
    KnobPickup knob_warp, knob_span, knob_morph;
    KnobPickup knob_seed, knob_scan, knob_basis;

    // Parameter smoothers
    KnobSmoother warp_smooth, span_smooth, morph_smooth, scan_smooth, basis_smooth;

    // Envelope
    int32_t env_level = 0;
    bool gate_open;

    // Pulse output state for 1/2 divider
    bool last_phase_msb = false;
    bool half_div_state = false;

    // Switch state tracking for bank cycling and page changes
    bool switch_was_down = true; // assume down at boot to ignore initial state
    Switch last_page = Switch::Up; // track which page we were on

    // LED update counter (don't update every sample)
    // Start at threshold so LEDs update on the very first sample
    uint16_t led_counter = 480;

    // LED mapping: bank index -> LED index
    // ComputerCard layout (top-to-bottom, left-to-right):
    //   0  1
    //   2  3
    //   4  5
    // Banks map directly: bank 0 = top-left, bank 1 = top-right, etc.
    static constexpr int led_for_bank[6] = {0, 1, 2, 3, 4, 5};

    void update_controls()
    {
        Switch sw = SwitchVal();

        // Detect page change and release knobs so they need to be "picked up"
        if (sw != Switch::Down && sw != last_page)
        {
            if (sw == Switch::Up)
            {
                // Switching TO page 1: release page 1 knobs
                knob_warp.release();
                knob_span.release();
                knob_morph.release();
            }
            else if (sw == Switch::Middle)
            {
                // Switching TO page 2: release page 2 knobs
                knob_seed.release();
                knob_scan.release();
                knob_basis.release();
            }
            last_page = sw;
        }

        if (sw == Switch::Up)
        {
            // Page 1: WARP / SPAN / MORPH
            warp_raw = knob_warp.update(KnobVal(Knob::Main));
            span_raw = knob_span.update(KnobVal(Knob::X));
            morph_raw = knob_morph.update(KnobVal(Knob::Y));
        }
        else if (sw == Switch::Middle)
        {
            // Page 2: SEED / SCAN / BASIS
            int32_t new_seed_raw = knob_seed.update(KnobVal(Knob::Main));
            if (new_seed_raw != seed_raw) // only knob changes override Pulse2
            {
                seed_raw = new_seed_raw;
                seed_val = seed_raw;
            }
            scan_raw = knob_scan.update(KnobVal(Knob::X));
            basis_raw = knob_basis.update(KnobVal(Knob::Y));
        }

        // Switch down = momentary bank cycle (with crossfade)
        if (sw == Switch::Down)
        {
            if (!switch_was_down)
            {
                // Rising edge: cycle bank
                trigger_bank_cycle();
                switch_was_down = true;
            }
        }
        else
        {
            switch_was_down = false;
        }

        // Pulse1: gate
        if (Connected(Input::Pulse1))
        {
            gate_open = PulseIn1();
        }

        // Pulse2: dual-purpose (short pulse = SEED randomize, long hold = bank cycle)
        if (PulseIn2())
        {
            if (!pulse2_was_high)
            {
                // Rising edge: randomize seed immediately (preserves trigger behavior)
                static uint32_t rng = 42;
                rng = rng * 1664525 + 1013904223;
                seed_val = (rng >> 20) & 4095;
                pulse2_high_counter = 0;
                pulse2_bank_cycled = false;
            }
            pulse2_high_counter++;
            if (pulse2_high_counter >= PULSE2_LONG_THRESHOLD && !pulse2_bank_cycled)
            {
                // Long hold: cycle bank with crossfade
                trigger_bank_cycle();
                pulse2_bank_cycled = true;
            }
            pulse2_was_high = true;
        }
        else
        {
            pulse2_was_high = false;
        }
    }

    void trigger_bank_cycle()
    {
        if (xfade_active) return; // already transitioning
        int target = (current_bank + 1) % NUM_BANKS;
        if (env_level == 0)
        {
            // Silent: instant swap, no crossfade needed
            current_bank = target;
        }
        else
        {
            // Buffer already contains the old bank's recent output.
            // Switch bank immediately and crossfade buffer (old) → new bank.
            xfade_old_bank = current_bank;
            current_bank = target;
            xfade_active = true;
            xfade_phase = 0;
            xfade_samples_left = XFADE_BUF_LEN;
        }
    }

    void process_bank(int bank_idx, const OscParams& params, int16_t& out_l, int16_t& out_r)
    {
        switch (bank_idx)
        {
            case 0: bank_sine.process(params, out_l, out_r); break;
            case 1: bank_cluster.process(params, out_l, out_r); break;
            case 2: bank_diatonic.process(params, out_l, out_r); break;
            case 3: bank_analogue.process(params, out_l, out_r); break;
            case 4: bank_waveshape.process(params, out_l, out_r); break;
            case 5: bank_wavetable.process(params, out_l, out_r); break;
        }
    }

    uint32_t get_bank_phase0(int bank_idx)
    {
        switch (bank_idx)
        {
            case 0: return bank_sine.phase[0];
            case 1: return bank_cluster.phase[0];
            case 2: return bank_diatonic.phase[0];
            case 3: return bank_analogue.phase[0];
            case 4: return bank_waveshape.phase[0];
            case 5: return bank_wavetable.phase[0];
            default: return 0;
        }
    }

    uint32_t get_phase0()
    {
        return get_bank_phase0(current_bank);
    }

    void update_leds()
    {
        led_counter++;
        if (led_counter < 480) return; // Update ~100 Hz
        led_counter = 0;

        if (xfade_active)
        {
            // Crossfading: old bank dims, new bank brightens (equal-power curve)
            int idx = xfade_phase >> 16;
            if (idx > 255) idx = 255;
            int32_t new_brightness = (int32_t)xfade_curve[idx] * 4095 >> 15;
            int32_t old_brightness = (int32_t)xfade_curve[255 - idx] * 4095 >> 15;
            for (int i = 0; i < 6; i++)
            {
                if (i == current_bank)
                    LedBrightness(led_for_bank[i], new_brightness);
                else if (i == xfade_old_bank)
                    LedBrightness(led_for_bank[i], old_brightness);
                else
                    LedOff(led_for_bank[i]);
            }
        }
        else
        {
            for (int i = 0; i < 6; i++)
            {
                if (i == current_bank)
                    LedBrightness(led_for_bank[i], 4095);
                else
                    LedOff(led_for_bank[i]);
            }
        }
    }

    // Process external audio input through waveshaping chain.
    // channel: 0=left, 1=right (applies slight offset for stereo image)
    int16_t process_input(int16_t input, const OscParams& p, int channel)
    {
        int32_t sig = input;

        // Stereo offset: slightly different drive/fold for L vs R
        int32_t warp_offset = channel ? (p.warp + 200) : p.warp;
        if (warp_offset > 4095) warp_offset = 4095;
        int32_t scan_offset = channel ? p.scan : (p.scan + 150);
        if (scan_offset > 4095) scan_offset = 4095;

        // WARP: wavefold intensity (same as DTON bank wavefold)
        if (warp_offset > 100)
        {
            int32_t scaled = sig * (4096 + warp_offset * 3) >> 12;
            int32_t fold_idx = (scaled >> 6) + 512;
            if (fold_idx < 0) fold_idx = 0;
            if (fold_idx > 1023) fold_idx = 1023;
            sig = fold_table[fold_idx];
        }

        // SCAN: tanh saturation
        if (scan_offset > 100)
        {
            int32_t drive = sig * (4096 + scan_offset * 4) >> 12;
            int32_t tanh_idx = (drive >> 7) + 512;
            if (tanh_idx < 0) tanh_idx = 0;
            if (tanh_idx > 1023) tanh_idx = 1023;
            sig = tanh_table[tanh_idx];
        }

        // MORPH: blend between dry and processed (0 = fully processed, 4095 = more dry)
        // This gives a way to control how much processing is applied
        sig = q15_lerp((int16_t)sig, input, p.morph);

        return (int16_t)sig;
    }

    // Approximate exponential frequency mapping
    // input 0-4095 -> ~55-440 Hz (3 octave range)
    // Uses piecewise linear approximation of 2^(x/1365) * 55
    int32_t exp_freq(int32_t input)
    {
        if (input < 0) input = 0;
        if (input > 8191) input = 8191;

        // Split into octave and fraction
        // Each 1365 units = 1 octave (for 0-4095 = 3 octaves)
        // Avoid division: 1/1365 ≈ 48/65536, so x/1365 ≈ x*48>>16
        int32_t octave = (input * 48) >> 16;
        if (octave > 6) octave = 6;
        int32_t frac = input - octave * 1365;

        // Base frequency for each octave: 55, 110, 220, 440, 880...
        int32_t base = 55 << octave;

        // Linear interpolation within octave (approximate exponential)
        // Next octave frequency is base * 2
        // base * frac / 1365 ≈ base * frac * 48 >> 16
        int32_t freq = base + ((int32_t)((int64_t)base * frac * 48 >> 16));

        return freq;
    }
};

int main()
{
    set_sys_clock_khz(144000, true);
    Siren siren;
    siren.EnableNormalisationProbe();
    siren.Run();
}
