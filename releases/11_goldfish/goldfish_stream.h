/**
 * goldfish_stream.h — flash-backed audio+CV streaming store for Goldfish 2.0.
 *
 * Milestone 1: the recording / flash-plumbing layer.
 *
 *   - Audio is IMA-ADPCM (4 bits/sample, ~4:1) written to a wear-levelled
 *     region of the program card's flash.
 *   - CV is stored raw: 8-bit, decimated to 12 kHz (GOLDFISH_CV_DECIM), in a
 *     parallel region, co-indexed to the audio timeline so a single sample
 *     index addresses both channels in sync.
 *   - Keyframes (encoder state snapshots) are captured every
 *     goldfish_stream_keyframe_interval() samples so any audio position can be
 *     reached by seeking to the nearest keyframe and decoding forward.  The
 *     interval scales with card size so the in-RAM index stays roughly constant
 *     (~GOLDFISH_KEYFRAME_BUDGET entries) regardless of 2 MB vs 16 MB flash.
 *
 * Threading model (wired up in later milestones):
 *   - Core 0 (audio): goldfish_stream_record_sample() — encode + enqueue.
 *   - Core 1 (flash I/O): goldfish_stream_io_task() — drain page ring to flash
 *     with sector erase-ahead.
 * Core 0's audio path must be fully RAM-resident so it never stalls on XIP
 * while core 1 is mid-erase.
 */

#ifndef GOLDFISH_STREAM_H
#define GOLDFISH_STREAM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Compile-time configuration                                         */
/* ------------------------------------------------------------------ */

/* Flash reserved for firmware at the bottom of the chip. Generous; the audio
 * region begins after this. Must be a multiple of the 4 KB sector size. */
#ifndef GOLDFISH_FIRMWARE_RESERVE
#define GOLDFISH_FIRMWARE_RESERVE (384u * 1024u)
#endif

/* Target maximum number of keyframe entries kept in RAM. The keyframe interval
 * is chosen at init so the actual count stays at or below this, bounding the
 * RAM index to ~GOLDFISH_KEYFRAME_BUDGET * 4 bytes (~32 KB at 8192). */
#ifndef GOLDFISH_KEYFRAME_BUDGET
#define GOLDFISH_KEYFRAME_BUDGET 8192u
#endif

/* Number of audio channels stored to flash (stereo: L, R). */
#ifndef GOLDFISH_AUDIO_CHANNELS
#define GOLDFISH_AUDIO_CHANNELS 2u
#endif

/* CV decimation: audio runs at 48 kHz; CV is stored every Nth sample (12 kHz). */
#ifndef GOLDFISH_CV_DECIM
#define GOLDFISH_CV_DECIM 4u
#endif

/* Flash program page size (bytes) staged before hand-off to core 1. */
#define GOLDFISH_PAGE_SIZE 256u

/* Number of staged pages in the core0->core1 ring. Sized to absorb worst-case
 * erase latency without the producer overrunning the consumer. */
#ifndef GOLDFISH_PAGE_RING_COUNT
#define GOLDFISH_PAGE_RING_COUNT 32u
#endif

/* Sectors the erase frontier leads the write head in continuous (DELAY) mode.
 * These sectors are pre-erased so pending pages always land in erased flash. */
#ifndef GOLDFISH_ERASE_LOOKAHEAD
#define GOLDFISH_ERASE_LOOKAHEAD 2u
#endif

#define GOLDFISH_STREAM_MAGIC 0x47324653u /* 'G2FS' */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * Detect flash size, compute the flash partition, and choose the keyframe
 * interval. Does not erase or write anything. Call once at boot (single-core).
 */
void goldfish_stream_init(void);

/** Begin a fresh recording (clears the current loop). Resets write cursor,
 *  encoder state and keyframe index. */
void goldfish_stream_record_start(void);

/**
 * Record one stereo audio + CV frame. Called from core 0 at the audio rate.
 *  left, right: -32768..32767 audio samples (L and R channels)
 *  cv:         -2048..2047 (12-bit), a single mono CV stream
 * Returns false once the recording region is full.
 */
bool goldfish_stream_record_sample(int16_t left, int16_t right, int16_t cv);

/** Stop recording: flush partial encoder byte and partial pages. */
void goldfish_stream_record_stop(void);

/** Begin continuous, wrapping record for the flash delay line (DELAY mode).
 *  Writes lap the whole audio region (distributing wear), and reads trail the
 *  write position via playback heads. */
void goldfish_stream_delay_start(void);

/**
 * Core 1 service routine: drains staged pages to flash with erase-ahead.
 * Call frequently from the core 1 loop. Returns the number of pages written.
 */
uint32_t goldfish_stream_io_task(void);

/** True once all staged pages have been written to flash by core 1. */
bool goldfish_stream_io_idle(void);

/* ---- Read-back (random access) ---- */

/** Read the CV value (sign-extended back to 12-bit) at the given audio index. */
int16_t goldfish_stream_read_cv(uint32_t sample_index);

/** Decode `count` PCM samples of `channel` from absolute sample `start` into
 *  `out`. Reads flash directly — only call when goldfish_stream_io_idle(). */
void goldfish_stream_decode_into(uint8_t channel, uint32_t start, uint32_t count, int16_t *out);

/* ---- Loop-boundary crossfade previews (decoded on core 1) ----
 * Length of each pre-decoded preview buffer (loop start / loop end), matched by
 * the PLAY crossfade window in main.cpp. */
#define GOLDFISH_PREVIEW_LEN 388u

/** Core 0: ask core 1 to (re)decode the loop start + end previews for a loop of
 *  `loop_len` samples. Clears the ready flag; poll goldfish_stream_previews_ready. */
void goldfish_stream_request_previews(uint32_t loop_len);

/** True once core 1 has filled the preview buffers for the last request. */
bool goldfish_stream_previews_ready(void);

/** Pointers to the pre-decoded loop-start / loop-end PCM (GOLDFISH_PREVIEW_LEN
 *  samples each). Valid only while goldfish_stream_previews_ready() is true. */
const int16_t *goldfish_stream_preview_start(uint8_t channel);
const int16_t *goldfish_stream_preview_end(uint8_t channel);

/** Core 0: ask core 1 to decode a seek/cut target (per channel start sample)
 *  into the seek buffers. Clears the ready flag; poll goldfish_stream_seek_ready. */
void goldfish_stream_request_seek(uint32_t startL, uint32_t startR);

/** True once core 1 has filled the seek buffers for the last seek request. */
bool goldfish_stream_seek_ready(void);

/** Pointer to the pre-decoded seek-target PCM (GOLDFISH_PREVIEW_LEN samples). */
const int16_t *goldfish_stream_seek_buf(uint8_t channel);

/* ---- Core-1-refilled playback head (forward + reverse, varispeed) ----
 *
 * A head is a decoded-PCM window kept filled by core 1 so that core 0 can read
 * any recently-visited sample in O(1) with no decode work on the audio thread.
 * This is required whenever recording is in progress (DELAY): core 0 must not
 * touch the flash bus during a core-1 erase, so all reads come from the ring.
 * Core 0 publishes the position it wants (req_pos); core 1 slides/refills the
 * window to keep it covered, seeking from a keyframe on large/backward jumps. */

#define GOLDFISH_RING_BITS 12u
#define GOLDFISH_RING_SZ   (1u << GOLDFISH_RING_BITS) /* 4096 samples, 8 KB */
#define GOLDFISH_RING_MASK (GOLDFISH_RING_SZ - 1u)

typedef struct {
	int16_t           pcm[GOLDFISH_RING_SZ]; /* decoded window, indexed by idx&MASK */
	volatile uint32_t req_pos;   /* core 0: sample index it is reading */
	volatile bool     active;    /* core 0: head in use this block */
	volatile uint32_t lo, hi;    /* core 1: valid window [lo, hi) */
	int16_t           last;      /* core 0: last good sample (underrun hold) */
	uint8_t           channel;   /* which audio channel (0 = L, 1 = R) this head reads */
	/* core 1 private forward-decode state */
	int16_t           predictor;
	int8_t            step_index;
	uint32_t          fill_next; /* next sample index core 1 will decode forward */
	bool              fwd_valid; /* forward decoder state matches fill_next */
	bool              need_seek; /* core 1 must re-seek before filling */
} goldfish_head_t;

/** Reset a playback head (core 0). */
void goldfish_stream_head_init(goldfish_head_t *h, uint8_t channel);

/** Read the decoded sample at sample_index from the head's ring (core 0). */
int16_t goldfish_stream_head_read(goldfish_head_t *h, uint32_t sample_index);

/** Read a CV value co-indexed with an audio sample position, via a core-1 ring. */
int16_t goldfish_stream_cv_read(uint32_t sample_index);

/* Register the heads/CV that core 1 should keep refilled. Pass NULL to disable
 * a slot. Call when entering a playback mode. */
void goldfish_stream_set_heads(goldfish_head_t *hL, goldfish_head_t *hR);



/* ---- Introspection (geometry) ---- */

uint32_t goldfish_stream_flash_size(void);        /* detected total flash bytes */
uint32_t goldfish_stream_keyframe_interval(void); /* samples between keyframes  */
uint32_t goldfish_stream_capacity_samples(void);  /* max recordable audio samples */
uint32_t goldfish_stream_recorded_samples(void);  /* length of current recording */
uint32_t goldfish_stream_write_index(void);       /* monotonic samples written (DELAY) */
uint32_t goldfish_stream_erase_count(void);       /* sectors erased since boot   */
float    goldfish_stream_capacity_seconds(void);  /* capacity_samples / 24000    */

#ifdef __cplusplus
}
#endif

#endif /* GOLDFISH_STREAM_H */
