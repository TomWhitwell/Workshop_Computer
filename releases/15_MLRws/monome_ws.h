/**
 * monome_ws.h - lightweight monome grid/arc driver for RP2040.
 *
 * Public app-facing API for Computer cards. Apps submit normalized 0-15
 * grid/arc LED levels and consume normalized events; this driver owns USB
 * byte transport, mext discovery, and legacy series/40h protocol translation.
 */

#ifndef MONOME_WS_H
#define MONOME_WS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONOME_WS_GRID_MAX_X    16
#define MONOME_WS_GRID_MAX_Y    16
#define MONOME_WS_ARC_MAX_ENCODERS 4
#define MONOME_WS_ARC_RING_LEDS 64

#define MONOME_WS_EVENT_QUEUE_SIZE 128
#if (MONOME_WS_EVENT_QUEUE_SIZE & (MONOME_WS_EVENT_QUEUE_SIZE - 1)) != 0
#error "MONOME_WS_EVENT_QUEUE_SIZE must be a power of two"
#endif
#define MONOME_WS_EVENT_QUEUE_MASK (MONOME_WS_EVENT_QUEUE_SIZE - 1)

typedef enum {
	MONOME_WS_TRANSPORT_HOST = 0,
	MONOME_WS_TRANSPORT_DEVICE = 1,
} monome_ws_transport_t;

typedef enum {
	MONOME_WS_PROTOCOL_UNKNOWN = 0,
	MONOME_WS_PROTOCOL_MEXT,
	MONOME_WS_PROTOCOL_SERIES,
	MONOME_WS_PROTOCOL_40H,
} monome_ws_protocol_t;

typedef enum {
	MONOME_WS_DEVICE_UNKNOWN = 0,
	MONOME_WS_DEVICE_GRID,
	MONOME_WS_DEVICE_ARC,
} monome_ws_device_kind_t;

typedef enum {
	MONOME_WS_EVENT_GRID_KEY,
	MONOME_WS_EVENT_ARC_DELTA,
	MONOME_WS_EVENT_ARC_KEY,
} monome_ws_event_type_t;

typedef struct {
	monome_ws_event_type_t type;
	union {
		struct { uint8_t x, y, z; } grid;
		struct { uint8_t n; int8_t delta; } arc;
		struct { uint8_t n; uint8_t z; } arc_key;
	};
} monome_ws_event_t;

typedef struct {
	monome_ws_event_t buf[MONOME_WS_EVENT_QUEUE_SIZE];
	volatile uint8_t w;
	volatile uint8_t r;
} monome_ws_event_queue_t;

typedef struct {
	volatile bool     connected;
	volatile int      cdc_idx;
	uint8_t           device_cdc_itf;
	uint8_t           transport;
	uint8_t           protocol;
	uint8_t           device_kind;
	bool              supports_levels;

	uint8_t           grid_x;
	uint8_t           grid_y;
	uint8_t           grid_led[MONOME_WS_GRID_MAX_X * MONOME_WS_GRID_MAX_Y];
	uint8_t           grid_next[MONOME_WS_GRID_MAX_X * MONOME_WS_GRID_MAX_Y];
	bool              grid_dirty[4];
	volatile bool     grid_frame_pending;
	uint8_t           grid_intensity;
	bool              intensity_pending;

	uint8_t           arc_enc_count;
	uint8_t           arc_ring[MONOME_WS_ARC_MAX_ENCODERS][MONOME_WS_ARC_RING_LEDS];
	bool              arc_refresh_pending;

	uint8_t           rx_buf[64];
	uint8_t           rx_len;
	uint8_t           rx_expected;

	uint32_t          discovery_tick;
	uint64_t          connect_time_us;
	uint64_t          force_refresh_until_us;
	uint64_t          next_force_refresh_us;

	monome_ws_event_queue_t events;
} monome_ws_state_t;

extern monome_ws_state_t g_monome_ws;

void monome_ws_init(monome_ws_transport_t transport, uint8_t device_cdc_itf);
void monome_ws_task(void);
void monome_ws_connect(int cdc_idx);
void monome_ws_disconnect(void);
void monome_ws_rx_feed(const uint8_t *data, uint32_t len);
void monome_ws_send_discovery(void);

bool monome_ws_connected(void);
monome_ws_protocol_t monome_ws_protocol(void);
monome_ws_device_kind_t monome_ws_device_kind(void);

bool monome_ws_grid_ready(void);
uint8_t monome_ws_grid_cols(void);
uint8_t monome_ws_grid_rows(void);
bool monome_ws_grid_supports_levels(void);
void monome_ws_grid_led_set(uint8_t x, uint8_t y, uint8_t level);
void monome_ws_grid_led_all(uint8_t level);
void monome_ws_grid_led_intensity(uint8_t level);
void monome_ws_grid_refresh(void);
bool monome_ws_grid_frame_submit(const uint8_t *levels);
void monome_ws_grid_all_off(void);

bool monome_ws_arc_ready(void);
uint8_t monome_ws_arc_encoders(void);
void monome_ws_arc_led_set(uint8_t ring, uint8_t led, uint8_t level);
void monome_ws_arc_led_all(uint8_t ring, uint8_t level);
void monome_ws_arc_led_map(uint8_t ring, const uint8_t *levels);
void monome_ws_arc_led_intensity(uint8_t ring, uint8_t level);

bool monome_ws_event_pop(monome_ws_event_t *out);
uint8_t monome_ws_event_backlog(void);

#ifdef __cplusplus
}
#endif

#endif /* MONOME_WS_H */
