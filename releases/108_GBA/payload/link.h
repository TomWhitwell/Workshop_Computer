// link.h — the GBA side of the serial link: a NORMAL-mode 32-bit SLAVE, externally clocked.
//
// The slave can only have ONE transfer pending: the hardware clears SIO_START when the host
// finishes clocking a word, and until we re-arm it we are deaf. The rule that keeps the link
// alive has not changed — never block, and re-arm the instant a word lands.
#pragma once

#include <stdint.h>
#include "../gba_proto.h"

// Set to 1 to let the serial IRQ re-arm the slave the instant a word lands, so the console is
// no longer deaf for the length of a redraw.
//
// The polled path is NOT removed when this is on — link_service() still runs the same body
// with interrupts briefly masked, which makes the two mutually exclusive rather than racing.
// That is deliberate. If the IRQ never fires (a bad vector, an unset enable) the payload
// degrades to exactly the polled behaviour that has worked all along, instead of going deaf
// and looking like a failed multiboot. Belt and braces on the one mechanism we cannot debug
// from the bench.
#ifndef LINK_USE_IRQ
#define LINK_USE_IRQ 1
#endif

// ---- what the host is sending us ----
#define LINK_IN_CV1  0
#define LINK_IN_CV2  1
#define LINK_IN_AUD1 2
#define LINK_IN_AUD2 3

extern volatile uint16_t g_in[4];        // 0..4095, 2048 = 0 V
extern volatile uint8_t  g_gate;         // Pulse In 2 level
// Monotonic count of gate rising edges, NOT a sticky flag. link_pump() can run from the serial
// interrupt, so a flag the synth clears would have two writers and the losing interleaving is
// exactly the one that matters: an edge arriving between the read and the clear would vanish.
// The synth keeps its own tally and advances it by one per edge, so nothing is lost or merged.
extern volatile uint32_t g_gateEdges;
extern volatile uint16_t g_knob[3];      // 0..4095
extern volatile uint8_t  g_switch;       // 0 down, 1 middle, 2 up
extern volatile uint16_t g_hostCaps;     // GBA_CAP_* from the host's HELLO
extern volatile uint8_t  g_hostSeen;     // a HELLO has arrived
extern volatile uint32_t g_rx;           // every word, good or bad
extern volatile uint32_t g_streamRx;     // valid stream words
extern volatile uint32_t g_streamBad;    // words rejected by tag or check bits

// Per-kind receive counters. Kept, but no longer displayed: they were added to settle "the knobs
// do not work", which has at least three completely different causes that look identical from
// the front panel — the host not sending, the words not arriving, or core 0 never updating the
// values in the first place. Counting each kind separated them in one glance. The fault turned
// out to be core 0 blocked inside multicore_launch_core1(), and the counters cost three
// increments, so they stay for the next time that question comes up.
extern volatile uint32_t g_ctlRx;        // CONTROL words decoded, any opcode
extern volatile uint32_t g_knobRx;       // GBA_OP_KNOB specifically
extern volatile uint32_t g_swRx;         // GBA_OP_SWITCH specifically

void link_init(void);
void link_set_buttons(uint16_t b);

// ---- patch transfer -------------------------------------------------------------------------
// One byte per link word in either direction, each carrying its own index, so a lost word leaves
// a hole the receiver can see rather than silently shifting everything after it. A save repeats
// the whole block until the host acknowledges, which makes the transfer self-healing without an
// explicit retransmit protocol.
#define LINK_XFER_NONE 0
#define LINK_XFER_SAVE 1
#define LINK_XFER_LOAD 2

#define LINK_RESULT_IDLE  0
#define LINK_RESULT_OK    1
#define LINK_RESULT_EMPTY 2
// The bytes arrived but were not a patch. Worth its own code: a host/GBA disagreement about the
// wire layout delivered 256 zeroes, the magic check quietly threw them away, and the page still
// said OK - so a load that changed nothing looked exactly like a load that had nothing to change.
#define LINK_RESULT_BAD   3

extern volatile uint8_t g_xferState;                       // LINK_XFER_*
extern volatile uint8_t g_xferSlot;
extern volatile uint8_t g_xferResult;                      // LINK_RESULT_*
extern volatile uint8_t g_loadReady;                       // a LOAD landed; the UI clears it
extern volatile uint16_t g_slotMask;                       // which host slots hold a patch
extern volatile uint8_t g_patchBuf[GBA_PATCH_SLOT_BYTES];  // staging for both directions

void link_begin_save(uint8_t slot, const uint8_t *data, int len);
void link_begin_load(uint8_t slot);
int  link_xfer_progress(void);      // 0..100 while a transfer runs
void link_post_note(uint8_t note, uint8_t gate);
void link_post_status(uint8_t page, uint8_t mode, uint8_t flags);

// Harvest a completed word and re-arm. Safe to call from anywhere at any time; under
// LINK_USE_IRQ it masks interrupts around the body so it can never collide with the handler.
// Usually finds the transfer still pending and returns immediately.
void link_service(void);

// True if the serial IRQ has actually fired at least once. Shown on the CAL page: it is the
// difference between "the IRQ is doing the work" and "we silently fell back to polling".
int link_irq_alive(void);

// ---- diagnostic modes (diagnostics/linkrate.uf2, diagnostics/bandwidth.uf2) --------------
// These share this same payload image. Deleting them would silently break the instruments
// that characterised the link, which is how we would re-measure after any regression.
#define BENCH_ENTER   0xBE7CBE7Cu
#define BENCH_LEAVE   0xBE7C0000u
#define LT_SEQ_MAGIC  0xA5A5u
#define LT_RATE_MAGIC 0xA6A6u

extern volatile int      g_bench;
extern volatile int      g_benchDirty;
extern volatile int      g_linkTest;
extern volatile uint32_t g_ltRate;
extern volatile uint32_t g_ltVal;
extern volatile uint32_t g_ltFailAt;
extern volatile int      g_ltFailed;
extern volatile uint32_t g_ltPassOk;
extern volatile uint32_t g_ltPassBad;
extern volatile uint32_t g_ltBadMagic;
extern volatile int      g_ltMeasuring;
extern volatile int      g_ltPassDone;
extern volatile int      g_ltArmed;
extern volatile uint32_t g_ltSkips;
extern volatile uint32_t g_ltWild;
extern volatile int      g_ltHave;

// Tight service loop used by the diagnostics' frozen measuring window. Under IRQ mode this
// just burns time while the interrupt does the work, which is exactly what it needs to do.
void link_spin(uint32_t iterations);
