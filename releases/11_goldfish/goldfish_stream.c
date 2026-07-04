/**
 * goldfish_stream.c — flash-backed audio+CV streaming store for Goldfish 2.0.
 * See goldfish_stream.h for the design overview. Milestone 1: record + flash
 * plumbing + read-back for integrity testing.
 */

#include "goldfish_stream.h"
#include "flash_size.h"
#include "adpcm.h"

#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE 4096u
#endif

/* ------------------------------------------------------------------ */
/* Keyframe + header layout                                           */
/* ------------------------------------------------------------------ */

typedef struct {
	int16_t predictor;
	int8_t  step_index;
	int8_t  _pad;
} goldfish_keyframe_t;

/* Fixed-size metadata prefix written to the header region of flash. It is
 * immediately followed in flash by num_keyframes goldfish_keyframe_t entries. */
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t sample_count;      /* recorded length, in audio samples */
	uint32_t keyframe_interval; /* samples between keyframes */
	uint32_t num_keyframes;
	uint32_t cv_decim;
	uint32_t audio_off;         /* geometry echo (sanity check on load) */
	uint32_t cv_off;
} goldfish_stream_hdr_t;

/* ------------------------------------------------------------------ */
/* Page ring (core 0 producer -> core 1 consumer, single each)        */
/* ------------------------------------------------------------------ */

typedef struct {
	uint32_t flash_off;               /* absolute flash offset to program */
	uint8_t  data[GOLDFISH_PAGE_SIZE];
} goldfish_page_t;

static goldfish_page_t   s_page_ring[GOLDFISH_PAGE_RING_COUNT];
static volatile uint32_t s_page_w; /* producer index (core 0) */
static volatile uint32_t s_page_r; /* consumer index (core 1) */

/* ------------------------------------------------------------------ */
/* Module state                                                       */
/* ------------------------------------------------------------------ */

static goldfish_keyframe_t s_keyframes[GOLDFISH_KEYFRAME_BUDGET];
static uint32_t            s_num_keyframes;

/* Geometry (computed in init) */
static uint32_t s_flash_size;
static uint32_t s_header_off;
static uint32_t s_header_size;
static uint32_t s_audio_off;
static uint32_t s_audio_bytes;
static uint32_t s_cv_off;
static uint32_t s_cv_bytes;
static uint32_t s_capacity_samples;
static uint32_t s_keyframe_interval;

/* Record state (core 0) */
static bool         s_rec_active;
static uint32_t     s_write_index;      /* audio samples written so far */
static adpcm_state_t s_enc;
static uint8_t      s_cur_byte;
static bool         s_nybble_phase;     /* false = expecting low nybble */
static uint8_t      s_audio_page[GOLDFISH_PAGE_SIZE];
static uint32_t     s_audio_fill;
static uint32_t     s_audio_write_off;  /* next flash offset for audio page */
static uint8_t      s_cv_page[GOLDFISH_PAGE_SIZE];
static uint32_t     s_cv_fill;
static uint32_t     s_cv_write_off;     /* next flash offset for cv page */
static uint32_t     s_recorded_samples;

/* Core 1 erase-ahead watermarks and instrumentation */
static uint32_t          s_audio_next_erase;
static uint32_t          s_cv_next_erase;
static volatile uint32_t s_erase_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1u); }
static inline uint32_t align_up(uint32_t v, uint32_t a)   { return (v + a - 1u) & ~(a - 1u); }

static inline uint32_t next_pow2(uint32_t v)
{
	uint32_t p = 1u;
	while (p < v) p <<= 1;
	return p;
}

static inline const uint8_t *xip_ptr(uint32_t flash_off)
{
	return (const uint8_t *)(XIP_BASE + flash_off);
}

/* Erase one sector / program one page with interrupts masked on the calling
 * (core 1) core. Correctness relies on core 0 being fully RAM-resident so it
 * never touches XIP during these windows. */
static void flash_erase_sector(uint32_t off)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(off, FLASH_SECTOR_SIZE);
	restore_interrupts(ints);
	s_erase_count++;
}

static void flash_program_page(uint32_t off, const uint8_t *data)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_program(off, data, GOLDFISH_PAGE_SIZE);
	restore_interrupts(ints);
}

/* Enqueue a full page for core 1. If the ring is full the frame is dropped
 * (an overrun; should not happen when core 1 keeps up). */
static void enqueue_page(uint32_t flash_off, const uint8_t *data)
{
	uint32_t w = s_page_w;
	if (w - s_page_r >= GOLDFISH_PAGE_RING_COUNT) {
		return; /* overrun */
	}
	uint32_t slot = w & (GOLDFISH_PAGE_RING_COUNT - 1u);
	s_page_ring[slot].flash_off = flash_off;
	memcpy(s_page_ring[slot].data, data, GOLDFISH_PAGE_SIZE);
	__dmb();
	s_page_w = w + 1u;
}

/* ------------------------------------------------------------------ */
/* Init / geometry                                                    */
/* ------------------------------------------------------------------ */

void goldfish_stream_init(void)
{
	s_flash_size = goldfish_detect_flash_size();

	uint32_t usable = (s_flash_size > GOLDFISH_FIRMWARE_RESERVE)
	                      ? (s_flash_size - GOLDFISH_FIRMWARE_RESERVE)
	                      : 0u;

	/* Header holds the fixed metadata plus up to GOLDFISH_KEYFRAME_BUDGET
	 * keyframe entries. */
	uint32_t header_bytes = sizeof(goldfish_stream_hdr_t)
	                        + GOLDFISH_KEYFRAME_BUDGET * sizeof(goldfish_keyframe_t);
	s_header_size = align_up(header_bytes, FLASH_SECTOR_SIZE);
	s_header_off  = GOLDFISH_FIRMWARE_RESERVE;

	uint32_t remaining = (usable > s_header_size) ? (usable - s_header_size) : 0u;

	/* Audio ADPCM (24 KB/s) and raw CV (12 KB/s) share a 2:1 byte budget so
	 * both channels run out of space at the same recording time. */
	s_audio_bytes = align_down((remaining * 2u) / 3u, FLASH_SECTOR_SIZE);
	s_cv_bytes    = align_down(remaining - s_audio_bytes, FLASH_SECTOR_SIZE);

	s_audio_off = s_header_off + s_header_size;
	s_cv_off    = s_audio_off + s_audio_bytes;

	/* Capacity is whichever channel bounds first. Audio: 2 samples/byte.
	 * CV: GOLDFISH_CV_DECIM audio samples per stored byte. */
	uint32_t cap_audio = s_audio_bytes * 2u;
	uint32_t cap_cv    = s_cv_bytes * GOLDFISH_CV_DECIM;
	s_capacity_samples = (cap_audio < cap_cv) ? cap_audio : cap_cv;

	/* Choose the smallest power-of-two interval that keeps the keyframe count
	 * within budget. Power-of-two keeps keyframe indexing a shift. */
	uint32_t need = (s_capacity_samples + GOLDFISH_KEYFRAME_BUDGET - 1u)
	                / GOLDFISH_KEYFRAME_BUDGET;
	s_keyframe_interval = next_pow2(need < 256u ? 256u : need);

	s_num_keyframes    = 0u;
	s_recorded_samples = 0u;
	s_rec_active       = false;
	s_page_w = s_page_r = 0u;
	s_erase_count = 0u;
}

/* ------------------------------------------------------------------ */
/* Record path (core 0)                                               */
/* ------------------------------------------------------------------ */

void goldfish_stream_record_start(void)
{
	s_rec_active   = true;
	s_write_index  = 0u;
	s_enc.predictor  = 0;
	s_enc.step_index = 0;
	s_cur_byte     = 0u;
	s_nybble_phase = false;
	s_audio_fill   = 0u;
	s_cv_fill      = 0u;
	s_audio_write_off = s_audio_off;
	s_cv_write_off    = s_cv_off;
	s_num_keyframes   = 0u;

	/* Erase-ahead starts at the base of each region. */
	s_audio_next_erase = s_audio_off;
	s_cv_next_erase    = s_cv_off;
}

bool goldfish_stream_record_sample(int16_t audio, int16_t cv)
{
	if (!s_rec_active) return false;
	if (s_write_index >= s_capacity_samples) {
		return false; /* region full */
	}

	/* Capture keyframe (encoder state *before* encoding this sample) at each
	 * interval boundary, so a decoder can restart from it. */
	if ((s_write_index & (s_keyframe_interval - 1u)) == 0u
	    && s_num_keyframes < GOLDFISH_KEYFRAME_BUDGET) {
		s_keyframes[s_num_keyframes].predictor  = s_enc.predictor;
		s_keyframes[s_num_keyframes].step_index = s_enc.step_index;
		s_keyframes[s_num_keyframes]._pad       = 0;
		s_num_keyframes++;
	}

	/* Encode audio nybble, pack two per byte. */
	uint8_t nyb = adpcm_encode(audio, &s_enc);
	if (!s_nybble_phase) {
		s_cur_byte = nyb;
		s_nybble_phase = true;
	} else {
		s_cur_byte |= (uint8_t)(nyb << 4);
		s_nybble_phase = false;
		s_audio_page[s_audio_fill++] = s_cur_byte;
		if (s_audio_fill == GOLDFISH_PAGE_SIZE) {
			enqueue_page(s_audio_write_off, s_audio_page);
			s_audio_write_off += GOLDFISH_PAGE_SIZE;
			s_audio_fill = 0u;
		}
	}

	/* Store one decimated 8-bit CV byte per GOLDFISH_CV_DECIM audio samples. */
	if ((s_write_index % GOLDFISH_CV_DECIM) == 0u) {
		int16_t c = cv;
		if (c > 2047) c = 2047;
		if (c < -2048) c = -2048;
		s_cv_page[s_cv_fill++] = (uint8_t)(int8_t)(c >> 4);
		if (s_cv_fill == GOLDFISH_PAGE_SIZE) {
			enqueue_page(s_cv_write_off, s_cv_page);
			s_cv_write_off += GOLDFISH_PAGE_SIZE;
			s_cv_fill = 0u;
		}
	}

	s_write_index++;
	return true;
}

void goldfish_stream_record_stop(void)
{
	if (!s_rec_active) return;

	/* Flush a dangling nybble (odd sample count) into a full byte. */
	if (s_nybble_phase) {
		s_audio_page[s_audio_fill++] = s_cur_byte;
		s_nybble_phase = false;
		if (s_audio_fill == GOLDFISH_PAGE_SIZE) {
			enqueue_page(s_audio_write_off, s_audio_page);
			s_audio_write_off += GOLDFISH_PAGE_SIZE;
			s_audio_fill = 0u;
		}
	}

	/* Flush partial audio page (pad to a full program page). */
	if (s_audio_fill > 0u) {
		memset(s_audio_page + s_audio_fill, 0, GOLDFISH_PAGE_SIZE - s_audio_fill);
		enqueue_page(s_audio_write_off, s_audio_page);
		s_audio_write_off += GOLDFISH_PAGE_SIZE;
		s_audio_fill = 0u;
	}

	/* Flush partial CV page. */
	if (s_cv_fill > 0u) {
		memset(s_cv_page + s_cv_fill, 0, GOLDFISH_PAGE_SIZE - s_cv_fill);
		enqueue_page(s_cv_write_off, s_cv_page);
		s_cv_write_off += GOLDFISH_PAGE_SIZE;
		s_cv_fill = 0u;
	}

	s_recorded_samples = s_write_index;
	s_rec_active = false;
}

/* ------------------------------------------------------------------ */
/* Core 1 flash I/O                                                   */
/* ------------------------------------------------------------------ */

uint32_t goldfish_stream_io_task(void)
{
	uint32_t written = 0u;

	while (s_page_r != s_page_w) {
		uint32_t slot = s_page_r & (GOLDFISH_PAGE_RING_COUNT - 1u);
		uint32_t off  = s_page_ring[slot].flash_off;

		/* Erase-ahead: erase each sector just before the first page lands in
		 * it. Writes within a region are strictly sequential. */
		if (off >= s_audio_off && off < s_cv_off) {
			while (off >= s_audio_next_erase) {
				flash_erase_sector(s_audio_next_erase);
				s_audio_next_erase += FLASH_SECTOR_SIZE;
			}
		} else if (off >= s_cv_off && off < s_cv_off + s_cv_bytes) {
			while (off >= s_cv_next_erase) {
				flash_erase_sector(s_cv_next_erase);
				s_cv_next_erase += FLASH_SECTOR_SIZE;
			}
		}

		flash_program_page(off, s_page_ring[slot].data);

		__dmb();
		s_page_r++;
		written++;
	}

	return written;
}

bool goldfish_stream_io_idle(void)
{
	return s_page_r == s_page_w;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                        */
/* ------------------------------------------------------------------ */

void goldfish_stream_persist(void)
{
	/* Build the header region in a scratch buffer one page at a time. The
	 * header region is erased then programmed sequentially. */
	uint32_t off = s_header_off;

	/* Erase the whole header region. */
	for (uint32_t s = 0u; s < s_header_size; s += FLASH_SECTOR_SIZE) {
		flash_erase_sector(s_header_off + s);
	}

	goldfish_stream_hdr_t hdr;
	hdr.magic             = GOLDFISH_STREAM_MAGIC;
	hdr.version           = 2u;
	hdr.sample_count      = s_recorded_samples;
	hdr.keyframe_interval = s_keyframe_interval;
	hdr.num_keyframes     = s_num_keyframes;
	hdr.cv_decim          = GOLDFISH_CV_DECIM;
	hdr.audio_off         = s_audio_off;
	hdr.cv_off            = s_cv_off;

	/* Program the header + keyframe array as contiguous 256-byte pages. */
	uint8_t page[GOLDFISH_PAGE_SIZE];
	const uint8_t *hdr_bytes = (const uint8_t *)&hdr;
	const uint8_t *kf_bytes  = (const uint8_t *)s_keyframes;
	uint32_t hdr_len = sizeof(hdr);
	uint32_t kf_len  = s_num_keyframes * sizeof(goldfish_keyframe_t);
	uint32_t total   = hdr_len + kf_len;

	uint32_t pos = 0u;
	while (pos < total) {
		uint32_t n = 0u;
		for (; n < GOLDFISH_PAGE_SIZE && pos < total; n++, pos++) {
			page[n] = (pos < hdr_len) ? hdr_bytes[pos] : kf_bytes[pos - hdr_len];
		}
		if (n < GOLDFISH_PAGE_SIZE) {
			memset(page + n, 0, GOLDFISH_PAGE_SIZE - n);
		}
		flash_program_page(off, page);
		off += GOLDFISH_PAGE_SIZE;
	}
}

bool goldfish_stream_load(void)
{
	const goldfish_stream_hdr_t *hdr =
	    (const goldfish_stream_hdr_t *)xip_ptr(s_header_off);

	if (hdr->magic != GOLDFISH_STREAM_MAGIC) return false;
	if (hdr->keyframe_interval != s_keyframe_interval) return false;
	if (hdr->audio_off != s_audio_off || hdr->cv_off != s_cv_off) return false;
	if (hdr->num_keyframes > GOLDFISH_KEYFRAME_BUDGET) return false;

	s_recorded_samples = hdr->sample_count;
	s_num_keyframes    = hdr->num_keyframes;

	const goldfish_keyframe_t *kf =
	    (const goldfish_keyframe_t *)xip_ptr(s_header_off + sizeof(goldfish_stream_hdr_t));
	memcpy(s_keyframes, kf, s_num_keyframes * sizeof(goldfish_keyframe_t));

	return true;
}

/* ------------------------------------------------------------------ */
/* Read-back (random access)                                          */
/* ------------------------------------------------------------------ */

int16_t goldfish_stream_read_audio(uint32_t sample_index)
{
	if (sample_index >= s_recorded_samples) return 0;

	uint32_t k = sample_index / s_keyframe_interval;
	if (k >= s_num_keyframes) k = s_num_keyframes ? (s_num_keyframes - 1u) : 0u;

	adpcm_state_t st;
	st.predictor  = s_keyframes[k].predictor;
	st.step_index = s_keyframes[k].step_index;

	uint32_t start = k * s_keyframe_interval;
	const uint8_t *base = xip_ptr(s_audio_off);

	int16_t sample = st.predictor;
	for (uint32_t i = start; i <= sample_index; i++) {
		uint8_t byte = base[i >> 1];
		uint8_t nyb  = (i & 1u) ? (uint8_t)((byte >> 4) & 0x0Fu)
		                        : (uint8_t)(byte & 0x0Fu);
		sample = adpcm_decode(nyb, &st);
	}
	return sample;
}

int16_t goldfish_stream_read_cv(uint32_t sample_index)
{
	if (sample_index >= s_recorded_samples) return 0;
	uint32_t cv_index = sample_index / GOLDFISH_CV_DECIM;
	const int8_t *base = (const int8_t *)xip_ptr(s_cv_off);
	return (int16_t)((int16_t)base[cv_index] << 4);
}

/* ------------------------------------------------------------------ */
/* Introspection                                                      */
/* ------------------------------------------------------------------ */

uint32_t goldfish_stream_flash_size(void)        { return s_flash_size; }
uint32_t goldfish_stream_keyframe_interval(void) { return s_keyframe_interval; }
uint32_t goldfish_stream_capacity_samples(void)  { return s_capacity_samples; }
uint32_t goldfish_stream_recorded_samples(void)  { return s_recorded_samples; }
uint32_t goldfish_stream_erase_count(void)       { return s_erase_count; }
float    goldfish_stream_capacity_seconds(void)  { return s_capacity_samples / 48000.0f; }
