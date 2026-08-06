/**
 * monome_ws.c - lightweight monome grid/arc driver for RP2040.
 */

#include "monome_ws.h"

#include <string.h>
#include "tusb.h"
#include "pico/time.h"
#include "pico/platform.h"

#ifdef MLR_PERF_PROFILING
void mlr_perf_count_monome_ws_event_drop(void);
#endif

#ifndef MONOME_WS_LEGACY_DEFAULT_PROTOCOL
#define MONOME_WS_LEGACY_DEFAULT_PROTOCOL MONOME_WS_PROTOCOL_SERIES
#endif
#ifndef MONOME_WS_LEGACY_DEFAULT_COLS
#define MONOME_WS_LEGACY_DEFAULT_COLS 8
#endif
#ifndef MONOME_WS_LEGACY_DEFAULT_ROWS
#define MONOME_WS_LEGACY_DEFAULT_ROWS 8
#endif
#ifndef MONOME_WS_FORCE_8X8_MONOBRIGHT
#define MONOME_WS_FORCE_8X8_MONOBRIGHT 0
#endif

#define MONOME_WS_REFRESH_US 16667
#define MONOME_WS_BINARY_THRESHOLD 7
#define MONOME_WS_LEGACY_SNIFF_DELAY_US 1500000u
#define MONOME_WS_OUTPUT_SETTLE_US 2500000ull
#define MONOME_WS_FORCE_REFRESH_WINDOW_US 1000000ull
#define MONOME_WS_FORCE_REFRESH_INTERVAL_US 250000ull

monome_ws_state_t g_monome_ws;
static uint64_t s_last_refresh_us = 0;

static bool monome_ws_send_raw(const uint8_t *data, uint32_t len)
{
	if (!g_monome_ws.connected || g_monome_ws.cdc_idx < 0)
		return false;

	uint8_t padded[64];
	if (len < 64) {
		memcpy(padded, data, len);
		memset(padded + len, 0xFF, 64 - len);
		if (g_monome_ws.transport == MONOME_WS_TRANSPORT_DEVICE) {
			if (tud_cdc_n_write_available(g_monome_ws.device_cdc_itf) < 64)
				return false;
			tud_cdc_n_write(g_monome_ws.device_cdc_itf, padded, 64);
			tud_cdc_n_write_flush(g_monome_ws.device_cdc_itf);
		} else {
			if (tuh_cdc_write_available((uint8_t)g_monome_ws.cdc_idx) < 64)
				return false;
			if (tuh_cdc_write((uint8_t)g_monome_ws.cdc_idx, padded, 64) != 64)
				return false;
			tuh_cdc_write_flush((uint8_t)g_monome_ws.cdc_idx);
		}
	} else {
		if (g_monome_ws.transport == MONOME_WS_TRANSPORT_DEVICE) {
			if (tud_cdc_n_write_available(g_monome_ws.device_cdc_itf) < len)
				return false;
			tud_cdc_n_write(g_monome_ws.device_cdc_itf, data, len);
			tud_cdc_n_write_flush(g_monome_ws.device_cdc_itf);
		} else {
			if (tuh_cdc_write_available((uint8_t)g_monome_ws.cdc_idx) < len)
				return false;
			if (tuh_cdc_write((uint8_t)g_monome_ws.cdc_idx, data, len) != len)
				return false;
			tuh_cdc_write_flush((uint8_t)g_monome_ws.cdc_idx);
		}
	}
	return true;
}

static void monome_ws_send(const uint8_t *data, uint32_t len)
{
	if (!g_monome_ws.connected || g_monome_ws.cdc_idx < 0)
		return;

	if (g_monome_ws.transport == MONOME_WS_TRANSPORT_DEVICE) {
		if (tud_cdc_n_write_available(g_monome_ws.device_cdc_itf) < len)
			return;
		tud_cdc_n_write(g_monome_ws.device_cdc_itf, data, len);
		tud_cdc_n_write_flush(g_monome_ws.device_cdc_itf);
	} else {
		tuh_cdc_write((uint8_t)g_monome_ws.cdc_idx, data, len);
		tuh_cdc_write_flush((uint8_t)g_monome_ws.cdc_idx);
	}
}

static void event_push(const monome_ws_event_t *e)
{
	uint8_t next = (g_monome_ws.events.w + 1) & MONOME_WS_EVENT_QUEUE_MASK;
	if (next == g_monome_ws.events.r) {
#ifdef MLR_PERF_PROFILING
		mlr_perf_count_monome_ws_event_drop();
#endif
		return;
	}
	g_monome_ws.events.buf[g_monome_ws.events.w] = *e;
	g_monome_ws.events.w = next;
}

static inline uint8_t quad_idx(uint8_t x, uint8_t y)
{
	return ((y >= 8) << 1) | (x >= 8);
}

static inline uint8_t level_to_bit(uint8_t level)
{
	return level > MONOME_WS_BINARY_THRESHOLD ? 1u : 0u;
}

static uint8_t frame_row_mask(uint8_t x_off, uint8_t y)
{
	uint8_t mask = 0;
	for (uint8_t bit = 0; bit < 8; bit++) {
		uint8_t x = x_off + bit;
		if (x < g_monome_ws.grid_x && y < g_monome_ws.grid_y) {
			uint8_t level = g_monome_ws.grid_led[y * MONOME_WS_GRID_MAX_X + x];
			if (level_to_bit(level))
				mask |= (uint8_t)(1u << bit);
		}
	}
	return mask;
}

static void set_grid_identity(uint8_t cols, uint8_t rows, monome_ws_protocol_t protocol)
{
	bool identity_changed = g_monome_ws.protocol != (uint8_t)protocol ||
		g_monome_ws.grid_x != cols || g_monome_ws.grid_y != rows;

	g_monome_ws.protocol = (uint8_t)protocol;
	g_monome_ws.device_kind = MONOME_WS_DEVICE_GRID;
#if MONOME_WS_FORCE_8X8_MONOBRIGHT
	if (protocol == MONOME_WS_PROTOCOL_MEXT) {
		cols = 8;
		rows = 8;
	}
#endif
	g_monome_ws.supports_levels = protocol == MONOME_WS_PROTOCOL_MEXT && !MONOME_WS_FORCE_8X8_MONOBRIGHT;
	g_monome_ws.grid_x = cols;
	g_monome_ws.grid_y = rows;
	if (identity_changed) {
		uint64_t now_us = time_us_64();
		uint64_t first_refresh_us = now_us;
		if (g_monome_ws.transport == MONOME_WS_TRANSPORT_HOST &&
			(now_us - g_monome_ws.connect_time_us) < MONOME_WS_OUTPUT_SETTLE_US) {
			first_refresh_us = g_monome_ws.connect_time_us + MONOME_WS_OUTPUT_SETTLE_US;
		}
		memset(g_monome_ws.grid_led, 0xFF, sizeof(g_monome_ws.grid_led));
		for (int q = 0; q < 4; q++)
			g_monome_ws.grid_dirty[q] = false;
		g_monome_ws.force_refresh_until_us = first_refresh_us + MONOME_WS_FORCE_REFRESH_WINDOW_US;
		g_monome_ws.next_force_refresh_us = first_refresh_us;
	}
}

static uint8_t mext_response_len(uint8_t header)
{
	uint8_t addr = header >> 4;
	uint8_t cmd  = header & 0x0F;

	switch (addr) {
	case 0x0:
		switch (cmd) {
		case 0x0: return 3;
		case 0x1: return 33;
		case 0x2: return 4;
		case 0x3: return 3;
		case 0x4: return 3;
		case 0xF: return 9;
		default:  return 1;
		}
	case 0x2:
		return (cmd <= 0x1) ? 3 : 1;
	case 0x5:
		if (cmd == 0x0) return 3;
		if (cmd == 0x1 || cmd == 0x2) return 2;
		return 1;
	case 0x8:
		if (cmd == 0x0) return 2;
		if (cmd == 0x1) return 8;
		return 1;
	default:
		return 1;
	}
}

static void mext_dispatch_message(void)
{
	uint8_t header = g_monome_ws.rx_buf[0];
	uint8_t addr   = header >> 4;
	uint8_t cmd    = header & 0x0F;

	switch (addr) {
	case 0x0:
		if (cmd == 0x3 && g_monome_ws.rx_expected == 3) {
			uint8_t cols = g_monome_ws.rx_buf[1];
			uint8_t rows = g_monome_ws.rx_buf[2];
			if (cols > 0 && cols <= MONOME_WS_GRID_MAX_X && rows > 0 && rows <= MONOME_WS_GRID_MAX_Y)
				set_grid_identity(cols, rows, MONOME_WS_PROTOCOL_MEXT);
		} else if (cmd == 0x0 && g_monome_ws.rx_expected == 3) {
			uint8_t subsystem = g_monome_ws.rx_buf[1];
			uint8_t count = g_monome_ws.rx_buf[2];
			if (subsystem == 0x5) {
				g_monome_ws.protocol = MONOME_WS_PROTOCOL_MEXT;
				g_monome_ws.device_kind = MONOME_WS_DEVICE_ARC;
				g_monome_ws.supports_levels = true;
				g_monome_ws.arc_enc_count = count > MONOME_WS_ARC_MAX_ENCODERS ? MONOME_WS_ARC_MAX_ENCODERS : count;
			}
		}
		break;

	case 0x2: {
		monome_ws_event_t e;
		if (g_monome_ws.device_kind == MONOME_WS_DEVICE_ARC) {
			e.type = MONOME_WS_EVENT_ARC_KEY;
			e.arc_key.n = g_monome_ws.rx_buf[1];
			e.arc_key.z = (cmd == 0x1) ? 1 : 0;
		} else {
			e.type = MONOME_WS_EVENT_GRID_KEY;
			e.grid.x = g_monome_ws.rx_buf[1];
			e.grid.y = g_monome_ws.rx_buf[2];
			e.grid.z = (cmd == 0x1) ? 1 : 0;
		}
		event_push(&e);
	} break;

	case 0x5: {
		monome_ws_event_t e;
		if (cmd == 0x0) {
			e.type = MONOME_WS_EVENT_ARC_DELTA;
			e.arc.n = g_monome_ws.rx_buf[1];
			e.arc.delta = (int8_t)g_monome_ws.rx_buf[2];
		} else if (cmd == 0x1 || cmd == 0x2) {
			e.type = MONOME_WS_EVENT_ARC_KEY;
			e.arc_key.n = g_monome_ws.rx_buf[1];
			e.arc_key.z = (cmd == 0x2) ? 1 : 0;
		} else {
			break;
		}
		event_push(&e);
	} break;

	default:
		break;
	}
}

static void mext_rx_consume_byte(uint8_t byte)
{
	if (g_monome_ws.rx_expected == 0) {
		if (byte == 0xFF) return;
		g_monome_ws.rx_buf[0] = byte;
		g_monome_ws.rx_len = 1;
		g_monome_ws.rx_expected = mext_response_len(byte);
		if (g_monome_ws.rx_expected <= 1) {
			if (g_monome_ws.rx_expected == 1)
				mext_dispatch_message();
			g_monome_ws.rx_expected = 0;
			g_monome_ws.rx_len = 0;
		}
		return;
	}

	if (g_monome_ws.rx_len < sizeof(g_monome_ws.rx_buf))
		g_monome_ws.rx_buf[g_monome_ws.rx_len++] = byte;

	if (g_monome_ws.rx_len >= g_monome_ws.rx_expected) {
		mext_dispatch_message();
		g_monome_ws.rx_expected = 0;
		g_monome_ws.rx_len = 0;
	}
}

static void legacy_push_key(uint8_t packed, uint8_t z)
{
	monome_ws_event_t e;
	e.type = MONOME_WS_EVENT_GRID_KEY;
	e.grid.x = packed >> 4;
	e.grid.y = packed & 0x0F;
	e.grid.z = z;
	if (e.grid.x < MONOME_WS_GRID_MAX_X && e.grid.y < MONOME_WS_GRID_MAX_Y) {
		if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_SERIES) {
			if (e.grid.x >= 8 && g_monome_ws.grid_x < 16)
				g_monome_ws.grid_x = 16;
			if (e.grid.y >= 8 && g_monome_ws.grid_y < 16)
				g_monome_ws.grid_y = 16;
		}
		event_push(&e);
	}
}

static void legacy_rx_consume_byte(uint8_t byte)
{
	if (g_monome_ws.rx_expected == 0) {
		if (byte == 0xFF) return;

		if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_SERIES && byte == 0x01)
			set_grid_identity(8, 8, MONOME_WS_PROTOCOL_40H);

		if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_UNKNOWN) {
			if (byte == 0x01) {
				set_grid_identity(8, 8, MONOME_WS_PROTOCOL_40H);
			} else if ((byte & 0xF0) == 0x10 || (byte & 0xF0) == 0x00) {
				set_grid_identity(MONOME_WS_LEGACY_DEFAULT_COLS, MONOME_WS_LEGACY_DEFAULT_ROWS,
					(monome_ws_protocol_t)MONOME_WS_LEGACY_DEFAULT_PROTOCOL);
			} else {
				return;
			}
		}

		g_monome_ws.rx_buf[0] = byte;
		g_monome_ws.rx_len = 1;
		g_monome_ws.rx_expected = 2;
		return;
	}

	g_monome_ws.rx_buf[g_monome_ws.rx_len++] = byte;
	if (g_monome_ws.rx_len < g_monome_ws.rx_expected)
		return;

	uint8_t header = g_monome_ws.rx_buf[0];
	uint8_t payload = g_monome_ws.rx_buf[1];
	if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_SERIES) {
		if ((header & 0xF0) == 0x00)
			legacy_push_key(payload, 1);
		else if ((header & 0xF0) == 0x10)
			legacy_push_key(payload, 0);
	} else if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_40H) {
		if (header == 0x01)
			legacy_push_key(payload, 1);
		else if (header == 0x00)
			legacy_push_key(payload, 0);
	}

	g_monome_ws.rx_expected = 0;
	g_monome_ws.rx_len = 0;
}

void monome_ws_init(monome_ws_transport_t transport, uint8_t device_cdc_itf)
{
	memset(&g_monome_ws, 0, sizeof(g_monome_ws));
	g_monome_ws.cdc_idx = -1;
	g_monome_ws.transport = (uint8_t)transport;
	g_monome_ws.device_cdc_itf = device_cdc_itf;
	g_monome_ws.protocol = MONOME_WS_PROTOCOL_UNKNOWN;
	g_monome_ws.device_kind = MONOME_WS_DEVICE_UNKNOWN;
	g_monome_ws.grid_intensity = 15;
}

void monome_ws_connect(int cdc_idx)
{
	g_monome_ws.cdc_idx = cdc_idx;
	g_monome_ws.connected = true;
	g_monome_ws.protocol = MONOME_WS_PROTOCOL_UNKNOWN;
	g_monome_ws.device_kind = MONOME_WS_DEVICE_UNKNOWN;
	g_monome_ws.supports_levels = false;
	g_monome_ws.grid_x = 0;
	g_monome_ws.grid_y = 0;
	g_monome_ws.arc_enc_count = 0;
	g_monome_ws.rx_len = 0;
	g_monome_ws.rx_expected = 0;
	memset(g_monome_ws.grid_led, 0, sizeof(g_monome_ws.grid_led));
	memset(g_monome_ws.grid_next, 0, sizeof(g_monome_ws.grid_next));
	memset(g_monome_ws.arc_ring, 0, sizeof(g_monome_ws.arc_ring));
	for (int q = 0; q < 4; q++) g_monome_ws.grid_dirty[q] = false;
	g_monome_ws.grid_frame_pending = false;
	g_monome_ws.discovery_tick = 0;
	g_monome_ws.connect_time_us = time_us_64();
	g_monome_ws.force_refresh_until_us = 0;
	g_monome_ws.next_force_refresh_us = 0;
}

void monome_ws_disconnect(void)
{
	g_monome_ws.connected = false;
	g_monome_ws.cdc_idx = -1;
}

bool monome_ws_connected(void) { return g_monome_ws.connected; }
monome_ws_protocol_t monome_ws_protocol(void) { return (monome_ws_protocol_t)g_monome_ws.protocol; }
monome_ws_device_kind_t monome_ws_device_kind(void) { return (monome_ws_device_kind_t)g_monome_ws.device_kind; }
bool __not_in_flash_func(monome_ws_grid_ready)(void) { return g_monome_ws.connected && g_monome_ws.grid_x > 0 && g_monome_ws.grid_y > 0; }
uint8_t monome_ws_grid_cols(void) { return g_monome_ws.grid_x; }
uint8_t monome_ws_grid_rows(void) { return g_monome_ws.grid_y; }
bool monome_ws_grid_supports_levels(void) { return g_monome_ws.supports_levels; }
bool monome_ws_arc_ready(void) { return g_monome_ws.connected && g_monome_ws.arc_enc_count > 0; }
uint8_t monome_ws_arc_encoders(void) { return g_monome_ws.arc_enc_count; }

void monome_ws_grid_led_set(uint8_t x, uint8_t y, uint8_t level)
{
	if (x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return;
	if (level > 15) level = 15;
	g_monome_ws.grid_led[y * MONOME_WS_GRID_MAX_X + x] = level;
	g_monome_ws.grid_dirty[quad_idx(x, y)] = true;
}

void monome_ws_grid_led_all(uint8_t level)
{
	if (level > 15) level = 15;
	memset(g_monome_ws.grid_led, level, sizeof(g_monome_ws.grid_led));
	for (int q = 0; q < 4; q++) g_monome_ws.grid_dirty[q] = true;
}

void monome_ws_grid_led_intensity(uint8_t level)
{
	g_monome_ws.grid_intensity = level & 0x0F;
	g_monome_ws.intensity_pending = true;
}

void monome_ws_grid_all_off(void)
{
	if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_MEXT || g_monome_ws.protocol == MONOME_WS_PROTOCOL_UNKNOWN) {
		const uint8_t buf[] = {0x12};
		monome_ws_send_raw(buf, sizeof(buf));
	} else {
		monome_ws_grid_led_all(0);
	}
}

bool __not_in_flash_func(monome_ws_grid_frame_submit)(const uint8_t *levels)
{
	if (levels == NULL) return false;
	if (g_monome_ws.grid_frame_pending) return false;
	memcpy(g_monome_ws.grid_next, levels, sizeof(g_monome_ws.grid_next));
	__dmb();
	g_monome_ws.grid_frame_pending = true;
	return true;
}

void monome_ws_send_discovery(void)
{
	const uint8_t query_caps[] = {0x00};
	const uint8_t query_id[] = {0x01};
	const uint8_t query_size[] = {0x05};
	monome_ws_send(query_caps, sizeof(query_caps));
	monome_ws_send(query_id, sizeof(query_id));
	monome_ws_send(query_size, sizeof(query_size));
}

static void refresh_mext_grid(void)
{
	if (g_monome_ws.intensity_pending) {
		uint8_t ibuf[2] = {0x17, g_monome_ws.grid_intensity};
		if (monome_ws_send_raw(ibuf, 2))
			g_monome_ws.intensity_pending = false;
	}

	for (uint8_t yo = 0; yo < g_monome_ws.grid_y; yo += 8) {
		for (uint8_t xo = 0; xo < g_monome_ws.grid_x; xo += 8) {
			uint8_t q = quad_idx(xo, yo);
			if (!g_monome_ws.grid_dirty[q]) continue;

			uint8_t buf[35];
			buf[0] = 0x1A;
			buf[1] = xo;
			buf[2] = yo;
			uint8_t *p = buf + 3;
			for (uint8_t r = 0; r < 8; r++) {
				for (uint8_t c = 0; c < 8; c += 2) {
					uint8_t gx0 = xo + c;
					uint8_t gx1 = gx0 + 1;
					uint8_t gy = yo + r;
					uint8_t a = (gx0 < g_monome_ws.grid_x && gy < g_monome_ws.grid_y) ? g_monome_ws.grid_led[gy * MONOME_WS_GRID_MAX_X + gx0] : 0;
					uint8_t b = (gx1 < g_monome_ws.grid_x && gy < g_monome_ws.grid_y) ? g_monome_ws.grid_led[gy * MONOME_WS_GRID_MAX_X + gx1] : 0;
					*p++ = (uint8_t)((a << 4) | (b & 0x0F));
				}
			}
			if (monome_ws_send_raw(buf, sizeof(buf)))
				g_monome_ws.grid_dirty[q] = false;
		}
	}
}

#if MONOME_WS_FORCE_8X8_MONOBRIGHT
static void refresh_mext_binary_grid(void)
{
	if (g_monome_ws.intensity_pending) {
		uint8_t ibuf[2] = {0x17, g_monome_ws.grid_intensity};
		if (monome_ws_send_raw(ibuf, 2))
			g_monome_ws.intensity_pending = false;
	}

	for (uint8_t yo = 0; yo < g_monome_ws.grid_y; yo += 8) {
		for (uint8_t xo = 0; xo < g_monome_ws.grid_x; xo += 8) {
			uint8_t q = quad_idx(xo, yo);
			if (!g_monome_ws.grid_dirty[q]) continue;

			uint8_t buf[11];
			buf[0] = 0x14;
			buf[1] = xo;
			buf[2] = yo;
			for (uint8_t r = 0; r < 8; r++)
				buf[3 + r] = frame_row_mask(xo, yo + r);
			if (monome_ws_send_raw(buf, sizeof(buf)))
				g_monome_ws.grid_dirty[q] = false;
		}
	}
}
#endif

static void refresh_series_grid(void)
{
	if (g_monome_ws.intensity_pending) {
		uint8_t ibuf = (uint8_t)(0xA0 | (g_monome_ws.grid_intensity & 0x0F));
		monome_ws_send_raw(&ibuf, 1);
		g_monome_ws.intensity_pending = false;
	}

	for (uint8_t yo = 0; yo < g_monome_ws.grid_y; yo += 8) {
		for (uint8_t xo = 0; xo < g_monome_ws.grid_x; xo += 8) {
			uint8_t q = quad_idx(xo, yo);
			if (!g_monome_ws.grid_dirty[q]) continue;

			uint8_t buf[9];
			buf[0] = (uint8_t)(0x80 | q);
			for (uint8_t r = 0; r < 8; r++)
				buf[1 + r] = frame_row_mask(xo, yo + r);
			if (monome_ws_send_raw(buf, sizeof(buf)))
				g_monome_ws.grid_dirty[q] = false;
		}
	}
}

static void refresh_40h_grid(void)
{
	if (g_monome_ws.intensity_pending) {
		uint8_t ibuf[2] = {0x30, g_monome_ws.grid_intensity};
		monome_ws_send_raw(ibuf, sizeof(ibuf));
		g_monome_ws.intensity_pending = false;
	}

	if (!g_monome_ws.grid_dirty[0]) return;
	for (uint8_t y = 0; y < 8; y++) {
		uint8_t buf[2] = {(uint8_t)(0x70 | (y & 0x07)), frame_row_mask(0, y)};
		if (!monome_ws_send_raw(buf, sizeof(buf)))
			return;
	}
	g_monome_ws.grid_dirty[0] = false;
}

void monome_ws_grid_refresh(void)
{
	if (!g_monome_ws.connected) return;
	if (g_monome_ws.grid_x == 0 || g_monome_ws.grid_y == 0) return;

	if (g_monome_ws.grid_frame_pending) {
		bool dirty[4] = { false, false, false, false };
		for (uint8_t y = 0; y < g_monome_ws.grid_y; y++) {
			for (uint8_t x = 0; x < g_monome_ws.grid_x; x++) {
				uint16_t idx = (uint16_t)y * MONOME_WS_GRID_MAX_X + x;
				if (g_monome_ws.grid_led[idx] != g_monome_ws.grid_next[idx])
					dirty[quad_idx(x, y)] = true;
			}
		}
		for (uint8_t y = 0; y < g_monome_ws.grid_y; y++) {
			for (uint8_t x = 0; x < g_monome_ws.grid_x; x++) {
				uint8_t q = quad_idx(x, y);
				if (!dirty[q]) continue;
				uint16_t idx = (uint16_t)y * MONOME_WS_GRID_MAX_X + x;
				g_monome_ws.grid_led[idx] = g_monome_ws.grid_next[idx];
			}
		}
		for (int q = 0; q < 4; q++) g_monome_ws.grid_dirty[q] |= dirty[q];
		__dmb();
		g_monome_ws.grid_frame_pending = false;
	}

	uint64_t now_us = time_us_64();
	if (g_monome_ws.transport == MONOME_WS_TRANSPORT_HOST &&
		(now_us - g_monome_ws.connect_time_us) < MONOME_WS_OUTPUT_SETTLE_US) {
		return;
	}

	if (g_monome_ws.force_refresh_until_us != 0 && now_us >= g_monome_ws.next_force_refresh_us) {
		if (now_us <= g_monome_ws.force_refresh_until_us) {
			for (uint8_t yo = 0; yo < g_monome_ws.grid_y; yo += 8) {
				for (uint8_t xo = 0; xo < g_monome_ws.grid_x; xo += 8)
					g_monome_ws.grid_dirty[quad_idx(xo, yo)] = true;
			}
			g_monome_ws.next_force_refresh_us = now_us + MONOME_WS_FORCE_REFRESH_INTERVAL_US;
		} else {
			g_monome_ws.force_refresh_until_us = 0;
		}
	}

	switch (g_monome_ws.protocol) {
	case MONOME_WS_PROTOCOL_SERIES:
		refresh_series_grid();
		break;
	case MONOME_WS_PROTOCOL_40H:
		refresh_40h_grid();
		break;
	case MONOME_WS_PROTOCOL_MEXT:
#if MONOME_WS_FORCE_8X8_MONOBRIGHT
		if (g_monome_ws.supports_levels)
			refresh_mext_grid();
		else
			refresh_mext_binary_grid();
#else
		refresh_mext_grid();
#endif
		break;
	case MONOME_WS_PROTOCOL_UNKNOWN:
	default:
		refresh_mext_grid();
		break;
	}
}

void monome_ws_arc_led_set(uint8_t ring, uint8_t led, uint8_t level)
{
	if (ring >= MONOME_WS_ARC_MAX_ENCODERS || led >= MONOME_WS_ARC_RING_LEDS) return;
	if (level > 15) level = 15;
	g_monome_ws.arc_ring[ring][led] = level;
	g_monome_ws.arc_refresh_pending = true;
}

void monome_ws_arc_led_all(uint8_t ring, uint8_t level)
{
	if (ring >= MONOME_WS_ARC_MAX_ENCODERS) return;
	if (level > 15) level = 15;
	memset(g_monome_ws.arc_ring[ring], level, MONOME_WS_ARC_RING_LEDS);
	g_monome_ws.arc_refresh_pending = true;
}

void monome_ws_arc_led_map(uint8_t ring, const uint8_t *levels)
{
	if (ring >= MONOME_WS_ARC_MAX_ENCODERS || levels == NULL) return;
	memcpy(g_monome_ws.arc_ring[ring], levels, MONOME_WS_ARC_RING_LEDS);
	g_monome_ws.arc_refresh_pending = true;
}

void monome_ws_arc_led_intensity(uint8_t ring, uint8_t level)
{
	(void)ring;
	(void)level;
}

void monome_ws_rx_feed(const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		bool legacy_sniff_ready =
			g_monome_ws.protocol == MONOME_WS_PROTOCOL_UNKNOWN &&
			(time_us_64() - g_monome_ws.connect_time_us) > MONOME_WS_LEGACY_SNIFF_DELAY_US;

		if (legacy_sniff_ready && data[i] == 0x10) {
			g_monome_ws.rx_len = 0;
			g_monome_ws.rx_expected = 0;
			legacy_rx_consume_byte(data[i]);
		} else if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_SERIES || g_monome_ws.protocol == MONOME_WS_PROTOCOL_40H) {
			legacy_rx_consume_byte(data[i]);
		} else {
			mext_rx_consume_byte(data[i]);
		}
	}
}

bool __not_in_flash_func(monome_ws_event_pop)(monome_ws_event_t *out)
{
	if (g_monome_ws.events.r == g_monome_ws.events.w)
		return false;
	*out = g_monome_ws.events.buf[g_monome_ws.events.r];
	g_monome_ws.events.r = (g_monome_ws.events.r + 1) & MONOME_WS_EVENT_QUEUE_MASK;
	return true;
}

uint8_t __not_in_flash_func(monome_ws_event_backlog)(void)
{
	return (uint8_t)((g_monome_ws.events.w - g_monome_ws.events.r) & MONOME_WS_EVENT_QUEUE_MASK);
}

void monome_ws_task(void)
{
	if (g_monome_ws.transport == MONOME_WS_TRANSPORT_HOST)
		tuh_task();

	if (!g_monome_ws.connected)
		return;

	if (g_monome_ws.transport == MONOME_WS_TRANSPORT_HOST) {
		uint8_t buf[64];
		uint32_t n = tuh_cdc_read((uint8_t)g_monome_ws.cdc_idx, buf, sizeof(buf));
		if (n > 0)
			monome_ws_rx_feed(buf, n);
	} else {
		if (tud_mounted()) {
			uint8_t buf[64];
			uint32_t n = tud_cdc_n_read(g_monome_ws.device_cdc_itf, buf, sizeof(buf));
			if (n > 0)
				monome_ws_rx_feed(buf, n);
		}
	}

	if (g_monome_ws.protocol == MONOME_WS_PROTOCOL_UNKNOWN && g_monome_ws.grid_x == 0 && g_monome_ws.arc_enc_count == 0) {
		uint64_t elapsed_us = time_us_64() - g_monome_ws.connect_time_us;
		uint32_t next_tick = g_monome_ws.discovery_tick + 1u;
		if ((elapsed_us > 200000u && g_monome_ws.discovery_tick == 0) ||
		    (elapsed_us > 1000000u && elapsed_us > (uint64_t)next_tick * 1000000u)) {
			g_monome_ws.discovery_tick = next_tick;
			monome_ws_send_discovery();
		}
	}

	uint64_t now_us = time_us_64();
	if (now_us - s_last_refresh_us >= MONOME_WS_REFRESH_US) {
		s_last_refresh_us = now_us;
		monome_ws_grid_refresh();
	}
}

void tuh_cdc_mount_cb(uint8_t idx)
{
	if (g_monome_ws.transport != MONOME_WS_TRANSPORT_HOST)
		return;
	if (!g_monome_ws.connected)
		monome_ws_connect((int)idx);
}

void tuh_cdc_umount_cb(uint8_t idx)
{
	if (g_monome_ws.transport != MONOME_WS_TRANSPORT_HOST)
		return;
	if (g_monome_ws.cdc_idx == (int)idx)
		monome_ws_disconnect();
}

void tuh_cdc_rx_cb(uint8_t idx) { (void)idx; }
void tuh_cdc_tx_complete_cb(uint8_t idx) { (void)idx; }
