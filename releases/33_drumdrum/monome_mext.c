/**
 * monome_mext.c — Lightweight mext protocol for monome grid/arc on RP2040.
 *
 * Vendored verbatim from dessertplanet/Workshop_Computer (MLRws release).
 * Direct byte-level mext TX/RX, modelled on dessertplanet/viii and
 * monome/ansible.  All writes are padded to 64 bytes with 0xFF for
 * USB bulk packet alignment — essential for FTDI compatibility.
 *
 * Transport: TinyUSB CDC host (tuh_cdc_write / tuh_cdc_read).
 */

#include "monome_mext.h"

#include <string.h>
#include "tusb.h"
#include "pico/time.h"
#include "pico/platform.h"

/* ------------------------------------------------------------------ */
/* Global state                                                       */
/* ------------------------------------------------------------------ */
mext_state_t g_mext;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* drumdrum customisation: we never use CDC as a USB device (browser
 * mode is MIDI-only), so the device-CDC branches are compiled out
 * unless the descriptor table actually exposes a CDC interface. */
#if CFG_TUD_CDC > 0
#  define MEXT_HAVE_DEVICE_CDC 1
#else
#  define MEXT_HAVE_DEVICE_CDC 0
#endif

/** Send raw bytes padded to 64 with 0xFF — for LED commands. */
static void mext_send_raw(const uint8_t *data, uint32_t len)
{
	if (!g_mext.connected || g_mext.cdc_idx < 0)
		return;

	uint8_t padded[64];
	if (len < 64) {
		memcpy(padded, data, len);
		memset(padded + len, 0xFF, 64 - len);
		if (g_mext.transport == MEXT_TRANSPORT_DEVICE) {
#if MEXT_HAVE_DEVICE_CDC
			if (tud_cdc_n_write_available(g_mext.device_cdc_itf) < 64)
				return;
			tud_cdc_n_write(g_mext.device_cdc_itf, padded, 64);
			tud_cdc_n_write_flush(g_mext.device_cdc_itf);
#endif
		} else {
			tuh_cdc_write((uint8_t)g_mext.cdc_idx, padded, 64);
			tuh_cdc_write_flush((uint8_t)g_mext.cdc_idx);
		}
	} else {
		if (g_mext.transport == MEXT_TRANSPORT_DEVICE) {
#if MEXT_HAVE_DEVICE_CDC
			if (tud_cdc_n_write_available(g_mext.device_cdc_itf) < len)
				return;
			tud_cdc_n_write(g_mext.device_cdc_itf, data, len);
			tud_cdc_n_write_flush(g_mext.device_cdc_itf);
#endif
		} else {
			tuh_cdc_write((uint8_t)g_mext.cdc_idx, data, len);
			tuh_cdc_write_flush((uint8_t)g_mext.cdc_idx);
		}
	}
}

/** Send bytes without padding — for discovery/system queries. */
static void mext_send(const uint8_t *data, uint32_t len)
{
	if (!g_mext.connected || g_mext.cdc_idx < 0)
		return;

	if (g_mext.transport == MEXT_TRANSPORT_DEVICE) {
#if MEXT_HAVE_DEVICE_CDC
		if (tud_cdc_n_write_available(g_mext.device_cdc_itf) < len)
			return;
		tud_cdc_n_write(g_mext.device_cdc_itf, data, len);
		tud_cdc_n_write_flush(g_mext.device_cdc_itf);
#endif
	} else {
		tuh_cdc_write((uint8_t)g_mext.cdc_idx, data, len);
		tuh_cdc_write_flush((uint8_t)g_mext.cdc_idx);
	}
}

static void event_push(const mext_event_t *e)
{
	uint8_t next = (g_mext.events.w + 1) % MEXT_EVENT_QUEUE_SIZE;
	if (next == g_mext.events.r)
		return; /* queue full, drop event */
	g_mext.events.buf[g_mext.events.w] = *e;
	g_mext.events.w = next;
}

static inline uint8_t quad_idx(uint8_t x, uint8_t y)
{
	return ((y >= 8) << 1) | (x >= 8);
}

/* ------------------------------------------------------------------ */
/* mext RX parser                                                     */
/* ------------------------------------------------------------------ */

static uint8_t mext_response_len(uint8_t header)
{
	uint8_t addr = header >> 4;
	uint8_t cmd  = header & 0x0F;

	switch (addr) {
	case 0x0: /* system */
		switch (cmd) {
		case 0x0: return 3;
		case 0x1: return 33;
		case 0x2: return 4;
		case 0x3: return 3;
		case 0x4: return 3;
		case 0xF: return 9;
		default:  return 1;
		}

	case 0x2: /* key grid */
		return (cmd <= 0x1) ? 3 : 1;

	case 0x5: /* encoder */
		if (cmd == 0x0) return 3;
		if (cmd == 0x1 || cmd == 0x2) return 2;
		return 1;

	case 0x8: /* tilt */
		if (cmd == 0x0) return 2;
		if (cmd == 0x1) return 8;
		return 1;

	default:
		return 1;
	}
}

static void mext_dispatch_message(void)
{
	uint8_t header = g_mext.rx_buf[0];
	uint8_t addr   = header >> 4;
	uint8_t cmd    = header & 0x0F;

	switch (addr) {
	case 0x0:
		if (cmd == 0x3 && g_mext.rx_expected == 3) {
			uint8_t cols = g_mext.rx_buf[1];
			uint8_t rows = g_mext.rx_buf[2];
			if (cols > 0 && cols <= MEXT_MAX_GRID_X) g_mext.grid_x = cols;
			if (rows > 0 && rows <= MEXT_MAX_GRID_Y) g_mext.grid_y = rows;
		} else if (cmd == 0x0 && g_mext.rx_expected == 3) {
			uint8_t subsystem = g_mext.rx_buf[1];
			uint8_t count     = g_mext.rx_buf[2];
			if (subsystem == 0x5) {
				g_mext.arc_enc_count = (count > MEXT_MAX_ENCODERS)
					? MEXT_MAX_ENCODERS : count;
				if (g_mext.arc_enc_count > 0)
					g_mext.is_arc = true;
			}
		}
		break;

	case 0x2:
		{
			mext_event_t e;
			if (g_mext.is_arc) {
				e.type      = MEXT_EVENT_ENC_KEY;
				e.enc_key.n = g_mext.rx_buf[1];
				e.enc_key.z = (cmd == 0x1) ? 1 : 0;
			} else {
				e.type   = MEXT_EVENT_GRID_KEY;
				e.grid.x = g_mext.rx_buf[1];
				e.grid.y = g_mext.rx_buf[2];
				e.grid.z = (cmd == 0x1) ? 1 : 0;
			}
			event_push(&e);
		}
		break;

	case 0x5:
		{
			mext_event_t e;
			if (cmd == 0x0) {
				e.type      = MEXT_EVENT_ENC_DELTA;
				e.enc.n     = g_mext.rx_buf[1];
				e.enc.delta = (int8_t)g_mext.rx_buf[2];
			} else if (cmd == 0x1) {
				e.type      = MEXT_EVENT_ENC_KEY;
				e.enc_key.n = g_mext.rx_buf[1];
				e.enc_key.z = 0;
			} else if (cmd == 0x2) {
				e.type      = MEXT_EVENT_ENC_KEY;
				e.enc_key.n = g_mext.rx_buf[1];
				e.enc_key.z = 1;
			} else {
				break;
			}
			event_push(&e);
		}
		break;

	default:
		break;
	}
}

static void mext_rx_consume_byte(uint8_t byte)
{
	if (g_mext.rx_expected == 0) {
		if (byte == 0xFF) return;

		g_mext.rx_buf[0]  = byte;
		g_mext.rx_len      = 1;
		g_mext.rx_expected = mext_response_len(byte);

		if (g_mext.rx_expected <= 1) {
			if (g_mext.rx_expected == 1)
				mext_dispatch_message();
			g_mext.rx_expected = 0;
			g_mext.rx_len      = 0;
		}
		return;
	}

	if (g_mext.rx_len < sizeof(g_mext.rx_buf)) {
		g_mext.rx_buf[g_mext.rx_len++] = byte;
	}

	if (g_mext.rx_len >= g_mext.rx_expected) {
		mext_dispatch_message();
		g_mext.rx_expected = 0;
		g_mext.rx_len      = 0;
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void mext_init(mext_transport_t transport, uint8_t device_cdc_itf)
{
	memset(&g_mext, 0, sizeof(g_mext));
	g_mext.cdc_idx        = -1;
	g_mext.transport      = (uint8_t)transport;
	g_mext.device_cdc_itf = device_cdc_itf;
	g_mext.grid_intensity = 15;
}

void mext_connect(int cdc_idx)
{
	g_mext.cdc_idx   = cdc_idx;
	g_mext.connected = true;

	g_mext.grid_x       = 0;
	g_mext.grid_y       = 0;
	g_mext.arc_enc_count = 0;
	g_mext.is_arc        = false;
	g_mext.rx_len        = 0;
	g_mext.rx_expected   = 0;

	memset(g_mext.grid_led, 0, sizeof(g_mext.grid_led));
	memset(g_mext.grid_next, 0, sizeof(g_mext.grid_next));
	memset(g_mext.arc_ring, 0, sizeof(g_mext.arc_ring));
	for (int q = 0; q < 4; q++) g_mext.grid_dirty[q] = false;
	g_mext.grid_frame_pending = false;

	g_mext.discovery_tick = 0;
	g_mext.connect_time_us = time_us_64();
}

void mext_disconnect(void)
{
	g_mext.connected = false;
	g_mext.cdc_idx   = -1;
}

void mext_grid_led_set(uint8_t x, uint8_t y, uint8_t level)
{
	if (x >= MEXT_MAX_GRID_X || y >= MEXT_MAX_GRID_Y) return;
	if (level > 15) level = 15;

	g_mext.grid_led[y * MEXT_MAX_GRID_X + x] = level;
	g_mext.grid_dirty[quad_idx(x, y)] = true;
}

void mext_grid_led_all(uint8_t level)
{
	if (level > 15) level = 15;
	memset(g_mext.grid_led, level, sizeof(g_mext.grid_led));
	for (int q = 0; q < 4; q++) g_mext.grid_dirty[q] = true;
}

void mext_grid_led_intensity(uint8_t level)
{
	g_mext.grid_intensity = level & 0x0F;
	g_mext.intensity_pending = true;
}

void mext_grid_all_off(void)
{
	const uint8_t buf[] = {0x12};
	mext_send_raw(buf, sizeof(buf));
}

bool __not_in_flash_func(mext_grid_frame_submit)(const uint8_t *levels)
{
	if (levels == NULL)
		return false;
	if (g_mext.grid_frame_pending)
		return false;

	memcpy(g_mext.grid_next, levels, sizeof(g_mext.grid_next));
	__dmb();
	g_mext.grid_frame_pending = true;
	return true;
}

void mext_send_discovery(void)
{
	const uint8_t query_caps[] = {0x00};
	const uint8_t query_id[]   = {0x01};
	const uint8_t query_size[] = {0x05};

	mext_send(query_caps, sizeof(query_caps));
	mext_send(query_id,   sizeof(query_id));
	mext_send(query_size, sizeof(query_size));
}

void mext_grid_refresh(void)
{
	if (!g_mext.connected) return;
	if (g_mext.grid_x == 0 || g_mext.grid_y == 0) return;

	if (g_mext.grid_frame_pending) {
		memcpy(g_mext.grid_led, g_mext.grid_next, sizeof(g_mext.grid_led));
		for (int q = 0; q < 4; q++) g_mext.grid_dirty[q] = true;
		__dmb();
		g_mext.grid_frame_pending = false;
	}

	if (g_mext.intensity_pending) {
		uint8_t ibuf[2] = {0x17, g_mext.grid_intensity};
		mext_send_raw(ibuf, 2);
		g_mext.intensity_pending = false;
	}

	for (uint8_t yo = 0; yo < g_mext.grid_y; yo += 8) {
		for (uint8_t xo = 0; xo < g_mext.grid_x; xo += 8) {
			uint8_t q = quad_idx(xo, yo);
			if (!g_mext.grid_dirty[q]) continue;

			uint8_t levels[64];
			for (uint8_t r = 0; r < 8; r++) {
				for (uint8_t c = 0; c < 8; c++) {
					uint8_t gx = xo + c;
					uint8_t gy = yo + r;
					if (gx < g_mext.grid_x && gy < g_mext.grid_y)
						levels[r * 8 + c] = g_mext.grid_led[gy * MEXT_MAX_GRID_X + gx];
					else
						levels[r * 8 + c] = 0;
				}
			}

			uint8_t buf[35];
			buf[0] = 0x1A;
			buf[1] = xo;
			buf[2] = yo;
			uint8_t *p = buf + 3;
			for (int i = 0; i < 64; i += 2) {
				*p++ = (levels[i] << 4) | (levels[i + 1] & 0x0F);
			}

			mext_send_raw(buf, 35);
			g_mext.grid_dirty[q] = false;
		}
	}
}

void mext_rx_feed(const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		mext_rx_consume_byte(data[i]);
	}
}

bool __not_in_flash_func(mext_event_pop)(mext_event_t *out)
{
	if (g_mext.events.r == g_mext.events.w)
		return false;

	*out = g_mext.events.buf[g_mext.events.r];
	g_mext.events.r = (g_mext.events.r + 1) % MEXT_EVENT_QUEUE_SIZE;
	return true;
}

bool __not_in_flash_func(mext_grid_ready)(void)
{
	return g_mext.connected && g_mext.grid_x > 0 && g_mext.grid_y > 0;
}

/* ------------------------------------------------------------------ */
/* Polling task                                                       */
/* ------------------------------------------------------------------ */

#define MEXT_REFRESH_US  16667  /* ~60 fps */

static uint64_t s_last_refresh_us = 0;

void mext_task(void)
{
	if (g_mext.transport == MEXT_TRANSPORT_HOST) {
		tuh_task();
	}

	if (!g_mext.connected)
		return;

	if (g_mext.transport == MEXT_TRANSPORT_HOST) {
		uint8_t buf[64];
		uint32_t n = tuh_cdc_read((uint8_t)g_mext.cdc_idx, buf, sizeof(buf));
		if (n > 0) {
			mext_rx_feed(buf, n);
		}
	} else {
#if MEXT_HAVE_DEVICE_CDC
		if (tud_mounted()) {
			uint8_t buf[64];
			uint32_t n = tud_cdc_n_read(g_mext.device_cdc_itf, buf, sizeof(buf));
			if (n > 0) {
				mext_rx_feed(buf, n);
			}
		}
#endif
	}

	if (g_mext.grid_x == 0 || g_mext.grid_y == 0) {
		uint64_t now_us = time_us_64();
		uint64_t elapsed_ms = (now_us - g_mext.connect_time_us) / 1000;
		if ((elapsed_ms > 200 && g_mext.discovery_tick == 0) ||
		    (elapsed_ms > 1000 && (elapsed_ms / 1000) > g_mext.discovery_tick)) {
			g_mext.discovery_tick = (uint32_t)(elapsed_ms / 1000);
			mext_send_discovery();
		}
	}

	uint64_t now_us = time_us_64();
	if (now_us - s_last_refresh_us >= MEXT_REFRESH_US) {
		s_last_refresh_us = now_us;
		mext_grid_refresh();
	}
}

/* ------------------------------------------------------------------ */
/* TinyUSB CDC host callbacks                                         */
/* ------------------------------------------------------------------ */

void tuh_cdc_mount_cb(uint8_t idx)
{
	if (g_mext.transport != MEXT_TRANSPORT_HOST)
		return;

	if (!g_mext.connected) {
		mext_connect((int)idx);
	}
}

void tuh_cdc_umount_cb(uint8_t idx)
{
	if (g_mext.transport != MEXT_TRANSPORT_HOST)
		return;

	if (g_mext.cdc_idx == (int)idx) {
		mext_disconnect();
	}
}

void tuh_cdc_rx_cb(uint8_t idx)
{
	(void)idx;
}

void tuh_cdc_tx_complete_cb(uint8_t idx)
{
	(void)idx;
}
