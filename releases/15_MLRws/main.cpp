/**
 * MLRws — Monome Live Remix for Workshop System Computer.
 *
 * 6 looping ADPCM tracks stored in flash.  Grid rows 1–6 control
 * tracks.  Row 0 is page navigation + pattern/recall controls.
 * Row 7 is a master-volume gradient bar (right-to-left fill).
 *
 * Pages (selected via row 0):
 *   REC (col 0): per-track speed, reverse, volume, record arm
 *   CUT (col 1): playhead cutting, loop-a-section
 *
 * Row 0 layout (16-wide grid; 8x8 collapses Cols 4–7 → 3–6 and
 * Col 14 → Col 7, and omits the recall block):
 *   Col 0     = REC page
 *   Col 1     = CUT page
 *   Cols 4–7  = Pattern 1–4 (timed motion recorders)
 *   Cols 9–12 = Recall 1–4 (instant snapshot slots, 16-wide only)
 *   Col 14    = ALT modifier
 *
 * Recording: hold col 0 (or col 1 on 16-wide, to select input
 *   channel 2) on a track row + switch up/down.
 *   Speed-linked — tape speed controls both record and playback rate.
 *   Records from position 0, variable length up to flash limit.
 *
 * Knob Main      = master output volume
 * Audio In 1/2   = record source (per-track recorded_channel selects
 *                  which input is captured; the other can be left idle)
 * Audio Out 1/2  = mixed playback, split L/R by per-track recorded_channel
 */

#include "ComputerCard.h"
#include "MonomeGrid.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "bsp/board.h"
#include "tusb.h"

extern "C" {
#include "monome_ws.h"
#include "mlr.h"
#include "device_mode.h"
}

/* ------------------------------------------------------------------ */
/* Grid LED update rate (sub-sampled from 48 kHz)                     */
/* ------------------------------------------------------------------ */
#define LED_UPDATE_INTERVAL 2400  /* ~50 ms at 48 kHz → 20 fps grid update */
#define PAT_TICK_INTERVAL     48  /* ~1 ms at 48 kHz → pattern playback resolution */
#define MAIN_CTRL_DIV        16   /* 48 kHz / 16 = 3 kHz UI/control polling (~333 us quantization) */
#define GRID_POLL_MAX_EVENTS 16   /* Bound USB/grid event burst work per UI-control tick */
#define GATE_HOLD_THRESHOLD 48000  /* 1 second at 48 kHz sample rate */

/* Group-creation/dissolve flash feedback (samples at 48 kHz). */
#define GROUP_FLASH_CREATE_SAMPLES   24000  /* ~500 ms total */
#define GROUP_FLASH_CREATE_PERIOD     6000  /* ~125 ms half-period (~4 Hz blink) */
#define GROUP_FLASH_DISSOLVE_SAMPLES 36000  /* ~750 ms total: three quick confirmation flashes */
#define GROUP_FLASH_DISSOLVE_PERIOD   6000  /* ~125 ms half-period (~4 Hz blink) */
#define COPY_FLASH_SAMPLES            24000  /* ~500 ms total */
#define COPY_FLASH_PERIOD              6000  /* ~125 ms half-period */

/* How long to highlight each group when DELETE is held (in gate_pulse ticks;
 * gate_pulse advances at the LED refresh rate, ~60 Hz). */
#define GROUP_CYCLE_TICKS             16    /* two blink pulses per group */
#define KNOB_HARD_TAKEOVER_THRESHOLD 80  /* ADC counts (0..4095) — must exceed ADC noise floor */
#define GRIDLESS_REC_HOLD_SAMPLES 96000u  /* 2 seconds at 48 kHz */
#define RECORD_REARM_DELAY_SAMPLES 24000u /* 0.5 seconds at 48 kHz */
#define FORCE_8X8_GRID_LAYOUT 0  /* for testing: force compact grid layout regardless of detected width */
#define CUT_PULSE_TRIG_SAMPLES 960u  /* 20 ms at 48 kHz: PulseOut1 trigger width fired on output-enabled cut events (manual or pattern/recall playback) */
#define EMPTY_KEYBOARD_LINGER_US 3000000ull  /* 3 seconds */
#define RECALL_KEY_GATE_US      100000u
#define CV_ENV_PEAK            2047  /* CV2 envelope peak (matches CV full positive range) */
#define CV2_FLOOR_OFFSET       341   /* raw DAC subtract: -1 V at rest (assuming +6 V = +2047 raw) */
#define CV_ATTACK_MIN_SAMPLES  0u       /* instant attack at Y=0 */
#define CV_ATTACK_MAX_SAMPLES  48000u   /* 1 s at 48 kHz: attack length at Y=4095 */
#define CV_DECAY_MIN_SAMPLES   480u     /* 10 ms at 48 kHz: decay length at Y=0 */
#define CV_DECAY_MAX_SAMPLES   144000u  /* 3 s at 48 kHz: decay length at Y=4095 */
#define CV_NOTE_BASE_MIDI      48       /* C3: column 0 with X knob centered yields MIDI 48 */
#define CV_X_OFFSET_SEMITONES  24       /* ±24 semitones (~±2 V) at X knob extremes */
#define X_KNOB_LPF_SHIFT       10       /* one-pole IIR on Knob::X (sample-rate). tau≈21 ms */
#define DELETE_RESET_HOLD_SAMPLES 240000u  /* 5 seconds at 48 kHz */
#define DELETE_RESET_FLASH_PERIOD_SAMPLES 4800u  /* 100 ms half-period */
#define DELETE_RESET_FLASH_SAMPLES (DELETE_RESET_FLASH_PERIOD_SAMPLES * 6u)  /* three quick flashes */
#define EXTRA_GUIDE_ENABLED 0x80u
#define EXTRA_PATTERN_KEY_MASK 0x7Fu
#define EXTRA_PATTERN_KEY_NONE 0x70u

/* Monitor fade step per sample (Q8). 256/4 = 64 samples ≈ 1.3 ms fade. */
#define MLR_MON_FADE_STEP 4

/* ------------------------------------------------------------------ */
/* Pages                                                              */
/* ------------------------------------------------------------------ */
#define PAGE_REC  0
#define PAGE_CUT  1

enum class Mode {
	HostMLR,          /* USB host, grid connected directly */
	DeviceGridless,      /* USB device, nothing connected — gridless */
	DeviceMLR,        /* USB device, grid app connected via CDC */
	DeviceSampleMgr,  /* USB device, sample manager via CDC */
};

/* ------------------------------------------------------------------ */
/* Card                                                               */
/* ------------------------------------------------------------------ */

class MLRCard : public ComputerCard
{
public:
	MLRCard()
	{
		sleep_ms(150);
		EnableNormalisationProbe();

		/* ---- Determine mode from USB power state + protocol detection ---- */
		device_detect_result_t detect_result = {};

		USBPowerState_t power_state = USBPowerState();
		if (power_state != UFP) {
			/* Host mode — grid connected directly */
			mode_ = Mode::HostMLR;
		} else {
			/* Device mode — init TUD and detect protocol */
			board_init();
			tud_init(TUD_OPT_RHPORT);
			detect_result = device_mode_detect_protocol(2000);
			switch (detect_result.protocol) {
			case DEVICE_PROTO_SAMPLE_MGR:
				mode_ = Mode::DeviceSampleMgr;
				break;
			case DEVICE_PROTO_MEXT:
				mode_ = Mode::DeviceMLR;
				break;
			default:
				mode_ = Mode::DeviceGridless;
				break;
			}
		}

		/* ---- Mode-specific startup LED animation ---- */
		switch (mode_) {
		case Mode::HostMLR: {
			/* Host MLR: pairs sweep downward {0,1} → {2,3} → {4,5} */
			for (int step = 0; step < 6; step++) {
				int row = step % 3;
				int prev_row = (step + 2) % 3;
				LedOn(prev_row * 2, false);
				LedOn(prev_row * 2 + 1, false);
				LedOn(row * 2, true);
				LedOn(row * 2 + 1, true);
				sleep_ms(150);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceMLR: {
			/* Device MLR: forward chase {0, 1, 3, 5, 4, 2} */
			static const uint8_t chase_path[] = {0, 1, 3, 5, 4, 2};
			const int chase_len = sizeof(chase_path);
			for (int step = 0; step < 30; step++) {
				int prev = chase_path[(step + chase_len - 1) % chase_len];
				int curr = chase_path[step % chase_len];
				LedOn(prev, false);
				LedOn(curr, true);
				sleep_ms(50);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceGridless: {
			/* Gridless: single LED chase 0→5 and back */
			for (int rep = 0; rep < 2; rep++) {
				for (int i = 0; i < 6; i++) {
					for (int j = 0; j < 6; j++) LedOn(j, j == i);
					sleep_ms(60);
				}
				for (int i = 4; i >= 1; i--) {
					for (int j = 0; j < 6; j++) LedOn(j, j == i);
					sleep_ms(60);
				}
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		case Mode::DeviceSampleMgr: {
			/* Sample Manager: all LEDs pulse on then off */
			for (int b = 0; b < 4096; b += 256) {
				for (int i = 0; i < 6; i++) LedBrightness(i, (uint16_t)b);
				sleep_ms(30);
			}
			for (int b = 4095; b >= 0; b -= 256) {
				for (int i = 0; i < 6; i++) LedBrightness(i, (uint16_t)(b > 0 ? b : 0));
				sleep_ms(30);
			}
			for (int i = 0; i < 6; i++) LedOn(i, false);
		} break;
		}

		/* ---- Common init ---- */
		led_counter     = 0;
		pat_counter     = 0;
		pattern_clock_ms_ = 0;
		play_page       = PAGE_REC;
		rec_speed_accum = 0;
		rec_limit_latched = false;
		prev_rec_track  = -1;
		mlr_master_level_raw = (uint16_t)KnobVal(Knob::Main);
		master_knob_arm_raw_ = (int)mlr_master_level_raw;
		master_knob_last_raw_ = (int)mlr_master_level_raw;
		master_knob_takeover_armed_ = false;

		s_mode_ = mode_;
		mlr_init();

		/* ---- Mode-specific subsystem init ---- */
		switch (mode_) {
		case Mode::HostMLR:
			monome_ws_init(MONOME_WS_TRANSPORT_HOST, 0);
			break;
		case Mode::DeviceMLR:
			monome_ws_init(MONOME_WS_TRANSPORT_DEVICE, 0);
			monome_ws_connect(0);  /* CDC already connected during detection */
			if (detect_result.has_pending_byte) {
				monome_ws_rx_feed(&detect_result.first_byte, 1);
			}
			break;
		case Mode::DeviceGridless:
			gridless_scan_tracks();
			gl_active_track_ = 0;
			gridless_restart_all_tracks_from_zero();
			gl_prev_start_col_ = 0;
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
			break;
		case Mode::DeviceSampleMgr:
			device_mode_init();
			if (detect_result.has_pending_byte) {
				device_mode_inject_byte(detect_result.first_byte);
			}
			break;
		}

		s_card_ = this;
		multicore_launch_core1(core1_entry);
	}

	static void service_grid_redraw_core1()
	{
		if (s_card_)
			s_card_->update_grid_slice();
	}

	static void service_panel_vu_leds_core1()
	{
		if (s_card_)
			s_card_->apply_panel_vu_leds();
	}

	static bool sample_mgr_wake_byte(uint8_t byte)
	{
		return byte == 'I' || byte == 'S' || byte == 'R' ||
		       byte == 'E' || byte == 'W' || byte == 'P' || byte == 'X';
	}

	static void run_sample_manager_until_disconnect(uint8_t first_byte, bool resume_gridless)
	{
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].playing) {
				mlr_tracks[t].playing = false;
				mlr_tracks[t].pcm.r = mlr_tracks[t].pcm.w;
			}
		}

		s_sample_mgr_active_ = true;
		__dmb();

		device_mode_init();
		device_mode_inject_byte(first_byte);

		while (tud_cdc_n_connected(0)) {
			tud_task();
			device_mode_task();
		}

		for (int t = 0; t < MLR_NUM_TRACKS; t++)
			mlr_rescan_track(t);

		if (resume_gridless)
			s_rescan_needed_ = true;

		s_sample_mgr_active_ = false;
		__dmb();
	}

	static void prepare_gridless_resume_from_sample_manager()
	{
		for (int t = 0; t < MLR_NUM_TRACKS; t++)
			mlr_rescan_track(t);

		s_rescan_needed_ = true;
		if (s_card_)
			s_card_->mode_ = Mode::DeviceGridless;
		s_mode_ = Mode::DeviceGridless;
		s_sample_mgr_active_ = false;
		__dmb();
	}

	static void run_device_gridless_core1_loop()
	{
		device_mode_init();  /* pre-init so it's ready if needed */
		while (true) {
			tud_task();

			/* ---- Monitor CDC: if a sample-manager byte arrives, transition ---- */
			if (tud_cdc_n_connected(0) && tud_cdc_n_available(0) > 0) {
				uint8_t peek = 0;
				tud_cdc_n_read(0, &peek, 1);

				if (sample_mgr_wake_byte(peek)) {
					run_sample_manager_until_disconnect(peek, true);
				}
				/* else: unknown byte — ignore, stay in gridless mode */
			}

			mlr_io_task();
		}
	}

	/* Core 1: USB + I/O — mode-specific loop */
	static void core1_entry()
	{
		switch (s_mode_) {
		case Mode::HostMLR:
			board_init();
			tusb_init();
			while (true) {
				monome_ws_task();
				mlr_io_task();
				service_panel_vu_leds_core1();
				service_grid_redraw_core1();
			}
			break;
		case Mode::DeviceMLR:
			/* board_init + tud_init already done in constructor */
			while (true) {
				tud_task();
				if (tud_cdc_n_connected(0) && tud_cdc_n_available(0) > 0) {
					uint8_t peek = 0;
					tud_cdc_n_read(0, &peek, 1);
					if (sample_mgr_wake_byte(peek)) {
						run_sample_manager_until_disconnect(peek, false);
					} else {
						monome_ws_rx_feed(&peek, 1);
					}
				}
				monome_ws_task();
				mlr_io_task();
				service_panel_vu_leds_core1();
				service_grid_redraw_core1();
			}
			break;
		case Mode::DeviceGridless:
			/* board_init + tud_init already done in constructor */
			run_device_gridless_core1_loop();
			break;
		case Mode::DeviceSampleMgr:
			/* board_init + tud_init already done in constructor */
			s_sample_mgr_active_ = true;
			__dmb();
			while (tud_cdc_n_connected(0)) {
				tud_task();
				device_mode_task();
			}
			prepare_gridless_resume_from_sample_manager();
			run_device_gridless_core1_loop();
			break;
		}
	}

	virtual void __not_in_flash("ProcessSample") ProcessSample() override
	{
#ifdef MLR_PERF_PROFILING
		mlr_perf_count_process_sample();
		bool perf_ui_tick = false;
		struct PerfProcessSampleScope {
			uint32_t start_us;
			bool *ui_tick;
			~PerfProcessSampleScope()
			{
				mlr_perf_note_process_sample_us(time_us_32() - start_us, *ui_tick);
			}
		} perf_process_sample_scope{time_us_32(), &perf_ui_tick};
#endif
		if (rec_start_lockout_samples_ > 0)
			rec_start_lockout_samples_--;

		/* ---- Mode dispatch ---- */
		switch (mode_) {
		case Mode::DeviceGridless:
			if (s_sample_mgr_active_) {
				/* Sample manager owns flash — output silence */
				AudioOut1(0);
				return;
			}
			if (s_rescan_needed_) {
				/* Sample manager just disconnected — rescan tracks */
				s_rescan_needed_ = false;
				gridless_scan_tracks();
				if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS)
					gl_active_track_ = 0;
				gridless_restart_all_tracks_from_zero();
				gl_prev_start_col_ = 0;
				knob_hard_takeover_arm(Knob::Main);
				knob_hard_takeover_arm(Knob::X);
				knob_hard_takeover_arm(Knob::Y);
			}
			{
				bool gridless_control_tick = gridless_process_sample();
#ifdef MLR_PERF_PROFILING
				perf_ui_tick = gridless_control_tick;
#else
				(void)gridless_control_tick;
#endif
			}
			return;
		case Mode::DeviceSampleMgr:
			/* Silence — sample manager owns flash exclusively */
			AudioOut1(0);
			return;
		case Mode::DeviceMLR:
			if (s_sample_mgr_active_) {
				AudioOut1(0);
				return;
			}
			break;
		case Mode::HostMLR:
			break;  /* fall through to MLR processing */
		}

		/* Smooth Knob::X once per sample. Gridful uses (input gain, CV1
		 * transpose, LED 2) all read x_knob_filtered_ rather than the raw
		 * ADC value, so dither at semitone/gain boundaries doesn't flicker. */
		x_knob_filtered_ += ((int32_t)KnobVal(Knob::X) - x_knob_filtered_) >> X_KNOB_LPF_SHIFT;

		ui_ctrl_div_++;
		bool run_ui_control = false;
		if (ui_ctrl_div_ >= MAIN_CTRL_DIV) {
			ui_ctrl_div_ = 0;
			run_ui_control = true;
		}
		if (mlr_flushing && !mlr_copying) run_ui_control = false;
#ifdef MLR_PERF_PROFILING
		perf_ui_tick = run_ui_control;
		uint32_t perf_ui_section_start = 0;
		uint8_t perf_ui_section_probe = 0xFF;
		/* UI section profiling samples one rotating section per UI tick; full
		 * process_sample_ui counters measure the whole tick, including audio mix. */
		if (run_ui_control) {
			perf_ui_section_probe = perf_ui_section_probe_;
			perf_ui_section_probe_ = (uint8_t)((perf_ui_section_probe_ + 1u) & (MLR_PERF_UI_SECTIONS - 1u));
		}
#define PERF_UI_SECTION_START(section) do { \
	if (run_ui_control && perf_ui_section_probe == (section)) perf_ui_section_start = time_us_32(); \
} while (0)
#define PERF_UI_SECTION_END(section) do { \
	if (run_ui_control && perf_ui_section_probe == (section)) \
		mlr_perf_note_ui_section_us((section), time_us_32() - perf_ui_section_start); \
} while (0)
#else
#define PERF_UI_SECTION_START(section) do { (void)(section); } while (0)
#define PERF_UI_SECTION_END(section) do { (void)(section); } while (0)
#endif

		PERF_UI_SECTION_START(0);
		if (run_ui_control) {
			grid.poll(GRID_POLL_MAX_EVENTS);
			if (!mlr_flushing) {
				process_delete_reset_hold();
				process_bottom_master_control();
				process_extra_rows();
			}
		}
		PERF_UI_SECTION_END(0);

		/* ---- recording state machine: armed track + switch position ---- */
		PERF_UI_SECTION_START(1);
		int sw = SwitchVal();
		bool rec_modifier = (sw == Switch::Up || sw == Switch::Down);
		if (run_ui_control) {
			bool switch_down = (sw == Switch::Down);
			if (switch_down && !cv_attack_switch_down_prev_)
				knob_hard_takeover_arm(Knob::Y);
			cv_attack_switch_down_prev_ = switch_down;

			if (switch_down && rec_armed_track < 0 && mlr_rec_track < 0 &&
			    !rec_gated && !rec_pulse_gated_ &&
			    knob_hard_takeover_accept(Knob::Y, KnobVal(Knob::Y))) {
				set_cv2_attack_from_raw((uint16_t)KnobVal(Knob::Y));
			}
		}

		if (mlr_flushing) {
			if (resume_after_arm_track_ == rec_armed_track)
				resume_after_arm_track_ = -1;
			rec_armed_track = -1;  /* disarm if flushing */
			rec_gated = false;
			rec_pulse_gated_ = false;
		}

		/* Stop gated recording if switch returns to middle */
		if (rec_gated && !rec_modifier && mlr_rec_track >= 0) {
			mlr_stop_record();
			rec_speed_accum = 0;
			rec_gated = false;
		}

		/* Start recording if armed track exists and switch moves to rec position */
		if (rec_armed_track >= 0 && rec_modifier && mlr_rec_track < 0 && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
			if (resume_after_arm_track_ == rec_armed_track)
				resume_after_arm_track_ = -1;
			mlr_start_record(rec_armed_track);
			rec_speed_accum = 0;
		}
		/* Stop recording if switch returns to middle or armed track changes */
		if (mlr_rec_track >= 0 && !rec_gated && !rec_pulse_gated_ && (!rec_modifier || rec_armed_track != mlr_rec_track)) {
			mlr_stop_record();
			rec_speed_accum = 0;
			if (rec_armed_track >= 0 && rec_modifier && rec_armed_track != mlr_rec_track && !mlr_flushing && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
				mlr_start_record(rec_armed_track);
				rec_speed_accum = 0;
			}
		}

		/* ---- stop armed track so recording starts from silence ---- */
		if (run_ui_control && rec_armed_track >= 0 && !rec_gated &&
		    mlr_tracks[rec_armed_track].playing) {
			resume_after_arm_track_ = rec_armed_track;
			mlr_stop_track(rec_armed_track);
		}

		/* ---- Pulse In handling (gridful mode) ----
		 * Pulse In 1: rising edge resets the playhead to loop_col_start
		 *   for every track that is currently playing AND has an active
		 *   loop. It always triggers the CV2 envelope. Loop stays active.
		 *   (Mirrors gridless Pulse In 1 = reset.)
		 * Pulse In 2: gate-style record for the armed track. Rising edge
		 *   starts recording (same gates as switch-driven start); falling
		 *   edge stops it. While pulse-gated, the switch-stop path is
		 *   bypassed (see !rec_pulse_gated_ guard above).
		 * Pulse Out 1/2 are intentionally left idle in gridful mode. */
		if (rec_pulse_gated_ && mlr_rec_track < 0) {
			/* Orphaned flag (rec ended via some other path) — clear. */
			rec_pulse_gated_ = false;
		}
		if (PulseIn1RisingEdge()) {
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				if (mlr_tracks[t].playing && mlr_tracks[t].loop_active) {
					mlr_cut(t, mlr_tracks[t].loop_col_start);
				}
			}
			trigger_cv2_envelope();
		}
		if (PulseIn2RisingEdge()) {
			if (rec_armed_track >= 0 && mlr_rec_track < 0 && !mlr_flushing &&
			    !rec_limit_latched && rec_start_lockout_samples_ == 0) {
				if (mlr_tracks[rec_armed_track].playing) {
					resume_after_arm_track_ = rec_armed_track;
					mlr_stop_track(rec_armed_track);
				}
				mlr_start_record(rec_armed_track);
				rec_speed_accum = 0;
				rec_pulse_gated_ = true;
			}
		}
		if (PulseIn2FallingEdge()) {
			if (rec_pulse_gated_ && mlr_rec_track >= 0) {
				mlr_stop_record();
				rec_speed_accum = 0;
				rec_pulse_gated_ = false;
			}
		}

		/* ---- Group create/dissolve flash countdown ---- */
		if (group_flash_samples_remaining_ > 0) {
			group_flash_samples_remaining_--;
			if (group_flash_samples_remaining_ == 0)
				group_flash_mask_ = 0;
		}
		if (!mlr_flushing && copy_flash_samples_remaining_ > 0) {
			copy_flash_samples_remaining_--;
			if (copy_flash_samples_remaining_ == 0)
				copy_flash_mask_ = 0;
		}
		if (delete_reset_flash_samples_remaining_ > 0)
			delete_reset_flash_samples_remaining_--;
		PERF_UI_SECTION_END(1);

		PERF_UI_SECTION_START(2);
		/* ---- speed-linked recording ---- */
		int16_t dry_in;
		{
			int audio_in1 = (int)AudioIn1();
			int audio_in2 = (int)AudioIn2();
			/* Default to the last-known monitor channel so a disarm during
			 * monitor fade-out doesn't flip dry_in between AudioIn1/AudioIn2
			 * in a single sample (which would click through the envelope). */
			uint8_t rec_ch = mon_ch_last_;
			int route_track = (mlr_rec_track >= 0) ? mlr_rec_track : rec_armed_track;
			if (route_track >= 0) rec_ch = mlr_tracks[route_track].recorded_channel;
			dry_in = (rec_ch == 1) ? (int16_t)audio_in2 : (int16_t)audio_in1;
		}
		uint8_t dry_level = (uint8_t)(x_knob_filtered_ >> 4);  /* 0–255, from LPF'd Knob::X */
		if (mlr_rec_track >= 0) {
			/* Recording captures the input scaled only by the input gain knob.
			 * The mixer slot is a playback control and is applied during
			 * playback (mlr_play_mix_dual_sum) and during monitoring
			 * (section 5 below); applying it here too would double-attenuate
			 * the audio on playback. */
			int32_t scaled = ((int32_t)dry_in * (int32_t)dry_level) >> 8;
			if (scaled > 2047) scaled = 2047;
			if (scaled < -2048) scaled = -2048;
			uint16_t spd = mlr_tracks[mlr_rec_track].speed_frac;
			rec_speed_accum += spd;
			while (rec_speed_accum >= 256) {
				rec_speed_accum -= 256;
				mlr_record_sample((int16_t)scaled);
				if (mlr_get_rec_progress() >= MLR_MAX_SAMPLES) {
					mlr_stop_record();
					rec_speed_accum = 0;
					rec_limit_latched = true;
					rec_armed_track = -1;  /* disarm when limit reached */
					rec_gated = false;
					rec_pulse_gated_ = false;
					break;
				}
			}
		}
		PERF_UI_SECTION_END(2);

		/* ---- page navigation + pattern/recall buttons (row 0, always active) ---- */
		PERF_UI_SECTION_START(3);
		bool page_switched = false;
		if (run_ui_control && grid.keyDown() && grid.lastY() == 0) {
			int col = grid.lastX();
			bool alt_held = delete_action_held();
			int pcs = pat_col_start();
			if (col == 0) {
				bool leaving_cut = (play_page == PAGE_CUT);
				play_page = PAGE_REC;
				page_switched = true;
				if (leaving_cut) {
					/* Treat page navigation as releasing any held gated CUT voices,
					 * but do not record navigation side effects into patterns. */
					for (int t = 0; t < MLR_NUM_TRACKS; t++) {
						if (mlr_gate_mode[t]) {
							if (mlr_tracks[t].playing) {
								mlr_stop_track(t);
								if (mlr_tracks[t].loop_active)
									mlr_clear_loop(t);
							}
							release_gated_choke_pauses(t);
						}
					}
				}
			}
			else if (col >= pcs && col <= pcs + 3) {
				/* pattern buttons */
				int p = col - pcs;
				if (alt_held) {
					mlr_pattern_rec_stop(p);
					mlr_pattern_play_stop(p);
					mlr_pattern_clear(p);
					mlr_scene_save_start();
				} else if (mlr_patterns[p].state == MLR_PAT_RECORDING) {
					/* stop recording, immediately start looping */
					mlr_pattern_rec_stop(p);
					if (mlr_patterns[p].count > 0) {
						mlr_pattern_play_start(p);
						mlr_scene_save_start();
					}
				} else if (mlr_patterns[p].state == MLR_PAT_ARMED) {
					/* disarm */
					mlr_pattern_rec_stop(p);
				} else if (mlr_patterns[p].state == MLR_PAT_IDLE) {
					mlr_pattern_arm(p);
				} else if (mlr_patterns[p].state == MLR_PAT_PLAYING) {
					replay_gate_mask_ = 0;
					if (!live_gate_high_ && env_gate_high_)
						release_cv2_envelope();
					mlr_pattern_play_stop(p);
				} else if (mlr_patterns[p].state == MLR_PAT_STOPPED) {
					mlr_pattern_play_start(p);
				}
			} else if (!small_grid_ && col >= 9 && col <= 12) {
				/* recall/scene buttons (16-wide only) */
				int r = col - 9;
				if (alt_held) {
					mlr_recall_clear(r);
					mlr_scene_save_start();
				} else {
					bool finalized_other = false;
					for (int u = 0; u < MLR_NUM_RECALLS; u++) {
						if (u == r || !mlr_recalls[u].recording) continue;
						mlr_recall_rec_stop(u);
						if (mlr_recalls[u].has_data)
							finalized_other = true;
					}
					if (finalized_other)
						mlr_scene_save_start();

					if (mlr_recalls[r].recording) {
						mlr_recall_rec_stop(r);
						if (mlr_recalls[r].has_data)
							mlr_scene_save_start();
					} else if (mlr_recall_active == r) {
						/* pressing active scene: undo to pre-recall state */
						mlr_recall_undo_and_record();
					} else if (!mlr_recalls[r].has_data) {
						mlr_recall_arm(r);
					} else {
						/* has data: recall it */
						mlr_recall_exec_and_record(r);
					}
				}
			}
			else if (col == 1) {
				play_page = PAGE_CUT;
				page_switched = true;
			}
		}
		PERF_UI_SECTION_END(3);

		/* ---- page interaction (always active) ---- */
		PERF_UI_SECTION_START(4);
		if (run_ui_control && !mlr_flushing && !page_switched) {
			if (play_page == PAGE_REC) {
				process_gate_hold();  /* REC play/stop/gate/group-dissolve */
				process_copy_gesture();  /* REC arm/channel copy/paste gesture */
			}
			if (mlr_flushing) {
				/* A copy gesture just committed; suppress any same-event
				 * column-0 record/gate handling while flash copy owns I/O. */
			} else if (play_page == PAGE_REC)
				process_page_rec();
			else
				process_page_cut();
		}
		PERF_UI_SECTION_END(4);

		/* ---- Cut-event-driven CV / pulse outputs ----
		 *   CV1     = chromatic note (S&H) + live X-knob offset
		 *   CV2     = ASR envelope, gated by output-enabled cut keys
		 *   Pulse1  = 20 ms trigger countdown, fires on output-enabled cut events
		 *   Pulse2  = gate, high while any output-enabled CUT-page track row is held
		 * CV1 / CV2 / Pulse1 / Pulse2 all mirror through pattern/recall playback
		 * via mlr_event_playback_hook. */

		/* Gate detection + CV1 refresh at UI-control rate (~3 kHz, 333 µs
		 * latency). Gate drives PulseOut2 and LED 5; CV1 only needs UI-tick
		 * rate since DAC holds the value between writes — saves the
		 * per-sample MIDIToDAC math. */
		if (run_ui_control) {
			bool new_live_gate = false;
			if (play_page == PAGE_CUT) {
				for (int t = 0; t < MLR_NUM_TRACKS; t++) {
					if (track_outputs_enabled(t) && grid.heldRowMask((uint8_t)(t + 1))) {
						new_live_gate = true;
						break;
					}
				}
			}
			if (!new_live_gate) {
				for (int t = 0; t < MLR_NUM_TRACKS; t++) {
					if (track_outputs_enabled(t) && mlr_gate_mode[t] && mlr_tracks[t].playing) {
						new_live_gate = true;
						break;
					}
				}
			}
			/* Held cut keys on bottom-half "extra" rows gate Pulse2 the
			 * same as held cut keys on (empty) top-half track rows.
			 * Page-agnostic: bottom-half is a free keyboard layer. */
			if (!new_live_gate && extra_rows_any_held())
				new_live_gate = true;
			live_gate_high_ = new_live_gate;
			bool new_gate = live_gate_high_ || replay_gate_mask_ != 0;
			if (!new_gate && env_gate_high_)
				release_cv2_envelope();
			gate_high_ = new_gate;

			int x_knob = (int)x_knob_filtered_;  /* 0..4095, LPF'd */
			int x_off  = ((x_knob - 2048) * CV_X_OFFSET_SEMITONES) / 2048;
			int note   = (int)cv_step_base_midi_ + x_off;
			if (note < 0)   note = 0;
			if (note > 127) note = 127;
			CVOut1MIDINote((uint8_t)note);
		}

		(void)update_cv2_envelope_output();

		if (cv_pulse1_remaining_ > 0) {
			cv_pulse1_remaining_--;
			PulseOut1(true);
		} else {
			PulseOut1(false);
		}
		PulseOut2(gate_high_);

		/* ---- mix and output ---- */
		PERF_UI_SECTION_START(5);
		int knob_main_raw = KnobVal(Knob::Main);
		if (master_knob_last_raw_ < 0) master_knob_last_raw_ = knob_main_raw;
		if (!master_knob_takeover_armed_) {
			if (knob_main_raw != master_knob_last_raw_) {
				mlr_master_level_raw = (uint16_t)knob_main_raw;
				master_knob_arm_raw_ = knob_main_raw;
				mlr_master_override = true;  /* knob moved, suppress pattern master */
			}
		} else {
			int diff = knob_main_raw - master_knob_arm_raw_;
			if (diff < 0) diff = -diff;
			if (diff > KNOB_HARD_TAKEOVER_THRESHOLD) {
				master_knob_takeover_armed_ = false;
				mlr_master_level_raw = (uint16_t)knob_main_raw;
				master_knob_arm_raw_ = knob_main_raw;
				mlr_master_override = true;  /* knob takeover, suppress pattern master */
			}
		}
		master_knob_last_raw_ = knob_main_raw;

		uint8_t master_level = (uint8_t)(mlr_master_level_raw >> 4);
		int16_t mix_r;
		int16_t mix = mlr_play_mix_dual(&mix_r);
		int32_t outL = (int32_t)mix;
		int32_t outR = (int32_t)mix_r;
		if (dry_level > 0 && (rec_armed_track >= 0 || sw == Switch::Up)) {
			/* Latch channel/volume while the monitor is active so the
			 * fade-out tail keeps using the right routing/gain after the
			 * armed track or switch state changes. */
			int mon_track = (mlr_rec_track >= 0) ? mlr_rec_track : rec_armed_track;
			if (mon_track >= 0) {
				mon_vol_last_ = mlr_tracks[mon_track].volume_frac;
				mon_ch_last_  = mlr_tracks[mon_track].recorded_channel;
			}
			uint16_t next = (uint16_t)(mon_fade_frac_ + MLR_MON_FADE_STEP);
			mon_fade_frac_ = (next > 256u) ? 256u : next;
		} else if (mon_fade_frac_ > 0) {
			mon_fade_frac_ = (mon_fade_frac_ <= MLR_MON_FADE_STEP)
				? 0u
				: (uint16_t)(mon_fade_frac_ - MLR_MON_FADE_STEP);
		}

		if (mon_fade_frac_ > 0) {
			int32_t in_scaled = ((int32_t)dry_in * (int32_t)dry_level) >> 8;
			in_scaled = (in_scaled * (int32_t)mon_vol_last_) >> 8;
			in_scaled = (in_scaled * (int32_t)mon_fade_frac_) >> 8;
			if (mon_ch_last_ == 1) outR += in_scaled;
			else                   outL += in_scaled;
		}
		outL = (outL * (int32_t)master_level) >> 8;
		outR = (outR * (int32_t)master_level) >> 8;
		if (outL >  2047) outL =  2047;
		if (outL < -2048) outL = -2048;
		if (outR >  2047) outR =  2047;
		if (outR < -2048) outR = -2048;
		AudioOut1((int16_t)outL);
		AudioOut2((int16_t)outR);
		PERF_UI_SECTION_END(5);

		/* ---- tick pattern playback (~1 ms resolution) ---- */
		PERF_UI_SECTION_START(6);
		if (!mlr_flushing || mlr_copying) {
			mlr_recall_task();
			mlr_recall_check_active_match();
			pat_counter++;
			if (pat_counter >= PAT_TICK_INTERVAL) {
				pat_counter = 0;
				pattern_clock_ms_++;
				mlr_clock_set_ms(pattern_clock_ms_);
				mlr_pattern_tick(pattern_clock_ms_);
			}
		}
		PERF_UI_SECTION_END(6);

		/* ---- update LEDs (sub-sampled) ---- */
		PERF_UI_SECTION_START(7);
		if (!mlr_flushing || mlr_copying) {
			led_counter++;
			if (led_counter >= LED_UPDATE_INTERVAL) {
				led_counter = 0;
				rec_blink = (rec_blink + 1) & 15;
				gate_pulse++;

				/* Panel LED layout:
				 *   LED 0 = input  level     LED 1 = output level
				 *   LED 2 = CV1    level     LED 3 = CV2    level
				 *   LED 4 = Pulse1 trigger   LED 5 = Pulse2 gate
				 * LEDs 0/1: just |sample| at this LED tick, scaled to brightness.
				 * Single-sample snapshot — simple and cheap; the LED's slow refresh
				 * naturally averages out per-tick variation. */
				{
					int32_t in_abs = dry_in < 0 ? -dry_in : dry_in;
					uint32_t v = (uint32_t)in_abs << 1;
					if (v > 4095u) v = 4095u;
					panel_vu_led_[0] = (uint16_t)v;
				}
				{
					int32_t l = outL < 0 ? -outL : outL;
					int32_t r = outR < 0 ? -outR : outR;
					int32_t out_abs = (l > r) ? l : r;
					uint32_t v = (uint32_t)out_abs << 1;
					if (v > 4095u) v = 4095u;
					panel_vu_led_[1] = (uint16_t)v;
				}
				{
					/* CV1 brightness tracks the live note (with X-knob offset)
					 * so the LED moves as you transpose, not just on cuts. */
					int x_knob = (int)x_knob_filtered_;
					int x_off  = ((x_knob - 2048) * CV_X_OFFSET_SEMITONES) / 2048;
					int note   = (int)cv_step_base_midi_ + x_off;
					if (note < 0)   note = 0;
					if (note > 127) note = 127;
					panel_vu_led_[2] = (uint16_t)((note * 4095) / 127);
				}
				{
					int32_t v = env_val_q16_ >> 15;  /* 0..(CV_ENV_PEAK<<1) ~= 0..4094 */
					if (v < 0)    v = 0;
					if (v > 4095) v = 4095;
					panel_vu_led_[3] = (uint16_t)v;
				}
				panel_vu_led_[4] = pulse1_led_latch_ ? 4095u : 0u;
				pulse1_led_latch_ = false;
				panel_vu_led_[5] = gate_high_ ? 4095u : 0u;
				__dmb();
				panel_vu_led_pending_ = true;

				/* Track grid size after ready; some controllers answer size late. */
				if (grid.ready()) {
#if FORCE_8X8_GRID_LAYOUT
					bool detected_small_grid = true;
#else
					bool detected_small_grid = (grid.cols() <= 8);
#endif
					if (!grid_size_latched_ || small_grid_ != detected_small_grid) {
						small_grid_ = detected_small_grid;
						grid_size_latched_ = true;
						grid_redraw_phase_ = -1;
					}
				}

				if (grid_redraw_phase_ < 0) {
					grid_render_rec_pos_ = (sw == Switch::Up || sw == Switch::Down);
					grid_render_delete_held_ = delete_held();
					grid_render_delete_action_held_ = delete_action_held();
					grid_redraw_page_ = play_page;
					grid_redraw_flushing_ = (mlr_flushing && mlr_copying);
					__dmb();
					grid_redraw_phase_ = 0;
				}
			}
		}
		PERF_UI_SECTION_END(7);
#undef PERF_UI_SECTION_START
#undef PERF_UI_SECTION_END

		/* ---- detect flush completion: stop gated tracks from auto-playing ---- */
		if (was_flushing_ && !mlr_flushing && prev_rec_track >= 0) {
			if (mlr_gate_mode[prev_rec_track] && mlr_tracks[prev_rec_track].playing) {
				mlr_stop_track(prev_rec_track);
			}
			rec_start_lockout_samples_ = RECORD_REARM_DELAY_SAMPLES;
		}
		was_flushing_ = mlr_flushing;

		if (mlr_rec_track >= 0) prev_rec_track = mlr_rec_track;
	}

private:
	/** Read the switch position directly via ADC (safe before Run()/ISR).
	 *  The switch is on the analog mux at position 3 (MX_A=1, MX_B=1),
	 *  read through MUX_IO_1 (ADC input 2, GPIO 28). */
	static Switch read_switch_direct()
	{
		/* Set mux to position 3: A=1, B=1 */
		gpio_put(24, true);   /* MX_A */
		gpio_put(25, true);   /* MX_B */
		sleep_ms(2);          /* settle time for mux + RC filter */

		/* Do a single blocking ADC read on channel 2 (GPIO 28 = MUX_IO_1) */
		adc_select_input(2);
		uint16_t raw = adc_read();  /* 12-bit: 0–4095 */

		return static_cast<Switch>((raw > 1000) + (raw > 3000));
	}

	MonomeGrid grid;
	uint32_t   led_counter;
	uint32_t   pat_counter;
	uint32_t   pattern_clock_ms_ = 0;
	uint8_t    ui_ctrl_div_ = 0;
	int        play_page;
	Mode       mode_;
	uint16_t   rec_speed_accum;
	bool       rec_limit_latched;
	int        prev_rec_track = -1;  /* last track that was recording */
	uint8_t    rec_blink = 0;        /* blink counter for record indicator */
	int        rec_armed_track = -1; /* armed track for recording (-1 = none) */
	int        resume_after_arm_track_ = -1; /* track auto-stopped due to arm; resume on disarm */
	bool       rec_gated = false;    /* true during press-to-record (gated) */
	bool       rec_pulse_gated_ = false; /* true while recording was started by Pulse In 1 rising edge */
	uint32_t   rec_auto_stop_samples_ = MLR_MAX_SAMPLES; /* overwrite records stop at previous length */
	int        master_knob_arm_raw_ = -1;
	int        master_knob_last_raw_ = -1;
	bool       master_knob_takeover_armed_ = false;
	uint32_t   rec_start_lockout_samples_ = 0;
	bool       cv_attack_switch_down_prev_ = false;
	bool       was_flushing_ = false; /* edge detect for flush completion */
	uint32_t   play_col_hold_time[MLR_NUM_TRACKS] = {};  /* sample counter for play-col hold */
	bool       play_col_armed[MLR_NUM_TRACKS] = {};  /* true while play-col is held */
	uint8_t    armed_mask_ = 0;                       /* bit t set iff play_col_armed[t] */
	bool       gate_entered[MLR_NUM_TRACKS] = {}; /* true once hold crossed threshold */
	bool       small_grid_ = false;  /* true when connected grid is 8x8 or smaller */
	bool       grid_size_latched_ = false;  /* true once grid size has been detected */
	volatile int8_t grid_redraw_phase_ = -1;  /* core 1 slices one grid redraw outside ProcessSample */
	volatile int    grid_redraw_page_ = PAGE_REC;
	volatile bool   grid_redraw_flushing_ = false;
	volatile bool   grid_render_rec_pos_ = false;
	volatile bool   grid_render_delete_held_ = false;
	volatile bool   grid_render_delete_action_held_ = false;
	volatile uint16_t panel_vu_led_[6] = {};
	volatile bool   panel_vu_led_pending_ = false;
	uint16_t   gate_pulse = 0;       /* free-running counter for gate LED pulse */
#ifdef MLR_PERF_PROFILING
	uint8_t    perf_ui_section_probe_ = 0;
#endif

	/* Track-group gesture and visual-feedback state. */
	uint8_t    group_gesture_mask_ = 0;           /* tracks participating in current multi-hold gesture */
	bool       group_gesture_committed_ = false;  /* true after first release in a multi-hold gesture */
	uint8_t    group_flash_mask_ = 0;             /* tracks whose play LED is currently flashing */
	uint32_t   group_flash_samples_remaining_ = 0;  /* countdown for confirmation blink */
	uint16_t   group_flash_period_ = GROUP_FLASH_CREATE_PERIOD;  /* current blink half-period */
	int        copy_source_track_ = -1;           /* source for arm/channel copy gesture */
	int        copy_source_column_ = 0;           /* arm/channel column where source press began */
	uint8_t    copy_participant_mask_ = 0;        /* arm/channel keys suppressed by copy gesture */
	uint8_t    copy_target_mask_ = 0;             /* empty tracks selected as copy destinations */
	bool       copy_gesture_committed_ = false;
	bool       copy_gesture_touched_other_ = false;
	bool       copy_arm_suppress_event_ = false;
	uint8_t    copy_flash_mask_ = 0;              /* tracks whose record/copy LED is flashing */
	uint32_t   copy_flash_samples_remaining_ = 0;
	uint16_t   copy_flash_period_ = COPY_FLASH_PERIOD;
	uint8_t    gated_choke_pause_mask_[MLR_NUM_TRACKS] = {};  /* tracks paused while gated keys are held */
	bool       cut_loop_pending_[MLR_NUM_TRACKS] = {};  /* non-gated CUT loop commits on first release */
	int        cut_loop_pending_start_[MLR_NUM_TRACKS] = {};
	int        cut_loop_pending_end_[MLR_NUM_TRACKS] = {};
	uint64_t   empty_keyboard_linger_until_us_[MLR_NUM_TRACKS] = {};
	uint8_t    empty_keyboard_linger_col_[MLR_NUM_TRACKS] = {};
	uint8_t    empty_pattern_key_col_[MLR_NUM_TRACKS] = {};
	bool       empty_pattern_key_active_[MLR_NUM_TRACKS] = {};
	uint32_t   empty_pattern_key_until_us_[MLR_NUM_TRACKS] = {};
	/* Bottom-half rows on a 16-row grid: free-running keyboard overlay,
	 * without any associated track, audio playback, or pattern recording. */
	uint8_t    extra_keyboard_state_ = (EXTRA_GUIDE_ENABLED | EXTRA_PATTERN_KEY_NONE);
	/* Cut-event-driven CV / pulse outputs (fire on manual cuts and on
	 * pattern/recall playback via mlr_event_playback_hook). */
	uint32_t   cv_pulse1_remaining_ = 0;   /* PulseOut1 trigger countdown (samples) */
	bool       pulse1_led_latch_ = false;  /* latch PulseOut1 events for LED visibility (cleared each LED frame) */
	int8_t     cv_step_base_midi_ = CV_NOTE_BASE_MIDI;  /* CV1 S&H base note from last cut col */
	int32_t    x_knob_filtered_ = 2048;    /* one-pole LPF of Knob::X (sample-rate) to suppress
	                                          ADC dither flicker at semitone/gain boundaries */
	/* CV2 envelope: attack to peak, sustain while gated, then linear release
	 * to 0. env_val_q16_ holds the value in Q16. Segment steps are latched
	 * so knob changes do not disturb an in-flight segment. */
	static constexpr uint8_t ENV_IDLE = 0;
	static constexpr uint8_t ENV_ATTACK = 1;
	static constexpr uint8_t ENV_SUSTAIN = 2;
	static constexpr uint8_t ENV_RELEASE = 3;
	uint8_t    env_stage_ = ENV_IDLE;
	int32_t    env_val_q16_ = 0;           /* 0 .. CV_ENV_PEAK << 16 */
	int32_t    env_attack_step_q16_ = 0;   /* per-sample add, Q16 */
	int32_t    env_release_step_q16_ = 0;  /* per-sample subtract, Q16 */
	uint32_t   cv_attack_samples_ = CV_ATTACK_MIN_SAMPLES;
	bool       env_gate_high_ = false;
	uint32_t   gl_turing_rng_state_ = 0x6D2B79F5u;
	/* PulseOut2 / LED 5 gate: high while any output-enabled CUT-page track
	 * row key is held (independent of envelope state — useful for VCA gates / mutes). */
	bool       gate_high_ = false;
	bool       live_gate_high_ = false;
	uint8_t    replay_gate_mask_ = 0;
	uint32_t   delete_reset_hold_samples_ = 0;    /* continuous DELETE hold duration */
	uint32_t   delete_reset_flash_samples_remaining_ = 0;  /* confirmation flash countdown */
	bool       delete_reset_fired_ = false;       /* require DELETE release before another reset */
	uint16_t   mon_fade_frac_ = 0;   /* monitor fade-in envelope, 0..256 */
	uint16_t   mon_vol_last_  = 256; /* last-known armed/rec track volume_frac */
	uint8_t    mon_ch_last_   = 0;   /* last-known armed/rec track recorded_channel */
	inline static Mode s_mode_ = Mode::HostMLR;

	/* Cross-core volatile state for DeviceGridless ↔ DeviceSampleMgr transitions.
	 * Written by core 1, read by core 0. */
	inline static volatile bool s_sample_mgr_active_ = false;
	inline static volatile bool s_rescan_needed_ = false;
public:
	inline static MLRCard *s_card_ = nullptr;
private:

	/* ---- Gridless mode state ---- */
	int        gl_active_track_ = -1;    /* currently selected/playing track index */
	int        gl_num_populated_ = 0;    /* number of tracks with content */
	int        gl_populated_[MLR_NUM_TRACKS] = {};  /* indices of populated tracks */
	int        gl_prev_knob_track_ = -1; /* previous track from knob, for hysteresis */
	int        gl_prev_start_col_ = 0;   /* last accepted start column (0..15) */
	bool       gl_prev_switch_down_ = false; /* edge detect for switch-down reset */
	int        gl_locked_track_ = -1;     /* locked track while switch is Up */
	Switch     gl_prev_switch_mode_ = Switch::Middle;
	uint16_t   gl_base_speed_frac_[MLR_NUM_TRACKS] = {256, 256, 256, 256, 256, 256};
	uint16_t   gl_playback_gain_frac_ = 256; /* playback-layer gain, Q8: 256 = unity */
	uint16_t   gl_radiate_frac_ = 0;      /* global-mode radiate amount, Q8: 0..256 */
	uint16_t   gl_playback_track_gain_frac_[MLR_NUM_TRACKS] = {}; /* per-track playback layer gain */
	bool       gl_mix_cache_valid_ = false;
	int        gl_mix_cache_active_track_ = -1;
	bool       gl_mix_cache_record_mode_ = false;
	bool       gl_mix_cache_recording_active_ = false;
	uint16_t   gl_mix_cache_playback_gain_frac_ = 0;
	uint16_t   gl_mix_cache_radiate_frac_ = 0;
	uint8_t    gl_mix_cache_volume_slot_[MLR_NUM_TRACKS] = {};
	enum class GridlessMixMode {
		HardCutSolo,
	};
	GridlessMixMode gl_mix_mode_ = GridlessMixMode::HardCutSolo;
	uint32_t   gl_led_counter_ = 0;      /* sub-sample counter for LED updates */
	uint16_t   gl_led_pulse_ = 0;        /* free-running pulse counter for LEDs */
	bool       knob_hard_takeover_armed_[3] = {false, false, false};
	int        knob_hard_takeover_anchor_[3] = {0, 0, 0};
	uint8_t    gl_ctrl_div_ = 0;              /* control-rate divider */
	uint8_t    gl_start_maint_div_ = 0;       /* all-track restart maintenance divider */
	bool       gl_pulse1_pending_ = false;    /* latched PulseIn1 edge between control ticks */
	bool       gl_pulse2_pending_ = false;    /* latched PulseIn2 edge between control ticks */
	uint16_t   gl_turing_probability_raw_ = 0; /* Main knob, sampled at gridless control rate */
	uint8_t    gl_turing_range_semitones_ = 0; /* X knob: 1 note .. 3 octaves before major quantize */
	uint8_t    gl_turing_length_ = 6;          /* Y knob: 3 .. 8 active cells */
	uint8_t    gl_turing_cells_[8] = {0, 1, 2, 3, 0, 1, 2, 3}; /* BYO_Benjolin-style 2-bit cells */
	uint8_t    gl_turing_cv_midi_ = CV_NOTE_BASE_MIDI;
	bool       gl_turing_cv_dirty_ = true;
	bool       gl_turing_clocked_ = false;
	uint32_t   gl_wrap_pulse_remaining_ = 0;  /* PulseOut1: 20 ms when any playing track wraps */
	uint32_t   gl_env_end_pulse_remaining_ = 0; /* PulseOut2: 20 ms when CV2 envelope reaches rest */
	bool       gl_prev_switch_down_sample_ = false;
	bool       gl_switch_down_rise_pending_ = false;
	bool       gl_switch_down_fall_pending_ = false;
	uint32_t   gl_switch_down_hold_samples_ = 0;
	bool       gl_record_hold_active_ = false;
	bool       gl_record_mode_ = false;
	bool       gl_record_mode_wait_release_ = false;
	bool       gl_recording_active_ = false;
	int        gl_record_track_ = -1;
	bool       gl_post_record_start_pending_ = false;
	int        gl_post_record_start_track_ = -1;
	bool       gl_audio1_mod_rearm_required_ = false;
	bool       gl_audio2_mod_rearm_required_ = false;
	bool       gl_audio1_seen_disconnect_ = false;
	bool       gl_audio2_seen_disconnect_ = false;

	/* ---- 8x8 grid layout helpers ---- */

	/** Current physical grid width. */
	int grid_width() const { return small_grid_ ? 8 : 16; }
	/** Play/stop/gate column (rightmost on grid). */
	int play_col() const { return small_grid_ ? 7 : 15; }
	/** ALT/delete column on row 0. */
	int alt_col() const { return small_grid_ ? 7 : 14; }
	/** Number of CUT scrub columns. CUT track rows use the full grid width. */
	int cut_cols() const { return grid_width(); }
	/** First cut column index on the grid. */
	int cut_col_start() const { return 0; }
	/** Last cut column index on the grid. */
	int cut_col_end() const { return grid_width() - 1; }
	/** First pattern button column on row 0. */
	int pat_col_start() const { return small_grid_ ? 3 : 4; }
	/** First recall button column on row 0. */
	int recall_col_start() const { return 9; /* only on 16-wide */ }

	/** True while the row-0 DELETE/ALT key is held. */
	bool delete_held() const { return grid.held((uint8_t)alt_col(), 0); }

	/** True while DELETE can modify another key; false after long-hold reset fires. */
	bool delete_action_held() const { return delete_held() && !delete_reset_fired_; }

	/** Map a full-width CUT grid column to internal column (0-15). */
	int cut_grid_to_internal(int grid_col) const {
		int max_grid_col = cut_cols() - 1;
		if (grid_col < 0) grid_col = 0;
		if (grid_col > max_grid_col) grid_col = max_grid_col;
		return (grid_col * (MLR_GRID_COLS - 1) + (max_grid_col / 2)) / max_grid_col;
	}

	/** Map an internal column (0-15) to a full-width CUT display column. */
	int cut_internal_to_grid(int internal_col) const {
		if (internal_col < 0) internal_col = 0;
		if (internal_col >= MLR_GRID_COLS) internal_col = MLR_GRID_COLS - 1;
		int max_grid_col = cut_cols() - 1;
		return (internal_col * max_grid_col + ((MLR_GRID_COLS - 1) / 2)) / (MLR_GRID_COLS - 1);
	}

	static constexpr uint8_t KEY_GUIDE_LEVEL = 2;
	static constexpr uint8_t KEY_OCTAVE_LEVEL = 6;
	static constexpr uint8_t KEY_HELD_LEVEL = 12;

	int keyboard_local_row(int row) const
	{
		return row & 7;
	}

	int keyboard_midi_from_internal(int internal_col, int row) const
	{
		if (internal_col < 0) internal_col = 0;
		if (internal_col >= MLR_GRID_COLS) internal_col = MLR_GRID_COLS - 1;
		int local_row = keyboard_local_row(row);
		return 60 + (internal_col - 8) + 5 * (4 - local_row) + ((local_row + 5) >> 3) - 3;
	}

	uint8_t keyboard_guide_level(int internal_col, int row) const
	{
		int pitch_class = keyboard_midi_from_internal(internal_col, row) % 12;
		if (pitch_class == 4) return KEY_OCTAVE_LEVEL;
		return ((0xB5Au >> pitch_class) & 1u) ? KEY_GUIDE_LEVEL : 0;
	}

	bool extra_keyboard_guide_enabled() const
	{
		return (extra_keyboard_state_ & EXTRA_GUIDE_ENABLED) != 0;
	}

	bool track_outputs_enabled(int track) const
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return false;
		return !mlr_tracks[track].has_content || mlr_tracks[track].cv1_pitch_enabled;
	}

	void grid_frame_led_max(int x, int y, uint8_t level)
	{
		if (x < 0 || y < 0 || x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return;
		if (grid.ledGet((uint8_t)x, (uint8_t)y) < level)
			grid.frameLedUnchecked(x, y, level);
	}

	bool pattern_playing_empty_track_pitch(int track) const
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return false;
		for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
			if (mlr_patterns[p].state != MLR_PAT_PLAYING) continue;
			for (uint16_t i = 0; i < mlr_patterns[p].count; i++) {
				const mlr_event_t *e = &mlr_patterns[p].events[i];
				if ((e->type == MLR_EVT_CUT || e->type == MLR_EVT_GROUP_CUT) && e->track == (uint8_t)track)
					return true;
			}
		}
		return false;
	}

	void draw_empty_track_keyboard_overlay()
	{
		uint64_t now64 = time_us_64();
		uint32_t now32 = time_us_32();
		int cs = cut_col_start();
		int ce = cut_col_end();
		uint16_t zone_mask = (uint16_t)(((1u << (ce - cs + 1)) - 1u) << cs);
		bool show_guide = false;

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].has_content) continue;
			uint16_t held = (uint16_t)(grid.heldRowMask((uint8_t)(t + 1)) & zone_mask);
			if (held)
				empty_keyboard_linger_until_us_[t] = now64 + EMPTY_KEYBOARD_LINGER_US;

			bool pattern_keyboard_active = pattern_playing_empty_track_pitch(t);
			if (empty_pattern_key_active_[t] && empty_pattern_key_until_us_[t] != 0 &&
				(int32_t)(now32 - empty_pattern_key_until_us_[t]) >= 0)
				empty_pattern_key_active_[t] = false;
			if (empty_pattern_key_active_[t] && empty_pattern_key_until_us_[t] == 0 && !pattern_keyboard_active)
				empty_pattern_key_active_[t] = false;

			if (now64 < empty_keyboard_linger_until_us_[t] || empty_pattern_key_active_[t])
				show_guide = true;
		}

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].has_content) continue;
			int row = t + 1;
			if (show_guide) {
				for (int internal = 0; internal < MLR_GRID_COLS; internal++) {
					uint8_t level = keyboard_guide_level(internal, row);
					if (level)
						grid_frame_led_max(cut_internal_to_grid(internal), row, level);
				}
			}
			int held_col = cut_internal_to_grid(empty_keyboard_linger_col_[t] & 0x0F);
			if (grid.held((uint8_t)held_col, (uint8_t)row))
				grid_frame_led_max(held_col, row, KEY_HELD_LEVEL);
			if (empty_pattern_key_active_[t])
				grid_frame_led_max(cut_internal_to_grid(empty_pattern_key_col_[t]), row, KEY_HELD_LEVEL);
		}
	}

	/** Map 8x8 REC speed column (2-6) to speed_shift. */
	static constexpr int8_t kSmallSpeedShifts[5] = { -3, -2, 0, 2, 3 };

	int speed_col_to_shift_small(int col) const {
		int idx = col - 2;
		if (idx < 0) idx = 0;
		if (idx > 4) idx = 4;
		return kSmallSpeedShifts[idx];
	}

	/** Map a speed_shift to an 8x8 REC display column. */
	int speed_shift_to_col_small(int shift) const {
		for (int i = 0; i < 5; i++) {
			if (kSmallSpeedShifts[i] == shift) return i + 2;
		}
		/* fallback: find nearest */
		int best = 2;
		int best_d = 99;
		for (int i = 0; i < 5; i++) {
			int d = shift - kSmallSpeedShifts[i];
			if (d < 0) d = -d;
			if (d < best_d) { best_d = d; best = i + 2; }
		}
		return best;
	}

	int knob_index(Knob knob)
	{
		switch (knob) {
		case Knob::Main: return 0;
		case Knob::X:    return 1;
		case Knob::Y:    return 2;
		}
		return -1;
	}

	void knob_hard_takeover_arm(Knob knob)
	{
		int i = knob_index(knob);
		if (i < 0) return;
		knob_hard_takeover_armed_[i] = true;
		knob_hard_takeover_anchor_[i] = KnobVal(knob);
	}

	bool knob_hard_takeover_accept(Knob knob, int raw, int threshold = KNOB_HARD_TAKEOVER_THRESHOLD)
	{
		int i = knob_index(knob);
		if (i < 0) return true;
		if (!knob_hard_takeover_armed_[i]) return true;

		int diff = raw - knob_hard_takeover_anchor_[i];
		if (diff < 0) diff = -diff;
		if (diff <= threshold) return false;

		knob_hard_takeover_armed_[i] = false;
		return true;
	}

	/* ================================================================ */
	/* Gridless mode                                      */
	/* ================================================================ */

	/** Scan mlr_tracks[] and build the populated track list. */
	void gridless_scan_tracks()
	{
		gl_num_populated_ = 0;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].has_content) {
				gl_populated_[gl_num_populated_++] = t;
			}
		}
	}

	int gridless_track_idx_from_knob(int knob_main) const
	{
		if (knob_main < 0) knob_main = 0;
		if (knob_main > 4095) knob_main = 4095;
		int track_idx = (knob_main * MLR_NUM_TRACKS) >> 12;  /* 0..5 */
		if (track_idx < 0) track_idx = 0;
		if (track_idx >= MLR_NUM_TRACKS) track_idx = MLR_NUM_TRACKS - 1;
		return track_idx;
	}

	void gridless_restart_all_tracks_from_zero()
	{
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (mlr_tracks[t].has_content)
				mlr_cut(t, 0);
		}
	}

	void gridless_start_all_tracks_if_needed()
	{
		bool any_waiting_to_stop = false;
		bool any_stopped = false;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			if (!mlr_tracks[t].has_content) continue;
			if (mlr_tracks[t].stop_pending) any_waiting_to_stop = true;
			else if (!mlr_tracks[t].playing) any_stopped = true;
		}
		if (any_stopped && !any_waiting_to_stop)
			gridless_restart_all_tracks_from_zero();
	}

	void gridless_apply_level_layers()
	{
		/* Mirrors kVolFrac in mlr.c — keep these tables in sync. */
		static const uint16_t kSlotFrac[MLR_NUM_VOL_SLOTS] = {
			362,  /* slot 0: +3 dB */
			256,  /* slot 1: unity */
			128,  /* slot 2: -6 dB */
			64,   /* slot 3: -12 dB */
			0,    /* slot 4: silent */
		};

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			mlr_track_t *tr = &mlr_tracks[t];
			uint8_t slot = tr->volume_slot;
			if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
			uint32_t level = kSlotFrac[slot];

			/* Playback layer gain is per-track (solo/radiate matrix). */
			level = (level * (uint32_t)gl_playback_track_gain_frac_[t]) >> 8;

			if (level > 1024u) level = 1024u;
			tr->volume_target = (uint16_t)level;
			if (!tr->playing)
				tr->volume_frac = tr->volume_target;
		}
	}

	static bool gridless_update_u16_hysteresis(uint16_t *dst, uint16_t value, uint16_t threshold)
	{
		int delta = (int)value - (int)*dst;
		if (delta < 0) delta = -delta;
		if (delta < (int)threshold) return false;
		*dst = value;
		return true;
	}

	bool gridless_mix_inputs_changed()
	{
		bool changed = !gl_mix_cache_valid_ ||
			gl_mix_cache_active_track_ != gl_active_track_ ||
			gl_mix_cache_record_mode_ != gl_record_mode_ ||
			gl_mix_cache_recording_active_ != gl_recording_active_ ||
			gl_mix_cache_playback_gain_frac_ != gl_playback_gain_frac_ ||
			gl_mix_cache_radiate_frac_ != gl_radiate_frac_;

		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint8_t slot = mlr_tracks[t].volume_slot;
			if (!gl_mix_cache_valid_ || gl_mix_cache_volume_slot_[t] != slot) {
				changed = true;
				gl_mix_cache_volume_slot_[t] = slot;
			}
		}

		if (changed) {
			gl_mix_cache_valid_ = true;
			gl_mix_cache_active_track_ = gl_active_track_;
			gl_mix_cache_record_mode_ = gl_record_mode_;
			gl_mix_cache_recording_active_ = gl_recording_active_;
			gl_mix_cache_playback_gain_frac_ = gl_playback_gain_frac_;
			gl_mix_cache_radiate_frac_ = gl_radiate_frac_;
		}

		return changed;
	}

	void gridless_apply_mix_matrix(bool switch_middle)
	{
		(void)switch_middle;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			gl_playback_track_gain_frac_[t] = 0;
			mlr_tracks[t].muted = false;
		}

		if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS)
			return;

		if (gl_record_mode_ || gl_recording_active_) {
			gl_playback_track_gain_frac_[gl_active_track_] = 256u;
			return;
		}

		switch (gl_mix_mode_) {
		case GridlessMixMode::HardCutSolo:
		default:
			/* Normal playback: always use radiate matrix.
			 * Record mode/recording above enforces selected-track solo. */
			uint32_t spread = (uint32_t)gl_radiate_frac_ * MLR_NUM_TRACKS;  /* 0..1792 */
			for (int t = 0; t < MLR_NUM_TRACKS; t++) {
				int d = t - gl_active_track_;
				if (d < 0) d = -d;
				uint32_t w = 0;
				if (d == 0) {
					w = 256;
				} else {
					int32_t raw = (int32_t)spread - (int32_t)(d * 256);
					if (raw > 256) raw = 256;
					if (raw < 0) raw = 0;
					w = (uint32_t)raw;
				}
				gl_playback_track_gain_frac_[t] = (uint16_t)((w * (uint32_t)gl_playback_gain_frac_) >> 8);
			}
			break;
		}

		/* Gridless keeps populated tracks running in phase; the mix matrix only
		 * controls audibility. */
	}

	/**
	 * Gridless ProcessSample
	 *
	 * Global mode (switch middle):
	 *   Knob Main = channel select + CV1 Turing probability
	 *   Knob X    = playback layer gain (unity at center) + CV1 range
	 *   Knob Y    = radiate amount (left=selected only, right=all channels)
	 * Channel mode (switch up):
	 *   Knob Main = bipolar speed control + CV1 Turing probability
	 *     center = minimum speed, left = reverse, right = forward
	 *     edges map to ±2 octaves
	 *   Knob X    = per-channel level (grid mixer style) + CV1 range
	 *   Knob Y    = reset start position (column 0–15)
	 * Switch Down / Pulse In 1 rising edge = reset to start position
	 * Pulse In 1/2 rising edge = clock CV1 Turing output + trigger CV2 envelope
	 * Pulse In 2 also advances the active track
	 * Hold Switch Down ~2 s to arm recording onto the active track
	 * (switch Down again to start, Down release to stop).
	 */
	bool __not_in_flash("gridless") gridless_process_sample()
	{
		/* Latch external pulse edges at full sample rate; consume at control rate. */
		if (PulseIn1RisingEdge()) gl_pulse1_pending_ = true;
		if (PulseIn2RisingEdge()) gl_pulse2_pending_ = true;

		/* After a recording pass, require per-input disconnect/reconnect before
		 * re-enabling AudioIn modulation. */
		bool a1_connected = Connected(Input::Audio1);
		bool a2_connected = Connected(Input::Audio2);
		if (gl_audio1_mod_rearm_required_) {
			if (!a1_connected) gl_audio1_seen_disconnect_ = true;
			if (a1_connected && gl_audio1_seen_disconnect_) {
				gl_audio1_mod_rearm_required_ = false;
				gl_audio1_seen_disconnect_ = false;
			}
		}
		if (gl_audio2_mod_rearm_required_) {
			if (!a2_connected) gl_audio2_seen_disconnect_ = true;
			if (a2_connected && gl_audio2_seen_disconnect_) {
				gl_audio2_mod_rearm_required_ = false;
				gl_audio2_seen_disconnect_ = false;
			}
		}

		/* Latch switch-down edges at full sample rate. */
		int sw_now = SwitchVal();
		bool switch_down_now = (sw_now == Switch::Down);
		if (switch_down_now && !gl_prev_switch_down_sample_) gl_switch_down_rise_pending_ = true;
		if (!switch_down_now && gl_prev_switch_down_sample_) gl_switch_down_fall_pending_ = true;
		gl_prev_switch_down_sample_ = switch_down_now;

		/* Hold switch down 3s to enter record mode. */
		if (!gl_record_mode_ && !gl_recording_active_ && !mlr_flushing && rec_start_lockout_samples_ == 0) {
			if (switch_down_now) {
				if (gl_switch_down_hold_samples_ < GRIDLESS_REC_HOLD_SAMPLES)
					gl_switch_down_hold_samples_++;
				if (gl_switch_down_hold_samples_ >= GRIDLESS_REC_HOLD_SAMPLES) {
					gl_record_mode_ = true;
					gl_record_mode_wait_release_ = true;
					gl_record_hold_active_ = false;
					gl_switch_down_hold_samples_ = 0;
					gl_switch_down_rise_pending_ = false;
				}
			} else {
				gl_switch_down_hold_samples_ = 0;
			}
		}
		if (gl_record_mode_wait_release_ && !switch_down_now)
			gl_record_mode_wait_release_ = false;
		gl_record_hold_active_ = (!gl_record_mode_ && switch_down_now &&
			gl_switch_down_hold_samples_ > 0 && gl_switch_down_hold_samples_ < GRIDLESS_REC_HOLD_SAMPLES);

		/* In record mode (idle), switch Up cancels and returns to normal playback mode
		 * without starting any recording/overwrite. */
		if (gl_record_mode_ && !gl_recording_active_ && sw_now == Switch::Up) {
			gl_record_mode_ = false;
			gl_record_mode_wait_release_ = false;
			gl_switch_down_hold_samples_ = 0;
			gl_switch_down_rise_pending_ = false;
			gl_switch_down_fall_pending_ = false;
		}

		/* Record-mode switch behavior: next switch-down starts recording. */
		if (gl_record_mode_ && !gl_record_mode_wait_release_ && !gl_recording_active_ && !mlr_flushing && rec_start_lockout_samples_ == 0 && gl_switch_down_rise_pending_) {
			gl_switch_down_rise_pending_ = false;
			if (gl_active_track_ >= 0 && gl_active_track_ < MLR_NUM_TRACKS) {
				gl_record_track_ = gl_active_track_;
				/* Auto-pick the input jack: if exactly one of Audio1/Audio2 is
				 * connected, record from that input and set the track's
				 * recorded_channel to match. With both plugged or neither,
				 * default to Audio In 1 / channel 0. */
				bool a1 = Connected(Input::Audio1);
				bool a2 = Connected(Input::Audio2);
				uint8_t ch = (!a1 && a2) ? 1 : 0;
				mlr_tracks[gl_record_track_].recorded_channel = ch;
				mlr_start_record(gl_record_track_);
				rec_speed_accum = 0;
				gl_recording_active_ = true;
			}
		}

		int16_t rec_monitor_l = 0;

		/* Active recording: speed-linked write, auto-stop at max length. */
		if (gl_recording_active_ && gl_record_track_ >= 0 && gl_record_track_ < MLR_NUM_TRACKS) {
			/* Read from whichever input was selected at start-of-record. */
			int16_t dry_in_l = (mlr_tracks[gl_record_track_].recorded_channel == 1)
				? AudioIn2()
				: AudioIn1();
			uint8_t in_gain = (uint8_t)(KnobVal(Knob::X) >> 4);  /* 0..255 */

			int32_t scaled_l = ((int32_t)dry_in_l * (int32_t)in_gain) >> 8;
			if (scaled_l > 2047) scaled_l = 2047;
			if (scaled_l < -2048) scaled_l = -2048;
			rec_monitor_l = (int16_t)scaled_l;

			rec_speed_accum += 256u;  /* fixed 1x record speed in gridless mode */

			bool stop_record = false;
			while (rec_speed_accum >= 256) {
				rec_speed_accum -= 256;
				mlr_record_sample(rec_monitor_l);
				if (mlr_get_rec_progress() >= MLR_MAX_SAMPLES) {
					stop_record = true;
					break;
				}
			}

			if (gl_switch_down_fall_pending_) {
				gl_switch_down_fall_pending_ = false;
				stop_record = true;
			}

			if (stop_record) {
				mlr_stop_record();
				rec_speed_accum = 0;
				gl_recording_active_ = false;
				gl_record_mode_ = false;
				gl_record_mode_wait_release_ = false;
				gl_audio1_mod_rearm_required_ = true;
				gl_audio2_mod_rearm_required_ = true;
				gl_audio1_seen_disconnect_ = false;
				gl_audio2_seen_disconnect_ = false;
				gl_post_record_start_pending_ = true;
				gl_post_record_start_track_ = gl_record_track_;
				gl_record_track_ = -1;
			}
		}
		if (!gl_recording_active_) {
			gl_switch_down_fall_pending_ = false;
		}

		/* Once flush completes, start looping new material from start. */
		if (gl_post_record_start_pending_ && !mlr_flushing) {
			int t = gl_post_record_start_track_;
			if (t >= 0 && t < MLR_NUM_TRACKS && mlr_tracks[t].has_content) {
				gl_active_track_ = t;
				gridless_restart_all_tracks_from_zero();
			}
			rec_start_lockout_samples_ = RECORD_REARM_DELAY_SAMPLES;
			gl_post_record_start_pending_ = false;
			gl_post_record_start_track_ = -1;
		}

		/* Match gridful UI/control rate: 48 kHz / 16 = 3 kHz. */
		const uint8_t kGridlessControlDiv = MAIN_CTRL_DIV;
		gl_ctrl_div_++;
		bool run_control = false;
		if (gl_ctrl_div_ >= kGridlessControlDiv) {
			gl_ctrl_div_ = 0;
			run_control = true;
		}

		if (run_control) {
			/* Maintenance doesn't need control-rate updates. */
			gl_start_maint_div_++;
			if (gl_start_maint_div_ >= 8) {  /* ~1.5 kHz effective */
				gl_start_maint_div_ = 0;
				gridless_start_all_tracks_if_needed();
			}

			int sw = sw_now;
			bool switch_middle = (sw == Switch::Middle);
			bool switch_up = (sw == Switch::Up);
			if (gl_record_mode_ || gl_recording_active_) {
				switch_middle = true;
				switch_up = false;
			}

		/* ---- Switch mode transitions: lock track + re-arm takeover ---- */
		if (!gl_record_mode_ && !gl_recording_active_ && switch_up && gl_prev_switch_mode_ == Switch::Middle) {
			if (gl_active_track_ >= 0 && gl_active_track_ < MLR_NUM_TRACKS) {
				gl_locked_track_ = gl_active_track_;
			} else {
				gl_locked_track_ = gridless_track_idx_from_knob(KnobVal(Knob::Main));
			}
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
		} else if (!gl_record_mode_ && !gl_recording_active_ && switch_middle && gl_prev_switch_mode_ == Switch::Up) {
			knob_hard_takeover_arm(Knob::Main);
			knob_hard_takeover_arm(Knob::X);
			knob_hard_takeover_arm(Knob::Y);
		} else if (!gl_record_mode_ && !gl_recording_active_ && sw == Switch::Down && gl_prev_switch_mode_ != Switch::Down) {
			knob_hard_takeover_arm(Knob::Y);
		}

			/* ---- Track selection from Knob Main ---- */
			int knob_main = KnobVal(Knob::Main);  /* 0–4095 */
			gridless_update_u16_hysteresis(&gl_turing_probability_raw_, (uint16_t)knob_main, 2);
			int new_track = gl_active_track_;
			if (switch_up && gl_locked_track_ >= 0 && gl_locked_track_ < MLR_NUM_TRACKS) {
				new_track = gl_locked_track_;
			} else if (gl_active_track_ < 0 || (switch_middle && knob_hard_takeover_accept(Knob::Main, knob_main))) {
				int track_idx = gridless_track_idx_from_knob(knob_main);
				new_track = track_idx;
			}

			/* Audio In 1 modulation (if connected): channel wrap offset. */
			if (!gl_record_mode_ && !gl_recording_active_ && !gl_audio1_mod_rearm_required_ && Connected(Input::Audio1)) {
				int32_t a1 = AudioIn1();
				if (a1 < 0) a1 = -a1;
				if (a1 > 2047) a1 = 2047;
				int offset = (int)(((uint32_t)a1 * MLR_NUM_TRACKS) >> 11);  /* 0..6 */
				if (offset >= MLR_NUM_TRACKS) offset = MLR_NUM_TRACKS - 1;
				int base = (new_track >= 0 && new_track < MLR_NUM_TRACKS) ? new_track : 0;
				new_track = (base + offset) % MLR_NUM_TRACKS;
			}

			/* Pulse In 2 rising edge: advance to next track (wrap 6->1). */
			if (gl_pulse2_pending_) {
				int base = (new_track >= 0 && new_track < MLR_NUM_TRACKS) ? new_track : 0;
				new_track = (base + 1) % MLR_NUM_TRACKS;
				if (switch_up) gl_locked_track_ = new_track;
				knob_hard_takeover_arm(Knob::Main);
			}

			/* No channel switching during active recording. */
			if (gl_recording_active_ && gl_record_track_ >= 0) {
				new_track = gl_record_track_;
			}

			/* Track changed: update the selected mix focus only. */
			if (new_track != gl_active_track_) {
				gl_active_track_ = new_track;
			}

			/* ---- X knob layer controls ---- */
			int knob_x = KnobVal(Knob::X);  /* 0..4095 */
			uint8_t turing_range = (uint8_t)(((uint32_t)knob_x * 36u) / 4095u);
			if (turing_range != gl_turing_range_semitones_)
				gl_turing_range_semitones_ = turing_range;
			bool cv1_connected = Connected(Input::CV1);
			int mod_x = knob_x;
			if (cv1_connected) {
				int cv1 = CVIn1(); /* -2048..2047 */
				mod_x = knob_x + cv1;
				if (mod_x < 0) mod_x = 0;
				if (mod_x > 4095) mod_x = 4095;
			}

			bool x_accept = knob_hard_takeover_accept(Knob::X, knob_x);
			if (!gl_recording_active_ && switch_up && gl_active_track_ >= 0 && (x_accept || cv1_connected)) {
			/* Channel mode: per-channel level slot, like grid mixer.
			 * Left=quietest slot 5, right=loudest slot 0. */
				int idx = (mod_x * MLR_NUM_VOL_SLOTS) >> 12;  /* 0..5 */
			if (idx >= MLR_NUM_VOL_SLOTS) idx = MLR_NUM_VOL_SLOTS - 1;
			int slot = (MLR_NUM_VOL_SLOTS - 1) - idx;
			mlr_set_volume(gl_active_track_, slot);
			} else if (!gl_recording_active_ && switch_middle && (x_accept || cv1_connected)) {
			/* Global mode: playback-layer level, unity at center. */
				uint16_t target_gain;
				if (mod_x <= 2048) {
					target_gain = (uint16_t)(((uint32_t)mod_x * 256u) / 2048u);  /* 0..256 */
			} else {
					target_gain = (uint16_t)(256u + (((uint32_t)(mod_x - 2048) * 256u) / 2047u));  /* 256..512 */
			}
				gridless_update_u16_hysteresis(&gl_playback_gain_frac_, target_gain, 2);
			}

			/* ---- Y knob layer controls ---- */
			int knob_y = KnobVal(Knob::Y);  /* 0..4095 */
			bool cv2_connected = Connected(Input::CV2);
			int mod_y = knob_y;
			if (cv2_connected) {
				int cv2 = CVIn2(); /* -2048..2047 */
				mod_y = knob_y + cv2;
				if (mod_y < 0) mod_y = 0;
				if (mod_y > 4095) mod_y = 4095;
			}

			bool y_accept = knob_hard_takeover_accept(Knob::Y, knob_y);
			uint8_t turing_length = (uint8_t)(3u + (((uint32_t)knob_y * 5u) / 4095u));
			if (turing_length != gl_turing_length_)
				gl_turing_length_ = turing_length;
			bool switch_down = (sw == Switch::Down);
			if (!gl_record_mode_ && !gl_recording_active_ && rec_armed_track < 0 && mlr_rec_track < 0 && switch_down && y_accept) {
				set_cv2_attack_from_raw((uint16_t)mod_y);
			} else if (!gl_recording_active_ && switch_up && (y_accept || cv2_connected)) {
			/* Channel mode: reset start position for selected channel resets. */
				int start_col = (mod_y * MLR_GRID_COLS) >> 12;
			if (start_col >= MLR_GRID_COLS) start_col = MLR_GRID_COLS - 1;
			if (start_col < 0) start_col = 0;
			gl_prev_start_col_ = start_col;
			} else if (!gl_recording_active_ && switch_middle && (y_accept || cv2_connected)) {
			/* Global mode: radiate amount. */
				uint16_t target_radiate = (uint16_t)(((uint32_t)mod_y * 256u) / 4095u);
				gridless_update_u16_hysteresis(&gl_radiate_frac_, target_radiate, 2);
			}

			/* ---- Channel mode speed from Knob Main ---- */
			if (switch_up && gl_active_track_ >= 0 && knob_hard_takeover_accept(Knob::Main, knob_main)) {
				int centered = knob_main - 2048;        /* -2048..+2047 */
				int magnitude = centered < 0 ? -centered : centered;
				uint32_t speed_frac = 64u + (((uint32_t)magnitude * (1024u - 64u)) / 2048u);
				if (speed_frac > 1024u) speed_frac = 1024u;

				bool target_reverse = centered < 0;
				uint16_t target_speed = (uint16_t)speed_frac;
				mlr_track_t *tr = &mlr_tracks[gl_active_track_];
				bool effective_reverse = tr->seek_reverse_pending ? tr->seek_reverse_target :
					(tr->seek_handoff_reverse_pending ? tr->seek_handoff_reverse_target : tr->reverse);
				uint16_t prev_speed = gl_base_speed_frac_[gl_active_track_];
				int speed_delta = (int)target_speed - (int)prev_speed;
				if (speed_delta < 0) speed_delta = -speed_delta;

				bool reverse_changed = effective_reverse != target_reverse;
				if (reverse_changed)
					mlr_set_reverse(gl_active_track_, target_reverse);

				/* Main-knob ADC dither can constantly perturb continuous speed,
				 * which is especially audible in reverse because it invalidates
				 * wrap previews. Ignore tiny movements but apply immediately
				 * when changing direction. */
				if (reverse_changed || speed_delta >= 4) {
					gl_base_speed_frac_[gl_active_track_] = target_speed;
					mlr_set_speed_frac_nondeclick(gl_active_track_, target_speed);
				}
			}

			/* Audio In 2 modulation (if connected): selected-channel speed.
			 * -6V => -2 oct (0.25x), 0V => 1x, +6V => +2 oct (4x).
			 * In this modulation mode speed is always forward. */
			if (gl_active_track_ >= 0) {
				if (!gl_record_mode_ && !gl_recording_active_ && !gl_audio2_mod_rearm_required_ && Connected(Input::Audio2)) {
					int32_t a2 = AudioIn2();
					if (a2 > 2047) a2 = 2047;
					if (a2 < -2048) a2 = -2048;
					uint16_t speed_frac;
					if (a2 >= 0) {
						speed_frac = (uint16_t)(256u + (((uint32_t)a2 * (1024u - 256u)) / 2047u));
					} else {
						speed_frac = (uint16_t)(256u - (((uint32_t)(-a2) * (256u - 64u)) / 2048u));
					}
					mlr_set_reverse(gl_active_track_, false);
					mlr_set_speed_frac_nondeclick(gl_active_track_, speed_frac);
				} else {
					mlr_set_speed_frac_nondeclick(gl_active_track_, gl_base_speed_frac_[gl_active_track_]);
				}
			}

			if (gridless_mix_inputs_changed()) {
				gridless_apply_mix_matrix(switch_middle);
				gridless_apply_level_layers();
			}

			/* ---- Reset: Switch Down or Pulse In 1 rising edge ---- */
			bool switch_reset = gl_switch_down_rise_pending_;
			gl_switch_down_rise_pending_ = false;

			bool pulse_reset = gl_pulse1_pending_;
			gl_pulse1_pending_ = false;
			bool turing_clock = pulse_reset || gl_pulse2_pending_;
			gl_pulse2_pending_ = false;
			bool pulse_gate_now = PulseIn1() || PulseIn2() || turing_clock;

			if (!gl_record_mode_ && !gl_recording_active_ && (switch_reset || pulse_reset) && gl_active_track_ >= 0)
				mlr_cut(gl_active_track_, gl_prev_start_col_);
			if (!gl_record_mode_ && !gl_recording_active_ && turing_clock) {
				gridless_step_turing();
				trigger_cv2_envelope();
			}
			if (!pulse_gate_now && env_gate_high_)
				release_cv2_envelope();

			gl_prev_switch_mode_ = static_cast<Switch>(sw);
		}

		if (update_cv2_envelope_output())
			gl_env_end_pulse_remaining_ = CUT_PULSE_TRIG_SAMPLES;

		/* ---- Mix and output ----
		 * Use the same dual-bus soft-clipped mixer as gridful so the
		 * audio character (per-channel soft-knee limiting) matches across
		 * modes. Per-track `recorded_channel` selects which output bus
		 * a track lands on, just like in gridful. Master volume is
		 * intentionally not applied here — gridless has no UI to set it,
		 * and honoring a stale `mlr_master_level_raw` from a previous
		 * session would be confusing. */
		int16_t mix_r;
		int16_t mix_l = mlr_play_mix_dual(&mix_r);
		int32_t outL = (int32_t)mix_l;
		int32_t outR = (int32_t)mix_r;
		if (gl_recording_active_ && gl_record_track_ >= 0 && gl_record_track_ < MLR_NUM_TRACKS) {
			/* Route the input monitor to whichever output bus the
			 * track being recorded will play back on, so what you
			 * hear while recording matches the post-recording mix. */
			if (mlr_tracks[gl_record_track_].recorded_channel == 1)
				outR += (int32_t)rec_monitor_l;
			else
				outL += (int32_t)rec_monitor_l;
		}
		if (outL >  2047) outL =  2047;
		if (outL < -2048) outL = -2048;
		if (outR >  2047) outR =  2047;
		if (outR < -2048) outR = -2048;
		AudioOut1((int16_t)outL);
		AudioOut2((int16_t)outR);

		if (mlr_consume_wrap_events()) {
			gl_wrap_pulse_remaining_ = CUT_PULSE_TRIG_SAMPLES;
		}
		if (gl_turing_clocked_ && gl_turing_cv_dirty_) {
			gl_turing_clocked_ = false;
			gl_turing_cv_dirty_ = false;
			CVOut1MIDINote(gl_turing_cv_midi_);
		}

		if (gl_wrap_pulse_remaining_ > 0) {
			gl_wrap_pulse_remaining_--;
			PulseOut1(true);
		} else {
			PulseOut1(false);
		}
		if (gl_env_end_pulse_remaining_ > 0) {
			gl_env_end_pulse_remaining_--;
			PulseOut2(true);
		} else {
			PulseOut2(false);
		}

		/* ---- LED update (sub-sampled) ---- */
		gl_led_counter_++;
		if (gl_led_counter_ >= LED_UPDATE_INTERVAL) {
			gl_led_counter_ = 0;
			gl_led_pulse_++;
			gridless_update_leds();
		}

		return run_control;

	}

	/** Update the 6 card LEDs for gridless mode.
	 *  Track-to-LED mapping (zig-zag across the two columns of 3):
	 *    tr1→Led0  tr2→Led2  tr3→Led4
	 *    tr4→Led1  tr5→Led3  tr6→Led5
	 */
	static constexpr uint8_t kTrackLed[MLR_NUM_TRACKS] = {0, 2, 4, 1, 3, 5};

	void __not_in_flash("gridless") gridless_update_leds()
	{
		if (gl_active_track_ < 0 || gl_active_track_ >= MLR_NUM_TRACKS) {
			for (int i = 0; i < 6; i++) LedBrightness(i, 0);
			return;
		}

		int led = kTrackLed[gl_active_track_];
		for (int i = 0; i < 6; i++) LedBrightness(i, 0);

		if (gl_record_mode_ || gl_recording_active_) {
			bool on = gl_recording_active_ ? ((gl_led_pulse_ & 1) != 0)
			                               : ((gl_led_pulse_ & 2) != 0);
			if (!on) return;
			LedBrightness(led, 4095);
			return;
		}

		if (gl_record_hold_active_) {
			uint32_t lit = (gl_switch_down_hold_samples_ * 6u) / GRIDLESS_REC_HOLD_SAMPLES;
			if (lit > 6u) lit = 6u;
			for (int i = 0; i < 6; i++) {
				LedBrightness(i, ((uint32_t)i < lit) ? 4095 : 0);
			}
			return;
		}

		/* Show every track's effective level (volume slot × radiate × playback
		 * gain, including CV modulation).  volume_target is 0..1024 (Q8 with
		 * +6 dB headroom), already computed by gridless_apply_level_layers(). */
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint32_t vt = mlr_tracks[t].volume_target;  /* 0..1024 */
			uint32_t bri = (vt * 4095u) / 1024u;
			if (bri > 4095u) bri = 4095u;
			LedBrightness(kTrackLed[t], (uint16_t)bri);
		}
	}

	/* ================================================================ */
	/* Helper: build and dispatch an event (records into patterns too)  */
	/* ================================================================ */

	__attribute__((noinline, noclone)) void __not_in_flash("dispatch_event") dispatch_event(uint8_t type, uint8_t track, int8_t a, int8_t b)
	{
		/* any manual interaction deselects the active recall,
		 * except recall events themselves (which set it). */
		if (type != MLR_EVT_RECALL)
			mlr_recall_active = -1;

		mlr_event_t e;
		e.timestamp_ms = 0;  /* filled by pattern recorder */
		e.type  = type;
		e.track = track;
		e.param_a = a;
		e.param_b = b;
		mlr_pattern_event(&e);
		mlr_recall_event(&e);

		/* Mirror cut events to CV / pulse outputs for manual gestures.
		 * Pattern/recall replay reaches cut_trigger() via the C hook
		 * mlr_event_playback_hook below. */
		if (type == MLR_EVT_CUT)
			cut_trigger((int)track, (int)a);
	}

	/* ================================================================ */
	/* cut_trigger — fire CV1 S&H, CV2 decay envelope, PulseOut1 on cut.*/
	/* Called from dispatch_event() (manual) and from the C playback    */
	/* hook (pattern / recall replay). Must be cheap and safe to call   */
	/* from the audio core.                                              */
	/* ================================================================ */
public:
	void __not_in_flash_func(cut_trigger)(int track, int col)
	{
		if (track >= 8) {
			if (col < 0) col = 0;
			if (col > 15) col = 15;
			cv_step_base_midi_ = (int8_t)keyboard_midi_from_internal(col, track);
			trigger_cv2_envelope();
			cv_pulse1_remaining_ = CUT_PULSE_TRIG_SAMPLES;
			pulse1_led_latch_ = true;
			return;
		}
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		if (col < 0) col = 0;
		if (col > 15) col = 15;
		/* Empty tracks still drive CV1/Pulse1/envelope: the track row acts
		 * as a playable mini-keyboard even before audio is recorded. */
		if (!mlr_tracks[track].has_content) {
			empty_keyboard_linger_until_us_[track] = time_us_64() + EMPTY_KEYBOARD_LINGER_US;
			empty_keyboard_linger_col_[track] = (uint8_t)((empty_keyboard_linger_col_[track] & 0x80u) | (uint8_t)col);
		}

		bool cv_outputs_enabled = track_outputs_enabled(track);
		if (cv_outputs_enabled) {
			/* CV1 sample-and-hold: row-aware keyboard pitch, with middle C
			 * at (8,4) and the same map repeated on the bottom half. */
			int midi = keyboard_midi_from_internal(col, track + 1);
			cv_step_base_midi_ = (int8_t)midi;

			trigger_cv2_envelope();
		}

		if (cv_outputs_enabled) {
			cv_pulse1_remaining_ = CUT_PULSE_TRIG_SAMPLES;
			pulse1_led_latch_ = true;
		}
	}

	void __not_in_flash_func(pattern_cut_trigger)(int track, int col, uint8_t source)
	{
		cut_trigger(track, col);
		if (track >= 8 && source == MLR_PLAYBACK_SOURCE_PATTERN) {
			replay_gate_mask_ |= (uint8_t)(1u << 6);
			extra_keyboard_state_ = (uint8_t)((extra_keyboard_state_ & EXTRA_GUIDE_ENABLED) |
				(((track - 8) << 4) | (col & 0x0F)));
			gate_high_ = true;
			return;
		}
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		if (source == MLR_PLAYBACK_SOURCE_PATTERN && track_outputs_enabled(track)) {
			replay_gate_mask_ |= (uint8_t)(1u << track);
			gate_high_ = true;
		}
		if (!mlr_tracks[track].has_content) {
			empty_pattern_key_col_[track] = (uint8_t)col;
			empty_pattern_key_active_[track] = true;
			empty_pattern_key_until_us_[track] = (source == MLR_PLAYBACK_SOURCE_RECALL)
				? time_us_32() + RECALL_KEY_GATE_US
				: 0;
		}
	}

	void __not_in_flash_func(cut_release_trigger)(int track)
	{
		if (track < 0) return;
		uint8_t bit = (track < MLR_NUM_TRACKS) ? (uint8_t)(1u << track) : (uint8_t)(1u << 6);
		replay_gate_mask_ &= (uint8_t)~bit;
		bool new_gate = live_gate_high_ || replay_gate_mask_ != 0;
		if (!new_gate && env_gate_high_)
			release_cv2_envelope();
		gate_high_ = new_gate;
	}

	void __not_in_flash_func(pattern_stop_trigger)(int track)
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		empty_pattern_key_active_[track] = false;
		empty_pattern_key_until_us_[track] = 0;
	}
private:
	uint32_t __not_in_flash_func(gridless_turing_rand12)()
	{
		uint32_t x = gl_turing_rng_state_;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		gl_turing_rng_state_ = x ? x : 0x6D2B79F5u;
		return (x >> 20) & 0x0FFFu;
	}

	void __not_in_flash_func(gridless_update_turing_cv)()
	{
		static const uint8_t kMajor[12] = {0, 0, 2, 2, 4, 4, 5, 7, 7, 9, 9, 11};
		uint8_t value = (uint8_t)((gl_turing_cells_[0] & 0x03u) |
			((gl_turing_cells_[1] & 0x03u) << 2) |
			((gl_turing_cells_[2] & 0x03u) << 4));
		uint8_t semitone = (uint8_t)(((uint32_t)value * (uint32_t)gl_turing_range_semitones_ + 31u) / 63u);
		uint8_t midi = (uint8_t)(CV_NOTE_BASE_MIDI + (semitone / 12u) * 12u + kMajor[semitone % 12u]);
		if (midi > 127u) midi = 127u;
		gl_turing_cv_midi_ = midi;
		gl_turing_cv_dirty_ = true;
	}

	void __not_in_flash_func(gridless_step_turing)()
	{
		uint16_t probability = gl_turing_probability_raw_;
		bool force_toggle = false;
		if (probability < 15u) {
			probability = 0;
			force_toggle = true;
		} else if (probability > (4095u - 15u)) {
			probability = 4095u;
		}

		uint8_t length = gl_turing_length_;
		if (length < 3u) length = 3u;
		if (length > 8u) length = 8u;

		uint8_t feedback = gl_turing_cells_[length - 1u] & 0x03u;
		for (int i = (int)length - 1; i > 0; i--)
			gl_turing_cells_[i] = gl_turing_cells_[i - 1] & 0x03u;

		uint32_t r = gridless_turing_rand12();
		if (force_toggle)
			feedback = (uint8_t)((~feedback) & 0x03u);
		else if (r > probability)
			feedback = (uint8_t)((~r) & 0x03u);
		gl_turing_cells_[0] = feedback;
		gridless_update_turing_cv();
		gl_turing_clocked_ = true;
	}

	bool __not_in_flash_func(update_cv2_envelope_output)()
	{
		bool was_active = (env_stage_ != ENV_IDLE);
		if (env_stage_ == ENV_ATTACK) {
			env_val_q16_ += env_attack_step_q16_;
			if (env_val_q16_ >= ((int32_t)CV_ENV_PEAK << 16)) {
				env_val_q16_ = (int32_t)CV_ENV_PEAK << 16;
				env_stage_ = env_gate_high_ ? ENV_SUSTAIN : ENV_RELEASE;
			}
		} else if (env_stage_ == ENV_SUSTAIN) {
			env_val_q16_ = (int32_t)CV_ENV_PEAK << 16;
		} else if (env_stage_ == ENV_RELEASE) {
			env_val_q16_ -= env_release_step_q16_;
			if (env_val_q16_ <= 0) {
				env_val_q16_ = 0;
				env_stage_ = ENV_IDLE;
			}
		}

		/* CV2 raw output: (env_val >> 16) - CV2_FLOOR_OFFSET shifts the
		 * rest value to roughly -1 V (assuming +6 V = +2047 raw). Peak is
		 * reduced from +6 V to ~+5 V; calibration not applied. */
		CVOut2((int16_t)((env_val_q16_ >> 16) - CV2_FLOOR_OFFSET));
		return was_active && env_stage_ == ENV_IDLE;
	}

	void __not_in_flash_func(latch_cv2_release_from_raw)(uint16_t raw)
	{
		uint32_t y = raw;
		if (y > 4095u) y = 4095u;
		uint32_t y_sq = (y * y) / 4095u;  /* 0..4095, square-law for musical knob feel */
		uint32_t T_rel = CV_DECAY_MIN_SAMPLES +
			(uint32_t)(((uint64_t)(CV_DECAY_MAX_SAMPLES - CV_DECAY_MIN_SAMPLES) * y_sq) / 4095u);
		if (T_rel < 1u) T_rel = 1u;
		if (env_val_q16_ <= 0) {
			env_release_step_q16_ = 1;
			return;
		}
		env_release_step_q16_ = env_val_q16_ / (int32_t)T_rel;
		if (env_release_step_q16_ < 1) env_release_step_q16_ = 1;
	}

	void __not_in_flash_func(set_cv2_attack_from_raw)(uint16_t raw)
	{
		uint32_t y = raw;
		if (y > 4095u) y = 4095u;
		uint32_t y_sq = (y * y) / 4095u;
		cv_attack_samples_ = CV_ATTACK_MIN_SAMPLES +
			(uint32_t)(((uint64_t)(CV_ATTACK_MAX_SAMPLES - CV_ATTACK_MIN_SAMPLES) * y_sq) / 4095u);
	}

	void __not_in_flash_func(trigger_cv2_envelope)()
	{
		/* CV2 ASR envelope: attack from current value to peak, sustain while
		 * gated, then release to 0 when the gate source drops. */
		env_gate_high_ = true;
		int32_t peak_q16 = (int32_t)CV_ENV_PEAK << 16;
		if (cv_attack_samples_ == 0) {
			env_val_q16_ = peak_q16;
			env_stage_ = ENV_SUSTAIN;
		} else {
			int32_t remaining_q16 = peak_q16 - env_val_q16_;
			if (remaining_q16 <= 0) {
				env_val_q16_ = peak_q16;
				env_stage_ = ENV_SUSTAIN;
				return;
			}
			env_attack_step_q16_ = remaining_q16 / (int32_t)cv_attack_samples_;
			if (env_attack_step_q16_ < 1) env_attack_step_q16_ = 1;
			env_stage_ = ENV_ATTACK;
		}
	}

	void __not_in_flash_func(release_cv2_envelope)()
	{
		env_gate_high_ = false;
		if (env_stage_ == ENV_IDLE) return;
		latch_cv2_release_from_raw((uint16_t)KnobVal(Knob::Y));
		env_stage_ = ENV_RELEASE;
	}

	void process_bottom_master_control()
	{
		int row = MLR_NUM_TRACKS + 1;
		if (row > 7) return;
		if (!grid.keyDown()) return;
		if (grid.lastY() != row) return;

		int max_col = small_grid_ ? 7 : 15;
		int col = grid.lastX();
		if (col < 0) col = 0;
		if (col > max_col) col = max_col;

		int inv = max_col - col;  /* left=max, right=0 */
		uint16_t raw = (uint16_t)(((uint32_t)inv * 4095u + (uint32_t)(max_col / 2)) / (uint32_t)max_col);
		if (raw > 4095u) raw = 4095u;
		mlr_master_level_raw = raw;
		master_knob_arm_raw_ = KnobVal(Knob::Main);
		master_knob_last_raw_ = master_knob_arm_raw_;
		master_knob_takeover_armed_ = true;
		mlr_master_override = true;  /* suppress pattern master events */

		dispatch_event(MLR_EVT_MASTER, (uint8_t)(raw >> 8), (int8_t)(raw & 0xFF), 0);
	}

	/* ================================================================ */
	/* Bottom-half rows (rows 8..grid.rows()-1) on a tall grid behave   */
	/* like the row-of-an-empty-track keyboard overlay: each key-down   */
	/* updates CV1 (S&H pitch), fires CV2 envelope + PulseOut1, and     */
	/* arms a 1-second piano-key linger overlay on that row. The bottom */
	/* half has no associated track, no audio playback, and is not      */
	/* recorded into patterns or recalls. Page-agnostic (works the same */
	/* on CUT and REC pages). */
	/* ================================================================ */
	void __not_in_flash("process_extra_rows") process_extra_rows()
	{
		if (!grid.ready() || grid.rows() <= 8) return;
		if (!(grid.keyDown() || grid.keyUp())) return;
		int y = grid.lastY();
		int col = grid.lastX();
		if (grid.keyDown() && !small_grid_ && grid.rows() >= 16 && y == 15 &&
		    ((col == 0 && grid.held(15, 15)) || (col == 15 && grid.held(0, 15)))) {
			extra_keyboard_state_ ^= EXTRA_GUIDE_ENABLED;
			return;
		}
		if (y <= 8 || y >= grid.rows() || y >= 15) return;
		int cs = cut_col_start();
		int ce = cut_col_end();
		if (col < cs || col > ce) return;

		int internal = cut_grid_to_internal(col);
		uint8_t key = (uint8_t)(((y - 8) << 4) | (internal & 0x0F));
		if (grid.keyUp()) {
			if ((extra_keyboard_state_ & EXTRA_PATTERN_KEY_MASK) == key) {
				extra_keyboard_state_ = (uint8_t)((extra_keyboard_state_ & EXTRA_GUIDE_ENABLED) | EXTRA_PATTERN_KEY_NONE);
				dispatch_event(MLR_EVT_CUT_RELEASE, (uint8_t)y, 0, 0);
			}
			return;
		}

		uint8_t prev = (uint8_t)(extra_keyboard_state_ & EXTRA_PATTERN_KEY_MASK);
		if (prev != EXTRA_PATTERN_KEY_NONE)
			dispatch_event(MLR_EVT_CUT_RELEASE, (uint8_t)((prev >> 4) + 8), 0, 0);
		extra_keyboard_state_ = (uint8_t)((extra_keyboard_state_ & EXTRA_GUIDE_ENABLED) | key);
		dispatch_event(MLR_EVT_CUT, (uint8_t)y, (int8_t)internal, 0);
	}

	/* True if any cut-zone key on a bottom-half row is currently held. */
	bool extra_rows_any_held() const
	{
		if (!grid.ready() || grid.rows() <= 8) return false;
		int rmax = grid.rows();
		if (rmax > MONOME_WS_GRID_MAX_Y) rmax = MONOME_WS_GRID_MAX_Y;
		int cs = cut_col_start();
		int ce = cut_col_end();
		uint16_t zone_mask = (uint16_t)(((1u << (ce - cs + 1)) - 1u) << cs);
		for (int row = 9; row < rmax && row < 15; row++) {
			if (grid.heldRowMask((uint8_t)row) & zone_mask)
				return true;
		}
		return false;
	}

	void __not_in_flash("handle_rec_arm_col_press") handle_rec_arm_col_press(int track, int column)
	{
		if (delete_action_held()) {
			mlr_clear_track(track);
			if (rec_armed_track == track) {
				rec_armed_track = -1;
				if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
			}
			return;
		}

		/* 16-wide grid: col 0 = ch1, col 1 = ch2. */
		if (!small_grid_) {
			if (rec_armed_track == track &&
			    mlr_tracks[track].recorded_channel != (uint8_t)column) {
				set_track_recorded_channel(track, (uint8_t)column, true);
				return;
			}
			if (rec_armed_track != track) {
				set_track_recorded_channel(track, (uint8_t)column, true);
			}
		}

		int sw_now = SwitchVal();
		bool rec_pos = (sw_now == Switch::Up || sw_now == Switch::Down);
		if (rec_pos && rec_armed_track < 0 && mlr_rec_track < 0 && !mlr_flushing && !rec_limit_latched && rec_start_lockout_samples_ == 0) {
			mlr_start_record(track);
			rec_speed_accum = 0;
			rec_gated = true;
		} else if (!rec_pos || rec_armed_track >= 0) {
			if (rec_armed_track == track) {
				set_armed_track(-1);
				rec_limit_latched = false;
			} else {
				set_armed_track(track);
				rec_limit_latched = false;
			}
		}
	}

	void process_delete_reset_hold()
	{
		if (!delete_held()) {
			delete_reset_hold_samples_ = 0;
			delete_reset_fired_ = false;
			return;
		}
		if (delete_reset_fired_)
			return;
		if (mlr_rec_track >= 0 || mlr_flushing || mlr_scene_saving) {
			delete_reset_hold_samples_ = 0;
			return;
		}

		if (delete_reset_hold_samples_ < DELETE_RESET_HOLD_SAMPLES)
			delete_reset_hold_samples_ += MAIN_CTRL_DIV;
		if (delete_reset_hold_samples_ < DELETE_RESET_HOLD_SAMPLES)
			return;

		mlr_scene_reset_params_to_defaults();
		mlr_scene_save_start();

		rec_armed_track = -1;
		resume_after_arm_track_ = -1;
		rec_limit_latched = false;
		group_gesture_mask_ = 0;
		group_gesture_committed_ = false;
		group_flash_mask_ = 0;
		group_flash_samples_remaining_ = 0;
		reset_copy_gesture();
		copy_arm_suppress_event_ = false;
		copy_flash_mask_ = 0;
		copy_flash_samples_remaining_ = 0;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			play_col_armed[t] = false;
			play_col_hold_time[t] = 0;
			gate_entered[t] = false;
			release_gated_choke_pauses(t);
		}
		armed_mask_ = 0;

		delete_reset_flash_samples_remaining_ = DELETE_RESET_FLASH_SAMPLES;
		delete_reset_fired_ = true;
	}

	void resume_armed_track_if_needed(int track)
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		if (resume_after_arm_track_ != track) return;
		resume_after_arm_track_ = -1;

		if (!mlr_tracks[track].has_content || mlr_tracks[track].playing)
			return;

		int col = mlr_get_column(track);
		if (col < 0) col = 0;
		if (col >= MLR_GRID_COLS) col = MLR_GRID_COLS - 1;
		mlr_cut(track, col);
	}

	void __not_in_flash("set_track_recorded_channel") set_track_recorded_channel(int track, uint8_t channel, bool persist_existing)
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return;
		channel &= 0x01;
		bool changed = (mlr_tracks[track].recorded_channel != channel);
		mlr_set_recorded_channel(track, channel);
		mlr_tracks[track].channel_user_chosen = true;
		if (changed)
			dispatch_event(MLR_EVT_CHANNEL, (uint8_t)track, (int8_t)channel, 0);
		if (changed && persist_existing && mlr_tracks[track].has_content)
			mlr_rewrite_track_header(track);
	}

	void set_armed_track(int track)
	{
		if (track < -1 || track >= MLR_NUM_TRACKS) return;

		if (track == rec_armed_track) {
			resume_armed_track_if_needed(track);
			rec_armed_track = -1;
			return;
		}

		if (rec_armed_track >= 0)
			resume_armed_track_if_needed(rec_armed_track);

		rec_armed_track = track;

		/* Plug auto-detect: if the user has not explicitly picked a channel
		 * for this track, and exactly one of Audio1/Audio2 is connected,
		 * auto-select that channel. With both or neither plugged, leave the
		 * track's previous recorded_channel in place. */
		if (track >= 0 && !mlr_tracks[track].channel_user_chosen) {
			bool a1 = Connected(Input::Audio1);
			bool a2 = Connected(Input::Audio2);
			if (a1 && !a2)      mlr_tracks[track].recorded_channel = 0;
			else if (!a1 && a2) mlr_tracks[track].recorded_channel = 1;
		}
	}

	/* ================================================================ */
	/* Track-group helpers                                              */
	/* ================================================================ */

	/* Apply a fresh group membership. Only populated tracks are eligible;
	 * each included track leaves its previous group (the rest of which stays
	 * together) and joins new_mask. */
	void __not_in_flash("set_group") set_group(uint8_t new_mask)
	{
		uint8_t groupable_mask = 0;
		int popcount = 0;
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if ((new_mask & (1u << u)) && mlr_tracks[u].has_content) {
				groupable_mask |= (uint8_t)(1u << u);
				popcount++;
			}
		}
		if (popcount < 2) return;
		new_mask = groupable_mask;

		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (!(new_mask & (1u << u))) continue;
			uint8_t self = (uint8_t)(1u << u);
			uint8_t old = mlr_track_groups[u];
			if (old == self) continue;
			uint8_t remainder = (uint8_t)(old & ~self);
			for (int v = 0; v < MLR_NUM_TRACKS; v++) {
				if (remainder & (1u << v))
					mlr_track_groups[v] = remainder;
			}
			mlr_track_groups[u] = self;
		}
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (new_mask & (1u << u))
				mlr_track_groups[u] = new_mask;
		}
		mlr_scene_save_start();
	}

	/* Dissolve the group containing track t. Returns the prior mask so
	 * callers can drive a confirmation blink across former members. */
	uint8_t __not_in_flash("dissolve_group") dissolve_group(int t)
	{
		if (t < 0 || t >= MLR_NUM_TRACKS) return 0;
		uint8_t old = mlr_track_groups[t];
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (old & (1u << u)) {
				mlr_track_groups[u] = (uint8_t)(1u << u);
			}
		}
		return old;
	}

	bool __not_in_flash("dissolve_group_if_delete_touch") dissolve_group_if_delete_touch(int t)
	{
		if (!delete_action_held() || t < 0 || t >= MLR_NUM_TRACKS) return false;
		uint8_t old_mask = mlr_track_groups[t];
		int popcount = 0;
		for (int u = 0; u < MLR_NUM_TRACKS; u++)
			if (old_mask & (1u << u)) popcount++;
		if (popcount < 2) return false;

		dissolve_group(t);
		mlr_scene_save_start();
		group_flash_mask_ = old_mask;
		group_flash_samples_remaining_ = GROUP_FLASH_DISSOLVE_SAMPLES;
		group_flash_period_ = GROUP_FLASH_DISSOLVE_PERIOD;
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (!(old_mask & (1u << u))) continue;
			play_col_armed[u] = false;
			play_col_hold_time[u] = 0;
			gate_entered[u] = false;
			release_gated_choke_pauses(u);
		}
		armed_mask_ &= (uint8_t)~old_mask;
		return true;
	}

	/* When DELETE is held, find the currently-highlighted group (cycled over
	 * time). Returns 0 if no multi-member groups exist. */
	uint8_t __not_in_flash("cycled_group_mask") cycled_group_mask()
	{
		uint8_t seen[MLR_NUM_TRACKS];
		int n = 0;
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint8_t m = mlr_track_groups[t];
			if (m == (uint8_t)(1u << t)) continue;
			bool dup = false;
			for (int i = 0; i < n; i++) if (seen[i] == m) { dup = true; break; }
			if (!dup) seen[n++] = m;
		}
		if (n == 0) return 0;
		int idx = (gate_pulse / GROUP_CYCLE_TICKS) % n;
		return seen[idx];
	}

	bool __not_in_flash("track_gate_paused") track_gate_paused(int track) const
	{
		if (track < 0 || track >= MLR_NUM_TRACKS) return false;
		uint8_t bit = (uint8_t)(1u << track);
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (gated_choke_pause_mask_[u] & bit)
				return true;
		}
		return false;
	}

	void __not_in_flash("pause_other_group_members_for_gate") pause_other_group_members_for_gate(int t)
	{
		if (t < 0 || t >= MLR_NUM_TRACKS) return;
		uint8_t mask = mlr_track_groups[t];
		uint8_t paused = 0;
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if (u == t) continue;
			if (!(mask & (1u << u))) continue;
			if (!mlr_tracks[u].playing) continue;
			mlr_tracks[u].playing = false;
			paused |= (uint8_t)(1u << u);
		}
		gated_choke_pause_mask_[t] |= paused;
	}

	void __not_in_flash("release_gated_choke_pauses") release_gated_choke_pauses(int t)
	{
		if (t < 0 || t >= MLR_NUM_TRACKS) return;
		uint8_t mask = gated_choke_pause_mask_[t];
		gated_choke_pause_mask_[t] = 0;
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if ((mask & (1u << u)) && !track_gate_paused(u))
				mlr_tracks[u].playing = true;
		}
	}

	/* --- Group-aware action helpers. Grouped tracks behave as a single
	 *     choke voice: starting one track stops all other members of its
	 *     group. Parameter changes (speed/reverse/volume) and loops apply
	 *     only to the tapped track — there is no in-sync broadcast mode. */

	void __not_in_flash("group_cut") group_cut(int t, int col)
	{
		if (mlr_gate_mode[t]) {
			mlr_clear_loop(t);
			mlr_cut(t, col);
			pause_other_group_members_for_gate(t);
		} else {
			mlr_choke_group_cut(t, col);
		}
		dispatch_event(MLR_EVT_CUT, (uint8_t)t, (int8_t)col, 0);
	}

	void __not_in_flash("group_set_loop") group_set_loop(int t, int a, int b)
	{
		if (!mlr_tracks[t].has_content) return;
		if (mlr_gate_mode[t]) {
			mlr_set_loop(t, a, b);
			pause_other_group_members_for_gate(t);
		} else {
			mlr_choke_group_set_loop(t, a, b);
		}
		dispatch_event(MLR_EVT_LOOP, (uint8_t)t, (int8_t)a, (int8_t)b);
	}

	void __not_in_flash("group_set_speed") group_set_speed(int t, int speed)
	{
		mlr_set_speed(t, speed);
		dispatch_event(MLR_EVT_SPEED, (uint8_t)t, (int8_t)speed, 0);
	}

	void __not_in_flash("group_set_reverse") group_set_reverse(int t, bool reverse)
	{
		mlr_set_reverse(t, reverse);
		dispatch_event(MLR_EVT_REVERSE, (uint8_t)t, reverse ? 1 : 0, 0);
	}

	void __not_in_flash("group_set_volume") group_set_volume(int t, int slot)
	{
		mlr_set_volume(t, slot);
		dispatch_event(MLR_EVT_VOLUME, (uint8_t)t, (int8_t)slot, 0);
	}

	void __not_in_flash("group_stop_track") group_stop_track(int t)
	{
		bool any_was_playing = false;
		uint8_t mask = mlr_track_groups[t];
		for (int u = 0; u < MLR_NUM_TRACKS; u++) {
			if ((mask & (1u << u)) && mlr_tracks[u].playing) { any_was_playing = true; break; }
		}
		mlr_group_stop_track(t);
		if (any_was_playing)
			dispatch_event(MLR_EVT_STOP, (uint8_t)t, 0, 0);
	}

	/* Toggle the tapped track in choke mode: if that track is playing,
	 * stop the group; otherwise start the tapped track from its
	 * loop_col_start (or 0) and choke any sibling that is currently
	 * playing. Tapping a stopped sibling while another group member
	 * plays performs a choke-handoff: the playing member stops and the
	 * tapped one starts. */
	void __not_in_flash("group_play_toggle") group_play_toggle(int t)
	{
		if (mlr_tracks[t].playing) {
			group_stop_track(t);
			return;
		}

		if (!mlr_tracks[t].has_content) return;
		if (rec_armed_track == t) {
			rec_armed_track = -1;
			if (resume_after_arm_track_ == t) resume_after_arm_track_ = -1;
			rec_limit_latched = false;
		}
		int start_col = mlr_tracks[t].reverse ? (MLR_GRID_COLS - 1) : 0;
		if (mlr_tracks[t].loop_active)
			start_col = mlr_tracks[t].reverse ? mlr_tracks[t].loop_col_end : mlr_tracks[t].loop_col_start;
		mlr_choke_group_resume(t, start_col);
		dispatch_event(MLR_EVT_START, (uint8_t)t, (int8_t)start_col, 0);
	}

	/* Gate mode is per-track even when tracks are grouped; only play toggles
	 * broadcast through the group. */
	void __not_in_flash("set_gate_mode") set_gate_mode(int t, bool on)
	{
		if (t < 0 || t >= MLR_NUM_TRACKS) return;
		mlr_gate_mode[t] = on;
		if (on)
			mlr_stop_track(t);
		else
			release_gated_choke_pauses(t);
	}

	void __not_in_flash("reset_copy_gesture") reset_copy_gesture()
	{
		copy_source_track_ = -1;
		copy_source_column_ = 0;
		copy_participant_mask_ = 0;
		copy_target_mask_ = 0;
		copy_gesture_committed_ = false;
		copy_gesture_touched_other_ = false;
	}

	bool __not_in_flash("copy_gesture_arm_col") copy_gesture_arm_col(int column) const
	{
		return column == 0 || (!small_grid_ && column == 1);
	}

	/* Arm/channel copy/paste gesture. Hold a populated source arm/channel key,
	 * touch one or more empty destination arm/channel keys, then release. */
	void __not_in_flash("process_copy_gesture") process_copy_gesture()
	{
		copy_arm_suppress_event_ = false;
		if (play_page != PAGE_REC) return;
		if (!(grid.keyDown() || grid.keyUp())) return;
		int column = grid.lastX();
		if (!copy_gesture_arm_col(column) || grid.lastY() < 1 || grid.lastY() > MLR_NUM_TRACKS) return;

		int track = grid.lastY() - 1;
		uint8_t bit = (uint8_t)(1u << track);
		bool rec_pos = (SwitchVal() == Switch::Up || SwitchVal() == Switch::Down);
		bool eligible = !delete_action_held() && !rec_pos && rec_armed_track < 0 &&
		                mlr_rec_track < 0 && !mlr_flushing && !mlr_scene_saving;

		if (grid.keyDown()) {
			if (!eligible) return;
			if (copy_source_track_ < 0) {
				if (!mlr_tracks[track].has_content) return;
				copy_source_track_ = track;
				copy_source_column_ = column;
				copy_participant_mask_ = bit;
				copy_target_mask_ = 0;
				copy_gesture_committed_ = false;
				copy_gesture_touched_other_ = false;
				copy_arm_suppress_event_ = true;
				return;
			}

			copy_participant_mask_ |= bit;
			copy_arm_suppress_event_ = true;
			if (track != copy_source_track_) {
				copy_gesture_touched_other_ = true;
				if (!mlr_tracks[track].has_content)
					copy_target_mask_ |= bit;
			}
			return;
		}

		if (copy_source_track_ < 0 || !(copy_participant_mask_ & bit)) return;
		copy_arm_suppress_event_ = true;

		if (!copy_gesture_committed_ && copy_target_mask_ != 0) {
			uint8_t accepted = mlr_copy_track_mask(copy_source_track_, copy_target_mask_);
			if (accepted) {
				copy_flash_mask_ = accepted;
				copy_flash_samples_remaining_ = COPY_FLASH_SAMPLES;
				copy_flash_period_ = COPY_FLASH_PERIOD;
				reset_copy_gesture();
				return;
			}
			copy_gesture_committed_ = true;
		}

		copy_participant_mask_ &= (uint8_t)~bit;
		if (copy_participant_mask_ == 0) {
			if (!copy_gesture_committed_ && !copy_gesture_touched_other_ && track == copy_source_track_)
				handle_rec_arm_col_press(copy_source_track_, copy_source_column_);
			reset_copy_gesture();
		}
	}

	/* ================================================================ */
	/* Gate mode: play-col hold detection (runs every sample, any page) */
	/* ================================================================ */

	void __not_in_flash("process_gate_hold") process_gate_hold()
	{
		int pc = play_col();

		/* Fast path: skip the whole function when nothing relevant is
		 * happening this sample. This is the common state (no play-col
		 * keys held, no armed timers, no multi-hold gesture in progress)
		 * and keeps the REC-page UI tick close to CUT-page cost. */
		bool any_event = grid.keyDown() || grid.keyUp();
		uint16_t col_mask_raw = grid.heldColMask((uint8_t)pc);
		uint8_t held_now = (uint8_t)((col_mask_raw >> 1) & ((1u << MLR_NUM_TRACKS) - 1u));
		if (!any_event && held_now == 0 && group_gesture_mask_ == 0 && armed_mask_ == 0)
			return;

		bool alt_held = delete_action_held();

		/* Snapshot of which play-col keys are currently held. Empty tracks
		 * may be tapped normally, but they are not eligible for grouping. */
		uint8_t groupable_held = 0;
		{
			uint8_t m = held_now;
			while (m) {
				int t = __builtin_ctz(m);
				if (mlr_tracks[t].has_content)
					groupable_held |= (uint8_t)(1u << t);
				m &= (uint8_t)(m - 1);
			}
		}

		/* Detect multi-hold: if at any moment 2+ populated play-col keys are held,
		 * cancel any pending long-press timers (so no gate_mode side-effect
		 * fires) and accumulate the participants into group_gesture_mask_.
		 * The group commits on the first release of a participating key. */
		int held_count = __builtin_popcount(groupable_held);
		if (held_count >= 2) {
			if (group_gesture_committed_ && (groupable_held & (uint8_t)~group_gesture_mask_)) {
				group_gesture_mask_ = 0;
				group_gesture_committed_ = false;
			}
			group_gesture_mask_ |= groupable_held;
			uint8_t m = held_now;
			while (m) {
				int t = __builtin_ctz(m);
				play_col_armed[t] = false;
				play_col_hold_time[t] = 0;
				gate_entered[t] = false;
				m &= (uint8_t)(m - 1);
			}
			armed_mask_ &= (uint8_t)~held_now;
		}

		/* Arm on key-down of play col on any track row (single-hold path). */
		if (grid.keyDown() && grid.lastX() == pc && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			if (group_gesture_mask_ == 0) {
				play_col_armed[track] = true;
				play_col_hold_time[track] = 0;
				gate_entered[track] = false;
				armed_mask_ |= (uint8_t)(1u << track);
			}
		}

		/* Increment hold counters and enter gate mode at threshold. Walk
		 * only the armed tracks; mask out any that are now part of a
		 * multi-hold gesture. */
		{
			uint8_t m = (uint8_t)(armed_mask_ & ~group_gesture_mask_);
			while (m) {
				int t = __builtin_ctz(m);
				play_col_hold_time[t] += MAIN_CTRL_DIV;
				if (play_col_hold_time[t] >= GATE_HOLD_THRESHOLD && !gate_entered[t]) {
					gate_entered[t] = true;
					/* Long-press: enable gate_mode for this track and stop it. */
					set_gate_mode(t, true);
				}
				m &= (uint8_t)(m - 1);
			}
		}

		/* Key-up of play col */
		if (grid.keyUp() && grid.lastX() == pc && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			bool was_armed = play_col_armed[track];
			bool was_gate_entered = gate_entered[track];

			play_col_armed[track] = false;
			play_col_hold_time[track] = 0;
			gate_entered[track] = false;
			armed_mask_ &= (uint8_t)~(1u << track);

			if (group_gesture_mask_ & (1u << track)) {
				/* This release participates in the multi-hold gesture. */
				if (!group_gesture_committed_) {
					uint8_t mask = group_gesture_mask_;
					/* Defensive recount (the mask is built from held-now
					 * snapshots of populated tracks and always contains
					 * >=2 bits by construction). */
					int popcount = 0;
					for (int u = 0; u < MLR_NUM_TRACKS; u++)
						if (mask & (1u << u)) popcount++;
					if (popcount >= 2) {
						set_group(mask);
						group_flash_mask_ = mask;
						group_flash_samples_remaining_ = GROUP_FLASH_CREATE_SAMPLES;
						group_flash_period_ = GROUP_FLASH_CREATE_PERIOD;
					}
					group_gesture_committed_ = true;
				}
				group_gesture_mask_ &= (uint8_t)~(1u << track);
				if (group_gesture_mask_ == 0)
					group_gesture_committed_ = false;
				return;  /* suppress play-toggle for this key-up */
			}

			if (alt_held) {
				/* DELETE + any key on a group member dissolves that group.
				 * Keep the historical play-column release path as a backup
				 * for events armed before the key-down handler sees them. */
				dissolve_group_if_delete_touch(track);
				return;
			}

			if (was_armed && !was_gate_entered) {
				/* Short press only — a completed long-press already entered
				 * gate_mode at threshold and the key-up here is a no-op. */
				if (mlr_gate_mode[track]) {
					/* Disable gate mode for this track and stop it. */
					set_gate_mode(track, false);
					mlr_stop_track(track);
				} else {
					/* Normal play/stop toggle, state-synced across group. */
					group_play_toggle(track);
				}
			}
		}
	}

	/* ================================================================ */
	/* ================================================================ */
	/* REC page key handler                                            */
	/* 16x8: col 0=arm, 1-6=mixer, 7=reverse, 8-14=speed, 15=play     */
	/* 8x8:  col 0=arm, 1=reverse, 2-6=speed, 7=play                  */
	/* ================================================================ */

	void __not_in_flash("process_page_rec") process_page_rec()
	{
		/* --- gated recording: no track armed + switch in rec + key up on col 0 (or col 1 on 16-wide) = stop --- */
		if (grid.keyUp() && (grid.lastX() == 0 || (!small_grid_ && grid.lastX() == 1)) && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			if (copy_arm_suppress_event_) return;
			int track = grid.lastY() - 1;
			if (mlr_rec_track == track && rec_gated) {
				/* gated recording stop on key release */
				mlr_stop_record();
				rec_speed_accum = 0;
				rec_gated = false;
			}
		}

		if (!grid.keyDown()) return;
		if (grid.lastY() < 1 || grid.lastY() > MLR_NUM_TRACKS) return;

		int track  = grid.lastY() - 1;
		int column = grid.lastX();
		if (dissolve_group_if_delete_touch(track)) return;

		/* Cols 0 / 1 (16-wide only): combined channel-select + record-arm.
		 * On 8x8 grids col 1 is reverse, so we only honor the channel-pick
		 * on the second column when running with a 16-wide grid. */
		bool is_arm_col = (column == 0) || (!small_grid_ && column == 1);

		if (is_arm_col) {
			if (copy_arm_suppress_event_) return;
			handle_rec_arm_col_press(track, column);
			return;
		}

		if (small_grid_) {
			/* 8x8 REC: col 1=reverse, cols 2-6=speed, col 7=play (gate_hold) */
			if (column == 1) {
				bool new_rev = !mlr_tracks[track].reverse;
				group_set_reverse(track, new_rev);
			} else if (column >= 2 && column <= 6) {
				int speed = speed_col_to_shift_small(column);
				group_set_speed(track, speed);
			}
			/* col 7 is handled by process_gate_hold() */
		} else {
			/* 16x8 REC: cols 0-1=channel+arm (above), cols 2-6=mixer (5 slots),
			 * col 7=reverse, cols 8-14=speed, col 15=play. */
			if (column >= 2 && column <= 6) {
				int slot = column - 2;
				group_set_volume(track, slot);
			} else if (column == 7) {
				bool new_rev = !mlr_tracks[track].reverse;
				group_set_reverse(track, new_rev);
			} else if (column >= 8 && column <= 14) {
				int speed = column - 11;  /* -3 .. +3 */
				group_set_speed(track, speed);
			}
			/* col 15 is handled by process_gate_hold() */
		}
	}

	/* ================================================================ */
	/* CUT page: playhead cutting + loop-a-section                     */
	/* ================================================================ */

	void __not_in_flash("process_page_cut") process_page_cut()
	{
		int cs = cut_col_start();
		int ce = cut_col_end();
		bool alt_held = delete_action_held();

		/* Empty-track keyboard patterns need key-up events so replayed pitch
		 * highlights last for the same duration as the original press. */
		if (grid.keyUp() && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			int column = grid.lastX();
			if (column >= cs && column <= ce && track_outputs_enabled(track)) {
				int cut_col = cut_grid_to_internal(column);
				if ((empty_keyboard_linger_col_[track] & 0x80u) &&
				    (empty_keyboard_linger_col_[track] & 0x0Fu) == (uint8_t)cut_col) {
					empty_keyboard_linger_col_[track] = (uint8_t)cut_col;
					dispatch_event(MLR_EVT_CUT_RELEASE, (uint8_t)track, 0, 0);
					if (!mlr_tracks[track].has_content)
						dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
				}
			}
		}

		/* Gate-mode CUT playback remains momentary: cuts start on key-down,
		 * then stop when the last cut key on that track row is released. */
		if (grid.keyUp() && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track = grid.lastY() - 1;
			int column = grid.lastX();
			if (column >= cs && column <= ce && mlr_gate_mode[track] && mlr_tracks[track].playing) {
				int row = track + 1;
				uint16_t zone_mask = (uint16_t)(((1u << (ce - cs + 1)) - 1u) << cs);
				uint16_t held_in_zone = (uint16_t)(grid.heldRowMask((uint8_t)row) & zone_mask);
				if (held_in_zone == 0) {
					mlr_stop_track(track);
					if (mlr_tracks[track].loop_active) {
						mlr_clear_loop(track);
						dispatch_event(MLR_EVT_LOOP_CLR, (uint8_t)track, 0, 0);
					}
					release_gated_choke_pauses(track);
					dispatch_event(MLR_EVT_STOP, (uint8_t)track, 0, 0);
				}
			}
		}

		/* on key down on track rows */
		if (grid.keyDown() && grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS) {
			int track  = grid.lastY() - 1;
			int column = grid.lastX();

			/* ignore columns outside the cut zone */
			if (column < cs || column > ce) return;

			if (alt_held) {
				/* DELETE + cut key:
				 *   - if track is playing, stop it (this leaves any loop in
				 *     place so the user can press again to clear it);
				 *   - if track is already stopped but has an active loop,
				 *     clear the loop.
				 * Group dissolve is only available from the play column. */
				if (mlr_tracks[track].playing) {
					mlr_stop_track(track);
				} else if (mlr_tracks[track].loop_active) {
					mlr_clear_loop(track);
					dispatch_event(MLR_EVT_LOOP_CLR, (uint8_t)track, 0, 0);
				}
				return;
			}

			/* Remap grid col to internal col (0–15) */
			int cut_col = cut_grid_to_internal(column);
			if (track_outputs_enabled(track)) {
				if (empty_keyboard_linger_col_[track] & 0x80u)
					dispatch_event(MLR_EVT_CUT_RELEASE, (uint8_t)track, 0, 0);
				empty_keyboard_linger_col_[track] = (uint8_t)(0x80u | (uint8_t)cut_col);
			}

			if (rec_armed_track == track) {
				rec_armed_track = -1;
				if (resume_after_arm_track_ == track) resume_after_arm_track_ = -1;
				rec_limit_latched = false;
			}

			/* immediate cut — clears any active loop. For gated tracks this is
			 * monophonic: last key wins. Broadcast to the whole group so each
			 * member cuts to the same grid column (proportional position). */
			group_cut(track, cut_col);
		}

		/* ---- loop-a-section: detect 2+ held keys per track row ---- */
		bool track_row_event = grid.lastY() >= 1 && grid.lastY() <= MLR_NUM_TRACKS;
		if (!alt_held && track_row_event && (grid.keyDown() || grid.keyUp())) {
			int t = grid.lastY() - 1;
			int row = t + 1;
			uint16_t zone_mask = (uint16_t)(((1u << (ce - cs + 1)) - 1u) << cs);
			uint16_t held_in_zone = (uint16_t)(grid.heldRowMask((uint8_t)row) & zone_mask);
			int held_count = __builtin_popcount(held_in_zone);
			int held_min = 16, held_max = -1;
			if (held_in_zone) {
				/* cut_grid_to_internal() is monotonic non-decreasing,
				 * so min/max grid cols map to min/max internal cols. */
				int min_grid_c = __builtin_ctz(held_in_zone);
				int max_grid_c = 31 - __builtin_clz((uint32_t)held_in_zone);
				held_min = cut_grid_to_internal(min_grid_c);
				held_max = cut_grid_to_internal(max_grid_c);
			}

			bool cut_key_released = grid.keyUp() && grid.lastY() == row &&
			                        grid.lastX() >= cs && grid.lastX() <= ce;

			if (!mlr_gate_mode[t] && cut_key_released && cut_loop_pending_[t]) {
				group_set_loop(t, cut_loop_pending_start_[t], cut_loop_pending_end_[t]);
				cut_loop_pending_[t] = false;
			} else if (held_count >= 2 && mlr_tracks[t].has_content) {
				if (mlr_gate_mode[t]) {
					group_set_loop(t, held_min, held_max);
					cut_loop_pending_[t] = false;
				} else {
					cut_loop_pending_[t] = true;
					cut_loop_pending_start_[t] = held_min;
					cut_loop_pending_end_[t] = held_max;
				}
			} else if (held_count == 0) {
				cut_loop_pending_[t] = false;
			}
		}
	}

	/* ================================================================ */
	/* Grid display: shared row 0 pattern/recall LEDs                  */
	/* ================================================================ */

	void __not_in_flash("draw_row0_nav") draw_row0_nav()
	{
		int pcs = pat_col_start();

		/* pattern buttons */
		static uint8_t pat_blink = 0;
		pat_blink++;
		for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
			int b = 4;  /* idle: brighter than recall keys */
			switch (mlr_patterns[p].state) {
			case MLR_PAT_ARMED:
				/* slow flash: toggle every 4 ticks (~200ms on/off) */
				b = (pat_blink & 4) ? 12 : 0;
				break;
			case MLR_PAT_RECORDING:
				/* fast flash: toggle every 2 ticks (~100ms on/off) */
				b = (pat_blink & 2) ? 15 : 0;
				break;
			case MLR_PAT_PLAYING:   b = 15; break;
			case MLR_PAT_STOPPED:   b = 6;  break;
			default:                b = 2;  break;
			}
			grid.frameLedUnchecked(p + pcs, 0, b);
		}

		if (!small_grid_) {
			/* recall buttons: cols 9–12 (16-wide only) */
			for (int r = 0; r < MLR_NUM_RECALLS; r++) {
				int b = 2;
				if (mlr_recalls[r].recording)         b = (pat_blink & (mlr_recalls[r].count ? 2 : 4)) ? 15 : 0;
				else if (mlr_recall_active == r)      b = 15;  /* selected */
				else if (mlr_recalls[r].has_data)     b = 5;   /* has data */
				grid.frameLedUnchecked(r + 9, 0, b);
			}
		}

		/* ALT */
		int ac = alt_col();
		uint8_t alt_bright;
		if (delete_reset_flash_samples_remaining_ > 0) {
			uint32_t elapsed = DELETE_RESET_FLASH_SAMPLES - delete_reset_flash_samples_remaining_;
			alt_bright = ((elapsed / DELETE_RESET_FLASH_PERIOD_SAMPLES) & 1u) ? 0 : 15;
		} else {
			alt_bright = grid_render_delete_held_ ? 12 : 2;
		}
		grid.frameLedUnchecked(ac, 0, alt_bright);
	}

	/* Grid bottom row (row 7 when 6 tracks): master gradient bar.
	 * Lowest level appears at the right, filling toward the left. */
	void __not_in_flash("draw_bottom_vu_row") draw_bottom_vu_row()
	{
		int row = MLR_NUM_TRACKS + 1;
		if (row > 7) return;

		int gw = small_grid_ ? 8 : 16;

		/* Master volume from latched master value (0..4095) as gradient bar right->left.
		 * Full scale reaches col 0. */
		uint32_t master_raw = (uint32_t)mlr_master_level_raw;
		uint32_t master_segs = ((master_raw * (uint32_t)gw) + 4094u) / 4095u;  /* ceil */
		if (master_segs > (uint32_t)gw) master_segs = (uint32_t)gw;
		for (uint32_t i = 0; i < master_segs; i++) {
			int col = (gw - 1) - (int)i;
			uint8_t g = (uint8_t)(2u + (i * 10u) / (uint32_t)(gw - 1));  /* smooth 2..12 ramp */
			grid.frameLedUnchecked(col, row, g);
		}
	}

	/* Bottom-half "extra" rows on a tall grid (rows 8..grid.rows()-1).
	 * A permanent free keyboard layer on 16-row grids. The pitch map repeats
	 * the top half, so middle C is at both (8,4) and (8,12). */
	void __not_in_flash("draw_extra_rows") draw_extra_rows()
	{
		if (!grid.ready() || grid.rows() <= 8) return;
		int rmax = grid.rows();
		if (rmax > MONOME_WS_GRID_MAX_Y) rmax = MONOME_WS_GRID_MAX_Y;
		int cs = cut_col_start();
		int ce = cut_col_end();
		uint16_t zone_mask = (uint16_t)(((1u << (ce - cs + 1)) - 1u) << cs);
		bool show_guide = extra_keyboard_guide_enabled();
		for (int row = 9; row < rmax && row < 15; row++) {
			uint16_t held_in_zone = (uint16_t)(grid.heldRowMask((uint8_t)row) & zone_mask);
			if (show_guide) {
				for (int internal = 0; internal < MLR_GRID_COLS; internal++) {
					uint8_t level = keyboard_guide_level(internal, row);
					if (level)
						grid_frame_led_max(cut_internal_to_grid(internal), row, level);
				}
			}
			uint8_t pat_key = (uint8_t)(extra_keyboard_state_ & EXTRA_PATTERN_KEY_MASK);
			if ((int)(pat_key >> 4) == row - 8) {
				int col = cut_internal_to_grid(pat_key & 0x0F);
				if ((replay_gate_mask_ & (1u << 6)) || grid.held((uint8_t)col, (uint8_t)row))
					grid_frame_led_max(col, row, KEY_HELD_LEVEL);
			}
		}
	}

	/* ================================================================ */
	/* Grid display: REC page                                          */
	/* ================================================================ */

	void __not_in_flash("update_grid_page_rec_slice") update_grid_page_rec_slice(uint8_t phase)
	{
		if (!grid.ready()) return;
		if (phase == 0) {
			grid.frameClear();
			grid.frameLedUnchecked(0, 0, 15);  /* REC = active */
			grid.frameLedUnchecked(1, 0,  4);  /* CUT = dim */
			draw_row0_nav();
			return;
		}
		if (phase > MLR_NUM_TRACKS) {
			draw_bottom_vu_row();
			draw_extra_rows();
			grid.submitFrame();
			return;
		}

		bool recording = (mlr_rec_track >= 0);
		bool rec_pos = grid_render_rec_pos_;
		bool gated_rec_ready = rec_pos && rec_armed_track < 0;

		int pc = play_col();
		bool alt_held = grid_render_delete_action_held_;
		uint8_t cycled = alt_held ? cycled_group_mask() : 0;

		int t = (int)phase - 1;
		{
			int row = t + 1;

			bool has = mlr_tracks[t].has_content;
			bool armed = (rec_armed_track == t);
			bool actively_rec = (recording && t == mlr_rec_track);

			/* col 0 (and col 1 on 16-wide): paired channel-select + arm/status indicator.
			 * On 8x8 grids only col 0 is the arm indicator. */
			uint8_t state_bright;
			if (actively_rec) {
				state_bright = (rec_blink & 2) ? 15 : 4;
			} else if (armed) {
				state_bright = (rec_blink & 8) ? 12 : 3;
			} else if (gated_rec_ready) {
				state_bright = (rec_blink & 4) ? 10 : 3;
			} else {
				state_bright = has ? 6 : 3;
			}
			if ((copy_flash_mask_ & (1u << t)) && copy_flash_samples_remaining_ > 0) {
				bool on = (copy_flash_samples_remaining_ / copy_flash_period_) & 1;
				state_bright = on ? 15 : 0;
			}
			if (small_grid_) {
				/* 8x8: col 0 alone keeps today's record-arm semantics. */
				grid.frameLedUnchecked(0, row, state_bright);
			} else {
				uint8_t ch = mlr_tracks[t].recorded_channel;
				bool user_chosen = mlr_tracks[t].channel_user_chosen;
				uint8_t col0_bright, col1_bright;
				if (!has && !armed && !actively_rec) {
					col0_bright = state_bright;
					col1_bright = state_bright;
				} else {
					uint8_t selected = state_bright;
					uint8_t other    = 0;
					if (!user_chosen && state_bright > 4) {
						other    = 2;
						selected = (uint8_t)(state_bright - 1);
					}
					if (ch == 1) { col0_bright = other;    col1_bright = selected; }
					else         { col0_bright = selected; col1_bright = other;    }
				}
				if ((copy_flash_mask_ & (1u << t)) && copy_flash_samples_remaining_ > 0) {
					bool on = (copy_flash_samples_remaining_ / copy_flash_period_) & 1;
					col0_bright = on ? 15 : 0;
				}
				grid.frameLedUnchecked(0, row, col0_bright);
				grid.frameLedUnchecked(1, row, col1_bright);
			}

			/* play col: play/stop/gate indicator */
			uint8_t play_bright;
			if (mlr_gate_mode[t]) {
				play_bright = (gate_pulse & 8) ? 12 : 4;
				if (mlr_tracks[t].playing) play_bright = 15;
			} else {
				play_bright = mlr_tracks[t].playing ? 15 : (has ? 6 : 3);
			}
			if (cycled && (cycled & (1u << t))) {
				/* DELETE is held: cycle through existing groups, fast-blinking
				 * each group's members in turn so the user can see which play
				 * keys would dissolve which group. */
				play_bright = (gate_pulse & 4) ? 15 : 2;
			}
			if ((group_flash_mask_ & (1u << t)) && group_flash_samples_remaining_ > 0) {
				bool on = (group_flash_samples_remaining_ / group_flash_period_) & 1;
				play_bright = on ? 15 : 0;
			}
			grid.frameLedUnchecked(pc, row, play_bright);

			if (small_grid_) {
				/* 8x8 REC: col 1=reverse, cols 2-6=speed */
				/* record progress: cols 1-6 during recording */
				if (actively_rec) {
					uint32_t progress = mlr_get_rec_progress() * 6 / MLR_MAX_SAMPLES;
					if (progress > 6) progress = 6;
					for (uint32_t c = 0; c < progress; c++)
						grid.frameLedUnchecked(c + 1, row, 12);
					if (progress < 6)
						grid.frameLedUnchecked(progress + 1, row, (rec_blink & 2) ? 8 : 0);
				} else {
					/* reverse: col 1 */
					grid.frameLedUnchecked(1, row, mlr_tracks[t].reverse ? 12 : 4);

					/* speed: cols 2-6 (5 slots) */
					int scol = speed_shift_to_col_small(mlr_tracks[t].speed_shift);
					if (scol >= 2 && scol <= 6)
						grid.frameLedUnchecked(scol, row, 14);
					/* dim 1x indicator (col 4) when not selected */
					if (scol != 4)
						grid.frameLedUnchecked(4, row, 3);
				}
			} else {
				/* 16x8 REC: cols 0-1=channel+arm indicator (drawn above),
				 * cols 2-6=mixer (5 slots), col 7=reverse, cols 8-14=speed */
				/* record progress: cols 2–6 (overlays mixer during recording) */
				if (actively_rec) {
					uint32_t progress = mlr_get_rec_progress() * 5 / MLR_MAX_SAMPLES;
					if (progress > 5) progress = 5;
					for (uint32_t c = 0; c < progress; c++)
						grid.frameLedUnchecked(c + 2, row, 12);
					if (progress < 5)
						grid.frameLedUnchecked(progress + 2, row, (rec_blink & 2) ? 8 : 0);
				} else if (has || armed) {
					/* volume mixer: 5 slots on cols 2..6, slot 1 (col 3) = unity */
					static uint8_t vol_gradient[MLR_NUM_VOL_SLOTS] = { 12, 9, 6, 3, 1 };
					uint8_t cur_slot = mlr_tracks[t].volume_slot;
					if (cur_slot >= MLR_NUM_VOL_SLOTS) cur_slot = MLR_NUM_VOL_SLOTS - 1;
					for (int c = 0; c < MLR_NUM_VOL_SLOTS; c++) {
						if ((uint8_t)c == cur_slot)
							grid.frameLedUnchecked(c + 2, row, 14);  /* active level */
						else if ((uint8_t)c > cur_slot)
							grid.frameLedUnchecked(c + 2, row, vol_gradient[c]);
						/* cols above current level stay off */
					}
				}

				grid.frameLedUnchecked(7, row, mlr_tracks[t].reverse ? 12 : 4);

				/* cols 8–14: speed selection (col 11 = 1x) — show for all tracks */
				int speed_col = mlr_tracks[t].speed_shift + 11;
				if (speed_col >= 8 && speed_col <= 14)
					grid.frameLedUnchecked(speed_col, row, 14);
				if (speed_col != 11)
					grid.frameLedUnchecked(11, row, 3);  /* dim 1x indicator when not selected */
			}
		}
	}

	/* ================================================================ */
	/* Grid display: CUT page                                          */
	/* ================================================================ */

	void __not_in_flash("update_grid_page_cut_slice") update_grid_page_cut_slice(uint8_t phase)
	{
		if (!grid.ready()) return;
		if (phase == 0) {
			grid.frameClear();
			grid.frameLedUnchecked(0, 0,  4);  /* REC = dim */
			grid.frameLedUnchecked(1, 0, 15);  /* CUT = active */
			draw_row0_nav();
			return;
		}
		if (phase > MLR_NUM_TRACKS) {
			draw_empty_track_keyboard_overlay();
			draw_bottom_vu_row();
			draw_extra_rows();
			grid.submitFrame();
			return;
		}

		bool recording = (mlr_rec_track >= 0);

		int cs = cut_col_start();
		int nc = cut_cols();

		int t = (int)phase - 1;
		{
			int row = t + 1;
			bool has = mlr_tracks[t].has_content;
			bool actively_rec = (recording && t == mlr_rec_track);

			/* recording track: progress bar on cut columns */
			if (actively_rec) {
				uint32_t progress = mlr_get_rec_progress() * (uint32_t)nc / MLR_MAX_SAMPLES;
				if (progress > (uint32_t)nc) progress = (uint32_t)nc;
				for (uint32_t c = 0; c < progress; c++)
					grid.frameLedUnchecked((int)c + cs, row, 12);
				if (progress < (uint32_t)nc)
					grid.frameLedUnchecked((int)progress + cs, row, (rec_blink & 2) ? 8 : 0);
				return;
			}

			/* show loop zone dim even when stopped */
			int loop_s = -1, loop_e = -1;
			mlr_get_loop_cols(t, &loop_s, &loop_e);
			if (loop_s >= 0) {
				/* remap internal loop cols to display cols */
				int ds = cut_internal_to_grid(loop_s);
				int de = cut_internal_to_grid(loop_e);
				for (int c = ds; c <= de; c++)
					grid.frameLedUnchecked(c, row, 4);
			}

			if (!has) {
				return;
			}

			/* Gate-paused grouped tracks keep showing their frozen playhead. */
			if (!mlr_tracks[t].playing && !track_gate_paused(t)) return;

			int icol = mlr_get_column(t);  /* 0–15 internal */
			int gcol = cut_internal_to_grid(icol);
			grid.frameLedUnchecked(gcol, row, 15);
		}
	}

	/* ---- grid display: flushing to flash ---- */
	void __not_in_flash("update_grid_flushing") update_grid_flushing()
	{
		if (!grid.ready()) return;
		grid.frameClear();

		int nc = cut_cols();
		int cs = cut_col_start();
		/* saving animation: sweep across cut columns */
		static uint8_t sweep = 0;
		sweep = (sweep + 1) % nc;
		int row = (mlr_get_flush_track() >= 0) ? mlr_get_flush_track() + 1 : 1;
		for (int c = 0; c < nc; c++) {
			int dist = (c - sweep);
			if (dist < 0) dist = -dist;
			int bright = 15 - dist * 2;
			if (bright < 0) bright = 0;
			grid.frameLedUnchecked(c + cs, row, bright);
		}
		draw_bottom_vu_row();
		grid.submitFrame();
	}

	void __not_in_flash("update_grid_slice") update_grid_slice()
	{
		int8_t phase_signed = grid_redraw_phase_;
		if (phase_signed < 0) return;
		uint8_t phase = (uint8_t)phase_signed;
		if (grid_redraw_flushing_)
			update_grid_flushing();
		else if (grid_redraw_page_ == PAGE_REC)
			update_grid_page_rec_slice(phase);
		else
			update_grid_page_cut_slice(phase);

		if (grid_redraw_flushing_ || phase >= MLR_NUM_TRACKS + 1)
			grid_redraw_phase_ = -1;
		else
			grid_redraw_phase_++;
	}

	void __not_in_flash("apply_panel_vu_leds") apply_panel_vu_leds()
	{
		if (!panel_vu_led_pending_) return;
		uint16_t led0 = panel_vu_led_[0];
		uint16_t led1 = panel_vu_led_[1];
		uint16_t led2 = panel_vu_led_[2];
		uint16_t led3 = panel_vu_led_[3];
		uint16_t led4 = panel_vu_led_[4];
		uint16_t led5 = panel_vu_led_[5];
		panel_vu_led_pending_ = false;
		LedBrightness(0, led0);
		LedBrightness(1, led1);
		LedBrightness(2, led2);
		LedBrightness(3, led3);
		LedBrightness(4, led4);
		LedBrightness(5, led5);
	}
};

/* ------------------------------------------------------------------ */
/* C-linkage hook called by mlr.c::event_exec for every replayed      */
/* pattern/recall event. Routes cut events to the same trigger path   */
/* manual gestures use, so CV/pulse outputs replay along with the     */
/* recorded performance.                                              */
/* ------------------------------------------------------------------ */

extern "C" void __not_in_flash_func(mlr_event_playback_hook)(const mlr_event_t *e)
{
	if (!e) return;
	if (!MLRCard::s_card_) return;
	if (e->type == MLR_EVT_CUT || e->type == MLR_EVT_GROUP_CUT) {
		MLRCard::s_card_->pattern_cut_trigger((int)e->track, (int)e->param_a, mlr_event_playback_source);
	} else if (e->type == MLR_EVT_CUT_RELEASE) {
		MLRCard::s_card_->cut_release_trigger((int)e->track);
	} else if (e->type == MLR_EVT_STOP && mlr_event_playback_source == MLR_PLAYBACK_SOURCE_PATTERN) {
		MLRCard::s_card_->pattern_stop_trigger((int)e->track);
	}
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main()
{
	vreg_set_voltage(VREG_VOLTAGE_1_20);
	sleep_ms(10);
	set_sys_clock_khz(192000, true);

	/* DFP: we're the host → normal grid/audio mode */
	MLRCard card;
	card.Run();
}
