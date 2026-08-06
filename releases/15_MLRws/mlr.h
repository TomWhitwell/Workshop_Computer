/**
 * mlr.h — MLR track engine v2: ring-buffer streaming playback + JIT flash.
 *
 * Playback always reads from per-track RAM ring buffers (never flash).
 * Core 1 refills rings from flash XIP and handles recording writes.
 * Recording streams ADPCM pages to flash just-in-time with sector
 * erase-ahead, so there is no upfront delay and no flush on stop.
 *
 * Core 0: encode samples, decode from ring buffers (RAM only).
 * Core 1: refill rings from flash, erase/write during recording.
 */

#ifndef MLR_H
#define MLR_H

#include <stdint.h>
#include <stdbool.h>
#include "adpcm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define MLR_NUM_TRACKS         6
#define MLR_GRID_COLS          16

/* Number of audio channels stored per track. Tracks are mono and can be
 * routed independently to either output in grid mode. */
#define MLR_NUM_CHANNELS       1

/* Flash layout — 2 MB total.
 * Firmware reserve and track size are set from CMake so the build can
 * validate the flash partition and fail if the program grows too large. */
#ifndef MLR_FIRMWARE_RESERVE
#define MLR_FIRMWARE_RESERVE   (160 * 1024)
#endif
#ifndef MLR_TRACK_FLASH_SIZE
#define MLR_TRACK_FLASH_SIZE   (312 * 1024)
#endif
#define MLR_HEADER_SIZE        4096               /* first sector = header+kf */
#define MLR_AUDIO_SIZE         (MLR_TRACK_FLASH_SIZE - MLR_HEADER_SIZE)
#define MLR_TRACK_OFFSET(t)    (MLR_FIRMWARE_RESERVE + (t) * MLR_TRACK_FLASH_SIZE)

/* ADPCM: 2 nybbles per byte, so each byte stores 2 mono samples. */
#define MLR_MAX_SAMPLES        (MLR_AUDIO_SIZE * 2)

/* Keyframes every N sample-frames for instant seeking */
#define MLR_KEYFRAME_INTERVAL  1024
#define MLR_MAX_KEYFRAMES      ((MLR_MAX_SAMPLES / MLR_KEYFRAME_INTERVAL) + 1)

/* Per-track playback ring buffer (decoded PCM, consumed by core 0) */
#define MLR_RING_SAMPLES       8192  /* frames (~171ms at 48kHz mono); power-of-two keeps ring indexing cheap */
#define MLR_DECLICK_SHIFT      5
#define MLR_DECLICK_SAMPLES    (1u << MLR_DECLICK_SHIFT)  /* 32-sample crossfade */
#define MLR_FADE_SAMPLES       120                        /* 2.5ms V-fade samples (total 5ms) */
#define MLR_SEEK_PREVIEW_SAMPLES 256                      /* CUT-page overlap crossfade bridge */
#define MLR_SEEK_PRIME_SAMPLES (MLR_KEYFRAME_INTERVAL * 2)  /* post-seek refill to cover direction changes */
#define MLR_PERF_UI_SECTIONS   8

#define MLR_TRANS_HANDOFF 0x01u
#define MLR_TRANS_FADE    0x02u
#define MLR_TRANS_XFADE   0x04u
#define MLR_TRANS_WRAP    0x08u

/* Recording page ring (ADPCM pages, core 0 fills, core 1 writes) */
#define MLR_PAGE_SIZE          256
#define MLR_PAGE_RING_COUNT    32

/* Flash sector size */
#ifndef MLR_SECTOR_SIZE
#define MLR_SECTOR_SIZE        4096
#endif

/* Track header magic. v4 added record_speed_shift field. */
#define MLR_MAGIC              0x4D4C5234  /* 'MLR4' — mono ADPCM v2 */
#define MLR_CV1_PITCH_ENABLED_MODE   0u
#define MLR_CV1_PITCH_DISABLED_MODE  1u

/* ------------------------------------------------------------------ */
/* Pattern / recall / scene constants                                 */
/* ------------------------------------------------------------------ */

#define MLR_NUM_PATTERNS       4
#define MLR_NUM_RECALLS        4
#define MLR_PATTERN_MAX_EVENTS 64

/* Event types stored in pattern/recall event structs */
#define MLR_EVT_CUT            1
#define MLR_EVT_STOP           2
#define MLR_EVT_START          3
#define MLR_EVT_SPEED          4
#define MLR_EVT_REVERSE        5
#define MLR_EVT_LOOP           6
#define MLR_EVT_LOOP_CLR       7
#define MLR_EVT_VOLUME         8
#define MLR_EVT_PAT_PLAY      9   /* param: pattern index in track field */
#define MLR_EVT_PAT_STOP     10   /* param: pattern index in track field */
#define MLR_EVT_RECALL       11   /* param: recall slot in track field */
#define MLR_EVT_RECALL_UNDO  12   /* undo last recall */
#define MLR_EVT_MASTER       13   /* master volume: raw=((track<<8)|param_a), 0..4095 */
#define MLR_EVT_GROUP_CUT    14   /* CUT event broadcast to every member of track's group */
#define MLR_EVT_GROUP_LOOP   15   /* loop-a-section broadcast to every member of track's group */
#define MLR_EVT_GROUP_SPEED  16   /* speed change broadcast to every member of track's group */
#define MLR_EVT_GROUP_REVERSE 17  /* reverse change broadcast to every member of track's group */
#define MLR_EVT_GROUP_VOLUME 18   /* volume change broadcast to every member of track's group */
#define MLR_EVT_CHANNEL      19   /* per-track input/output channel: param_a=0/1 */
#define MLR_EVT_CUT_RELEASE  20   /* CUT key release for CV/pulse gate playback only */

/* Volume slots: 5 levels (cols 2–6 on REC page). Slot 1 = unity. */
#define MLR_NUM_VOL_SLOTS      5

/* Pattern states */
#define MLR_PAT_IDLE           0
#define MLR_PAT_RECORDING      1
#define MLR_PAT_PLAYING        2
#define MLR_PAT_STOPPED        3   /* has data, not playing */
#define MLR_PAT_ARMED          4   /* waiting for first event to start recording */

/* Scene flash layout — stored after the track area */
#define MLR_SCENE_OFFSET       (MLR_FIRMWARE_RESERVE + MLR_NUM_TRACKS * MLR_TRACK_FLASH_SIZE)
#define MLR_SCENE_MAGIC        0x4D4C5332  /* 'MLS2' */
#ifndef MLR_SCENE_SECTORS
#define MLR_SCENE_SECTORS      3
#endif



/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
	int16_t predictor;
	int8_t  step_index;
	uint8_t _pad;
} mlr_keyframe_t;

/* Per-channel keyframe wrapper; one channel remains for dual-mono tracks. */
typedef struct {
	mlr_keyframe_t ch[MLR_NUM_CHANNELS];
} mlr_keyframe_channels_t;

/* Stored in flash (first sector of each track) */
typedef struct {
	uint32_t              magic;
	uint32_t              sample_count;
	uint32_t              adpcm_bytes;
	uint32_t              num_keyframes;
	int8_t                record_speed_shift;  /* speed at time of recording */
	uint8_t               recorded_channel;    /* 0 = Audio1/Out1, 1 = Audio2/Out2 in dual-mono grid mode. */
	uint8_t               pan_class;           /* Reserved; kept for flash header compatibility. */
	uint8_t               cv1_pitch_mode;      /* 0/0xFF = update CV1 pitch on cuts, 1 = leave CV1 pitch unchanged. */
	mlr_keyframe_channels_t keyframes[MLR_MAX_KEYFRAMES];
} mlr_track_header_t;

#if defined(__cplusplus)
static_assert(sizeof(mlr_track_header_t) <= MLR_HEADER_SIZE,
	"mlr_track_header_t exceeds MLR_HEADER_SIZE; reduce track size or header contents");
#else
_Static_assert(sizeof(mlr_track_header_t) <= MLR_HEADER_SIZE,
	"mlr_track_header_t exceeds MLR_HEADER_SIZE; reduce track size or header contents");
#endif

/* ---- Pattern / recall event (8 bytes, tightly packed) ---- */
typedef struct {
	uint32_t       timestamp_ms;   /* ms since pattern record started   */
	uint8_t        type;           /* MLR_EVT_*                         */
	uint8_t        track;          /* 0–6                               */
	int8_t         param_a;        /* column (cut), speed_shift, loop_start, reverse */
	int8_t         param_b;        /* loop_end (only for MLR_EVT_LOOP)  */
} mlr_event_t;

/* ---- Timed pattern recorder / player ---- */
typedef struct {
	mlr_event_t    events[MLR_PATTERN_MAX_EVENTS];
	uint16_t       count;          /* number of recorded events          */
	uint16_t       play_idx;       /* next event to fire during playback */
	uint32_t       loop_len_ms;    /* total loop duration in ms          */
	uint32_t       rec_start_ms;   /* absolute time when recording began */
	uint32_t       play_start_ms;  /* absolute time when playback began  */
	uint8_t        state;          /* MLR_PAT_*                          */
	uint8_t        event_flash;    /* decremented by UI; >0 = darken LED */
} mlr_pattern_t;

/* ---- Recall (snapshot — instant replay, no timestamps) ---- */
typedef struct {
	mlr_event_t    events[MLR_PATTERN_MAX_EVENTS];
	uint16_t       count;
	bool           recording;
	bool           has_data;
} mlr_recall_t;

/* ---- Per-track saved state (for scene persistence) ---- */
typedef struct {
	int8_t         speed_shift;
	uint8_t        reverse;        /* bool stored as byte for alignment */
	int8_t         loop_col_start; /* -1 = no loop */
	int8_t         loop_col_end;
	uint8_t        volume_slot;    /* 0..4: mixer column index (grid cols 2..6); slot 1 = unity */
	uint8_t        _pad[3];        /* align to 8 bytes */
} mlr_track_scene_t;

/* Playback ring — decoded PCM, core 1 fills, core 0 reads */
typedef struct {
	int16_t  buf[MLR_RING_SAMPLES * MLR_NUM_CHANNELS];
	volatile uint32_t w;   /* write index in frames (core 1) */
	volatile uint32_t r;   /* read index in frames (core 0) */
} mlr_pcm_ring_t;

/* Recording page ring — ADPCM pages, core 0 fills, core 1 flushes */
typedef struct {
	uint8_t  pages[MLR_PAGE_RING_COUNT][MLR_PAGE_SIZE];
	volatile uint8_t w;    /* write slot (core 0) */
	volatile uint8_t r;    /* read slot (core 1) */
	uint32_t fill;         /* bytes in current page */
} mlr_page_ring_t;

/* Per-track state */
typedef struct {
	/* metadata (from flash header) */
	uint32_t       length_samples;
	uint32_t       length_bytes;
	bool           has_content;

	/* playback */
	bool           playing;
	uint32_t       playhead;           /* current sample in track */

	/* PCM ring (core 0 reads, core 1 writes) */
	mlr_pcm_ring_t pcm;

	/* core 1 decode state for ring fill */
	uint32_t       fill_byte_pos;      /* ADPCM byte offset in flash */
	bool           fill_high_nyb;
	adpcm_state_t  fill_decode[MLR_NUM_CHANNELS];
	uint32_t       fill_sample_pos;    /* frame position of fill head */
	volatile bool  fill_seek_pending;  /* set by cut, core 1 refills */

	/* seek target (set by core 0, consumed by core 1) */
	volatile uint32_t seek_target_sample;
	volatile bool     seek_reverse_pending;
	volatile bool     seek_reverse_target;
	volatile bool     seek_start_pending;

	/* seek handoff (set by core 1, consumed by core 0) */
	volatile uint32_t seek_handoff_r;
	volatile uint32_t seek_handoff_playhead;
	volatile uint16_t seek_handoff_speed_accum;
	volatile bool     seek_handoff_start_pending;
	volatile bool     seek_handoff_pending;
	volatile bool     seek_handoff_reverse_pending;
	volatile bool     seek_handoff_reverse_target;
	int16_t           seek_preview[MLR_SEEK_PREVIEW_SAMPLES * MLR_NUM_CHANNELS];
	volatile uint16_t seek_preview_count;
	uint16_t          seek_xfade_pos;
	bool              seek_xfade_active;
	volatile bool     wrap_preview_ready;
	bool              wrap_preview_reverse;
	uint32_t          wrap_preview_start;
	uint32_t          wrap_preview_end;
	uint16_t          wrap_preview_speed_frac;
	/* Per-sample skip threshold for maybe_start_wrap_preview_xfade(): set by
	 * Core 1 when wrap_preview_ready becomes true, read by Core 0 to skip the
	 * heavy check while the playhead is still far from the wrap boundary. */
	volatile uint32_t wrap_preview_arm_playhead;
	volatile uint8_t  transition_flags;

	/* loop-a-section: sub-loop boundaries (set by core 0) */
	bool           loop_active;
	uint32_t       loop_start_sample;   /* first sample of sub-loop */
	uint32_t       loop_end_sample;     /* one past last sample     */
	int            loop_col_start;      /* grid column (inclusive)  */
	int            loop_col_end;        /* grid column (inclusive)  */

	/* speed & reverse (set by core 0) */
	int8_t         speed_shift;         /* -3..+3: musical speed slot    */
	int8_t         record_speed_shift;  /* speed when track was recorded */
	uint16_t       speed_frac;          /* fixed 8.8: effective playback */
	uint16_t       speed_accum;         /* fractional accumulator        */
	int16_t        last_pcm[MLR_NUM_CHANNELS];  /* held sample for sub-1x speeds */
	int16_t        interp_prev[MLR_NUM_CHANNELS]; /* previous sample for interp */
	int16_t        last_out[MLR_NUM_CHANNELS];   /* last post-volume output sample */
	int16_t        out_hist[MLR_NUM_CHANNELS][MLR_DECLICK_SAMPLES]; /* recent output for overlap xfade */
	uint8_t        out_hist_pos;        /* next history slot to write */
	uint8_t        declick_hist_pos;    /* frozen history read start for active xfade */
	uint8_t        declick_count;       /* remaining crossfade samples */
	bool           stop_pending;        /* ramp to zero before hard stop */
	
	/* fade state */
	uint16_t       fade_out_count;
	uint16_t       fade_in_count;
	bool           fade_out_active;
	
	bool           reverse;             /* true = play backwards         */
	bool           muted;               /* true = skip mix, keep position */

	/* per-track volume (set by core 0) */
	uint8_t        volume_slot;         /* 0..4: mixer column index (grid cols 2..6); slot 1 = unity */
	uint16_t       volume_frac;         /* fixed 8.8: current (slewed)    */
	uint16_t       volume_target;       /* fixed 8.8: target from slot    */

	/* per-track input/output channel routing (set by core 0).
	 * 0 = Audio1/Out1, 1 = Audio2/Out2. Persisted in the track header. */
	uint8_t        recorded_channel;
	uint8_t        pan_class;            /* Reserved; kept for flash header compatibility. */
	bool           cv1_pitch_enabled;    /* true = cut keys on this row update CV1 pitch. */
	bool           channel_user_chosen;  /* RAM-only: true once the user has explicitly picked
	                                      * a channel on the grid; suppresses auto-detect override */
	uint8_t        pending_channel;      /* RAM-only: target channel queued behind a fade-out. */
	bool           channel_swap_pending; /* RAM-only: when true, fade-out completion applies
	                                      * pending_channel before starting fade-in. */

	/* keyframes (copied to RAM at boot) */
	uint32_t          num_keyframes;
	mlr_keyframe_channels_t keyframes[MLR_MAX_KEYFRAMES];
} mlr_track_t;

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

extern mlr_track_t     mlr_tracks[MLR_NUM_TRACKS];
extern volatile int    mlr_rec_track;
extern volatile bool   mlr_flushing;  /* true while header being written */
extern volatile bool   mlr_copying;   /* true while async track copy owns flash */
extern mlr_page_ring_t mlr_page_ring;
extern volatile uint16_t mlr_master_level_raw; /* 0..4095 master level (grid/pattern/scene controlled) */
extern volatile bool     mlr_master_override;   /* true = suppress pattern master events until loop wrap */

extern mlr_pattern_t   mlr_patterns[MLR_NUM_PATTERNS];
extern mlr_recall_t    mlr_recalls[MLR_NUM_RECALLS];
extern volatile bool   mlr_scene_saving;  /* true during flash scene write */
extern volatile int    mlr_recall_active;  /* currently selected recall (-1=none) */

/* Per-track group-membership bitmask (see mlr.c for invariant). */
extern uint8_t         mlr_track_groups[MLR_NUM_TRACKS];

/* Per-track gated-playback flag — toggled by the UI, persisted with the scene. */
extern bool            mlr_gate_mode[MLR_NUM_TRACKS];

#ifdef MLR_PERF_PROFILING
typedef struct {
	volatile uint32_t process_sample_count;
	volatile uint32_t pcm_ring_min[MLR_NUM_TRACKS];
	volatile uint32_t pcm_underruns[MLR_NUM_TRACKS];
	volatile uint32_t page_ring_max;
	volatile uint32_t seek_count[MLR_NUM_TRACKS];
	volatile uint32_t grid_frame_drops;
	volatile uint32_t grid_event_drops;
	volatile uint32_t grid_poll_events_max;
	volatile uint32_t grid_poll_backlog_max;
	volatile uint32_t grid_poll_backlog_last;
	volatile uint32_t rec_page_flush_count;
	volatile uint32_t scene_save_count;
	volatile uint32_t refill_max_us;
	volatile uint32_t seek_max_us;
	volatile uint32_t flash_erase_max_us;
	volatile uint32_t flash_program_max_us;
} mlr_perf_t;

extern volatile mlr_perf_t mlr_perf;
extern volatile uint32_t mlr_perf_reset_request;
extern volatile uint32_t mlr_perf_process_sample_count;
extern volatile uint32_t mlr_perf_process_sample_last_us;
extern volatile uint32_t mlr_perf_process_sample_max_us;
extern volatile uint32_t mlr_perf_process_sample_overruns;
extern volatile uint32_t mlr_perf_process_sample_ui_max_us;
extern volatile uint32_t mlr_perf_process_sample_ui_overruns;
extern volatile uint32_t mlr_perf_process_sample_audio_max_us;
extern volatile uint32_t mlr_perf_process_sample_audio_overruns;
extern volatile uint32_t mlr_perf_ui_section_last_us[MLR_PERF_UI_SECTIONS];
extern volatile uint32_t mlr_perf_ui_section_max_us[MLR_PERF_UI_SECTIONS];
extern volatile uint32_t mlr_perf_ui_section_overruns[MLR_PERF_UI_SECTIONS];
extern volatile uint32_t mlr_perf_pcm_ring_avail[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_pcm_ring_min[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_pcm_underruns[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_seek_underruns[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_reverse_toggle_avail[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_reverse_handoff_avail[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_reverse_handoff_count[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_page_ring_max;
extern volatile uint32_t mlr_perf_seek_count[MLR_NUM_TRACKS];
extern volatile uint32_t mlr_perf_grid_frame_drops;
extern volatile uint32_t mlr_perf_grid_event_drops;
extern volatile uint32_t mlr_perf_grid_poll_events_last;
extern volatile uint32_t mlr_perf_grid_poll_events_max;
extern volatile uint32_t mlr_perf_grid_poll_backlog_last;
extern volatile uint32_t mlr_perf_grid_poll_backlog_max;
extern volatile uint32_t mlr_perf_rec_page_flush_count;
extern volatile uint32_t mlr_perf_scene_save_count;
extern volatile uint32_t mlr_perf_refill_max_us;
extern volatile uint32_t mlr_perf_seek_max_us;
extern volatile uint32_t mlr_perf_flash_erase_max_us;
extern volatile uint32_t mlr_perf_flash_program_max_us;
extern volatile uint32_t mlr_perf_adc_mux_resets;
extern volatile uint32_t mlr_perf_adc_fifo_level_max;
void mlr_perf_reset(void);
void mlr_perf_count_grid_frame_drop(void);
void mlr_perf_count_monome_ws_event_drop(void);
void mlr_perf_note_grid_poll(uint32_t processed, uint32_t backlog_before, uint32_t backlog_after);
void mlr_perf_count_process_sample(void);
void mlr_perf_note_process_sample_us(uint32_t elapsed_us, bool ui_tick);
void mlr_perf_note_ui_section_us(uint32_t section, uint32_t elapsed_us);
void mlr_perf_count_adc_mux_reset(void);
void mlr_perf_note_adc_fifo_level(uint32_t level);
#endif

/* ------------------------------------------------------------------ */
/* Core 0 API                                                         */
/* ------------------------------------------------------------------ */

void     mlr_init(void);
void     mlr_rescan_track(int track);  /* re-read header from flash (call only when track is stopped) */
void     mlr_start_record(int track);
void     mlr_record_sample(int16_t sample);  /* mono: single sample */
void     mlr_stop_record(void);
int16_t  mlr_play_mix(uint8_t volume);
int16_t  mlr_play_mix_dual(int16_t *out_right);
void     mlr_cut(int track, int column);
void     mlr_cut_sample(int track, uint32_t sample_pos);
void     mlr_stop_track(int track);
void     mlr_clear_track(int track);
bool     mlr_rewrite_track_header(int track);  /* persist header-only metadata changes */
uint8_t  mlr_copy_track_mask(int src_track, uint8_t dst_mask);
void     mlr_set_loop(int track, int col_start, int col_end);
void     mlr_clear_loop(int track);
void     mlr_set_speed(int track, int speed_shift);
void     mlr_set_speed_frac(int track, uint16_t speed_frac); /* fixed 8.8, clamped to 64..1024 */
void     mlr_set_speed_frac_nondeclick(int track, uint16_t speed_frac); /* fixed 8.8, clamped to 64..1024 */
void     mlr_set_reverse(int track, bool reverse);
void     mlr_set_volume(int track, int slot);
int      mlr_get_column(int track);
void     mlr_get_loop_cols(int track, int *col_start, int *col_end);
uint8_t  mlr_consume_wrap_events(void);
uint32_t mlr_get_rec_progress(void);
int      mlr_get_flush_track(void);

/* ---- Track groups ---- */
void     mlr_groups_default(void);              /* reset every track to solo */
void     mlr_leave_group(int track);            /* remove track from its group */

/* Group helpers used by gesture handlers and pattern replay. Play-column
 * actions broadcast. CUT-page handlers choose between choke and broadcast
 * behavior depending on how the grouped tracks were started. */
void     mlr_group_stop_track(int track);
void     mlr_choke_group_cut(int track, int column);
void     mlr_choke_group_resume(int track, int column);
void     mlr_set_recorded_channel(int track, uint8_t channel);
void     mlr_choke_group_set_loop(int track, int col_start, int col_end);

/* ---- Pattern engine (core 0) ---- */
void     mlr_clock_set_ms(uint32_t now_ms);        /* sample-derived core-0 clock */
void     mlr_pattern_event(const mlr_event_t *e);  /* record into active patterns */
void     mlr_pattern_arm(int pat);                  /* arm: wait for first event to start recording */
void     mlr_pattern_rec_start(int pat);
void     mlr_pattern_rec_stop(int pat);
void     mlr_pattern_play_start(int pat);
void     mlr_pattern_play_stop(int pat);
void     mlr_pattern_clear(int pat);
void     mlr_pattern_tick(uint32_t now_ms);         /* call at pattern playback tick rate */
void     mlr_recall_check_active_match(void);       /* dim active recall if restored playheads drift */

#define MLR_PLAYBACK_SOURCE_NONE    0
#define MLR_PLAYBACK_SOURCE_PATTERN 1
#define MLR_PLAYBACK_SOURCE_RECALL  2
extern volatile uint8_t mlr_event_playback_source;

/* Called by mlr.c from event_exec() for every pattern/recall event that is
 * replayed. Implemented in main.cpp so the card can mirror events (cuts in
 * particular) to CV/pulse outputs. Keep it short — called from audio core. */
void     mlr_event_playback_hook(const mlr_event_t *e);

/* ---- Recall engine (core 0) ---- */
void     mlr_recall_event(const mlr_event_t *e);   /* record into armed recalls */
void     mlr_recall_arm(int slot);
void     mlr_recall_rec_stop(int slot);
void     mlr_recall_snapshot(int slot);  /* instant capture of all track state */
void     mlr_recall_exec(int slot);
void     mlr_recall_exec_and_record(int slot);  /* exec + feed events into pattern recorder */
void     mlr_recall_undo(void);          /* restore state from before last recall */
void     mlr_recall_undo_and_record(void);      /* undo + feed events into pattern recorder */
void     mlr_recall_task(void);          /* service one pending recall event */
void     mlr_recall_clear(int slot);

/* ---- Scene persistence ---- */
void     mlr_scene_load(void);      /* called from mlr_init() */
void     mlr_scene_save_start(void);/* triggers async save on core 1 */
void     mlr_scene_reset_params_to_defaults(void); /* reset current scene params without deleting audio/patterns/recalls */

/* ------------------------------------------------------------------ */
/* Core 1 API                                                         */
/* ------------------------------------------------------------------ */

/** Call frequently from core 1 loop. Refills playback rings,
 *  flushes recording pages, handles erase-ahead. */
void mlr_io_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MLR_H */
