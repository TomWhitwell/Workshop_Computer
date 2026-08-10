#ifndef DSP_BLOCKS_H
#define DSP_BLOCKS_H

// ============================================================================
// dsp_blocks.h  —  45_bends Multi-FX DSP Blocks
// ============================================================================
// Six sequential stereo effect stages, each a self-contained fixed-point C++
// struct. All blocks run on Core 1 at 48 kHz inside ProcessSample().
//
// Signal convention: int16_t Q15, range [-32768, 32767] ≡ [-1.0, 1.0).
// All internal state that accumulates energy is promoted to int32_t to keep
// sufficient headroom without the FPU the RP2040 lacks.
//
// Block order in the chain:
//   1. ChorusBlock           — 90s BBD-style stereo chorus / vibrato
//   2. CodecDemolisherBlock  — lo-fi decimator, MP3-ring artifacts, glitch gate
//   1. MultiTapDelayBlock    — stereo tape echo with 3 rhythmic taps
//   4. GlitcherBlock         — phrase-loop granular stutter sampler
//   5. FilterBlock           — morphable Chamberlin SVF (LP/BP/HP)
//   6. ReverbBlock           — Schroeder plate (4 comb + 2 allpass per side)
// ============================================================================

#include "fixed_math.h"
#include <string.h>   // memset
#include <atomic>
#include "hardware/interp.h"

extern uint32_t rand_seed;
extern int16_t mulaw_decode_table[256];
extern std::atomic<bool> vis_delay_pulse;
extern std::atomic<bool> vis_codec_glitch;
extern std::atomic<uint16_t> vis_input_level;

// Fast G.711 mu-law encoder
inline uint8_t encode_mulaw(int16_t sample) {
    int32_t x = sample;
    int32_t sign = (x < 0) ? 0x80 : 0x00;
    if (x < 0) x = -x;
    x += 132;
    if (x > 32767) x = 32767;

    int32_t exponent;
    if (x < 2048) {
        if (x < 512) {
            if (x < 256) {
                exponent = 7;
            } else {
                exponent = 8;
            }
        } else {
            if (x < 1024) {
                exponent = 9;
            } else {
                exponent = 10;
            }
        }
    } else {
        if (x < 8192) {
            if (x < 4096) {
                exponent = 11;
            } else {
                exponent = 12;
            }
        } else {
            if (x < 16384) {
                exponent = 13;
            } else {
                exponent = 14;
            }
        }
    }

    int32_t mantissa = (x >> (exponent - 4)) & 0x0F;
    return (uint8_t)(sign | ((exponent - 7) << 4) | mantissa);
}

// G.711 mu-law decoder optimized to a single memory lookup
inline int16_t decode_mulaw(uint8_t u_val) {
    return mulaw_decode_table[u_val];
}

// ============================================================================
// Hardware Interpolator Setup (Core 1 only — must be called once from Core 1)
// INTERP0 → BLEND mode  : single-cycle linear interpolation for delay reads
// INTERP1 → CLAMP mode  : branch-free Q15 saturation (replaces saturate_q15)
// ============================================================================
inline void init_hardware_interp() {
    // INTERP0: blend mode (INTERP0 lane 0 only supports BLEND bit)
    interp_config c0 = interp_default_config();
    interp_config_set_blend(&c0, true);   // lane1 result = lerp(BASE0, BASE1, frac8)
    interp_config_set_signed(&c0, true);  // signed audio samples
    interp_set_config(interp0, 0, &c0);
    // Lane 1: default pass-through (blend reads from lane 0 automatically)
    interp_config c0l1 = interp_default_config();
    interp_config_set_signed(&c0l1, true);
    interp_set_config(interp0, 1, &c0l1);

    // INTERP1: clamp mode (INTERP1 lane 0 only supports CLAMP bit)
    interp_config c1 = interp_default_config();
    interp_config_set_clamp(&c1, true);
    interp_config_set_signed(&c1, true);
    interp_set_config(interp1, 0, &c1);
    interp1->base[0] = (uint32_t)(int32_t)(-32768); // lower clamp bound
    interp1->base[1] = (uint32_t)(int32_t)(32767);  // upper clamp bound
}

// ============================================================================
// Fixed-Point DC Blocker (High-Pass Filter with cutoff at ~10 Hz @ 48 kHz)
// Prevents feedback DC accumulation in feedback loops.
// ============================================================================
struct DCBlocker {
    int32_t x1 = 0;
    int32_t y1 = 0;

    void init() {
        x1 = 0;
        y1 = 0;
    }

    int16_t process(int16_t in, int32_t r_q15 = 32751) {
        int32_t x = in;
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        // 32751 / 32768 ≈ 0.9995 (ultra-tight DC cutoff ~1.9 Hz at 24 kHz)
        int32_t y = x - x1 + ((y1 * r_q15) >> 15);
        x1 = x;
        y1 = clamp_i32(y, -1000000, 1000000);
        return saturate_q15(y);
    }
};

// ============================================================================
// 1.  CHORUS BLOCK
// ============================================================================
// Classic bucket-brigade delay (BBD) chorus emulation.
// Overhauled for classic vintage dual-phase triangle LFO modulation (180° out of phase)
// and a high-pass filter in the wet path to keep the low end clean.
//
// Parameters (all int32_t in range [0, 32767]):
//   mainMix     — Wet / dry blend.  0 = fully dry, 32767 = fully wet.
//   rate        — LFO speed.       Slow (0) → Fast (32767). CV1 warps it.
//   depthFeedback — Dual-function:
//                   [0, 16383] → Depth scales from 0 to max. Feedback = 0.
//                   [16384, 32767] → Depth stays at max; Feedback rises to 100%.
// ============================================================================
struct ChorusBlock {
    // Stereo BBD delay lines – 1024 samples each (~21 ms @ 48 kHz)
    int16_t  delayL[1024];
    int16_t  delayR[1024];
    uint16_t write_ptr  = 0;

    // LFO phase accumulator – wraps naturally
    uint16_t lfo_phase  = 0;

    // 1-pole LPF state for BBD emulation
    int32_t lp_stateL = 0;
    int32_t lp_stateR = 0;

    // Vintage wet-path high-pass filters to cut sub-bass mud (cutoff ~110 Hz)
    int32_t hp_x1L = 0, hp_y1L = 0;
    int32_t hp_x1R = 0, hp_y1R = 0;

    void init() {
        memset(delayL, 0, sizeof(delayL));
        memset(delayR, 0, sizeof(delayR));
        write_ptr = 0;
        lfo_phase = 0;
        lp_stateL = 0;
        lp_stateR = 0;
        hp_x1L = hp_y1L = hp_x1R = hp_y1R = 0;
    }

    __attribute__((always_inline)) inline void process(int16_t inL, int16_t &outL, int16_t inR, int16_t &outR,
                 int32_t mainMix, int32_t rate, int16_t depth, int32_t feedback, int32_t xor_mask,
                 int32_t cv1Warp, int32_t raw_depth_fb)
    {
        auto get_tri = [](uint16_t phase) -> int16_t {
            int32_t tmp = (phase < 32768) ? ((phase << 1) - 32768) : (32767 - ((phase - 32768) << 1));
            return (int16_t)tmp;
        };

        int32_t eff_feedback = feedback;
        int32_t res_xor = 0;
        int32_t lp_coef = 16384; // Default cutoff ~6kHz

        if (raw_depth_fb >= 22500) {
            if (raw_depth_fb < 31500) {
                // Clean resonant zone (69% → 96% knob):
                // Feedback ramps smoothly from 27000 (~82% short pluck) to 32767 (100% infinite sustain)
                int32_t t = ((raw_depth_fb - 22500) * 32767) / (31500 - 22500);
                eff_feedback = 27000 + ((t * (32767 - 27000)) >> 15);
                res_xor = 0;
            } else {
                // Corrupt tip (top ~4%): feedback 32767, mild scramble 0→8
                eff_feedback = 32767;
                int32_t t = ((raw_depth_fb - 31500) * 32767) / (32767 - 31500);
                res_xor = (t * 8) >> 15;
            }
        }

        if (mainMix < 50) {
            outL = inL;
            outR = inR;
            delayL[write_ptr] = inL;
            delayR[write_ptr] = inR;
            write_ptr = (write_ptr + 1) & 0x3FF;
            
            if (raw_depth_fb >= 22500) {
                uint16_t phase_inc = (uint16_t)(1 + (rate >> 10));
                lfo_phase += phase_inc;
            } else {
                int32_t eff_rate = rate + (cv1Warp * 4);
                eff_rate = clamp_i32(eff_rate, 0, 32767);
                uint16_t phase_inc = (uint16_t)(1 + (eff_rate >> 10));
                lfo_phase += phase_inc;
            }
            return;
        }

        int32_t delay_L_q16;
        int32_t delay_R_q16;

        if (raw_depth_fb >= 22500) {
            // Resonator Mode: Knob X (rate) directly controls pitch (delay time)
            int32_t pitch_ctrl = rate;
            int32_t base_delay = 1000 - ((pitch_ctrl * (1000 - 12)) >> 15);
            
            // Bipolar CV1 (cv1Warp) V/Oct scaling: delay = base_delay * 1024 / (1024 + cv1Warp)
            int32_t delay_val = base_delay;
            if (cv1Warp != 0) {
                int32_t denominator = 1024 + cv1Warp;
                if (denominator < 24) denominator = 24; // clamp to prevent division by zero
                delay_val = (base_delay * 1024) / denominator;
            }
            delay_val = clamp_i32(delay_val, 12, 1000);

            // Pitch-dependent LPF cutoff: brighter filter in Karplus mode allows long acoustic string sustain
            lp_coef = clamp_i32(28000 - ((delay_val * 10000) >> 10), 16000, 30000);

            // Add a tiny organic LFO drift (depth = 1 sample) to keep the drone alive
            uint16_t phase_inc = (uint16_t)(1 + (rate >> 11));
            lfo_phase += phase_inc;
            
            int16_t lfoL = get_tri(lfo_phase);
            int16_t lfoR = get_tri((uint16_t)(lfo_phase + 32768u));
            
            // Tiny ±1 sample BBD drift (depth_samples = 1)
            delay_L_q16 = (delay_val << 16) + (((int32_t)lfoL * 1) << 1);
            delay_R_q16 = (delay_val << 16) + (((int32_t)lfoR * 1) << 1);
        } else {
            // Normal Chorus: Knob X controls LFO speed
            int32_t eff_rate = rate + (cv1Warp * 4);
            eff_rate = clamp_i32(eff_rate, 0, 32767);
            uint16_t phase_inc = (uint16_t)(1 + (eff_rate >> 10));
            lfo_phase += phase_inc;

            int16_t lfoL = get_tri(lfo_phase);
            int16_t lfoR = get_tri((uint16_t)(lfo_phase + 32768u));

            int32_t depth_samples = (int32_t)depth * 28 >> 15;
            delay_L_q16 = (180 << 16) + (((int32_t)lfoL * depth_samples) << 1);
            delay_R_q16 = (180 << 16) + (((int32_t)lfoR * depth_samples) << 1);
        }
        
        if (delay_L_q16 < (1 << 16)) delay_L_q16 = (1 << 16);
        if (delay_R_q16 < (1 << 16)) delay_R_q16 = (1 << 16);

        delayL[write_ptr] = inL;
        delayR[write_ptr] = inR;

        auto read_frac = [](const int16_t *buf, int32_t wp_q16, int32_t delay_q16) -> int16_t {
            int32_t  rp_q16 = wp_q16 - delay_q16;
            int32_t  idx    = (rp_q16 >> 16) & 0x3FF;
            int32_t  nxt    = (idx + 1)       & 0x3FF;
            uint16_t frac   = (uint16_t)(rp_q16 & 0xFFFF);
            int16_t  y0     = buf[idx];
            int16_t  y1     = buf[nxt];
            return lerp_delay_q15(y0, y1, frac);
        };

        int16_t wetL = read_frac(delayL, (write_ptr << 16), delay_L_q16);
        int16_t wetR = read_frac(delayR, (write_ptr << 16), delay_R_q16);

        // BBD low-pass filter (cutoff depends on mode)
        lp_stateL += ((wetL - lp_stateL) * lp_coef) >> 15;
        wetL = (int16_t)lp_stateL;
        lp_stateR += ((wetR - lp_stateR) * lp_coef) >> 15;
        wetR = (int16_t)lp_stateR;

        // Vintage HPF on wet path (cutoff ~110 Hz in chorus, ~15 Hz in Karplus to preserve bass fundamental)
        int32_t hp_coef = (raw_depth_fb >= 22500) ? 32740 : 32000;
        int32_t yL = (int32_t)wetL - hp_x1L + ((hp_y1L * hp_coef) >> 15);
        hp_x1L = (int32_t)wetL;
        hp_y1L = clamp_i32(yL, -1000000, 1000000);
        wetL = saturate_q15(yL);

        int32_t yR = (int32_t)wetR - hp_x1R + ((hp_y1R * hp_coef) >> 15);
        hp_x1R = (int32_t)wetR;
        hp_y1R = clamp_i32(yR, -1000000, 1000000);
        wetR = saturate_q15(yR);

        // Feedback loop
        if (eff_feedback > 0) {
            int32_t fbL = (int32_t)inL + (((int32_t)wetL * eff_feedback) >> 15);
            int32_t fbR = (int32_t)inR + (((int32_t)wetR * eff_feedback) >> 15);
            
            int16_t wrL = soft_limit_q15(fbL);
            int16_t wrR = soft_limit_q15(fbR);
            
            if (raw_depth_fb >= 22500) {
                if (res_xor > 0) {
                    uint8_t byteL = encode_mulaw(wrL);
                    uint8_t byteR = encode_mulaw(wrR);
                    byteL ^= (res_xor & 0x0F);
                    byteR ^= (res_xor & 0x0F);
                    wrL = decode_mulaw(byteL);
                    wrR = decode_mulaw(byteR);
                }
            } else if (xor_mask > 0) {
                wrL ^= (uint16_t)xor_mask;
                wrR ^= (uint16_t)xor_mask;
            }
            
            delayL[write_ptr] = wrL;
            delayR[write_ptr] = wrR;
        }

        write_ptr = (write_ptr + 1) & 0x3FF;

        if (raw_depth_fb >= 22500) {
            // Volume leveling: prevent Karplus string resonance from blowing up in loudness
            wetL = (int16_t)(((int32_t)wetL * 22000) >> 15);
            wetR = (int16_t)(((int32_t)wetR * 22000) >> 15);
        }

        outL = split_mix_q15(inL, wetL, (int16_t)mainMix);
        outR = split_mix_q15(inR, wetR, (int16_t)mainMix);
    }
};

// ============================================================================
// 2.  CODEC DEMOLISHER BLOCK
// ============================================================================
// Simulates the artefacts of digital audio degradation: sample-rate reduction,
// MP3-style resonant ringing at codec crossover frequencies, packet dropout
// (CD-skip / lossy-stream style), and raw bit-level XOR corruption.
//
// Internal bandpass filter: second-order state variable, promoted to int32
// states for numerical stability at high Q.
//
// Parameters:
//   mainMix      — Wet / dry blend.
//   downsample   — Decimation factor.  0 = no change; 32767 = ~64× hold.
//   ringingXor   — [0,16383] → MP3 ring amount. [16384,32767] → + bit corruption
//                  and frame dropout probability.
//   cv2Corruption — CV2: additional corruption/destruction amount.
//   pulse2Scramble — Pulse 2 jack: instant burst of frame-drop + XOR chaos.
// ============================================================================

// Real-time variable-bitrate G.711 mu-law logarithmic compander simulation
inline int16_t compress_expand_mulaw_variable(int16_t sample, int32_t insanity) {
    if (insanity <= 0) return sample;

    // Fast path: encode to mu-law 8-bit index, table-lookup decode
    int32_t x = sample;
    int32_t sign = (x < 0) ? 1 : 0;
    if (x < 0) x = -x;

    uint16_t abs_val = (uint16_t)x;
    uint8_t exponent = 0;
    if (abs_val >= 256) {
        exponent = (31 - __builtin_clz(abs_val)) - 7;
    }
    if (exponent > 7) exponent = 7;

    uint8_t mantissa = (exponent > 0)
        ? (abs_val >> (exponent + 1)) & 0xF
        : (abs_val >> 2) & 0xF;

    // Build full-precision mu-law index and crushed index (drop low mantissa bits)
    uint8_t idx_full = (sign << 7) | (exponent << 4) | mantissa;

    // Scale insanity to select bit-depth reduction: 0..3 levels
    int32_t val = insanity * 3; // 0 to ~98301
    int32_t level = val >> 15;
    int16_t fade = val & 0x7FFF;

    // Mask mantissa bits based on crush level for two adjacent depths
    static const uint8_t mant_masks[4] = { 0x0F, 0x0E, 0x0C, 0x08 };
    uint8_t mask_a = mant_masks[level < 3 ? level : 3];
    uint8_t mask_b = mant_masks[level < 2 ? level + 1 : 3];

    uint8_t idx_a = (idx_full & 0xF0) | (mantissa & mask_a);
    uint8_t idx_b = (idx_full & 0xF0) | (mantissa & mask_b);

    int16_t sa = mulaw_decode_table[idx_a];
    int16_t sb = mulaw_decode_table[idx_b];

    return lerp_q15(sa, sb, fade);
}

// Second-order bandpass: Chamberlin SVF in bandpass mode.
// States promoted to int32_t for Q stability at high resonance.
// Uses the "corrected" Chamberlin update with a single integration per step.
struct CodecDemolisherBlock {
    // Telecom SVF states
    int32_t codec_v1L = 0, codec_v2L = 0;
    int32_t codec_v1R = 0, codec_v2R = 0;

    // Broken Transmission history buffers (1024 samples per channel)
    int16_t trans_historyL[1024];
    int16_t trans_historyR[1024];
    uint16_t trans_wr = 0;
    uint16_t trans_drop_rd = 0;
    uint16_t trans_frame_ctr = 0;
    uint16_t trans_frame_size = 320;
    bool trans_dropped = false;
    uint16_t trans_loop_size = 128;
    uint16_t trans_loop_ctr = 0;

    // Watery MP3 lossy subband simulation delay lines
    int16_t mp3_delayL[256];
    int16_t mp3_delayR[256];
    uint8_t mp3_wr = 0;

    // Dedicated Tape Pitch Wow & Flutter delay lines (256 samples)
    int16_t tape_delayL[256];
    int16_t tape_delayR[256];
    uint8_t tape_wr = 0;
    uint16_t mp3_phase = 0;
    bool     link_state_bad = false;

    // Sample-hold (decimator) state
    int16_t  decL = 0, decR = 0;
    uint32_t dec_ctr = 0;
    uint32_t current_dec_step = 1;

    // Compressor envelope state
    int32_t env = 0;

    // LPF states to filter out harsh high frequency aliasing
    int32_t lp_newL = 0, lp_newR = 0;
    int32_t dec_lpL = 0, dec_lpR = 0;
    uint32_t vibe_lfo = 0;
    uint32_t carrier_phase = 0;
    uint16_t sputter_timer = 0;
    bool sputter_active = false;
    int32_t pop_decayL = 0;
    int32_t pop_decayR = 0;
    int32_t pop_decay_rate = 28000;
    int32_t vinyl_lpL = 0;
    int32_t vinyl_lpR = 0;

    // Upgraded engines states
    uint16_t flutter_phase = 0;
    uint32_t vinyl_timer = 0;
    int16_t trans_rep_ctr = 0;
    int16_t trans_max_reps = 0;
    uint32_t shred_state = 0;

    // Split-band low-end preservation state (< 120Hz)
    int32_t sub_hpL = 0, sub_hpR = 0;
    // Data howl feedback state
    int16_t howl_fbL = 0, howl_fbR = 0;
    // Sample rate decimation clock slew phase
    uint32_t dec_slew_phase = 0;

    void init() {
        codec_v1L = codec_v2L = codec_v1R = codec_v2R = 0;
        for (int i = 0; i < 1024; i++) {
            trans_historyL[i] = trans_historyR[i] = 0;
        }
        for (int i = 0; i < 256; i++) {
            mp3_delayL[i] = mp3_delayR[i] = 0;
        }
        for (int i = 0; i < 128; i++) {
            tape_delayL[i] = tape_delayR[i] = 0;
        }
        tape_wr = 0;
        flutter_phase = 0;
        vinyl_timer = 0;
        trans_rep_ctr = 0;
        trans_max_reps = 0;
        shred_state = 0;

        sub_hpL = sub_hpR = 0;
        howl_fbL = howl_fbR = 0;
        dec_slew_phase = 0;
        pop_decayL = pop_decayR = 0;
        vinyl_lpL = vinyl_lpR = 0;

        trans_wr = 0;
        mp3_wr = 0;
        mp3_phase = 0;
        link_state_bad = false;
        trans_drop_rd = 0;
        trans_frame_ctr = 0;
        trans_frame_size = 320;
        trans_dropped = false;
        trans_loop_size = 128;
        trans_loop_ctr = 0;
        
        decL = decR = 0;
        dec_ctr = 0;
        current_dec_step = 1;
        env = 0;
        lp_newL = lp_newR = 0;
        dec_lpL = dec_lpR = 0;
        vibe_lfo = 0;
        carrier_phase = 0;
        sputter_timer = 0;
        sputter_active = false;
        pop_decayL = pop_decayR = 0;
        pop_decay_rate = 28000;
    }

    // Helper for polynomial tape saturation
    inline int16_t tape_saturate(int16_t in) {
        int32_t x = in;
        int32_t x3 = (((x * x) >> 15) * x) >> 15;
        int32_t y = x - ((x3 * 5461) >> 15);
        return saturate_q15(y);
    }



    __attribute__((always_inline)) inline void process(int16_t inL, int16_t &outL, int16_t inR, int16_t &outR,
                 int32_t strength,
                 int32_t mp3_ring_level, int32_t fuzz_level, int32_t decimate_level,
                 int32_t pop_prob, int32_t click_depth, int32_t bad_conn_level, int32_t scramble_level,
                 int32_t sputter_prob, int32_t tape_sat, int32_t tape_hiss, int32_t tape_wow_flutter, int32_t active_loss,
                 uint32_t &rand_seed, bool pulse1_live = false, bool p1_rising = false)
    {
        if (strength < 800) {
            outL = inL;
            outR = inR;
            decL = inL;
            decR = inR;
            dec_ctr = 0;
            env = 0;
            vibe_lfo = 0;
            trans_wr = 0;
            trans_dropped = false;
            trans_frame_ctr = 0;
            sputter_timer = 0;
            sputter_active = false;
            codec_v1L = codec_v2L = codec_v1R = codec_v2R = 0;
            return;
        }

        // ── 1. Temporal Breathing / Vibe LFO (breathes at ~1.4 Hz) ─────────────
        vibe_lfo += 4; // doubled for 24kHz
        int16_t vibe_sine = lookup_sine(vibe_lfo); // [-32768, 32767]

        int16_t sigL = inL;
        int16_t sigR = inR;

        // Scale gating by absolute input amplitude to keep silence clean
        int32_t input_amp = (sigL < 0 ? -sigL : sigL) + (sigR < 0 ? -sigR : sigR);

        // ── Stage 0: Analog Tape Saturation (from X Knob) & Pitch Wow/Flutter (from Y Knob) ──
        tape_delayL[tape_wr] = sigL;
        tape_delayR[tape_wr] = sigR;

        if (tape_wow_flutter > 0) {
            // Tape Pitch Wow (~0.4 Hz slow pitch drift) + Flutter (~6.8 Hz micro-wobble)
            // Wow depth (1..6 samples), Flutter depth (1..2 samples)
            int32_t wow = (vibe_sine * (1 + ((tape_wow_flutter * 5) >> 15))) >> 15;
            int32_t flutter = (lookup_sine(vibe_lfo * 14) * 2) >> 15;
            uint8_t delay_off = (uint8_t)(16 + wow + flutter);

            uint8_t rd_idx = (tape_wr - delay_off) & 0xFF;
            int16_t wowL = tape_delayL[rd_idx];
            int16_t wowR = tape_delayR[rd_idx];

            sigL = lerp_q15(sigL, wowL, tape_wow_flutter);
            sigR = lerp_q15(sigR, wowR, tape_wow_flutter);
        }
        tape_wr++;

        // Warm Tape Soft-Clipping Drive Saturation (scaled by X Knob tape_sat)
        if (tape_sat > 0) {
            int32_t satL = tape_saturate(((int32_t)sigL * (32768 + tape_sat)) >> 15);
            int32_t satR = tape_saturate(((int32_t)sigR * (32768 + tape_sat)) >> 15);
            sigL = lerp_q15(sigL, satL, tape_sat);
            sigR = lerp_q15(sigR, satR, tape_sat);
        }
        if (tape_hiss > 0 && input_amp > 800) {
            int32_t noiseL = (((int32_t)(fast_rand(rand_seed) & 0x1FF)) - 256) * tape_hiss >> 10;
            int32_t noiseR = (((int32_t)(fast_rand(rand_seed) & 0x1FF)) - 256) * tape_hiss >> 10;
            sigL = saturate_q15(sigL + noiseL);
            sigR = saturate_q15(sigR + noiseR);
        }

        // ── Stage 0.2: Digital Clock Slip & Vinyl Crackle Pops (Zone 1/2 of Y) ──
        if (pop_prob > 0) {
            uint32_t roll = fast_rand(rand_seed) & 0x7FFF;
            if ((int32_t)roll < pop_prob) {
                int32_t pop_sign = (fast_rand(rand_seed) & 1) ? 1 : -1;
                int32_t pop_mag = (click_depth > 1500 ? click_depth : 3500) + (int32_t)(fast_rand(rand_seed) & 0x7FF);
                int32_t pop_val = pop_sign * pop_mag;
                sigL = saturate_q15(sigL + pop_val);
                sigR = saturate_q15(sigR + pop_val);
                vis_codec_glitch.store(true, std::memory_order_relaxed);
            }
        }

        // ── Stage 0.5: Digital Hash Noise (Zone 3 of Y) ──
        if (scramble_level > 0 && input_amp > 800) {
            int32_t noise_amp = (scramble_level * 20) >> 15; // subtle pre-bitcrush digital noise
            int32_t hashL = (((int32_t)(fast_rand(rand_seed) & 0x1FF)) - 256) * noise_amp >> 10;
            int32_t hashR = (((int32_t)(fast_rand(rand_seed) & 0x1FF)) - 256) * noise_amp >> 10;
            sigL = saturate_q15(sigL + hashL);
            sigR = saturate_q15(sigR + hashR);
        }

        // ── Stage 1: Warm Fuzz (Bitcrushing) ──
        if (fuzz_level > 0) {
            int16_t compL = compress_expand_mulaw_variable(sigL, fuzz_level);
            int16_t compR = compress_expand_mulaw_variable(sigR, fuzz_level);

            // Continuous fractional bitcrusher for fuzz
            int32_t fuzz_sq = ((int32_t)fuzz_level * fuzz_level) >> 15;
            int32_t shift_q15 = (fuzz_sq * 10);
            int32_t int_shift = shift_q15 >> 15;
            int32_t frac_shift = shift_q15 & 0x7FFF;
            if (int_shift > 0 || frac_shift > 0) {
                // Left channel symmetric bitcrushing
                int32_t signL = compL < 0 ? -1 : 1;
                int32_t absL = compL < 0 ? -compL : compL;
                int32_t q1L = (absL >> int_shift) << int_shift;
                int32_t q2L = (absL >> (int_shift + 1)) << (int_shift + 1);
                compL = signL * lerp_q15(q1L, q2L, frac_shift);

                // Right channel symmetric bitcrushing
                int32_t signR = compR < 0 ? -1 : 1;
                int32_t absR = compR < 0 ? -compR : compR;
                int32_t q1R = (absR >> int_shift) << int_shift;
                int32_t q2R = (absR >> (int_shift + 1)) << (int_shift + 1);
                compR = signR * lerp_q15(q1R, q2R, frac_shift);
            }

            // Warm fuzz saturation
            int32_t satL = tape_saturate(((int32_t)compL * (32768 + fuzz_level)) >> 15);
            int32_t satR = tape_saturate(((int32_t)compR * (32768 + fuzz_level)) >> 15);
            
            // Smoothly crossfade clean to fuzz based on fuzz_level
            sigL = lerp_q15(sigL, satL, fuzz_level);
            sigR = lerp_q15(sigR, satR, fuzz_level);
        }

        // ── Telecom Bandpass Filter (Always active for stability and Zone 3 AM Radio) ──
        int32_t rg_c = 2200; 
        int32_t rr_c = 28000 - ((fuzz_level * 22000) >> 15);

        int32_t hp_cL = (int32_t)sigL - ((rr_c * codec_v1L) >> 15) - codec_v2L;
        codec_v1L += (rg_c * hp_cL) >> 15;
        codec_v1L = saturate_q15(codec_v1L);
        int32_t bp_cL = codec_v1L;
        codec_v2L = codec_v2L + ((rg_c * codec_v1L) >> 15);
        codec_v2L = saturate_q15(codec_v2L);

        int32_t hp_cR = (int32_t)sigR - ((rr_c * codec_v1R) >> 15) - codec_v2R;
        codec_v1R += (rg_c * hp_cR) >> 15;
        codec_v1R = saturate_q15(codec_v1R);
        int32_t bp_cR = codec_v1R;
        codec_v2R = codec_v2R + ((rg_c * codec_v1R) >> 15);
        codec_v2R = saturate_q15(codec_v2R);

        // Scale the bandpass filter mix continuously with fuzz_level (up to 12000)
        int32_t bp_mix_amount = (fuzz_level * 12000) >> 15;
        if (bp_mix_amount > 0) {
            sigL = lerp_q15(sigL, saturate_q15(bp_cL), bp_mix_amount);
            sigR = lerp_q15(sigR, saturate_q15(bp_cR), bp_mix_amount);
        }

        // ── Stage 1.5: Watery Lossy MP3 Subband Ringing ──
        if (mp3_ring_level > 0) {
            mp3_phase += 150 + (mp3_ring_level >> 4);
            int16_t lfo_val = lookup_sine(mp3_phase); 
            int32_t delay_samples = 25 + (((int32_t)lfo_val * (15 + (mp3_ring_level >> 11))) >> 15);
            int32_t idx = (mp3_wr - delay_samples) & 0xFF;
            int16_t delay_sampleL = mp3_delayL[idx];
            int16_t delay_sampleR = mp3_delayR[idx];
            int32_t fbL = (int32_t)sigL + (((int32_t)delay_sampleL * 16384) >> 15);
            int32_t fbR = (int32_t)sigR + (((int32_t)delay_sampleR * 16384) >> 15);
            mp3_delayL[mp3_wr] = saturate_q15(fbL);
            mp3_delayR[mp3_wr] = saturate_q15(fbR);
            mp3_wr = (mp3_wr + 1) & 0xFF;
            int16_t ringL = saturate_q15(sigL + (((int32_t)delay_sampleL * 8000) >> 15));
            int16_t ringR = saturate_q15(sigR + (((int32_t)delay_sampleR * 8000) >> 15));
            sigL = lerp_q15(sigL, ringL, mp3_ring_level);
            sigR = lerp_q15(sigR, ringR, mp3_ring_level);
        }

        // ── Stage 2: Bad Connection (Packet Drops & Stutter Repetition) ──────
        if (bad_conn_level > 0 || scramble_level > 0) {
            if (!trans_dropped) {
                trans_historyL[trans_wr] = sigL;
                trans_historyR[trans_wr] = sigR;
            }

            bool frame_eval = (trans_frame_ctr >= trans_frame_size);
            if (pulse1_live && p1_rising) {
                frame_eval = true;
            }
            if (frame_eval) {
                trans_frame_ctr = 0;
                trans_frame_size = 240 + (((fast_rand(rand_seed) & 0xFFFF) * 720) >> 16);

                int32_t p_good_to_bad = (active_loss * 400) >> 15;
                int32_t p_bad_to_good = 4000 - ((active_loss * 2500) >> 15);

                uint32_t state_roll = fast_rand(rand_seed) & 0x7FFF;
                if (link_state_bad) {
                    if ((int32_t)state_roll < p_bad_to_good) {
                        link_state_bad = false;
                    }
                } else {
                    if ((int32_t)state_roll < p_good_to_bad) {
                        link_state_bad = true;
                    }
                }

                int32_t drop_thresh = 0;
                if (link_state_bad) {
                    drop_thresh = (active_loss * 12000) >> 15;
                    if (scramble_level > 0) {
                        drop_thresh += (scramble_level * 4000) >> 15;
                    }
                }

                uint32_t roll = fast_rand(rand_seed) & 0x7FFF;
                trans_dropped = ((int32_t)roll < drop_thresh);

                if (trans_dropped) {
                    vis_codec_glitch.store(true, std::memory_order_relaxed);
                    trans_loop_size = 64 + ((active_loss * 512) >> 15);
                    if (scramble_level > 0) {
                        trans_loop_size += (scramble_level * 128) >> 15;
                    }
                    trans_drop_rd = (trans_wr - trans_loop_size) & 0x3FF;
                }
            }
            trans_frame_ctr++;

            if (trans_dropped) {
                sigL = trans_historyL[trans_drop_rd];
                sigR = trans_historyR[trans_drop_rd];
                
                trans_loop_ctr++;
                if (trans_loop_ctr >= trans_loop_size) {
                    trans_loop_ctr = 0;
                    trans_drop_rd = (trans_wr - trans_loop_size) & 0x3FF;
                } else {
                    trans_drop_rd = (trans_drop_rd + 1) & 0x3FF;
                }
            } else {
                trans_wr = (trans_wr + 1) & 0x3FF;
                trans_loop_ctr = 0;
            }

            if (scramble_level > 0) {
                // XOR bit corruption for extra destruction
                uint8_t byteL = encode_mulaw(sigL);
                uint8_t byteR = encode_mulaw(sigR);
                uint8_t mask = (scramble_level >> 11) & 0x1F;
                byteL ^= mask;
                byteR ^= mask;
                sigL = decode_mulaw(byteL);
                sigR = decode_mulaw(byteR);
            }
        }

        // ── Stage 3: VCA Compression & Analog Saturation (scaled by Strength) ───────
        int32_t absL = sigL < 0 ? -sigL : sigL;
        int32_t absR = sigR < 0 ? -sigR : sigR;
        int32_t peak = absL > absR ? absL : absR;
        if (peak > 32767) peak = 32767;

        // Envelope follower (Vibe LFO creates breathing release fluctuations)
        int32_t attack_shift = 4; // sped up for 24kHz
        int32_t release_shift = 10 + (vibe_sine >> 13); // sped up for 24kHz
        if (peak > env) env += (peak - env) >> attack_shift;
        else env += (peak - env) >> release_shift;

        // Threshold matched to 6dB input headroom scaling
        int32_t thresh = 14000 - ((strength * 12500) >> 15);
        int32_t slope = (strength * 27000) >> 15;

        int32_t gain_coef = 32768;
        if (env > thresh) {
            int32_t overshoot = env - thresh;
            int32_t gain_reduction = ((int32_t)overshoot * slope) >> 15;
            gain_coef = 32768 - gain_reduction;
            if (gain_coef < 4096) gain_coef = 4096; // limit GR to -18dB
        }

        int32_t drive_gain = 32768 + ((strength * 11468) >> 15);
        int32_t compLi = soft_limit_q15(((int32_t)sigL * drive_gain) >> 15);
        int32_t compRi = soft_limit_q15(((int32_t)sigR * drive_gain) >> 15);
        compLi = (compLi * gain_coef) >> 15;
        compRi = (compRi * gain_coef) >> 15;

        // Compensated makeup gain to keep overall wet path loudness constant
        int32_t makeup_gain = 32768 - ((strength * 6000) >> 15);
        compLi = (compLi * makeup_gain) >> 15;
        compRi = (compRi * makeup_gain) >> 15;

        int16_t wetL = tape_saturate(saturate_q15(compLi));
        int16_t wetR = tape_saturate(saturate_q15(compRi));

        // ── Stage 4: Downsampling (from X Knob) ─────────────────────────────────────
        if (decimate_level > 800) {
            int32_t xor_mask = ((decimate_level - 800) * 127) / (32767 - 800);
            if (xor_mask > 0) {
                uint8_t byteL = encode_mulaw(wetL);
                uint8_t byteR = encode_mulaw(wetR);
                byteL ^= xor_mask;
                byteR ^= xor_mask;
                wetL = decode_mulaw(byteL);
                wetR = decode_mulaw(byteR);
            }

            int16_t pre_decL = wetL;
            int16_t pre_decR = wetR;

            if (dec_ctr >= current_dec_step) {
                decL    = wetL;
                decR    = wetR;
                dec_ctr = 0;

                uint32_t ds_sq = ((uint32_t)decimate_level * decimate_level) >> 15;
                uint32_t base_step = 1 + ((ds_sq * 23) >> 15);
                uint32_t jitter_range = ((strength >> 11) * (decimate_level >> 11)) >> 4;
                if (jitter_range < 2) jitter_range = 2;
                uint32_t jitter = (((fast_rand(rand_seed) >> 16) * jitter_range) >> 16);
                current_dec_step = base_step + jitter;
                if (current_dec_step > 32) current_dec_step = 32;
            } else {
                wetL = decL;
                wetR = decR;
            }
            dec_ctr++;

            static const int32_t dec_coef_lut[10] = {
                32767, 32767, 16384, 10922, 8192, 6553, 5461, 4681, 4096, 3640
            };
            int32_t dec_idx = 1 + (current_dec_step >> 2);
            if (dec_idx > 9) dec_idx = 9;
            int32_t dec_coef = dec_coef_lut[dec_idx];

            dec_lpL += (((int32_t)wetL - dec_lpL) * dec_coef) >> 15;
            dec_lpR += (((int32_t)wetR - dec_lpR) * dec_coef) >> 15;
            dec_lpL = clamp_i32(dec_lpL, -32768, 32767);
            dec_lpR = clamp_i32(dec_lpR, -32768, 32767);
            
            wetL = lerp_q15(pre_decL, (int16_t)dec_lpL, decimate_level);
            wetR = lerp_q15(pre_decR, (int16_t)dec_lpR, decimate_level);
        } else {
            decL    = wetL;
            decR    = wetR;
            dec_ctr = 0;
            current_dec_step = 1;
            dec_lpL = wetL;
            dec_lpR = wetR;
        }

        // ── Stage 5: CD-Skipping Micro-Stutter Repeats (sputter_prob) ─────────
        int16_t out_wetL = wetL;
        int16_t out_wetR = wetR;

        if (sputter_prob > 0) {
            uint32_t roll = fast_rand(rand_seed) & 0x7FFF;

            if (sputter_timer > 0) {
                sputter_timer--;
                if (sputter_active) {
                    uint8_t read_idx = (trans_wr - 16 - (sputter_timer & 63)) & 0xFF;
                    out_wetL = trans_historyL[read_idx];
                    out_wetR = trans_historyR[read_idx];
                } else {
                    out_wetL = (out_wetL * 3) >> 3;
                    out_wetR = (out_wetR * 3) >> 3;
                }
            } else {
                sputter_active = false;
                if ((int32_t)roll < sputter_prob) {
                    sputter_timer = 20 + (((fast_rand(rand_seed) & 0xFFFF) * 350) >> 16);
                    sputter_active = ((fast_rand(rand_seed) & 0x7FFF) < 22000);
                }
            }
        } else {
            sputter_timer = 0;
            sputter_active = false;
        }

        // Final mix: reaches 100% wet at 25% strength (8192) to prevent dry masking
        int32_t mix_coeff = strength * 4;
        if (mix_coeff > 32767) mix_coeff = 32767;

        outL = lerp_q15(inL, out_wetL, (int16_t)mix_coeff);
        outR = lerp_q15(inR, out_wetR, (int16_t)mix_coeff);
    }

    bool isFrameDropped() const { return trans_dropped; }
};

// ============================================================================
// 3.  MULTI-TAP DELAY BLOCK
// ============================================================================
// Stereo tape echo with three rhythmic taps. Delay time is slewed through a
// one-pole IIR for "tape inertia" pitch-glide on time changes.
// Cross-feedback (L→R, R→L) gives rich stereo spatial spread.
// CV1 warps delay time for pitch effects (chorus-style).
// Freeze (switch UP) locks the write pointer — the delay buffer loops forever.
//
// Parameters:
//   mainMix  — Wet / dry blend.
//   time     — Primary tap delay time. [0..32767] → [128..16300] samples.
//   feedback — Cross-feedback amount. High values → infinite wash.
//   freeze   — When true: write pointer frozen; buffer loops without new input.
// ============================================================================
struct MultiTapDelayBlock {
    // Contiguous ring buffer — 40960 samples total.
    // Stereo: L uses [0..20479], R uses [20480..40959]  (853 ms each @ 24 kHz)
    // Mono:   full [0..40959] used as one delay line    (1706 ms @ 24 kHz)
    int16_t  buf[40960];
    uint16_t wr = 0;   // wraps at BUF_HALF (20480) in stereo, BUF_FULL (40960) in mono

    // IIR-smoothed delay time (prevents zipper on rapid changes)
    int32_t  smooth_t = 8000;

    // Crossfade state for click-free large time jumps (sync mode division changes)
    int32_t  xfade_t    = 0;   // read position of the "old" head being faded out
    int32_t  xfade_gain = 0;   // 128 → 0: old weight / (128 − xfade_gain) = new weight

    // Wow & flutter LFO phase accumulator and smoothed depth
    uint16_t flutter_phase = 0;
    int32_t  smooth_flutter_depth = 0;

    // Feedback 1-pole LPF state variables
    int32_t  lp_feedback_L = 0;
    int32_t  lp_feedback_R = 0;

    // PT2399 clock decimation state variables (only used when time > 16384)
    uint32_t clk_phase = 0;
    int16_t  last_outL = 0;
    int16_t  last_outR = 0;
    int32_t  lp_outL = 0;
    int32_t  lp_outR = 0;
    int32_t  lp_out2L = 0;
    int32_t  lp_out2R = 0;

    DCBlocker dcL;
    DCBlocker dcR;

    static constexpr int32_t BUF_HALF = 20480;
    static constexpr int32_t BUF_FULL = 40960;

    void init() {
        memset(buf, 0, sizeof(buf));
        wr                   = 0;
        smooth_t             = 8000;
        xfade_t              = 0;
        xfade_gain           = 0;
        flutter_phase        = 0;
        smooth_flutter_depth = 0;
        lp_feedback_L        = 0;
        lp_feedback_R        = 0;
        clk_phase            = 0;
        last_outL            = 0;
        last_outR            = 0;
        lp_outL              = 0;
        lp_outR              = 0;
        lp_out2L             = 0;
        lp_out2R             = 0;
        dcL.init();
        dcR.init();
    }

    __attribute__((always_inline)) inline void process(int16_t inL, int16_t &outL, int16_t inR, int16_t &outR,
                 int32_t mainMix, int32_t time, int32_t feedback,
                 bool freeze, int32_t cv1Warp, int32_t cv2Corruption, int32_t globalNoiseScale = 16384,
                 bool pulse1_live = false, uint32_t clk_period_samples = 0,
                 bool mono_mode = false, int32_t stereo_width = 16384)
    {
        const int32_t buf_len = mono_mode ? BUF_FULL : BUF_HALF; // 40960 or 20480
        const int32_t max_t   = mono_mode ? 96000 : 48000;       // 4.0s mono, 2.0s stereo

        // ── Compute target delay time (quadratic taper expands short times across knob) ──
        int32_t time_sq  = (time * time) >> 15;
        int32_t target_t = 24 + (int32_t)(((int64_t)time_sq * (max_t - 24)) >> 15);
        if (pulse1_live && clk_period_samples > 240) {
            if (time < 3000) {
                // Karplus-Strong fast pitch range (24..512 samples)
                target_t = 24 + (time * 488) / 3000;
            } else {
                // 16 rhythmic subdivisions (in 16th note units):
                // 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1 beat, 1.5b, 2b, 3b, 4b (1 bar), 6b, 8b (2 bars), 12b (3 bars), 16b (4 bars)
                static const int32_t div_num[16] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256};
                
                // Dynamically count valid subdivisions that fit inside max_t (buffer limit)
                int32_t valid_steps = 16;
                while (valid_steps > 1 && (((int64_t)clk_period_samples * div_num[valid_steps - 1]) >> 4) > max_t) {
                    valid_steps--;
                }

                // Map remaining knob range (3000..32767) evenly across valid_steps
                int32_t rel_time = time - 3000;
                int32_t step = (rel_time * valid_steps) / 29768;
                if (step < 0) step = 0;
                if (step >= valid_steps) step = valid_steps - 1;

                target_t = (int32_t)(((int64_t)clk_period_samples * div_num[step]) >> 4);
            }
        }
        target_t = target_t + (cv1Warp * 4);
        target_t = clamp_i32(target_t, 24, max_t);

        // ── Smooth IIR time tracking: continuous tape pitch-bending without zipper noise ──
        smooth_t += (target_t - smooth_t) >> 5;

        // ── PT2399 Underclocking Zone for Extended Delays (1.7s → 4.0s in mono, 0.85s → 2.0s in stereo) ──
        int32_t max_buf_delay = buf_len - 128;
        uint32_t clk_inc_q16 = 65536;
        int32_t effective_t  = smooth_t;
        int32_t filter_coef  = 32767;

        if (smooth_t > max_buf_delay) {
            // Underclock write rate smoothly from 24kHz down to ~10kHz
            clk_inc_q16 = (uint32_t)(((int64_t)max_buf_delay << 16) / smooth_t);
            if (clk_inc_q16 < 20000) clk_inc_q16 = 20000;
            effective_t = max_buf_delay;

            // PT2399 anti-aliasing reconstruction filter cutoff lowers with underclocking
            filter_coef = (int32_t)((clk_inc_q16 * 32767) >> 16);
            if (filter_coef < 12000) filter_coef = 12000;
        }

        if (mainMix < 50) {
            outL      = inL;  outR      = inR;
            last_outL = inL;  last_outR = inR;
            lp_outL   = inL;  lp_outR   = inR;
            if (!freeze) {
                clk_phase += clk_inc_q16;
                while (clk_phase >= 65536) {
                    clk_phase -= 65536;
                    if (mono_mode) {
                        int16_t in_mono = (int16_t)(((int32_t)inL + inR) >> 1);
                        buf[wr] = in_mono;
                    } else {
                        buf[wr]            = inL;
                        buf[BUF_HALF + wr] = inR;
                    }
                    wr++; if (wr >= (uint32_t)buf_len) wr = 0;
                }
            }
            return;
        }

        // ── Wow & Flutter LFO (~2.2 Hz) ──
        flutter_phase += 6;
        int16_t lfo = lookup_sine_fast(flutter_phase);
        int32_t cv2_abs = cv2Corruption < 0 ? -cv2Corruption : cv2Corruption;
        int32_t cv2_warp = cv2_abs * 12;
        int32_t target_flutter_depth = ((feedback * 40) >> 15) + ((cv2_warp * 20) >> 15);
        smooth_flutter_depth += ((target_flutter_depth - smooth_flutter_depth) * 64) >> 10;

        int32_t flutter_scale;
        if (effective_t < 300) {
            flutter_scale = 0;
        } else if (effective_t < 1000) {
            flutter_scale = ((effective_t - 300) * 32767) / 700;
        } else {
            flutter_scale = 32767;
        }
        int32_t flutter = ((lfo * smooth_flutter_depth) >> 15) * flutter_scale >> 15;

        // ── Fractional-sample read with linear interpolation ──
        auto read_stereo = [&](int32_t delay_q16, int16_t &rL, int16_t &rR) {
            int32_t delay_samples = delay_q16 >> 16;
            int32_t rp_i = (int32_t)wr - delay_samples;
            
            // Modulo wrapping for negative index
            rp_i %= buf_len;
            if (rp_i < 0) rp_i += buf_len;

            int32_t rp_n = rp_i + 1;
            if (rp_n >= buf_len) rp_n = 0;

            uint16_t frac = (uint16_t)(delay_q16 & 0xFFFF);
            if (mono_mode) {
                int16_t y0 = buf[rp_i], y1 = buf[rp_n];
                int16_t val = lerp_delay_q15(y0, y1, frac);
                rL = val; rR = val;
            } else {
                int16_t y0 = buf[rp_i],            y1 = buf[rp_n];
                rL = lerp_delay_q15(y0, y1, frac);
                int16_t y0r = buf[BUF_HALF + rp_i], y1r = buf[BUF_HALF + rp_n];
                rR = lerp_delay_q15(y0r, y1r, frac);
            }
        };

        // ── Stereo Width & Ping-Pong Cross-Over ──
        int32_t cross_q15 = stereo_width;
        if (cross_q15 > 32767) cross_q15 = 32767;
        if (cross_q15 < 0)     cross_q15 = 0;

        int64_t eff_tot = (int64_t)effective_t + flutter;
        int32_t t1L_q16 = (int32_t)(eff_tot << 16);
        int32_t t1R_q16 = t1L_q16;
        if (!mono_mode && cross_q15 > 16384) {
            int32_t offset = (((cross_q15 - 16384) * effective_t) >> 18); // up to 6% prime delay offset
            t1R_q16 += (offset << 16);
        }

        int32_t t2_q16 = (int32_t)((eff_tot * 3) << 14); // 1.5x
        int32_t t3_q16 = (int32_t)(((eff_tot * 40503) >> 15) << 16); // ~1.236x

        int16_t w1L, w1R, w2L, w2R, w3L, w3R;
        read_stereo(t1L_q16, w1L, w1R);
        if (t1R_q16 != t1L_q16) {
            int16_t dummyL, rR_offset;
            read_stereo(t1R_q16, dummyL, rR_offset);
            w1R = rR_offset;
        }
        read_stereo(t2_q16, w2L, w2R);
        read_stereo(t3_q16, w3L, w3R);

        // ── Gain mix: single-tap below 512 samples (KS / resonator mode) ──
        int32_t gain1, gain2L, gain2R, gain3L, gain3R;
        if (effective_t < 512) {
            gain1  = 32768;
            gain2L = gain2R = gain3L = gain3R = 0;
        } else {
            int32_t tap_fade = 32767;
            if (effective_t > (buf_len * 2) / 3) {
                int32_t rem = buf_len - effective_t;
                if (rem < 0) rem = 0;
                tap_fade = (rem * 32767) / (buf_len / 3);
                if (tap_fade < 0) tap_fade = 0;
            }
            gain1  = 32768 - ((feedback * tap_fade) >> 16);
            gain2L = (int32_t)(((int64_t)feedback * 12000 * tap_fade) >> 30);
            gain2R = (int32_t)(((int64_t)feedback *  4384 * tap_fade) >> 30);
            gain3L = (int32_t)(((int64_t)feedback *  4384 * tap_fade) >> 30);
            gain3R = (int32_t)(((int64_t)feedback * 12000 * tap_fade) >> 30);
        }

        int16_t raw_mixL = saturate_q15(
            ((int32_t)w1L * gain1 + (int32_t)w2L * gain2L + (int32_t)w3L * gain3L) >> 15);
        int16_t raw_mixR = saturate_q15(
            ((int32_t)w1R * gain1 + (int32_t)w2R * gain2R + (int32_t)w3R * gain3R) >> 15);

        if ((raw_mixL < -1500 || raw_mixL > 1500) || (raw_mixR < -1500 || raw_mixR > 1500)) {
            vis_delay_pulse.store(true, std::memory_order_relaxed);
        }

        // Spatial stereo output spread
        int16_t mixL = raw_mixL;
        int16_t mixR = raw_mixR;
        if (!mono_mode && cross_q15 > 0) {
            mixL = (int16_t)(((int32_t)raw_mixL * (32767 - (cross_q15 >> 1)) + (int32_t)raw_mixR * (cross_q15 >> 1)) >> 15);
            mixR = (int16_t)(((int32_t)raw_mixR * (32767 - (cross_q15 >> 1)) + (int32_t)raw_mixL * (cross_q15 >> 1)) >> 15);
        }

        // ── Write to buffer (feedback path with 100% Ping-Pong Cross-Over) ──
        if (!freeze) {
            int32_t fL = w1L, fR = w1R;
            if (!mono_mode && cross_q15 > 0) {
                // Ping-pong cross-feed: Left & Right exchange channels on every repeat
                fL = (int16_t)(((int32_t)w1L * (32767 - cross_q15) + (int32_t)w1R * cross_q15) >> 15);
                fR = (int16_t)(((int32_t)w1R * (32767 - cross_q15) + (int32_t)w1L * cross_q15) >> 15);
            }
            int32_t feedL = (int32_t)inL + (((int32_t)fL * feedback) >> 15);
            int32_t feedR = (int32_t)inR + (((int32_t)fR * feedback) >> 15);
            feedL = soft_limit_q15(feedL);
            feedR = soft_limit_q15(feedR);

            int32_t damp_coef = 28000;
            if (effective_t >= 500) {
                damp_coef = 15000 + (((effective_t - 500) * 15000) >> 14);
                if (damp_coef > 30000) damp_coef = 30000;
            }
            if (feedback >= 31000) {
                // Infinite 100% feedback sustain: bypass damping so high frequencies don't decay into silence
                damp_coef = 32767;
            }
            lp_feedback_L += ((feedL - lp_feedback_L) * damp_coef) >> 15;
            lp_feedback_R += ((feedR - lp_feedback_R) * damp_coef) >> 15;
            lp_feedback_L = soft_limit_q15(lp_feedback_L);
            lp_feedback_R = soft_limit_q15(lp_feedback_R);

            int16_t wrL = dcL.process((int16_t)lp_feedback_L, 32751);
            int16_t wrR = dcR.process((int16_t)lp_feedback_R, 32751);

            int32_t cv2_abs2 = cv2Corruption < 0 ? -cv2Corruption : cv2Corruption;
            int32_t corr = (cv2_abs2 * 8 * globalNoiseScale) >> 14;
            if (corr > 200) {
                uint16_t mask = (uint16_t)(corr >> 3);
                wrL ^= mask;
                wrR ^= mask;
            }

            clk_phase += clk_inc_q16;
            while (clk_phase >= 65536) {
                clk_phase -= 65536;
                if (mono_mode) {
                    buf[wr] = (int16_t)(((int32_t)wrL + wrR) >> 1);
                } else {
                    buf[wr]            = wrL;
                    buf[BUF_HALF + wr] = wrR;
                }
                wr++; if (wr >= (uint32_t)buf_len) wr = 0;
            }
        }

        last_outL = mixL;
        last_outR = mixR;

        // 2-pole cascaded anti-aliasing reconstruction LPF (100% zipper-free clock filtering)
        lp_outL  += (((int32_t)last_outL - lp_outL) * filter_coef) >> 15;
        lp_out2L += ((lp_outL - lp_out2L) * filter_coef) >> 15;

        lp_outR  += (((int32_t)last_outR - lp_outR) * filter_coef) >> 15;
        lp_out2R += ((lp_outR - lp_out2R) * filter_coef) >> 15;

        outL = split_mix_q15(inL, (int16_t)lp_out2L, (int16_t)mainMix);
        outR = split_mix_q15(inR, (int16_t)lp_out2R, (int16_t)mainMix);
    }
};
// 4.  CIRCUIT-BENT GLITCHER BLOCK
// ============================================================================
// Phrase-loop stutter sampler. When inactive, it continuously writes incoming
// audio to a 16384-sample (341 ms) circular buffer.
//
// Triggering (either via Pulse 1 trigger or random roll exceeding mainProb)
// locks the write pointer, and starts playing a loop of 'size' samples.
//
// Parameters:
//   mainMix     — Wet / dry blend.
//   size        — Loop length: [0..32767] → [32..16384] samples.
//   speedDir    — Playback speed + direction:
//                   [0, 15000]   → reverse, −2.0× to −0.05×
//                   [15001,17000] → freeze (0× speed deadzone)
//                   [17001,32767] → forward, +0.05× to +2.0×
//   gateTrigger — Pulse 1 jack: stutter gate (momentary).
//   switchFreeze — Switch UP: latched freeze.
// ============================================================================


// Zoned speed determination helper. Placed in FLASH (not RAM) to save memory,
// since it is only called on grain boundaries/initialization, not per-sample.
inline int32_t determine_speed_zoned(int32_t sq, int32_t cv2_corr, uint32_t &seed, uint8_t arp_step, int32_t loop_len) {
    int32_t base_speed;

    // If the loop length is short, keep it clean (stuck CD/tape scrub style) rather than chaotic metallic buzzes
    bool force_clean = (loop_len < 768);

    if (force_clean) {
        // At short loop lengths, limit to 1x forward or 1x reverse
        if (sq < 6554) {
            base_speed = 65536; // 1x fwd
        } else {
            base_speed = (fast_rand(seed) & 1) ? 65536 : -65536; // 1x fwd or 1x rev
        }
    } else {
        if (sq < 6554) {
            // Zone 0: always 1x forward — pure rhythmic stutter, no pitch change
            base_speed = 65536;
        } else if (sq < 13107) {
            // Zone 1: always 1x forward or 1x reverse (direction changes, no pitch shifts)
            base_speed = (fast_rand(seed) & 1) ? 65536 : -65536;
        } else if (sq < 19661) {
            // Zone 2: tonal octave family {0.5x, 1x, 2x, -0.5x, -1x}
            uint32_t choice = ((fast_rand(seed) & 0xFFFF) * 5) >> 16;
            switch (choice) {
                case 0:  base_speed =  65536;  break; // 1x fwd
                case 1:  base_speed =  131072; break; // 2x fwd
                case 2:  base_speed =  32768;  break; // 0.5x fwd
                case 3:  base_speed = -65536;  break; // 1x rev
                default: base_speed = -32768;  break; // 0.5x rev
            }
        } else if (sq < 26214) {
            // Zone 3: Subtle detuned tape drift (advances step on every repeat, but stays close to unison)
            static const int32_t arp_seq[8] = {
                65536,         // 1x fwd (unison)
                66100,         // +15 cents
                65000,         // -14 cents
                65536,         // unison
                -65536,        // -1x rev (unison)
                -66100,        // -1x rev + 15 cents
                -65000,        // -1x rev - 14 cents
                -65536         // -1x rev (unison)
            };
            base_speed = arp_seq[arp_step & 7];
        } else {
            // Zone 4: full chaos — all 6 speeds equally weighted
            uint32_t c = ((fast_rand(seed) & 0xFFFF) * 6) >> 16; // 0..5
            switch (c) {
                case 0:  base_speed =  65536;  break; // 1x fwd
                case 1:  base_speed =  131072; break; // 2x fwd
                case 2:  base_speed =  32768;  break; // 0.5x fwd
                case 3:  base_speed = -65536;  break; // 1x rev
                case 4:  base_speed = -131072; break; // 2x rev
                default: base_speed = -32768;  break; // 0.5x rev
            }
        }
    }

    int32_t speed = base_speed;

    // Add micro pitch jitter under chaos
    int32_t eff_chaos = cv2_corr < 0 ? -cv2_corr : cv2_corr;
    if (eff_chaos > 200) {
        int32_t jitter = ((((int32_t)(fast_rand(seed) & 0x7FFF)) - 16384) * (eff_chaos >> 6)) >> 15; // up to ±5% speed drift
        speed += jitter;
    }

    if (speed == 0) speed = 3277;
    return speed;
}

struct GlitcherBlock {
    // Contiguous mu-law buffer — 65536 bytes total.
    // Stereo: L uses [0..32767], R uses [32768..65535]  (1.365 s each @ 24 kHz)
    // Mono:   full [0..65535] used as one loop buffer    (2.730 s @ 24 kHz)
    uint8_t  buf[65536];
    static constexpr int32_t BUF_HALF = 32768;
    static constexpr int32_t BUF_FULL = 65536;
    uint16_t wr = 0;

    bool     active     = false;
    uint16_t freeze_wr  = 0;       // write pointer at moment of freeze
    uint32_t frozen_clk_period = 0; // clock period captured at moment of freeze

    // Playback pointer (Q16: integer + 16-bit fraction relative to loop_start)
    int64_t  rd_q16     = 0;

    // Crossfade for click-free loop boundaries
    int32_t  xfade_ctr  = 0;       // countdown: xfade_len → 0
    int32_t  xfade_rd   = 0;       // secondary read pointer (Q16) for crossfade
    int32_t  xfade_len  = 256;
    int32_t  xfade_phase = 0;
    int32_t  xfade_step  = 0;

    // Dynamic loop length tracking
    int32_t  current_loop_len = 512;
    int32_t  sample_ctr = 0;

    // Smooth exit crossfade (stutter playback → live dry)
    int32_t  dry_fade_ctr = 0;
    int32_t  dry_fade_rd  = 0;
    int32_t  dry_fade_len = 256;
    int32_t  dry_fade_phase = 0;
    int32_t  dry_fade_step  = 0;

    // Loop onset crossfade (fading from live dry to loop playback)
    int32_t  onset_fade_ctr = 0;
    int32_t  onset_fade_len = 256;
    int32_t  onset_fade_phase = 0;
    int32_t  onset_fade_step  = 0;

    // Latched playback speed (for random timings and reverses probability field)
    int32_t  current_speed_q16 = 65536;
    
    // Active offset of loop_start relative to freeze_wr
    int32_t  active_offset = 0;

    // Clustering and probability warping states
    int32_t  cluster_state = 16384;
    uint16_t cluster_timer = 0;

    // Evolve freeze states
    uint32_t freeze_evolve_ctr = 0;
    bool     evolve_active = false;
    int32_t  evolve_samples_left = 0;

    uint8_t  arpeggio_step = 0;
    bool     trig_out1 = false;
    bool     trig_out2 = false;
    uint8_t  current_g711_sample = 128;
    bool     last_freezeGate = false;
    int32_t  trigger_ctr = 0;
    int32_t  active_duration_ctr = 0;
    int32_t  refill_ctr = 0;
    uint32_t clock_pulse_counter = 0;
    uint32_t auto_chaos_phase = 0;
    int32_t  clk_trig_accum = 0;    // clock-accumulated trigger for musical rhythmic patterns
    int32_t  frozen_beat_count = 0;  // number of full beats captured in frozen buffer
    bool     freeze_releasing = false; // delayed release: wait for next boundary before unfreezing

    void init() {
        memset(buf, 0, sizeof(buf));
        wr         = 0;
        active     = false;
        freeze_wr  = 0;
        last_freezeGate = false;
        rd_q16     = 0;
        auto_chaos_phase = 0;
        xfade_ctr  = 0;
        xfade_rd   = 0;
        xfade_len  = 256;
        xfade_phase = 0;
        xfade_step  = 0;
        current_loop_len = 512;
        sample_ctr = 0;
        active_duration_ctr = 0;
        refill_ctr = 0;
        dry_fade_ctr = 0;
        dry_fade_rd  = 0;
        dry_fade_len = 256;
        dry_fade_phase = 0;
        dry_fade_step  = 0;
        onset_fade_ctr = 0;
        onset_fade_len = 256;
        onset_fade_phase = 0;
        onset_fade_step  = 0;
        current_speed_q16 = 65536;
        active_offset = 0;
        cluster_state = 16384;
        cluster_timer = 0;
        freeze_evolve_ctr = 0;
        evolve_active = false;
        evolve_samples_left = 0;
        arpeggio_step = 0;
        trig_out1 = false;
        trig_out2 = false;
        current_g711_sample = 128;
        frozen_clk_period = 0;
        trigger_ctr = 0;
        clock_pulse_counter = 0;
        clk_trig_accum = 0;
        frozen_beat_count = 0;
        freeze_releasing = false;
    }

    __attribute__((always_inline)) inline void process(int16_t inL, int16_t &outL, int16_t inR, int16_t &outR,
                 int32_t mainProb, int32_t size, int32_t speedQuant, bool isFreezePage,
                 bool glitchInjector, bool freezeGate, int32_t cv1Warp, int32_t cv2Corruption,
                 uint32_t &rand_seed, int32_t scrubOffset, int32_t glitchFeedback, int32_t globalNoiseScale,
                 bool pulse1_live, bool p1_rising, bool p1_gate,
                 bool pulse2_live, bool p2_rising, bool p2_gate,
                 uint32_t clk_period_samples, uint32_t clk_timer,
                 int32_t target_loop_size, int32_t target_offset, int32_t target_speed_q16,
                 bool mono_mode = false, bool dual_mono_mode = false)
    {
        const int32_t buf_mask = mono_mode ? (BUF_FULL - 1) : (BUF_HALF - 1);
        const int32_t buf_size = mono_mode ? BUF_FULL : BUF_HALF;

        // Helper: write one stereo sample into the appropriate buffer slot(s)
        auto write_buf = [&](uint16_t pos, int16_t sL, int16_t sR) {
            if (mono_mode) {
                buf[pos & buf_mask] = encode_mulaw((int16_t)(((int32_t)sL + sR) >> 1));
            } else {
                buf[pos & buf_mask]              = encode_mulaw(sL);
                buf[BUF_HALF + (pos & buf_mask)] = encode_mulaw(sR);
            }
        };

        bool is_clock_sync = pulse1_live && (clk_period_samples > 240);
        bool eff_glitchInjector = glitchInjector;
        if (is_clock_sync) {
            eff_glitchInjector = false;
        }
        // p1_gate excluded: raw clock gate must not force glitcher active (causes clock bleed)
        bool want_active = eff_glitchInjector || freezeGate;
        bool is_loop_frozen = freezeGate || (mainProb >= 32760);
        int32_t speed_q16 = 65536;

        if (mainProb < 50 && !want_active && !active && dry_fade_ctr == 0) {
            outL = inL;
            outR = inR;
            write_buf(wr, inL, inR);
            wr = (wr + 1) & buf_mask;
            return;
        }
        if (is_clock_sync && mainProb < 50 && !active && dry_fade_ctr == 0) {
            outL = inL;
            outR = inR;
            write_buf(wr, inL, inR);
            wr = (wr + 1) & buf_mask;
            return;
        }

        // Update slow-moving cluster state (cutoff ~3Hz at 24kHz)
        cluster_timer++;
        if (cluster_timer >= 256) {
            cluster_timer = 0;
            if (dual_mono_mode) {
                rand_seed ^= 0x55555555u; // Decorrelate 2-channel random seed sequence in Dual Mono mode
            }
            cluster_state += (((int32_t)(fast_rand(rand_seed) & 0x7FFF)) - cluster_state) >> 5;
        }

        // Warp input probability curve cubicly for sparser, more musical triggering at medium knob settings.
        // If clocked, use a linear response so stutters trigger easily and feel responsive.
        int32_t warpedProb = 0;
        if (pulse1_live) {
            warpedProb = mainProb;
        } else {
            int32_t mainProbSq = (mainProb * mainProb) >> 15;
            warpedProb = (mainProbSq * mainProb) >> 15;
        }

        // Modulate probability with cluster state, scaling down depth near 0% and 100% knob
        int32_t mod_depth = (mainProb * (32767 - mainProb)) >> 14;
        int32_t cluster_mod = ((cluster_state - 16384) * mod_depth) >> 15;
        int32_t finalProb = warpedProb + cluster_mod;
        if (finalProb < 0) finalProb = 0;
        if (finalProb > 32767) finalProb = 32767;

        // ── FREEZE MODE ──────────────────────────────────────────────────────
        bool is_freezing = freezeGate || (pulse2_live && p2_gate);
        if (is_freezing) {
            if (!last_freezeGate) {
                active = false;
            }
            last_freezeGate = true;

            // 1. Lock recording and initialize freeze on transition
            if (!active) {
                active = true;
                int32_t target_wr = wr;
                if (pulse1_live && clk_period_samples > 240) {
                    // Snap freeze_wr to the last beat boundary for beat-aligned capture
                    target_wr = wr - (int32_t)clk_timer;
                    frozen_clk_period = clk_period_samples;
                    // Calculate how many full beats of audio are available in the buffer
                    frozen_beat_count = buf_size / (int32_t)clk_period_samples;
                    if (frozen_beat_count < 1) frozen_beat_count = 1;
                } else {
                    frozen_clk_period = 0;
                    frozen_beat_count = 0;
                }
                freeze_wr = target_wr & buf_mask;
                
                current_loop_len = target_loop_size;
                active_offset = target_offset;

                // Determine initial speed and rd_q16 direction
                current_speed_q16 = isFreezePage ? target_speed_q16 : 65536;
                speed_q16 = current_speed_q16;
                rd_q16 = (speed_q16 >= 0) ? 0 : ((int64_t)current_loop_len << 16);

                xfade_ctr = 0;
                dry_fade_ctr = 0;
                sample_ctr = 0;
                
                int32_t cur_xfade = current_loop_len < 2048 ? (current_loop_len >> 1) : 1024;
                if (cur_xfade < 4) cur_xfade = 4;
                onset_fade_len = cur_xfade;
                onset_fade_ctr = cur_xfade;
                onset_fade_step = (32767 << 15) / cur_xfade;
                onset_fade_phase = 0;
            }

            int32_t loop_size = target_loop_size;
            int32_t cur_xfade = loop_size < 512 ? (loop_size >> 1) : 256;
            if (cur_xfade < 4) cur_xfade = 4;

            speed_q16 = isFreezePage ? target_speed_q16 : 65536;

            int32_t loop_start = (((int32_t)freeze_wr - active_offset) & buf_mask) << 16;

            rd_q16 += speed_q16;
            sample_ctr++;

            // Natural boundary check
            bool crossed = false;
            if (speed_q16 >= 0) {
                if (rd_q16 >= ((int64_t)current_loop_len << 16)) {
                    crossed = true;
                }
            } else {
                if (rd_q16 < 0) {
                    crossed = true;
                }
            }

            if (crossed) {
                sample_ctr = 0; // Reset grain sample counter for continuous granular windowing
                clock_pulse_counter = 0;
                trig_out1 = true; // Output loop sync pulse

                // Spawn next grain repeat at updated position/length targets
                // Ensure xfade_rd stays inside the valid loop boundary
                int32_t xfade_offset = current_loop_len > cur_xfade ? (current_loop_len - cur_xfade) : 0;
                xfade_rd = (speed_q16 >= 0) ? (loop_start + ((int64_t)xfade_offset << 16)) : loop_start;
                rd_q16 = (speed_q16 >= 0) ? 0 : ((int64_t)loop_size << 16);

                xfade_len = cur_xfade;
                xfade_ctr = cur_xfade;
                xfade_step = (32767 << 15) / xfade_len;
                xfade_phase = 0;

                current_loop_len = loop_size;
                active_offset = target_offset;
                loop_start = (((int32_t)freeze_wr - active_offset) & buf_mask) << 16;
            }

            int16_t sL = 0, sR = 0;
            auto read_buf = [&](int32_t ptr, int16_t &valL, int16_t &valR) {
                int32_t  idx  = (ptr >> 16) & buf_mask;
                int32_t  nxt  = (idx + 1)   & buf_mask;
                uint16_t frac = (uint16_t)(ptr & 0xFFFF);
                if (mono_mode) {
                    int16_t y0 = decode_mulaw(buf[idx]), y1 = decode_mulaw(buf[nxt]);
                    int16_t val = lerp_delay_q15(y0, y1, frac);
                    valL = val; valR = val;
                } else {
                    int16_t y0L = decode_mulaw(buf[idx]),            y1L = decode_mulaw(buf[nxt]);
                    valL = lerp_delay_q15(y0L, y1L, frac);
                    int16_t y0R = decode_mulaw(buf[BUF_HALF + idx]), y1R = decode_mulaw(buf[BUF_HALF + nxt]);
                    valR = lerp_delay_q15(y0R, y1R, frac);
                }
            };

            read_buf(loop_start + rd_q16, sL, sR);
            current_g711_sample = buf[((int32_t)(loop_start + rd_q16) >> 16) & buf_mask];

            // Smooth loop boundary crossfade
            if (xfade_ctr > 0) {
                int16_t xL, xR;
                xfade_rd += speed_q16;
                read_buf(xfade_rd, xL, xR);

                xfade_phase += xfade_step;
                int32_t val = xfade_phase >> 15;
                if (val > 32767) val = 32767;
                int16_t t = (int16_t)val;
                sL = lerp_q15(xL, sL, t);
                sR = lerp_q15(xR, sR, t);
                xfade_ctr--;
            }

            // Smooth onset crossfade
            if (onset_fade_ctr > 0) {
                onset_fade_phase += onset_fade_step;
                int32_t val = onset_fade_phase >> 15;
                if (val > 32767) val = 32767;
                int16_t t = (int16_t)val;
                sL = lerp_q15(inL, sL, t);
                sR = lerp_q15(inR, sR, t);
                onset_fade_ctr--;
            }

            // Smooth microsound grain windowing for short grain windows (< 500 samples / ~20ms)
            // Bypassed when glitchFeedback >= 16384 to preserve self-oscillating Karplus-Strong string delays!
            if (current_loop_len < 500 && glitchFeedback < 16384) {
                int32_t grain_phase = (sample_ctr * 32767) / current_loop_len;
                if (grain_phase > 32767) grain_phase = 32767;
                int32_t win = lookup_sine(grain_phase);
                if (win < 0) win = 0;
                sL = (sL * win) >> 15;
                sR = (sR * win) >> 15;
            }

            // Glitcher Feedback Loop (disabled when frozen to prevent volume build-up)
            if (glitchFeedback > 0 && !freezeGate) {
                int32_t idx = ((loop_start + rd_q16) >> 16) & buf_mask;
                int32_t scaled_fb = (glitchFeedback * 29491) >> 15;
                int16_t oldL = decode_mulaw(buf[idx]);
                int16_t oldR = mono_mode ? oldL : decode_mulaw(buf[BUF_HALF + idx]);
                int16_t newL = soft_limit_q15(((int32_t)oldL * (32768 - scaled_fb) + (int32_t)sL * scaled_fb) >> 15);
                int16_t newR = soft_limit_q15(((int32_t)oldR * (32768 - scaled_fb) + (int32_t)sR * scaled_fb) >> 15);
                buf[idx] = encode_mulaw(newL);
                if (!mono_mode) buf[BUF_HALF + idx] = encode_mulaw(newR);
            }

            int32_t mix_coef = (mainProb * 5);
            if (mix_coef > 32767) mix_coef = 32767;
            outL = lerp_q15(inL, sL, (int16_t)mix_coef);
            outR = lerp_q15(inR, sR, (int16_t)mix_coef);
            return;
        }

        // ── NORMAL GLITCH MODE ───────────────────────────────────────────────
        else {
            if (last_freezeGate) {
                last_freezeGate = false;
                freeze_releasing = false; // clear any pending release
                if (active) {
                    active = false;
                    int32_t loop_start = (((int32_t)freeze_wr - active_offset) & buf_mask) << 16;
                    dry_fade_rd = loop_start + rd_q16;
                    dry_fade_len = 512;
                    dry_fade_ctr = 512;
                    dry_fade_step = (32767 << 15) / 512;
                    dry_fade_phase = 32767 << 15;
                    refill_ctr = 4096; // Set a 170ms glitch-free refill period to load fresh audio
                }
            }
            last_freezeGate = false;
            int32_t norm_loop_size = 512;
            if (pulse1_live && clk_period_samples > 240) {
                // 16 rhythmic subdivisions (in 16th note units):
                // 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1 beat, 1.5b, 2b, 3b, 4b (1 bar), 6b, 8b (2 bars), 12b (3 bars), 16b (4 bars)
                static const int32_t clk_div_num[16] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256};
                
                // Dynamically count valid subdivisions that fit inside buf_size (Glitcher memory)
                int32_t valid_steps = 16;
                while (valid_steps > 1 && (((int64_t)clk_period_samples * clk_div_num[valid_steps - 1]) / 16) > buf_size) {
                    valid_steps--;
                }

                int32_t step = (size * valid_steps) >> 15;
                if (step < 0) step = 0;
                if (step >= valid_steps) step = valid_steps - 1;
                norm_loop_size = (clk_period_samples * clk_div_num[step]) / 16;
                if (norm_loop_size < 128) norm_loop_size = 128;
                if (norm_loop_size > buf_size) norm_loop_size = buf_size;
            } else {
                if (cv1Warp == 0) {
                    static const int32_t size_lut[9] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
                    int32_t num_steps = mono_mode ? 9 : 8;
                    int32_t step = (size * num_steps) >> 15;
                    if (step < 0) step = 0;
                    if (step > num_steps - 1) step = num_steps - 1;
                    norm_loop_size = size_lut[step];
                } else {
                    int32_t base_size = 128 + (size >> 1);
                    norm_loop_size = base_size + (cv1Warp * 4);
                }
            }
            norm_loop_size = clamp_i32(norm_loop_size, 128, buf_size);

            int32_t cur_xfade = norm_loop_size < 512 ? (norm_loop_size >> 1) : 256;
            if (cur_xfade < 4) cur_xfade = 4;

            if (!active) {
                write_buf(wr, inL, inR);

                bool boundary = false;
                if (pulse1_live && (clk_period_samples > 240)) {
                    boundary = p1_rising;
                } else {
                    trigger_ctr--;
                    if (trigger_ctr <= 0) {
                        trigger_ctr = norm_loop_size < 3072 ? 3072 : norm_loop_size;
                        boundary = true;
                    }
                }

                bool trigger = false;
                if (refill_ctr > 0) {
                    refill_ctr--;
                } else if (boundary) {
                    if (pulse1_live) {
                        // Clock-accumulated triggering: accumulate probability on each beat.
                        // At 50% prob, triggers every 2nd beat. At 33%, every 3rd. More musical than pure random.
                        clk_trig_accum += finalProb;
                        if (clk_trig_accum >= 32767 || eff_glitchInjector) {
                            trigger = true;
                            clk_trig_accum = 0;
                        }
                    } else {
                        bool immediate = (finalProb > 31000);
                        trigger = immediate || ((fast_rand(rand_seed) & 0x7FFF) < (uint32_t)finalProb) || eff_glitchInjector;
                    }
                } else if (eff_glitchInjector) {
                    trigger = true;
                }

                if (trigger) {
                    active = true;
                    freeze_wr = wr;
                    trigger_ctr = 0; // reset counter on trigger exit
                    active_duration_ctr = 0;
                    
                    int32_t final_size = norm_loop_size;
                    int32_t mid_amount = 16384 - abs(mainProb - 16384);
                    if (!pulse1_live) {
                        if (cv1Warp == 0) {
                            if ((int32_t)(fast_rand(rand_seed) & 0x7FFF) < mid_amount) {
                                int32_t step = (size * 8) >> 15;
                                uint32_t r = fast_rand(rand_seed) & 3;
                                if (r == 3) r = 0;
                                int32_t offset = (int32_t)r - 1;
                                step += offset;
                                if (step < 0) step = 0;
                                if (step > 7) step = 7;
                                static const int32_t size_lut[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
                                final_size = size_lut[step];
                            }
                        } else {
                            if ((int32_t)(fast_rand(rand_seed) & 0x7FFF) < mid_amount) {
                                int32_t range = final_size >> 2;
                                if (range > 0) {
                                    int32_t double_range = range * 2;
                                    if (double_range < 1) double_range = 1;
                                    int32_t offset = (((int32_t)(fast_rand(rand_seed) & 0x7FFF) * double_range) >> 15) - range;
                                    final_size += offset;
                                }
                            }
                        }
                    }
                    if (speedQuant >= 30000) {
                        uint32_t r = fast_rand(rand_seed) & 7;
                        if (pulse1_live && clk_period_samples > 240) {
                            uint32_t r_div = ((fast_rand(rand_seed) & 0xFFFF) * 5) >> 16;
                            static const int32_t shifts[5] = {4, 3, 2, 1, 0};
                            final_size = clk_period_samples >> shifts[r_div];
                        } else {
                            static const int32_t size_lut[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
                            final_size = size_lut[r];
                        }
                    } else if (speedQuant >= 19661) {
                        int32_t jitter_range = final_size >> 2;
                        if (jitter_range > 0) {
                            int32_t offset = (((int32_t)(fast_rand(rand_seed) & 0x7FFF) * (jitter_range * 2)) >> 15) - jitter_range;
                            final_size += offset;
                        }
                    }
                    current_loop_len = clamp_i32(final_size, 128, buf_size);
                    
                    arpeggio_step = 0;
                    if (is_loop_frozen) {
                        current_speed_q16 = isFreezePage ? target_speed_q16 : 65536;
                    } else {
                        current_speed_q16 = determine_speed_zoned(speedQuant, cv2Corruption, rand_seed, arpeggio_step, current_loop_len);
                    }
                    speed_q16 = current_speed_q16;
                    
                    rd_q16 = (speed_q16 >= 0) ? 0 : ((int64_t)current_loop_len << 16);
                    xfade_ctr = 0;
                    dry_fade_ctr = 0;
                    sample_ctr = 0;
                    
                    onset_fade_len = cur_xfade;
                    onset_fade_ctr = cur_xfade;
                    onset_fade_step = (32767 << 15) / cur_xfade;
                    onset_fade_phase = 0;
                }
                wr = (wr + 1) & buf_mask;
            }

            if (active) {
                active_duration_ctr++;
                auto read_buf = [&](int32_t ptr, int16_t &sL, int16_t &sR) {
                    int32_t  idx  = (ptr >> 16) & buf_mask;
                    int32_t  nxt  = (idx + 1)   & buf_mask;
                    uint16_t frac = (uint16_t)(ptr & 0xFFFF);
                    if (mono_mode) {
                        int16_t y0 = decode_mulaw(buf[idx]), y1 = decode_mulaw(buf[nxt]);
                        int16_t val = lerp_delay_q15(y0, y1, frac);
                        sL = val; sR = val;
                    } else {
                        int16_t y0L = decode_mulaw(buf[idx]),            y1L = decode_mulaw(buf[nxt]);
                        sL = lerp_delay_q15(y0L, y1L, frac);
                        int16_t y0R = decode_mulaw(buf[BUF_HALF + idx]), y1R = decode_mulaw(buf[BUF_HALF + nxt]);
                        sR = lerp_delay_q15(y0R, y1R, frac);
                    }
                };

                int32_t offset_samples = (scrubOffset * 32768) >> 15;
                int32_t lookback = current_loop_len;
                if (pulse1_live && clk_period_samples > 240) {
                    lookback = 0; // Capture drum hit transient directly at freeze_wr
                }
                if (is_loop_frozen) {
                    lookback = 0;
                }
                int32_t loop_start = (((int32_t)(freeze_wr) - lookback - offset_samples) & buf_mask) << 16;

                // Step arpeggiator on Pulse 2 rising edge (disabled when loop is frozen)
                if (!is_loop_frozen && pulse2_live && p2_rising) {
                    if (speedQuant >= 19661 && speedQuant < 26214) {
                        arpeggio_step++;
                        current_speed_q16 = determine_speed_zoned(speedQuant, cv2Corruption, rand_seed, arpeggio_step, current_loop_len);
                        trig_out2 = true;
                    }
                }

                if (isFreezePage) {
                    current_speed_q16 = target_speed_q16;
                } else if (is_loop_frozen) {
                    current_speed_q16 = 65536; // force unison pitch
                }
                speed_q16 = current_speed_q16;
                rd_q16 += speed_q16;
                sample_ctr++;

                bool crossed = false;
                if (speed_q16 >= 0) {
                    if (rd_q16 >= ((int64_t)current_loop_len << 16)) {
                        crossed = true;
                    }
                } else {
                    if (rd_q16 < 0) {
                        crossed = true;
                    }
                }

                // Natural boundary check determines loop completion (norm_loop_size is already clock-synced)

                if (crossed) {
                    trig_out1 = true; // Output loop sync trigger

                    // CD-skip buffer sliding under chaos:
                    int32_t chaos_depth = (globalNoiseScale - 16384);
                    if (chaos_depth < 0) chaos_depth = 0;
                    int32_t abs_cv2 = cv2Corruption < 0 ? -cv2Corruption : cv2Corruption;
                    chaos_depth += abs_cv2 * 8;
                    if (chaos_depth > 2000) {
                        uint32_t slip_roll = fast_rand(rand_seed) & 0x7FFF;
                        int32_t slip_prob = (chaos_depth * 10) >> 15;
                        if ((int32_t)slip_roll < slip_prob) {
                            int32_t slip_samples = (((int32_t)(fast_rand(rand_seed) & 0x7FFF)) - 16384) >> 4;
                            freeze_wr = (freeze_wr + slip_samples) & buf_mask;
                        }
                    }

                    uint32_t roll = fast_rand(rand_seed) & 0x7FFF;
                    int32_t loop_prob = finalProb;
                    // On long loop sizes (large X knob >= 1 beat), bound repeat probability so phrases play 1-2 times and exit gracefully
                    if (is_clock_sync && current_loop_len >= (int32_t)clk_period_samples && finalProb < 28000) {
                        if (loop_prob > 20000) loop_prob = 20000; // ~60% repeat chance per phrase
                    }

                    if (finalProb >= 32760) {
                        loop_prob = 32767; // 100% loop probability only at maximum Main Knob (full CW limit)
                    }
                    if (loop_prob > 32767) loop_prob = 32767;

                    bool keep_looping = (roll < (uint32_t)loop_prob) || eff_glitchInjector;
                    if (finalProb >= 32760) {
                        keep_looping = true; // Infinite repeat only at maximum Main Knob (full CW limit)
                    }

                    // Enforce a tempo-relative minimum glitch duration.
                    // Clocked: 1/4 beat minimum (capped at 3072). Unclocked: 3072 samples (~128ms).
                    int32_t min_active_dur = 3072;
                    if (is_clock_sync && clk_period_samples > 240) {
                        min_active_dur = clk_period_samples >> 2; // 1/4 beat
                        if (min_active_dur < 512) min_active_dur = 512;
                        if (min_active_dur > 3072) min_active_dur = 3072;
                    }
                    if (active_duration_ctr < min_active_dur) {
                        keep_looping = true;
                    }

                    if (keep_looping) {
                        xfade_rd = loop_start + rd_q16;
                        
                        bool reroll_every_boundary = (speedQuant >= 19661) && !is_loop_frozen;
                        if (reroll_every_boundary || (sample_ctr >= 1024 && !is_loop_frozen)) {
                            if (!pulse2_live) {
                                arpeggio_step++;
                                trig_out2 = true;
                            }
                            current_speed_q16 = determine_speed_zoned(speedQuant, cv2Corruption, rand_seed, arpeggio_step, current_loop_len);
                            sample_ctr = 0;
                        }
                        speed_q16 = current_speed_q16;
                        
                        int32_t final_size = norm_loop_size;
                        int32_t mid_amount = 16384 - abs(mainProb - 16384);
                        if (!pulse1_live) {
                            if (cv1Warp == 0) {
                                if ((int32_t)(fast_rand(rand_seed) & 0x7FFF) < mid_amount) {
                                    int32_t step = (size * 8) >> 15;
                                    uint32_t r = fast_rand(rand_seed) & 3;
                                    if (r == 3) r = 0;
                                    int32_t offset = (int32_t)r - 1;
                                    step += offset;
                                    if (step < 0) step = 0;
                                    if (step > 7) step = 7;
                                    static const int32_t size_lut[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
                                    final_size = size_lut[step];
                                }
                            } else {
                                if ((int32_t)(fast_rand(rand_seed) & 0x7FFF) < mid_amount) {
                                    int32_t range = final_size >> 2;
                                    if (range > 0) {
                                        int32_t double_range = range * 2;
                                        if (double_range < 1) double_range = 1;
                                        int32_t offset = (((int32_t)(fast_rand(rand_seed) & 0x7FFF) * double_range) >> 15) - range;
                                        final_size += offset;
                                    }
                                }
                            }
                        }
                        if (speedQuant >= 30000) {
                            uint32_t r = fast_rand(rand_seed) & 7;
                            if (pulse1_live && clk_period_samples > 240) {
                                uint32_t r_div = ((fast_rand(rand_seed) & 0xFFFF) * 5) >> 16;
                                static const int32_t shifts[5] = {4, 3, 2, 1, 0};
                                final_size = clk_period_samples >> shifts[r_div];
                            } else {
                                static const int32_t size_lut[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
                                final_size = size_lut[r];
                            }
                        } else if (speedQuant >= 19661) {
                            int32_t jitter_range = final_size >> 2;
                            if (jitter_range > 0) {
                                int32_t offset = (((int32_t)(fast_rand(rand_seed) & 0x7FFF) * (jitter_range * 2)) >> 15) - jitter_range;
                                final_size += offset;
                            }
                        }
                        current_loop_len = clamp_i32(final_size, 128, buf_size);

                        int32_t xfade_offset = current_loop_len > cur_xfade ? (current_loop_len - cur_xfade) : 0;
                        xfade_rd = (speed_q16 >= 0) ? (loop_start + ((int64_t)xfade_offset << 16)) : loop_start;
                        if (speed_q16 >= 0) {
                            rd_q16 = 0;
                        } else {
                            rd_q16 = ((int64_t)current_loop_len << 16);
                        }
                        xfade_len = cur_xfade;
                        xfade_ctr = cur_xfade;
                        xfade_step = (32767 << 15) / cur_xfade;
                        xfade_phase = 0;
                        
                        int32_t active_lookback = current_loop_len;
                        if (is_loop_frozen) {
                            active_lookback = 0;
                        }
                        loop_start = (((int32_t)freeze_wr - active_lookback - offset_samples) & buf_mask) << 16;
                    } else {
                        active = false;
                        trigger_ctr = norm_loop_size < 3072 ? 3072 : norm_loop_size;
                        dry_fade_rd = loop_start + rd_q16;
                        dry_fade_len = cur_xfade;
                        dry_fade_ctr = cur_xfade;
                        dry_fade_step = (32767 << 15) / cur_xfade;
                        dry_fade_phase = 32767 << 15;
                    }
                }

                if (active) {
                    int16_t sL, sR;
                    read_buf(loop_start + rd_q16, sL, sR);
                    current_g711_sample = buf[((int32_t)(loop_start + rd_q16) >> 16) & buf_mask];

                    if (xfade_ctr > 0) {
                        int16_t xL, xR;
                        xfade_rd += speed_q16;
                        read_buf(xfade_rd, xL, xR);

                        xfade_phase += xfade_step;
                        int32_t val = xfade_phase >> 15;
                        if (val > 32767) val = 32767;
                        int16_t t = (int16_t)val;
                        sL = lerp_q15(xL, sL, t);
                        sR = lerp_q15(xR, sR, t);
                        xfade_ctr--;
                    }

                    if (onset_fade_ctr > 0) {
                        onset_fade_phase += onset_fade_step;
                        int32_t val = onset_fade_phase >> 15;
                        if (val > 32767) val = 32767;
                        int16_t t = (int16_t)val;
                        sL = lerp_q15(inL, sL, t);
                        sR = lerp_q15(inR, sR, t);
                        onset_fade_ctr--;
                    }

                    if (glitchFeedback > 0 && !freezeGate) {
                        int32_t idx = ((loop_start + rd_q16) >> 16) & buf_mask;
                        int32_t scaled_fb = (glitchFeedback * 29491) >> 15;
                        int16_t oldL = decode_mulaw(buf[idx]);
                        int16_t oldR = mono_mode ? oldL : decode_mulaw(buf[BUF_HALF + idx]);
                        int16_t newL = soft_limit_q15(((int32_t)oldL * (32768 - scaled_fb) + (int32_t)sL * scaled_fb) >> 15);
                        int16_t newR = soft_limit_q15(((int32_t)oldR * (32768 - scaled_fb) + (int32_t)sR * scaled_fb) >> 15);
                        buf[idx] = encode_mulaw(newL);
                        if (!mono_mode) buf[BUF_HALF + idx] = encode_mulaw(newR);
                    }

                    // Wet/Dry mix reaches 100% wet in the first 20% of Main Knob rotation (mainProb >= 6553)
                    int32_t mix_coef = (mainProb * 5);
                    if (mix_coef > 32767) mix_coef = 32767;
                    outL = lerp_q15(inL, sL, (int16_t)mix_coef);
                    outR = lerp_q15(inR, sR, (int16_t)mix_coef);
                    return;
                }
            }
        }

        if (dry_fade_ctr > 0) {
            auto read_buf = [&](int32_t ptr, int16_t &sL, int16_t &sR) {
                int32_t  idx  = (ptr >> 16) & buf_mask;
                int32_t  nxt  = (idx + 1)   & buf_mask;
                uint16_t frac = (uint16_t)(ptr & 0xFFFF);
                if (mono_mode) {
                    int16_t y0 = decode_mulaw(buf[idx]), y1 = decode_mulaw(buf[nxt]);
                    int16_t val = lerp_delay_q15(y0, y1, frac);
                    sL = val; sR = val;
                } else {
                    int16_t y0L = decode_mulaw(buf[idx]),            y1L = decode_mulaw(buf[nxt]);
                    sL = lerp_delay_q15(y0L, y1L, frac);
                    int16_t y0R = decode_mulaw(buf[BUF_HALF + idx]), y1R = decode_mulaw(buf[BUF_HALF + nxt]);
                    sR = lerp_delay_q15(y0R, y1R, frac);
                }
            };

            int16_t sL, sR;
            dry_fade_rd += speed_q16;
            read_buf(dry_fade_rd, sL, sR);

            dry_fade_phase -= dry_fade_step;
            int32_t val = dry_fade_phase >> 15;
            if (val < 0) val = 0;
            if (val > 32767) val = 32767;
            int16_t t = (int16_t)val;
            int16_t wetL = lerp_q15(inL, sL, t);
            int16_t wetR = lerp_q15(inR, sR, t);
            outL = wetL;
            outR = wetR;
            dry_fade_ctr--;
        } else {
            outL = inL;
            outR = inR;
        }
    }
};

// ============================================================================
// 5.  RESONANT FILTER BLOCK
// ============================================================================
// Chamberlin State Variable Filter (SVF) — morphable LP / BP / HP.
// The SVF is the gold standard for musical fixed-point filters:
// one set of state variables produces all three outputs simultaneously,
// with no additional cost.
//
// KEY CORRECTNESS FIX vs. v1: The canonical Chamberlin update is:
//   hp  = in − r·v1 − v2
//   v1 += g·hp              ← single integration (velocity)
//   lp  = v2 + g·v1         ← position before integration step
struct FilterBlock {
    // Filter state — Q15 units but promoted to int32_t for headroom
    int32_t v1L = 0, v2L = 0;
    int32_t v1R = 0, v2R = 0;

    // Smoothed coefficients/parameters in Q15
    int32_t sm_g = 1000;
    int32_t sm_r = 8192;
    int32_t sm_cutoff = 16384;
    int32_t sm_grit = 0;

    // Output lowpass warmth filter states
    int32_t lp_outL = 0, lp_outR = 0;

    // DC blockers to strip offset before sending to reverb/outputs
    DCBlocker dcL;
    DCBlocker dcR;

    // Compressor envelope follower state
    int32_t env = 0;

    inline int32_t filter_saturate(int32_t x) {
        // Stabilize and soft-saturate using a division-free cubic curve: y = x - x^3 / 6
        int32_t clamped_x = x;
        if (clamped_x > 32767) clamped_x = 32767;
        int32_t x3 = (((clamped_x * clamped_x) >> 15) * clamped_x) >> 15;
        // x3 * 5461 >> 15 is x^3 / 6 (since 5461/32768 ≈ 1/6)
        int32_t y = clamped_x - ((x3 * 5461) >> 15);
        return y;
    }

    void init() {
        v1L = v2L = v1R = v2R = 0;
        sm_g = 1000;
        sm_r = 8192;
        sm_cutoff = 16384;
        sm_grit = 0;
        lp_outL = lp_outR = 0;
        dcL.init();
        dcR.init();
        env = 0;
    }

    __attribute__((always_inline)) inline void process(int16_t inL, int16_t &outL, int16_t inR, int16_t &outR,
                 int32_t cutoff, int32_t resonance, int32_t grit_param,
                 int32_t cv1Warp, int32_t cv2Corruption = 0)
    {
        (void)cv1Warp;
        (void)cv2Corruption;

        // Smooth control parameters to prevent clicking/plopping on fast sweeps
        IIR_SMOOTH(sm_cutoff, cutoff, 8);
        IIR_SMOOTH(sm_grit, grit_param, 8);

        int32_t eff_cutoff = sm_cutoff;
        int32_t eff_grit = sm_grit;

        // ── Cutoff and Mode Mapping ──────────────────────────────────────────
        int32_t target_g = 300;
        if (eff_cutoff < 16384) {
            int32_t ratio = (eff_cutoff * 32767) >> 14;
            if (ratio > 32767) ratio = 32767;
            int32_t quad_part = (ratio * ratio) >> 15;
            int32_t cubic_part = (quad_part * ratio) >> 15;
            // Smoother 3-way taper for natural, musical frequency response
            int32_t mixed = (ratio * 4000 + quad_part * 10000 + cubic_part * 18768) >> 15;
            target_g = 300 + mixed;
        } else {
            int32_t ratio = ((eff_cutoff - 16384) * 32767) >> 14;
            if (ratio > 32767) ratio = 32767;
            int32_t quad_part = (ratio * ratio) >> 15;
            int32_t cubic_part = (quad_part * ratio) >> 15;
            int32_t mixed = (ratio * 4000 + quad_part * 10000 + cubic_part * 18768) >> 15;
            target_g = 300 + mixed;
        }

        // 1V/Oct CV1 Pitch Tracking for Filter (active when filter is off-center & resonance >= 20000)
        if (cv1Warp != 0 && resonance >= 20000 && (eff_cutoff < 14884 || eff_cutoff > 17884)) {
            if (cv1Warp > 0) {
                int64_t mult = 1024 + (int64_t)cv1Warp;
                target_g = (int32_t)(((int64_t)target_g * mult) >> 10);
            } else {
                int64_t div = 1024 - (int64_t)cv1Warp;
                if (div < 64) div = 64;
                target_g = (int32_t)(((int64_t)target_g << 10) / div);
            }
        }
        target_g = clamp_i32(target_g, 150, 22000);

        // ── Resonance Damping Mapping ────────────────────────────────────────
        // Normal range: map [0..28000] to damping [32000..4500] (highly resonant).
        // Self-oscillation: above 28000, fade damping down to 0 (infinite Q) for clean sine waves.
        int32_t target_r;
        if (resonance < 28000) {
            target_r = 32000 - ((resonance * 27500) >> 15);
        } else {
            int32_t diff = resonance - 28000; // 0..4767
            target_r = 4500 - ((diff * 4500) / 4767);
        }
        target_r = clamp_i32(target_r, 0, 32000);

        // ── Grit / Folder Mapping (Y knob) ───────────────────────────────────
        // Swept parameters (drive, fold_mix, fold_gain, fuzz_gain) are calculated continuously
        // inside the processing stage below to ensure pop-free, gradual timbre morphs.

        // Output volume compensation to keep perceived loudness stable as grit increases (scales down to 50%)
        int32_t grit_volume_scale = 32768 - (eff_grit / 2);

        // Smooth coefficients to eliminate zipper noise
        IIR_SMOOTH(sm_g, target_g, 8);
        IIR_SMOOTH(sm_r, target_r, 8);

        int32_t g = sm_g;
        int32_t r = sm_r;

        // Compensate for self-oscillation volume spike by slightly dipping the gain at high resonance
        int32_t gain_q15 = 16384 - ((32000 - r) >> 4);
        gain_q15 = clamp_i32(gain_q15, 13000, 16384);

        // ── Volume Fade at Extremes ──────────────────────────────────────────
        // ── Volume Fade at Extremes ──────────────────────────────────────────
        int32_t volume_scale = 32768; // Q15
        if (eff_cutoff < 512) {
            volume_scale = eff_cutoff << 6;
        } else if (eff_cutoff > 32255) {
            volume_scale = (32767 - eff_cutoff) << 6;
        }

        // ── 1. Drive & Distortion Stage ──────────────────────────────────────
        int16_t distL = inL;
        int16_t distR = inR;
        if (eff_grit > 0) {
            // Calculate continuous parameter sweeps based on eff_grit
            // 1. Drive sweeps from 1.0x (32768) to 3.0x (98304) to prevent severe digital noise
            int32_t drive = 32768 + (eff_grit * 2);

            // 2. Folder Mix sweeps from 0% (at 10000) to 100% (at 22000)
            int32_t fold_mix = 0;
            if (eff_grit >= 10000) {
                if (eff_grit < 22000) {
                    fold_mix = ((eff_grit - 10000) * 22370) >> 13;
                } else {
                    fold_mix = 32768;
                }
            }

            // 3. Folder Gain sweeps from 128 (0.5x) to 384 (1.5x) (moderate Buchla-style folding folds)
            int32_t fold_gain_q8 = 128;
            if (eff_grit >= 10000) {
                fold_gain_q8 = 128 + (((eff_grit - 10000) * 256) / 22767);
            }

            // 4. Fuzz Gain sweeps from 1.0x (32768) to 2.0x (65536) for clean soft-saturating overdrive
            int32_t fuzz_gain = 32768;
            if (eff_grit >= 22000) {
                int32_t diff = eff_grit - 22000;
                int32_t ratio = (diff * 24931) >> 13; // 0..32767
                int32_t ratio_sq = (ratio * ratio) >> 15; // 0..32767 (quadratic curve)
                fuzz_gain = 32768 + ratio_sq; // sweeps 32768 to 65535 (2.0x)
            }

            // Drive inputs
            int32_t drive_xL = ((int32_t)distL * drive) >> 15;
            int32_t drive_xR = ((int32_t)distR * drive) >> 15;

            // Calculate soft overdrive (Stage 1)
            int16_t odL = soft_limit_q15(drive_xL);
            int16_t odR = soft_limit_q15(drive_xR);

            // Calculate wavefolder (Stage 2)
            int32_t phaseL = (drive_xL * fold_gain_q8) >> 8;
            int32_t phaseR = (drive_xR * fold_gain_q8) >> 8;
            int16_t foldL = lookup_sine((uint16_t)phaseL);
            int16_t foldR = lookup_sine((uint16_t)phaseR);

            // Apply fuzz gain boost if we are in the fuzz region (Stage 3)
            if (eff_grit >= 22000) {
                int16_t fL = soft_limit_q15(((int32_t)foldL * fuzz_gain) >> 15);
                int16_t fR = soft_limit_q15(((int32_t)foldR * fuzz_gain) >> 15);

                int32_t fuzz_diff = eff_grit - 22000;
                if (fuzz_diff < 2048) {
                    int32_t fuzz_blend = fuzz_diff << 4; // morph range is exactly 2048 samples
                    foldL = lerp_q15(foldL, fL, fuzz_blend);
                    foldR = lerp_q15(foldR, fR, fuzz_blend);
                } else {
                    foldL = fL;
                    foldR = fR;
                }
            }

            // Linearly interpolate between soft overdrive and wavefolded/fuzzed signal
            distL = lerp_q15(odL, foldL, fold_mix);
            distR = lerp_q15(odR, foldR, fold_mix);
        }

        // ── 2. Post-Distortion Compressor Stage ──────────────────────────────
        if (eff_grit > 0) {
            int32_t absL = distL < 0 ? -distL : distL;
            int32_t absR = distR < 0 ? -distR : distR;
            int32_t peak = absL > absR ? absL : absR;
            if (peak > 32767) peak = 32767;

            // attack time ~2.5ms, release time ~40ms
            int32_t attack_shift = 5;
            int32_t release_shift = 9;
            if (peak > env) env += (peak - env) >> attack_shift;
            else env += (peak - env) >> release_shift;

            // Threshold sweeps from 32767 down to 12767 as grit increases
            int32_t thresh = 32767 - ((eff_grit * 20000) >> 15);
            // Compression slope sweeps up from 0 to 15000 (around 1.5:1 ratio)
            int32_t slope = (eff_grit * 15000) >> 15;

            int32_t gain_coef = 32768; // Q15
            if (env > thresh) {
                int32_t overshoot = env - thresh;
                int32_t gain_reduction = ((int32_t)overshoot * slope) >> 15;
                gain_coef = 32768 - gain_reduction;
                if (gain_coef < 18000) gain_coef = 18000; // max ~5.2dB gain reduction for constant volume
            }

            distL = ((int32_t)distL * gain_coef) >> 15;
            distR = ((int32_t)distR * gain_coef) >> 15;
        }

        // ── 3. SVF Filtering Stage (Sweeps the distorted sound!) ─────────────
        // Left Channel
        int32_t feedbackL = ((r * v1L) >> 15) + v2L;
        int32_t hpL = (int32_t)distL - filter_saturate(feedbackL);
        v1L       += (g * hpL) >> 15;
        v1L        = soft_limit_q15(v1L);
        int32_t lpL = v2L + ((g * v1L) >> 15);
        v2L        = lpL;
        v2L        = soft_limit_q15(v2L);

        int16_t lp16L = saturate_q15(lpL);
        int16_t hp16L = saturate_q15(hpL);
        int16_t morphedL = (eff_cutoff < 16384) ? lp16L : hp16L;

        if (eff_cutoff < 16384) {
            if (eff_cutoff > 14336) {
                int32_t dry_ratio = (eff_cutoff - 14336) << 4;
                morphedL = lerp_q15(morphedL, distL, dry_ratio);
            }
        } else {
            if (eff_cutoff < 18432) {
                int32_t dry_ratio = (18432 - eff_cutoff) << 4;
                morphedL = lerp_q15(morphedL, distL, dry_ratio);
            }
        }

        // Right Channel
        int32_t feedbackR = ((r * v1R) >> 15) + v2R;
        int32_t hpR = (int32_t)distR - filter_saturate(feedbackR);
        v1R       += (g * hpR) >> 15;
        v1R        = soft_limit_q15(v1R);
        int32_t lpR = v2R + ((g * v1R) >> 15);
        v2R        = lpR;
        v2R        = soft_limit_q15(v2R);

        int16_t lp16R = saturate_q15(lpR);
        int16_t hp16R = saturate_q15(hpR);
        int16_t morphedR = (eff_cutoff < 16384) ? lp16R : hp16R;

        if (eff_cutoff < 16384) {
            if (eff_cutoff > 14336) {
                int32_t dry_ratio = (eff_cutoff - 14336) << 4;
                morphedR = lerp_q15(morphedR, distR, dry_ratio);
            }
        } else {
            if (eff_cutoff < 18432) {
                int32_t dry_ratio = (18432 - eff_cutoff) << 4;
                morphedR = lerp_q15(morphedR, distR, dry_ratio);
            }
        }

        // ── 4. Warmth Smoothing & DC Blocker ─────────────────────────────────
        // Left Channel
        {
            // Gentle post-distortion lowpass filter to smooth out harsh high harmonics
            int32_t lp_coef = 32768 - ((eff_grit * 12000) >> 15); // sweeps from 32768 down to 20768
            lp_outL += (((int32_t)morphedL - lp_outL) * lp_coef) >> 15;
            int16_t finalL = dcL.process((int16_t)lp_outL);

            int32_t out32 = saturate_q15(((int32_t)finalL * gain_q15) >> 14);
            out32 = (out32 * grit_volume_scale) >> 15;
            outL = (out32 * volume_scale) >> 15;
        }

        // Right Channel
        {
            // Gentle post-distortion lowpass filter to smooth out harsh high harmonics
            int32_t lp_coef = 32768 - ((eff_grit * 12000) >> 15); // sweeps from 32768 down to 20768
            lp_outR += (((int32_t)morphedR - lp_outR) * lp_coef) >> 15;
            int16_t finalR = dcR.process((int16_t)lp_outR);

            int32_t out32 = saturate_q15(((int32_t)finalR * gain_q15) >> 14);
            out32 = (out32 * grit_volume_scale) >> 15;
            outR = (out32 * volume_scale) >> 15;
        }
    }
};

// ============================================================================
// 6.  STUDIO REVERB BLOCK
// ============================================================================
// Classic Schroeder plate reverb topology:
//   4 parallel comb filters → summed → 2 series all-pass diffusers.
// Left and right channels use slightly different prime-length delays for
// natural decorrelation and wide stereo image.
//
// Comb filter improvement: the first-order damping LPF state is kept as
// int32_t (not int16_t) so it doesn't quantise the HF rolloff.
// The comb buffer size is capped at 1700 to comfortably fit prime lengths
// up to ≈1700 samples (≈35 ms at 48 kHz).
//
// Parameters:
//   mainMix — Wet / dry blend.
//   decay   — RT60 / tail length: [0..32767] → feedback [0.70 to 0.97].
//   damping — HF damping per loop. 0 = bright; 32767 = very warm/muffled.
// ============================================================================

struct ReverbBlock {
    struct AP {
        int16_t *bufIn, *bufOut;
        uint16_t mask, ptr, len;
        int16_t g;

        void init(int16_t *b, uint16_t m, uint16_t l, int16_t gain) {
            bufIn = b;
            bufOut = b + m + 1;
            mask = m;
            len = l & m; // Store effective length
            g = gain;
            ptr = 0;
        }

        int16_t process(int16_t in, int32_t len_scale) { // len_scale is Q15
            int32_t scaled_len_q16 = len_scale * len * 2;
            int32_t scaled_len_int = scaled_len_q16 >> 16;
            uint16_t frac = (uint16_t)(scaled_len_q16 & 0xFFFF);

            if (scaled_len_int < 2) {
                scaled_len_int = 2;
                frac = 0;
            }
            if (scaled_len_int >= mask) {
                scaled_len_int = mask - 1;
                frac = 0xFFFF;
            }

            uint16_t rd1 = (ptr - scaled_len_int) & mask;
            uint16_t rd2 = (ptr - (scaled_len_int + 1)) & mask;

            int16_t dIn  = lerp_delay_q15(bufIn[rd1],  bufIn[rd2],  frac);
            int16_t dOut = lerp_delay_q15(bufOut[rd1], bufOut[rd2], frac);

            int32_t interm = ((int32_t)(g * in) >> 15) + dIn - ((int32_t)(dOut * g) >> 15);
            int16_t out = soft_limit_q15(interm);
            bufIn[ptr] = in;
            bufOut[ptr] = out;
            ptr = (ptr + 1) & mask;
            return out;
        }

        inline int16_t process_fixed(int16_t in) {
            uint16_t rd = (ptr - len) & mask;
            int16_t dIn  = bufIn[rd];
            int16_t dOut = bufOut[rd];

            int32_t interm = ((int32_t)(g * in) >> 15) + dIn - ((int32_t)(dOut * g) >> 15);
            int16_t out = soft_limit_q15(interm);
            bufIn[ptr] = in;
            bufOut[ptr] = out;
            ptr = (ptr + 1) & mask;
            return out;
        }
    };

    struct Delay {
        int16_t *buf;
        uint16_t mask, ptr, len;

        void init(int16_t *b, uint16_t m, uint16_t l) {
            buf = b;
            mask = m;
            len = l & m; // Store effective length
            ptr = 0;
        }

        void write(int16_t in) {
            buf[ptr] = in;
            ptr = (ptr + 1) & mask;
        }

        int16_t read(int32_t len_scale) { // len_scale is Q15
            int32_t scaled_len_q16 = len_scale * len * 2;
            int32_t scaled_len_int = scaled_len_q16 >> 16;
            uint16_t frac = (uint16_t)(scaled_len_q16 & 0xFFFF);

            if (scaled_len_int < 2) {
                scaled_len_int = 2;
                frac = 0;
            }
            if (scaled_len_int >= mask) {
                scaled_len_int = mask - 1;
                frac = 0xFFFF;
            }

            uint16_t rd1 = (ptr - scaled_len_int) & mask;
            uint16_t rd2 = (ptr - (scaled_len_int + 1)) & mask;

            int16_t val1 = buf[rd1];
            int16_t val2 = buf[rd2];
            return lerp_delay_q15(val1, val2, frac);
        }

        inline int16_t read_integer(int32_t len_scale) {
            int32_t scaled_len = (len_scale * len) >> 15;
            if (scaled_len < 1) scaled_len = 1;
            if (scaled_len >= mask) scaled_len = mask - 1;
            uint16_t rd = (ptr - scaled_len) & mask;
            return buf[rd];
        }
    };

    int16_t mem[28672];
    AP apIn[4], apTankL, apTankR;
    Delay modL, d1L, d2L, modR, d1R, d2R;
    int32_t lpL = 0, lpR = 0, lpIn = 0;
    uint32_t lfo_phase1 = 0, lfo_phase2 = 0, lfo_phase3 = 0, lfo_phase4 = 0;
    int32_t lp_size_scale = 32767;

    // Decimation state for glitch effect
    uint16_t decimate_phase = 0;
    int16_t last_outL = 0;
    int16_t last_outR = 0;
    int32_t dec_lpL = 0, dec_lpR = 0;

    // Input soft-limiter: fast-attack / slow-release gain reduction to absorb
    // clipped transients and pops gracefully instead of hard-slamming the tank.
    int32_t limiter_gain = 32767; // Q15 gain applied to tank input (32767 = unity)

    DCBlocker dc_loopL;
    DCBlocker dc_loopR;

    // Pre-tank EQ state variables
    int32_t pre_hpL = 0, pre_hpR = 0;
    int32_t pre_lpL = 0, pre_lpR = 0;

    void init() {
        memset(mem, 0, sizeof(mem));
        int16_t *p = mem;
        apIn[0].init(p, 127, 229, 24576);
        p += 256;
        apIn[1].init(p, 127, 172, 24576);
        p += 256;
        apIn[2].init(p, 511, 611, 20480);
        p += 1024;
        apIn[3].init(p, 255, 447, 20480);
        p += 512;
        modL.init(p, 1023, 1083);
        p += 1024;
        d1L.init(p, 4095, 6000);
        p += 4096;
        apTankL.init(p, 2047, 2903, 16384);
        p += 4096;
        d2L.init(p, 4095, 5800);
        p += 4096;
        modR.init(p, 1023, 1464);
        p += 1024;
        d1R.init(p, 4095, 6200);
        p += 4096;
        apTankR.init(p, 2047, 3850, 16384);
        p += 4096;
        d2R.init(p, 4095, 5500);
        p += 4096;

        lpL = 0;
        lpR = 0;
        lpIn = 0;
        pre_hpL = pre_hpR = 0;
        pre_lpL = pre_lpR = 0;
        lfo_phase1 = 0;
        lfo_phase2 = 16384 << 16;
        lfo_phase3 = 32768 << 16;
        lfo_phase4 = 49152 << 16;
        decimate_phase = 0;
        last_outL = 0;
        last_outR = 0;
        dec_lpL = 0;
        dec_lpR = 0;
        limiter_gain = 32767;
        dc_loopL.init();
        dc_loopR.init();
        lp_size_scale = 32767;
    }

    __attribute__((always_inline)) inline void process(int16_t &L, int16_t &R, int32_t mix, int32_t size_scale,
                 int32_t decay, int32_t damp, int32_t lofi_level,
                 int32_t sparkle_level, int32_t circuit_bent_level,
                 int32_t lofi_shift, int32_t lofi_frac, int32_t reverb_mode = 0,
                 bool dual_mono_mode = false) {
        if (mix < 50) {
            return;
        }
        // Modulate delay times independently using 4 slow, prime-spaced LFOs (speeds ~0.4Hz to 0.9Hz) (doubled for 24kHz)
        lfo_phase1 += 122;
        lfo_phase2 += 158;
        lfo_phase3 += 194;
        lfo_phase4 += 226;

        auto get_tri = [](uint32_t phase) -> int32_t {
            uint16_t ph = (uint16_t)(phase >> 16);
            int32_t tmp = (ph < 32768) ? ((ph << 1) - 32768) : (32767 - ((ph - 32768) << 1));
            return tmp; // [-32768, 32767]
        };

        // Jitter/modulate the size scales independently (depth = ~150 Q15 units, approx 0.45% size)
        int32_t mod1 = (get_tri(lfo_phase1) * 150) >> 15;
        int32_t mod2 = (get_tri(lfo_phase2) * 150) >> 15;
        int32_t mod3 = (get_tri(lfo_phase3) * 150) >> 15;
        int32_t mod4 = (get_tri(lfo_phase4) * 150) >> 15;

        // Smooth size scale slowly to eliminate pitch-glide howling when changing room sizes
        lp_size_scale += (size_scale - lp_size_scale) >> 11; // ~85ms time constant

        int32_t scale_d1L = lp_size_scale + mod1;
        int32_t scale_d2L = lp_size_scale + mod2;
        int32_t scale_d1R = lp_size_scale + mod3;
        int32_t scale_d2R = lp_size_scale + mod4;
        int32_t scale_modL = lp_size_scale + ((mod1 + mod2) >> 1);
        int32_t scale_modR = lp_size_scale + ((mod3 + mod4) >> 1);



        auto clamp_scale = [](int32_t s) -> int32_t {
            if (s < 3276) return 3276;
            if (s > 32767) return 32767;
            return s;
        };

        scale_d1L = clamp_scale(scale_d1L);
        scale_d2L = clamp_scale(scale_d2L);
        scale_d1R = clamp_scale(scale_d1R);
        scale_d2R = clamp_scale(scale_d2R);
        scale_modL = clamp_scale(scale_modL);
        scale_modR = clamp_scale(scale_modR);

        // Send level is scaled by mix so at low mix settings less signal feeds the tank
        int16_t monoL = dual_mono_mode ? L : (int16_t)(((int32_t)L + (int32_t)R) >> 1);
        int16_t monoR = dual_mono_mode ? R : (int16_t)(((int32_t)L + (int32_t)R) >> 1);
        {
            int32_t send = (mix < 16384) ? (mix * 2) : 32767;
            monoL = (int16_t)(((int32_t)monoL * send) >> 15);
            monoR = (int16_t)(((int32_t)monoR * send) >> 15);
        }


        // Input soft-limiter — prevents clipped transients / pops from slamming the tank
        {
            int32_t peakL = (int32_t)monoL < 0 ? -(int32_t)monoL : (int32_t)monoL;
            int32_t gain_target = 32767;
            if (peakL > 28000) {
                gain_target = (28000 * 32767) / peakL;
            }
            if (gain_target < limiter_gain) {
                limiter_gain += (gain_target - limiter_gain) >> 3;  // fast attack
            } else {
                limiter_gain += (gain_target - limiter_gain) >> 8;  // faster release to prevent tank starvation
            }
            monoL = (int16_t)(((int32_t)monoL * limiter_gain) >> 15);
            monoR = (int16_t)(((int32_t)monoR * limiter_gain) >> 15);
        }

        // ── Pre-Tank EQing (HPF @ ~120 Hz to cut sub-bass mud, LPF @ ~9 kHz to tame harsh transients) ──
        pre_hpL += (((int32_t)monoL - pre_hpL) * 3200) >> 15;
        pre_hpR += (((int32_t)monoR - pre_hpR) * 3200) >> 15;
        monoL = (int16_t)((int32_t)monoL - pre_hpL);
        monoR = (int16_t)((int32_t)monoR - pre_hpR);

        pre_lpL += (((int32_t)monoL - pre_lpL) * 24000) >> 15;
        pre_lpR += (((int32_t)monoR - pre_lpR) * 24000) >> 15;
        monoL = (int16_t)pre_lpL;
        monoR = (int16_t)pre_lpR;

        // ── Spring Reverb Dispersion & Drip (OP-1 Style Dual Spring Tank) ────────
        if (reverb_mode == 1) {
            // High-pass filter to cut sub-bass rumble from spring tank (< 180Hz)
            monoL = dc_loopL.process(monoL);
            monoR = dc_loopR.process(monoR);

            // Left Spring Coil Dispersion (high-density chirp)
            int32_t sp1L = ((int32_t)monoL * 24000) >> 15;
            int32_t sp2L = ((int32_t)sp1L * 20000) >> 15;
            monoL = saturate_q15(monoL + sp1L - sp2L);

            // Right Spring Coil Dispersion (prime offset chirp for wide 3D stereo drip)
            int32_t sp1R = ((int32_t)monoR * 20000) >> 15;
            int32_t sp2R = ((int32_t)sp1R * 25000) >> 15;
            monoR = saturate_q15(monoR + sp1R - sp2R);
            
            // Add asymmetric 14Hz spring coil wobble/flutter modulation
            scale_modL += (get_tri(lfo_phase1 * 3) * 450) >> 15;
            scale_modR += (get_tri(lfo_phase3 * 3 + 16384) * 450) >> 15;
        }

        // Input all-passes are kept at fixed scale to prevent pitch-glide in the diffusion network
        int16_t monoL_ap = apIn[0].process_fixed(monoL);
        monoL_ap = apIn[1].process_fixed(monoL_ap);

        int16_t monoR_ap = apIn[2].process_fixed(monoR);
        monoR_ap = apIn[3].process_fixed(monoR_ap);

        // Read tank loop outputs using independent scales
        int16_t tOutL = d2L.read_integer(scale_d2L);
        int16_t tOutR = d2R.read_integer(scale_d2R);

        // Isolate Left and Right reverb tank feedback in Dual Mono Mode
        int16_t fb_L = dual_mono_mode ? tOutL : tOutR;
        int16_t fb_R = dual_mono_mode ? tOutR : tOutL;

        // Run high-pass sub-bass filter (~120Hz) INSIDE the feedback loop to eliminate sub-rumble explosions
        lpIn += (((int32_t)fb_L - lpIn) * 3500) >> 15;
        fb_L = soft_limit_q15(fb_L - (int16_t)lpIn);
        fb_R = soft_limit_q15(dc_loopR.process(fb_R));

        // Progressive in-tank degradation: bitcrush & XOR mask inside feedback loop
        if (lofi_level > 2000) {
            int32_t shift = (lofi_level * 3) >> 15; // 0..3 bits shift
            if (shift > 0) {
                fb_L = (fb_L >> shift) << shift;
                fb_R = (fb_R >> shift) << shift;
            }
        }
        if (sparkle_level > 4000) {
            int16_t mask = (int16_t)((sparkle_level * 15) >> 15);
            fb_L ^= mask;
            fb_R ^= mask;
        }

        // Left Tank
        int32_t iL = (int32_t)monoL_ap + (((int32_t)decay * fb_L) >> 15);
        int16_t iL_soft = soft_limit_q15(iL);
        int16_t sL = iL_soft + (int16_t)((16384 * modL.read_integer(scale_modL)) >> 15);
        modL.write(saturate_q15((int32_t)iL_soft - (int16_t)((16384 * sL) >> 15)));
        d1L.write(sL);
        sL = d1L.read_integer(scale_d1L);
        lpL += (((int32_t)sL - lpL) * damp) >> 15;
        lpL = saturate_q15(lpL);
        sL = (int16_t)lpL;
        sL = apTankL.process_fixed(sL);
        d2L.write(sL);

        // Right Tank
        int32_t iR = (int32_t)monoR_ap + (((int32_t)decay * fb_R) >> 15);
        int16_t iR_soft = soft_limit_q15(iR);
        int16_t sR = iR_soft + (int16_t)((16384 * modR.read_integer(scale_modR)) >> 15);
        modR.write(saturate_q15((int32_t)iR_soft - (int16_t)((16384 * sR) >> 15)));
        d1R.write(sR);
        sR = d1R.read_integer(scale_d1R);
        lpR += (((int32_t)sR - lpR) * damp) >> 15;
        lpR = saturate_q15(lpR);
        sR = (int16_t)lpR;
        sR = apTankR.process_fixed(sR);
        d2R.write(sR);

        int32_t wetL = sL;
        int32_t wetR = sR;

        // Wet output degradation:
        // 1. Vintage G.711 mu-law compander bitcrushing
        if (lofi_level > 0) {
            int16_t crushL = compress_expand_mulaw_variable((int16_t)wetL, lofi_level);
            int16_t crushR = compress_expand_mulaw_variable((int16_t)wetR, lofi_level);
            wetL = lerp_q15((int16_t)wetL, crushL, lofi_level);
            wetR = lerp_q15((int16_t)wetR, crushR, lofi_level);
        }

        // 2. Decimation & XOR Shred (circuit-bent range)
        if (circuit_bent_level > 0) {
            int32_t dec_factor = 1 + ((circuit_bent_level * 5) >> 15); // up to 6x decimation
            decimate_phase++;
            if (decimate_phase >= dec_factor) {
                decimate_phase = 0;
                last_outL = (int16_t)wetL;
                last_outR = (int16_t)wetR;
            }
            wetL = last_outL;
            wetR = last_outR;

            if (circuit_bent_level > 12000) {
                int16_t shred_mask = (int16_t)(((circuit_bent_level - 12000) * 127) >> 14);
                wetL ^= shred_mask;
                wetR ^= shred_mask;
            }
        }

        wetL = soft_limit_q15(wetL);
        wetR = soft_limit_q15(wetR);

        // Wet/Dry mix using split_mix_q15 to prevent volume drops
        L = split_mix_q15(L, (int16_t)wetL, (int16_t)mix);
        R = split_mix_q15(R, (int16_t)wetR, (int16_t)mix);
    }
};

#endif // DSP_BLOCKS_H
