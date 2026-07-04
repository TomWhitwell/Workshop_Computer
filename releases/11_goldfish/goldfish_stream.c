/**
 * goldfish_stream.c — flash-backed audio+CV streaming store for Goldfish 2.0.
 * See goldfish_stream.h for the design overview. Milestone 1: record + flash
 * plumbing + read-back for integrity testing.
 */

#include "goldfish_stream.h"
#include "flash_size.h"
#include "adpcm.h"

#include <string.h>
#include "pico/platform.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/ioqspi.h"

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

/* Per-channel audio state (index 0 = L, 1 = R). Both channels share the logical
 * timeline (write_index, keyframe grid, continuous/wrap); only the flash region,
 * ADPCM encoder, keyframe values and flush/erase cursors are per-channel. */
typedef struct {
	uint32_t            audio_off;      /* flash base of this channel's region */
	/* record encoder (core 0) */
	adpcm_state_t       enc;
	uint8_t             cur_byte;
	bool                nybble_phase;   /* false = expecting low nybble */
	uint8_t             page[GOLDFISH_PAGE_SIZE];
	uint32_t            fill;
	uint32_t            write_off;      /* next flash offset for an audio page */
	/* keyframes (core 0 writes; both cores read) */
	goldfish_keyframe_t keyframes[GOLDFISH_KEYFRAME_BUDGET];
	uint32_t            num_keyframes;
	/* core 1 erase-ahead + flush tracking */
	uint32_t            next_erase;
	volatile uint32_t   flushed_samples;
	uint32_t            pages_written;
} goldfish_audio_channel_t;

static goldfish_audio_channel_t s_ch[GOLDFISH_AUDIO_CHANNELS];

/* Geometry (computed in init) */
static uint32_t s_flash_size;
static uint32_t s_header_off;
static uint32_t s_header_size;
static uint32_t s_audio_bytes;       /* bytes per audio channel (both equal) */
static uint32_t s_cv_off;
static uint32_t s_cv_bytes;
static uint32_t s_capacity_samples;
static uint32_t s_keyframe_interval;
static uint32_t s_kf_slots;          /* keyframe slots = capacity/interval (ring in continuous mode) */
static bool     s_continuous;        /* DELAY: wrap region + never stop recording */

/* Record state (core 0), shared across channels */
static volatile bool s_rec_active;      /* cross-core: gates core1 erase-ahead */
static uint32_t     s_write_index;      /* audio samples written so far */
static uint8_t      s_cv_page[GOLDFISH_PAGE_SIZE];
static uint32_t     s_cv_fill;
static uint32_t     s_cv_write_off;     /* next flash offset for cv page */
static uint32_t     s_recorded_samples; /* readable length = min(channel flushed) in DELAY */

/* Core 1 erase-ahead watermark (CV) and counters */
static uint32_t          s_cv_next_erase;
static volatile uint32_t s_erase_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1u); }
static inline uint32_t align_up(uint32_t v, uint32_t a)   { return (v + a - 1u) & ~(a - 1u); }

/* Modular mapping of a logical position onto the (circular) flash regions. */
static inline uint32_t kf_slot(uint32_t k)          { return k % s_kf_slots; }
static inline uint32_t audio_byte_wrap(uint32_t b)  { return b % s_audio_bytes; }

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

/* Program one page with interrupts masked on the calling (core 1) core.
 * Correctness relies on core 0 being fully RAM-resident so it never touches
 * XIP during these windows. */
static void flash_program_page(uint32_t off, const uint8_t *data)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_program(off, data, GOLDFISH_PAGE_SIZE);
	restore_interrupts(ints);
}

/* Account one just-programmed audio page towards the flushed (readable) limit.
 * CV pages carry no keyframes and don't gate the audio heads, so are ignored. */
static inline void note_page_flushed(uint32_t off)
{
	for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) {
		if (off >= s_ch[c].audio_off && off < s_ch[c].audio_off + s_audio_bytes) {
			s_ch[c].pages_written++;
			s_ch[c].flushed_samples = s_ch[c].pages_written * (GOLDFISH_PAGE_SIZE * 2u);
			/* Readable limit = the least-flushed channel (a sample is only
			 * playable once BOTH channels have programmed it to flash). */
			if (s_continuous) {
				uint32_t lim = s_ch[0].flushed_samples;
				for (uint32_t d = 1u; d < GOLDFISH_AUDIO_CHANNELS; d++)
					if (s_ch[d].flushed_samples < lim) lim = s_ch[d].flushed_samples;
				s_recorded_samples = lim;
			}
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Low-level QSPI: erase-suspend + program-during-erase                */
/* ------------------------------------------------------------------ */
/*
 * A blocking sector erase freezes core 1 for tens of ms, during which no new
 * audio can be flushed and the playback heads cannot be refilled — the source
 * of DELAY-mode underruns. To reach zero underruns we instead:
 *   1. Erase one region "ahead" of the write head (erase sector M while the
 *      producer is still filling sector M-1), so pending pages always target
 *      already-erased sectors.
 *   2. During each sector erase, repeatedly SUSPEND the erase, program any
 *      pending pages (advancing the flushed frontier) and refill the heads
 *      via XIP, then RESUME. The flushed frontier therefore keeps advancing
 *      right through the erase, so a trailing read head never starves.
 *
 * This drives the flash controller directly (bypassing hardware/flash.h) so it
 * can issue Erase-Suspend (0x75) / Erase-Resume (0x7A). It runs only on core 1
 * with interrupts masked; correctness relies on core 0 being fully RAM-resident
 * (copy_to_ram) so it never touches XIP during these windows.
 */

/* Playback heads serviced by core 1 (registered via goldfish_stream_set_heads). */
static goldfish_head_t *s_head[2];
static void head_refill(goldfish_head_t *h);

typedef void (*flash_rom_fn)(void);
static flash_rom_fn s_rom_connect;   /* connect_internal_flash */
static flash_rom_fn s_rom_exit_xip;  /* flash_exit_xip         */
static flash_rom_fn s_rom_flush;     /* flash_flush_cache      */
static flash_rom_fn s_rom_enter_xip; /* flash_enter_cmd_xip    */

static void qspi_rom_init(void)
{
	s_rom_connect   = (flash_rom_fn)rom_func_lookup(rom_table_code('I', 'F'));
	s_rom_exit_xip  = (flash_rom_fn)rom_func_lookup(rom_table_code('E', 'X'));
	s_rom_flush     = (flash_rom_fn)rom_func_lookup(rom_table_code('F', 'C'));
	s_rom_enter_xip = (flash_rom_fn)rom_func_lookup(rom_table_code('C', 'X'));
}

/* Drive the QSPI chip-select via the pad override (SDK does the same). */
static void __not_in_flash_func(qspi_cs)(bool high)
{
	uint32_t v = high ? IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH
	                  : IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW;
	hw_write_masked(&ioqspi_hw->io[1].ctrl,
	                v << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
	                IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

/* One command-mode transaction over the SSI in single-bit (0x03-style) mode.
 * tx may be NULL (send zeros); rx may be NULL (discard). Mirrors the inner loop
 * of the SDK's flash_do_cmd, keeping <=14 bytes in flight. */
static void __not_in_flash_func(qspi_xfer)(const uint8_t *tx, uint8_t *rx, size_t n)
{
	qspi_cs(false);
	size_t tx_rem = n, rx_rem = n;
	while (tx_rem || rx_rem) {
		uint32_t sr = ssi_hw->sr;
		if ((sr & SSI_SR_TFNF_BITS) && tx_rem && (rx_rem - tx_rem) < 14u) {
			ssi_hw->dr0 = tx ? (uint32_t)*tx++ : 0u;
			--tx_rem;
		}
		if ((sr & SSI_SR_RFNE_BITS) && rx_rem) {
			uint8_t b = (uint8_t)ssi_hw->dr0;
			if (rx) *rx++ = b;
			--rx_rem;
		}
	}
	qspi_cs(true);
}

/* Read status register 1 (WIP = bit 0). */
static uint8_t __not_in_flash_func(qspi_status)(void)
{
	uint8_t tx[2] = { 0x05u, 0x00u };
	uint8_t rx[2] = { 0u, 0u };
	qspi_xfer(tx, rx, 2);
	return rx[1];
}

static void __not_in_flash_func(qspi_write_enable)(void)
{
	uint8_t c = 0x06u;
	qspi_xfer(&c, NULL, 1);
}

/* Program one 256-byte page in command mode (XIP must already be exited).
 * Blocks on WIP so the caller may safely resume an erase afterwards. */
static void __not_in_flash_func(qspi_program_page)(uint32_t off, const uint8_t *data)
{
	qspi_write_enable();
	uint8_t hdr[4] = { 0x02u, (uint8_t)(off >> 16), (uint8_t)(off >> 8), (uint8_t)off };
	uint32_t total = 4u + GOLDFISH_PAGE_SIZE;
	qspi_cs(false);
	uint32_t sent = 0u, got = 0u;
	while (sent < total || got < total) {
		uint32_t sr = ssi_hw->sr;
		if ((sr & SSI_SR_TFNF_BITS) && sent < total && (sent - got) < 14u) {
			uint8_t b = (sent < 4u) ? hdr[sent] : data[sent - 4u];
			ssi_hw->dr0 = (uint32_t)b;
			++sent;
		}
		if ((sr & SSI_SR_RFNE_BITS) && got < total) {
			(void)ssi_hw->dr0;
			++got;
		}
	}
	qspi_cs(true);
	while (qspi_status() & 0x01u) { /* WIP: page program in progress */ }
}

/* Erase one 4KB sector, suspending as needed to program pending pages (keeping
 * the flushed frontier advancing) and refill the heads. Interrupts masked. */
static void __not_in_flash_func(flash_erase_sector_suspend)(uint32_t off)
{
	uint32_t ints = save_and_disable_interrupts();
	s_rom_connect();
	s_rom_exit_xip();

	qspi_write_enable();
	uint8_t er[4] = { 0x20u, (uint8_t)(off >> 16), (uint8_t)(off >> 8), (uint8_t)off };
	qspi_xfer(er, NULL, 4);

	uint32_t guard = 0u;
	while (qspi_status() & 0x01u) {          /* WIP set: erase running */
		if (s_page_r != s_page_w) {
			/* Suspend so we can program a pending page. It targets a sector
			 * behind the erase frontier (already erased), so this is safe. */
			uint8_t sus = 0x75u;
			qspi_xfer(&sus, NULL, 1);
			busy_wait_us(20);                /* tSUS: ready for next command */

			uint32_t slot = s_page_r & (GOLDFISH_PAGE_RING_COUNT - 1u);
			uint32_t poff = s_page_ring[slot].flash_off;
			qspi_program_page(poff, s_page_ring[slot].data);
			note_page_flushed(poff);
			__dmb();
			s_page_r++;

			/* Refill the heads from freshly flushed data (needs XIP mapped). */
			s_rom_flush();
			s_rom_enter_xip();
			head_refill(s_head[0]);
			head_refill(s_head[1]);
			s_rom_connect();
			s_rom_exit_xip();

			uint8_t res = 0x7Au;
			qspi_xfer(&res, NULL, 1);
			busy_wait_us(30);                /* tRES: let erase restart (WIP=1) */
		}
		if (++guard > 4000000u) break;       /* safety: never spin forever */
	}

	s_rom_flush();
	s_rom_enter_xip();
	restore_interrupts(ints);
	s_erase_count++;
}

/* Region-relative distance the erase frontier leads the write head, modulo the
 * region size. Kept >= GOLDFISH_ERASE_LOOKAHEAD sectors so pending pages always
 * land in erased sectors. Per audio channel c. */
static void ensure_erase_ahead_audio(uint32_t c)
{
	if (s_audio_bytes == 0u) return;
	goldfish_audio_channel_t *ch = &s_ch[c];
	uint32_t wrel = (ch->write_off - ch->audio_off) % s_audio_bytes;
	uint32_t guard = 0u;
	for (;;) {
		uint32_t erel  = (ch->next_erase - ch->audio_off) % s_audio_bytes;
		uint32_t ahead = (erel + s_audio_bytes - wrel) % s_audio_bytes;
		if (ahead >= GOLDFISH_ERASE_LOOKAHEAD * FLASH_SECTOR_SIZE) break;
		flash_erase_sector_suspend(ch->next_erase);
		ch->next_erase += FLASH_SECTOR_SIZE;
		if (ch->next_erase >= ch->audio_off + s_audio_bytes) ch->next_erase = ch->audio_off;
		if (++guard >= GOLDFISH_ERASE_LOOKAHEAD + 2u) break;
	}
}

static void ensure_erase_ahead_cv(void)
{
	if (s_cv_bytes == 0u) return;
	uint32_t wrel = (s_cv_write_off - s_cv_off) % s_cv_bytes;
	uint32_t guard = 0u;
	for (;;) {
		uint32_t erel  = (s_cv_next_erase - s_cv_off) % s_cv_bytes;
		uint32_t ahead = (erel + s_cv_bytes - wrel) % s_cv_bytes;
		if (ahead >= GOLDFISH_ERASE_LOOKAHEAD * FLASH_SECTOR_SIZE) break;
		flash_erase_sector_suspend(s_cv_next_erase);
		s_cv_next_erase += FLASH_SECTOR_SIZE;
		if (s_cv_next_erase >= s_cv_off + s_cv_bytes) s_cv_next_erase = s_cv_off;
		if (++guard >= GOLDFISH_ERASE_LOOKAHEAD + 2u) break;
	}
}

/* Enqueue a full page for core 1. If the ring is full the frame is dropped
 * (an overrun; should not happen when core 1 keeps up). */
static void __not_in_flash_func(enqueue_page)(uint32_t flash_off, const uint8_t *data)
{
	uint32_t w = s_page_w;
	if (w - s_page_r >= GOLDFISH_PAGE_RING_COUNT) {
		return; /* overrun */
	}
	uint32_t slot = w & (GOLDFISH_PAGE_RING_COUNT - 1u);
	s_page_ring[slot].flash_off = flash_off;
	for (uint32_t i = 0u; i < GOLDFISH_PAGE_SIZE; i++) {
		s_page_ring[slot].data[i] = data[i];
	}
	__dmb();
	s_page_w = w + 1u;
}

/* ------------------------------------------------------------------ */
/* Init / geometry                                                    */
/* ------------------------------------------------------------------ */

void goldfish_stream_init(void)
{
	qspi_rom_init();
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

	/* Two audio ADPCM channels + one raw CV stream. Per channel: audio is
	 * 2 samples/byte, CV is one byte per GOLDFISH_CV_DECIM samples. To make all
	 * three run out together the byte budget is audioL : audioR : cv = 2 : 2 : 1
	 * (each audio channel gets 2/5 of the space, CV 1/5). */
	s_audio_bytes = align_down((remaining * 2u) / 5u, FLASH_SECTOR_SIZE);
	s_cv_bytes    = align_down(remaining - 2u * s_audio_bytes, FLASH_SECTOR_SIZE);

	s_ch[0].audio_off = s_header_off + s_header_size;
	s_ch[1].audio_off = s_ch[0].audio_off + s_audio_bytes;
	s_cv_off          = s_ch[1].audio_off + s_audio_bytes;

	/* Capacity is whichever stream bounds first. Audio: 2 samples/byte (per
	 * channel, both equal). CV: GOLDFISH_CV_DECIM audio samples per stored byte. */
	uint32_t cap_audio = s_audio_bytes * 2u;
	uint32_t cap_cv    = s_cv_bytes * GOLDFISH_CV_DECIM;
	s_capacity_samples = (cap_audio < cap_cv) ? cap_audio : cap_cv;

	/* Choose the smallest power-of-two interval that keeps the keyframe count
	 * within budget. Power-of-two keeps keyframe indexing a shift. */
	uint32_t need = (s_capacity_samples + GOLDFISH_KEYFRAME_BUDGET - 1u)
	                / GOLDFISH_KEYFRAME_BUDGET;
	s_keyframe_interval = next_pow2(need < 256u ? 256u : need);

	s_kf_slots = s_capacity_samples / s_keyframe_interval;
	if (s_kf_slots == 0u) s_kf_slots = 1u;
	if (s_kf_slots > GOLDFISH_KEYFRAME_BUDGET) s_kf_slots = GOLDFISH_KEYFRAME_BUDGET;
	s_continuous = false;

	for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) s_ch[c].num_keyframes = 0u;
	s_recorded_samples = 0u;
	s_rec_active       = false;
	s_page_w = s_page_r = 0u;
	s_erase_count = 0u;
}

/* ------------------------------------------------------------------ */
/* Record path (core 0)                                               */
/* ------------------------------------------------------------------ */

void __not_in_flash_func(goldfish_stream_record_start)(void)
{
	s_rec_active  = true;
	s_write_index = 0u;

	for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) {
		goldfish_audio_channel_t *ch = &s_ch[c];
		ch->enc.predictor   = 0;
		ch->enc.step_index  = 0;
		ch->cur_byte        = 0u;
		ch->nybble_phase    = false;
		ch->fill            = 0u;
		ch->write_off       = ch->audio_off;
		ch->num_keyframes   = 0u;
		ch->next_erase      = ch->audio_off; /* erase-ahead starts at region base */
		ch->flushed_samples = 0u;
		ch->pages_written   = 0u;
	}

	s_cv_fill       = 0u;
	s_cv_write_off  = s_cv_off;
	s_cv_next_erase = s_cv_off;
	s_continuous    = false;
	s_recorded_samples = 0u;
}

void __not_in_flash_func(goldfish_stream_delay_start)(void)
{
	/* Continuous, wrapping record for the flash delay line. */
	goldfish_stream_record_start();
	s_continuous = true;
}

bool __not_in_flash_func(goldfish_stream_record_sample)(int16_t left, int16_t right, int16_t cv)
{
	if (!s_rec_active) return false;
	if (!s_continuous && s_write_index >= s_capacity_samples) {
		return false; /* region full (fixed recording) */
	}

	int16_t in[GOLDFISH_AUDIO_CHANNELS] = { left, right };

	/* Keyframe boundary is shared by both channels (same logical timeline). */
	bool     kf   = ((s_write_index & (s_keyframe_interval - 1u)) == 0u);
	uint32_t slot = kf ? kf_slot(s_write_index / s_keyframe_interval) : 0u;

	for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) {
		goldfish_audio_channel_t *ch = &s_ch[c];

		/* Capture keyframe (encoder state *before* encoding this sample). */
		if (kf) {
			ch->keyframes[slot].predictor  = ch->enc.predictor;
			ch->keyframes[slot].step_index = ch->enc.step_index;
			ch->keyframes[slot]._pad       = 0;
			if (slot + 1u > ch->num_keyframes) ch->num_keyframes = slot + 1u;
		}

		/* Encode audio nybble, pack two per byte. */
		uint8_t nyb = adpcm_encode(in[c], &ch->enc);
		if (!ch->nybble_phase) {
			ch->cur_byte = nyb;
			ch->nybble_phase = true;
		} else {
			ch->cur_byte |= (uint8_t)(nyb << 4);
			ch->nybble_phase = false;
			ch->page[ch->fill++] = ch->cur_byte;
			if (ch->fill == GOLDFISH_PAGE_SIZE) {
				enqueue_page(ch->write_off, ch->page);
				ch->write_off += GOLDFISH_PAGE_SIZE;
				if (ch->write_off >= ch->audio_off + s_audio_bytes) ch->write_off = ch->audio_off;
				ch->fill = 0u;
			}
		}
	}

	/* Store one mono decimated 8-bit CV byte per GOLDFISH_CV_DECIM audio samples. */
	if ((s_write_index % GOLDFISH_CV_DECIM) == 0u) {
		int16_t cc = cv;
		if (cc > 2047) cc = 2047;
		if (cc < -2048) cc = -2048;
		s_cv_page[s_cv_fill++] = (uint8_t)(int8_t)(cc >> 4);
		if (s_cv_fill == GOLDFISH_PAGE_SIZE) {
			enqueue_page(s_cv_write_off, s_cv_page);
			s_cv_write_off += GOLDFISH_PAGE_SIZE;
			if (s_cv_write_off >= s_cv_off + s_cv_bytes) s_cv_write_off = s_cv_off;
			s_cv_fill = 0u;
		}
	}

	s_write_index++;
	return true;
}

void __not_in_flash_func(goldfish_stream_record_stop)(void)
{
	if (!s_rec_active) return;

	for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) {
		goldfish_audio_channel_t *ch = &s_ch[c];

		/* Flush a dangling nybble (odd sample count) into a full byte. */
		if (ch->nybble_phase) {
			ch->page[ch->fill++] = ch->cur_byte;
			ch->nybble_phase = false;
			if (ch->fill == GOLDFISH_PAGE_SIZE) {
				enqueue_page(ch->write_off, ch->page);
				ch->write_off += GOLDFISH_PAGE_SIZE;
				ch->fill = 0u;
			}
		}

		/* Flush partial audio page (pad to a full program page). */
		if (ch->fill > 0u) {
			for (uint32_t i = ch->fill; i < GOLDFISH_PAGE_SIZE; i++) ch->page[i] = 0u;
			enqueue_page(ch->write_off, ch->page);
			ch->write_off += GOLDFISH_PAGE_SIZE;
			ch->fill = 0u;
		}
	}

	/* Flush partial CV page. */
	if (s_cv_fill > 0u) {
		for (uint32_t i = s_cv_fill; i < GOLDFISH_PAGE_SIZE; i++) s_cv_page[i] = 0u;
		enqueue_page(s_cv_write_off, s_cv_page);
		s_cv_write_off += GOLDFISH_PAGE_SIZE;
		s_cv_fill = 0u;
	}

	s_recorded_samples = s_write_index;
	s_rec_active = false;
	s_continuous = false;
}

/* ------------------------------------------------------------------ */
/* Core 1 flash I/O                                                   */
/* ------------------------------------------------------------------ */

uint32_t goldfish_stream_io_task(void)
{
	uint32_t written = 0u;

	/* Top up the playback heads first. */
	head_refill(s_head[0]);
	head_refill(s_head[1]);

	/* Keep the erase frontier ahead of the write head. These erases suspend to
	 * program pending pages (advancing flushed) and refill the heads, so the
	 * flushed frontier keeps advancing right through every erase.
	 * Gated on s_rec_active: the frontiers are only valid once record_start has
	 * initialised them. Running before that would erase from flash offset 0
	 * (the firmware region), so this guard is essential. */
	if (s_rec_active) {
		for (uint32_t c = 0u; c < GOLDFISH_AUDIO_CHANNELS; c++) ensure_erase_ahead_audio(c);
		ensure_erase_ahead_cv();
	}

	/* Program any pages not already drained during an erase-suspend above. Their
	 * sectors were pre-erased by the erase-ahead, so no inline erase is needed. */
	while (s_page_r != s_page_w) {
		uint32_t slot = s_page_r & (GOLDFISH_PAGE_RING_COUNT - 1u);
		uint32_t off  = s_page_ring[slot].flash_off;

		flash_program_page(off, s_page_ring[slot].data);
		note_page_flushed(off);

		__dmb();
		s_page_r++;
		written++;
	}

	/* Keep the playback heads' decode windows filled. */
	head_refill(s_head[0]);
	head_refill(s_head[1]);

	return written;
}

bool goldfish_stream_io_idle(void)
{
	return s_page_r == s_page_w;
}

/* ------------------------------------------------------------------ */
/* Read-back (random access)                                          */
/* ------------------------------------------------------------------ */

int16_t goldfish_stream_read_cv(uint32_t sample_index)
{
	if (sample_index >= s_recorded_samples) return 0;
	uint32_t cv_index = sample_index / GOLDFISH_CV_DECIM;
	const int8_t *base = (const int8_t *)xip_ptr(s_cv_off);
	return (int16_t)((int16_t)base[cv_index] << 4);
}

/* ------------------------------------------------------------------ */
/* Core-1-refilled playback heads                                     */
/* ------------------------------------------------------------------ */

void goldfish_stream_head_init(goldfish_head_t *h, uint8_t channel)
{
	h->req_pos    = 0u;
	h->active     = false;
	h->lo         = 0u;
	h->hi         = 0u;
	h->last       = 0;
	h->channel    = (uint8_t)(channel % GOLDFISH_AUDIO_CHANNELS);
	h->predictor  = 0;
	h->step_index = 0;
	h->fill_next  = 0u;
	h->fwd_valid  = false;
	h->need_seek  = true;
}

void goldfish_stream_set_heads(goldfish_head_t *hL, goldfish_head_t *hR)
{
	s_head[0] = hL;
	s_head[1] = hR;
}

int16_t __not_in_flash_func(goldfish_stream_head_read)(goldfish_head_t *h, uint32_t sample_index)
{
	if (s_recorded_samples == 0u) return 0;
	if (sample_index >= s_recorded_samples) sample_index = s_recorded_samples - 1u;

	h->req_pos = sample_index;
	h->active  = true;

	uint32_t lo = h->lo;
	uint32_t hi = h->hi;
	if (sample_index >= lo && sample_index < hi) {
		h->last = h->pcm[sample_index & GOLDFISH_RING_MASK];
	}
	/* else: underrun — hold last good sample until core 1 catches up. */
	return h->last;
}

/* Core-1: keep one head's window covering its requested position, with margin on
 * BOTH sides so forward and reverse playback never outrun the decoded region.
 * Forward growth is an incremental decode; downward growth (for reverse) decodes
 * the previous keyframe block and prepends it. A full reseek only happens on a
 * large jump (e.g. loop wrap). Work per call is bounded. */
static void head_refill(goldfish_head_t *h)
{
	if (h == NULL || !h->active || s_recorded_samples == 0u) return;

	goldfish_audio_channel_t *ch = &s_ch[h->channel];
	const uint32_t MARGIN = 1536u;   /* runway kept each side of the playhead */
	uint32_t pos  = h->req_pos;
	const uint8_t *base = xip_ptr(ch->audio_off);

	uint32_t want_lo = (pos > MARGIN) ? (pos - MARGIN) : 0u;
	uint32_t want_hi = pos + MARGIN;
	if (want_hi > s_recorded_samples) want_hi = s_recorded_samples;

	/* Full reseek if the window is empty or no longer contains pos. */
	if (h->need_seek || h->hi <= h->lo || pos < h->lo || pos >= h->hi) {
		uint32_t k = want_lo / s_keyframe_interval;
		uint32_t kstart = k * s_keyframe_interval;

		h->predictor  = ch->keyframes[kf_slot(k)].predictor;
		h->step_index = ch->keyframes[kf_slot(k)].step_index;
		h->fill_next  = kstart;
		h->lo         = kstart;
		h->hi         = kstart;
		h->fwd_valid  = true;
		h->need_seek  = false;
	}

	/* Forward: extend hi up to want_hi. Re-prime the decoder first if a reverse
	 * drop invalidated it. */
	if (h->fill_next < want_hi) {
		if (!h->fwd_valid) {
			uint32_t k = h->fill_next / s_keyframe_interval;
			adpcm_state_t ps;
			ps.predictor  = ch->keyframes[kf_slot(k)].predictor;
			ps.step_index = ch->keyframes[kf_slot(k)].step_index;
			for (uint32_t i = k * s_keyframe_interval; i < h->fill_next; i++) {
				uint8_t byte = base[audio_byte_wrap(i >> 1)];
				uint8_t nyb  = (i & 1u) ? (uint8_t)((byte >> 4) & 0x0Fu)
				                        : (uint8_t)(byte & 0x0Fu);
				(void)adpcm_decode(nyb, &ps);
			}
			h->predictor  = ps.predictor;
			h->step_index = ps.step_index;
			h->fwd_valid  = true;
		}

		adpcm_state_t st;
		st.predictor  = h->predictor;
		st.step_index = h->step_index;
		uint32_t budget = 2048u;
		while (h->fill_next < want_hi && budget-- != 0u) {
			uint32_t idx = h->fill_next;
			uint8_t byte = base[audio_byte_wrap(idx >> 1)];
			uint8_t nyb  = (idx & 1u) ? (uint8_t)((byte >> 4) & 0x0Fu)
			                          : (uint8_t)(byte & 0x0Fu);
			h->pcm[idx & GOLDFISH_RING_MASK] = adpcm_decode(nyb, &st);
			h->fill_next++;
			__dmb();
			h->hi = h->fill_next;
			if (h->hi - h->lo > GOLDFISH_RING_SZ) h->lo = h->hi - GOLDFISH_RING_SZ;
		}
		h->predictor  = st.predictor;
		h->step_index = st.step_index;
	}

	/* Backward: extend lo down to want_lo by decoding whole keyframe blocks. */
	uint32_t bbudget = 2048u;
	while (h->lo > want_lo && bbudget != 0u) {
		uint32_t span_hi = h->lo;
		uint32_t k = (span_hi - 1u) / s_keyframe_interval;
		uint32_t dstart = k * s_keyframe_interval;

		adpcm_state_t bst;
		bst.predictor  = ch->keyframes[kf_slot(k)].predictor;
		bst.step_index = ch->keyframes[kf_slot(k)].step_index;
		for (uint32_t i = dstart; i < span_hi; i++) {
			uint8_t byte = base[audio_byte_wrap(i >> 1)];
			uint8_t nyb  = (i & 1u) ? (uint8_t)((byte >> 4) & 0x0Fu)
			                        : (uint8_t)(byte & 0x0Fu);
			int16_t s = adpcm_decode(nyb, &bst);
			if (i >= want_lo) h->pcm[i & GOLDFISH_RING_MASK] = s;
		}
		__dmb();
		h->lo = (dstart > want_lo) ? dstart : want_lo;
		if (h->hi - h->lo > GOLDFISH_RING_SZ) {
			h->hi = h->lo + GOLDFISH_RING_SZ;
			h->fill_next = h->hi;
			h->fwd_valid = false;   /* forward decoder no longer matches fill_next */
		}
		uint32_t did = span_hi - dstart;
		bbudget = (bbudget > did) ? (bbudget - did) : 0u;
	}
}

int16_t __not_in_flash_func(goldfish_stream_cv_read)(uint32_t sample_index)
{
	/* CV is raw + low-rate; direct flash read (valid while no erase in flight). */
	return goldfish_stream_read_cv(sample_index);
}

/* ------------------------------------------------------------------ */
/* Introspection                                                      */
/* ------------------------------------------------------------------ */

uint32_t goldfish_stream_flash_size(void)        { return s_flash_size; }
uint32_t goldfish_stream_keyframe_interval(void) { return s_keyframe_interval; }
uint32_t goldfish_stream_capacity_samples(void)  { return s_capacity_samples; }
uint32_t goldfish_stream_recorded_samples(void)  { return s_recorded_samples; }
uint32_t goldfish_stream_write_index(void)       { return s_write_index; }
uint32_t goldfish_stream_erase_count(void)       { return s_erase_count; }
float    goldfish_stream_capacity_seconds(void)  { return s_capacity_samples / 24000.0f; }
