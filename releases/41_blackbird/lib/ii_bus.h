#pragma once

// ii (I2C) bus for a Workshop Computer card: no dependency on the card, its
// language or crow. On the UART pads, ComputerCard moves I2C0 there
// (EnableI2COverUART(); COMPUTERCARD_II_ON_UART_PINS in blackbird's copy),
// and the card passes those pins here.
//
// Leader: non-blocking. A transaction (write, then optionally a read) fits in
// the controller's 16-entry FIFOs, so it's loaded in one go and the card's
// main loop polls for the end. Nothing here waits on the bus, except that
// recovering from a timeout (a stuck bus) takes about 1ms.
//
// Follower (optional): answers at one address, from the I2C interrupt on the
// core that called ii_bus_follow(). Messages a leader writes are queued for
// the main loop (ii_bus_receive); a leader reading from us gets an answer from
// the card's request callback, straight away or later (ii_bus_reply), the
// bus held meanwhile. The RP2040's I2C can't follow and lead at once: while
// a leader transaction of ours is in flight, our address doesn't answer.

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define II_BUS_MAX_BYTES 16  // write + read bytes per transaction (FIFO depth)

typedef enum {
    II_BUS_IDLE,     // nothing in flight: ii_bus_start() can go
    II_BUS_BUSY,     // in flight
    II_BUS_DONE,     // finished; any read bytes are in the buffer passed to poll
    II_BUS_NAK,      // nobody answered at that address (or a data byte wasn't acked)
    II_BUS_TIMEOUT,  // didn't finish in time (stuck bus, or a follower holding SCL): aborted
    II_BUS_ERROR,    // other abort (arbitration lost, ...)
} ii_bus_status_t;

typedef struct {
    i2c_inst_t* i2c;   // already initialised and on its pins (ComputerCard does this)
    uint8_t sda_pin;
    uint8_t scl_pin;
} ii_bus_config_t;

void ii_bus_init(const ii_bus_config_t* config);

// Start a transaction to 7-bit `address`: write `tx_len` bytes, then read
// `rx_len` (0 for none). Returns false if busy or the lengths don't fit.
bool ii_bus_start(uint8_t address, const uint8_t* tx, uint8_t tx_len, uint8_t rx_len);

// Advance the transaction in flight. DONE/NAK/TIMEOUT/ERROR are each returned
// once, then the driver is IDLE again. On DONE, read bytes go to `rx`.
ii_bus_status_t ii_bus_poll(uint8_t* rx);

bool ii_bus_idle(void);

// --- follower ---

#define II_BUS_DEFER (-1)

// A leader is reading from us. `msg`, `len`: what it wrote earlier in the same
// transfer, or, for a read on its own, the last message it wrote to us (the
// request half of a two-step query, as Teletype does them; that message was
// also queued for ii_bus_receive). len 0: nothing written yet. Fill `reply`
// (up to II_BUS_MAX_BYTES) and return its length, or return II_BUS_DEFER to
// hold the bus (clock stretched) until ii_bus_reply(). Called from the I2C
// interrupt: be quick.
typedef int (*ii_bus_request_fn)(const uint8_t* msg, uint8_t len, uint8_t* reply);

// Answer at 7-bit `address` (0: stop answering)
void ii_bus_follow(uint8_t address, ii_bus_request_fn request);

// The next message a leader wrote to us (its length), or 0 if none. Main loop.
uint8_t ii_bus_receive(uint8_t* msg);

// Answer the request held by II_BUS_DEFER. Ignored if the leader gave up.
void ii_bus_reply(const uint8_t* reply, uint8_t len);

void ii_bus_set_fast(bool fast);      // 400kHz, else 100kHz (crow's default)
void ii_bus_set_pullups(bool on);     // the RP2040's weak internal pull-ups

#ifdef __cplusplus
}
#endif
