#define COMPUTERCARD_SAMPLE_RATE_DIV 2 // Run at 24kHz — doubles CPU budget per sample

#include "ComputerCard.h"
#include "fixed_math.h"
#include "dsp_blocks.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include <atomic>
#include <cstring>
#include "hardware/flash.h"
#include "hardware/sync.h"

// DC Blockers and dynamic softener filters for dual inputs
DCBlocker dc_inL;
DCBlocker dc_inR;
int32_t lp_inL = 0;
int32_t lp_inR = 0;

// Anti-aliasing lowpass state (1-pole, -3dB @ ~8.5 kHz @ 24 kHz)
// Removes MUX clock sub-harmonics that the 12 kHz notch in ComputerCard misses.
int32_t lp_aa_L = 0;
int32_t lp_aa_R = 0;


inline int32_t apply_deadzone(int32_t val) {
    if (val < 500) return 0;
    if (val > 32200) return 32767;
    return val;
}

// ============================================================================
// Global DSP Instances & State Variables
// ============================================================================
int16_t sine_table[SINE_TABLE_SIZE + 1];

uint32_t rand_seed = 987654321u;
int16_t mulaw_decode_table[256];

// Sequential DSP block instances — all live in SRAM (global scope)
ChorusBlock       chorus;
CodecDemolisherBlock codec;
MultiTapDelayBlock   delay_fx;  // avoid shadowing std::delay
GlitcherBlock     glitcher;
FilterBlock       filter;
ReverbBlock       reverb;

// ============================================================================
// Core Parameter Double-Buffer Struct (Core 0 → Core 1 Param Sharing)
// ============================================================================
struct Core1Params {
    int32_t chorus_mix;
    int32_t chorus_rate;
    int32_t chorus_depth_fb; // Keep for legacy, but we will use the decoded ones
    bool karplus_active;
    int32_t chorus_depth;
    int32_t chorus_feedback;
    int32_t chorus_xor_mask;

    int32_t codec_mix;
    int32_t codec_downsample;
    int32_t codec_ringing_xor;
    int32_t codec_mp3_ring;
    int32_t codec_fuzz;
    int32_t codec_decimate;
    int32_t codec_pop_prob;
    int32_t codec_click_depth;
    int32_t codec_bad_conn;
    int32_t codec_scramble;
    int32_t codec_sputter_prob;
    int32_t codec_tape_sat;
    int32_t codec_tape_hiss;
    int32_t codec_tape_wow_flutter;
    int32_t codec_active_loss;

    int32_t delay_mix;
    int32_t delay_time;
    int32_t delay_feedback;

    int32_t glitch_mix;
    int32_t glitch_size;
    int32_t glitch_speed;
    int32_t glitch_speed_mapped;
    int32_t glitch_feedback;
    int32_t global_noise_scale;

    int32_t filter_cutoff;
    int32_t filter_res;
    int32_t filter_morph;

    int32_t reverb_mix;
    int32_t reverb_mode;
    int32_t reverb_size;
    int32_t reverb_decay;
    int32_t reverb_damp;
    int32_t reverb_lofi_level;
    int32_t reverb_sparkle_level;
    int32_t reverb_circuit_bent_level;
    int32_t reverb_lofi_shift;
    int32_t reverb_lofi_frac;

    int32_t cv1;
    int32_t cv1_bipolar;
    int32_t cv2;

    int32_t grittiness_macro;

    int32_t input_width;
    int32_t routing_mode;

    int32_t glitch_loop_size;
    int32_t glitch_target_offset;
    int32_t glitch_speed_q16;

    bool freeze;
    bool stutter;
    bool no_audio1;
    bool no_audio2;
    bool mono_mode;       // Extended Mono Mode active (Input 2 unplugged + user setting)
    bool dual_mono_mode;  // Dual Mono Mode active (Switch DOWN + Knob X > 31500)
    bool is_freeze_page;
    bool flash_writing;
    bool pulse1_live;
    bool pulse2_live;
    bool cv1_live;
    bool cv2_live;
};

volatile Core1Params g_params[2];
std::atomic<uint32_t> g_params_idx{0};
volatile uint32_t g_clk_period_samples = 0;
// Grittiness macro state (written on Core 0, read in push_params_to_core1)
static int32_t grittiness_macro = 16384; // 50% = transparent: baking at this value changes nothing
static bool g_macro_active = false;
static int32_t global_input_width = 9830;
static int32_t global_routing_mode = 0;
static bool    global_mono_mode    = false; // Extended Mono Mode: doubles delay+glitch time (enabled via Macro X CCW limit)
static bool    global_dual_mono_mode = false; // Dual Mono Mode: decorrelated 2-channel independent processing
static int last_modified_macro_knob = 0; // 0 = Macro, 1 = Width, 2 = Routing

// ============================================================================
// Core 0: UI State & Virtual Parameters
// ============================================================================
static int     currentPage          = 0;    // [0, 5]
static uint32_t pageFlashTimer      = 0;    // ms remaining in page-change flash

// Virtual parameter table -- 6 pages x 3 knobs [Main, X, Y]
// Values are Q15 [0..32767].
static int32_t vp[6][3] = {
    // Page 0 -- Chorus / Tape Loss: Mix, Rate/Drift, Saturation/Damping
    {     0, 12000, 16384 },
    // Page 1 -- Digital Loss: Mix/Strength, Decimate/Bitcrush, Corruption/Glitches
    {     0, 10000, 12000 },
    // Page 2 -- Delay:    Wet,   Time,   Feedback
    {     0, 12000,  8000 },
    // Page 3 -- Glitcher: Mix,   Size,   Speed
    {     0, 16384, 16384 },
    // Page 4 -- Filter:   Cutoff, Res,   Type/Morph
    { 16384,     0,     0 },
    // Page 5 -- Reverb:   Wet,   Decay,  Damping
    {     0, 16384, 16384 }
};

// Dedicated freeze scrub buffer -- completely separate from vp[3] (Glitcher page)
// so that freeze knob adjustments never corrupt the live Glitcher settings.
static int32_t freeze_vp[3] = { 32767, 19661, 16384 }; // scrub offset (32767 = current beat), loop size, speed

static uint32_t chainVisTimer = 0;
static int      chainVisMode = 0;
static bool     chainVisMono = false;

static void get_routing_chain(int routing_mode, bool mono_mode, int chain[6]) {
    switch (routing_mode) {
        case 1: // Space Wash
            if (mono_mode) {
                chain[0] = 5; chain[1] = 4; chain[2] = 2; chain[3] = 3; chain[4] = 0; chain[5] = 1;
            } else {
                chain[0] = 5; chain[1] = 4; chain[2] = 0; chain[3] = 2; chain[4] = 3; chain[5] = 1;
            }
            break;
        case 2: // Scatter Cloud
            chain[0] = 3; chain[1] = 1; chain[2] = 2; chain[3] = 0; chain[4] = 4; chain[5] = 5;
            break;
        case 3: // Filtered Dub
            if (mono_mode) {
                chain[0] = 4; chain[1] = 1; chain[2] = 2; chain[3] = 3; chain[4] = 0; chain[5] = 5;
            } else {
                chain[0] = 4; chain[1] = 1; chain[2] = 0; chain[3] = 2; chain[4] = 3; chain[5] = 5;
            }
            break;
        case 0:
        default: // Series Standard
            if (mono_mode) {
                chain[0] = 1; chain[1] = 2; chain[2] = 3; chain[3] = 0; chain[4] = 4; chain[5] = 5;
            } else {
                chain[0] = 0; chain[1] = 1; chain[2] = 2; chain[3] = 3; chain[4] = 4; chain[5] = 5;
            }
            break;
    }
}

static void bends_trigger_chain_vis(int routing_mode, bool mono_mode) {
    chainVisMode = routing_mode;
    chainVisMono = mono_mode;
    chainVisTimer = 1200;
}

static void bends_trigger_chain_vis_fast() {
    // Fast 300ms confirmation flash (no routing sequence)
    chainVisMode = global_routing_mode;
    chainVisMono = global_mono_mode;
    chainVisTimer = 300;
}

// ============================================================================
// Flash Persistence (last 4 KB sector)
// ============================================================================
static constexpr uint32_t BENDS_SETTINGS_FLASH_OFFSET =
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE); // top sector of 2 MB flash

static uint32_t saveFlashTimer = 0;         // ms remaining for manual save LED animation
static uint32_t factoryResetFlashTimer = 0; // ms remaining for factory reset LED animation

struct BendsSettings {
    uint32_t magic;           // 0xBE4D0004 -- version tag v4 (full 7-page preset + dual mono)
    int32_t  routing_mode;    // global_routing_mode (0-3)
    int32_t  input_width;     // global_input_width
    uint8_t  mono_mode;       // Extended Mono Mode on/off (1/0)
    uint8_t  current_page;    // active page index (0..5)
    uint8_t  dual_mono_mode;  // Dual Mono Mode on/off (1/0)
    uint8_t  _reserved[9];    // reserved space
    int32_t  vp[6][3];        // 18 x 4 = 72 bytes: 6 main page knob states
    int32_t  freeze_vp[3];   // 3 x 4 = 12 bytes: Freeze scrub page knob state
    uint8_t  _pad[18];        // padding so crc lands at offset 126
    uint16_t crc;             // simple additive checksum of first 126 bytes
};
static_assert(sizeof(BendsSettings) == 128, "BendsSettings must be 128 bytes");

static uint16_t bends_settings_checksum(const BendsSettings &s) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
    uint16_t sum = 0;
    for (int i = 0; i < 126; i++) sum += p[i];
    return sum;
}

static void bends_load_settings() {
    const BendsSettings *s = reinterpret_cast<const BendsSettings *>(
        XIP_BASE + BENDS_SETTINGS_FLASH_OFFSET);
    if (s->crc != bends_settings_checksum(*s)) return; // corrupt or blank
    if (s->magic == 0xBE4D0004u) {
        global_routing_mode   = (s->routing_mode >= 0 && s->routing_mode <= 3) ? s->routing_mode : 0;
        global_input_width    = s->input_width;
        global_mono_mode      = (s->mono_mode == 1);
        global_dual_mono_mode = (s->dual_mono_mode == 1);
        // Always start on Page 0 (Chorus) on boot so navigation is predictable & intuitive
        currentPage = 0;
        memcpy(vp, s->vp, sizeof(vp));
        memcpy(freeze_vp, s->freeze_vp, sizeof(freeze_vp));
    }
}

// Must be called from Core 0 only. Pauses Core 1 via multicore_lockout to
// prevent flash-fetch crashes during erase/program.
static void bends_save_settings() {
    static uint8_t page_buf[FLASH_PAGE_SIZE]; // 256 bytes
    memset(page_buf, 0xFF, sizeof(page_buf));
    BendsSettings *s = reinterpret_cast<BendsSettings *>(page_buf);
    s->magic          = 0xBE4D0004u;
    s->routing_mode   = global_routing_mode;
    s->input_width    = global_input_width;
    s->mono_mode      = global_mono_mode ? 1 : 0;
    s->dual_mono_mode = global_dual_mono_mode ? 1 : 0;
    s->current_page   = 0;  // Always boot to Page 0 (not restored from flash)
    memcpy(s->vp, vp, sizeof(vp));
    memcpy(s->freeze_vp, freeze_vp, sizeof(freeze_vp));
    s->crc            = bends_settings_checksum(*s);

    // Pause Core 1 so it cannot fetch flash instructions during erase/program.
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(BENDS_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(BENDS_SETTINGS_FLASH_OFFSET, page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

static void bends_reset_factory_defaults() {
    global_routing_mode   = 0; // Preset 0: Standard
    global_input_width    = 32767;
    global_mono_mode      = false;
    global_dual_mono_mode = false;
    currentPage           = 0;
    static const int32_t default_vp[6][3] = {
        {     0, 12000, 16384 },
        {     0, 10000, 12000 },
        {     0, 12000,  8000 },
        {     0, 16384, 16384 },
        { 16384,     0,     0 },
        {     0, 16384, 16384 }
    };
    memcpy(vp, default_vp, sizeof(vp));
    freeze_vp[0] = 32767;
    freeze_vp[1] = 19661;
    freeze_vp[2] = 16384;
}

static void bends_erase_settings() {
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(BENDS_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

// Visual feedback (Core 1 → Core 0)
std::atomic<uint16_t> vis_lfo_phase{0};       // Chorus LFO phase for LED glow
std::atomic<bool>     vis_frame_dropped{false}; // Codec dropout for LED flash
std::atomic<bool>     vis_delay_pulse{false};   // Delay echo repeat pulse for LED flash
std::atomic<bool>     vis_codec_glitch{false};  // Codec vinyl pop & glitch detector
std::atomic<uint16_t> vis_input_level{0};      // Real-time audio input level meter

// ============================================================================
// ComputerCard Subclass
// ============================================================================
class BendsCard : public ComputerCard {
public:
    BendsCard() {}

    void ProcessSample() override;
    void BackgroundLoop() override {}

    // UI / control loop runs on Core 0 as a member to access protected I/O
    void run_core0_ui_loop();
    void tick_ui_once();

    // Public wrappers so main() can access protected ComputerCard members
    bool                 JackDisconnected(Input i)   { return Disconnected(i); }
    int32_t              ReadKnob(Knob k)            { return KnobVal(k); }
    ComputerCard::Switch ReadSw()                    { return SwitchVal(); }
    void                 SetLed(uint32_t idx, uint16_t val) { LedBrightness(idx, val); }
};

BendsCard card;

inline int32_t scale_grit(int32_t val, int32_t max_val, int32_t macro, int32_t gain_q15 = 32768) {
    if (macro < 16384) {
        return (val * macro) >> 14;
    } else {
        int32_t target_max = max_val;
        if (gain_q15 < 32768) {
            target_max = val + (((max_val - val) * gain_q15) >> 15);
        }
        if (target_max < val) {
            target_max = val; // Never scale down when turning macro up
        }
        int32_t diff = target_max - val;
        int32_t scale_q15 = (macro - 16384) * 2;
        return val + ((diff * scale_q15) >> 15);
    }
}

inline int32_t get_staggered_macro(int32_t macro, int32_t start_x, int32_t end_x) {
    if (macro < 16384) {
        return macro; // Linear / flat decay when dialing grittiness out
    }
    int32_t x = (macro - 16384) * 2; // 0..32767 progress above center
    if (x < start_x) {
        return 16384; // Stays clean / transparent
    }
    if (x >= end_x) {
        return 32767; // Reaches max intensity
    }
    int32_t range = end_x - start_x;
    int32_t val = ((x - start_x) * 32768) / range;
    return 16384 + ((val * 16383) >> 15); // Maps back to [16384, 32767]
}

static void bake_macro_to_vp(int32_t active_macro) {
    int32_t macro_chorus = get_staggered_macro(active_macro, 12000, 30000);
    int32_t macro_codec  = get_staggered_macro(active_macro, 4000, 26000);
    int32_t macro_delay  = get_staggered_macro(active_macro, 16000, 31000);
    int32_t macro_glitch = get_staggered_macro(active_macro, 8000, 28000);
    int32_t macro_filter = get_staggered_macro(active_macro, 20000, 32767);
    int32_t macro_reverb = get_staggered_macro(active_macro, 24000, 32767);

    vp[0][2] = scale_grit(vp[0][2], 32767, macro_chorus);
    vp[1][0] = scale_grit(vp[1][0], 32767, macro_codec);
    vp[1][1] = scale_grit(vp[1][1], 32767, macro_codec);
    vp[1][2] = scale_grit(vp[1][2], 32767, macro_codec);
    vp[2][2] = scale_grit(vp[2][2], 32767, macro_delay);
    vp[3][0] = scale_grit(vp[3][0], 32767, macro_glitch);
    if (macro_filter > 16384) {
        vp[4][0] = scale_grit(vp[4][0], 32767, macro_filter); // Only bake upward
    }
    vp[5][0] = scale_grit(vp[5][0], 32767, macro_reverb);
}

// ============================================================================
// Core 1 DSP Stage Helper Wrappers (noinline to prevent duplicate RAM bloat)
// ============================================================================
__attribute__((noinline)) void __not_in_flash_func(run_chorus)(int16_t &L, int16_t &R, const volatile Core1Params &p) {
    chorus.process(L, L, R, R, p.chorus_mix, p.chorus_rate, p.chorus_depth, p.chorus_feedback, p.chorus_xor_mask, p.cv1_bipolar, p.chorus_depth_fb);
}

__attribute__((noinline)) void __not_in_flash_func(run_codec)(int16_t &L, int16_t &R, const volatile Core1Params &p, bool pulse1_live = false, bool p1_rising = false) {
    codec.process(L, L, R, R,
                  p.codec_mix,
                  p.codec_mp3_ring, p.codec_fuzz, p.codec_decimate,
                  p.codec_pop_prob, p.codec_click_depth, p.codec_bad_conn, p.codec_scramble,
                  p.codec_sputter_prob, p.codec_tape_sat, p.codec_tape_hiss, p.codec_tape_wow_flutter, p.codec_active_loss,
                  rand_seed, pulse1_live, p1_rising);
}

__attribute__((noinline)) void __not_in_flash_func(run_delay)(int16_t &L, int16_t &R, const volatile Core1Params &p, bool freeze, bool pulse1_live, uint32_t clk_period_samples) {
    delay_fx.process(L, L, R, R,
                     p.delay_mix, p.delay_time, p.delay_feedback, freeze, 0, 0, p.global_noise_scale,
                     pulse1_live, clk_period_samples, p.mono_mode, p.input_width);
}

__attribute__((noinline)) void __not_in_flash_func(run_glitcher)(int16_t &L, int16_t &R, const volatile Core1Params &p, bool freeze, bool stutter, bool is_freeze_page, int32_t cv2, int32_t scrub_offset, bool pulse1_live, bool p1_rising, bool p1_val, bool pulse2_live, bool p2_rising, bool p2_val, uint32_t clk_period_samples, uint32_t clk_timer) {
    glitcher.process(L, L, R, R,
                     freeze ? 32767 : p.glitch_mix,
                     p.glitch_size, p.glitch_speed, is_freeze_page,
                     stutter, freeze,
                     0, cv2, rand_seed,
                     scrub_offset, p.glitch_feedback, p.global_noise_scale,
                     pulse1_live, p1_rising, p1_val,
                     pulse2_live, p2_rising, p2_val,
                     clk_period_samples, clk_timer,
                     p.glitch_loop_size, p.glitch_target_offset, p.glitch_speed_q16,
                     p.mono_mode, p.dual_mono_mode);
}

__attribute__((noinline)) void __not_in_flash_func(run_filter)(int16_t &L, int16_t &R, const volatile Core1Params &p) {
    filter.process(L, L, R, R, p.filter_cutoff, p.filter_res, p.filter_morph, 0);
}

__attribute__((noinline)) void __not_in_flash_func(run_reverb)(int16_t &L, int16_t &R, const volatile Core1Params &p) {
    reverb.process(L, R, p.reverb_mix, p.reverb_size,
                   p.reverb_decay, p.reverb_damp, p.reverb_lofi_level,
                   p.reverb_sparkle_level, p.reverb_circuit_bent_level,
                   p.reverb_lofi_shift, p.reverb_lofi_frac, p.reverb_mode,
                   p.dual_mono_mode);
}

// ============================================================================
// Core 1: ProcessSample() — 24 kHz audio interrupt
// ============================================================================
// ALL DSP including reverb runs here on Core 1.
// Core 0 is a pure UI loop — no inter-core FIFO, no mutual blocking.
void __not_in_flash_func(BendsCard::ProcessSample)() {

    // --- Load all parameters in a single burst from the double buffer ---
    const uint32_t params_idx     = g_params_idx.load(std::memory_order_relaxed);
    const volatile Core1Params &p = g_params[params_idx];

    const int32_t eff_glitch_mix          = p.glitch_mix;

    const int32_t eff_input_width   = p.input_width;
    const int32_t eff_routing_mode  = p.routing_mode;

    const bool    freeze  = p.freeze;
    const bool    stutter = p.stutter;
    const int32_t cv2    = p.cv2;
    const bool    pulse1_live = p.pulse1_live;
    const bool    pulse2_live = p.pulse2_live;

    const bool no_audio2 = p.no_audio2;
    const bool is_freeze_page = p.is_freeze_page;

    // --- Sample-Accurate Edge Detection for Eurorack Pulses ---
    bool p1_val = PulseIn1();
    bool p2_val = PulseIn2();
    static bool last_p1_val = false;
    static bool last_p2_val = false;
    bool p1_rising = p1_val && !last_p1_val;
    bool p2_rising = p2_val && !last_p2_val;
    last_p1_val = p1_val;
    last_p2_val = p2_val;

    // Track clock period on Pulse 1 (Stutter Clock)
    static uint32_t clk_timer = 0;
    static uint32_t clk_period_samples = 0;
    static uint32_t clk_lockout = 0;
    clk_timer++;

    // Timeout Check: if no clock pulse received for 10 seconds (240,000 samples @ 24kHz)
    // or if the cable is unplugged, disable clock sync.
    static uint32_t last_mult = 1;
    static uint8_t din_sync_stability = 0;
    static bool din_sync_locked = false;
    static uint8_t din_unlock_ctr = 0;
    if (clk_timer > 240000 || !pulse1_live) {
        clk_period_samples = 0;
        g_clk_period_samples = 0;
        clk_lockout = 0;
        last_mult = 1;
        din_sync_stability = 0;
        din_sync_locked = false;
        din_unlock_ctr = 0;
    }

    if (pulse1_live && p1_rising) {
        if (clk_timer > 120 && (clk_timer > clk_lockout ||
            (clk_period_samples > 0 && clk_timer > (clk_period_samples >> 1)))) {
            clk_period_samples = clk_timer;
            // Dynamic lockout at 20% of period to accept swung clocks (up to 80/20 swing)
            clk_lockout = (clk_period_samples * 20) / 100;
            if (clk_lockout < 120) clk_lockout = 120;

            // Multi-Standard Clock Auto-Detection (1 PPQN, 2 PPQN, 4 PPQN, 24 PPQN DIN Sync)
            // Measures raw pulse interval and normalises to 1 PPQN (quarter note).
            // Uses hysteresis + stability counter for robust DIN sync lock.
            uint32_t eff_clk_period = clk_period_samples;
            uint32_t mult = 1;

            if (din_sync_locked) {
                // Already locked to 24 PPQN — stay locked unless 3 consecutive out-of-range pulses
                if (eff_clk_period < 3000) {
                    mult = 24;
                    din_unlock_ctr = 0;
                } else {
                    din_unlock_ctr++;
                    if (din_unlock_ctr >= 3) {
                        din_sync_locked = false;
                        din_sync_stability = 0;
                        din_unlock_ctr = 0;
                    } else {
                        mult = 24; // hold lock during brief glitches
                    }
                }
            } else {
                // Threshold detection with hysteresis for each PPQN standard
                if (last_mult == 24) {
                    mult = (eff_clk_period < 3000) ? 24 : ((eff_clk_period < 4400) ? 4 : ((eff_clk_period < 8600) ? 2 : 1));
                } else if (last_mult == 4) {
                    mult = (eff_clk_period < 2400) ? 24 : ((eff_clk_period < 4400) ? 4 : ((eff_clk_period < 8600) ? 2 : 1));
                } else if (last_mult == 2) {
                    mult = (eff_clk_period < 2400) ? 24 : ((eff_clk_period < 3600) ? 4 : ((eff_clk_period < 8600) ? 2 : 1));
                } else {
                    mult = (eff_clk_period < 2400) ? 24 : ((eff_clk_period < 3600) ? 4 : ((eff_clk_period < 7400) ? 2 : 1));
                }

                // Build stability counter for DIN sync lock
                if (mult == 24) {
                    din_sync_stability++;
                    if (din_sync_stability >= 8) {
                        din_sync_locked = true;
                    }
                } else {
                    din_sync_stability = 0;
                }
            }
            last_mult = mult;
            eff_clk_period *= mult;

            // Clamped to valid 1 PPQN quarter-note period range (4000..144000 samples = 10..360 BPM)
            eff_clk_period = clamp_i32(eff_clk_period, 4000, 144000);

            // Rock-solid clock stabilization: ignore hardware pulse jitter (< 1.5%) to prevent delay pitch wobble
            if (g_clk_period_samples == 0) {
                g_clk_period_samples = eff_clk_period;
            } else {
                int32_t delta = (int32_t)eff_clk_period - (int32_t)g_clk_period_samples;
                if (delta < 0) delta = -delta;
                int32_t deadband = g_clk_period_samples >> 6; // 1.5% deadband (~270 samples @ 80 BPM)
                if (delta > deadband) {
                    int32_t threshold = g_clk_period_samples >> 3; // 12.5% threshold for tempo jumps
                    if (delta > threshold) {
                        g_clk_period_samples = eff_clk_period;
                    } else {
                        g_clk_period_samples = (g_clk_period_samples * 7 + eff_clk_period) / 8;
                    }
                }
            }
            clk_timer = 0;
        }
    }

    // --- Generative Microsound Engine (always active in the background) ---
    // (clicks, digital sweeps, squeals, vinyl crackle, geiger pops)
    // Level is controlled by Page 4 Y knob (filter_morph).
    // Bleep density, pitch, and type are controlled by Page 1 (Codec) knobs.
    static uint32_t ms_event_timer = 0;
    static int32_t  ms_env = 0;          // Q15 envelope
    static int32_t  ms_decay = 0;        // Q15 decay factor
    static uint32_t ms_phase = 0;        // phase accumulator
    static int32_t  ms_freq = 0;         // phase delta
    static int32_t  ms_pitch_sweep = 0;  // pitch sweep delta
    static int32_t  ms_type = 0;         // 0 = click, 1 = chirp, 2 = squeal, 3 = noise pop

    if (pulse1_live && p1_rising) {
        uint32_t ms_prob = (p.codec_mix > 500) ? (uint32_t)p.codec_mix : 8000;
        if (((fast_rand(rand_seed) & 0x7FFF) < ms_prob) && ms_event_timer > 500) {
            ms_event_timer = 0; // Align microsound trigger to clock pulse
        }
    }

    if (ms_event_timer > 0) {
        ms_event_timer--;
    } else {
        // 1. Codec Y Knob (Temporal Instability / Pops) scales pop rate & Geiger density:
        // Low Y = sparse relaxed pops (1.2s interval); High Y = dense geiger crackle & rapid microglitches (50ms interval)
        int32_t ms_rate_param = p.codec_scramble > p.codec_pop_prob ? p.codec_scramble : p.codec_pop_prob;
        int32_t max_interval = 28800 - ((ms_rate_param * 27600) >> 15);
        if (max_interval < 1200) max_interval = 1200;

        uint32_t r = fast_rand(rand_seed);
        ms_event_timer = 480 + (((r & 0xFFFF) * (max_interval - 480)) >> 16);
        
        // 2. Codec X Knob (Demolition Character / Noise) controls sound type & noise vs tone balance:
        // Low X = deep sub-clicks, Mid X = tonal sine chirps/squeals, High X = noise pops & digital hash noise
        int32_t x_param = p.codec_downsample;
        if (x_param < 10000) {
            ms_type = (r & 1) ? 0 : 1; // Clicks & low blups
        } else if (x_param < 22000) {
            ms_type = (r & 1) ? 1 : 2; // Tonal chirps & high squeals
        } else {
            ms_type = (r & 1) ? 3 : 2; // Noise pops & digital noise
        }

        ms_env = 32767;
        ms_phase = 0;

        // 3. Codec X Knob scales chirp pitch/frequency, decay time, and sweep speed:
        int32_t pitch_offset = (x_param * 100000000) >> 15;

        if (ms_type == 0) {
            ms_decay = 0; // single sample click
        } else if (ms_type == 1) {
            // Digital chirp: rapid pitch sweep down.
            ms_decay = 31200 - ((x_param * 4000) >> 15);
            ms_freq = 40000000 + pitch_offset + (int32_t)(fast_rand(rand_seed) & 0x0FFFFFFF);
            ms_pitch_sweep = -120000 - ((x_param * 200000) >> 15);
        } else if (ms_type == 2) {
            // High squeal: slow pitch slide up.
            ms_decay = 32500 - ((x_param * 6000) >> 15);
            ms_freq = 90000000 + pitch_offset + (int32_t)(fast_rand(rand_seed) & 0x0FFFFFFF);
            ms_pitch_sweep = 15000 + ((x_param * 30000) >> 15);
        } else {
            // Short noise pop / digital hash.
            ms_decay = 29000 - ((x_param * 5000) >> 15);
        }
    }

    // Calculate event signal
    int32_t event_sig = 0;
    if (ms_env > 0) {
        if (ms_type == 0) {
            event_sig = ms_env;
            ms_env = 0;
        } else if (ms_type == 1 || ms_type == 2) {
            ms_phase += ms_freq;
            ms_freq += ms_pitch_sweep;
            if (ms_freq < 1000000) ms_freq = 1000000;

            uint16_t lookup_idx = (uint16_t)(ms_phase >> 16);
            int16_t sine_val = lookup_sine(lookup_idx);
            
            // Shred active parameter (scramble) applies digital XOR ring modulation to the bleeps,
            // morphing clean analog sine chirps into robotic, bit-reduced digital "blups".
            if (p.codec_scramble > 0) {
                int16_t xor_val = (p.codec_scramble >> 9); // 0..63
                sine_val ^= (xor_val << 6);
            }

            event_sig = (sine_val * ms_env) >> 15;

            ms_env = (ms_env * ms_decay) >> 15;
            if (ms_env < 100) ms_env = 0;
        } else if (ms_type == 3) {
            int32_t noise = ((int32_t)(fast_rand(rand_seed) & 0xFFFF)) - 32768;
            event_sig = (noise * ms_env) >> 15;

            ms_env = (ms_env * ms_decay) >> 15;
            if (ms_env < 100) ms_env = 0;
        }
    }

    // Calculate continuous background surface crackles (dust/EMI)
    // Scales dynamically with active Vinyl pop_prob and Shred scramble_level
    int32_t active_noise = (p.codec_pop_prob * 2730) + p.codec_scramble;
    if (active_noise > 32767) active_noise = 32767;

    int32_t crackle_mask = 0xFFF - ((active_noise * 3840) >> 15);
    if (crackle_mask < 255) crackle_mask = 255;

    int32_t surface_crackle = 0;
    uint32_t crackle_roll = fast_rand(rand_seed);
    if ((crackle_roll & crackle_mask) == 0) {
        surface_crackle = (((int32_t)(crackle_roll & 0xFF)) - 128) * 8; // [-1024, 1024]
    }

    int32_t rawL = event_sig + surface_crackle;
    int32_t rawR = (ms_type == 3) ? -rawL : (event_sig + surface_crackle);

    // --- Read Audio Inputs & Attenuate for Headroom ---
    // AudioIn() returns ±2048 (12-bit). Net << 3 → ±16384 in Q15 (50% FS, 6dB headroom).
    int16_t L = (int16_t)((AudioIn1() << 4) >> 1);
    int16_t R = no_audio2 ? L : (int16_t)((AudioIn2() << 4) >> 1);

    // If both input jacks are unplugged, only route microsound generator if Codec/Destruction mix is active
    if (p.no_audio1 && p.no_audio2) {
        int32_t active_codec = p.codec_mix;
        if (active_codec > 500) {
            int32_t gain_level = (active_codec * 16384) >> 15;
            L = (int16_t)((rawL * gain_level) >> 15);
            R = (int16_t)((rawR * gain_level) >> 15);
        } else {
            L = 0;
            R = 0;
        }
    }

    // --- Anti-Aliasing Lowpass (1-pole, coef=30500 → -3dB @ ~8.5 kHz @ 24 kHz) ---
    lp_aa_L += (((int32_t)L - lp_aa_L) * 30500) >> 15;
    L = (int16_t)lp_aa_L;
    lp_aa_R += (((int32_t)R - lp_aa_R) * 30500) >> 15;
    R = (int16_t)lp_aa_R;

    // --- Apply Input DC Blockers ---
    L = dc_inL.process(L);
    R = dc_inR.process(R);

    // Apply Noise Gate & Downward Expander when audio cables are plugged in
    if (!(p.no_audio1 && p.no_audio2)) {
        // Full-band sidechain with gentle LPF (~6kHz) to accurately track all musical pitches & harmonics
        static int32_t gate_lpL = 0;
        static int32_t gate_lpR = 0;
        gate_lpL += (((int32_t)L - gate_lpL) * 26000) >> 15;
        gate_lpR += (((int32_t)R - gate_lpR) * 26000) >> 15;

        int32_t absL = gate_lpL < 0 ? -gate_lpL : gate_lpL;
        int32_t absR = gate_lpR < 0 ? -gate_lpR : gate_lpR;
        int32_t input_level = absL > absR ? absL : absR;
        
        static int32_t gate_env = 0;
        if (input_level > gate_env) {
            // Instant attack: catch transients immediately on sample 1 so note heads are never chopped
            gate_env = input_level;
        } else {
            // Smooth decay (~300ms) to preserve natural note decay tails
            gate_env += (input_level - gate_env) >> 10;
        }
        vis_input_level.store((uint16_t)gate_env, std::memory_order_relaxed);
        
        // Hysteresis Noise Gate & Downward Expander:
        //   Threshold: 350 DAC counts (-40 dB).
        //   When input_level > 350: gate opens instantly (32768)
        //   When input_level < 200: expander attenuates idle cable hiss to zero
        static bool gate_open = false;
        if (gate_env > 350) {
            gate_open = true;
        } else if (gate_env < 200) {
            gate_open = false;
        }

        int32_t target_gain = 0;
        if (gate_open) {
            target_gain = 32768;
        } else if (gate_env > 50) {
            target_gain = ((gate_env - 50) * 16384) / 150;
        } else {
            target_gain = 0;
        }
        
        static int32_t gate_gain = 0;
        gate_gain += (target_gain - gate_gain) >> 6; // ~3ms click-free smooth fade
        
        L = (int16_t)(((int32_t)L * gate_gain) >> 15);
        R = (int16_t)(((int32_t)R * gate_gain) >> 15);
    }

    // --- Apply Global Stereo Width Control (Switch Down hold tweaks) ---
    {
        // Width cross-mixing (0 = mono sum, 32767 = full hard panned stereo)
        int32_t mono_sum = ((int32_t)L + (int32_t)R) >> 1;
        L = (int16_t)(mono_sum + (((L - mono_sum) * eff_input_width) >> 15));
        R = (int16_t)(mono_sum + (((R - mono_sum) * eff_input_width) >> 15));
    }

    // --- Dynamic Transient Softener for Hot/Clipping Inputs ---
    {
        int32_t absL = L < 0 ? -L : L;
        int32_t coefL = 32767;
        if (absL >= 12288) {
            int32_t overshoot = absL - 12288;
            coefL = 32767 - (overshoot * 4);
        }
        lp_inL += (((int32_t)L - lp_inL) * coefL) >> 15;
        L = (int16_t)lp_inL;

        int32_t absR = R < 0 ? -R : R;
        int32_t coefR = 32767;
        if (absR >= 12288) {
            int32_t overshoot = absR - 12288;
            coefR = 32767 - (overshoot * 4);
        }
        lp_inR += (((int32_t)R - lp_inR) * coefR) >> 15;
        R = (int16_t)lp_inR;
    }

    // scrub_offset: seek position within the frozen loop (MAIN knob on Page 6)
    int32_t scrub_offset = is_freeze_page ? eff_glitch_mix : 0;

    // --- Selectable DSP Routing Matrix ---
    // In Extended Mono Mode (mono_mode), Chorus is moved after Glitcher in all
    // presets so mono delay/stutter tails are spatialised into wide stereo before Reverb.
    const bool mono_mode = p.mono_mode;
    switch (eff_routing_mode) {
        case 1: // Space Wash
            run_reverb(L, R, p);
            run_filter(L, R, p);
            if (!mono_mode) run_chorus(L, R, p);
            run_delay(L, R, p, freeze, pulse1_live, clk_period_samples);
            run_glitcher(L, R, p, freeze, stutter, is_freeze_page, cv2, scrub_offset, pulse1_live, p1_rising, p1_val, pulse2_live, p2_rising, p2_val, g_clk_period_samples, clk_timer);
            if (mono_mode) run_chorus(L, R, p);
            run_codec(L, R, p, pulse1_live, p1_rising);
            break;

        case 2: // Scatter Cloud (Chorus already post-delay in this preset)
            run_glitcher(L, R, p, freeze, stutter, is_freeze_page, cv2, scrub_offset, pulse1_live, p1_rising, p1_val, pulse2_live, p2_rising, p2_val, g_clk_period_samples, clk_timer);
            run_codec(L, R, p, pulse1_live, p1_rising);
            // Delay follows glitcher: don't freeze delay write pointer so it keeps recording
            // the frozen glitch output -- passing freeze=true would stale the delay buffer.
            run_delay(L, R, p, false, pulse1_live, clk_period_samples);
            run_chorus(L, R, p);
            run_filter(L, R, p);
            run_reverb(L, R, p);
            break;

        case 3: // Filtered Dub
            run_filter(L, R, p);
            run_codec(L, R, p, pulse1_live, p1_rising);
            if (!mono_mode) run_chorus(L, R, p);
            run_delay(L, R, p, freeze, pulse1_live, clk_period_samples);
            run_glitcher(L, R, p, freeze, stutter, is_freeze_page, cv2, scrub_offset, pulse1_live, p1_rising, p1_val, pulse2_live, p2_rising, p2_val, g_clk_period_samples, clk_timer);
            if (mono_mode) run_chorus(L, R, p);
            run_reverb(L, R, p);
            break;

        case 0:
        default: // Series Standard
            if (!mono_mode) run_chorus(L, R, p);
            run_codec(L, R, p, pulse1_live, p1_rising);
            run_delay(L, R, p, freeze, pulse1_live, clk_period_samples);
            run_glitcher(L, R, p, freeze, stutter, is_freeze_page, cv2, scrub_offset, pulse1_live, p1_rising, p1_val, pulse2_live, p2_rising, p2_val, g_clk_period_samples, clk_timer);
            if (mono_mode) run_chorus(L, R, p);
            run_filter(L, R, p);
            run_reverb(L, R, p);
            break;
    }

    // Capture the loop trigger before it's cleared by the Pulse 1 generator
    bool loop_triggered = glitcher.trig_out1;

    // Calculate envelope follower once for use in both CV and Pulse 2 outputs
    int32_t env_in = (L < 0 ? -L : L) + (R < 0 ? -R : R);
    static int32_t env_followed = 0;
    if (env_in > env_followed) {
        env_followed += ((env_in - env_followed) * 8192) >> 15;
    } else {
        env_followed += ((env_in - env_followed) * 256) >> 15;
    }

    // --- CV Out 1 & 2: Semi-Random Matched Harmonic Voltages ---
    static int16_t last_cv1 = 0;
    static int16_t last_cv2 = 0;
    static uint32_t slow_clock_ctr = 0;
    
    // Check if we need to update our stepped CV values:
    bool trigger_cv_update = false;
    bool clock_synced = (g_clk_period_samples > 0);
    
    if (clock_synced) {
        // Clock-synced: advance step on rising edge of Pulse In 1
        trigger_cv_update = p1_rising;
    } else {
        // Unclocked: advance step on loop boundaries OR internal clock timer (~500ms @ 24 kHz)
        slow_clock_ctr++;
        if (slow_clock_ctr >= 12000) { // 500ms interval (120 BPM step tempo)
            slow_clock_ctr = 0;
            trigger_cv_update = true;
        }
        if (loop_triggered) {
            trigger_cv_update = true;
        }
    }
    
    if (trigger_cv_update) {
        // Universal Harmonically Weighted Semitone Options (15 notes):
        // Index 0..4: Consonant / Tonal (4th down, unison, 3rd, 5th, octave) -> {-5, 0, 4, 7, 12}
        // Index 5..9: Warm / Extended (5th down, minor 3rd down, 2nd, 4th, 6th) -> {-7, -3, 2, 5, 9}
        // Index 10..14: Tense / Full 2-Octave Chromatic (octave down, 6th down, 7th, tritone) -> {-12, -9, -1, 6, 11}
        static const int16_t semitone_table[15] = {
            -5,  0, 4, 7, 12,
            -7, -3, 2, 5,  9,
           -12, -9,-1, 6, 11
        };

        // Glitcher Page Y Knob (vp[3][2]) controls Sequencer mutation rate & scale degree palette:
        int32_t seq_knob = vp[3][2];

        int32_t max_choice = 5;
        if (seq_knob > 10000) max_choice = 10;
        if (seq_knob > 22000) max_choice = 15;

        // Generative 8-step pattern buffer (Turing Machine style):
        static int32_t seq_pattern[8] = {0, 2, 4, 1, 3, 0, 4, 2};
        static uint8_t pat_idx = 0;
        
        pat_idx = (pat_idx + 1) & 7;

        // Low Glitcher Y Knob (< 4000): 0% mutation -> Pattern is LOCKED into a fixed 8-note repeating riff!
        // Turning Glitcher Y Knob up (> 4000): Mutation rate scales smoothly (0% .. 40%), generating evolving melodies!
        int32_t mutate_rate = 0;
        if (seq_knob > 4000) {
            mutate_rate = ((seq_knob - 4000) * 13107) / 28767;
        }

        if (mutate_rate > 0 && (fast_rand(rand_seed) & 0x7FFF) < mutate_rate) {
            int32_t delta = ((fast_rand(rand_seed) & 1) == 0) ? 1 : -1;
            int32_t new_val = seq_pattern[pat_idx] + delta;
            if (new_val < 0) new_val = max_choice - 1;
            if (new_val >= max_choice) new_val = 0;
            seq_pattern[pat_idx] = new_val;
        }

        int32_t choice = seq_pattern[pat_idx] % max_choice;
        int16_t lead_semi = semitone_table[choice];

        // Convert semitones to DAC counts (Eurorack 1V/Oct standard: 68 DAC counts per semitone)
        last_cv1 = lead_semi * 68;

        // CV Out 2: Stepped Random Voltage Generator (Sample & Hold)
        // Outputs random 0V to +3.0V stepped CV modulation on every clock / sequencer step!
        last_cv2 = (int16_t)(((fast_rand(rand_seed) & 0x7FFF) * 2048) >> 15);
    }
    
    CVOut1(last_cv1);
    CVOut2(last_cv2);

    // --- Pulse Out 1: Sequencer Step & Loop Sync Pulses ---
    static int16_t p1_trig_timer = 0;
    if (trigger_cv_update || glitcher.trig_out1) {
        glitcher.trig_out1 = false;
        p1_trig_timer = 48; // 2 ms pulse
    }
    
    if (p1_trig_timer > 0) {
        p1_trig_timer--;
        PulseOut1(true);
    } else {
        PulseOut1(false);
    }



    // --- Output ---
    // Output microsound generator on Pulse Out 2 as PWM audio
    // rawL contains the combined event_sig + surface_crackle from the microsound engine
    // Scale to 10-bit PWM range. Hardware buffer inverts, so use (1023 - level).
    {
        int32_t pwm_sig = rawL;
        if (pwm_sig > 16384) pwm_sig = 16384;
        if (pwm_sig < -16384) pwm_sig = -16384;
        uint16_t pwm_val = 1023 - (uint16_t)((pwm_sig + 16384) >> 5);
        pwm_set_gpio_level(PULSE_2_RAW_OUT, pwm_val);
    }
    // Unity gain: input << 3 and output >> 4 cancel exactly (±16384 Q15 → ±1024 DAC ≡ ±3V).
    // A ±6V Eurorack signal (ADC ±2048 → Q15 ±16384) comes out at the same ±6V.
    // The previous << 1 caused a spurious +6dB, making Bends louder than peer modules.
    AudioOut1(soft_limit_q15((int32_t)L) >> 4);
    AudioOut2(soft_limit_q15((int32_t)R) >> 4);
}

// Core 1 entry point
void core1_entry() {
    // Register this core as a lockout victim so Core 0 can safely pause us
    // during flash erase/program operations (settings save).
    multicore_lockout_victim_init();

    // Configure hardware interpolators for Core 1's audio ISR:
    //   INTERP0 = blend mode  -> lerp_delay_q15() (1-cycle delay interpolation)
    //   INTERP1 = clamp mode  -> saturate_q15()   (branch-free Q15 saturation)
    init_hardware_interp();
    card.Run();
}



// KnobLock instances — one per physical knob
static KnobLock lockMain, lockX, lockY, lockMacro;

// Smoothed knob values (IIR-filtered raw ADC readings)
static int32_t smMain = 0, smX = 0, smY = 0;

// Last known switch state (for debounce edge detection)
static bool freeze_latched = false;
static bool is_frozen       = false;



static void push_params_for_idx(
    uint32_t idx,
    bool cv1_live = false, int32_t raw_cv1_val = 0,
    bool cv2_live = false, int32_t raw_cv2_val = 0,
    bool pulse1_live = false, bool pulse1_val = false,
    bool pulse2_live = false,
    bool debounced_no_audio1 = true, bool debounced_no_audio2 = true,
    ComputerCard::Switch debounced_sw = ComputerCard::Switch::Middle,
    uint32_t active_sw_held_ms = 0
) {
    volatile Core1Params &p = g_params[idx];
    
    g_macro_active = (debounced_sw == ComputerCard::Switch::Down && active_sw_held_ms >= 350);
    int32_t base_macro = g_macro_active ? grittiness_macro : 16384;

    // CV1 no longer modulates macro globally (dedicated to digital loss engine)
    int32_t cv1_val = cv1_live ? raw_cv1_val : 0;
    int32_t cv2_val = cv2_live ? raw_cv2_val : 0;

    // Scale CV inputs so that the 4 Voltages button presses (max ~1.62V for CV1, ~1.82V for CV2 on Volt3/4)
    // can sweep the full range, while normal CV still functions correctly (clamping at the limits).
    if (cv1_live) {
        // Capped at 1000 (approx 48% of full scale) so button-triggered degradation is tasteful and not too strong
        cv1_val = (cv1_val * 1000) / 553;
        if (cv1_val > 1000) cv1_val = 1000;
        if (cv1_val < -1000) cv1_val = -1000;
    }
    if (cv2_live) {
        cv2_val = (cv2_val * 2048) / 621;
        if (cv2_val > 2048) cv2_val = 2048;
        if (cv2_val < -2048) cv2_val = -2048;
    }

    int32_t cv1_abs = cv1_val < 0 ? -cv1_val : cv1_val;
    int32_t cv2_abs = cv2_val < 0 ? -cv2_val : cv2_val;
    int32_t active_macro = base_macro;

    // Precompute staggered macros for the grittiness sweep
    int32_t macro_chorus = get_staggered_macro(active_macro, 12000, 30000);
    int32_t macro_codec  = get_staggered_macro(active_macro, 4000, 26000);
    int32_t macro_delay  = get_staggered_macro(active_macro, 16000, 31000);
    int32_t macro_glitch = get_staggered_macro(active_macro, 8000, 28000);
    int32_t macro_filter = get_staggered_macro(active_macro, 20000, 32767);
    int32_t macro_reverb = get_staggered_macro(active_macro, 24000, 32767);

    // Precompute global noise scale first (tied to reverb/lofi entrance)
    int32_t noise_scale = 16384;
    int32_t rev_damping = vp[5][2];
    if (rev_damping > 16384) {
        int32_t diff = rev_damping - 16384;
        noise_scale = 16384 + (diff << 1);
    }
    int32_t globalNoiseScale = scale_grit(noise_scale, 49152, macro_reverb);
    p.global_noise_scale = globalNoiseScale;

    bool chorus_karplus_active = (vp[0][2] >= 22500);
    bool delay_karplus_active  = (vp[2][1] < 3000 && vp[2][2] >= 24000);
    bool filter_note_active    = (vp[4][1] >= 24000);
    bool is_note_gen_mode      = chorus_karplus_active || delay_karplus_active || filter_note_active;

    p.chorus_mix       = scale_grit(apply_deadzone(vp[0][0]), 32767, macro_chorus); // Macro scales mix below center for full clean
    int32_t eff_chorus_rate = vp[0][1];
    if (chorus_karplus_active && cv1_live && cv1_val != 0) {
        eff_chorus_rate = clamp_i32(eff_chorus_rate + (cv1_val * 4), 0, 32767);
    }
    p.chorus_rate      = eff_chorus_rate;
    p.chorus_depth_fb  = vp[0][2];
    p.karplus_active   = chorus_karplus_active;

    // Pre-decode Chorus depth, feedback, and destroy
    int32_t raw_depth_fb = vp[0][2];
    int32_t chorus_depth = 0;
    int32_t chorus_feedback = 0;
    int32_t chorus_xor_mask = 0;
    if (raw_depth_fb < 16384) {
        chorus_depth = raw_depth_fb * 2;
        chorus_feedback = 0;
    } else {
        chorus_depth = 32767;
        int32_t base_fb = 0;
        int32_t base_xor = 0;
        if (raw_depth_fb < 22500) {
            base_fb = ((raw_depth_fb - 16384) * 26000) / (22500 - 16384);
        } else if (raw_depth_fb < 31500) {
            base_fb = 26000 + (((raw_depth_fb - 22500) * 6700) / (31500 - 22500));
            base_xor = 0;
        } else {
            base_fb = 32700 + (((raw_depth_fb - 31500) * 67) / (32767 - 31500));
            base_xor = ((raw_depth_fb - 31500) * 8) / (32767 - 31500);
        }
        if (active_macro < 16384) {
            chorus_feedback = (base_fb * active_macro) >> 14;
            chorus_xor_mask = (base_xor * active_macro) >> 14;
        } else {
            int32_t diff_fb = 32767 - base_fb;
            int32_t diff_xor = 15 - base_xor;
            int32_t scale_q15 = (macro_chorus - 16384) * 2;
            chorus_feedback = base_fb + ((diff_fb * scale_q15) >> 15);
            chorus_xor_mask = base_xor + ((diff_xor * scale_q15) >> 15);
        }
    }
    p.chorus_depth = chorus_depth;
    p.chorus_feedback = chorus_feedback;
    p.chorus_xor_mask = chorus_xor_mask;

    // Pre-calculate Codec levels
    int32_t raw_strength = scale_grit(apply_deadzone(vp[1][0]), 32767, macro_codec);
    int32_t raw_downsample = scale_grit(vp[1][1], 32767, macro_codec);
    int32_t raw_ringing_xor = scale_grit(vp[1][2], 32767, macro_codec);
    
    p.codec_mix = raw_strength;
    p.codec_downsample = raw_downsample;
    p.codec_ringing_xor = raw_ringing_xor;

    int32_t X = raw_downsample;
    int32_t Y = raw_ringing_xor;
    Y = clamp_i32(Y, 0, 32767);

    int32_t mp3_ring_level = 0;
    int32_t fuzz_level = 0;
    int32_t decimate_level = 0;

    // X Knob: 4-Zone Demolition Character Morph (0 to 32767)
    // Zone 0 (0..5000): Tape Drive & Warmth Zone (tape_sat 0..32767 & tape_hiss 0..500)
    // Zone 1 (5000..14000): MP3 Lossy Subband Ringing
    // Zone 2 (14000..23000): Lossy Bitcrush Fuzz
    // Zone 3 (23000..32767): Bit-Shred Decimation
    int32_t tape_sat = 0;
    int32_t tape_hiss = 0;

    if (X < 5000) {
        tape_sat = (X * 32767) / 5000;
        // Tape Hiss exists ONLY in Tape Zone (0..5000): ramps up 0..2500 then fades quickly to 0 at 5000
        if (X < 2500) {
            tape_hiss = (X * 450) / 2500;
        } else {
            tape_hiss = 450 - (((X - 2500) * 450) / 2500);
        }
        mp3_ring_level = 0;
        fuzz_level = 0;
        decimate_level = 0;
    } else if (X < 14000) {
        int32_t t = ((X - 5000) * 32767) / 9000;
        tape_sat = 32767;
        tape_hiss = 0;
        mp3_ring_level = (t * 32767) >> 15;
        fuzz_level = 0;
        decimate_level = 0;
    } else if (X < 23000) {
        int32_t t = ((X - 14000) * 32767) / 9000;
        tape_sat = 32767;
        tape_hiss = 0;
        mp3_ring_level = 32767 - ((t * 32767) >> 15);
        fuzz_level = (t * 32767) >> 15;
        decimate_level = 0;
    } else {
        int32_t t = ((X - 23000) * 32767) / 9767;
        tape_sat = 32767;
        tape_hiss = 0;
        mp3_ring_level = 0;
        fuzz_level = 32767 - ((t * 20000) >> 15);
        decimate_level = (t * 32767) >> 15;
    }

    // Y Knob: Temporal Artifacts & Instability (0 to 32767)
    // Zone 1 (0..10922): Pitch Wow & Flutter Warble & Vinyl Crackle Pops
    // Zone 2 (10922..21845): Bad Connection & Packet Drops
    // Zone 3 (21845..32767): CD Sputter & Digital Scramble
    int32_t tape_wow_flutter = 0;
    int32_t pop_prob = 0;
    int32_t bad_conn_level = 0;
    int32_t sputter_prob = 0;
    int32_t scramble_level = 0;

    if (Y < 10922) {
        tape_wow_flutter = (Y * 32767) / 10922;
        pop_prob = (Y * 40) / 10922; // Authentic vinyl pops
    } else if (Y < 21845) {
        int32_t rel_y = Y - 10922;
        tape_wow_flutter = 32767 - ((rel_y * 16384) / 10923);
        bad_conn_level = (rel_y * 32767) / 10923;
        pop_prob = 40 - ((rel_y * 20) / 10923);
    } else {
        int32_t rel_y = Y - 21845;
        tape_wow_flutter = 16383;
        bad_conn_level = 32767;
        sputter_prob = (rel_y * 50) / 10922;
        scramble_level = (rel_y * 32767) / 10922;
        if (scramble_level > 32767) scramble_level = 32767;
    }

    // Scale pop_prob by X quality
    int32_t x_scale_q15 = 8000 + ((X * 24767) >> 15);
    pop_prob = (pop_prob * x_scale_q15) >> 15;

    // Fuzz level quadratic warp
    fuzz_level = (fuzz_level * fuzz_level) >> 15;

    // Scale by Main knob (strength)
    fuzz_level = (fuzz_level * raw_strength) >> 15;
    decimate_level = (decimate_level * raw_strength) >> 15;
    mp3_ring_level = (mp3_ring_level * raw_strength) >> 15;

    // Global Noise Scale modifier
    fuzz_level = (fuzz_level * globalNoiseScale) >> 14;
    decimate_level = (decimate_level * globalNoiseScale) >> 14;
    mp3_ring_level = (mp3_ring_level * globalNoiseScale) >> 14;
    bad_conn_level = (bad_conn_level * globalNoiseScale) >> 14;
    scramble_level = (scramble_level * globalNoiseScale) >> 14;
    pop_prob = (pop_prob * globalNoiseScale) >> 14;

    if (fuzz_level > 32767) fuzz_level = 32767;
    if (decimate_level > 32767) decimate_level = 32767;
    if (mp3_ring_level > 32767) mp3_ring_level = 32767;
    if (bad_conn_level > 32767) bad_conn_level = 32767;
    if (scramble_level > 32767) scramble_level = 32767;

    int32_t click_ratio = 0;
    if (Y < 8000) {
        click_ratio = 4000 + ((Y * 12000) / 8000); // 4000 to 16000 in Zone 1
    } else if (Y >= 8000 && Y < 16000) {
        click_ratio = 16000 + (((Y - 8000) * 8000) / 8000); // 16000 to 24000 in Zone 2
    } else {
        click_ratio = 24000;
    }
    int32_t click_depth = (click_ratio * raw_strength) >> 15;
    click_depth = (click_depth * x_scale_q15) >> 15;
    click_depth = (click_depth * 16000) >> 15;

    sputter_prob = (sputter_prob * raw_strength) >> 15;
    sputter_prob = (sputter_prob * globalNoiseScale) >> 14;

    // CV1 global circuit bending injection (active when NOT in pitch/note generation mode):
    if (!is_note_gen_mode && cv1_live && cv1_abs > 100) {
        p.codec_mix = clamp_i32(p.codec_mix + (cv1_abs * 16), 0, 32767);
        int32_t cv1_x_mod = (cv1_abs * 16);
        X = clamp_i32(X + cv1_x_mod, 0, 32767);

        if (X < 16384) {
            fuzz_level = X * 2;
            decimate_level = 0;
        } else {
            decimate_level = (X - 16384) * 2;
            if (decimate_level > 32767) decimate_level = 32767;
            fuzz_level = 32767 - decimate_level;
        }

        fuzz_level = (fuzz_level * raw_strength) >> 15;
        decimate_level = (decimate_level * raw_strength) >> 15;
        fuzz_level = (fuzz_level * globalNoiseScale) >> 14;
        decimate_level = (decimate_level * globalNoiseScale) >> 14;
        mp3_ring_level = (mp3_ring_level * globalNoiseScale) >> 14;
        
        if (fuzz_level > 32767) fuzz_level = 32767;
        if (decimate_level > 32767) decimate_level = 32767;
        if (mp3_ring_level > 32767) mp3_ring_level = 32767;

        pop_prob = clamp_i32(pop_prob + (cv1_abs * 45) / 2048, 0, 50);
        int32_t pop_cv1_depth = (cv1_abs * 16000) >> 11;
        if (click_depth < pop_cv1_depth) click_depth = pop_cv1_depth;

        if (cv1_abs > 500) {
            int32_t sputter_mod = ((cv1_abs - 500) * 80) / 1548;
            sputter_prob = clamp_i32(sputter_prob + sputter_mod, 0, 80);
        }

        if (cv1_abs > 1200) {
            int32_t scramble_mod = ((cv1_abs - 1200) * 32767) / 848;
            scramble_level = clamp_i32(scramble_level + scramble_mod, 0, 32767);
        }
    }

    int32_t active_loss = bad_conn_level > scramble_level ? bad_conn_level : scramble_level;

    p.codec_mp3_ring = mp3_ring_level;
    p.codec_fuzz = fuzz_level;
    p.codec_decimate = decimate_level;
    p.codec_pop_prob = pop_prob;
    p.codec_click_depth = click_depth;
    p.codec_bad_conn = bad_conn_level;
    p.codec_scramble = scramble_level;
    p.codec_sputter_prob = sputter_prob;
    p.codec_tape_sat = tape_sat;
    p.codec_tape_hiss = tape_hiss;
    p.codec_tape_wow_flutter = tape_wow_flutter;
    p.codec_active_loss = active_loss;

    p.delay_mix      = scale_grit(apply_deadzone(vp[2][0]), 32767, macro_delay); // Macro scales mix below center for full clean
    
    // Scale delay time by active_macro
    int32_t raw_delay_time = vp[2][1];
    int32_t scaled_delay_time = raw_delay_time;
    if (active_macro < 16384) {
        if (raw_delay_time > 16384) {
            int32_t diff = raw_delay_time - 16384;
            scaled_delay_time = 16384 + ((diff * macro_delay) >> 14);
        }
    } else {
        int32_t diff = 32767 - raw_delay_time;
        scaled_delay_time = raw_delay_time + ((diff * (macro_delay - 16384)) / 16383);
    }
    if (delay_karplus_active && cv1_live && cv1_val != 0) {
        scaled_delay_time = clamp_i32(scaled_delay_time + (cv1_val * 4), 24, 32767);
    }
    static int32_t slewed_delay_time = 12000;
    slewed_delay_time += (scaled_delay_time - slewed_delay_time) >> 3; // ~45ms tape inertia pitch-glide
    p.delay_time     = slewed_delay_time;

    // Custom Delay Feedback and Glitch Feedback scaling:
    int32_t raw_fb = vp[2][2];
    int32_t clean_fb = raw_fb;
    int32_t glitch_fb = 0;
    if (raw_fb > 22937) {
        int32_t diff = raw_fb - 22937;
        clean_fb = 22937 + ((diff * (33000 - 22937)) / (32767 - 22937));
        glitch_fb = (diff * 32767) / (32767 - 22937);
    }

    if (active_macro < 16384) {
        glitch_fb = (glitch_fb * active_macro) >> 14;
    } else {
        int32_t diff_fb = 32767 - glitch_fb;
        int32_t scale_q15 = (macro_delay - 16384) * 2;
        glitch_fb = glitch_fb + ((diff_fb * scale_q15) >> 15);
    }

    p.delay_feedback = clean_fb;
    p.glitch_feedback = glitch_fb;

    bool use_freeze_params = is_frozen;
    int32_t raw_glitch_speed = 16384;
    if (use_freeze_params) {
        p.glitch_mix     = freeze_vp[0]; // scrub offset
        raw_glitch_speed = freeze_vp[2]; // speed
        p.glitch_size    = freeze_vp[1]; // loop size
    } else {
        int32_t raw_mix = apply_deadzone(vp[3][0]);
        p.glitch_mix   = (raw_mix >= 32760) ? 32767 : scale_grit(raw_mix, 32767, macro_glitch);
        p.glitch_size  = vp[3][1];
        raw_glitch_speed = scale_grit(vp[3][2], 32767, macro_glitch);

        if (cv2_live && cv2_abs > 800) {
            int32_t mix_inject;
            if (cv2_abs > 1900) {
                int32_t t = clamp_i32(cv2_abs - 1900, 0, 148);
                mix_inject = 24000 + (t * 8767) / 148;
            } else {
                int32_t t = cv2_abs - 800;
                mix_inject = (t * 24000) / 1100;
            }
            p.glitch_mix = clamp_i32(p.glitch_mix + mix_inject, 0, 32767);
        }
    }
    p.glitch_speed = raw_glitch_speed;

    // Glitch / Freeze speed mapping
    int32_t glitch_speed_mapped = 65536;
    if (use_freeze_params) {
        int32_t knob = freeze_vp[2];
        if (knob > 7600 && knob < 8700) {
            glitch_speed_mapped = 32768; // Octave Down
        } else if (knob > 10300 && knob < 11500) {
            glitch_speed_mapped = 43700; // Fifth Down
        } else if (knob > 15300 && knob < 17400) {
            glitch_speed_mapped = 65536; // Unison
        } else if (knob > 23800 && knob < 25200) {
            glitch_speed_mapped = 98304; // Fifth Up
        } else if (knob > 31500) {
            glitch_speed_mapped = 131072; // Octave Up
        } else {
            glitch_speed_mapped = (int32_t)(((int64_t)knob * 131072) / 32767);
        }
    } else {
        if (raw_glitch_speed > 18000) {
            glitch_speed_mapped = 65536 + (((raw_glitch_speed - 18000) * 65536) / 14767);
        } else if (raw_glitch_speed < 14000) {
            glitch_speed_mapped = -65536 + ((raw_glitch_speed * 131072) / 14000);
        }
    }
    p.glitch_speed_mapped = glitch_speed_mapped;

    // Precompute Glitch targets for Core 1
    {
        static bool last_is_frozen = false;
        static uint32_t frozen_clk_period = 0;
        if (is_frozen && !last_is_frozen) {
            frozen_clk_period = g_clk_period_samples;
        }
        last_is_frozen = is_frozen;

        bool eff_mono = global_mono_mode;
        int32_t max_buf_samples = eff_mono ? 65520 : 32760;
        int32_t active_clk = is_frozen ? (frozen_clk_period > 0 ? frozen_clk_period : g_clk_period_samples) : g_clk_period_samples;
        int32_t size = p.glitch_size;
        if (cv2_live && cv2_abs > 800 && size < 4000) {
            size = 4000;
        }
        int32_t loop_size = 128;
        if (size < 4096) {
            loop_size = 32 + ((size * 224) >> 12);
        } else if (active_clk > 240) {
            // 16 rhythmic subdivisions (in 16th note units):
            // 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1 beat, 1.5b, 2b, 3b, 4b (1 bar), 6b, 8b (2 bars), 12b (3 bars), 16b (4 bars)
            static const int32_t clk_div_num[16] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256};
            
            // Dynamically count valid subdivisions that fit inside max_buf_samples
            int32_t valid_steps = 16;
            while (valid_steps > 1 && (((int64_t)active_clk * clk_div_num[valid_steps - 1]) / 16) > max_buf_samples) {
                valid_steps--;
            }

            int32_t norm_size = ((size - 4096) * 32768) / (32767 - 4096);
            int32_t step = (norm_size * valid_steps) >> 15;
            if (step < 0) step = 0;
            if (step >= valid_steps) step = valid_steps - 1;
            int32_t target_beat_samples = (active_clk * clk_div_num[step]) / 16;

            int32_t speed_mag = std::abs(p.glitch_speed_mapped);
            if (speed_mag < 4096) speed_mag = 4096;
            if (speed_mag < 65536) {
                loop_size = (int32_t)(((int64_t)target_beat_samples * 65536) / speed_mag);
            } else {
                loop_size = (int32_t)(((int64_t)target_beat_samples * speed_mag) >> 16);
            }
        } else {
            static const int32_t size_lut[8] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
            int32_t num_steps = eff_mono ? 8 : 7;
            int32_t norm_size = ((size - 4096) * 32768) / (32767 - 4096);
            int32_t step = (norm_size * num_steps) >> 15;
            if (step < 0) step = 0;
            if (step > num_steps - 1) step = num_steps - 1;
            int32_t base_size = size_lut[step];
            int32_t speed_mag = std::abs(p.glitch_speed_mapped);
            if (speed_mag < 4096) speed_mag = 4096;
            if (speed_mag < 65536) {
                loop_size = (int32_t)(((int64_t)base_size * 65536) / speed_mag);
            } else {
                loop_size = base_size;
            }
        }
        p.glitch_loop_size = clamp_i32(loop_size, 512, max_buf_samples);

        int32_t scrub_offset = 0;
        static uint16_t freeze_lfo_phase = 0;
        static uint16_t freeze_skip_timer = 0;
        static int32_t freeze_random_step = 0;

        int32_t cv1_offset = p.cv1 * 6;
        int32_t range = max_buf_samples - p.glitch_loop_size;
        if (range < 0) range = 0;

        if (is_frozen) {
            int32_t raw_mix = p.glitch_mix;
            if (raw_mix < 3277) {
                // Auto Sine LFO Scrubbing (Main knob < 10% / CCW limit)
                // Normalize LFO speed against range/grid steps so LFO cycle rate remains steady
                int32_t base_lfo_speed = 3 + (((3277 - raw_mix) * 18) / 3277);
                int32_t grid_unit = p.glitch_loop_size;
                if (grid_unit > active_clk) grid_unit = active_clk;
                if (grid_unit < 128) grid_unit = 128;
                int32_t num_grid_steps = (grid_unit > 0) ? (range / grid_unit) : 1;
                
                int32_t eff_lfo_speed = base_lfo_speed;
                if (num_grid_steps > 8) {
                    eff_lfo_speed = (base_lfo_speed * 8) / num_grid_steps;
                    if (eff_lfo_speed < 1) eff_lfo_speed = 1;
                }
                
                freeze_lfo_phase += eff_lfo_speed;
                int16_t sine_val = lookup_sine(freeze_lfo_phase);
                scrub_offset = (sine_val + 32768) >> 1;
            } else if (raw_mix > 29490) {
                // Random Skip Scrubbing (Main knob > 90% / CW limit)
                int32_t skip_interval_ms;
                if (active_clk > 240) {
                    // Clock-synced: jump rhythmically on beat subdivisions (4 beats -> 1 beat -> 1/4 beat)
                    int32_t clk_ms = active_clk / 24;
                    if (clk_ms < 100) clk_ms = 100;
                    int32_t knob_frac = ((raw_mix - 29490) * 32767) / (32767 - 29490);
                    if (knob_frac < 8192) {
                        skip_interval_ms = clk_ms * 4;       // Every 4 beats (1 bar)
                    } else if (knob_frac < 16384) {
                        skip_interval_ms = clk_ms * 2;       // Every 2 beats
                    } else if (knob_frac < 24576) {
                        skip_interval_ms = clk_ms;           // Every 1 beat
                    } else if (knob_frac < 28672) {
                        skip_interval_ms = clk_ms / 2;       // Every 1/2 beat (8th note)
                    } else {
                        skip_interval_ms = clk_ms / 4;       // Every 1/4 beat (16th note)
                    }
                } else {
                    // Unclocked: smooth time sweep from 800ms down to 80ms
                    skip_interval_ms = 800 - (((raw_mix - 29490) * 720) / (32767 - 29490));
                    if (skip_interval_ms < 80) skip_interval_ms = 80;
                }

                freeze_skip_timer++;
                if (freeze_skip_timer >= (uint32_t)skip_interval_ms) {
                    freeze_skip_timer = 0;
                    freeze_random_step = fast_rand(rand_seed) & 0x7FFF;
                }
                scrub_offset = freeze_random_step;
            } else {
                // Manual Scrubbing (10% to 90% knob travel): 28500 = repeat current live beat steadily
                int32_t manual_ratio = ((raw_mix - 3277) * 32768) / (29490 - 3277);
                scrub_offset = clamp_i32(manual_ratio, 0, 32767);
            }
        } else {
            freeze_lfo_phase = 0;
            freeze_skip_timer = 0;
        }

        int32_t raw_offset = 0;
        if (active_clk > 240) {
            // Beat-aligned scrubbing: grid unit = loop size for breakbeat slicing,
            // or 1 beat if loop size is larger than a beat
            int32_t grid_unit = p.glitch_loop_size;
            if (grid_unit > active_clk) grid_unit = active_clk;
            if (grid_unit < 128) grid_unit = 128;
            int32_t num_grid_steps = range / grid_unit;
            if (num_grid_steps > 0) {
                int32_t step_idx = ((32767 - scrub_offset) * num_grid_steps) >> 15;
                step_idx = clamp_i32(step_idx, 0, num_grid_steps);
                raw_offset = (step_idx * grid_unit) + p.glitch_loop_size;
            } else {
                raw_offset = p.glitch_loop_size;
            }
        } else {
            raw_offset = (((32767 - scrub_offset) * range) >> 15) + p.glitch_loop_size;
        }
        int32_t target_offset = raw_offset + cv1_offset;
        p.glitch_target_offset = clamp_i32(target_offset, 0, max_buf_samples);

        p.glitch_speed_q16 = p.glitch_speed_mapped;
    }

    // Filter cutoff: only scale upward by macro (above center). Below center, leave cutoff
    // unchanged — closing the filter makes the signal quiet/muffled, not clean.
    int32_t eff_filter_cutoff = vp[4][0];
    if (macro_filter > 16384) {
        eff_filter_cutoff = scale_grit(vp[4][0], 32767, macro_filter);
    }
    if (filter_note_active && cv1_live && cv1_val != 0) {
        eff_filter_cutoff = clamp_i32(eff_filter_cutoff + (cv1_val * 4), 0, 32767);
    }
    p.filter_cutoff = eff_filter_cutoff;
    p.filter_res    = vp[4][1];
    p.filter_morph  = scale_grit(vp[4][2], 32767, macro_filter);

    p.freeze = is_frozen;
    bool cv2_stutter = cv2_live && (cv2_abs > 1900);
    p.stutter = (pulse1_live && pulse1_val) || cv2_stutter;
    p.cv1 = cv1_live ? cv1_abs : 0;
    p.cv1_bipolar = cv1_live ? cv1_val : 0;
    p.cv2 = cv2_val;
    p.pulse1_live = pulse1_live;
    p.pulse2_live = pulse2_live;
    p.cv1_live = cv1_live;
    p.cv2_live = cv2_live;

    p.no_audio1 = debounced_no_audio1;
    p.no_audio2 = debounced_no_audio2;
    // Extended Mono Mode (~2.73s buffer): active when user enables Mono Mode via Macro Switch DOWN + Knob X CCW limit
    p.mono_mode = global_mono_mode;
    p.dual_mono_mode = global_dual_mono_mode;

    p.is_freeze_page = is_frozen;
    p.flash_writing  = false;
    p.grittiness_macro = active_macro;
    p.input_width = global_input_width;
    p.routing_mode = global_routing_mode;

    // Reverb params
    p.reverb_mix  = scale_grit(apply_deadzone(vp[5][0]), 32767, macro_reverb); // Macro scales mix below center for full clean
    {
        int32_t fb = vp[5][2];
        int32_t fb_glitch = fb;
        if (fb > 12000) {
            if (active_macro < 16384) {
                fb_glitch = 12000 + (((fb - 12000) * active_macro) >> 14);
            } else {
                int32_t target_max = 32767;
                fb_glitch = fb + (((target_max - fb) * (macro_reverb - 16384)) / 16383);
            }
        }
        
        int32_t raw_x = vp[5][1];
        int32_t reverb_mode = 0;
        int32_t size_scale = 16384;

        if (raw_x < 14000) {
            reverb_mode = 1;
            size_scale = 6000 + (((14000 - raw_x) * 26767) / 14000);
        } else if (raw_x > 18700) {
            reverb_mode = 0;
            size_scale = 6000 + (((raw_x - 18700) * 26767) / 14067);
        } else {
            reverb_mode = 0;
            size_scale = 6000;
        }

        p.reverb_mode = reverb_mode;
        p.reverb_size = size_scale;
        
        // Max decay capped at 30000 (~0.915) to ensure feedback tank stability without bass runaway
        int32_t max_decay = 24000 + (((size_scale - 3276) * 45000) >> 20); // max 30000
        int32_t decay = (fb_glitch * max_decay) >> 15;
        if (decay > 30000) decay = 30000;

        // Damping mapped between 18000 (warm dark plate) and 29000 (bright plate)
        int32_t damp = 29000 - ((fb_glitch * 11000) >> 15);

        // Bitcrushing & lo-fi degradation trigger ONLY at high Y knob settings (> 20000 / ~62% rotation):
        int32_t lofi_level = (fb_glitch > 20000) ? (((fb_glitch - 20000) * 32767) / 12767) : 0;
        int32_t sparkle_level = (fb_glitch > 24000) ? (((fb_glitch - 24000) * 32767) / 8767) : 0;
        int32_t circuit_bent_level = (fb_glitch > 27000) ? (((fb_glitch - 27000) * 32767) / 5767) : 0;

        p.reverb_decay = decay;
        p.reverb_damp = damp;
        p.reverb_lofi_level = lofi_level;
        p.reverb_sparkle_level = sparkle_level;
        p.reverb_circuit_bent_level = circuit_bent_level;
        p.reverb_lofi_shift = 0;
        p.reverb_lofi_frac = 0;
    }

    if (cv1_live && cv1_abs > 100) {
        if (cv1_abs > 1200) {
            int32_t t = clamp_i32(cv1_abs - 1200, 0, 848);
            p.filter_morph    = clamp_i32(p.filter_morph + (t * 3000) / 848, 0, 32767);
            int32_t xor_add   = (t * 2) / 848;
            p.chorus_xor_mask = clamp_i32(p.chorus_xor_mask + xor_add, 0, 31);
        }
    }

    if (cv2_live && cv2_abs > 800) {
        if (cv2_abs <= 1900) {
            int32_t t = (cv2_abs - 800) * 32767 / 1100;
            p.chorus_mix    = clamp_i32(p.chorus_mix    + ((4000 * t) >> 15), 0, 32767);
            p.chorus_depth  = clamp_i32(p.chorus_depth  + ((2000 * t) >> 15), 0, 32767);
            p.chorus_rate   = clamp_i32(p.chorus_rate   + ((1000 * t) >> 15), 0, 32767);
            p.filter_cutoff = clamp_i32(p.filter_cutoff - ((1500 * t) >> 15), 0, 32767);
        } else {
            p.chorus_mix    = clamp_i32(p.chorus_mix    + 4000, 0, 32767);
            p.chorus_depth  = clamp_i32(p.chorus_depth  + 2000, 0, 32767);
            p.chorus_rate   = clamp_i32(p.chorus_rate   + 1000, 0, 32767);
            p.filter_cutoff = clamp_i32(p.filter_cutoff - 1500, 0, 32767);
            int32_t t = clamp_i32(cv2_abs - 1900, 0, 148) * 32767 / 148;
            p.reverb_mix           = clamp_i32(p.reverb_mix           + ((4000 * t) >> 15), 0, 32767);
            p.reverb_decay         = clamp_i32(p.reverb_decay         + ((3000 * t) >> 15), 0, 32767);
            p.reverb_sparkle_level = clamp_i32(p.reverb_sparkle_level + ((2000 * t) >> 15), 0, 32767);
        }
    }
}

static void push_params_to_core1() {
    push_params_for_idx(0);
    push_params_for_idx(1);
}

static void get_bar_graph_leds(int32_t val, int16_t out_brightness[6]) {
    for (int i = 0; i < 6; i++) {
        int32_t lower = i * 5461;
        int32_t upper = (i + 1) * 5461;
        if (val <= lower) {
            out_brightness[i] = 0;
        } else if (val >= upper) {
            out_brightness[i] = 3000;
        } else {
            int32_t frac = val - lower;
            out_brightness[i] = (frac * 3000) / 5461;
        }
    }
}

void BendsCard::tick_ui_once() {
    static ComputerCard::Switch debounced_sw = ComputerCard::Switch::Middle;
    static ComputerCard::Switch prev_sw = ComputerCard::Switch::Middle;
    static uint32_t sw_stable_timer = 0;
    static ComputerCard::Switch last_debounced_sw = ComputerCard::Switch::Middle;
    static uint32_t active_sw_held_ms = 0;
    static bool     hold_action_triggered = false;
    static int      pageBeforeUp = 0;
    static bool     settings_adjusted_this_hold = false;

    // ── 1. Read & IIR-smooth knobs ────────────────────────────────────────
    // KnobVal returns 0..4095 (12-bit ADC). Scale to Q15 via <<3 → 0..32760.
    // IIR: alpha = 1/16 → τ ≈ 16 ms at 1 kHz polling rate.
    int32_t rawMain = KnobVal(Knob::Main) << 3;
    int32_t rawX    = KnobVal(Knob::X)    << 3;
    int32_t rawY    = KnobVal(Knob::Y)    << 3;

    IIR_SMOOTH(smMain, rawMain, 4);
    IIR_SMOOTH(smX,    rawX,    4);
    IIR_SMOOTH(smY,    rawY,    4);

    // Apply deadzones to smoothed values to guarantee reaching absolute 0 and 32767
    int32_t dzMain = apply_deadzone(smMain);
    int32_t dzX    = apply_deadzone(smX);
    int32_t dzY    = apply_deadzone(smY);

    static bool first_tick = true;
    static int32_t last_dzMain = 0;
    static int32_t last_dzX = 0;
    if (first_tick) {
        last_dzMain = dzMain;
        last_dzX = dzX;
        first_tick = false;
    }
    if (chainVisTimer > 0) {
        if (abs(dzMain - last_dzMain) > 100 || abs(dzX - last_dzX) > 100) {
            chainVisTimer = 0;
        }
    }
    last_dzMain = dzMain;
    last_dzX = dzX;

    // ── 2. Jacks → debouncing & state determination ───────────────────────
    static int pulse1_connected_ctr = 0;
    static int pulse2_connected_ctr = 0;
    static int cv1_connected_ctr    = 0;
    static int cv2_connected_ctr    = 0;
    const  int JACK_DEBOUNCE        = 50; // ms

    if (Disconnected(Input::Pulse1)) { if (pulse1_connected_ctr > 0) pulse1_connected_ctr--; }
    else { if (pulse1_connected_ctr < JACK_DEBOUNCE) pulse1_connected_ctr++; }

    if (Disconnected(Input::Pulse2)) { if (pulse2_connected_ctr > 0) pulse2_connected_ctr--; }
    else { if (pulse2_connected_ctr < JACK_DEBOUNCE) pulse2_connected_ctr++; }

    if (Disconnected(Input::CV1)) { if (cv1_connected_ctr > 0) cv1_connected_ctr--; }
    else { if (cv1_connected_ctr < JACK_DEBOUNCE) cv1_connected_ctr++; }

    if (Disconnected(Input::CV2)) { if (cv2_connected_ctr > 0) cv2_connected_ctr--; }
    else { if (cv2_connected_ctr < JACK_DEBOUNCE) cv2_connected_ctr++; }

    bool pulse1_live = (pulse1_connected_ctr >= JACK_DEBOUNCE);
    bool pulse2_live = (pulse2_connected_ctr >= JACK_DEBOUNCE);
    bool cv1_live    = (cv1_connected_ctr    >= JACK_DEBOUNCE);
    bool cv2_live    = (cv2_connected_ctr    >= JACK_DEBOUNCE);

    bool pulse2_freeze = pulse2_live && PulseIn2();
    is_frozen = freeze_latched || pulse2_freeze;

    // Debounce Audio 1 and Audio 2 connection states
    static int audio1_connected_ctr = 0;
    static int audio2_connected_ctr = 0;
    const int DEBOUNCE_LIMIT = 50; // 50 ms filter
    
    if (Disconnected(Input::Audio1)) {
        if (audio1_connected_ctr > 0) audio1_connected_ctr--;
    } else {
        if (audio1_connected_ctr < DEBOUNCE_LIMIT) audio1_connected_ctr++;
    }
    
    if (Disconnected(Input::Audio2)) {
        if (audio2_connected_ctr > 0) audio2_connected_ctr--;
    } else {
        if (audio2_connected_ctr < DEBOUNCE_LIMIT) audio2_connected_ctr++;
    }
    
    static bool debounced_no_audio1 = true;
    if (debounced_no_audio1 && audio1_connected_ctr >= DEBOUNCE_LIMIT) {
        debounced_no_audio1 = false; // jack plugged in
    } else if (!debounced_no_audio1 && audio1_connected_ctr == 0) {
        debounced_no_audio1 = true;  // jack unplugged
    }
    
    static bool debounced_no_audio2 = true;
    if (debounced_no_audio2 && audio2_connected_ctr >= DEBOUNCE_LIMIT) {
        debounced_no_audio2 = false; // jack plugged in
    } else if (!debounced_no_audio2 && audio2_connected_ctr == 0) {
        debounced_no_audio2 = true;  // jack unplugged
    }

    // ── 3. Switch Debouncing & Settle Delay ──────────────────────────────
    static uint32_t startup_delay_ms = 0;
    if (startup_delay_ms < 500) {
        startup_delay_ms++;
        prev_sw = SwitchVal();
        debounced_sw = prev_sw;
        last_debounced_sw = prev_sw;
        sw_stable_timer = 0;
        active_sw_held_ms = 0;
        hold_action_triggered = false;
        freeze_latched = false;
    } else {
        ComputerCard::Switch raw_sw = SwitchVal();
        if (raw_sw != prev_sw) {
            prev_sw = raw_sw;
            sw_stable_timer = 0;
        } else {
            sw_stable_timer++;
            if (sw_stable_timer >= 25) { // Stable for 25 ms
                debounced_sw = raw_sw;
            }
        }
    }



    // ── 3b. Switch hold & flick state machine ────────────────────────────
    bool param_changed = false;
    (void)param_changed;

    bool sw_down_entered = (last_debounced_sw != ComputerCard::Switch::Down && debounced_sw == ComputerCard::Switch::Down);
    bool sw_down_exited  = (last_debounced_sw == ComputerCard::Switch::Down && debounced_sw != ComputerCard::Switch::Down);

    static bool routing_changed_this_hold = false;

    static bool manual_save_triggered = false;

    if (sw_down_entered) {
        grittiness_macro = 16384; // Default to transparent center on entry
        lockMacro.engage(dzMain, 16384, true); // Catchup lock: user must cross center before macro responds
        lockX.engage(dzX, global_input_width, false); // Delta unlock: move ~5% in either direction to unlock
        lockY.engage(dzY, global_routing_mode * 8192 + 4096, false); // Delta unlock: move ~5% in either direction to unlock
        settings_adjusted_this_hold = false;
        routing_changed_this_hold = false;
        manual_save_triggered = false;
    }

    if (sw_down_exited) {
        bool macro_was_turned = (grittiness_macro != 16384);

        if (macro_was_turned) {
            // ALWAYS bake Macro Grittiness into virtual parameters (vp) when Main Knob was used!
            bake_macro_to_vp(grittiness_macro);
            grittiness_macro = 16384; // Reset to transparent center
            g_macro_active = false;
            last_modified_macro_knob = 0;
        } else if (active_sw_held_ms < 350) {
            // Flick DOWN (<350ms) — cycle pages 0→1→2→3→4→5→0
            currentPage = (currentPage < 5) ? (currentPage + 1) : 0;
            pageFlashTimer = 400;
            param_changed = true;
        }

        if (active_sw_held_ms >= 350 && settings_adjusted_this_hold) {
            // Save settings to flash ONLY if Knob X or Knob Y were adjusted
            bends_save_settings();
            if (routing_changed_this_hold) {
                bends_trigger_chain_vis(global_routing_mode, global_mono_mode);
            }
            grittiness_macro = 16384;
            g_macro_active = false;
            last_modified_macro_knob = 0;
        }

        if (currentPage == 3 && is_frozen) {
            lockMain.engage(dzMain, freeze_vp[0]);
            lockX.engage(dzX, freeze_vp[1]);
            lockY.engage(dzY, freeze_vp[2]);
        } else {
            lockMain.engage(dzMain, vp[currentPage][0]);
            lockX.engage(dzX, vp[currentPage][1]);
            lockY.engage(dzY, vp[currentPage][2]);
        }
        lockMacro.engage(dzMain, grittiness_macro, true);
        param_changed = true;
    }

    if (debounced_sw == last_debounced_sw) {
        if (debounced_sw != ComputerCard::Switch::Middle) {
            active_sw_held_ms++;
            if (active_sw_held_ms >= 350) {
                if (!hold_action_triggered) {
                    hold_action_triggered = true;
                    if (debounced_sw == ComputerCard::Switch::Down) {
                        last_modified_macro_knob = 0; // default view is Macro
                    }
                }
            }
            // 3-second hold manual save with 600ms LED confirmation flash
            // Suppressed when Main Knob macro is active to avoid saving mid-adjustment
            if (debounced_sw == ComputerCard::Switch::Down && active_sw_held_ms >= 3000 && grittiness_macro == 16384) {
                if (!manual_save_triggered) {
                    manual_save_triggered = true;
                    bends_save_settings();
                    saveFlashTimer = 600; // 600ms LED confirmation flash
                }
            }
        }
    } else {
        // Transition detected
        if (last_debounced_sw == ComputerCard::Switch::Middle && debounced_sw == ComputerCard::Switch::Up) {
            // Instant freeze toggle on switch press UP
            if (!freeze_latched) {
                // Freeze and jump to Page 3 (Glitcher/Freeze)
                pageBeforeUp = currentPage;
                freeze_latched = true;
                is_frozen = true;
                currentPage = 3;
                freeze_vp[0] = 28500; // Default to top of manual scrub zone -> repeats current live beat steadily
                lockMain.engage(dzMain, freeze_vp[0], false); // Delta unlock: move ~5% in either direction to unlock
                lockX.engage(dzX, freeze_vp[1], false);
                lockY.engage(dzY, freeze_vp[2], false);
                param_changed = true;
            } else {
                // Unfreeze and return to the previous page
                freeze_latched = false;
                is_frozen = false;
                currentPage = pageBeforeUp;
                lockMain.engage(dzMain, vp[currentPage][0]);
                lockX.engage(dzX, vp[currentPage][1]);
                lockY.engage(dzY, vp[currentPage][2]);
                param_changed = true;
            }
        }
        // Reset state
        last_debounced_sw = debounced_sw;
        active_sw_held_ms = 0;
        hold_action_triggered = false;
    }

    // ── 3c. Sample Show Timer ───────────────────────────────────────────
    // ── 4. KnobLock gating → update virtual parameters ───────────────────
    static uint32_t boot_lock_timer = 300;
    if (boot_lock_timer > 0) {
        boot_lock_timer--;
        lockMain.engage(dzMain, vp[currentPage][0]);
        lockX.engage(dzX, vp[currentPage][1]);
        lockY.engage(dzY, vp[currentPage][2]);
        lockMacro.engage(dzMain, grittiness_macro);
    }

    if (debounced_sw == ComputerCard::Switch::Down && active_sw_held_ms >= 350) {
        int32_t nextMacro = lockMacro.update(dzMain);
        if (grittiness_macro != nextMacro) {
            grittiness_macro = nextMacro;
            param_changed = true;
            last_modified_macro_knob = 0;
        }
        int32_t nextWidth = lockX.update(dzX);
        if (global_input_width != nextWidth) {
            int32_t wdiff = global_input_width - nextWidth;
            if (wdiff < 0) wdiff = -wdiff;
            if (wdiff > 800) {
                settings_adjusted_this_hold = true;
            }
            global_input_width = nextWidth;
            param_changed = true;
            last_modified_macro_knob = 1;
        }
        // Mono Mode & Dual Mono Mode toggle via Knob X in Macro mode (Switch DOWN hold):
        // Knob X < 500 (CCW): Extended Mono Mode (mixes down dual inputs to 4.0s mono delay buffer)
        // Knob X > 31500 (CW): Dual Mono Mode (decorrelated 2-channel independent processing)
        bool want_mono = (nextWidth < 500);
        bool want_dual_mono = (nextWidth > 31500);
        if (global_mono_mode != want_mono || global_dual_mono_mode != want_dual_mono) {
            global_mono_mode = want_mono;
            global_dual_mono_mode = want_dual_mono;
            settings_adjusted_this_hold = true;
            param_changed = true;
            routing_changed_this_hold = true;
        }
        int32_t nextPresetKnob = lockY.update(dzY);
        int32_t nextMode = nextPresetKnob >> 13; // divides by 8192
        if (nextMode > 3) nextMode = 3;
        if (global_routing_mode != nextMode) {
            global_routing_mode = nextMode;
            settings_adjusted_this_hold = true;
            param_changed = true;
            last_modified_macro_knob = 2;
            routing_changed_this_hold = true;
        }

        // Clean Reset Gesture: Y knob fully right (> 31500) while Switch DOWN held
        // resets all parameters to the saved clean state (latched once per gesture)
        static bool clean_reset_latched = false;
        if (nextPresetKnob > 31500) {
            if (!clean_reset_latched) {
                clean_reset_latched = true;
                bends_load_settings(); // restore saved clean state
                lockMain.engage(dzMain, vp[currentPage][0]);
                lockX.engage(dzX, vp[currentPage][1]);
                lockY.engage(dzY, vp[currentPage][2]);
                grittiness_macro = 16384; // reset macro back to transparent center
                lockMacro.engage(dzMain, 16384);
                pageFlashTimer = 300; // single LED flash confirmation
            }
        } else if (nextPresetKnob < 26000) {
            clean_reset_latched = false;
        }
    }

    if (is_frozen && currentPage == 3) {
        // Frozen and on the freeze page -- write to freeze_vp only when NOT holding switch DOWN!
        if (debounced_sw != ComputerCard::Switch::Down) {
            int32_t nextMain = lockMain.update(dzMain);
            if (freeze_vp[0] != nextMain) {
                freeze_vp[0] = nextMain;
                param_changed = true;
            }
            int32_t nextX = lockX.update(dzX);
            if (freeze_vp[1] != nextX) {
                freeze_vp[1] = nextX;
                param_changed = true;
            }
            int32_t nextY = lockY.update(dzY);
            if (freeze_vp[2] != nextY) {
                freeze_vp[2] = nextY;
                param_changed = true;
            }
        }
    } else {
        if (debounced_sw != ComputerCard::Switch::Down) {
            int32_t nextMain = lockMain.update(dzMain);
            if (vp[currentPage][0] != nextMain) {
                vp[currentPage][0] = nextMain;
                param_changed = true;
            }
            int32_t nextX = lockX.update(dzX);
            if (vp[currentPage][1] != nextX) {
                vp[currentPage][1] = nextX;
                param_changed = true;
            }
            int32_t nextY = lockY.update(dzY);
            if (vp[currentPage][2] != nextY) {
                vp[currentPage][2] = nextY;
                param_changed = true;
            }
        }
    }



    // ── 5. Push virtual params to Core 1 double buffer ───────────────────
    {
        uint32_t next_idx = 1 - g_params_idx.load(std::memory_order_relaxed);
        int32_t cv1_val = cv1_live ? CVIn1() : 0;
        int32_t cv2_val = cv2_live ? CVIn2() : 0;
        bool p1_val = pulse1_live ? PulseIn1() : false;
        push_params_for_idx(
            next_idx,
            cv1_live, cv1_val,
            cv2_live, cv2_val,
            pulse1_live, p1_val,
            pulse2_live,
            debounced_no_audio1, debounced_no_audio2,
            debounced_sw, active_sw_held_ms
        );
        g_params_idx.store(next_idx, std::memory_order_release);
    }
    // ── 6. LED visualisation ──────────────────────────────────────────────
    if (factoryResetFlashTimer > 0) {
        factoryResetFlashTimer--;
        bool on = (factoryResetFlashTimer % 80) < 40;
        int16_t bright = on ? 4095 : 0;
        for (int i = 0; i < 6; i++) {
            LedBrightness(i, bright);
        }
        return;
    }
    if (saveFlashTimer > 0) {
        saveFlashTimer--;
        bool on = (saveFlashTimer % 100) < 50;
        int16_t bright = on ? 4095 : 100;
        for (int i = 0; i < 6; i++) {
            LedBrightness(i, bright);
        }
        return;
    }
    if (chainVisTimer > 0) {
        chainVisTimer--;
        int16_t bright_leds[6] = {0, 0, 0, 0, 0, 0};
        
        if (chainVisTimer > 900) {
            // Initial fast flash (1200-901 = first 300ms region)
            uint32_t elapsed = 1200 - chainVisTimer;
            if (elapsed < 80) {
                for (int i = 0; i < 6; i++) bright_leds[i] = 4095;
            }
            // else gap
        } else if (chainVisTimer >= 0 && chainVisTimer <= 900) {
            uint32_t elapsed_seq = 900 - chainVisTimer;
            if (elapsed_seq < 900) {
                int step_idx = elapsed_seq / 150;
                if (step_idx >= 0 && step_idx < 6) {
                    int chain[6];
                    get_routing_chain(chainVisMode, chainVisMono, chain);
                    int active_led = chain[step_idx];
                    if (active_led >= 0 && active_led < 6) {
                        bright_leds[active_led] = 4095;
                    }
                }
            }
        }

        for (int i = 0; i < 6; i++) {
            LedBrightness(i, bright_leds[i]);
        }
        return;
    }

    bool is_freeze_page = (currentPage == 3 && is_frozen);

    if (debounced_sw == ComputerCard::Switch::Down && active_sw_held_ms >= 350) {
        int16_t bar_leds[6];
        int32_t val_to_show = grittiness_macro;
        bool is_locked = lockMacro.locked;
        int32_t phys_val = dzMain;
        
        if (last_modified_macro_knob == 1) {
            val_to_show = global_input_width;
            is_locked = lockX.locked;
            phys_val = dzX;
        } else if (last_modified_macro_knob == 2) {
            is_locked = lockY.locked;
            phys_val = dzY;
        }
        
        if (last_modified_macro_knob == 2) {
            for (int i = 0; i < 6; i++) {
                bar_leds[i] = (i == global_routing_mode) ? 4095 : 100;
            }
            if (global_mono_mode) {
                bar_leds[5] = 4095; // Glowing LED 5 indicates Extended Mono Mode active
            } else if (global_dual_mono_mode) {
                bar_leds[4] = 4095; // Glowing LED 4 indicates Dual Mono Mode active
            }
            if (is_locked) {
                static uint32_t blink_counter = 0;
                blink_counter++;
                bool blink_on = (blink_counter % 200 < 100);
                if (blink_on) {
                    int phys_idx = phys_val / 5461;
                    if (phys_idx < 0) phys_idx = 0;
                    if (phys_idx > 5) phys_idx = 5;
                    bar_leds[phys_idx] = 4095;
                }
            }
        } else if (is_locked) {
            // Knob is locked, show catchup helper
            get_bar_graph_leds(val_to_show, bar_leds);
            // Make the virtual value target dim
            for (int i = 0; i < 6; i++) {
                bar_leds[i] = (bar_leds[i] * 300) >> 12; // dim it down
            }
            // Dynamic Knob-Lock Proximity Pulse: pulse rate accelerates as physical knob approaches target
            int32_t dist = phys_val - val_to_show;
            if (dist < 0) dist = -dist;
            uint32_t blink_period = 200;
            if (dist < 4000) blink_period = 40;        // Very close (< 12%): 25Hz rapid pulse
            else if (dist < 14000) blink_period = 80;  // Approaching (< 40%): 12.5Hz pulse
            else blink_period = 200;                    // Far away: 5Hz pulse

            static uint32_t blink_counter = 0;
            blink_counter++;
            bool blink_on = ((blink_counter % blink_period) < (blink_period / 2));
            if (blink_on) {
                int phys_idx = phys_val / 5461;
                if (phys_idx < 0) phys_idx = 0;
                if (phys_idx > 5) phys_idx = 5;
                bar_leds[phys_idx] = 4095; // flash bright
            }
        } else {
            // Unlocked, show normal value solid
            get_bar_graph_leds(val_to_show, bar_leds);
        }
        for (int i = 0; i < 6; i++) {
            LedBrightness(i, bar_leds[i]);
        }
    } else if (is_freeze_page) {
        // Freeze Scrub page: show the active loop window (glow) and moving playhead (bright)
        // In Extended Mono Mode the glitcher buffer is 65536 samples; scale sectors accordingly.
        const bool in_mono = global_mono_mode && is_frozen;
        const int32_t buf_mask  = in_mono ? 0xFFFF : 0x7FFF;
        const int32_t sector_sz = in_mono ? 10923  : 5461;
        int32_t start_idx = ((int32_t)glitcher.freeze_wr - glitcher.active_offset) & buf_mask;
        int32_t end_idx   = (start_idx + glitcher.current_loop_len) & buf_mask;
        int32_t play_pos  = (start_idx + (int32_t)(glitcher.rd_q16 >> 16)) & buf_mask;
        
        for (int i = 0; i < 6; i++) {
            int32_t led_sample = i * sector_sz + (sector_sz >> 1); // center of LED's sector
            bool in_loop = false;
            if (start_idx <= end_idx) {
                in_loop = (led_sample >= start_idx && led_sample < end_idx);
            } else {
                in_loop = (led_sample >= start_idx || led_sample < end_idx);
            }
            
            // Determine base brightness: glow if inside the loop window, very dim if outside
            int32_t brightness = in_loop ? 800 : 80;
            
            // Highlight the playhead position
            int32_t play_led = play_pos / sector_sz;
            if (play_led < 0) play_led = 0;
            if (play_led > 5) play_led = 5;
            if (play_led == i) {
                brightness = 4095;
            }
            
            LedBrightness(i, brightness);
        }
    } else if (pageFlashTimer > 0) {
        pageFlashTimer--;
        for (int i = 0; i < 6; i++) {
            int brightness = 0;
            if (i == currentPage) {
                brightness = 4095;
            }
            if (i == 5 && is_frozen) {
                brightness = 2048;
            }
            LedBrightness(i, brightness);
        }
    } else {
        // Normal mode: Active page LED soft steady glow (1800); inactive LEDs stay OFF (0) unless activity triggers
        // Clock sync pulse: Brief highlight pulse on active LED on clock pulses
        static uint16_t clk_pulse_flash = 0;
        static bool last_ui_p1 = false;
        bool ui_p1 = pulse1_live ? PulseIn1() : false;
        bool ui_p1_rising = ui_p1 && !last_ui_p1;
        last_ui_p1 = ui_p1;
        if (ui_p1_rising && pulse1_live) {
            clk_pulse_flash = 50; // 50ms clock pulse flash
        }
        if (clk_pulse_flash > 0) clk_pulse_flash--;

        uint16_t in_lvl = vis_input_level.load(std::memory_order_relaxed);
        bool codec_glitch = vis_codec_glitch.exchange(false, std::memory_order_relaxed);
        bool delay_repeat = vis_delay_pulse.exchange(false, std::memory_order_relaxed);

        for (int i = 0; i < 6; i++) {
            int brightness = 0; // 0 baseline glow (inactive LEDs remain dark unless active)
            if (i == 0) { // Chorus / Tape: Audio input level envelope follower
                brightness = (in_lvl * 1200) >> 15;
            } else if (i == 1) { // Codec: Glitches, pops, packet drops, and bit-scramble detector
                brightness = codec_glitch ? 1000 : 0;
            } else if (i == 2) { // Delay: Subtle rhythmic repeat pulse on active delay echoes
                brightness = delay_repeat ? 350 : 0;
            } else if (i == 3) { // Glitcher: Stutter active loop pulse
                brightness = glitcher.active ? 450 : 0;
            } else if (i == 4) { // Filter: Resonance & cutoff envelope
                brightness = (vp[4][1] * 350) >> 15;
            } else if (i == 5) { // Reverb / Freeze: Decay energy & freeze indicator
                if (is_frozen) {
                    brightness = 800;
                } else {
                    brightness = (vp[5][0] * 300) >> 15;
                }
            }

            // Highlight active page LED (soft 1600 glow, boosted to 2600 ONLY on clock pulse)
            if (i == currentPage) {
                if (brightness < 1600) brightness = 1600;
                if (clk_pulse_flash > 0) brightness = 2600;
            }

            LedBrightness(i, brightness);
        }
    }
}

void BendsCard::run_core0_ui_loop() {
    // Core 0 is a pure UI loop — no audio processing, no FIFO.
    // All DSP including reverb runs on Core 1 inside ProcessSample().
    // tick_ui_once() fires every ~1 ms using the hardware microsecond timer.
    uint32_t last_tick_us = time_us_32();
    while (1) {
        uint32_t now = time_us_32();
        if ((now - last_tick_us) >= 1000) {
            last_tick_us += 1000;
            tick_ui_once();
        }
    }
}

// ============================================================================
// main() — CPU setup, block init, core launch
// ============================================================================
int main() {
    // 1. Run at 192 MHz — clean PLL multiple of 48 kHz, slight overvolt for stability.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(10);
    set_sys_clock_khz(192000, true);

    // 2. Init stdio and the fixed-point sine table.
    stdio_init_all();
    init_sine_table();

    // Precompute mu-law decode table for branch-free lookups on Core 1
    for (int i = 0; i < 256; i++) {
        int32_t sign = (i & 0x80) ? -1 : 1;
        int32_t exponent = (i >> 4) & 0x07;
        int32_t mantissa = i & 0x0F;
        int32_t reconstructed = 0;
        if (exponent == 0) {
            reconstructed = (mantissa << 3);
        } else {
            reconstructed = (((mantissa << 3) + 132) << exponent) - 132;
        }
        if (reconstructed > 32767) reconstructed = 32767;
        if (reconstructed < 0) reconstructed = 0;
        mulaw_decode_table[i] = (int16_t)(sign * reconstructed);
    }

    // 3. Init sample headers and all DSP blocks — zeroes buffers.
    chorus.init();
    codec.init();
    delay_fx.init();
    glitcher.init();
    filter.init();
    reverb.init();

    // Initialize Pulse Out 2 for high-speed PWM audio output (microsound generator)
    gpio_set_function(PULSE_2_RAW_OUT, GPIO_FUNC_PWM);
    {
        pwm_config config = pwm_get_default_config();
        pwm_config_set_wrap(&config, 1023); // 10-bit PWM = ~122 kHz carrier
        pwm_init(pwm_gpio_to_slice_num(PULSE_2_RAW_OUT), &config, true);
    }
    pwm_set_gpio_level(PULSE_2_RAW_OUT, 512); // start at mid-point (silence)

    // 4. Enable normalisation probe and let it settle before reading jack state.
    //    The probe needs a few cycles to stabilise; 50 ms is more than enough.
    card.EnableNormalisationProbe();
    for (int i = 0; i < 10; i++) {
        (void)card.JackDisconnected(ComputerCard::Input::Audio1);
        (void)card.JackDisconnected(ComputerCard::Input::Audio2);
        sleep_ms(5);
    }

    // 5. Initialize Core 1 double-buffered parameters
    memset((void*)g_params, 0, sizeof(g_params));
    g_params[0].global_noise_scale = 16384;
    g_params[1].global_noise_scale = 16384;
    g_params[0].no_audio1 = true;
    g_params[1].no_audio1 = true;
    g_params[0].no_audio2 = true;
    g_params[1].no_audio2 = true;
    g_params[0].mono_mode = false;
    g_params[1].mono_mode = false;
    g_params[0].grittiness_macro = 16384;
    g_params[1].grittiness_macro = 16384;

    // Load persisted settings (routing mode, input width, mono mode, current page, parameter tables) from flash
    bends_load_settings();

    // Trigger chain visualization on boot
    bends_trigger_chain_vis_fast();

    // 6. Push default settings to Core 1 double-buffer before Core 1 starts.
    push_params_to_core1();

    // 7. Launch Core 1 (starts the background ADC interrupts & DMA).
    multicore_launch_core1(core1_entry);

    // 8. Sleep for 15 ms to let the background ADC/multiplexer interrupts populate true physical knob & switch values.
    sleep_ms(15);

    // 9. Instant Factory Reset on power-up if Switch DOWN is physically held at boot
    if (card.ReadSw() == ComputerCard::Switch::Down) {
        bends_erase_settings();
        bends_reset_factory_defaults();
        bends_save_settings();
        push_params_to_core1();
        factoryResetFlashTimer = 1000; // 1s fast LED blink feedback
    }

    // 10. Read initial knob positions (populated with true physical readings!)
    //     and engage KnobLocks so they start locked to the active page's defaults.
    smMain = card.ReadKnob(ComputerCard::Knob::Main) << 3;
    smX    = card.ReadKnob(ComputerCard::Knob::X)    << 3;
    smY    = card.ReadKnob(ComputerCard::Knob::Y)    << 3;
    lockMain.engage(smMain, vp[currentPage][0]);
    lockX.engage(smX, vp[currentPage][1]);
    lockY.engage(smY, vp[currentPage][2]);
    lockMacro.engage(smMain, grittiness_macro);

    // 11. Enter Core 0 UI loop — never returns.
    card.run_core0_ui_loop();
}
