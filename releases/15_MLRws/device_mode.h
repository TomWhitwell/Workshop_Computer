/**
 * device_mode.h — USB device-side services: protocol detection + sample manager.
 *
 * Protocol detection: at boot, waits for a CDC host to connect and examines
 * the first byte to determine whether the host is a sample manager or a
 * monome mext grid proxy.
 *
 * Sample manager protocol (binary, over CDC serial):
 *   Command byte followed by parameters.
 *
 *   'I' (0x49)         → Info: respond with <len4>, then metadata text
 *                         "MLR1 ... FW <version>\n" + track info + "END\n"
 *   'R' <track>        → Read track: respond with <len4>, then stream header
 *                         + ADPCM data in 1024-byte chunks. Host sends 'A'
 *                         after each chunk, then firmware sends "DONE\n".
 *   'E' <track>        → Erase track (header sector only)
 *   'P' <track> <en>   → Set whether CUT keys on this populated track drive CV outputs
 *   'W' <track> <len4> → Write track: respond "OK\n" when ready, then
 *                         receive <data...>, respond "OK\n" on completion
 *   'S'                → Status: respond with "OK\n" or "BUSY\n"
 *   'X'                → Sync/cancel: abort any in-progress operation,
 *                         respond "SYNC\n"
 *
 *   Read stream aborts if any byte other than chunk ACK 'A' is received
 *   mid-stream, responding "SYNC\n".
 *
 *   Write/drain data has a 5-second inactivity timeout, responding
 *   "TIMEOUT\n" on expiry.
 *
 * All multi-byte integers are little-endian.
 */

#ifndef DEVICE_MODE_H
#define DEVICE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Protocol detection                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
	DEVICE_PROTO_NONE = 0,        /* timeout — nothing connected */
	DEVICE_PROTO_SAMPLE_MGR = 1,  /* first byte was a sample-manager command */
	DEVICE_PROTO_MEXT = 2,        /* first byte was mext, or host connected but silent */
} device_protocol_t;

typedef struct {
	device_protocol_t protocol;
	uint8_t first_byte;           /* the consumed byte (valid when has_pending_byte) */
	bool    has_pending_byte;     /* true if a byte was consumed during detection */
} device_detect_result_t;

/**
 * Detect which protocol the USB host is speaking.
 *
 * Polls tud_task() in a loop, waiting up to @p timeout_ms for a CDC
 * connection and first byte.  Must be called after board_init() + tud_init()
 * and before launching core 1.
 */
device_detect_result_t device_mode_detect_protocol(uint32_t timeout_ms);

/* ------------------------------------------------------------------ */
/* Sample manager                                                     */
/* ------------------------------------------------------------------ */

/** Initialise sample-manager state. */
void device_mode_init(void);

/** Inject a byte that was already consumed (e.g. during protocol detection).
 *  It will be returned by the next internal CDC read. */
void device_mode_inject_byte(uint8_t byte);

/** Poll sample-manager services (call from core 1 loop). */
void device_mode_task(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_MODE_H */
