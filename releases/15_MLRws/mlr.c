/**
 * mlr.c — MLR track engine v2 implementation.
 *
 * Playback: core 1 decodes ADPCM from flash → PCM ring buffer.
 *           core 0 reads PCM from ring buffer. Never touches flash.
 * Recording: core 0 encodes ADPCM into page ring.
 *            core 1 drains page ring to flash with JIT sector erase.
 * Seeking:   core 0 sets seek target, core 1 flushes ring + refills.
 */

#include "mlr.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "pico/platform.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000
#endif

/* Cold code kept in flash instead of copied to RAM (copy_to_ram binary).
 * Only for functions core 0 never runs while core 1 may be erasing flash:
 * boot-time init, or core-1-only work such as the sample-manager rescan. */
#define MLR_COLD_FUNC __attribute__((section(".flashdata.mlr_cold")))

static void MLR_COLD_FUNC restore_loop_stopped(int track, int col_start, int col_end);

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

mlr_track_t     mlr_tracks[MLR_NUM_TRACKS];
volatile int    mlr_rec_track  = -1;
volatile bool   mlr_flushing   = false;
volatile bool   mlr_copying    = false;
mlr_page_ring_t mlr_page_ring;
volatile uint16_t mlr_master_level_raw = 4095;
volatile bool     mlr_master_override = false;
static volatile uint8_t mlr_wrap_event_mask;

mlr_pattern_t   mlr_patterns[MLR_NUM_PATTERNS];
mlr_recall_t    mlr_recalls[MLR_NUM_RECALLS];
volatile bool   mlr_scene_saving = false;
volatile uint8_t mlr_rot_range_pct = MLR_ROT_RANGE_DEFAULT;
volatile bool   mlr_settings_save_pending = false;
volatile uint8_t mlr_cv2_mode = MLR_CV2_MODE_FILTER;
volatile uint8_t mlr_cv1_target = MLR_CV_TARGET_LAST;
volatile uint8_t mlr_cv2_target = MLR_CV_TARGET_LAST;
volatile bool   mlr_pan_enabled = false;
volatile int    mlr_recall_active = -1;
static uint32_t mlr_clock_ms = 0;

static inline uint32_t page_ring_used_pages(void);

static inline void note_track_wrap(int track)
{
	if (track >= 0 && track < MLR_NUM_TRACKS)
		mlr_wrap_event_mask |= (uint8_t)(1u << track);
}

uint8_t __not_in_flash_func(mlr_consume_wrap_events)(void)
{
	uint8_t mask = mlr_wrap_event_mask;
	mlr_wrap_event_mask = 0;
	return mask;
}

#ifdef MLR_PERF_PROFILING
volatile mlr_perf_t mlr_perf;
volatile uint32_t mlr_perf_reset_request;
volatile uint32_t mlr_perf_process_sample_count;
volatile uint32_t mlr_perf_process_sample_last_us;
volatile uint32_t mlr_perf_process_sample_max_us;
volatile uint32_t mlr_perf_process_sample_overruns;
volatile uint32_t mlr_perf_process_sample_ui_max_us;
volatile uint32_t mlr_perf_process_sample_ui_overruns;
volatile uint32_t mlr_perf_process_sample_audio_max_us;
volatile uint32_t mlr_perf_process_sample_audio_overruns;
volatile uint32_t mlr_perf_ui_section_last_us[MLR_PERF_UI_SECTIONS];
volatile uint32_t mlr_perf_ui_section_max_us[MLR_PERF_UI_SECTIONS];
volatile uint32_t mlr_perf_ui_section_overruns[MLR_PERF_UI_SECTIONS];
volatile uint32_t mlr_perf_pcm_ring_avail[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_pcm_ring_min[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_pcm_underruns[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_seek_underruns[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_reverse_toggle_avail[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_reverse_handoff_avail[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_reverse_handoff_count[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_page_ring_max;
volatile uint32_t mlr_perf_seek_count[MLR_NUM_TRACKS];
volatile uint32_t mlr_perf_grid_frame_drops;
volatile uint32_t mlr_perf_grid_event_drops;
volatile uint32_t mlr_perf_grid_poll_events_last;
volatile uint32_t mlr_perf_grid_poll_events_max;
volatile uint32_t mlr_perf_grid_poll_backlog_last;
volatile uint32_t mlr_perf_grid_poll_backlog_max;
volatile uint32_t mlr_perf_rec_page_flush_count;
volatile uint32_t mlr_perf_scene_save_count;
volatile uint32_t mlr_perf_refill_max_us;
volatile uint32_t mlr_perf_seek_max_us;
volatile uint32_t mlr_perf_flash_erase_max_us;
volatile uint32_t mlr_perf_flash_program_max_us;
volatile uint32_t mlr_perf_adc_mux_resets;
volatile uint32_t mlr_perf_adc_fifo_level_max;
volatile uint32_t mlr_perf_rot_seam_fades;
volatile uint32_t mlr_perf_rot_seam_hard;
volatile uint32_t mlr_perf_rot_preview_waits;
volatile uint32_t mlr_perf_rot_xfade_skips;
volatile uint32_t mlr_perf_rot_map_errors;
volatile uint32_t mlr_perf_rot_samples;
volatile uint32_t mlr_perf_fill_resyncs;
volatile uint32_t mlr_perf_erase_holds;
volatile uint32_t mlr_perf_erase_hold_max_us;
volatile uint32_t mlr_perf_erase_forced;

static inline void perf_update_max(volatile uint32_t *dst, uint32_t value)
{
	if (value > *dst) *dst = value;
}

static inline void perf_note_pcm_avail(int track, uint32_t avail)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if ((mlr_perf_process_sample_count & 7u) != (uint32_t)track) return;
	mlr_perf_pcm_ring_avail[track] = avail;
	if (avail < mlr_perf.pcm_ring_min[track]) {
		mlr_perf.pcm_ring_min[track] = avail;
		mlr_perf_pcm_ring_min[track] = avail;
	}
}

static inline void perf_service_reset_request(void)
{
	if (mlr_perf_reset_request) mlr_perf_reset();
}

static inline void perf_note_page_ring_used(void)
{
	uint32_t used = page_ring_used_pages();
	perf_update_max(&mlr_perf.page_ring_max, used);
	perf_update_max(&mlr_perf_page_ring_max, used);
}

static void perf_flash_erase(uint32_t off, size_t bytes)
{
	uint32_t start = time_us_32();
	flash_range_erase(off, bytes);
	uint32_t elapsed = time_us_32() - start;
	perf_update_max(&mlr_perf.flash_erase_max_us, elapsed);
	perf_update_max(&mlr_perf_flash_erase_max_us, elapsed);
}

static void perf_flash_program(uint32_t off, const uint8_t *data, size_t bytes)
{
	uint32_t start = time_us_32();
	flash_range_program(off, data, bytes);
	uint32_t elapsed = time_us_32() - start;
	perf_update_max(&mlr_perf.flash_program_max_us, elapsed);
	perf_update_max(&mlr_perf_flash_program_max_us, elapsed);
}

void mlr_perf_reset(void)
{
	memset((void *)&mlr_perf, 0, sizeof(mlr_perf));
	mlr_perf_reset_request = 0;
	mlr_perf_process_sample_count = 0;
	mlr_perf_process_sample_last_us = 0;
	mlr_perf_process_sample_max_us = 0;
	mlr_perf_process_sample_overruns = 0;
	mlr_perf_process_sample_ui_max_us = 0;
	mlr_perf_process_sample_ui_overruns = 0;
	mlr_perf_process_sample_audio_max_us = 0;
	mlr_perf_process_sample_audio_overruns = 0;
	memset((void *)mlr_perf_ui_section_last_us, 0, sizeof(mlr_perf_ui_section_last_us));
	memset((void *)mlr_perf_ui_section_max_us, 0, sizeof(mlr_perf_ui_section_max_us));
	memset((void *)mlr_perf_ui_section_overruns, 0, sizeof(mlr_perf_ui_section_overruns));
	memset((void *)mlr_perf_pcm_ring_avail, 0, sizeof(mlr_perf_pcm_ring_avail));
	memset((void *)mlr_perf_pcm_underruns, 0, sizeof(mlr_perf_pcm_underruns));
	memset((void *)mlr_perf_seek_underruns, 0, sizeof(mlr_perf_seek_underruns));
	memset((void *)mlr_perf_reverse_toggle_avail, 0, sizeof(mlr_perf_reverse_toggle_avail));
	memset((void *)mlr_perf_reverse_handoff_avail, 0, sizeof(mlr_perf_reverse_handoff_avail));
	memset((void *)mlr_perf_reverse_handoff_count, 0, sizeof(mlr_perf_reverse_handoff_count));
	mlr_perf_page_ring_max = 0;
	memset((void *)mlr_perf_seek_count, 0, sizeof(mlr_perf_seek_count));
	mlr_perf_grid_frame_drops = 0;
	mlr_perf_grid_event_drops = 0;
	mlr_perf_grid_poll_events_last = 0;
	mlr_perf_grid_poll_events_max = 0;
	mlr_perf_grid_poll_backlog_last = 0;
	mlr_perf_grid_poll_backlog_max = 0;
	mlr_perf_rec_page_flush_count = 0;
	mlr_perf_scene_save_count = 0;
	mlr_perf_refill_max_us = 0;
	mlr_perf_seek_max_us = 0;
	mlr_perf_flash_erase_max_us = 0;
	mlr_perf_flash_program_max_us = 0;
	mlr_perf_adc_mux_resets = 0;
	mlr_perf_adc_fifo_level_max = 0;
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_perf.pcm_ring_min[t] = MLR_RING_SAMPLES;
		mlr_perf_pcm_ring_min[t] = MLR_RING_SAMPLES;
	}
}

void mlr_perf_note_ui_section_us(uint32_t section, uint32_t elapsed_us)
{
	const uint32_t sample_budget_us = 1000000u / MLR_SAMPLE_RATE_HZ;
	if (section >= MLR_PERF_UI_SECTIONS) return;
	mlr_perf_ui_section_last_us[section] = elapsed_us;
	if (elapsed_us > mlr_perf_ui_section_max_us[section])
		mlr_perf_ui_section_max_us[section] = elapsed_us;
	if (elapsed_us > sample_budget_us)
		mlr_perf_ui_section_overruns[section]++;
}

void mlr_perf_count_grid_frame_drop(void)
{
	mlr_perf.grid_frame_drops++;
	mlr_perf_grid_frame_drops++;
}

void mlr_perf_count_monome_ws_event_drop(void)
{
	mlr_perf.grid_event_drops++;
	mlr_perf_grid_event_drops++;
}

void mlr_perf_count_adc_mux_reset(void)
{
	mlr_perf_adc_mux_resets++;
}

void mlr_perf_note_adc_fifo_level(uint32_t level)
{
	perf_update_max(&mlr_perf_adc_fifo_level_max, level);
}

void mlr_perf_note_grid_poll(uint32_t processed, uint32_t backlog_before, uint32_t backlog_after)
{
	uint32_t backlog_max = backlog_before > backlog_after ? backlog_before : backlog_after;
	mlr_perf_grid_poll_events_last = processed;
	mlr_perf.grid_poll_backlog_last = backlog_after;
	mlr_perf_grid_poll_backlog_last = backlog_after;
	perf_update_max(&mlr_perf.grid_poll_events_max, processed);
	perf_update_max(&mlr_perf_grid_poll_events_max, processed);
	perf_update_max(&mlr_perf.grid_poll_backlog_max, backlog_max);
	perf_update_max(&mlr_perf_grid_poll_backlog_max, backlog_max);
}

void mlr_perf_count_process_sample(void)
{
	mlr_perf.process_sample_count++;
	mlr_perf_process_sample_count++;
}

void mlr_perf_note_process_sample_us(uint32_t elapsed_us, bool ui_tick)
{
	const uint32_t sample_budget_us = 1000000u / MLR_SAMPLE_RATE_HZ;
	mlr_perf_process_sample_last_us = elapsed_us;
	if (elapsed_us > mlr_perf_process_sample_max_us)
		mlr_perf_process_sample_max_us = elapsed_us;
	if (elapsed_us > sample_budget_us) {
		mlr_perf_process_sample_overruns++;
		if (ui_tick)
			mlr_perf_process_sample_ui_overruns++;
		else
			mlr_perf_process_sample_audio_overruns++;
	}
	if (ui_tick) {
		if (elapsed_us > mlr_perf_process_sample_ui_max_us)
			mlr_perf_process_sample_ui_max_us = elapsed_us;
	} else {
		if (elapsed_us > mlr_perf_process_sample_audio_max_us)
			mlr_perf_process_sample_audio_max_us = elapsed_us;
	}
}

#define PERF_NOTE_PCM_AVAIL(track, avail) perf_note_pcm_avail((track), (avail))
#define PERF_NOTE_PCM_UNDERRUN(track) do { \
	mlr_perf.pcm_underruns[(track)]++; \
	mlr_perf_pcm_underruns[(track)]++; \
	if (mlr_tracks[(track)].fill_seek_pending || mlr_tracks[(track)].seek_reverse_pending) \
		mlr_perf_seek_underruns[(track)]++; \
} while (0)
#define PERF_NOTE_REVERSE_TOGGLE(track) do { \
	if ((track) >= 0 && (track) < MLR_NUM_TRACKS) \
		mlr_perf_reverse_toggle_avail[(track)] = pcm_ring_avail(&mlr_tracks[(track)].pcm); \
} while (0)
#define PERF_NOTE_REVERSE_HANDOFF(track, avail) do { \
	if ((track) >= 0 && (track) < MLR_NUM_TRACKS) { \
		mlr_perf_reverse_handoff_avail[(track)] = (avail); \
		mlr_perf_reverse_handoff_count[(track)]++; \
	} \
} while (0)
#define PERF_NOTE_PAGE_RING_USED() perf_note_page_ring_used()
#define PERF_FLASH_ERASE(off, bytes) perf_flash_erase((off), (bytes))
#define PERF_FLASH_PROGRAM(off, data, bytes) perf_flash_program((off), (data), (bytes))
#define PERF_SERVICE_RESET_REQUEST() perf_service_reset_request()
#else
#define PERF_NOTE_PCM_AVAIL(track, avail) do { (void)(track); (void)(avail); } while (0)
#define PERF_NOTE_PCM_UNDERRUN(track) do { (void)(track); } while (0)
#define PERF_NOTE_REVERSE_TOGGLE(track) do { (void)(track); } while (0)
#define PERF_NOTE_REVERSE_HANDOFF(track, avail) do { (void)(track); (void)(avail); } while (0)
#define PERF_NOTE_PAGE_RING_USED() do { } while (0)
#define PERF_FLASH_ERASE(off, bytes) flash_range_erase((off), (bytes))
#define PERF_FLASH_PROGRAM(off, data, bytes) flash_range_program((off), (data), (bytes))
#define PERF_SERVICE_RESET_REQUEST() do { } while (0)
#endif

volatile uint8_t mlr_event_playback_source = MLR_PLAYBACK_SOURCE_NONE;

/* Per-track group membership bitmask. Invariant: every member of a group
 * has the same mask; the mask always contains the member's own bit.
 * Default = solo, i.e. mlr_track_groups[t] == (1 << t). */
uint8_t         mlr_track_groups[MLR_NUM_TRACKS];

/* Per-track gated-playback flag (set by main.cpp UI; persisted via scene). */
bool            mlr_gate_mode[MLR_NUM_TRACKS];

/* Recording state */
static int      rec_track_idx     = -1;
static uint32_t rec_flash_offset;       /* next flash write offset */
static uint32_t rec_next_erase;         /* next sector to erase */
static uint32_t rec_samples;
static uint32_t rec_bytes;
static uint32_t rec_num_keyframes;
static mlr_keyframe_channels_t rec_keyframes[MLR_MAX_KEYFRAMES];
static adpcm_state_t  rec_enc_state[MLR_NUM_CHANNELS];
static bool           rec_nybble_phase;
static uint8_t        rec_pending_byte;

/* Header write state */
static volatile bool hdr_write_pending = false;
static int           hdr_write_track   = -1;
static bool          hdr_write_start_playback = false;
static uint8_t       hdr_rewrite_pending_mask = 0;

/* Clear state */
static volatile bool clear_pending = false;
static int           clear_track   = -1;

/* Track copy state (core 0 enqueues, core 1 copies flash page-by-page). */
static volatile bool copy_pending = false;
static int           copy_src_track = -1;
static int           copy_dst_track = -1;
static uint8_t       copy_dst_mask = 0;
static uint32_t      copy_bytes_total = 0;
static uint32_t      copy_bytes_done = 0;
static uint32_t      copy_next_erase = 0;
static uint8_t       copy_page[MLR_PAGE_SIZE] __attribute__((aligned(4)));

/* Scene save state (core 1) — serialize, then erase/program pages */
static volatile bool scene_save_requested = false;
static volatile bool scene_save_pending = false;
static volatile bool scene_save_followup_requested = false;
static int           scene_save_sector  = 0;   /* 0..MLR_SCENE_SECTORS-1 */
static bool          scene_sector_erased = false;
static int           scene_page_idx     = 0;   /* 0..pages_per_sector-1 */
static uint32_t      scene_save_bytes = 0;
static uint32_t      scene_save_total_pages = 0;
static uint32_t      scene_save_total_sectors = 0;
#define SCENE_PAGES_PER_SECTOR (MLR_SECTOR_SIZE / MLR_PAGE_SIZE)  /* 16 */

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

#if (MLR_RING_SAMPLES & (MLR_RING_SAMPLES - 1)) != 0
#error "MLR_RING_SAMPLES must be a power of two"
#endif
#define MLR_RING_MASK (MLR_RING_SAMPLES - 1u)

static inline void transition_update_track_state(mlr_track_t *tr)
{
	uintptr_t idx = (uintptr_t)(tr - mlr_tracks);
	uint8_t bit = idx < MLR_NUM_TRACKS ? (uint8_t)(1u << idx) : 0;
	(void)bit;
	uint8_t flags = 0;
	if (tr->seek_handoff_pending) flags |= MLR_TRANS_HANDOFF;
	if (tr->seek_xfade_active) flags |= MLR_TRANS_XFADE;
	if (tr->fade_out_active || tr->fade_in_count > 0) flags |= MLR_TRANS_FADE;
	if (tr->wrap_preview_ready) flags |= MLR_TRANS_WRAP;
	tr->transition_flags = flags;
}

static inline const uint8_t *track_audio_xip(int t)
{
	return (const uint8_t *)(XIP_BASE + MLR_TRACK_OFFSET(t) + MLR_HEADER_SIZE);
}

static inline uint32_t track_audio_flash_off(int t)
{
	return MLR_TRACK_OFFSET(t) + MLR_HEADER_SIZE;
}

static inline uint32_t track_hdr_flash_off(int t)
{
	return MLR_TRACK_OFFSET(t);
}

/* A track's keyframes, straight from its header in flash. Core 1 only (it
 * never reads them mid-erase); valid once the header is written, which is
 * always before the track can play. */
static inline const mlr_keyframe_channels_t *track_keyframes(int t)
{
	return ((const mlr_track_header_t *)(XIP_BASE + MLR_TRACK_OFFSET(t)))->keyframes;
}

#ifdef MLR_PERF_PROFILING
#define PERF_ROT(counter) (mlr_perf_rot_##counter++)
#else
#define PERF_ROT(counter) ((void)0)
#endif
#ifdef MLR_PERF_PROFILING
/* Self-check: the fill decoder's raw audio position must equal the rotated
 * position for grid position v. */
#define PERF_ROT_CHECK(tr, v, ws, we) do { \
	uint32_t raw_ = (tr)->fill_byte_pos * 2u + ((tr)->fill_high_nyb ? 1u : 0u); \
	mlr_perf_rot_samples++; \
	if (raw_ != fill_audio_pos((tr), (v), (ws), (we))) mlr_perf_rot_map_errors++; \
} while (0)
#else
#define PERF_ROT_CHECK(tr, v, ws, we) ((void)0)
#endif


/* Where pcm.w/r reset or the fill restarts from scratch. */
static inline void fill_rot_reset(mlr_track_t *tr)
{
	tr->fill_rot = 0;
	tr->rot_hist_w = 0;
	tr->rot_hist_n = 0;
}

static inline uint32_t pcm_ring_avail(mlr_pcm_ring_t *r)
{
	return r->w - r->r;
}

static inline uint32_t pcm_ring_free(mlr_pcm_ring_t *r)
{
	return MLR_RING_SAMPLES - pcm_ring_avail(r);
}

static inline uint32_t page_ring_used_pages(void)
{
	return (uint8_t)(mlr_page_ring.w - mlr_page_ring.r);
}

/* Convert speed slot to 8.8 fixed-point factor.
 * Slots are: -3=-2 oct, -2=-1 oct, -1=-5th, 0=1x,
 *            +1=+5th, +2=+1 oct, +3=+2 oct. */
static inline uint16_t speed_shift_to_frac(int8_t s)
{
	static uint16_t kSpeedFrac[7] = {
		64,   /* -2 octaves = 0.25x */
		128,  /* -1 octave  = 0.5x  */
		171,  /* -5th       ≈ 0.667x */
		256,  /* unison     = 1.0x  */
		384,  /* +5th       = 1.5x  */
		512,  /* +1 octave  = 2.0x  */
		1024, /* +2 octaves = 4.0x  */
	};
	if (s < -3) s = -3;
	if (s > 3) s = 3;
	return kSpeedFrac[s + 3];
}

/* Convert volume slot (0=loudest, MLR_NUM_VOL_SLOTS-1=quietest) to 8.8
 * fixed-point.  Slot 1 = unity (0 dB).  Slot 0 boosts above unity for
 * intentional soft clipping into the ADPCM codec. */
static inline uint16_t volume_slot_to_frac(uint8_t slot)
{
	/* 5 levels mapped to grid cols 2..6. Slot 1 = unity. The previous loudest
	 * +6 dB slot was removed (the new col 0/1 region carries the per-track
	 * input-channel indicator instead). */
	static uint16_t kVolFrac[MLR_NUM_VOL_SLOTS] = {
		362,  /* 0: +3 dB  (boost) — grid col 2  */
		256,  /* 1: unity  (0 dB)  — grid col 3  */
		128,  /* 2:        (-6 dB) — grid col 4  */
		64,   /* 3:       (-12 dB) — grid col 5  */
		0,    /* 4: silent         — grid col 6  */
	};
	if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
	return kVolFrac[slot];
}

static inline int16_t clamp16_interp(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

static inline int16_t linear_interp_q8(int16_t x0, int16_t x1, uint8_t frac)
{
	if (frac == 0) return x0;
	int32_t y = (int32_t)x0 + ((((int32_t)x1 - (int32_t)x0) * (int32_t)frac + 128) >> 8);
	return clamp16_interp(y);
}

static inline uint32_t integer_speed_steps(uint16_t speed_frac)
{
	if (speed_frac == 256) return 1;
	if (speed_frac == 512) return 2;
	if (speed_frac == 1024) return 4;
	return 0;
}

static inline uint32_t wrap_preview_source_span(uint16_t speed_frac, uint16_t speed_accum)
{
	uint32_t source_span = (((uint32_t)MLR_SEEK_PREVIEW_SAMPLES *
		(uint32_t)speed_frac) + (uint32_t)speed_accum) >> 8;
	return source_span ? source_span : 1u;
}

static inline bool wrap_preview_should_start(uint32_t distance, uint16_t speed_frac, uint16_t speed_accum)
{
	uint32_t numerator = distance << 8;
	if (numerator < speed_accum) return false;
	uint32_t samples_until_wrap = numerator - speed_accum;
	uint32_t lower_bound = (uint32_t)(MLR_SEEK_PREVIEW_SAMPLES - 1u) * (uint32_t)speed_frac;
	uint32_t upper_bound = (uint32_t)MLR_SEEK_PREVIEW_SAMPLES * (uint32_t)speed_frac;
	return samples_until_wrap > lower_bound && samples_until_wrap <= upper_bound;
}

static inline void reset_track_audio_state(mlr_track_t *tr)
{
	tr->speed_accum = 0;
	memset(tr->last_pcm, 0, sizeof(tr->last_pcm));
	memset(tr->interp_prev, 0, sizeof(tr->interp_prev));
	memset(tr->last_out, 0, sizeof(tr->last_out));
	memset(tr->out_hist, 0, sizeof(tr->out_hist));
	tr->out_hist_pos = 0;
	tr->declick_hist_pos = 0;
	tr->declick_count = 0;
	tr->stop_pending = false;
	tr->fade_out_count = 0;
	tr->fade_in_count = 0;
	tr->fade_out_active = false;
	tr->seek_preview_count = 0;
	tr->seek_xfade_pos = 0;
	tr->seek_xfade_active = false;
	tr->seek_handoff_reverse_pending = false;
	tr->wrap_preview_ready = false;
	transition_update_track_state(tr);
}

static inline void begin_track_declick(mlr_track_t *tr, bool stop_after)
{
	tr->declick_hist_pos = tr->out_hist_pos;
	tr->declick_count = MLR_DECLICK_SAMPLES;
	tr->stop_pending = stop_after;
	transition_update_track_state(tr);
}

static inline void check_seek_handoff(mlr_track_t *tr)
{
	if (tr->seek_handoff_pending && tr->seek_handoff_start_pending) {
		tr->pcm.r = tr->seek_handoff_r;
		tr->playhead = tr->seek_handoff_playhead;
		tr->speed_accum = tr->seek_handoff_speed_accum;
		if (tr->seek_handoff_reverse_pending) {
			tr->reverse = tr->seek_handoff_reverse_target;
			tr->seek_handoff_reverse_pending = false;
		}
		tr->stop_pending = false;
		tr->playing = true;
		tr->seek_handoff_pending = false;
		tr->fade_in_count = MLR_FADE_SAMPLES;
		transition_update_track_state(tr);
	} else if (tr->seek_handoff_pending && !tr->seek_xfade_active) {
		tr->fade_out_active = false;
		tr->fade_out_count = 0;
		tr->fade_in_count = 0;
		tr->seek_xfade_pos = 0;
		tr->seek_xfade_active = (tr->seek_preview_count > 0);
		tr->seek_handoff_pending = false;
		if (!tr->seek_xfade_active) {
			tr->pcm.r = tr->seek_handoff_r;
			tr->playhead = tr->seek_handoff_playhead;
			tr->speed_accum = tr->seek_handoff_speed_accum;
			if (tr->seek_handoff_reverse_pending) {
				tr->reverse = tr->seek_handoff_reverse_target;
				tr->seek_handoff_reverse_pending = false;
			}
			tr->fade_in_count = MLR_FADE_SAMPLES;
		}
		transition_update_track_state(tr);
	}
}

static inline void trigger_track_fade_and_seek(mlr_track_t *tr, uint32_t target, bool start_pending)
{
	tr->seek_xfade_active = false;
	tr->seek_xfade_pos = 0;
	tr->seek_preview_count = 0;
	tr->seek_handoff_pending = false;
	tr->seek_handoff_reverse_pending = false;
	tr->wrap_preview_ready = false;
	if (start_pending) {
		tr->fade_out_active = false;
		tr->fade_out_count = 0;
	} else {
		tr->fade_out_active = false;
		tr->fade_out_count = 0;
	}
	tr->seek_target_sample = target;
	tr->seek_start_pending = start_pending;
	__dmb();
	tr->fill_seek_pending = true;
	transition_update_track_state(tr);
}

static inline void maybe_start_wrap_preview_xfade(mlr_track_t *tr, uint32_t wrap_start, uint32_t wrap_end)
{
	if (!tr->wrap_preview_ready || tr->seek_xfade_active || tr->seek_handoff_pending ||
	    tr->fill_seek_pending || tr->stop_pending || tr->fade_out_active)
		return;
	uint32_t source_span = wrap_preview_source_span(tr->speed_frac, tr->speed_accum);
	if (wrap_end <= wrap_start || wrap_end - wrap_start <= source_span) return;
	if (tr->wrap_preview_reverse != tr->reverse ||
	    tr->wrap_preview_start != wrap_start ||
	    tr->wrap_preview_end != wrap_end ||
	    tr->wrap_preview_speed_frac != tr->speed_frac) {
		tr->wrap_preview_ready = false;
		transition_update_track_state(tr);
		return;
	}

	uint32_t distance = tr->reverse
		? (tr->playhead >= wrap_start ? tr->playhead - wrap_start + 1u : 0u)
		: (tr->playhead < wrap_end ? wrap_end - tr->playhead : 0u);
	if (!wrap_preview_should_start(distance, tr->speed_frac, tr->speed_accum)) return;
	if (tr->seek_preview_count < MLR_SEEK_PREVIEW_SAMPLES) return;

	uint32_t needed = source_span * 2u;
	if (tr->pcm.w - tr->pcm.r < needed) return;
	/* The preview must be for this wrap (its rotation is per wrap). */
	int32_t ahead = (int32_t)(tr->wrap_preview_ring_idx - tr->pcm.r);
	if (ahead <= 0 || (uint32_t)ahead > needed) {
		PERF_ROT(xfade_skips);
		return;
	}

	tr->seek_preview_count = MLR_SEEK_PREVIEW_SAMPLES;
	tr->seek_xfade_pos = 0;
	tr->seek_handoff_r = tr->pcm.r + needed;
	tr->seek_handoff_speed_accum = (uint16_t)((tr->speed_accum +
		((uint32_t)MLR_SEEK_PREVIEW_SAMPLES * tr->speed_frac)) & 0xFFu);
	if (tr->reverse) {
		tr->seek_handoff_playhead = (wrap_end - 1u) - source_span;
	} else {
		tr->seek_handoff_playhead = wrap_start + source_span;
	}
	tr->seek_xfade_active = true;
	tr->wrap_preview_ready = false;
	note_track_wrap((int)(tr - mlr_tracks));
	transition_update_track_state(tr);
}

static inline int32_t apply_seek_preview_xfade(mlr_track_t *tr, int32_t old_sample)
{
	if (!tr->seek_xfade_active) return old_sample;

	uint16_t pos = tr->seek_xfade_pos;
	uint16_t count = tr->seek_preview_count;
	if (count == 0 || pos >= count) {
		tr->seek_xfade_active = false;
		transition_update_track_state(tr);
		return old_sample;
	}

	int32_t new_sample = ((int32_t)tr->seek_preview[pos] * (int32_t)tr->volume_frac) >> 8;
	uint32_t new_gain = (uint32_t)pos + 1u;
	uint32_t old_gain = (uint32_t)count - new_gain;
	int32_t mixed;
	if (count == MLR_SEEK_PREVIEW_SAMPLES) {
		mixed = (old_sample * (int32_t)old_gain + new_sample * (int32_t)new_gain)
			>> MLR_SEEK_PREVIEW_SHIFT;
	} else {
		mixed = (old_sample * (int32_t)old_gain + new_sample * (int32_t)new_gain) / (int32_t)count;
	}

	pos++;
	tr->seek_xfade_pos = pos;
	if (pos >= count) {
		tr->seek_xfade_active = false;
		tr->seek_preview_count = 0;
		tr->pcm.r = tr->seek_handoff_r;
		tr->playhead = tr->seek_handoff_playhead;
		tr->speed_accum = tr->seek_handoff_speed_accum;
		if (tr->seek_handoff_reverse_pending) {
			tr->reverse = tr->seek_handoff_reverse_target;
			tr->seek_handoff_reverse_pending = false;
		}
		tr->last_pcm[0] = tr->seek_preview[count - 1u];
		tr->interp_prev[0] = tr->last_pcm[0];
		transition_update_track_state(tr);
	}

	return mixed;
}

static inline int16_t apply_declick_sample(const mlr_track_t *tr, int ch, int32_t target)
{
	uint32_t alpha = (uint32_t)(MLR_DECLICK_SAMPLES - tr->declick_count) + 1u; /* 1..N */
	if (alpha > MLR_DECLICK_SAMPLES) alpha = MLR_DECLICK_SAMPLES;
	uint8_t hist_idx = (uint8_t)((tr->declick_hist_pos + (uint8_t)(alpha - 1u)) & (MLR_DECLICK_SAMPLES - 1u));
	int32_t old = tr->out_hist[ch][hist_idx];
	return (int16_t)(((old * (int32_t)(MLR_DECLICK_SAMPLES - alpha)) +
	                 target * (int32_t)alpha) >> MLR_DECLICK_SHIFT);
}

static inline void push_track_output(mlr_track_t *tr, const int16_t *samples)
{
	uint8_t pos = tr->out_hist_pos;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		tr->last_out[ch] = samples[ch];
		tr->out_hist[ch][pos] = samples[ch];
	}
	tr->out_hist_pos = (uint8_t)((pos + 1u) & (MLR_DECLICK_SAMPLES - 1u));
}

static inline void finish_pending_stop(mlr_track_t *tr)
{
	tr->playing = false;
	tr->pcm.r = tr->pcm.w;
	reset_track_audio_state(tr);
}

/* ------------------------------------------------------------------ */
/* Track group helpers                                                */
/* ------------------------------------------------------------------ */

void mlr_groups_default(void)
{
	for (int t = 0; t < MLR_NUM_TRACKS; t++)
		mlr_track_groups[t] = (uint8_t)(1u << t);
}

/* Remove track t from its current group; the remaining members stay
 * grouped together. No-op for solo tracks. */
void mlr_leave_group(int t)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	uint8_t self = (uint8_t)(1u << t);
	uint8_t old = mlr_track_groups[t];
	if (old == self) return;  /* already solo */
	uint8_t remainder = (uint8_t)(old & ~self);
	for (int u = 0; u < MLR_NUM_TRACKS; u++) {
		if (remainder & (1u << u))
			mlr_track_groups[u] = remainder;
	}
	mlr_track_groups[t] = self;
}

/* ------------------------------------------------------------------ */
/* Choke-group helpers — apply an action to track t and stop the other */
/* members of its group. The group can host at most one playing track  */
/* at a time. mlr_group_stop_track() stops every member of the group   */
/* (cheap no-op for members that are already stopped under choke).     */
/* Forward decls of the single-track engine ops follow below.          */
/* ------------------------------------------------------------------ */

void mlr_cut(int track, int column);
void mlr_stop_track(int track);
void mlr_set_loop(int track, int col_start, int col_end);
void mlr_clear_loop(int track);

/* Volatile function pointer prevents the compiler from inlining 6 copies
 * of mlr_stop_track into mlr_group_stop_track / stop_other_group_members. */
void mlr_group_stop_track(int t)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	void (*volatile fn)(int) = mlr_stop_track;
	uint8_t mask = mlr_track_groups[t];
	for (int u = 0; u < MLR_NUM_TRACKS; u++)
		if (mask & (1u << u)) fn(u);
}

static void stop_other_group_members(int t)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	void (*volatile fn)(int) = mlr_stop_track;
	uint8_t mask = mlr_track_groups[t];
	if (mask == (uint8_t)(1u << t)) return;
	for (int u = 0; u < MLR_NUM_TRACKS; u++) {
		if (u == t) continue;
		if (mask & (1u << u)) fn(u);
	}
}

void mlr_choke_group_cut(int t, int col)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	mlr_clear_loop(t);
	mlr_cut(t, col);
	stop_other_group_members(t);
}

/* Choke-resume: like choke-cut but does NOT clear the track's loop.
 * Used by REC-page play-toggle so that resuming a stopped track stays
 * inside its previous loop boundaries. */
void mlr_choke_group_resume(int t, int col)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	mlr_cut(t, col);
	stop_other_group_members(t);
}

/* Equal-power pan gains, Q15: index = pan + MLR_PAN_STEPS, from hard Out 1
 * (32768, 0) through centre (23170, 23170) to hard Out 2 (0, 32768). Hard
 * pans are exactly unity, matching the old per-channel routing. */
static const uint16_t pan_gain_out1[2 * MLR_PAN_STEPS + 1] = {
	32768, 32488, 31651, 30274, 28378, 25997, 23170, 19948, 16384, 12540, 8481, 4277, 0,
};  /* round(32768 * cos(i / 12 * pi / 2)); Out 2 gain is the mirror entry */
#if MLR_PAN_STEPS != 6
#error pan_gain tables assume MLR_PAN_STEPS == 6
#endif

void __not_in_flash_func(mlr_pan_gains)(int pan, uint16_t *gl, uint16_t *gr)
{
	if (pan < -MLR_PAN_STEPS) pan = -MLR_PAN_STEPS;
	if (pan >  MLR_PAN_STEPS) pan =  MLR_PAN_STEPS;
	*gl = pan_gain_out1[pan + MLR_PAN_STEPS];
	*gr = pan_gain_out1[MLR_PAN_STEPS - pan];
}

/* Out 1 gain at a fractional pan position, x = (pan + MLR_PAN_STEPS) * 256.
 * Linear between table steps keeps power within ~0.5% of equal-power. */
static inline uint16_t pan_gain_interp(int x)
{
	if (x <= 0) return pan_gain_out1[0];
	if (x >= 2 * MLR_PAN_STEPS * 256) return pan_gain_out1[2 * MLR_PAN_STEPS];
	int i = x >> 8, f = x & 255;
	int a = pan_gain_out1[i], b = pan_gain_out1[i + 1];
	return (uint16_t)(a + (((b - a) * f) >> 8));
}

static inline int __not_in_flash_func(pan_effective_q8)(const mlr_track_t *tr)
{
	int e = (int)(tr->pan_pos >> 12) + tr->pan_cv_q8;  /* pan_pos is 1/2^20 steps */
	if (e < -MLR_PAN_STEPS * 256) e = -MLR_PAN_STEPS * 256;
	if (e >  MLR_PAN_STEPS * 256) e =  MLR_PAN_STEPS * 256;
	return e;
}

int __not_in_flash_func(mlr_pan_effective_q8)(int track)
{
	return pan_effective_q8(&mlr_tracks[track]);
}

/* Recompute target gains from the (gliding) base pan + CV offset. Gains come
 * off the equal-power table at every glide position, so a sweep holds level. */
static void __not_in_flash_func(pan_update_gains)(mlr_track_t *tr)
{
	int e = pan_effective_q8(tr);
	tr->pan_gl = pan_gain_interp(e + MLR_PAN_STEPS * 256);
	tr->pan_gr = pan_gain_interp(MLR_PAN_STEPS * 256 - e);
	if (!tr->playing) {  /* nothing audible to slew */
		tr->pan_gl_cur = tr->pan_gl;
		tr->pan_gr_cur = tr->pan_gr;
	}
}

void __not_in_flash_func(mlr_set_pan_cv)(int track, int offset_q8)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (offset_q8 < -2 * MLR_PAN_STEPS * 256) offset_q8 = -2 * MLR_PAN_STEPS * 256;
	if (offset_q8 >  2 * MLR_PAN_STEPS * 256) offset_q8 =  2 * MLR_PAN_STEPS * 256;
	mlr_track_t *tr = &mlr_tracks[track];
	if (tr->pan_cv_q8 == offset_q8) return;
	tr->pan_cv_q8 = (int16_t)offset_q8;
	pan_update_gains(tr);
}

int __not_in_flash_func(mlr_pan_default)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return -MLR_PAN_STEPS;
	return (mlr_tracks[track].recorded_channel & 0x01) ? MLR_PAN_STEPS : -MLR_PAN_STEPS;
}

void __not_in_flash_func(mlr_set_pan)(int track, int pan)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (pan < -MLR_PAN_STEPS) pan = -MLR_PAN_STEPS;
	if (pan >  MLR_PAN_STEPS) pan =  MLR_PAN_STEPS;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->pan = (int8_t)pan;
	/* Playing tracks glide there one step per 100 ms (mix_track_route); a
	 * stopped track has nothing audible to glide, so it lands now. */
	if (!tr->playing) tr->pan_pos = pan * (1 << 20);
	pan_update_gains(tr);
}

/* Ladder stage coefficients, Q15 (G = g / (1 + g), g = tan(pi * fc / 24000)),
 * chosen so the whole resonant (k = 1) ladder is -3 dB at 250 Hz, 600 Hz,
 * 1.5 kHz and 4 kHz, peaking +2.3 dB just below; then open (passthrough). */
static const uint16_t lpf_coef[MLR_LPF_SLOTS] = { 1062, 2441, 5539, 12165, 32768 };

/* Ladder input stage: tanh over 0..2x full scale in steps of 1/64, Q15.
 * Unity gain for small signals; rounds full-scale peaks by ~2.4 dB. */
static const int16_t lpf_tanh[129] = {
	0, 512, 1024, 1535, 2045, 2555, 3063, 3570, 4075, 4578, 5079, 5577,
	6073, 6566, 7056, 7542, 8025, 8505, 8980, 9452, 9919, 10382, 10840, 11294,
	11742, 12186, 12625, 13058, 13486, 13908, 14325, 14737, 15142, 15542, 15936, 16324,
	16706, 17081, 17451, 17815, 18173, 18524, 18869, 19208, 19541, 19868, 20189, 20503,
	20812, 21114, 21411, 21701, 21986, 22264, 22537, 22804, 23065, 23320, 23570, 23815,
	24053, 24287, 24515, 24738, 24955, 25168, 25375, 25578, 25775, 25968, 26156, 26339,
	26518, 26693, 26863, 27029, 27190, 27348, 27501, 27650, 27796, 27937, 28075, 28210,
	28340, 28468, 28592, 28712, 28829, 28943, 29054, 29162, 29267, 29369, 29469, 29565,
	29659, 29750, 29839, 29925, 30009, 30090, 30169, 30246, 30321, 30393, 30464, 30532,
	30599, 30663, 30726, 30787, 30846, 30903, 30959, 31013, 31066, 31117, 31166, 31215,
	31261, 31306, 31350, 31393, 31435, 31475, 31514, 31552, 31588
};

/* Q15 multiply for ladder values up to |d| < 2^23 without 64-bit math. */
static inline int32_t __not_in_flash_func(lpf_mul)(int32_t d, int32_t g)
{
	return (((d >> 8) * g) >> 7) + (((d & 255) * g) >> 15);
}

/* Cache G and the zero-delay feedback terms: G^4 and 1 / (1 + G^4), Q15. */
static inline void __not_in_flash_func(lpf_set_coef)(mlr_track_t *tr, uint16_t g)
{
	uint32_t g2 = ((uint32_t)g * g) >> 15;
	uint32_t g4 = (g2 * g2) >> 15;
	tr->lpf_g = g;
	tr->lpf_g4 = (uint16_t)(g4 > 32768u ? 32768u : g4);
	tr->lpf_r = (uint16_t)((1u << 30) / (32768u + tr->lpf_g4));
}

void __not_in_flash_func(mlr_set_lpf)(int track, int slot)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (slot < 0) slot = 0;
	if (slot > MLR_LPF_OPEN) slot = MLR_LPF_OPEN;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->lpf_slot = (uint8_t)slot;
	uint32_t target = (uint32_t)slot << 20;
	tr->lpf_target = target;
	if (!tr->playing) {  /* nothing audible to glide */
		tr->lpf_pos = target;
		lpf_set_coef(tr, lpf_coef[slot]);
		return;
	}
	/* constant speed: one key per MLR_LPF_GLIDE_SAMPLES (500 ms per step) */
	tr->lpf_step = (1u << 20) / MLR_LPF_GLIDE_SAMPLES;
}

/* Coefficient at a fractional cutoff position (slot << 20): linear between
 * the two neighbouring slots, which are roughly an octave apart. The fine
 * scale keeps every 500 ms glide within ~0.5% of its length. */
static inline uint16_t __not_in_flash_func(lpf_coef_at)(uint32_t pos)
{
	uint32_t i = pos >> 20;
	if (i >= MLR_LPF_OPEN) return lpf_coef[MLR_LPF_OPEN];
	uint32_t f = (pos >> 12) & 255u;
	int32_t a = lpf_coef[i], b = lpf_coef[i + 1];
	return (uint16_t)(a + (((b - a) * (int32_t)f) >> 8));
}

/* CV: put the cutoff at a position directly, no glide. The nearest key
 * becomes the track's stored cutoff (what a save or the grid display uses). */
void __not_in_flash_func(mlr_set_lpf_pos)(int track, uint32_t pos)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (pos > ((uint32_t)MLR_LPF_OPEN << 20)) pos = (uint32_t)MLR_LPF_OPEN << 20;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->lpf_target = pos;
	if (tr->lpf_pos == pos) return;
	tr->lpf_pos = pos;
	tr->lpf_slot = (uint8_t)((pos + (1u << 19)) >> 20);
	lpf_set_coef(tr, lpf_coef_at(pos));
}

/* mlr_set_lpf_pos for several tracks at once (CV In 2 on a set of tracks):
 * the coefficients depend only on pos, so they're worked out once and copied,
 * which keeps a 6-track CV sweep cheap on the audio core. */
void __not_in_flash_func(mlr_set_lpf_pos_mask)(uint8_t mask, uint32_t pos)
{
	if (pos > ((uint32_t)MLR_LPF_OPEN << 20)) pos = (uint32_t)MLR_LPF_OPEN << 20;
	mlr_track_t *src = NULL;
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		if (!(mask & (1u << t))) continue;
		mlr_track_t *tr = &mlr_tracks[t];
		tr->lpf_target = pos;
		if (tr->lpf_pos == pos) continue;
		tr->lpf_pos = pos;
		tr->lpf_slot = (uint8_t)((pos + (1u << 19)) >> 20);
		if (!src) {
			lpf_set_coef(tr, lpf_coef_at(pos));
			src = tr;
		} else {
			tr->lpf_g = src->lpf_g;
			tr->lpf_g4 = src->lpf_g4;
			tr->lpf_r = src->lpf_r;
		}
	}
}

/* Smoothly swap a track's output channel. If the track is currently
 * playing, queues a fade-out → swap → fade-in around the change to
 * avoid the click that would otherwise occur from re-routing the active
 * sample stream between mixL and mixR. Stopped tracks change instantly. */
void __not_in_flash_func(mlr_set_recorded_channel)(int track, uint8_t channel)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	channel &= 0x01;
	mlr_track_t *tr = &mlr_tracks[track];

	/* Playing modes: the channel only picks the record input; pan sets the
	 * output, so there's nothing audible to fade. */
	if (mlr_pan_enabled || !tr->has_content || !tr->playing || tr->stop_pending) {
		tr->recorded_channel = channel;
		tr->channel_swap_pending = false;
		return;
	}

	if (tr->recorded_channel == channel && !tr->channel_swap_pending) {
		return;
	}

	tr->pending_channel = channel;
	tr->channel_swap_pending = true;
	tr->fade_out_active = true;
	tr->fade_out_count = MLR_FADE_SAMPLES;
	transition_update_track_state(tr);
}

void mlr_choke_group_set_loop(int t, int a, int b)
{
	if (t < 0 || t >= MLR_NUM_TRACKS) return;
	mlr_set_loop(t, a, b);
	stop_other_group_members(t);
}

/* ------------------------------------------------------------------ */
/* Init — load track metadata from flash, pre-fill rings              */
/* ------------------------------------------------------------------ */

void MLR_COLD_FUNC mlr_init(void)
{
	memset(mlr_tracks, 0, sizeof(mlr_tracks));
	memset(&mlr_page_ring, 0, sizeof(mlr_page_ring));
	memset(mlr_patterns, 0, sizeof(mlr_patterns));
	memset(mlr_recalls, 0, sizeof(mlr_recalls));
	mlr_rec_track  = -1;
	mlr_flushing   = false;
	mlr_scene_saving = false;
	scene_save_followup_requested = false;
	rec_track_idx  = -1;

	mlr_groups_default();
	memset(mlr_gate_mode, 0, sizeof(mlr_gate_mode));
#ifdef MLR_PERF_PROFILING
	mlr_perf_reset();
#endif

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		const mlr_track_header_t *hdr = (const mlr_track_header_t *)
			(XIP_BASE + MLR_TRACK_OFFSET(t));

		mlr_track_t *tr = &mlr_tracks[t];
		tr->pcm.w = 0;
		tr->pcm.r = 0;
		fill_rot_reset(tr);

		/* default speed/reverse for all tracks (including empty tracks) */
		tr->speed_shift = 0;
		tr->record_speed_shift = 0;
		tr->speed_frac  = 256;
		tr->speed_accum = 0;
		reset_track_audio_state(tr);
		tr->reverse     = false;
		tr->seek_reverse_pending = false;
		tr->volume_slot = 1;
		tr->volume_frac = 256;
		tr->volume_target = 256;
		tr->recorded_channel  = 0;
		tr->rate_mark         = MLR_RATE_MARK_24K;
		tr->cv1_pitch_enabled = true;
		tr->channel_user_chosen = false;

		if (hdr->magic == MLR_MAGIC &&
		    hdr->sample_count > 0 &&
		    hdr->sample_count <= MLR_MAX_SAMPLES &&
		    hdr->num_keyframes <= MLR_MAX_KEYFRAMES) {

			tr->has_content    = true;
			tr->length_samples = hdr->sample_count;
			tr->length_bytes   = hdr->adpcm_bytes;
			tr->num_keyframes  = hdr->num_keyframes;
			tr->record_speed_shift = hdr->record_speed_shift;
			tr->recorded_channel   = hdr->recorded_channel & 0x01;
			tr->rate_mark          = hdr->rate_mark;
			tr->cv1_pitch_enabled  = (hdr->cv1_pitch_mode == MLR_CV1_PITCH_ENABLED_MODE);
			tr->playing        = false;
			tr->playhead       = 0;


			/* init fill state at beginning */
			tr->fill_byte_pos   = 0;
			tr->fill_high_nyb   = false;
			tr->fill_sample_pos = 0;
			for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
				tr->fill_decode[ch].predictor  = 0;
				tr->fill_decode[ch].step_index = 0;
				if (tr->num_keyframes > 0) {
					tr->fill_decode[ch].predictor  = hdr->keyframes[0].ch[ch].predictor;
					tr->fill_decode[ch].step_index = hdr->keyframes[0].ch[ch].step_index;
				}
			}
			tr->fill_seek_pending = false;
			tr->seek_reverse_pending = false;
			tr->seek_start_pending = false;

		}
	}

	/* load saved scene (track state + patterns + recalls + settings) */
	mlr_rot_range_pct = MLR_ROT_RANGE_DEFAULT;
	mlr_cv2_mode = MLR_CV2_MODE_FILTER;
	mlr_cv1_target = mlr_cv2_target = MLR_CV_TARGET_LAST;
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_set_pan(t, mlr_pan_default(t));  /* until the scene says otherwise */
		mlr_set_lpf(t, MLR_LPF_OPEN);
	}
	mlr_scene_load();
}

/** Re-read a single track's header from flash.
 *  Call only when the track is stopped (not playing, not recording).
 *  Keeps the track's scene settings (speed, reverse, volume, loop) so a
 *  sample-manager session doesn't reset them. */
void MLR_COLD_FUNC mlr_rescan_track(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;

	mlr_track_t *tr = &mlr_tracks[track];
	const mlr_track_header_t *hdr = (const mlr_track_header_t *)
		(XIP_BASE + MLR_TRACK_OFFSET(track));

	const int8_t  keep_speed_shift = tr->speed_shift;
	const bool    keep_reverse     = tr->reverse;
	const uint8_t keep_volume_slot = tr->volume_slot;
	const bool    keep_loop        = tr->loop_active;
	const int8_t  keep_pan         = tr->pan;
	const bool    had_content      = tr->has_content;
	const uint32_t old_samples     = tr->length_samples;
	const uint32_t old_bytes       = tr->length_bytes;
	const int     keep_loop_start  = tr->loop_col_start;
	const int     keep_loop_end    = tr->loop_col_end;

	/* Reset playback state */
	tr->playing = false;
	tr->has_content = false;
	tr->pcm.w = 0;
	tr->pcm.r = 0;
	fill_rot_reset(tr);
	tr->speed_shift = 0;
	tr->record_speed_shift = 0;
	tr->speed_frac  = 256;
	tr->speed_accum = 0;
	reset_track_audio_state(tr);
	tr->reverse     = false;
	tr->seek_reverse_pending = false;
	tr->seek_start_pending = false;
	tr->loop_active = false;
	tr->volume_slot = 1;
	tr->volume_frac = 256;
	tr->volume_target = 256;
	tr->recorded_channel  = 0;
	tr->rate_mark         = MLR_RATE_MARK_24K;
	tr->cv1_pitch_enabled = true;
	tr->channel_user_chosen = false;
	tr->fill_seek_pending = false;

	if (hdr->magic == MLR_MAGIC &&
	    hdr->sample_count > 0 &&
	    hdr->sample_count <= MLR_MAX_SAMPLES &&
	    hdr->num_keyframes <= MLR_MAX_KEYFRAMES) {

		tr->has_content    = true;
		tr->length_samples = hdr->sample_count;
		tr->length_bytes   = hdr->adpcm_bytes;
		tr->num_keyframes  = hdr->num_keyframes;
		tr->record_speed_shift = hdr->record_speed_shift;
		tr->recorded_channel   = hdr->recorded_channel & 0x01;
		tr->rate_mark          = hdr->rate_mark;
		tr->cv1_pitch_enabled  = (hdr->cv1_pitch_mode == MLR_CV1_PITCH_ENABLED_MODE);
		tr->playhead       = 0;


		tr->fill_byte_pos   = 0;
		tr->fill_high_nyb   = false;
		tr->fill_sample_pos = 0;
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = 0;
			tr->fill_decode[ch].step_index = 0;
			if (tr->num_keyframes > 0) {
				tr->fill_decode[ch].predictor  = hdr->keyframes[0].ch[ch].predictor;
				tr->fill_decode[ch].step_index = hdr->keyframes[0].ch[ch].step_index;
			}
		}
	}

	/* Same setters mlr_scene_load() uses; loops need content to map columns. */
	mlr_set_speed(track, keep_speed_shift);
	mlr_set_reverse(track, keep_reverse);
	mlr_set_volume(track, keep_volume_slot);
	if (keep_loop && keep_loop_start >= 0 && keep_loop_end >= 0)
		restore_loop_stopped(track, keep_loop_start, keep_loop_end);  /* don't start it */
	/* New audio (uploaded or erased) starts hard to its channel side. */
	bool same_audio = had_content == tr->has_content &&
	                  old_samples == tr->length_samples && old_bytes == tr->length_bytes;
	if (!same_audio) tr->pan_cv_q8 = 0;
	/* ...unless the sample manager set it for this audio. */
	mlr_set_pan(track, (same_audio || tr->pan_keep) ? keep_pan : mlr_pan_default(track));
	tr->pan_keep = false;
}

/* ------------------------------------------------------------------ */
/* Recording — core 0 encodes, core 1 flushes pages                   */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_start_record)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;

	/* Recording over a grouped track removes it from its group; the rest
	 * of the group stays grouped together. Safe for solo tracks. */
	mlr_leave_group(track);

	mlr_track_t *tr = &mlr_tracks[track];

	/* fresh recording — stop any existing playback */
	tr->playing = false;
	tr->has_content = false;
	tr->cv1_pitch_enabled = false;

	/* Any previously saved loop-a-section on this track refers to the
	 * old sample range; clear it so the freshly recorded loop starts in
	 * "no sub-loop" state. Without this, a track that had a CUT-page loop
	 * saved (via scene load or held over from before) would keep the loop
	 * boundaries pointing at stale sample positions in the new recording. */
	tr->loop_active       = false;
	tr->loop_start_sample = 0;
	tr->loop_end_sample   = 0;
	tr->loop_col_start    = -1;
	tr->loop_col_end      = -1;

	tr->pcm.w = 0;
	tr->pcm.r = 0;
	fill_rot_reset(tr);
	/* Recording follows slower speed slots but is capped at 1x. Preserve the
	 * selected playback speed while storing the actual recording speed. */
	tr->record_speed_shift = tr->speed_shift > 0 ? 0 : tr->speed_shift;
	mlr_tracks[track].pan_cv_q8 = 0;
	mlr_set_pan(track, mlr_pan_default(track));  /* new audio: hard to its channel side */
	tr->rate_mark = MLR_RATE_MARK_24K;
	tr->speed_frac  = speed_shift_to_frac(tr->speed_shift);
	tr->speed_accum = 0;
	reset_track_audio_state(tr);
	/* preserve reverse — only affects playback, recording always forward */

	rec_track_idx       = track;
	mlr_rec_track       = track;
	rec_samples         = 0;
	rec_bytes           = 0;
	rec_num_keyframes   = 0;
	rec_flash_offset    = track_audio_flash_off(track);
	rec_next_erase      = rec_flash_offset;  /* first sector needs erase */

	mlr_page_ring.w    = 0;
	mlr_page_ring.r    = 0;
	mlr_page_ring.fill = 0;

	/* init encoder(s) */
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		rec_enc_state[ch].predictor  = 0;
		rec_enc_state[ch].step_index = 0;
	}
	rec_nybble_phase = false;
	rec_pending_byte = 0;

	/* save initial keyframe */
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		rec_keyframes[0].ch[ch].predictor  = 0;
		rec_keyframes[0].ch[ch].step_index = 0;
	}
	rec_num_keyframes = 1;
}

void __not_in_flash_func(mlr_record_sample)(int16_t sample)
{
	if (rec_track_idx < 0) return;

	/* scale 12-bit to 16-bit for better ADPCM quality */
	int16_t sample16 = sample << 4;

	uint8_t nybble = adpcm_encode(sample16, &rec_enc_state[0]);

	if (!rec_nybble_phase) {
		rec_pending_byte = nybble;
		rec_nybble_phase = true;
	} else {
		rec_pending_byte |= (nybble << 4);

		/* write byte into page ring */
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		uint32_t fill = mlr_page_ring.fill;
		mlr_page_ring.pages[slot][fill] = rec_pending_byte;
		mlr_page_ring.fill = fill + 1;

		if (mlr_page_ring.fill >= MLR_PAGE_SIZE) {
			mlr_page_ring.fill = 0;
			__dmb();
			mlr_page_ring.w++;
			PERF_NOTE_PAGE_RING_USED();
		}

		rec_nybble_phase = false;
	}

	rec_samples++;

	/* save keyframe at regular intervals */
	if ((rec_samples % MLR_KEYFRAME_INTERVAL) == 0 &&
	    rec_num_keyframes < MLR_MAX_KEYFRAMES) {
		rec_keyframes[rec_num_keyframes].ch[0].predictor  = rec_enc_state[0].predictor;
		rec_keyframes[rec_num_keyframes].ch[0].step_index = rec_enc_state[0].step_index;
		rec_num_keyframes++;
	}
}

void __not_in_flash_func(mlr_stop_record)(void)
{
	if (rec_track_idx < 0) return;
	mlr_track_t *tr = &mlr_tracks[rec_track_idx];
	mlr_flushing = true;

	/* flush any partial nybble */
	if (rec_nybble_phase) {
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		mlr_page_ring.pages[slot][mlr_page_ring.fill] = rec_pending_byte;
		mlr_page_ring.fill++;
		rec_nybble_phase = false;
	}

	/* flush partial page (pad with 0xFF) */
	if (mlr_page_ring.fill > 0) {
		uint8_t slot = mlr_page_ring.w % MLR_PAGE_RING_COUNT;
		memset(&mlr_page_ring.pages[slot][mlr_page_ring.fill], 0xFF,
		       MLR_PAGE_SIZE - mlr_page_ring.fill);
		mlr_page_ring.fill = 0;
		__dmb();
		mlr_page_ring.w++;
		PERF_NOTE_PAGE_RING_USED();
	}

	/* calculate actual ADPCM byte count */
	uint32_t actual_bytes = (rec_samples + 1) / 2;  /* 2 mono samples per byte */

	/* update track metadata — track becomes playable once header is written */
	tr->length_samples = rec_samples;
	tr->length_bytes   = actual_bytes;
	tr->num_keyframes  = rec_num_keyframes;  /* keyframes stay in rec_keyframes until the header write */

	hdr_write_track   = rec_track_idx;
	hdr_write_start_playback = true;
	hdr_write_pending = true;

	rec_track_idx = -1;
	mlr_rec_track = -1;
}

/* ------------------------------------------------------------------ */
/* Playback — core 0 reads decoded PCM from ring buffers              */
/* ------------------------------------------------------------------ */

int16_t __not_in_flash_func(mlr_play_mix)(uint8_t volume)
{
	int32_t mix = 0;

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		check_seek_handoff(tr);
		if (!tr->has_content || (!tr->playing && !tr->stop_pending)) continue;

		/* wrap boundaries */
		uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
		uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
		if (tr->wrap_preview_ready) {
			uint32_t arm = tr->wrap_preview_arm_playhead;
			bool near = tr->reverse ? (tr->playhead <= arm) : (tr->playhead >= arm);
			if (near) maybe_start_wrap_preview_xfade(tr, wrap_start, wrap_end);
		}
		bool wrapped = false;
		int32_t sample_out = 0;

		bool fast_integer = false;
		uint32_t avail = 0;
		uint32_t pcm_r = tr->pcm.r;
		uint32_t ring_pos = 0;
		bool ring_state_loaded = false;

		if (tr->speed_accum == 0) {
			uint32_t steps = integer_speed_steps(tr->speed_frac);
			bool no_wrap = tr->reverse
				? (tr->playhead >= wrap_start + steps)
				: (tr->playhead >= wrap_start && tr->playhead + steps < wrap_end);
			avail = (steps > 0 && no_wrap) ? tr->pcm.w - pcm_r : 0;
			if (avail >= steps && steps > 0) {
				PERF_NOTE_PCM_AVAIL(t, avail);
				ring_pos = pcm_r % MLR_RING_SAMPLES;
				uint32_t final_pos = ring_pos + steps - 1u;
				if (final_pos >= MLR_RING_SAMPLES)
					final_pos -= MLR_RING_SAMPLES;
				tr->interp_prev[0] = tr->last_pcm[0];
				tr->last_pcm[0] = tr->pcm.buf[final_pos * MLR_NUM_CHANNELS];
				tr->pcm.r = pcm_r + steps;
				if (tr->reverse) tr->playhead -= steps;
				else             tr->playhead += steps;
				sample_out = tr->last_pcm[0];
				fast_integer = true;
			}
		}

			/* variable-speed sample consumption via accumulator */
			if (!fast_integer) {
				tr->speed_accum += tr->speed_frac;
				while (tr->speed_accum >= 256) {
					tr->speed_accum -= 256;
					if (!ring_state_loaded) {
						avail = tr->pcm.w - pcm_r;
						PERF_NOTE_PCM_AVAIL(t, avail);
						ring_pos = pcm_r % MLR_RING_SAMPLES;
						ring_state_loaded = true;
					}
					if (avail > 0) {
						tr->interp_prev[0] = tr->last_pcm[0];
						tr->last_pcm[0] = tr->pcm.buf[ring_pos * MLR_NUM_CHANNELS];
						if (++ring_pos >= MLR_RING_SAMPLES) ring_pos = 0;
						pcm_r++;
						avail--;
						if (tr->reverse) {
							if (tr->playhead <= wrap_start) {
								tr->playhead = wrap_end > 0 ? wrap_end - 1 : 0;
								wrapped = true;
							} else {
								tr->playhead--;
							}
						} else {
							tr->playhead++;
							if (tr->playhead >= wrap_end) {
								tr->playhead = wrap_start;
								wrapped = true;
							}
						}
					} else {
						PERF_NOTE_PCM_UNDERRUN(t);
					}
				}
				if (ring_state_loaded)
					tr->pcm.r = pcm_r;
				if (wrapped && !tr->seek_xfade_active) {
					note_track_wrap(t);
					/* loop wraps don't do full handoff seek as they are contiguous in ring */
					tr->fade_out_active = true;
					tr->fade_out_count = MLR_FADE_SAMPLES;
				}

				/* Linear interpolation between current and next sample.
				 * Fractional phase is speed_accum in Q8 (0..255). */
				int16_t x0 = tr->last_pcm[0];
				uint8_t frac = (uint8_t)tr->speed_accum;
				if (frac == 0) {
					sample_out = x0;
				} else {
					if (!ring_state_loaded) {
						avail = tr->pcm.w - pcm_r;
						PERF_NOTE_PCM_AVAIL(t, avail);
						ring_pos = pcm_r % MLR_RING_SAMPLES;
					}
					int16_t x1 = x0;
					if (avail > 0) {
						x1 = tr->pcm.buf[ring_pos * MLR_NUM_CHANNELS];
					}
					sample_out = linear_interp_q8(x0, x1, frac);
				}
			}

		/* apply per-track volume (slew toward target to avoid clicks) */
		if (tr->volume_frac != tr->volume_target) {
			if (tr->volume_frac < tr->volume_target) {
				tr->volume_frac += 4;
				if (tr->volume_frac > tr->volume_target)
					tr->volume_frac = tr->volume_target;
			} else {
				if (tr->volume_frac < 4)
					tr->volume_frac = tr->volume_target;
				else
					tr->volume_frac -= 4;
				if (tr->volume_frac < tr->volume_target)
					tr->volume_frac = tr->volume_target;
			}
		}
		if (tr->volume_frac != 256)
			sample_out = (sample_out * (int32_t)tr->volume_frac) >> 8;
		sample_out = apply_seek_preview_xfade(tr, sample_out);
		
		if (tr->fade_out_active) {
			if (tr->fade_out_count > 0) {
				tr->fade_out_count--;
				sample_out = (sample_out * tr->fade_out_count) / MLR_FADE_SAMPLES;
				if (tr->fade_out_count == 0 && !tr->seek_handoff_pending && tr->fill_seek_pending) {
					sample_out = 0;
				} else if (tr->fade_out_count == 0) {
					tr->fade_out_active = false;
					if (tr->stop_pending) {
						finish_pending_stop(tr);
					}
				}
			} else {
				sample_out = 0;
			}
		} else if (tr->fade_in_count > 0) {
			tr->fade_in_count--;
			sample_out = (sample_out * (MLR_FADE_SAMPLES - tr->fade_in_count)) / MLR_FADE_SAMPLES;
		}

		if (!tr->muted)
			mix += sample_out;
	}

	if (mix == 0) return 0;
	mix = (mix * (int32_t)volume) >> 8;
	if (mix > 32767) mix = 32767;
	if (mix < -32768) mix = -32768;
	return (int16_t)(mix >> 4);
}

/* Route one track's sample to the two outputs: the ladder filter, then an
 * equal-power pan slewed ~20 ms end to end so pan moves don't click. Grid and
 * gridless modes both do this (gridless just has no controls for them).
 * With mlr_pan_enabled off, hard to the track's recorded channel, unfiltered. */
#define MLR_PAN_SLEW_STEP 64u
static inline uint16_t pan_slew(uint16_t cur, uint16_t target)
{
	if (cur < target) return (uint16_t)((uint32_t)(target - cur) > MLR_PAN_SLEW_STEP ? cur + MLR_PAN_SLEW_STEP : target);
	return (uint16_t)((uint32_t)(cur - target) > MLR_PAN_SLEW_STEP ? cur - MLR_PAN_SLEW_STEP : target);
}

static inline void __not_in_flash_func(mix_track_route)(mlr_track_t *tr, int32_t s,
	int32_t *mixL, int32_t *mixR)
{
	if (!mlr_pan_enabled) {
		if (tr->recorded_channel == 1) *mixR += s;
		else                           *mixL += s;
		return;
	}

	/* 4-pole ladder low-pass (Moog style, resonance 0): tanh input stage, then
	 * four TPT one-pole stages. The cutoff position glides to its target at one
	 * key per 500 ms, in key positions so the sweep is even by ear; open = exact
	 * passthrough, no saturation. */
	if (tr->lpf_pos != tr->lpf_target) {
		uint32_t pos = tr->lpf_pos, target = tr->lpf_target, step = tr->lpf_step;
		if (pos < target) pos = (target - pos > step) ? pos + step : target;
		else              pos = (pos - target > step) ? pos - step : target;
		tr->lpf_pos = pos;
		lpf_set_coef(tr, lpf_coef_at(pos));
	}
	uint16_t G = tr->lpf_g;
	if (G >= 32768u) {
		int32_t q = s * 16;  /* keep the states on the signal for a click-free close */
		tr->lpf_s[0] = tr->lpf_s[1] = tr->lpf_s[2] = tr->lpf_s[3] = q;
	} else {
		/* Zero-delay feedback, k = 1. Each TPT stage is y = G*x + (1-G)*s, so
		 * the ladder output is y4 = G^4*x + S, where S is what the states
		 * alone contribute. With x = u - y4 the loop solves exactly:
		 * y4 = (G^4*u + S) / (1 + G^4). Values are Q4 (1/16 sample). */
		int32_t g = (int32_t)G;
		int32_t u = s * 16;
		int32_t S = tr->lpf_s[0] - lpf_mul(tr->lpf_s[0], g);
		for (int k = 1; k < 4; k++)
			S = lpf_mul(S, g) + (tr->lpf_s[k] - lpf_mul(tr->lpf_s[k], g));
		int32_t y4 = lpf_mul(lpf_mul(u, tr->lpf_g4) + S, tr->lpf_r);

		/* input stage: tanh on the fed-back input, unity small-signal gain */
		int32_t xin = (u - y4) >> 4;
		uint32_t m = (uint32_t)(xin < 0 ? -xin : xin);
		int32_t x;
		if (m >= 65536u) {
			x = lpf_tanh[128];
		} else {
			uint32_t i = m >> 9, f = m & 511u;
			x = lpf_tanh[i] + (((lpf_tanh[i + 1] - lpf_tanh[i]) * (int32_t)f) >> 9);
		}
		int32_t in = (xin < 0 ? -x : x) * 16;
		/* TPT one-pole: v = G*(in - s); y = v + s; s = y + v */
		for (int k = 0; k < 4; k++) {
			int32_t v = lpf_mul(in - tr->lpf_s[k], g);
			int32_t y = v + tr->lpf_s[k];
			tr->lpf_s[k] = y + v;
			in = y;
		}
		/* feedback costs 1/(1+k) of the level: make it up (x2) after the drive */
		s = in >> 3;
	}

	/* pan glide: constant speed, one step per MLR_PAN_GLIDE_SAMPLES; the
	 * gain slew below still smooths CV moves and each glide increment */
	int32_t pan_target = tr->pan * (1 << 20);
	if (tr->pan_pos != pan_target) {
		const int32_t step = (int32_t)((1u << 20) / MLR_PAN_GLIDE_SAMPLES);
		int32_t d = pan_target - tr->pan_pos;
		tr->pan_pos = (d > step) ? tr->pan_pos + step : (d < -step) ? tr->pan_pos - step : pan_target;
		pan_update_gains(tr);
	}

	uint16_t gl = tr->pan_gl_cur, gr = tr->pan_gr_cur;
	if (gl != tr->pan_gl) tr->pan_gl_cur = gl = pan_slew(gl, tr->pan_gl);
	if (gr != tr->pan_gr) tr->pan_gr_cur = gr = pan_slew(gr, tr->pan_gr);
	if (gl) *mixL += (gl == 32768u) ? s : (s * (int32_t)gl) >> 15;
	if (gr) *mixR += (gr == 32768u) ? s : (s * (int32_t)gr) >> 15;
}

static inline bool mix_track_steady_integer_dual(int t, mlr_track_t *tr,
	uint8_t transition_flags, uint32_t wrap_start, uint32_t wrap_end,
	int32_t *mixL, int32_t *mixR)
{
	if ((transition_flags & (uint8_t)~MLR_TRANS_WRAP) != 0 || !tr->playing || tr->stop_pending)
		return false;
	if (tr->volume_frac != tr->volume_target)
		return false;
	if (tr->speed_accum != 0)
		return false;

	uint32_t steps = integer_speed_steps(tr->speed_frac);
	if (steps == 0) return false;

	bool will_wrap = tr->reverse
		? (tr->playhead < wrap_start + steps)
		: (tr->playhead < wrap_start || tr->playhead + steps >= wrap_end);

	uint32_t pcm_r = tr->pcm.r;
	uint32_t avail = tr->pcm.w - pcm_r;
	if (avail < steps) return false;

	PERF_NOTE_PCM_AVAIL(t, avail);
	uint32_t ring_pos = pcm_r & MLR_RING_MASK;
	uint32_t final_pos = (ring_pos + steps - 1u) & MLR_RING_MASK;
	tr->interp_prev[0] = tr->last_pcm[0];
	tr->last_pcm[0] = tr->pcm.buf[final_pos * MLR_NUM_CHANNELS];
	tr->pcm.r = pcm_r + steps;
	if (will_wrap) {
		if (tr->reverse) tr->playhead = wrap_end > 0 ? wrap_end - 1 : 0;
		else             tr->playhead = wrap_start;
		note_track_wrap(t);
	} else {
		if (tr->reverse) tr->playhead -= steps;
		else             tr->playhead += steps;
	}

	if (!tr->muted) {
		int32_t sample_out = tr->last_pcm[0];
		if (tr->volume_frac != 256)
			sample_out = (sample_out * (int32_t)tr->volume_frac) >> 8;
		mix_track_route(tr, sample_out, mixL, mixR);
	}
	return true;
}

static int32_t __not_in_flash_func(mlr_play_mix_dual_sum)(int32_t *out_right)
{
	int32_t mixL = 0, mixR = 0;

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		if (!tr->has_content) continue;
		uint8_t transition_flags = tr->transition_flags;
		if (transition_flags & MLR_TRANS_HANDOFF) {
			check_seek_handoff(tr);
			transition_flags = tr->transition_flags;
		}
		if (!tr->playing && !tr->stop_pending) continue;

		/* Wrap boundaries — computed once and reused by the wrap-preview
		 * gate, the steady fast path, and the slow path below. */
		uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
		uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

		/* Cheap pre-gate: skip the heavy maybe_start_wrap_preview_xfade()
		 * call until the playhead is within the arm window of the wrap
		 * boundary. The arm position is computed on core 1 when the
		 * preview is primed, with a guard band to absorb accumulator
		 * drift. */
		if (transition_flags & MLR_TRANS_WRAP) {
			uint32_t arm = tr->wrap_preview_arm_playhead;
			bool near = tr->reverse ? (tr->playhead <= arm) : (tr->playhead >= arm);
			if (near) {
				maybe_start_wrap_preview_xfade(tr, wrap_start, wrap_end);
				transition_flags = tr->transition_flags;
			}
		}

		if (mix_track_steady_integer_dual(t, tr, transition_flags,
		                                  wrap_start, wrap_end, &mixL, &mixR)) {
			continue;
		}

		bool wrapped = false;
		int32_t sample_out = 0;

		bool fast_integer = false;
		uint32_t avail = 0;
		uint32_t pcm_r = tr->pcm.r;
		uint32_t ring_pos = 0;
		bool ring_state_loaded = false;

		/* Integer fast-step retry: only meaningful when speed_frac is one
		 * of the integer ratios and the accumulator is aligned; otherwise
		 * fall straight through to the variable-speed accumulator path. */
		if (tr->speed_accum == 0) {
			uint32_t steps = integer_speed_steps(tr->speed_frac);
			if (steps > 0) {
				bool no_wrap = tr->reverse
					? (tr->playhead >= wrap_start + steps)
					: (tr->playhead >= wrap_start && tr->playhead + steps < wrap_end);
				if (no_wrap) {
					avail = tr->pcm.w - pcm_r;
					if (avail >= steps) {
						PERF_NOTE_PCM_AVAIL(t, avail);
						ring_pos = pcm_r & MLR_RING_MASK;
						uint32_t final_pos = (ring_pos + steps - 1u) & MLR_RING_MASK;
						tr->interp_prev[0] = tr->last_pcm[0];
						tr->last_pcm[0] = tr->pcm.buf[final_pos * MLR_NUM_CHANNELS];
						tr->pcm.r = pcm_r + steps;
						if (tr->reverse) tr->playhead -= steps;
						else             tr->playhead += steps;
						sample_out = tr->last_pcm[0];
						fast_integer = true;
					}
				}
			}
		}

			if (!fast_integer) {
				tr->speed_accum += tr->speed_frac;
				while (tr->speed_accum >= 256) {
					tr->speed_accum -= 256;
					if (!ring_state_loaded) {
						avail = tr->pcm.w - pcm_r;
						PERF_NOTE_PCM_AVAIL(t, avail);
						ring_pos = pcm_r & MLR_RING_MASK;
						ring_state_loaded = true;
					}
					if (avail > 0) {
						tr->interp_prev[0] = tr->last_pcm[0];
						tr->last_pcm[0] = tr->pcm.buf[ring_pos * MLR_NUM_CHANNELS];
						ring_pos = (ring_pos + 1u) & MLR_RING_MASK;
						pcm_r++;
						avail--;
						if (tr->reverse) {
							if (tr->playhead <= wrap_start) {
								tr->playhead = wrap_end > 0 ? wrap_end - 1 : 0;
								wrapped = true;
							} else {
								tr->playhead--;
							}
						} else {
							tr->playhead++;
							if (tr->playhead >= wrap_end) {
								tr->playhead = wrap_start;
								wrapped = true;
							}
						}
					} else {
						PERF_NOTE_PCM_UNDERRUN(t);
					}
				}
				if (ring_state_loaded)
					tr->pcm.r = pcm_r;
				if (wrapped && !(transition_flags & MLR_TRANS_XFADE)) {
					note_track_wrap(t);
					tr->fade_out_active = true;
					tr->fade_out_count = MLR_FADE_SAMPLES;
					transition_update_track_state(tr);
					transition_flags = tr->transition_flags;
				}

				int16_t x0 = tr->last_pcm[0];
				uint8_t frac = (uint8_t)tr->speed_accum;
				if (frac == 0) {
					sample_out = x0;
				} else {
					if (!ring_state_loaded) {
						avail = tr->pcm.w - pcm_r;
						PERF_NOTE_PCM_AVAIL(t, avail);
						ring_pos = pcm_r & MLR_RING_MASK;
					}
					int16_t x1 = x0;
					if (avail > 0) {
						x1 = tr->pcm.buf[ring_pos * MLR_NUM_CHANNELS];
					}
					sample_out = linear_interp_q8(x0, x1, frac);
				}
			}

		if (tr->volume_frac != tr->volume_target) {
			if (tr->volume_frac < tr->volume_target) {
				tr->volume_frac += 4;
				if (tr->volume_frac > tr->volume_target)
					tr->volume_frac = tr->volume_target;
			} else {
				if (tr->volume_frac < 4)
					tr->volume_frac = tr->volume_target;
				else
					tr->volume_frac -= 4;
				if (tr->volume_frac < tr->volume_target)
					tr->volume_frac = tr->volume_target;
			}
		}
		if (tr->volume_frac != 256)
			sample_out = (sample_out * (int32_t)tr->volume_frac) >> 8;
		if (transition_flags & MLR_TRANS_XFADE)
			sample_out = apply_seek_preview_xfade(tr, sample_out);
		/* apply_seek_preview_xfade() doesn't change fade_out_active or
		 * fade_in_count, so the snapshotted transition_flags above still
		 * describes the FADE state accurately. Skip the volatile re-read. */

		if (transition_flags & MLR_TRANS_FADE) {
			if (tr->fade_out_active) {
				if (tr->fade_out_count > 0) {
					tr->fade_out_count--;
					sample_out = (sample_out * tr->fade_out_count) / MLR_FADE_SAMPLES;
					if (tr->fade_out_count == 0 && !tr->seek_handoff_pending && tr->fill_seek_pending) {
						sample_out = 0;
						transition_update_track_state(tr);
					} else if (tr->fade_out_count == 0) {
						tr->fade_out_active = false;
						if (tr->stop_pending) {
							finish_pending_stop(tr);
						} else if (!tr->fill_seek_pending) {
							if (tr->channel_swap_pending) {
								tr->recorded_channel = tr->pending_channel;
								tr->channel_swap_pending = false;
							}
							tr->fade_in_count = MLR_FADE_SAMPLES;
							transition_update_track_state(tr);
						}
					}
				} else {
					sample_out = 0;
				}
			} else if (tr->fade_in_count > 0) {
				tr->fade_in_count--;
				sample_out = (sample_out * (MLR_FADE_SAMPLES - tr->fade_in_count)) / MLR_FADE_SAMPLES;
				if (tr->fade_in_count == 0)
					transition_update_track_state(tr);
			}
		}

		if (!tr->muted)
			mix_track_route(tr, sample_out, &mixL, &mixR);
	}

	*out_right = mixR;
	return mixL;
}

int16_t __not_in_flash_func(mlr_play_mix_dual)(int16_t *out_right)
{
	int32_t mixR;
	int32_t mixL = mlr_play_mix_dual_sum(&mixR);

	/* Three-stage cascaded soft clip applied at 16-bit scale before the
	 * final >>4 to 12-bit DAC range. Knees are the 12-bit master knees
	 * (1638/1945/2007 ≈ 80%/95%/98% of full scale) scaled by 16 so the
	 * curve shape lands at the same proportional output positions after
	 * the shift. This replaces what used to be a hard int16 clip and
	 * keeps multi-track summing overshoots out of the brick wall. */
	#define MLR_MIX_KNEE_1_16  26208   /* 1638 << 4 */
	#define MLR_MIX_KNEE_2_16  31120   /* 1945 << 4 */
	#define MLR_MIX_KNEE_3_16  32112   /* 2007 << 4 */

	if (mixL >  MLR_MIX_KNEE_1_16) mixL =  MLR_MIX_KNEE_1_16 + ((mixL - MLR_MIX_KNEE_1_16) >> 1);
	if (mixL < -MLR_MIX_KNEE_1_16) mixL = -MLR_MIX_KNEE_1_16 + ((mixL + MLR_MIX_KNEE_1_16) >> 1);
	if (mixL >  MLR_MIX_KNEE_2_16) mixL =  MLR_MIX_KNEE_2_16 + ((mixL - MLR_MIX_KNEE_2_16) >> 1);
	if (mixL < -MLR_MIX_KNEE_2_16) mixL = -MLR_MIX_KNEE_2_16 + ((mixL + MLR_MIX_KNEE_2_16) >> 1);
	if (mixL >  MLR_MIX_KNEE_3_16) mixL =  MLR_MIX_KNEE_3_16 + ((mixL - MLR_MIX_KNEE_3_16) >> 1);
	if (mixL < -MLR_MIX_KNEE_3_16) mixL = -MLR_MIX_KNEE_3_16 + ((mixL + MLR_MIX_KNEE_3_16) >> 1);
	if (mixL >  32767) mixL =  32767;
	if (mixL < -32768) mixL = -32768;

	if (mixR >  MLR_MIX_KNEE_1_16) mixR =  MLR_MIX_KNEE_1_16 + ((mixR - MLR_MIX_KNEE_1_16) >> 1);
	if (mixR < -MLR_MIX_KNEE_1_16) mixR = -MLR_MIX_KNEE_1_16 + ((mixR + MLR_MIX_KNEE_1_16) >> 1);
	if (mixR >  MLR_MIX_KNEE_2_16) mixR =  MLR_MIX_KNEE_2_16 + ((mixR - MLR_MIX_KNEE_2_16) >> 1);
	if (mixR < -MLR_MIX_KNEE_2_16) mixR = -MLR_MIX_KNEE_2_16 + ((mixR + MLR_MIX_KNEE_2_16) >> 1);
	if (mixR >  MLR_MIX_KNEE_3_16) mixR =  MLR_MIX_KNEE_3_16 + ((mixR - MLR_MIX_KNEE_3_16) >> 1);
	if (mixR < -MLR_MIX_KNEE_3_16) mixR = -MLR_MIX_KNEE_3_16 + ((mixR + MLR_MIX_KNEE_3_16) >> 1);
	if (mixR >  32767) mixR =  32767;
	if (mixR < -32768) mixR = -32768;

	*out_right = (int16_t)(mixR >> 4);
	return (int16_t)(mixL >> 4);
}

/* ------------------------------------------------------------------ */
/* Seeking                                                            */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_cut)(int track, int column)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content) return;

	uint32_t target;
	if (tr->reverse) {
		/* In reverse, land at the *end* of the tapped column's range so
		 * playback going backwards stays inside the visually-pressed cell
		 * for that column's duration before crossing into the previous
		 * column. Without this, the playhead lands at the column's first
		 * sample and immediately steps into the column to its left. */
		uint32_t next = (uint32_t)(column + 1) * tr->length_samples / MLR_GRID_COLS;
		target = (next > 0) ? next - 1 : 0;
	} else {
		target = (uint32_t)column * tr->length_samples / MLR_GRID_COLS;
	}
	if (target >= tr->length_samples) target = tr->length_samples - 1;

	/* signal core 1 to refill from this position */
	bool start_pending = !tr->playing || tr->stop_pending;
	trigger_track_fade_and_seek(tr, target, start_pending);
}

void __not_in_flash_func(mlr_cut_sample)(int track, uint32_t sample_pos)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content) return;

	if (sample_pos >= tr->length_samples)
		sample_pos = tr->length_samples - 1;

	bool start_pending = !tr->playing || tr->stop_pending;
	trigger_track_fade_and_seek(tr, sample_pos, start_pending);
}

void __not_in_flash_func(mlr_stop_track)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->playing || tr->stop_pending) return;
	tr->fade_out_active = true;
	tr->fade_out_count = MLR_FADE_SAMPLES;
	tr->stop_pending = true;
	transition_update_track_state(tr);
}

/* ------------------------------------------------------------------ */
/* Loop-a-section                                                     */
/* ------------------------------------------------------------------ */

/* Restore a saved loop on a stopped track without starting it (mlr_set_loop
 * starts a stopped track, which is right for a grid press but not for boot or
 * a sample-manager rescan). The next cut/play seeks inside the loop. */
static void MLR_COLD_FUNC restore_loop_stopped(int track, int col_start, int col_end)
{
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content || tr->length_samples == 0 || tr->playing) {
		if (tr->playing) mlr_set_loop(track, col_start, col_end);
		return;
	}
	if (col_start > col_end) { int tmp = col_start; col_start = col_end; col_end = tmp; }
	if (col_start == col_end) return;
	uint32_t start_s = (uint32_t)col_start * tr->length_samples / MLR_GRID_COLS;
	uint32_t end_s   = (uint32_t)(col_end + 1) * tr->length_samples / MLR_GRID_COLS;
	if (end_s > tr->length_samples) end_s = tr->length_samples;
	if (start_s >= end_s) return;
	tr->loop_start_sample = start_s;
	tr->loop_end_sample   = end_s;
	tr->loop_col_start    = col_start;
	tr->loop_col_end      = col_end;
	tr->loop_active       = true;
}

void __not_in_flash_func(mlr_set_loop)(int track, int col_start, int col_end)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content || tr->length_samples == 0) return;

	/* ensure col_start <= col_end */
	if (col_start > col_end) { int tmp = col_start; col_start = col_end; col_end = tmp; }
	if (col_start == col_end) return;  /* need at least 2 different columns */

	uint32_t start_s = (uint32_t)col_start * tr->length_samples / MLR_GRID_COLS;
	uint32_t end_s   = (uint32_t)(col_end + 1) * tr->length_samples / MLR_GRID_COLS;
	if (end_s > tr->length_samples) end_s = tr->length_samples;
	if (start_s >= end_s) return;

	tr->loop_start_sample = start_s;
	tr->loop_end_sample   = end_s;
	tr->loop_col_start    = col_start;
	tr->loop_col_end      = col_end;
	__dmb();
	tr->loop_active       = true;

	/* ALWAYS seek core 1 fill to new playhead bounds */
	uint32_t next_playhead = tr->playhead;
	if (next_playhead < start_s || next_playhead >= end_s) {
		next_playhead = start_s;
	}
	bool start_pending = !tr->playing || tr->stop_pending;
	trigger_track_fade_and_seek(tr, next_playhead, start_pending);
}

void __not_in_flash_func(mlr_clear_loop)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->loop_active) return;

	tr->loop_active = false;

	/* Seek core 1 only while active playback needs fresh non-loop samples.
	 * If a stop fade is already pending, clearing the loop must not queue a
	 * seek handoff that can keep the playhead alive after the stop. */
	if (tr->playing && !tr->stop_pending) {
		bool start_pending = !tr->playing || tr->stop_pending;
		trigger_track_fade_and_seek(tr, tr->playhead, start_pending);
	}
}

void __not_in_flash_func(mlr_set_speed)(int track, int speed_shift)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_shift < -3) speed_shift = -3;
	if (speed_shift >  3) speed_shift =  3;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->speed_shift = (int8_t)speed_shift;
	tr->speed_frac  = speed_shift_to_frac((int8_t)speed_shift);
	tr->speed_accum = 0;
	tr->fade_out_active = true;
	tr->fade_out_count = MLR_FADE_SAMPLES;
	/* speed change invalidates the cached wrap-preview arm position */
	tr->wrap_preview_ready = false;
	transition_update_track_state(tr);
}

void __not_in_flash_func(mlr_set_speed_frac)(int track, uint16_t speed_frac)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_frac < 64) speed_frac = 64;
	if (speed_frac > 1024) speed_frac = 1024;

	mlr_track_t *tr = &mlr_tracks[track];
	tr->speed_frac  = speed_frac;
	tr->speed_accum = 0;
	tr->fade_out_active = true;
	tr->fade_out_count = MLR_FADE_SAMPLES;
	/* speed change invalidates the cached wrap-preview arm position */
	tr->wrap_preview_ready = false;
	transition_update_track_state(tr);
}

void __not_in_flash_func(mlr_set_speed_frac_nondeclick)(int track, uint16_t speed_frac)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (speed_frac < 64) speed_frac = 64;
	if (speed_frac > 1024) speed_frac = 1024;

	mlr_track_t *tr = &mlr_tracks[track];
	if (tr->speed_frac == speed_frac) return;
	tr->speed_frac = speed_frac;
	/* speed change invalidates the cached wrap-preview arm position */
	tr->wrap_preview_ready = false;
	transition_update_track_state(tr);
}

void __not_in_flash_func(mlr_set_reverse)(int track, bool reverse)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	if (tr->reverse == reverse) return;  /* no change */
	PERF_NOTE_REVERSE_TOGGLE(track);

	if (!tr->playing && !tr->stop_pending) {
		tr->reverse = reverse;
		tr->seek_reverse_pending = false;
		tr->seek_handoff_reverse_pending = false;
		tr->seek_handoff_pending = false;
		tr->seek_xfade_active = false;
		tr->fill_seek_pending = false;
		tr->wrap_preview_ready = false;
		transition_update_track_state(tr);
		return;
	}

	/* re-seek core 1 fill to current playhead in new direction */
	tr->seek_reverse_target = reverse;
	tr->seek_reverse_pending = true;
	tr->seek_target_sample = tr->playhead;
	__dmb();
	tr->fill_seek_pending = true;
	transition_update_track_state(tr);
}

void __not_in_flash_func(mlr_set_volume)(int track, int slot)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	if (slot < 0) slot = 0;
	if (slot >= MLR_NUM_VOL_SLOTS) slot = MLR_NUM_VOL_SLOTS - 1;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->volume_slot = (uint8_t)slot;
	tr->volume_target = volume_slot_to_frac((uint8_t)slot);
	/* If not playing, apply immediately (no audio to slew) */
	if (!tr->playing)
		tr->volume_frac = tr->volume_target;
}

void __not_in_flash_func(mlr_get_loop_cols)(int track, int *col_start, int *col_end)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) { *col_start = -1; *col_end = -1; return; }
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->loop_active) { *col_start = -1; *col_end = -1; return; }
	*col_start = tr->loop_col_start;
	*col_end   = tr->loop_col_end;
}

void __not_in_flash_func(mlr_clear_track)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return;
	mlr_track_t *tr = &mlr_tracks[track];
	tr->playing        = false;
	tr->has_content    = false;
	tr->length_samples = 0;
	tr->length_bytes   = 0;
	tr->num_keyframes  = 0;
	tr->playhead       = 0;
	tr->loop_active    = false;
	tr->fill_byte_pos   = 0;
	tr->fill_high_nyb   = false;
	tr->fill_sample_pos = 0;
	tr->fill_seek_pending = false;
	tr->seek_target_sample = 0;
	tr->seek_reverse_pending = false;
	tr->seek_start_pending = false;
	tr->speed_shift = 0;
	tr->record_speed_shift = 0;
	tr->speed_frac  = 256;
	tr->speed_accum = 0;
	memset(tr->last_pcm, 0, sizeof(tr->last_pcm));
	memset(tr->interp_prev, 0, sizeof(tr->interp_prev));
	tr->reverse     = false;
	tr->volume_slot = 1;
	tr->volume_frac = 256;
	tr->volume_target = 256;
	tr->recorded_channel    = 0;
	tr->pan = -MLR_PAN_STEPS; tr->pan_cv_q8 = 0; tr->pan_keep = false;
	tr->pan_pos = -MLR_PAN_STEPS * (1 << 20);
	tr->lpf_slot = MLR_LPF_OPEN; lpf_set_coef(tr, 32768u);
	tr->lpf_pos = tr->lpf_target = (uint32_t)MLR_LPF_OPEN << 20;
	tr->pan_gl = tr->pan_gl_cur = 32768u; tr->pan_gr = tr->pan_gr_cur = 0;
	tr->rate_mark           = MLR_RATE_MARK_24K;
	tr->cv1_pitch_enabled   = true;
	tr->channel_user_chosen = false;
	tr->pcm.w = 0;
	tr->pcm.r = 0;
	fill_rot_reset(tr);

	/* schedule flash erase of header sector */
	clear_track   = track;
	clear_pending = true;
	transition_update_track_state(tr);
}

bool __not_in_flash_func(mlr_rewrite_track_header)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return false;
	if (!mlr_tracks[track].has_content || mlr_tracks[track].length_samples == 0) return false;
	if (mlr_rec_track >= 0 || rec_track_idx >= 0) return false;
	hdr_rewrite_pending_mask |= (uint8_t)(1u << track);
	mlr_flushing = true;
	__dmb();
	return true;
}

uint8_t __not_in_flash_func(mlr_copy_track_mask)(int src_track, uint8_t dst_mask)
{
	if (src_track < 0 || src_track >= MLR_NUM_TRACKS) return 0;
	if (mlr_rec_track >= 0 || rec_track_idx >= 0 || mlr_flushing || mlr_scene_saving) return 0;
	if (hdr_write_pending || clear_pending || scene_save_pending || copy_pending || copy_dst_track >= 0) return 0;

	mlr_track_t *src = &mlr_tracks[src_track];
	if (!src->has_content || src->length_samples == 0 || src->length_bytes == 0) return 0;

	uint8_t accepted = 0;
	for (int dst = 0; dst < MLR_NUM_TRACKS; dst++) {
		if (!(dst_mask & (1u << dst))) continue;
		if (dst == src_track) continue;
		if (mlr_tracks[dst].has_content) continue;
		accepted |= (uint8_t)(1u << dst);
	}
	if (accepted == 0) return 0;

	copy_src_track = src_track;
	copy_dst_mask = accepted;
	copy_dst_track = -1;
	copy_bytes_total = src->length_bytes;
	copy_bytes_done = 0;
	copy_next_erase = 0;
	copy_pending = true;
	mlr_copying = true;
	mlr_flushing = true;
	return accepted;
}

int __not_in_flash_func(mlr_get_column)(int track)
{
	if (track < 0 || track >= MLR_NUM_TRACKS) return 0;
	mlr_track_t *tr = &mlr_tracks[track];
	if (!tr->has_content || tr->length_samples == 0) return 0;
	return (int)((uint32_t)tr->playhead * MLR_GRID_COLS / tr->length_samples);
}

uint32_t __not_in_flash_func(mlr_get_rec_progress)(void)
{
	return rec_samples;
}

int __not_in_flash_func(mlr_get_flush_track)(void)
{
	if (copy_dst_track >= 0) return copy_dst_track;
	if (hdr_write_pending) return hdr_write_track;
	if (clear_pending) return clear_track;
	return -1;
}

/* ------------------------------------------------------------------ */
/* Event dispatch — execute a pattern/recall event                    */
/* ------------------------------------------------------------------ */

static void __not_in_flash_func(event_exec)(const mlr_event_t *e)
{
	mlr_event_playback_hook(e);
	switch (e->type) {
	case MLR_EVT_CUT:
	case MLR_EVT_GROUP_CUT:
		if (e->track < MLR_NUM_TRACKS && mlr_gate_mode[e->track]) {
			mlr_clear_loop(e->track);
			mlr_cut(e->track, e->param_a);
		} else {
			mlr_choke_group_cut(e->track, e->param_a);
		}
		break;
	case MLR_EVT_STOP:
		if (e->track < MLR_NUM_TRACKS && mlr_gate_mode[e->track])
			mlr_stop_track(e->track);
		else
			mlr_group_stop_track(e->track);
		break;
	case MLR_EVT_START:
		mlr_choke_group_resume(e->track, e->param_a);  /* start without CV cut trigger */
		break;
	case MLR_EVT_CUT_RELEASE:
		break;  /* CV/pulse-only event handled by mlr_event_playback_hook */
	case MLR_EVT_SPEED:
		mlr_set_speed(e->track, e->param_a);
		break;
	case MLR_EVT_GROUP_SPEED:
		/* legacy event — group-broadcast removed; apply to single track. */
		mlr_set_speed(e->track, e->param_a);
		break;
	case MLR_EVT_REVERSE:
		mlr_set_reverse(e->track, e->param_a != 0);
		break;
	case MLR_EVT_GROUP_REVERSE:
		/* legacy event — group-broadcast removed; apply to single track. */
		mlr_set_reverse(e->track, e->param_a != 0);
		break;
	case MLR_EVT_LOOP:
	case MLR_EVT_GROUP_LOOP:
		if (e->track < MLR_NUM_TRACKS && mlr_gate_mode[e->track])
			mlr_set_loop(e->track, e->param_a, e->param_b);
		else
			mlr_choke_group_set_loop(e->track, e->param_a, e->param_b);
		break;
	case MLR_EVT_LOOP_CLR:
		mlr_clear_loop(e->track);
		break;
	case MLR_EVT_VOLUME:
		mlr_set_volume(e->track, e->param_a);
		break;
	case MLR_EVT_GROUP_VOLUME:
		/* legacy event — group-broadcast removed; apply to single track. */
		mlr_set_volume(e->track, e->param_a);
		break;
	case MLR_EVT_PAN:
		mlr_set_pan(e->track, e->param_a);
		break;
	case MLR_EVT_LPF:
		mlr_set_lpf(e->track, e->param_a);
		break;
	case MLR_EVT_CHANNEL:
		if (e->track < MLR_NUM_TRACKS) {
			mlr_set_recorded_channel(e->track, (uint8_t)e->param_a & 0x01u);
			mlr_tracks[e->track].channel_user_chosen = true;
		}
		break;
	case MLR_EVT_PAT_PLAY:
		if (e->track < MLR_NUM_PATTERNS &&
		    mlr_patterns[e->track].count > 0)
			mlr_pattern_play_start(e->track);
		break;
	case MLR_EVT_PAT_STOP:
		if (e->track < MLR_NUM_PATTERNS)
			mlr_pattern_play_stop(e->track);
		break;
	case MLR_EVT_RECALL:
		if (e->track < MLR_NUM_RECALLS)
			mlr_recall_exec(e->track);
		break;
	case MLR_EVT_RECALL_UNDO:
		mlr_recall_undo();
		break;
	case MLR_EVT_MASTER: {
		if (mlr_master_override) break;  /* suppressed until loop wrap */
		uint16_t raw = ((uint16_t)e->track << 8) | (uint8_t)e->param_a;
		if (raw > 4095) raw = 4095;
		mlr_master_level_raw = raw;
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Pattern engine — timed event recorder / looping player             */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(mlr_clock_set_ms)(uint32_t now_ms)
{
	mlr_clock_ms = now_ms;
}

void __not_in_flash_func(mlr_pattern_event)(const mlr_event_t *e)
{
	uint32_t now = 0;
	bool have_now = false;

	/* transition armed patterns to recording on first event */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (mlr_patterns[p].state == MLR_PAT_ARMED) {
			if (!have_now) {
				now = mlr_clock_ms;
				have_now = true;
			}
			mlr_patterns[p].rec_start_ms = now;
			mlr_patterns[p].state = MLR_PAT_RECORDING;
		}
	}

	/* feed into all recording patterns */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (mlr_patterns[p].state != MLR_PAT_RECORDING) continue;
		if (mlr_patterns[p].count >= MLR_PATTERN_MAX_EVENTS) continue;

		mlr_pattern_t *pat = &mlr_patterns[p];
		if (!have_now) {
			now = mlr_clock_ms;
			have_now = true;
		}
		uint16_t idx = pat->count;
		pat->events[idx] = *e;
		pat->events[idx].timestamp_ms = now - pat->rec_start_ms;
		pat->count = idx + 1;
	}
}

void __not_in_flash_func(mlr_pattern_arm)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	/* disarm any other armed pattern */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (p != pat && mlr_patterns[p].state == MLR_PAT_ARMED)
			mlr_patterns[p].state = MLR_PAT_IDLE;
	}
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
	p->state = MLR_PAT_ARMED;
}

void __not_in_flash_func(mlr_pattern_rec_start)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
	p->rec_start_ms = mlr_clock_ms;
	p->state = MLR_PAT_RECORDING;
}

void __not_in_flash_func(mlr_pattern_rec_stop)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->state != MLR_PAT_RECORDING && p->state != MLR_PAT_ARMED) return;

	if (p->state == MLR_PAT_ARMED) {
		/* was armed but never started — go back to idle */
		p->state = MLR_PAT_IDLE;
		return;
	}

	uint32_t now = mlr_clock_ms;
	p->loop_len_ms = now - p->rec_start_ms;
	if (p->loop_len_ms < 10) p->loop_len_ms = 10;  /* minimum loop length */

	if (p->count == 0) {
		p->state = MLR_PAT_IDLE;
	} else {
		p->state = MLR_PAT_STOPPED;
	}
}

void __not_in_flash_func(mlr_pattern_play_start)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->count == 0) return;

	p->play_idx = 0;
	p->play_start_ms = mlr_clock_ms;
	p->state = MLR_PAT_PLAYING;
}

void __not_in_flash_func(mlr_pattern_play_stop)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	if (p->state == MLR_PAT_PLAYING) {
		p->state = MLR_PAT_STOPPED;
	}
}

void __not_in_flash_func(mlr_pattern_clear)(int pat)
{
	if (pat < 0 || pat >= MLR_NUM_PATTERNS) return;
	mlr_pattern_t *p = &mlr_patterns[pat];
	p->state = MLR_PAT_IDLE;
	p->count = 0;
	p->play_idx = 0;
	p->loop_len_ms = 0;
}

void __not_in_flash_func(mlr_pattern_tick)(uint32_t now_ms)
{
	enum { PATTERN_EVENTS_PER_TICK = 1 };
	static uint8_t start_pat;
	uint8_t events_left = PATTERN_EVENTS_PER_TICK;

	for (int i = 0; i < MLR_NUM_PATTERNS && events_left > 0; i++) {
		int p = start_pat + i;
		if (p >= MLR_NUM_PATTERNS) p -= MLR_NUM_PATTERNS;
		mlr_pattern_t *pat = &mlr_patterns[p];
		if (pat->state != MLR_PAT_PLAYING) continue;
		if (pat->count == 0) continue;

		uint32_t elapsed = now_ms - pat->play_start_ms;

		/* check for loop wrap */
		if (elapsed >= pat->loop_len_ms) {
			/* wrap: restart from beginning */
			pat->play_start_ms = now_ms;
			pat->play_idx = 0;
			elapsed = 0;
			mlr_master_override = false;  /* allow pattern master events again */
		}

		/* Fire at most one event per pattern scan to bound same-sample bursts. */
		if (pat->play_idx < pat->count &&
		    pat->events[pat->play_idx].timestamp_ms <= elapsed) {
			mlr_event_playback_source = MLR_PLAYBACK_SOURCE_PATTERN;
			event_exec(&pat->events[pat->play_idx]);
			mlr_event_playback_source = MLR_PLAYBACK_SOURCE_NONE;
			pat->play_idx++;
			pat->event_flash = 1;  /* flash for 1 LED update (~50ms) */
			events_left--;
		}
	}

	start_pat++;
	if (start_pat >= MLR_NUM_PATTERNS)
		start_pat = 0;
}

/* ------------------------------------------------------------------ */
/* Recall engine — instant snapshot replay                            */
/* ------------------------------------------------------------------ */

/* Undo buffer — stores pre-recall state */
static mlr_recall_t recall_undo;
static const mlr_event_t *recall_apply_events;
static uint16_t recall_apply_count;
static uint16_t recall_apply_idx;
static int8_t recall_apply_active_after;
static bool recall_apply_record;
static bool recall_apply_clear_undo;
static bool recall_snapshot_pending;
static uint8_t recall_snapshot_phase;
static uint8_t recall_snapshot_track;
static uint8_t recall_snapshot_track_step;
static uint8_t recall_snapshot_pattern;
static int8_t recall_snapshot_slot;
static bool recall_snapshot_record;
static uint8_t recall_match_cut_mask;
static int8_t recall_match_cut_col[MLR_NUM_TRACKS];

static void __not_in_flash_func(recall_apply_start)(const mlr_event_t *events,
	uint16_t count, int active_after, bool record_events, bool clear_undo)
{
	if (!events || count == 0) return;
	if (recall_apply_events) return;

	recall_apply_events = events;
	recall_apply_count = count;
	recall_apply_idx = 0;
	recall_apply_active_after = (int8_t)active_after;
	recall_apply_record = record_events;
	recall_apply_clear_undo = clear_undo;
}

static void __not_in_flash_func(recall_snapshot_before_apply_start)(int slot, bool record_events)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	if (recall_apply_events || recall_snapshot_pending) return;

	recall_undo.count = 0;
	recall_undo.recording = false;
	recall_undo.has_data = false;
	recall_snapshot_phase = 0;
	recall_snapshot_track = 0;
	recall_snapshot_track_step = 0;
	recall_snapshot_pattern = 0;
	recall_snapshot_slot = (int8_t)slot;
	recall_snapshot_record = record_events;
	recall_match_cut_mask = 0;
	recall_snapshot_pending = true;
}

static bool __not_in_flash_func(recall_snapshot_add)(uint8_t type, uint8_t track, int8_t a, int8_t b)
{
	if (recall_undo.count >= MLR_PATTERN_MAX_EVENTS) return false;
	mlr_event_t *e = &recall_undo.events[recall_undo.count++];
	e->timestamp_ms = 0;
	e->type = type;
	e->track = track;
	e->param_a = a;
	e->param_b = b;
	return true;
}

void __not_in_flash_func(mlr_recall_event)(const mlr_event_t *e)
{
	if (!e) return;
	if (e->type == MLR_EVT_RECALL || e->type == MLR_EVT_RECALL_UNDO) return;

	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		mlr_recall_t *rec = &mlr_recalls[r];
		if (!rec->recording) continue;
		if (rec->count >= MLR_PATTERN_MAX_EVENTS) continue;

		uint16_t idx = rec->count;
		rec->events[idx] = *e;
		rec->events[idx].timestamp_ms = 0;
		rec->count = idx + 1;
	}
}

/** Fill a recall struct with a snapshot of current state. */
static void snapshot_into(mlr_recall_t *r)
{
	r->count = 0;

	/* master volume */
	if (r->count < MLR_PATTERN_MAX_EVENTS) {
		uint16_t raw = mlr_master_level_raw;
		if (raw > 4095) raw = 4095;
		r->events[r->count].type = MLR_EVT_MASTER;
		r->events[r->count].track = (uint8_t)(raw >> 8);
		r->events[r->count].param_a = (int8_t)(raw & 0xFF);
		r->events[r->count].param_b = 0;
		r->events[r->count].timestamp_ms = 0;
		r->count++;
	}

	/* capture current state of all tracks */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_t *tr = &mlr_tracks[t];
		if (r->count >= MLR_PATTERN_MAX_EVENTS) break;

		/* speed */
		r->events[r->count].type = MLR_EVT_SPEED;
		r->events[r->count].track = (uint8_t)t;
		r->events[r->count].param_a = tr->speed_shift;
		r->events[r->count].param_b = 0;
		r->events[r->count].timestamp_ms = 0;
		r->count++;

		/* reverse */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			r->events[r->count].type = MLR_EVT_REVERSE;
			r->events[r->count].track = (uint8_t)t;
			r->events[r->count].param_a = tr->reverse ? 1 : 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* volume */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			r->events[r->count].type = MLR_EVT_VOLUME;
			r->events[r->count].track = (uint8_t)t;
			r->events[r->count].param_a = (int8_t)tr->volume_slot;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* loop (or clear loop) */
		if (r->count < MLR_PATTERN_MAX_EVENTS) {
			if (tr->loop_active) {
				r->events[r->count].type = MLR_EVT_LOOP;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = (int8_t)tr->loop_col_start;
				r->events[r->count].param_b = (int8_t)tr->loop_col_end;
			} else {
				r->events[r->count].type = MLR_EVT_LOOP_CLR;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = 0;
				r->events[r->count].param_b = 0;
			}
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}

		/* playing/stopped: cut to current position or stop */
		if (r->count < MLR_PATTERN_MAX_EVENTS && tr->has_content) {
			if (tr->playing) {
				int col = (int)((uint32_t)tr->playhead * MLR_GRID_COLS / tr->length_samples);
				r->events[r->count].type = MLR_EVT_CUT;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = (int8_t)col;
				r->events[r->count].param_b = 0;
			} else {
				r->events[r->count].type = MLR_EVT_STOP;
				r->events[r->count].track = (uint8_t)t;
				r->events[r->count].param_a = 0;
				r->events[r->count].param_b = 0;
			}
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}
	}

	/* capture motion record lane (pattern) play state */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		if (r->count >= MLR_PATTERN_MAX_EVENTS) break;
		if (mlr_patterns[p].state == MLR_PAT_PLAYING) {
			r->events[r->count].type = MLR_EVT_PAT_PLAY;
			r->events[r->count].track = (uint8_t)p;
			r->events[r->count].param_a = 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		} else if (mlr_patterns[p].state == MLR_PAT_STOPPED ||
		           mlr_patterns[p].state == MLR_PAT_RECORDING ||
		           mlr_patterns[p].state == MLR_PAT_ARMED) {
			/* stop any playing pattern when recalling */
			r->events[r->count].type = MLR_EVT_PAT_STOP;
			r->events[r->count].track = (uint8_t)p;
			r->events[r->count].param_a = 0;
			r->events[r->count].param_b = 0;
			r->events[r->count].timestamp_ms = 0;
			r->count++;
		}
	}

	r->recording = false;
	r->has_data = true;
}

void mlr_recall_snapshot(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	snapshot_into(&mlr_recalls[slot]);
	mlr_recall_active = slot;
}

void __not_in_flash_func(mlr_recall_arm)(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;

	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		if (r != slot)
			mlr_recalls[r].recording = false;
	}

	mlr_recall_t *rec = &mlr_recalls[slot];
	rec->count = 0;
	rec->recording = true;
	rec->has_data = false;
	mlr_recall_active = -1;
}

void __not_in_flash_func(mlr_recall_rec_stop)(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *rec = &mlr_recalls[slot];
	if (!rec->recording) return;

	rec->recording = false;
	rec->has_data = (rec->count > 0);
	if (!rec->has_data)
		rec->count = 0;
}

void mlr_recall_exec(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	if (!r->has_data) return;
	if (recall_apply_events || recall_snapshot_pending) return;

	recall_snapshot_before_apply_start(slot, false);
}

void mlr_recall_undo(void)
{
	if (!recall_undo.has_data) return;
	if (recall_apply_events || recall_snapshot_pending) return;
	recall_apply_start(recall_undo.events, recall_undo.count, -1, false, true);
}

void mlr_recall_exec_and_record(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	if (!r->has_data) return;
	if (recall_apply_events || recall_snapshot_pending) return;

	recall_snapshot_before_apply_start(slot, true);
}

void mlr_recall_undo_and_record(void)
{
	if (!recall_undo.has_data) return;
	if (recall_apply_events || recall_snapshot_pending) return;
	recall_apply_start(recall_undo.events, recall_undo.count, -1, true, true);
}

void __not_in_flash_func(mlr_recall_task)(void)
{
	if (recall_snapshot_pending) {
		bool emitted = false;
		while (!emitted && recall_snapshot_pending) {
			if (recall_snapshot_phase == 0) {
				uint16_t raw = mlr_master_level_raw;
				if (raw > 4095) raw = 4095;
				emitted = recall_snapshot_add(MLR_EVT_MASTER, (uint8_t)(raw >> 8),
					(int8_t)(raw & 0xFF), 0);
				recall_snapshot_phase = 1;
			} else if (recall_snapshot_phase == 1) {
				if (recall_snapshot_track >= MLR_NUM_TRACKS) {
					recall_snapshot_phase = 2;
					continue;
				}
				mlr_track_t *tr = &mlr_tracks[recall_snapshot_track];
				switch (recall_snapshot_track_step) {
				case 0:
					emitted = recall_snapshot_add(MLR_EVT_SPEED, recall_snapshot_track,
						tr->speed_shift, 0);
					recall_snapshot_track_step = 1;
					break;
				case 1:
					emitted = recall_snapshot_add(MLR_EVT_REVERSE, recall_snapshot_track,
						tr->reverse ? 1 : 0, 0);
					recall_snapshot_track_step = 2;
					break;
				case 2:
					emitted = recall_snapshot_add(MLR_EVT_CHANNEL, recall_snapshot_track,
						(int8_t)(tr->recorded_channel & 0x01), 0);
					recall_snapshot_track_step = 3;
					break;
				case 3:
					emitted = recall_snapshot_add(MLR_EVT_PAN, recall_snapshot_track,
						tr->pan, 0);
					recall_snapshot_track_step = 33;
					break;
				case 33:
					emitted = recall_snapshot_add(MLR_EVT_LPF, recall_snapshot_track,
						(int8_t)tr->lpf_slot, 0);
					recall_snapshot_track_step = 4;
					break;
				case 4:
					emitted = recall_snapshot_add(MLR_EVT_VOLUME, recall_snapshot_track,
						(int8_t)tr->volume_slot, 0);
					recall_snapshot_track_step = 5;
					break;
				case 5:
					if (tr->loop_active) {
						emitted = recall_snapshot_add(MLR_EVT_LOOP, recall_snapshot_track,
							(int8_t)tr->loop_col_start, (int8_t)tr->loop_col_end);
					} else {
						emitted = recall_snapshot_add(MLR_EVT_LOOP_CLR, recall_snapshot_track, 0, 0);
					}
					recall_snapshot_track_step = 6;
					break;
				default:
					if (tr->has_content) {
						if (tr->playing && tr->length_samples > 0) {
							int col = (int)((uint32_t)tr->playhead * MLR_GRID_COLS / tr->length_samples);
							emitted = recall_snapshot_add(MLR_EVT_CUT, recall_snapshot_track,
								(int8_t)col, 0);
						} else {
							emitted = recall_snapshot_add(MLR_EVT_STOP, recall_snapshot_track, 0, 0);
						}
					}
					recall_snapshot_track++;
					recall_snapshot_track_step = 0;
					break;
				}
			} else {
				if (recall_snapshot_pattern >= MLR_NUM_PATTERNS) {
					mlr_recall_t *r = &mlr_recalls[recall_snapshot_slot];
					recall_undo.has_data = recall_undo.count > 0;
					recall_snapshot_pending = false;
					recall_apply_start(r->events, r->count, recall_snapshot_slot,
						recall_snapshot_record, false);
					break;
				}
				mlr_pattern_t *pat = &mlr_patterns[recall_snapshot_pattern];
				if (pat->state == MLR_PAT_PLAYING) {
					emitted = recall_snapshot_add(MLR_EVT_PAT_PLAY, recall_snapshot_pattern, 0, 0);
				} else if (pat->state == MLR_PAT_STOPPED ||
				           pat->state == MLR_PAT_RECORDING ||
				           pat->state == MLR_PAT_ARMED) {
					emitted = recall_snapshot_add(MLR_EVT_PAT_STOP, recall_snapshot_pattern, 0, 0);
				}
				recall_snapshot_pattern++;
			}
			if (recall_undo.count >= MLR_PATTERN_MAX_EVENTS)
				recall_snapshot_phase = 2, recall_snapshot_pattern = MLR_NUM_PATTERNS;
		}
		return;
	}

	if (!recall_apply_events) return;

	const mlr_event_t *e = &recall_apply_events[recall_apply_idx];
	if ((e->type == MLR_EVT_CUT || e->type == MLR_EVT_GROUP_CUT) && e->track < MLR_NUM_TRACKS) {
		recall_match_cut_mask |= (uint8_t)(1u << e->track);
		recall_match_cut_col[e->track] = e->param_a;
	}
	mlr_event_playback_source = MLR_PLAYBACK_SOURCE_RECALL;
	event_exec(e);
	mlr_event_playback_source = MLR_PLAYBACK_SOURCE_NONE;
	if (recall_apply_record &&
	    e->type != MLR_EVT_PAT_PLAY &&
	    e->type != MLR_EVT_PAT_STOP) {
		mlr_pattern_event(e);
	}

	recall_apply_idx++;
	if (recall_apply_idx >= recall_apply_count) {
		if (recall_apply_clear_undo)
			recall_undo.has_data = false;
		mlr_recall_active = recall_apply_active_after;
		recall_apply_events = NULL;
		recall_apply_count = 0;
		recall_apply_idx = 0;
		recall_apply_record = false;
		recall_apply_clear_undo = false;
	}
}

void __not_in_flash_func(mlr_recall_check_active_match)(void)
{
	if (mlr_recall_active < 0 || recall_match_cut_mask == 0) return;
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		if (!(recall_match_cut_mask & (uint8_t)(1u << t))) continue;
		if (!mlr_tracks[t].playing) {
			mlr_recall_active = -1;
			return;
		}
		int col = mlr_get_column(t);
		if (col != (int)recall_match_cut_col[t]) {
			mlr_recall_active = -1;
			return;
		}
	}
}

void mlr_recall_clear(int slot)
{
	if (slot < 0 || slot >= MLR_NUM_RECALLS) return;
	mlr_recall_t *r = &mlr_recalls[slot];
	r->count = 0;
	r->recording = false;
	r->has_data = false;
	if (mlr_recall_active == slot)
		mlr_recall_active = -1;
}

/* ------------------------------------------------------------------ */
/* Scene persistence — save/load patterns + recalls + track state     */
/* ------------------------------------------------------------------ */

/* Scene is serialized into MLR_SCENE_SECTORS * 4096 bytes in flash.
 * Layout (offsets assume MLR_NUM_TRACKS=6, MLR_NUM_PATTERNS=4,
 * MLR_NUM_RECALLS=4, sizeof(mlr_track_scene_t)=8):
 *   [0]    uint32_t magic (MLR_SCENE_MAGIC)
 *   [4]    uint32_t data_len (bytes following, excluding CRC)
 *   [8]    mlr_track_scene_t[MLR_NUM_TRACKS]  (48 bytes)
 *          (track 0 _pad encodes master_level_raw + 0xA5 sentinel)
 *   [56]   pattern_count[MLR_NUM_PATTERNS] as uint16_t (8 bytes)
 *   [64]   pattern_loop_len_ms[MLR_NUM_PATTERNS] as uint32_t (16 bytes)
 *   [80]   pattern events (count * sizeof(mlr_event_t) per pattern, concatenated)
 *   [...]  recall_count[MLR_NUM_RECALLS] as uint16_t (8 bytes)
 *   [...]  recall_has_data[MLR_NUM_RECALLS] as uint8_t (4 bytes)
 *   [...]  recall events (count * sizeof(mlr_event_t) per recall, concatenated)
 *   [...]  groups_version (uint8_t) + mlr_track_groups[MLR_NUM_TRACKS] (7 bytes)
 *   [...]  gate_version  (uint8_t) + mlr_gate_mode[MLR_NUM_TRACKS]    (7 bytes)
 *   [...]  settings_version (uint8_t) + mlr_rot_range_pct (uint8_t)    (2 bytes)
 *   [...]  pan_version (uint8_t) + int8_t pan[MLR_NUM_TRACKS]          (7 bytes)
 *   [...]  lpf_version (uint8_t) + uint8_t lpf_slot[MLR_NUM_TRACKS]    (7 bytes)
 *   [...]  cv2_version (uint8_t) + mlr_cv2_mode (uint8_t)              (2 bytes)
 *   [...]  cvt_version (uint8_t) + mlr_cv1_target + mlr_cv2_target    (3 bytes)
 *          (v2: track masks; v1: 0 = last touched, 1..6 = one track)
 *   [end]  uint32_t CRC32 (over everything from offset 0 to here)
 *
 * Loaders gate the trailing groups / gate_mode blocks on data_len, so
 * older scenes without them remain compatible.
 */

/* Simple CRC32 (no table — small code size, only runs on save/load) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	crc = ~crc;
	for (uint32_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}
	return ~crc;
}

enum {
	MLR_SCENE_HEADER_BYTES = 8,
	MLR_SCENE_TRACK_BYTES = MLR_NUM_TRACKS * sizeof(mlr_track_scene_t),
	MLR_SCENE_PATTERN_META_BYTES = MLR_NUM_PATTERNS * (sizeof(uint16_t) + sizeof(uint32_t)),
	MLR_SCENE_PATTERN_EVENT_BYTES = MLR_NUM_PATTERNS * MLR_PATTERN_MAX_EVENTS * sizeof(mlr_event_t),
	MLR_SCENE_RECALL_META_BYTES = MLR_NUM_RECALLS * (sizeof(uint16_t) + sizeof(uint8_t)),
	MLR_SCENE_RECALL_EVENT_BYTES = MLR_NUM_RECALLS * MLR_PATTERN_MAX_EVENTS * sizeof(mlr_event_t),
	MLR_SCENE_GROUP_BYTES = 1 + MLR_NUM_TRACKS,
	MLR_SCENE_GATE_BYTES = 1 + MLR_NUM_TRACKS,
	MLR_SCENE_SETTINGS_BYTES = 2,
	MLR_SCENE_PAN_BYTES = 1 + MLR_NUM_TRACKS,
	MLR_SCENE_LPF_BYTES = 1 + MLR_NUM_TRACKS,
	MLR_SCENE_CV2_BYTES = 2,
	MLR_SCENE_CVT_BYTES = 3,
	MLR_SCENE_CRC_BYTES = 4,
	MLR_SCENE_MAX_SERIALIZED_BYTES =
		MLR_SCENE_HEADER_BYTES +
		MLR_SCENE_TRACK_BYTES +
		MLR_SCENE_PATTERN_META_BYTES +
		MLR_SCENE_PATTERN_EVENT_BYTES +
		MLR_SCENE_RECALL_META_BYTES +
		MLR_SCENE_RECALL_EVENT_BYTES +
		MLR_SCENE_GROUP_BYTES +
		MLR_SCENE_GATE_BYTES +
		MLR_SCENE_SETTINGS_BYTES +
		MLR_SCENE_PAN_BYTES +
		MLR_SCENE_LPF_BYTES +
		MLR_SCENE_CV2_BYTES +
		MLR_SCENE_CVT_BYTES +
		MLR_SCENE_CRC_BYTES,
	MLR_SCENE_STAGING_BYTES =
		((MLR_SCENE_MAX_SERIALIZED_BYTES + MLR_PAGE_SIZE - 1) / MLR_PAGE_SIZE) * MLR_PAGE_SIZE
};

_Static_assert((MLR_SECTOR_SIZE % MLR_PAGE_SIZE) == 0,
	"MLR_SECTOR_SIZE must be a multiple of MLR_PAGE_SIZE");
_Static_assert(MLR_SCENE_MAX_SERIALIZED_BYTES <= MLR_SCENE_SECTORS * MLR_SECTOR_SIZE,
	"MLR scene serialization exceeds reserved flash area");
_Static_assert(MLR_SCENE_STAGING_BYTES >= MLR_SCENE_MAX_SERIALIZED_BYTES,
	"MLR scene staging must fit max serialized scene");
_Static_assert(MLR_SCENE_STAGING_BYTES >= MLR_SECTOR_SIZE,
	"MLR scene staging must also fit one header sector");

/* Shared staging buffer — used as hdr_staging during header writes
 * and scene_blob during scene save.  Never concurrent. */
static union {
	uint8_t hdr[MLR_SECTOR_SIZE];
	uint8_t scene[MLR_SCENE_STAGING_BYTES];
} __attribute__((aligned(4))) staging_buf;

#define hdr_staging   (staging_buf.hdr)
#define scene_blob    (staging_buf.scene)

static uint32_t scene_serialize(void)
{
	memset(scene_blob, 0xFF, MLR_SCENE_STAGING_BYTES);
	uint32_t pos = 0;

	/* magic */
	uint32_t magic = MLR_SCENE_MAGIC;
	memcpy(&scene_blob[pos], &magic, 4); pos += 4;

	/* placeholder for data_len */
	uint32_t len_pos = pos;
	pos += 4;

	/* track states */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_scene_t ts;
		ts.speed_shift    = mlr_tracks[t].speed_shift;
		ts.reverse        = mlr_tracks[t].reverse ? 1 : 0;
		ts.loop_col_start = mlr_tracks[t].loop_active ? (int8_t)mlr_tracks[t].loop_col_start : -1;
		ts.loop_col_end   = mlr_tracks[t].loop_active ? (int8_t)mlr_tracks[t].loop_col_end   : -1;
		ts.volume_slot    = mlr_tracks[t].volume_slot;
		if (t == 0) {
			uint16_t raw = mlr_master_level_raw;
			if (raw > 4095) raw = 4095;
			ts._pad[0] = (uint8_t)(raw & 0xFF);
			ts._pad[1] = (uint8_t)(raw >> 8);
			ts._pad[2] = 0xA5;
		} else {
			ts._pad[0] = 0xFF; ts._pad[1] = 0xFF; ts._pad[2] = 0xFF;
		}
		memcpy(&scene_blob[pos], &ts, sizeof(ts)); pos += sizeof(ts);
	}

	/* pattern metadata */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint16_t cnt = mlr_patterns[p].count;
		memcpy(&scene_blob[pos], &cnt, 2); pos += 2;
	}
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint32_t ll = mlr_patterns[p].loop_len_ms;
		memcpy(&scene_blob[pos], &ll, 4); pos += 4;
	}

	/* pattern events (only used slots) */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		uint32_t sz = mlr_patterns[p].count * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(&scene_blob[pos], mlr_patterns[p].events, sz);
			pos += sz;
		}
	}

	/* recall metadata */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint16_t cnt = mlr_recalls[r].count;
		memcpy(&scene_blob[pos], &cnt, 2); pos += 2;
	}
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint8_t hd = mlr_recalls[r].has_data ? 1 : 0;
		scene_blob[pos++] = hd;
	}

	/* recall events (only used slots) */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		uint32_t sz = mlr_recalls[r].count * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(&scene_blob[pos], mlr_recalls[r].events, sz);
			pos += sz;
		}
	}

	/* track groups (appended at v1 — readers gate on pos < data_len) */
	scene_blob[pos++] = 1;  /* groups_version */
	for (int t = 0; t < MLR_NUM_TRACKS; t++)
		scene_blob[pos++] = mlr_track_groups[t];

	/* per-track gate_mode (appended at v1; gate-mode block follows groups) */
	scene_blob[pos++] = 1;  /* gate_mode version */
	for (int t = 0; t < MLR_NUM_TRACKS; t++)
		scene_blob[pos++] = mlr_gate_mode[t] ? 1 : 0;

	/* card-wide settings (appended at v1; follows gate_mode) */
	scene_blob[pos++] = 1;  /* settings version */
	scene_blob[pos++] = mlr_rot_range_pct;

	/* per-track pan (appended at v1; follows settings) */
	scene_blob[pos++] = 1;  /* pan version */
	for (int t = 0; t < MLR_NUM_TRACKS; t++)
		scene_blob[pos++] = (uint8_t)mlr_tracks[t].pan;

	/* per-track low-pass slot (appended at v1; follows pan) */
	scene_blob[pos++] = 1;  /* lpf version */
	for (int t = 0; t < MLR_NUM_TRACKS; t++)
		scene_blob[pos++] = mlr_tracks[t].lpf_slot;

	/* what CV In 2 drives (appended at v1; follows lpf) */
	scene_blob[pos++] = 1;  /* cv2 version */
	scene_blob[pos++] = mlr_cv2_mode;

	/* which tracks each CV input acts on (appended at v1; follows cv2) */
	scene_blob[pos++] = 2;  /* cvt version: track masks */
	scene_blob[pos++] = mlr_cv1_target;
	scene_blob[pos++] = mlr_cv2_target;

	/* write data_len */
	uint32_t data_len = pos - 8;
	memcpy(&scene_blob[len_pos], &data_len, 4);

	/* CRC32 over everything so far */
	uint32_t crc = crc32_update(0, scene_blob, pos);
	memcpy(&scene_blob[pos], &crc, 4); pos += 4;

	if (pos > MLR_SCENE_STAGING_BYTES)
		return 0;

	return pos;
}

void MLR_COLD_FUNC mlr_scene_load(void)
{
	const uint8_t *flash = (const uint8_t *)(XIP_BASE + MLR_SCENE_OFFSET);

	/* check magic */
	uint32_t magic;
	memcpy(&magic, flash, 4);
	if (magic != MLR_SCENE_MAGIC) return;

	uint32_t data_len;
	memcpy(&data_len, flash + 4, 4);
	uint32_t total = 8 + data_len + 4;  /* header + data + CRC */
	if (total > MLR_SCENE_SECTORS * MLR_SECTOR_SIZE) return;

	/* verify CRC */
	uint32_t crc_calc = crc32_update(0, flash, 8 + data_len);
	uint32_t crc_stored;
	memcpy(&crc_stored, flash + 8 + data_len, 4);
	if (crc_calc != crc_stored) return;

	/* deserialize */
	uint32_t pos = 8;

	/* track states — restore speed, reverse, and per-track mix; also restore
	 * master volume (track 0) and any active loop boundaries. */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_track_scene_t ts;
		memcpy(&ts, flash + pos, sizeof(ts)); pos += sizeof(ts);

		if (t == 0 && ts._pad[2] == 0xA5) {
			uint16_t raw = (uint16_t)ts._pad[0] | ((uint16_t)ts._pad[1] << 8);
			if (raw > 4095) raw = 4095;
			mlr_master_level_raw = raw;
		}

		/* Restore per-track playback state via canonical setters so any
		 * derived fields (speed_frac, volume_target, fill re-seek) stay
		 * consistent. Apply before mlr_set_loop so loop math sees the
		 * intended speed. Setters clamp invalid values defensively. */
		mlr_set_speed(t, ts.speed_shift);
		mlr_set_reverse(t, ts.reverse != 0);
		mlr_set_volume(t, ts.volume_slot);

		/* restore loop boundaries */
		if (ts.loop_col_start >= 0 && ts.loop_col_end >= 0 &&
		    mlr_tracks[t].has_content) {
			restore_loop_stopped(t, ts.loop_col_start, ts.loop_col_end);  /* don't start it */
		}
	}

	/* pattern metadata */
	uint16_t pat_counts[MLR_NUM_PATTERNS];
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		memcpy(&pat_counts[p], flash + pos, 2); pos += 2;
		if (pat_counts[p] > MLR_PATTERN_MAX_EVENTS)
			pat_counts[p] = MLR_PATTERN_MAX_EVENTS;
	}
	uint32_t pat_loop_lens[MLR_NUM_PATTERNS];
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		memcpy(&pat_loop_lens[p], flash + pos, 4); pos += 4;
	}

	/* pattern events */
	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		mlr_patterns[p].count = pat_counts[p];
		mlr_patterns[p].loop_len_ms = pat_loop_lens[p];
		mlr_patterns[p].play_idx = 0;
		mlr_patterns[p].state = (pat_counts[p] > 0) ? MLR_PAT_STOPPED : MLR_PAT_IDLE;
		uint32_t sz = pat_counts[p] * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(mlr_patterns[p].events, flash + pos, sz);
			pos += sz;
		}
	}

	/* recall metadata */
	uint16_t rec_counts[MLR_NUM_RECALLS];
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		memcpy(&rec_counts[r], flash + pos, 2); pos += 2;
		if (rec_counts[r] > MLR_PATTERN_MAX_EVENTS)
			rec_counts[r] = MLR_PATTERN_MAX_EVENTS;
	}
	uint8_t rec_has_data[MLR_NUM_RECALLS];
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		rec_has_data[r] = flash[pos++];
	}

	/* recall events */
	for (int r = 0; r < MLR_NUM_RECALLS; r++) {
		mlr_recalls[r].count = rec_counts[r];
		mlr_recalls[r].has_data = rec_has_data[r] != 0;
		mlr_recalls[r].recording = false;
		uint32_t sz = rec_counts[r] * sizeof(mlr_event_t);
		if (sz > 0) {
			memcpy(mlr_recalls[r].events, flash + pos, sz);
			pos += sz;
		}
	}

	/* track groups (appended in v1). Older scenes have no bytes here; we
	 * leave the solo defaults set by mlr_init/mlr_groups_default. */
	uint32_t end = 8 + data_len;
	if (pos + 1 + MLR_NUM_TRACKS <= end) {
		uint8_t groups_version = flash[pos++];
		(void)groups_version;  /* reserved for future format bumps */

		uint8_t loaded[MLR_NUM_TRACKS];
		for (int t = 0; t < MLR_NUM_TRACKS; t++)
			loaded[t] = flash[pos++];

		/* Validate: each mask must include the track's own bit and every
		 * other member must carry an identical mask. Otherwise demote that
		 * track to solo. */
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint8_t mask = loaded[t];
			uint8_t self = (uint8_t)(1u << t);
			if (!(mask & self)) {
				mlr_track_groups[t] = self;
				continue;
			}
			bool consistent = true;
			for (int u = 0; u < MLR_NUM_TRACKS; u++) {
				if (u == t) continue;
				if (mask & (1u << u)) {
					if (loaded[u] != mask) { consistent = false; break; }
				}
			}
			mlr_track_groups[t] = consistent ? mask : self;
		}

		/* per-track gate_mode (follows groups; same data_len-bounded read) */
		if (pos + 1 + MLR_NUM_TRACKS <= end) {
			uint8_t gate_version = flash[pos++];
			(void)gate_version;
			for (int t = 0; t < MLR_NUM_TRACKS; t++)
				mlr_gate_mode[t] = flash[pos++] != 0;

			/* card-wide settings (follow gate_mode) */
			if (pos + 2 <= end) {
				uint8_t settings_version = flash[pos++];
				(void)settings_version;
				uint8_t pct = flash[pos++];
				mlr_rot_range_pct = (pct >= 1 && pct <= 100) ? pct : MLR_ROT_RANGE_DEFAULT;

				/* per-track pan (follows settings); older scenes keep the
				 * channel-side defaults set in mlr_init() */
				if (pos + 1 + MLR_NUM_TRACKS <= end) {
					uint8_t pan_version = flash[pos++];
					(void)pan_version;
					for (int t = 0; t < MLR_NUM_TRACKS; t++)
						mlr_set_pan(t, (int8_t)flash[pos++]);

					/* per-track low-pass (follows pan); older scenes stay open */
					if (pos + 1 + MLR_NUM_TRACKS <= end) {
						uint8_t lpf_version = flash[pos++];
						(void)lpf_version;
						for (int t = 0; t < MLR_NUM_TRACKS; t++)
							mlr_set_lpf(t, flash[pos++]);

						/* CV In 2 mode (follows lpf); older scenes default to filter */
						if (pos + 2 <= end) {
							uint8_t cv2_version = flash[pos++];
							(void)cv2_version;
							uint8_t mode = flash[pos++];
							mlr_cv2_mode = (mode == MLR_CV2_MODE_ROTATION) ? MLR_CV2_MODE_ROTATION : MLR_CV2_MODE_FILTER;

							/* CV targets (follow cv2); older scenes: last touched */
							if (pos + 3 <= end) {
								uint8_t cvt_version = flash[pos++];
								uint8_t t1 = flash[pos++], t2 = flash[pos++];
								if (cvt_version < 2) {  /* v1: one track number -> mask */
									t1 = (t1 >= 1 && t1 <= MLR_NUM_TRACKS) ? (uint8_t)(1u << (t1 - 1)) : MLR_CV_TARGET_LAST;
									t2 = (t2 >= 1 && t2 <= MLR_NUM_TRACKS) ? (uint8_t)(1u << (t2 - 1)) : MLR_CV_TARGET_LAST;
								}
								mlr_cv1_target = t1 & MLR_CV_TARGET_ALL;
								mlr_cv2_target = t2 & MLR_CV_TARGET_ALL;
							}
						}
					}
				}
			}
		}
	}
}

void mlr_scene_save_start(void)
{
	if (scene_save_requested || scene_save_pending || mlr_scene_saving) {
		scene_save_followup_requested = true;
		return;
	}

	mlr_scene_saving = true;
	__dmb();
	scene_save_requested = true;
#ifdef MLR_PERF_PROFILING
	mlr_perf.scene_save_count++;
	mlr_perf_scene_save_count++;
#endif
}

static void finish_scene_save(void)
{
	scene_save_pending = false;
	if (scene_save_followup_requested) {
		scene_save_followup_requested = false;
		mlr_scene_saving = true;
		__dmb();
		scene_save_requested = true;
	} else {
		mlr_scene_saving = false;
	}
}

void __not_in_flash_func(mlr_scene_reset_params_to_defaults)(void)
{
	mlr_recall_active = -1;

	mlr_groups_default();
	memset(mlr_gate_mode, 0, sizeof(mlr_gate_mode));

	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		mlr_set_speed(t, 0);
		mlr_set_reverse(t, false);
		mlr_clear_loop(t);
		mlr_set_volume(t, 1);
		mlr_tracks[t].pan_cv_q8 = 0;  /* CV pan offset: back to the grid pan */
		mlr_set_pan(t, mlr_pan_default(t));
		mlr_set_lpf(t, MLR_LPF_OPEN);
		mlr_tracks[t].splice_q16 = 0;  /* CV rotation: back to normal at the next wrap */
	}

	for (int p = 0; p < MLR_NUM_PATTERNS; p++) {
		mlr_pattern_t *pat = &mlr_patterns[p];
		if (pat->state == MLR_PAT_PLAYING) {
			pat->play_idx = 0;
			pat->state = MLR_PAT_STOPPED;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Core 1 — ring fill, page flush, erase-ahead, header write          */
/* ------------------------------------------------------------------ */

/** Seek fill state to an arbitrary sample position using keyframes. */
/* ------------------------------------------------------------------ */
/* CV rotation (core 1)                                               */
/* ------------------------------------------------------------------ */

static inline void fill_region(const mlr_track_t *tr, uint32_t *ws, uint32_t *we)
{
	*we = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	*ws = tr->loop_active ? tr->loop_start_sample : 0;
}

/* The rotation turns the whole recording; a loop is a fixed window on the
 * grid over the rotated material, so turning the CV scrubs what it plays.
 * The CV rotation right now, in samples (always < the track length). */
static inline uint32_t fill_latch_rot(const mlr_track_t *tr)
{
	return (uint32_t)(((uint64_t)tr->length_samples * tr->splice_q16) >> 16);
}

/* Grid position of the seam (where the rotated audio reaches the end of the
 * recording and continues from its start) if it falls strictly inside the
 * loop/track window [ws, we); 0 = no seam to cross in this window. */
static inline uint32_t fill_seam_pos(const mlr_track_t *tr, uint32_t ws, uint32_t we)
{
	uint32_t rot = tr->fill_rot, len = tr->length_samples;
	if (rot == 0 || rot >= len) return 0;
	uint32_t seam = len - rot;
	return (seam > ws && seam < we) ? seam : 0u;
}

/* Audio position played at grid position v. */
static inline uint32_t fill_audio_pos(const mlr_track_t *tr, uint32_t v, uint32_t ws, uint32_t we)
{
	(void)ws; (void)we;
	uint32_t rot = tr->fill_rot, len = tr->length_samples;
	if (rot == 0 || rot >= len) return v;
	uint32_t a = v + rot;
	return a >= len ? a - len : a;
}

/* Fill wrap: latch the rotation for the new cycle and remember it so the wrap
 * preview for that wrap can be rendered with the same rotation. */
static void fill_rot_new_cycle(mlr_track_t *tr)
{
	tr->fill_rot = fill_latch_rot(tr);
	mlr_rot_event_t *ev = &tr->rot_hist[tr->rot_hist_w & (MLR_ROT_HIST_LEN - 1u)];
	ev->ring_idx = tr->pcm.w;
	ev->rot = tr->fill_rot;
	tr->rot_hist_w++;
	if (tr->rot_hist_n < MLR_ROT_HIST_LEN) tr->rot_hist_n++;
}

/* The next wrap playback will reach: the oldest recorded fill wrap still ahead
 * of pcm.r. False if the fill hasn't wrapped yet, or if more wraps are buffered
 * than recorded (tiny loops), so the rotation for that wrap isn't known. */
static bool fill_rot_next_wrap(const mlr_track_t *tr, uint32_t *ring_idx, uint32_t *rot)
{
	uint32_t r = tr->pcm.r;
	for (uint8_t i = tr->rot_hist_n; i > 0; i--) {
		const mlr_rot_event_t *ev = &tr->rot_hist[(uint8_t)(tr->rot_hist_w - i) & (MLR_ROT_HIST_LEN - 1u)];
		if ((int32_t)(ev->ring_idx - r) > 0) {
			if (i == MLR_ROT_HIST_LEN) return false;  /* older ones may be ahead too */
			*ring_idx = ev->ring_idx;
			*rot = ev->rot;
			return true;
		}
	}
	return false;
}

/* Seam fade window: up to MLR_SEAM_XFADE_SAMPLES, kept inside the window on
 * both sides of the seam. 0 = too tight, hard seam. */
static inline uint32_t seam_window(uint32_t seam, uint32_t ws, uint32_t we)
{
	uint32_t n = MLR_SEAM_XFADE_SAMPLES;
	if (n > seam - ws) n = seam - ws;
	if (n > we - seam) n = we - seam;
	return n >= 8u ? n : 0u;
}

/* The seam joins the recording's end to its start, so there is no audio past
 * either end to crossfade into: fade out into the seam and back in after it. */
static inline int16_t seam_fade(int16_t s, uint32_t dist, uint32_t n)
{
	return (int16_t)((int32_t)s * (int32_t)dist / (int32_t)n);
}

static void seek_fill_raw(mlr_track_t *tr, int t, uint32_t target);

/* Position the fill decoder for grid position target (rotated audio). */
static void seek_fill_to(mlr_track_t *tr, int t, uint32_t target)
{
	uint32_t ws, we;
	fill_region(tr, &ws, &we);
	seek_fill_raw(tr, t, fill_audio_pos(tr, target, ws, we));
	tr->fill_sample_pos = target;
	tr->fill_dec_synced = true;
}

/* Forward decode reached the seam: continue from the start of the recording. */
static inline void fill_cross_seam_forward(mlr_track_t *tr, int t)
{
	uint32_t v = tr->fill_sample_pos;
	seek_fill_raw(tr, t, 0);
	tr->fill_sample_pos = v;
}

/* Reverse decode: largest chunk below fill_sample_pos that doesn't span the seam. */
static inline uint32_t fill_reverse_avail(const mlr_track_t *tr, uint32_t ws, uint32_t we)
{
	uint32_t pos = tr->fill_sample_pos;
	uint32_t seam = fill_seam_pos(tr, ws, we);
	if (seam && pos > seam) return pos - seam;
	return pos - ws;
}

/* Decoder positioning by raw audio position (fill_sample_pos = audio). */
static void seek_fill_raw(mlr_track_t *tr, int t, uint32_t target)
{
	uint32_t kf_idx = target / MLR_KEYFRAME_INTERVAL;
	if (kf_idx >= tr->num_keyframes && tr->num_keyframes > 0)
		kf_idx = tr->num_keyframes - 1;

	uint32_t kf_sample = kf_idx * MLR_KEYFRAME_INTERVAL;
	if (tr->num_keyframes > 0) {
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = track_keyframes(t)[kf_idx].ch[ch].predictor;
			tr->fill_decode[ch].step_index = track_keyframes(t)[kf_idx].ch[ch].step_index;
		}
	} else {
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = 0;
			tr->fill_decode[ch].step_index = 0;
		}
		kf_sample = 0;
	}
	tr->fill_byte_pos   = kf_sample / 2;
	tr->fill_high_nyb   = (kf_sample & 1) != 0;
	tr->fill_sample_pos = kf_sample;

	/* decode forward to target (discard samples) */
	const uint8_t *flash_data = track_audio_xip(t);
	while (tr->fill_sample_pos < target) {
		uint8_t byte   = flash_data[tr->fill_byte_pos];
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		adpcm_decode(nybble, &tr->fill_decode[0]);
		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
		tr->fill_sample_pos++;
	}
}

/** Decode ADPCM from flash XIP and fill a track's PCM ring (forward). */
static void fill_pcm_ring_forward(int t, uint32_t max_fill)
{
	mlr_track_t *tr = &mlr_tracks[t];

	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t free_samples = pcm_ring_free(&tr->pcm);

	/* fill up to half the ring per call to stay responsive */
	uint32_t to_fill = free_samples;
	if (to_fill > MLR_RING_SAMPLES / 2) to_fill = MLR_RING_SAMPLES / 2;
	if (max_fill > 0 && to_fill > max_fill) to_fill = max_fill;

	/* determine wrap boundaries (loop-a-section or full track) */
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	/* Reverse chunks leave the decoder at the chunk's end; if the direction
	 * flipped without a seek, put it back where the fill head is. */
	if (!tr->fill_dec_synced && tr->fill_sample_pos < wrap_end) {
		seek_fill_to(tr, t, tr->fill_sample_pos);
#ifdef MLR_PERF_PROFILING
		mlr_perf_fill_resyncs++;
#endif
	}

	uint32_t seam = fill_seam_pos(tr, wrap_start, wrap_end);
	uint32_t win = seam ? seam_window(seam, wrap_start, wrap_end) : 0;

	for (uint32_t i = 0; i < to_fill; i++) {
		if (tr->fill_seek_pending) break;

		if (tr->fill_sample_pos >= wrap_end) {
			/* wrap: back to the loop/track start, with this cycle's rotation */
			fill_rot_new_cycle(tr);
			seek_fill_to(tr, t, wrap_start);
			seam = fill_seam_pos(tr, wrap_start, wrap_end);
			win = seam ? seam_window(seam, wrap_start, wrap_end) : 0;
		}
		uint32_t v = tr->fill_sample_pos;
		if (v == seam && seam) {
			fill_cross_seam_forward(tr, t);
			if (win) PERF_ROT(seam_fades); else PERF_ROT(seam_hard);
		}

		PERF_ROT_CHECK(tr, v, wrap_start, wrap_end);
		uint8_t byte   = flash_data[tr->fill_byte_pos];
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		int16_t sample = adpcm_decode(nybble, &tr->fill_decode[0]);
		if (win && v + win >= seam && v < seam + win)
			sample = seam_fade(sample, v < seam ? seam - v : v - seam + 1u, win);

		tr->pcm.buf[(tr->pcm.w % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS] = sample;

		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
		__dmb();
		tr->pcm.w++;

		tr->fill_sample_pos++;
	}
}

/**
 * Fill a track's PCM ring with reversed audio.
 * Decode forward in chunks from keyframes, then write reversed to ring.
 */
static int16_t rev_decode_tmp[MLR_KEYFRAME_INTERVAL * MLR_NUM_CHANNELS];  /* shared temp, core 1 only */

static void fill_pcm_ring_reverse(int t, uint32_t max_fill)
{
	mlr_track_t *tr = &mlr_tracks[t];

	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t free_samples = pcm_ring_free(&tr->pcm);
	uint32_t to_fill = free_samples;
	if (to_fill > MLR_RING_SAMPLES / 2) to_fill = MLR_RING_SAMPLES / 2;
	if (max_fill > 0 && to_fill > max_fill) to_fill = max_fill;

	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	while (to_fill > 0) {
		if (tr->fill_seek_pending) break;

		/* wrap: if fill_sample_pos is at or below wrap_start, jump to end
		 * with this cycle's rotation */
		if (tr->fill_sample_pos <= wrap_start) {
			fill_rot_new_cycle(tr);
			tr->fill_sample_pos = wrap_end;
		}

		/* how many samples backwards from fill_sample_pos in this batch
		 * (never across the rotation seam) */
		uint32_t avail = fill_reverse_avail(tr, wrap_start, wrap_end);
		uint32_t chunk = to_fill < avail ? to_fill : avail;
		if (chunk > MLR_KEYFRAME_INTERVAL) chunk = MLR_KEYFRAME_INTERVAL;
		if (chunk == 0) break;

		/* decode forward from (fill_sample_pos - chunk) for chunk samples */
		uint32_t decode_start = tr->fill_sample_pos - chunk;
		seek_fill_to(tr, t, decode_start);

		for (uint32_t i = 0; i < chunk; i++) {
			uint8_t byte   = flash_data[tr->fill_byte_pos];
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			rev_decode_tmp[i] = adpcm_decode(nybble, &tr->fill_decode[0]);
			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
		}

#ifdef MLR_PERF_PROFILING
		{
			/* chunk [decode_start, +chunk) must be one contiguous run of audio */
			uint32_t a0 = fill_audio_pos(tr, decode_start, wrap_start, wrap_end);
			uint32_t a1 = fill_audio_pos(tr, decode_start + chunk - 1u, wrap_start, wrap_end);
			mlr_perf_rot_samples++;
			if (a1 != a0 + chunk - 1u) mlr_perf_rot_map_errors++;
		}
#endif
		/* write into ring in reverse order, smoothing the rotation seam */
		uint32_t seam = fill_seam_pos(tr, wrap_start, wrap_end);
		uint32_t win = seam ? seam_window(seam, wrap_start, wrap_end) : 0;
		uint32_t top = decode_start + chunk;
		for (uint32_t i = 0; i < chunk; i++) {
			int16_t sample = rev_decode_tmp[chunk - 1 - i];
			uint32_t u = top - 1u - i;
			if (win && u + win >= seam && u < seam + win)
				sample = seam_fade(sample, u >= seam ? u - seam + 1u : seam - u, win);
			tr->pcm.buf[(tr->pcm.w % MLR_RING_SAMPLES) * MLR_NUM_CHANNELS] = sample;
			__dmb();
			tr->pcm.w++;
		}
		if (seam && decode_start == seam) {
			if (win) PERF_ROT(seam_fades); else PERF_ROT(seam_hard);
		}

		tr->fill_sample_pos = decode_start;
		tr->fill_dec_synced = false;
		to_fill -= chunk;
	}
}

/** Dispatch to forward or reverse ring fill. max_fill == 0 uses normal refill budget. */
static void fill_pcm_ring_limited_dir(int t, uint32_t max_fill, bool reverse)
{
	mlr_track_t *tr = &mlr_tracks[t];
	if (!tr->has_content) return;

#ifdef MLR_PERF_PROFILING
	uint32_t start = time_us_32();
#endif
	if (reverse)
		fill_pcm_ring_reverse(t, max_fill);
	else
		fill_pcm_ring_forward(t, max_fill);
#ifdef MLR_PERF_PROFILING
	uint32_t elapsed = time_us_32() - start;
	perf_update_max(&mlr_perf.refill_max_us, elapsed);
	perf_update_max(&mlr_perf_refill_max_us, elapsed);
#endif
}

static void fill_pcm_ring_limited(int t, uint32_t max_fill)
{
	fill_pcm_ring_limited_dir(t, max_fill, mlr_tracks[t].reverse);
}

static void fill_pcm_ring(int t)
{
	fill_pcm_ring_limited(t, 0);
}

static uint16_t fill_seek_preview_forward(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	for (uint16_t i = 0; i < MLR_SEEK_PREVIEW_SAMPLES; i++) {
		if (tr->fill_sample_pos >= wrap_end) {
			seek_fill_to(tr, t, wrap_start);
		}
		if (tr->fill_rot && tr->fill_sample_pos == fill_seam_pos(tr, wrap_start, wrap_end))
			fill_cross_seam_forward(tr, t);

		uint8_t byte   = flash_data[tr->fill_byte_pos];
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		tr->seek_preview[i] = adpcm_decode(nybble, &tr->fill_decode[0]);

		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
		tr->fill_sample_pos++;
	}

	return MLR_SEEK_PREVIEW_SAMPLES;
}

static uint16_t fill_seek_preview_reverse(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
	uint16_t written = 0;

	while (written < MLR_SEEK_PREVIEW_SAMPLES) {
		if (tr->fill_sample_pos <= wrap_start) {
			tr->fill_sample_pos = wrap_end;
		}

		uint32_t avail = fill_reverse_avail(tr, wrap_start, wrap_end);
		uint32_t remaining = (uint32_t)MLR_SEEK_PREVIEW_SAMPLES - written;
		uint32_t chunk = remaining < avail ? remaining : avail;
		if (chunk > MLR_KEYFRAME_INTERVAL) chunk = MLR_KEYFRAME_INTERVAL;
		if (chunk == 0) break;

		uint32_t decode_start = tr->fill_sample_pos - chunk;
		seek_fill_to(tr, t, decode_start);

		for (uint32_t i = 0; i < chunk; i++) {
			uint8_t byte   = flash_data[tr->fill_byte_pos];
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			rev_decode_tmp[i] = adpcm_decode(nybble, &tr->fill_decode[0]);
			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
		}

		for (uint32_t i = 0; i < chunk; i++) {
			tr->seek_preview[written++] = rev_decode_tmp[chunk - 1u - i];
		}

		tr->fill_sample_pos = decode_start;
		tr->fill_dec_synced = false;
	}

	return written;
}

static uint16_t fill_wrap_preview_forward(int t, uint32_t steps)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;

	for (uint16_t i = 0; i < MLR_SEEK_PREVIEW_SAMPLES; i++) {
		int16_t sample = 0;
		for (uint32_t s = 0; s < steps; s++) {
			if (tr->fill_sample_pos >= wrap_end) {
				seek_fill_to(tr, t, wrap_start);
			}
			if (tr->fill_rot && tr->fill_sample_pos == fill_seam_pos(tr, wrap_start, wrap_end))
				fill_cross_seam_forward(tr, t);

			uint8_t byte   = flash_data[tr->fill_byte_pos];
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			sample = adpcm_decode(nybble, &tr->fill_decode[0]);

			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
			tr->fill_sample_pos++;
		}
		tr->seek_preview[i] = sample;
	}

	return MLR_SEEK_PREVIEW_SAMPLES;
}

static uint16_t fill_wrap_preview_reverse(int t, uint32_t steps)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
	uint16_t written = 0;

	while (written < MLR_SEEK_PREVIEW_SAMPLES) {
		if (tr->fill_sample_pos <= wrap_start) {
			tr->fill_sample_pos = wrap_end;
		}

		uint32_t avail = fill_reverse_avail(tr, wrap_start, wrap_end);
		uint32_t chunk_outputs = (uint32_t)MLR_SEEK_PREVIEW_SAMPLES - written;
		uint32_t max_outputs = avail / steps;
		if (chunk_outputs > max_outputs) chunk_outputs = max_outputs;
		if (chunk_outputs == 0) break;
		uint32_t chunk_samples = chunk_outputs * steps;
		if (chunk_samples > MLR_KEYFRAME_INTERVAL) {
			chunk_outputs = MLR_KEYFRAME_INTERVAL / steps;
			chunk_samples = chunk_outputs * steps;
		}
		if (chunk_samples == 0) break;

		uint32_t decode_start = tr->fill_sample_pos - chunk_samples;
		seek_fill_to(tr, t, decode_start);

		for (uint32_t i = 0; i < chunk_samples; i++) {
			uint8_t byte   = flash_data[tr->fill_byte_pos];
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			rev_decode_tmp[i] = adpcm_decode(nybble, &tr->fill_decode[0]);
			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
		}

		for (uint32_t i = 0; i < chunk_outputs; i++) {
			uint32_t idx = chunk_samples - steps - (i * steps);
			tr->seek_preview[written++] = rev_decode_tmp[idx];
		}

		tr->fill_sample_pos = decode_start;
	}

	return written;
}

static uint16_t render_wrap_preview_forward(int t, uint16_t start_accum)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
	uint32_t source_needed = wrap_preview_source_span(tr->speed_frac, start_accum) + 2u;
	if (source_needed > MLR_KEYFRAME_INTERVAL) source_needed = MLR_KEYFRAME_INTERVAL;

	for (uint32_t i = 0; i < source_needed; i++) {
		if (tr->fill_sample_pos >= wrap_end) {
			seek_fill_to(tr, t, wrap_start);
		}
		if (tr->fill_rot && tr->fill_sample_pos == fill_seam_pos(tr, wrap_start, wrap_end))
			fill_cross_seam_forward(tr, t);
		uint8_t byte   = flash_data[tr->fill_byte_pos];
		uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
		rev_decode_tmp[i] = adpcm_decode(nybble, &tr->fill_decode[0]);
		if (tr->fill_high_nyb) {
			tr->fill_byte_pos++;
			tr->fill_high_nyb = false;
		} else {
			tr->fill_high_nyb = true;
		}
		tr->fill_sample_pos++;
	}

	uint32_t next_idx = 0;
	uint16_t accum = start_accum;
	int16_t last = rev_decode_tmp[0];
	for (uint16_t out = 0; out < MLR_SEEK_PREVIEW_SAMPLES; out++) {
		accum += tr->speed_frac;
		while (accum >= 256) {
			accum -= 256;
			if (next_idx < source_needed)
				last = rev_decode_tmp[next_idx++];
		}
		if (accum == 0) {
			tr->seek_preview[out] = last;
		} else {
			uint32_t lookahead = next_idx < source_needed ? next_idx : source_needed - 1u;
			tr->seek_preview[out] = linear_interp_q8(last, rev_decode_tmp[lookahead], (uint8_t)accum);
		}
	}

	return MLR_SEEK_PREVIEW_SAMPLES;
}

static uint16_t render_wrap_preview_reverse(int t, uint16_t start_accum)
{
	mlr_track_t *tr = &mlr_tracks[t];
	const uint8_t *flash_data = track_audio_xip(t);
	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
	uint32_t source_needed = wrap_preview_source_span(tr->speed_frac, start_accum) + 2u;
	if (source_needed > MLR_KEYFRAME_INTERVAL) source_needed = MLR_KEYFRAME_INTERVAL;
	uint32_t written = 0;

	while (written < source_needed) {
		if (tr->fill_sample_pos <= wrap_start) {
			tr->fill_sample_pos = wrap_end;
		}
		uint32_t avail = fill_reverse_avail(tr, wrap_start, wrap_end);
		uint32_t chunk = source_needed - written;
		if (chunk > avail) chunk = avail;
		if (chunk > MLR_KEYFRAME_INTERVAL) chunk = MLR_KEYFRAME_INTERVAL;
		if (chunk == 0) break;

		uint32_t decode_start = tr->fill_sample_pos - chunk;
		seek_fill_to(tr, t, decode_start);
		for (uint32_t i = 0; i < chunk; i++) {
			uint8_t byte   = flash_data[tr->fill_byte_pos];
			uint8_t nybble = tr->fill_high_nyb ? (byte >> 4) : (byte & 0x0F);
			rev_decode_tmp[written + chunk - 1u - i] = adpcm_decode(nybble, &tr->fill_decode[0]);
			if (tr->fill_high_nyb) {
				tr->fill_byte_pos++;
				tr->fill_high_nyb = false;
			} else {
				tr->fill_high_nyb = true;
			}
		}
		written += chunk;
		tr->fill_sample_pos = decode_start;
	}
	if (written == 0) return 0;

	uint32_t next_idx = 0;
	uint16_t accum = start_accum;
	int16_t last = rev_decode_tmp[0];
	for (uint16_t out = 0; out < MLR_SEEK_PREVIEW_SAMPLES; out++) {
		accum += tr->speed_frac;
		while (accum >= 256) {
			accum -= 256;
			if (next_idx < written)
				last = rev_decode_tmp[next_idx++];
		}
		if (accum == 0) {
			tr->seek_preview[out] = last;
		} else {
			uint32_t lookahead = next_idx < written ? next_idx : written - 1u;
			tr->seek_preview[out] = linear_interp_q8(last, rev_decode_tmp[lookahead], (uint8_t)accum);
		}
	}

	return MLR_SEEK_PREVIEW_SAMPLES;
}

static void prepare_wrap_preview(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	if (!tr->has_content || !tr->playing || tr->stop_pending) return;
	if (tr->fill_seek_pending || tr->seek_handoff_pending || tr->seek_xfade_active) return;
	uint32_t steps = integer_speed_steps(tr->speed_frac);

	uint32_t wrap_end   = tr->loop_active ? tr->loop_end_sample   : tr->length_samples;
	uint32_t wrap_start = tr->loop_active ? tr->loop_start_sample : 0;
	uint32_t source_span = steps ? MLR_SEEK_PREVIEW_SAMPLES * steps :
		wrap_preview_source_span(tr->speed_frac, tr->speed_accum);
	if (wrap_end <= wrap_start || wrap_end - wrap_start <= source_span) return;

	/* The preview is for the next wrap playback reaches, rendered with the
	 * rotation the fill used there (the fill is ahead, so it already wrapped). */
	uint32_t next_ring_idx, next_rot;
	if (!fill_rot_next_wrap(tr, &next_ring_idx, &next_rot)) return;

	if (tr->wrap_preview_ready &&
	    tr->wrap_preview_ring_idx == next_ring_idx &&
	    tr->wrap_preview_rot == next_rot &&
	    tr->wrap_preview_reverse == tr->reverse &&
	    tr->wrap_preview_start == wrap_start &&
	    tr->wrap_preview_end == wrap_end &&
	    tr->wrap_preview_speed_frac == tr->speed_frac)
		return;

	/* Core 0 must not start a crossfade from a half-rewritten preview. */
	tr->wrap_preview_ready = false;
	__dmb();

	uint32_t saved_byte_pos = tr->fill_byte_pos;
	bool saved_high_nyb = tr->fill_high_nyb;
	uint32_t saved_sample_pos = tr->fill_sample_pos;
	uint32_t saved_rot = tr->fill_rot;
	tr->fill_rot = next_rot;
	adpcm_state_t saved_decode[MLR_NUM_CHANNELS];
	memcpy(saved_decode, tr->fill_decode, sizeof(saved_decode));

	uint16_t preview_count;
	if (steps && tr->speed_accum == 0) {
		if (tr->reverse) {
			tr->fill_sample_pos = wrap_end;
			preview_count = fill_wrap_preview_reverse(t, steps);
		} else {
			seek_fill_to(tr, t, wrap_start);
			preview_count = fill_wrap_preview_forward(t, steps);
		}
	} else {
		if (tr->reverse) {
			tr->fill_sample_pos = wrap_end;
			preview_count = render_wrap_preview_reverse(t, tr->speed_accum);
		} else {
			seek_fill_to(tr, t, wrap_start);
			preview_count = render_wrap_preview_forward(t, tr->speed_accum);
		}
	}

	tr->fill_byte_pos = saved_byte_pos;
	tr->fill_high_nyb = saved_high_nyb;
	tr->fill_sample_pos = saved_sample_pos;
	tr->fill_rot = saved_rot;
	memcpy(tr->fill_decode, saved_decode, sizeof(saved_decode));

	tr->seek_preview_count = preview_count;
	tr->wrap_preview_ring_idx = next_ring_idx;
	tr->wrap_preview_rot = next_rot;
	tr->wrap_preview_reverse = tr->reverse;
	tr->wrap_preview_start = wrap_start;
	tr->wrap_preview_end = wrap_end;
	tr->wrap_preview_speed_frac = tr->speed_frac;
	/* Per-sample fast-skip threshold. The actual trigger fires within ~one
	 * sample of (wrap_end - source_span) for forward / (wrap_start + source_span)
	 * for reverse. Add a small guard band so accumulator drift / boundary
	 * fuzziness can't cause the pre-gate to miss the trigger window. */
	{
		uint32_t guard = (source_span >> 2) + 4u;
		uint32_t arm;
		if (tr->reverse) {
			arm = wrap_start + source_span + guard;
			if (arm > wrap_end) arm = wrap_end;
		} else {
			arm = (wrap_end > source_span + guard)
				? wrap_end - source_span - guard
				: 0u;
		}
		tr->wrap_preview_arm_playhead = arm;
	}
	__dmb();
	tr->wrap_preview_ready = (preview_count > 0);
	transition_update_track_state(tr);
}

/** Handle a pending seek: restore decoder state from keyframe, refill. */
static void handle_seek(int t)
{
	mlr_track_t *tr = &mlr_tracks[t];
	uint32_t target = tr->seek_target_sample;
	bool reverse_change = tr->seek_reverse_pending;
	bool target_reverse = reverse_change ? tr->seek_reverse_target : tr->reverse;
	bool start_pending = tr->seek_start_pending;

#ifdef MLR_PERF_PROFILING
	uint32_t start = time_us_32();
	mlr_perf.seek_count[t]++;
	mlr_perf_seek_count[t]++;
#endif
	/* A cut or loop/direction change starts a new cycle at the current CV. */
	tr->fill_rot = fill_latch_rot(tr);
	tr->rot_hist_n = 0;
	if (target_reverse) tr->fill_sample_pos = target;
	else                seek_fill_to(tr, t, target);

	uint32_t new_start;
	uint32_t handoff_playhead;
	uint32_t handoff_avail = 0;
	if (start_pending) {
		uint32_t reserve = MLR_SEEK_PRIME_SAMPLES;
		uint32_t free_samples = pcm_ring_free(&tr->pcm);
		if (free_samples < reserve) {
			uint32_t drop = reserve - free_samples;
			uint32_t avail = pcm_ring_avail(&tr->pcm);
			if (drop > avail) drop = avail;
			tr->pcm.r += drop;
			__dmb();
		}
		new_start = tr->pcm.w;
		handoff_playhead = target;
		tr->seek_preview_count = 0;
		tr->fill_seek_pending = false;
		fill_pcm_ring_limited_dir(t, MLR_SEEK_PRIME_SAMPLES, target_reverse);
		__dmb();
		handoff_avail = tr->pcm.w - new_start;
	} else {
		uint16_t preview_count = target_reverse
			? fill_seek_preview_reverse(t)
			: fill_seek_preview_forward(t);
		tr->seek_preview_count = preview_count;
		tr->seek_xfade_pos = 0;
		handoff_playhead = tr->fill_sample_pos;
		new_start = tr->pcm.w;
		tr->fill_seek_pending = false;
		fill_pcm_ring_limited_dir(t, MLR_SEEK_PRIME_SAMPLES, target_reverse);
		__dmb();
		handoff_avail = tr->pcm.w - new_start;
	}

	if (reverse_change) {
		tr->seek_handoff_reverse_pending = true;
		tr->seek_handoff_reverse_target = target_reverse;
		tr->seek_reverse_pending = false;
		PERF_NOTE_REVERSE_HANDOFF(t, handoff_avail);
	} else {
		tr->seek_handoff_reverse_pending = false;
	}
	
	tr->seek_handoff_r = new_start;
	tr->seek_handoff_playhead = handoff_playhead;
	tr->seek_handoff_speed_accum = 0;
	tr->seek_handoff_start_pending = start_pending;
	__dmb();
	tr->seek_handoff_pending = true;
	transition_update_track_state(tr);

	tr->seek_start_pending = false;
#ifdef MLR_PERF_PROFILING
	uint32_t elapsed = time_us_32() - start;
	perf_update_max(&mlr_perf.seek_max_us, elapsed);
	perf_update_max(&mlr_perf_seek_max_us, elapsed);
#endif
}

/* ---- Erase hold-off (core 1) ------------------------------------------
 * A sector erase blocks core 1 for up to ~34 ms, so no playback ring is
 * refilled meanwhile. A track that was just cut has an almost empty ring and
 * would run dry. Before any erase, require every playing track to hold enough
 * for ERASE_GUARD_MS at its speed; otherwise skip the erase this pass and keep
 * refilling. Capped so recording and header writes are never held for long. */
#define ERASE_GUARD_MS      36u
#define ERASE_HOLD_MAX_US   100000u

static bool     erase_holding = false;
static uint32_t erase_hold_since_us;

/* True if the erase should wait. urgent = the data behind it can't wait. */
static bool erase_hold_off(bool urgent)
{
	bool low = false;
	for (int t = 0; t < MLR_NUM_TRACKS && !low; t++) {
		const mlr_track_t *tr = &mlr_tracks[t];
		if (!tr->has_content || !tr->playing || tr->stop_pending) continue;
		/* Mid-cut the read index still points at the old audio until the
		 * handoff/crossfade jumps it to seek_handoff_r; count from there. */
		uint32_t r = (tr->seek_handoff_pending || tr->seek_xfade_active) ? tr->seek_handoff_r : tr->pcm.r;
		uint32_t avail = tr->pcm.w - r;
		uint32_t need = (ERASE_GUARD_MS * (MLR_SAMPLE_RATE_HZ / 1000u) * tr->speed_frac) >> 8;
		if (avail < need) low = true;
	}
	uint32_t now = time_us_32();
	if (low && !urgent) {
		if (!erase_holding) {
			erase_holding = true;
			erase_hold_since_us = now;
#ifdef MLR_PERF_PROFILING
			mlr_perf_erase_holds++;
#endif
		}
		if (now - erase_hold_since_us < ERASE_HOLD_MAX_US)
			return true;
#ifdef MLR_PERF_PROFILING
		mlr_perf_erase_forced++;
#endif
	}
#ifdef MLR_PERF_PROFILING
	if (erase_holding && now - erase_hold_since_us > mlr_perf_erase_hold_max_us)
		mlr_perf_erase_hold_max_us = now - erase_hold_since_us;
#endif
	erase_holding = false;
	return false;
}

/** Drain one page from the recording page ring to flash. */
static void flush_rec_page(void)
{
	if (mlr_page_ring.r == mlr_page_ring.w) return;  /* nothing to flush */

	uint8_t slot = mlr_page_ring.r % MLR_PAGE_RING_COUNT;

	/* erase-ahead: if we've reached the next sector boundary, erase it
	 * (held off while a playback ring is low, unless pages are piling up) */
	if (rec_flash_offset >= rec_next_erase) {
		if (erase_hold_off(page_ring_used_pages() >= MLR_PAGE_RING_COUNT / 2u))
			return;
		PERF_FLASH_ERASE(rec_next_erase, MLR_SECTOR_SIZE);
		rec_next_erase += MLR_SECTOR_SIZE;
	}

	/* write the page */
	PERF_FLASH_PROGRAM(rec_flash_offset, mlr_page_ring.pages[slot], MLR_PAGE_SIZE);
	rec_flash_offset += MLR_PAGE_SIZE;

	__dmb();
	mlr_page_ring.r++;
#ifdef MLR_PERF_PROFILING
	mlr_perf.rec_page_flush_count++;
	mlr_perf_rec_page_flush_count++;
#endif
}

/** Write the track header after recording stops or metadata changes. */

static void write_track_header(int t, bool start_playback)
{
	mlr_track_t *tr = &mlr_tracks[t];

	/* build header in static buffer (too large for core 1 stack). Keyframes
	 * come from the recording that just finished (start_playback), else they
	 * are the ones already in flash (metadata rewrite). */
	if (start_playback) {
		memset(hdr_staging, 0xFF, MLR_SECTOR_SIZE);
		memcpy(((mlr_track_header_t *)hdr_staging)->keyframes, rec_keyframes,
		       tr->num_keyframes * sizeof(mlr_keyframe_channels_t));
	} else {
		memcpy(hdr_staging, (const void *)(XIP_BASE + track_hdr_flash_off(t)), MLR_SECTOR_SIZE);
	}

	mlr_track_header_t *hdr = (mlr_track_header_t *)hdr_staging;
	hdr->magic               = MLR_MAGIC;
	hdr->sample_count        = tr->length_samples;
	hdr->adpcm_bytes         = tr->length_bytes;
	hdr->num_keyframes       = tr->num_keyframes;
	hdr->record_speed_shift  = tr->record_speed_shift;
	hdr->recorded_channel    = (uint8_t)(tr->recorded_channel & 0x01);
	hdr->rate_mark           = tr->rate_mark;
	hdr->cv1_pitch_mode      = tr->cv1_pitch_enabled ? MLR_CV1_PITCH_ENABLED_MODE : MLR_CV1_PITCH_DISABLED_MODE;

	uint32_t hdr_off = track_hdr_flash_off(t);
	PERF_FLASH_ERASE(hdr_off, MLR_SECTOR_SIZE);
	PERF_FLASH_PROGRAM(hdr_off, hdr_staging, MLR_SECTOR_SIZE);

	if (start_playback) {
		/* mark track as playable */
		tr->has_content     = true;
		tr->playing = true;
		tr->playhead        = 0;
		tr->fill_byte_pos   = 0;
		tr->fill_high_nyb   = false;
		tr->fill_sample_pos = 0;
		for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
			tr->fill_decode[ch].predictor  = 0;
			tr->fill_decode[ch].step_index = 0;
			if (tr->num_keyframes > 0) {
				tr->fill_decode[ch].predictor  = rec_keyframes[0].ch[ch].predictor;
				tr->fill_decode[ch].step_index = rec_keyframes[0].ch[ch].step_index;
			}
		}
		tr->pcm.w = 0;
		tr->pcm.r = 0;
		fill_rot_reset(tr);
		tr->speed_frac = speed_shift_to_frac(tr->speed_shift);
		tr->speed_accum = 0;
		reset_track_audio_state(tr);
		tr->fade_in_count = MLR_FADE_SAMPLES;
		transition_update_track_state(tr);
		/* preserve reverse — only affects playback, recording always forward */
	}
}

static void copy_prepare_destination_ram(int dst)
{
	mlr_track_t *tr = &mlr_tracks[dst];
	mlr_leave_group(dst);
	tr->playing        = false;
	tr->has_content    = false;
	tr->length_samples = 0;
	tr->length_bytes   = 0;
	tr->num_keyframes  = 0;
	tr->playhead       = 0;
	tr->loop_active    = false;
	tr->loop_start_sample = 0;
	tr->loop_end_sample   = 0;
	tr->loop_col_start    = -1;
	tr->loop_col_end      = -1;
	tr->fill_byte_pos   = 0;
	tr->fill_high_nyb   = false;
	tr->fill_sample_pos = 0;
	tr->fill_seek_pending = false;
	tr->seek_target_sample = 0;
	tr->seek_reverse_pending = false;
	tr->seek_start_pending = false;
	tr->speed_shift = 0;
	tr->speed_frac  = 256;
	tr->speed_accum = 0;
	tr->reverse     = false;
	tr->muted       = false;
	tr->volume_slot = 1;
	tr->volume_frac = 256;
	tr->volume_target = 256;
	tr->channel_user_chosen = false;
	tr->pcm.w = 0;
	tr->pcm.r = 0;
	fill_rot_reset(tr);
	reset_track_audio_state(tr);
	mlr_gate_mode[dst] = false;
}

static bool copy_begin_next_track(void)
{
	if (copy_dst_mask == 0) return false;
	for (int dst = 0; dst < MLR_NUM_TRACKS; dst++) {
		uint8_t bit = (uint8_t)(1u << dst);
		if (!(copy_dst_mask & bit)) continue;
		copy_dst_mask &= (uint8_t)~bit;
		copy_dst_track = dst;
		copy_bytes_done = 0;
		copy_next_erase = track_audio_flash_off(dst);
		copy_prepare_destination_ram(dst);
		return true;
	}
	return false;
}

static void copy_finish_track(void)
{
	mlr_track_t *src = &mlr_tracks[copy_src_track];
	mlr_track_t *dst = &mlr_tracks[copy_dst_track];

	memset(hdr_staging, 0xFF, MLR_SECTOR_SIZE);
	mlr_track_header_t *hdr = (mlr_track_header_t *)hdr_staging;
	hdr->magic               = MLR_MAGIC;
	hdr->sample_count        = src->length_samples;
	hdr->adpcm_bytes         = src->length_bytes;
	hdr->num_keyframes       = src->num_keyframes;
	hdr->record_speed_shift  = src->record_speed_shift;
	hdr->recorded_channel    = (uint8_t)(src->recorded_channel & 0x01);
	hdr->rate_mark           = src->rate_mark;
	hdr->cv1_pitch_mode      = src->cv1_pitch_enabled ? MLR_CV1_PITCH_ENABLED_MODE : MLR_CV1_PITCH_DISABLED_MODE;
	memcpy(hdr->keyframes, track_keyframes(copy_src_track),
	       src->num_keyframes * sizeof(mlr_keyframe_channels_t));

	uint32_t hdr_off = track_hdr_flash_off(copy_dst_track);
	PERF_FLASH_ERASE(hdr_off, MLR_SECTOR_SIZE);
	PERF_FLASH_PROGRAM(hdr_off, hdr_staging, MLR_SECTOR_SIZE);

	dst->has_content        = true;
	dst->length_samples     = src->length_samples;
	dst->length_bytes       = src->length_bytes;
	dst->num_keyframes      = src->num_keyframes;
	dst->record_speed_shift = src->record_speed_shift;
	dst->recorded_channel   = src->recorded_channel & 0x01;
	mlr_set_pan(copy_dst_track, src->pan);
	mlr_set_lpf(copy_dst_track, src->lpf_slot);
	dst->rate_mark          = src->rate_mark;
	dst->cv1_pitch_enabled  = src->cv1_pitch_enabled;
	for (int ch = 0; ch < MLR_NUM_CHANNELS; ch++) {
		dst->fill_decode[ch].predictor  = 0;
		dst->fill_decode[ch].step_index = 0;
		if (dst->num_keyframes > 0) {
			dst->fill_decode[ch].predictor  = track_keyframes(copy_src_track)[0].ch[ch].predictor;
			dst->fill_decode[ch].step_index = track_keyframes(copy_src_track)[0].ch[ch].step_index;
		}
	}
}

static void copy_track_task(void)
{
	if (!copy_pending) return;
	if (copy_dst_track < 0 && !copy_begin_next_track()) {
		copy_pending = false;
		copy_src_track = -1;
		mlr_copying = false;
		mlr_flushing = false;
		return;
	}

	const uint8_t *src_audio = track_audio_xip(copy_src_track);
	if (copy_bytes_done < copy_bytes_total) {
		uint32_t dst_off = track_audio_flash_off(copy_dst_track) + copy_bytes_done;
		if (dst_off >= copy_next_erase) {
			if (erase_hold_off(false)) return;
			PERF_FLASH_ERASE(copy_next_erase, MLR_SECTOR_SIZE);
			copy_next_erase += MLR_SECTOR_SIZE;
			return;
		}

		uint32_t page_len = copy_bytes_total - copy_bytes_done;
		if (page_len > MLR_PAGE_SIZE) page_len = MLR_PAGE_SIZE;
		memset(copy_page, 0xFF, MLR_PAGE_SIZE);
		memcpy(copy_page, &src_audio[copy_bytes_done], page_len);
		PERF_FLASH_PROGRAM(dst_off, copy_page, MLR_PAGE_SIZE);
		copy_bytes_done += page_len;
		return;
	}

	if (erase_hold_off(false)) return;  /* the header write erases a sector */
	copy_finish_track();
	copy_dst_track = -1;
	copy_bytes_done = 0;
	if (copy_dst_mask == 0) {
		copy_pending = false;
		copy_src_track = -1;
		mlr_copying = false;
		mlr_flushing = false;
	}
}

static int fill_rr = 0;  /* round-robin index for ring refill */
static int wrap_preview_rr = 0;

void mlr_io_task(void)
{
	PERF_SERVICE_RESET_REQUEST();

	/* Settings changed from the sample manager: save now that it's done
	 * with flash (this task doesn't run during a sample-manager session). */
	if (mlr_settings_save_pending) {
		mlr_settings_save_pending = false;
		mlr_scene_save_start();
	}

	/* 1. Handle pending seeks (always scan all — seeks are rare + cheap) */
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		if (mlr_tracks[t].fill_seek_pending) {
			handle_seek(t);
		}
	}

	/* 2. Drain recording pages first so record writes are not starved by
	 *    playback refill work when many tracks are running. */
	if (rec_track_idx >= 0 || mlr_page_ring.r != mlr_page_ring.w) {
		flush_rec_page();

		/* If backlog is growing, drain a few extra pages this tick. */
		uint32_t used = page_ring_used_pages();
		if (used > (MLR_PAGE_RING_COUNT / 2)) {
			int extra_budget = 3; /* up to 4 pages total this call */
			while (extra_budget-- > 0 && mlr_page_ring.r != mlr_page_ring.w) {
				flush_rec_page();
				if (page_ring_used_pages() <= (MLR_PAGE_RING_COUNT / 2))
					break;
			}
		}
	}

	/* 3. Refill ONE playing track's ring per call (keeps USB/mext
	 *    responsive), choosing the one that would run dry soonest: least
	 *    buffered time at its playback speed. A freshly cut track (only its
	 *    seek prime buffered) gets filled first instead of waiting its turn
	 *    behind the others. Ties go round-robin. */
	{
		int best = -1;
		uint32_t best_avail = 0, best_speed = 1;
		for (int i = 0; i < MLR_NUM_TRACKS; i++) {
			int t = (fill_rr + i) % MLR_NUM_TRACKS;
			mlr_track_t *tr = &mlr_tracks[t];
			if (!tr->has_content || !tr->playing ||
			    tr->seek_handoff_pending || tr->seek_xfade_active) continue;
			uint32_t avail = tr->pcm.w - tr->pcm.r;
			uint32_t speed = tr->speed_frac ? tr->speed_frac : 1u;
			/* avail / speed < best_avail / best_speed, without dividing */
			if (best < 0 || (uint64_t)avail * best_speed < (uint64_t)best_avail * speed) {
				best = t;
				best_avail = avail;
				best_speed = speed;
			}
		}
		if (best >= 0) {
			fill_pcm_ring(best);
			fill_rr = (best + 1) % MLR_NUM_TRACKS;
		}
	}

	/* 4. Prepare one wrap crossfade preview opportunistically. */
	for (int i = 0; i < MLR_NUM_TRACKS; i++) {
		int t = (wrap_preview_rr + i) % MLR_NUM_TRACKS;
		if (mlr_tracks[t].has_content && mlr_tracks[t].playing) {
			prepare_wrap_preview(t);
			wrap_preview_rr = (t + 1) % MLR_NUM_TRACKS;
			break;
		}
	}

	/* 5. Handle pending clear (quick — one sector erase) */
	if (clear_pending && !erase_hold_off(false)) {
		PERF_FLASH_ERASE(track_hdr_flash_off(clear_track), MLR_SECTOR_SIZE);
		hdr_rewrite_pending_mask &= (uint8_t)~(1u << clear_track);
		clear_pending = false;
	}

	if (!hdr_write_pending && hdr_rewrite_pending_mask &&
	    mlr_page_ring.r == mlr_page_ring.w &&
	    rec_track_idx < 0 && !clear_pending &&
	    !copy_pending && copy_dst_track < 0 &&
	    !scene_save_requested && !scene_save_pending && !mlr_scene_saving) {
		for (int t = 0; t < MLR_NUM_TRACKS; t++) {
			uint8_t bit = (uint8_t)(1u << t);
			if (!(hdr_rewrite_pending_mask & bit)) continue;
			hdr_rewrite_pending_mask &= (uint8_t)~bit;
			if (!mlr_tracks[t].has_content || mlr_tracks[t].length_samples == 0)
				break;
			hdr_write_track = t;
			hdr_write_start_playback = false;
			__dmb();
			hdr_write_pending = true;
			break;
		}
	}

	/* 6. Write header after recording stops or metadata changes */
	if (hdr_write_pending && mlr_page_ring.r == mlr_page_ring.w && !erase_hold_off(false)) {
		write_track_header(hdr_write_track, hdr_write_start_playback);
		hdr_write_pending = false;
		hdr_write_start_playback = false;
		mlr_flushing = false;
	}

	if (copy_pending && !hdr_write_pending && !clear_pending &&
	    rec_track_idx < 0 && mlr_page_ring.r == mlr_page_ring.w) {
		copy_track_task();
	}

	/* 7. No header write pending: flushing ends once page ring is drained */
	if (!hdr_write_pending && !copy_pending && copy_dst_track < 0 &&
	    mlr_flushing && rec_track_idx < 0 &&
	    mlr_page_ring.r == mlr_page_ring.w &&
	    hdr_rewrite_pending_mask == 0) {
		mlr_flushing = false;
	}

	if (scene_save_requested && !hdr_write_pending && !clear_pending &&
	    !copy_pending && copy_dst_track < 0 &&
	    rec_track_idx < 0 && mlr_page_ring.r == mlr_page_ring.w) {
		scene_save_requested = false;
		scene_save_bytes = scene_serialize();
		if (scene_save_bytes == 0) {
			finish_scene_save();
		} else {
			scene_save_total_pages = (scene_save_bytes + MLR_PAGE_SIZE - 1) / MLR_PAGE_SIZE;
			scene_save_total_sectors = (scene_save_bytes + MLR_SECTOR_SIZE - 1) / MLR_SECTOR_SIZE;
			scene_save_sector = 0;
			scene_sector_erased = false;
			scene_page_idx = 0;
			__dmb();
			scene_save_pending = true;
		}
	}

	/* 8. Scene save — interleaved: erase one sector, then program one
	 *    256-byte page per io_task call.  Ring refills happen between
	 *    each page write, keeping audio fed even at high speeds. */
	if (scene_save_pending && !hdr_write_pending && !clear_pending &&
	    !copy_pending && copy_dst_track < 0 &&
	    rec_track_idx < 0 && mlr_page_ring.r == mlr_page_ring.w) {
		uint32_t sector_off = MLR_SCENE_OFFSET + (uint32_t)scene_save_sector * MLR_SECTOR_SIZE;

		if (!scene_sector_erased) {
			/* phase 1: erase the sector (~30 ms), once the rings can ride it out */
			if (erase_hold_off(false)) return;
			PERF_FLASH_ERASE(sector_off, MLR_SECTOR_SIZE);
			scene_sector_erased = true;
			scene_page_idx = 0;
		} else {
			/* phase 2: program one 256-byte page (~0.7ms) */
			uint32_t blob_off = (uint32_t)scene_save_sector * MLR_SECTOR_SIZE
			                  + (uint32_t)scene_page_idx * MLR_PAGE_SIZE;
			PERF_FLASH_PROGRAM(sector_off + (uint32_t)scene_page_idx * MLR_PAGE_SIZE,
				&scene_blob[blob_off], MLR_PAGE_SIZE);
			scene_page_idx++;

			uint32_t programmed_pages =
				(uint32_t)scene_save_sector * SCENE_PAGES_PER_SECTOR + (uint32_t)scene_page_idx;
			if (programmed_pages >= scene_save_total_pages) {
				finish_scene_save();
			} else if (scene_page_idx >= SCENE_PAGES_PER_SECTOR) {
				/* sector complete — advance to next */
				scene_save_sector++;
				scene_sector_erased = false;
				if ((uint32_t)scene_save_sector >= scene_save_total_sectors) {
					finish_scene_save();
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* 48 kHz -> 24 kHz conversion of tracks from firmware before 1.2.0   */
/* ------------------------------------------------------------------ */

/* Odd taps h[±1], h[±3], ... h[±29] of a 63-tap Blackman-windowed half-band
 * low-pass (Q15). The centre tap is 0.5 and the other even taps are zero.
 * Flat to 10 kHz, -6 dB at 12 kHz, below -60 dB from 14 kHz (at 48 kHz). */
#define CONV_HB_TAPS 15
#define CONV_HB_SPAN (2 * CONV_HB_TAPS - 1)  /* input samples each side of centre */
#define CONV_HIST    64                      /* power of two > 2 * CONV_HB_SPAN */

/* Runs once at boot, so keep it in flash rather than copied to RAM. */
#define CONV_FLASH_FUNC __attribute__((section(".flashdata.convert_text")))
static const int32_t __attribute__((section(".flashdata.convert_rodata"))) conv_hb_odd[CONV_HB_TAPS] = {
	10387, -3347, 1876, -1209, 818, -561, 381, -254,
	  164,  -102,   60,   -32,  16,  -6,   1,
};

static void CONV_FLASH_FUNC conv_flash_erase(uint32_t off)
{
	uint32_t irq = save_and_disable_interrupts();
	flash_range_erase(off, MLR_SECTOR_SIZE);
	restore_interrupts(irq);
}

static void CONV_FLASH_FUNC conv_flash_write_sector(uint32_t off, const uint8_t *data)
{
	uint32_t irq = save_and_disable_interrupts();
	flash_range_erase(off, MLR_SECTOR_SIZE);
	flash_range_program(off, data, MLR_SECTOR_SIZE);
	restore_interrupts(irq);
}

/* Same test mlr_init() uses to load a track, so every track that would play
 * gets converted. sample_count <= MLR_MAX_SAMPLES keeps reads in the track. */
static bool CONV_FLASH_FUNC conv_header_valid(const mlr_track_header_t *hdr)
{
	return hdr->magic == MLR_MAGIC &&
	       hdr->sample_count > 0 &&
	       hdr->sample_count <= MLR_MAX_SAMPLES &&
	       hdr->num_keyframes <= MLR_MAX_KEYFRAMES;
}

/* Decimate one track 2:1 in place. Output is half the size of the input, so
 * output sector j is only written once input decoding has moved well past
 * the end of it (input byte ~8192 * (j + 1) vs. sector end 4096 * (j + 1)). */
static void CONV_FLASH_FUNC convert_track_to_24k(int t, mlr_convert_progress_fn progress)
{
	const mlr_track_header_t *hdr =
		(const mlr_track_header_t *)(XIP_BASE + track_hdr_flash_off(t));
	const uint32_t in_count = hdr->sample_count;
	const uint32_t out_count = (in_count + 1) / 2;
	const uint32_t out_bytes = (out_count + 1) / 2;
	const uint32_t total_sectors = (out_bytes + MLR_SECTOR_SIZE - 1) / MLR_SECTOR_SIZE;
	const int8_t record_speed_shift = hdr->record_speed_shift;
	const uint8_t recorded_channel = hdr->recorded_channel;
	const uint8_t cv1_pitch_mode = hdr->cv1_pitch_mode;
	adpcm_state_t dec = {0, 0};
	if (hdr->num_keyframes > 0) {
		dec.predictor  = hdr->keyframes[0].ch[0].predictor;
		dec.step_index = (int8_t)adpcm_clamp_index(hdr->keyframes[0].ch[0].step_index);
	}

	/* Mark the track first, so a power cut mid-conversion clears it at the
	 * next boot instead of leaving half-converted audio. */
	memcpy(hdr_staging, hdr, MLR_SECTOR_SIZE);
	((mlr_track_header_t *)hdr_staging)->rate_mark = MLR_RATE_MARK_CONVERTING;
	conv_flash_write_sector(track_hdr_flash_off(t), hdr_staging);

	/* Boot-time scratch: the page ring and rec_keyframes are idle until
	 * recording starts, and mlr_init() clears the page ring afterwards. */
	uint8_t *out_sector = &mlr_page_ring.pages[0][0];
	_Static_assert(sizeof(mlr_page_ring.pages) >= MLR_SECTOR_SIZE,
		"page ring too small for conversion sector buffer");
	const uint8_t *in = track_audio_xip(t);
	const uint32_t audio_off = track_audio_flash_off(t);

	int16_t hist[CONV_HIST] = {0};
	adpcm_state_t enc = {0, 0};
	uint32_t num_kf = 1;
	rec_keyframes[0].ch[0].predictor  = 0;
	rec_keyframes[0].ch[0].step_index = 0;
	rec_keyframes[0].ch[0]._pad       = 0;

	uint32_t out_n = 0, out_fill = 0, sectors_done = 0;
	memset(out_sector, 0xFF, MLR_SECTOR_SIZE);
	if (progress) progress(t, 0, total_sectors);

	for (uint32_t i = 0; out_n < out_count; i++) {
		int16_t x = 0;
		if (i < in_count) {
			uint8_t b = in[i >> 1];
			x = adpcm_decode((i & 1) ? (b >> 4) : (b & 0x0F), &dec);
		}
		hist[i & (CONV_HIST - 1)] = x;

		/* Output m is centred on input 2m and needs CONV_HB_SPAN samples
		 * after it; inputs before 0 are zero. */
		if (i < CONV_HB_SPAN || ((i - CONV_HB_SPAN) & 1)) continue;
		uint32_t c = i - CONV_HB_SPAN;
		int32_t acc = (int32_t)hist[c & (CONV_HIST - 1)] * 16384;  /* centre tap 0.5 */
		for (uint32_t k = 0; k < CONV_HB_TAPS; k++) {
			uint32_t d = 2 * k + 1;
			int32_t before = (c >= d) ? hist[(c - d) & (CONV_HIST - 1)] : 0;
			int32_t after  = hist[(c + d) & (CONV_HIST - 1)];
			acc += conv_hb_odd[k] * (before + after);
		}
		int16_t y = adpcm_clamp16((acc + (1 << 14)) >> 15);

		uint8_t nyb = adpcm_encode(y, &enc);
		if (out_n & 1) out_sector[out_fill++] |= (uint8_t)(nyb << 4);
		else           out_sector[out_fill] = nyb;
		out_n++;

		/* Same keyframe rule as recording: state after every 1024 samples. */
		if ((out_n % MLR_KEYFRAME_INTERVAL) == 0 && num_kf < MLR_MAX_KEYFRAMES) {
			rec_keyframes[num_kf].ch[0].predictor  = enc.predictor;
			rec_keyframes[num_kf].ch[0].step_index = enc.step_index;
			rec_keyframes[num_kf].ch[0]._pad       = 0;
			num_kf++;
		}

		bool last = (out_n == out_count);
		if (last && (out_n & 1)) out_fill++;  /* keep the trailing low nybble */
		if (out_fill == MLR_SECTOR_SIZE || (last && out_fill > 0)) {
			conv_flash_write_sector(audio_off + sectors_done * MLR_SECTOR_SIZE, out_sector);
			sectors_done++;
			out_fill = 0;
			memset(out_sector, 0xFF, MLR_SECTOR_SIZE);
			if (progress) progress(t, sectors_done, total_sectors);
		}
	}

	memset(hdr_staging, 0xFF, MLR_SECTOR_SIZE);
	mlr_track_header_t *out_hdr = (mlr_track_header_t *)hdr_staging;
	out_hdr->magic              = MLR_MAGIC;
	out_hdr->sample_count       = out_count;
	out_hdr->adpcm_bytes        = out_bytes;
	out_hdr->num_keyframes      = num_kf;
	out_hdr->record_speed_shift = record_speed_shift;
	out_hdr->recorded_channel   = recorded_channel;
	out_hdr->rate_mark          = MLR_RATE_MARK_24K;
	out_hdr->cv1_pitch_mode     = cv1_pitch_mode;
	memcpy(out_hdr->keyframes, rec_keyframes, num_kf * sizeof(mlr_keyframe_channels_t));
	conv_flash_write_sector(track_hdr_flash_off(t), hdr_staging);
}

void CONV_FLASH_FUNC mlr_convert_legacy_tracks(mlr_convert_progress_fn progress)
{
	for (int t = 0; t < MLR_NUM_TRACKS; t++) {
		const mlr_track_header_t *hdr =
			(const mlr_track_header_t *)(XIP_BASE + track_hdr_flash_off(t));
		if (hdr->magic != MLR_MAGIC || hdr->rate_mark == MLR_RATE_MARK_24K)
			continue;
		if (hdr->rate_mark == MLR_RATE_MARK_CONVERTING) {
			conv_flash_erase(track_hdr_flash_off(t));  /* interrupted: clear the track */
			continue;
		}
		if (conv_header_valid(hdr))
			convert_track_to_24k(t, progress);
	}
}
