// gba_proto.h — the wire protocol, shared verbatim by the RP2040 firmware and the GBA payload.
//
// ONE header, included by both sides, so the two can never drift. The payload is C and the
// firmware is C++: plain #define and static inline only — no namespaces, no enum class, no
// <cstdint>.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// THE ARCHITECTURE THIS ENCODES
//
// The Workshop senses; the GBA is the instrument. The Workshop streams its five raw inputs
// down and the GBA owns pitch tracking, the modulation matrix, the sound and the whole UI.
// That is why there is no "set duty" or "set envelope" opcode here and never needs to be:
// re-mapping Audio In 1 from detune to vibrato is a change to a table in GBA RAM. Had the
// mapping lived on the Workshop side, every edit page would have needed its own opcode.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// DOWNSTREAM (RP2040 -> GBA), 32-bit words, MSB first
//
//   [31:29] = 0b100  STREAM — the hot path: the current state of the inputs, 1000x a second
//             [28]     Pulse In 2 gate LEVEL
//             [27]     Pulse In 2 rising EDGE since the previous stream word (sticky)
//             [26]     pair select: 0 = {CV In 1, CV In 2}, 1 = {Audio In 1, Audio In 2}
//             [25:14]  value A, 12-bit unsigned (signed -2048..2047 biased by +2048)
//             [13:2]   value B, 12-bit unsigned
//             [1:0]    check bits (see gba_stream_check)
//
//   [31:29] = 0b000  CONTROL — everything that is not the hot path
//             [28:24]  opcode (0 is reserved so an all-zero word is never a valid message)
//             [23:0]   argument
//
//   [31:29] = 0b101  RESERVED for the diagnostics. Do not allocate.
//
// WHY 0b100 FOR STREAM, AND WHY 0b101 IS FENCED OFF
//   The three diagnostic magics already burned into diagnostics/linkrate.cpp and the payload
//   — 0xA5A5, 0xA6A6 and 0xBE7C — all begin 101 in their top three bits. Giving STREAM the
//   tag 100 makes a collision with them structurally impossible rather than merely unlikely.
//   Under a weaker tag a live stream word would impersonate the ramp magic roughly every 30
//   seconds at 1 kHz and throw the instrument into link-test mode mid-performance.
//
// THE STICKY EDGE BIT [27] IS NOT AN OPTIMISATION
//   A trigger shorter than the 1 ms poll interval would otherwise fall between two polls and
//   be silently swallowed. Core 0 sets this flag at 48 kHz on any rising edge of Pulse In 2;
//   core 1 clears it as it packs the word. A 100 us trigger still fires a note.
//
// THE RULE THIS PROJECT LEARNED TWICE
//   A CONTROL word obeys exactly the same inter-word gap as a STREAM word. The rock-steady
//   "dropped 2, corrupt 4" baseline that survived a 20x rate range was a rate-announce word
//   sent without its own gap, destroying the word behind it. Out-of-band does not mean
//   out-of-timing. See CLAUDE.md and diagnostics/POSTMORTEM.md.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// UPSTREAM (GBA -> RP2040)
//
// Only one word comes back per poll, so the message kinds share the channel. THE 16-BIT TAG
// IS THE KIND — which is what lets 0x600D keep its exact present meaning, so every existing
// diagnostic (linkrate, linkcheck, bandwidth) keeps parsing replies with no change at all.
#pragma once

#include <stdint.h>

#define GBA_PROTO_VERSION 1

// ---- downstream tags (top three bits) ---------------------------------------------------
#define GBA_TAG_SHIFT       29
#define GBA_TAG_MASK        0x7u
#define GBA_TAG_CONTROL     0x0u
#define GBA_TAG_STREAM      0x4u
#define GBA_TAG_DIAGNOSTIC  0x5u        // 0xA5A5 / 0xA6A6 / 0xBE7C live here. Reserved.

#define GBA_TAG_OF(w)       (((w) >> GBA_TAG_SHIFT) & GBA_TAG_MASK)

// ---- STREAM ------------------------------------------------------------------------------
#define GBA_ST_GATE         (1u << 28)
#define GBA_ST_EDGE         (1u << 27)
#define GBA_ST_PAIR         (1u << 26)   // 0 = {CV1, CV2}, 1 = {Audio1, Audio2}

#define GBA_ST_A(w)         (((w) >> 14) & 0xFFFu)
#define GBA_ST_B(w)         (((w) >>  2) & 0xFFFu)

// Two check bits over payload bits [28:2]. Every shift below is EVEN, so an input bit at an
// even position can only ever affect output bit 0 and one at an odd position only output
// bit 1 — the fold is exactly a parity split by position. That makes every single-bit error
// detectable, and a one-place bit-slip (which disturbs many bits at once) is caught with
// probability 3/4 on top of almost always breaking the three-bit tag as well.
//
// This matters more than it did during bring-up: a corrupted word used to be a counter
// increment, and is now an audible pitch glitch. A rejected word simply holds the last value.
static inline uint32_t gba_stream_check(uint32_t w)
{
    uint32_t p = (w >> 2) & 0x7FFFFFFu;   // bits [28:2], 27 bits
    p ^= p >> 16;
    p ^= p >> 8;
    p ^= p >> 4;
    p ^= p >> 2;
    return p & 0x3u;
}

// Build a stream word. `a` and `b` are 12-bit unsigned; `gate`/`edge` are 0 or non-zero.
static inline uint32_t gba_stream_pack(int pair, int gate, int edge, uint32_t a, uint32_t b)
{
    uint32_t w = ((uint32_t)GBA_TAG_STREAM << GBA_TAG_SHIFT)
               | (gate ? GBA_ST_GATE : 0u)
               | (edge ? GBA_ST_EDGE : 0u)
               | (pair ? GBA_ST_PAIR : 0u)
               | ((a & 0xFFFu) << 14)
               | ((b & 0xFFFu) <<  2);
    return w | gba_stream_check(w);
}

static inline int gba_stream_valid(uint32_t w)
{
    return GBA_TAG_OF(w) == GBA_TAG_STREAM && (w & 0x3u) == gba_stream_check(w);
}

// ---- CONTROL -----------------------------------------------------------------------------
#define GBA_CTL_OP(w)       (((w) >> 24) & 0x1Fu)
#define GBA_CTL_ARG(w)      ((w) & 0xFFFFFFu)

#define GBA_OP_NONE         0x00u   // reserved: an all-zero word must never mean anything
#define GBA_OP_PING         0x01u
#define GBA_OP_HELLO        0x02u   // arg: [23:16] proto version, [15:0] capability flags
#define GBA_OP_KNOB         0x03u   // arg: [23:22] index, [21:10] 12-bit value
#define GBA_OP_SWITCH       0x04u   // arg: 0 = down, 1 = middle, 2 = up

// ---- patch transfer, host -> GBA (a LOAD) ----
// One byte per word, each carrying its own index, so a dropped word leaves a hole the receiver
// can see rather than silently shifting everything after it. 232 bytes is about a quarter of a
// second at the 1 kHz poll rate, which is a fine price for a deliberate action.
#define GBA_OP_PATCH        0x05u   // arg: [23:16] byte index, [15:8] byte value
#define GBA_OP_PATCH_DONE   0x06u   // arg: [7:0] slot, bit 7 set = slot was empty
#define GBA_OP_PATCH_ACK    0x07u   // arg: [7:0] slot — host has stored a complete patch
#define GBA_OP_SLOTS        0x08u   // arg: [15:0] bitmask of slots that hold a patch

// Capability flags carried by GBA_OP_HELLO. The GBA shows these on its CAL page so you can
// tell a tuning problem from an uncalibrated module without opening the source.
#define GBA_CAP_CVOUT_CAL   (1u << 0)   // ComputerCard::CVOutsCalibrated() was true

static inline uint32_t gba_control_pack(uint32_t op, uint32_t arg)
{
    return ((uint32_t)GBA_TAG_CONTROL << GBA_TAG_SHIFT)
         | ((op & 0x1Fu) << 24) | (arg & 0xFFFFFFu);
}

static inline uint32_t gba_hello_pack(uint32_t caps)
{
    return gba_control_pack(GBA_OP_HELLO, ((uint32_t)GBA_PROTO_VERSION << 16) | (caps & 0xFFFFu));
}

static inline uint32_t gba_knob_pack(uint32_t idx, uint32_t val12)
{
    return gba_control_pack(GBA_OP_KNOB, ((idx & 0x3u) << 22) | ((val12 & 0xFFFu) << 10));
}

// ---- UPSTREAM tags -----------------------------------------------------------------------
// The low bytes form a distance-3 code {0x0D, 0x1E, 0x2A, 0x33}: no single flipped bit can
// turn one kind into another, so a mangled reply is rejected rather than misinterpreted as a
// different message. 0x600D is fixed by compatibility — the rest were chosen around it.
#define GBA_UP_BUTTONS      0x600Du   // [15:0]  button bits (GbaKey layout). UNCHANGED.
#define GBA_UP_STATUS       0x601Eu   // [15:12] edit page, [11:8] mode, [7:0] flags
#define GBA_UP_NOTE         0x602Au   // [15:8]  MIDI note, [7:0] gate/velocity
#define GBA_UP_PARAM        0x6033u   // [15:8]  param id, [7:0] value
#define GBA_UP_PATCH        0x6047u   // [15:8]  byte index, [7:0] byte value (a SAVE)
#define GBA_UP_REQ          0x6058u   // [15:12] request, [11:8] slot

#define GBA_UP_TAG(w)       ((w) >> 16)
#define GBA_UP_DATA(w)      ((w) & 0xFFFFu)

static inline uint32_t gba_up_pack(uint32_t tag, uint32_t data16)
{
    return (tag << 16) | (data16 & 0xFFFFu);
}

// Modes reported in GBA_UP_STATUS [11:8].
#define GBA_MODE_PLAY       0u
#define GBA_MODE_EDIT       1u

// GBA_UP_REQ requests, in [15:12].
#define GBA_REQ_SAVE        1u       // "take the patch I am about to stream and store it in slot"
#define GBA_REQ_LOAD        2u       // "send me the patch in slot"
#define GBA_REQ_SAVE_END    3u       // "that was the whole patch"

// Patch storage geometry. 16 slots of 256 bytes is exactly one 4 kB flash sector on the RP2040,
// which matters: the sector is the erase unit, so a whole sector is read, modified and rewritten
// for every save. Keep sizeof(Patch) under PATCH_SLOT_BYTES.
#define GBA_PATCH_SLOTS      16
#define GBA_PATCH_SLOT_BYTES 256

// Status flags, GBA_UP_STATUS [7:0].
#define GBA_FLAG_NOTE_ON    (1u << 0)
#define GBA_FLAG_GATE       (1u << 1)
#define GBA_FLAG_LATCH      (1u << 2)
