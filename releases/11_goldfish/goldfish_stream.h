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
 * Record one audio+CV frame. Called from core 0 at 48 kHz.
 *  audio: -32768..32767 (12-bit Goldfish audio may be pre-scaled by caller)
 *  cv:    -2048..2047   (12-bit)
 * Returns false once the recording region is full.
 */
bool goldfish_stream_record_sample(int16_t audio, int16_t cv);

/** Stop recording: flush partial encoder byte and partial pages. */
void goldfish_stream_record_stop(void);

/**
 * Core 1 service routine: drains staged pages to flash with erase-ahead.
 * Call frequently from the core 1 loop. Returns the number of pages written.
 */
uint32_t goldfish_stream_io_task(void);

/** True once all staged pages have been written to flash by core 1. */
bool goldfish_stream_io_idle(void);

/**
 * Persist the recording header (metadata + keyframe index) to flash so the
 * loop survives a power cycle. Runs on core 1; call only when io is idle.
 */
void goldfish_stream_persist(void);

/**
 * Load a previously persisted recording at boot. Returns true if a valid
 * header was found and the keyframe index restored.
 */
bool goldfish_stream_load(void);

/* ---- Read-back (random access) ---- */

/** Decode the audio sample at the given absolute index (0..recorded length). */
int16_t goldfish_stream_read_audio(uint32_t sample_index);

/** Read the CV value (sign-extended back to 12-bit) at the given audio index. */
int16_t goldfish_stream_read_cv(uint32_t sample_index);

/* ---- Introspection (geometry + instrumentation) ---- */

uint32_t goldfish_stream_flash_size(void);        /* detected total flash bytes */
uint32_t goldfish_stream_keyframe_interval(void); /* samples between keyframes  */
uint32_t goldfish_stream_capacity_samples(void);  /* max recordable audio samples */
uint32_t goldfish_stream_recorded_samples(void);  /* length of current recording */
uint32_t goldfish_stream_erase_count(void);       /* sectors erased since boot   */
float    goldfish_stream_capacity_seconds(void);  /* capacity_samples / 48000    */

#ifdef __cplusplus
}
#endif

#endif /* GOLDFISH_STREAM_H */
