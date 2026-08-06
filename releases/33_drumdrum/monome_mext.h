/**
 * monome_mext.h — Lightweight mext protocol for monome grid/arc on RP2040.
 *
 * Vendored verbatim from dessertplanet/Workshop_Computer (MLRws release).
 * Direct byte-level mext TX/RX, modelled on dessertplanet/viii and
 * monome/ansible.  No libmonome dependency — uses flat byte arrays
 * and pads all writes to 64 bytes with 0xFF for USB bulk alignment,
 * which is critical for compatibility with both modern (CDC/RP2040)
 * and older (FTDI) grids.
 *
 * Transport: TinyUSB CDC host on core 1.
 */

#ifndef MONOME_MEXT_H
#define MONOME_MEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Grid / Arc dimensions                                              */
/* ------------------------------------------------------------------ */
#define MEXT_MAX_GRID_X   16
#define MEXT_MAX_GRID_Y   16
#define MEXT_MAX_ENCODERS  4
#define MEXT_RING_LEDS    64

/* ------------------------------------------------------------------ */
/* Event types                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
	MEXT_EVENT_GRID_KEY,
	MEXT_EVENT_ENC_DELTA,
	MEXT_EVENT_ENC_KEY,
} mext_event_type_t;

typedef struct {
	mext_event_type_t type;
	union {
		struct { uint8_t x, y, z; } grid;
		struct { uint8_t n; int8_t delta; } enc;
		struct { uint8_t n; uint8_t z; } enc_key;
	};
} mext_event_t;

/* ------------------------------------------------------------------ */
/* Event queue (lock-free single-producer / single-consumer ring)     */
/* ------------------------------------------------------------------ */
#define MEXT_EVENT_QUEUE_SIZE 128

typedef struct {
	mext_event_t buf[MEXT_EVENT_QUEUE_SIZE];
	volatile uint8_t w;
	volatile uint8_t r;
} mext_event_queue_t;

/* ------------------------------------------------------------------ */
/* Device state (one instance, shared between cores)                  */
/* ------------------------------------------------------------------ */
typedef struct {
	/* connection */
	volatile bool     connected;
	volatile int      cdc_idx;        /* TinyUSB CDC index, -1 = none */
	uint8_t           device_cdc_itf; /* TinyUSB device CDC interface */
	uint8_t           transport;      /* mext_transport_t */

	/* grid */
	uint8_t           grid_x;         /* columns (0 until discovered) */
	uint8_t           grid_y;         /* rows */
	uint8_t           grid_led[MEXT_MAX_GRID_X * MEXT_MAX_GRID_Y];
	uint8_t           grid_next[MEXT_MAX_GRID_X * MEXT_MAX_GRID_Y];
	bool              grid_dirty[4];  /* one flag per 8x8 quadrant */
	volatile bool     grid_frame_pending;
	uint8_t           grid_intensity;
	bool              intensity_pending;

	/* arc */
	uint8_t           arc_enc_count;
	uint8_t           arc_ring[MEXT_MAX_ENCODERS][MEXT_RING_LEDS];
	bool              arc_refresh_pending;
	bool              is_arc;

	/* RX parser state */
	uint8_t           rx_buf[64];
	uint8_t           rx_len;
	uint8_t           rx_expected;

	/* discovery retry counter (ticked from USB loop) */
	uint32_t          discovery_tick;
	uint64_t          connect_time_us;

	/* event queue (grid→core0) */
	mext_event_queue_t events;
} mext_state_t;

typedef enum {
	MEXT_TRANSPORT_HOST = 0,
	MEXT_TRANSPORT_DEVICE = 1,
} mext_transport_t;

/* Global device state */
extern mext_state_t g_mext;

/* ------------------------------------------------------------------ */
/* Init / connection                                                  */
/* ------------------------------------------------------------------ */

void mext_init(mext_transport_t transport, uint8_t device_cdc_itf);
void mext_task(void);
bool mext_grid_ready(void);
void mext_connect(int cdc_idx);
void mext_disconnect(void);
void mext_send_discovery(void);

/* ------------------------------------------------------------------ */
/* TX — LED output                                                    */
/* ------------------------------------------------------------------ */

void mext_grid_led_set(uint8_t x, uint8_t y, uint8_t level);
void mext_grid_led_all(uint8_t level);
void mext_grid_led_intensity(uint8_t level);
void mext_grid_refresh(void);
bool mext_grid_frame_submit(const uint8_t *levels);
void mext_grid_all_off(void);

/* ------------------------------------------------------------------ */
/* RX                                                                 */
/* ------------------------------------------------------------------ */

void mext_rx_feed(const uint8_t *data, uint32_t len);

/* ------------------------------------------------------------------ */
/* Event queue                                                        */
/* ------------------------------------------------------------------ */

bool mext_event_pop(mext_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MONOME_MEXT_H */
