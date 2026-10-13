// gba_link.h — Cross-core shared state + the core-1 GBA link engine.
//
// Core 1 owns the physical link: it runs multiboot, then the post-boot polling loop, streaming
// the module's inputs down and dispatching whatever the GBA sends back. Core 0 (ComputerCard's
// 48 kHz ProcessSample) only ever touches GbaShared — never the SPI.
//
// CONCURRENCY MODEL: every field has a SINGLE WRITER. `volatile` plus word-aligned accesses
// no wider than 32 bits are atomic enough on RP2040 for this, and we never need a consistent
// multi-field snapshot.
//
// The one place that rule was awkward is the gate edge. A "sticky flag set by core 0, cleared
// by core 1" has two writers, and the interleaving that loses a set is exactly the one that
// matters — a trigger arriving as the flag is cleared would vanish. So it is a COUNTER that
// only core 0 ever increments; core 1 keeps its own tally of how many it has reported and
// sends one edge per increment. No update can be lost, and a burst is reported in order.
#pragma once

#include <cstdint>

// GBA button bitfield, active-high (already de-inverted from REG_KEYINPUT by the payload).
// Matches the standard GBA key bit order so the payload and host agree.
enum GbaKey : uint16_t {
    GBA_A      = 1u << 0,
    GBA_B      = 1u << 1,
    GBA_SELECT = 1u << 2,
    GBA_START  = 1u << 3,
    GBA_RIGHT  = 1u << 4,
    GBA_LEFT   = 1u << 5,
    GBA_UP     = 1u << 6,
    GBA_DOWN   = 1u << 7,
    GBA_R      = 1u << 8,
    GBA_L      = 1u << 9,
};

enum class LinkState : uint8_t {
    Idle,        // not started
    Connecting,  // running multiboot handshake
    Booted,      // payload uploaded, polling loop running
    Error,       // multiboot failed; will retry
};

// Index order matches the payload's g_in[] and the MAP page.
enum GbaInput : uint8_t { GBA_IN_CV1 = 0, GBA_IN_CV2 = 1, GBA_IN_AUD1 = 2, GBA_IN_AUD2 = 3 };

struct GbaShared {
    // ---- core 0 -> core 1 : what we send the instrument ----
    volatile uint16_t inputs[4]  = { 2048, 2048, 2048, 2048 };  // 12-bit unsigned, 2048 = 0 V
    volatile uint8_t  gate       = 0;    // Pulse In 2 level
    volatile uint32_t gateEdges  = 0;    // monotonic count of rising edges; see the note above
    volatile uint16_t knobs[3]   = { 0, 0, 0 };
    volatile uint8_t  switchPos  = 1;
    volatile uint16_t caps       = 0;    // GBA_CAP_* announced to the GBA in HELLO

    // ---- core 1 -> core 0 : what the instrument tells us ----
    volatile uint16_t  buttons   = 0;    // current GBA key state (GbaKey bits)
    volatile uint8_t   note      = 60;   // MIDI note the GBA is sounding -> CV Out 2
    volatile uint8_t   noteVel   = 0;    // 0 = note off, else the envelope level
    volatile uint8_t   page      = 0;    // edit page the GBA is showing
    volatile uint8_t   mode      = 0;    // GBA_MODE_PLAY / GBA_MODE_EDIT
    volatile uint8_t   flags     = 0;    // GBA_FLAG_*
    volatile uint32_t  rxSeq     = 0;    // increments on each well-formed reply (liveness)
    volatile uint32_t  rxBad     = 0;    // replies rejected by the tag check
    volatile LinkState state     = LinkState::Idle;
    volatile uint8_t   lastError = 0;    // MultibootResult of the last failed attempt
    volatile uint32_t  linkHz    = 0;    // SCK rate that multiboot actually succeeded at
    volatile uint16_t  patchMask = 0;    // which of the 16 flash slots hold a patch
};

// The single shared instance (defined in gba_link.cpp).
extern GbaShared gGba;

// Core-1 entry point. Init the SPI transport, upload the payload (retrying on NoGBA), then
// loop forever streaming inputs and dispatching replies. Never returns.
void gba_link_core1(const uint8_t *payload, uint32_t payload_size);
