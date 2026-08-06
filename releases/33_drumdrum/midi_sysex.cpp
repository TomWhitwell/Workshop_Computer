// SysEx parser/encoder for the WebMIDI editor.
// Runs entirely on Core 1; reads/writes gState directly (single-byte
// fields are atomic on the M0+).

#include "midi_sysex.h"
#include "shared_state.h"

#include "tusb.h"
#include <string.h>

// ── Inbound SysEx accumulator ─────────────────────────────────
// SysEx bodies for this card are tiny (max ~5 bytes payload), so a
// 32-byte buffer is plenty. If we ever exceed it the parser drops the
// message and resyncs at the next 0xF0.
static uint8_t  rx_buf[32];
static uint16_t rx_len     = 0;
static bool     rx_in_msg  = false;

// ── Outbound state mirror ─────────────────────────────────────
// We send param updates whenever Core 1's snapshot diverges from gState.
// This catches changes from any source (panel knobs, future Grid mode,
// our own SysEx writes) with a single polling loop.
struct Mirror {
    uint8_t  pitch[8];
    uint8_t  velocity[8];
    uint8_t  seqLength;
    uint8_t  playing;
    uint8_t  currentStep;
    uint32_t tickEpoch;
    bool     dumped;            // initial dump sent for current connection
    bool     was_mounted;
};
static Mirror mirror = {};

// ── Helpers ────────────────────────────────────────────────────
static void send_sysex(const uint8_t *payload, uint16_t len)
{
    if (!tud_midi_mounted()) return;

    uint8_t buf[40];
    if ((size_t)(len + 3) > sizeof(buf)) return;  // belt-and-braces

    buf[0] = 0xF0;
    buf[1] = DRUMDRUM_SYSEX_MFR;
    memcpy(&buf[2], payload, len);
    buf[2 + len] = 0xF7;

    tud_midi_stream_write(0, buf, len + 3);
}

static void send_full_dump(void)
{
    // Layout: cmd, seqLength, playing, currentStep, pitch[0..7],
    //         v_hi[0], v_lo[0], v_hi[1], v_lo[1], ... v_hi[7], v_lo[7]
    // Total payload bytes (after F0 7D): 1 + 3 + 8 + 16 = 28.
    uint8_t p[28];
    p[0] = DRUMDRUM_SYSEX_FULL_DUMP;
    p[1] = gState.seqLength;
    p[2] = gState.playing;
    p[3] = gState.currentStep;
    for (int i = 0; i < 8; i++) p[4 + i] = gState.pitch[i] & 0x7F;
    for (int i = 0; i < 8; i++) {
        uint8_t v = gState.velocity[i];
        p[12 + 2*i + 0] = (v >> 4) & 0x0F;
        p[12 + 2*i + 1] = v & 0x0F;
    }
    send_sysex(p, sizeof(p));
}

static void send_param_pitch(uint8_t step)
{
    uint8_t p[4] = { DRUMDRUM_SYSEX_PARAM_UPDATE,
                     DRUMDRUM_SYSEX_SET_PITCH,
                     step,
                     (uint8_t)(gState.pitch[step] & 0x7F) };
    send_sysex(p, sizeof(p));
}

static void send_param_velocity(uint8_t step)
{
    uint8_t v = gState.velocity[step];
    uint8_t p[5] = { DRUMDRUM_SYSEX_PARAM_UPDATE,
                     DRUMDRUM_SYSEX_SET_VELOCITY,
                     step,
                     (uint8_t)((v >> 4) & 0x0F),
                     (uint8_t)(v & 0x0F) };
    send_sysex(p, sizeof(p));
}

static void send_param_length(void)
{
    uint8_t p[3] = { DRUMDRUM_SYSEX_PARAM_UPDATE,
                     DRUMDRUM_SYSEX_SET_LENGTH,
                     gState.seqLength };
    send_sysex(p, sizeof(p));
}

static void send_param_playing(void)
{
    uint8_t p[3] = { DRUMDRUM_SYSEX_PARAM_UPDATE,
                     DRUMDRUM_SYSEX_SET_PLAYING,
                     (uint8_t)(gState.playing ? 1 : 0) };
    send_sysex(p, sizeof(p));
}

static void send_tick(uint8_t step)
{
    uint8_t p[2] = { DRUMDRUM_SYSEX_TICK, step };
    send_sysex(p, sizeof(p));
}

// ── Inbound dispatch ──────────────────────────────────────────
static void apply_sysex(const uint8_t *body, uint16_t len)
{
    // body[] is everything between F0 and F7, exclusive.
    // body[0] = manufacturer, body[1] = command, then payload.
    if (len < 2) return;
    if (body[0] != DRUMDRUM_SYSEX_MFR) return;

    uint8_t cmd = body[1];
    switch (cmd) {
    case DRUMDRUM_SYSEX_SET_PITCH:
        if (len >= 4) {
            uint8_t step  = body[2] & 0x07;
            uint8_t pitch = body[3] & 0x7F;
            gState.pitch[step] = pitch;
        }
        break;

    case DRUMDRUM_SYSEX_SET_VELOCITY:
        if (len >= 5) {
            uint8_t step = body[2] & 0x07;
            uint8_t hi   = body[3] & 0x0F;
            uint8_t lo   = body[4] & 0x0F;
            gState.velocity[step] = (uint8_t)((hi << 4) | lo);
        }
        break;

    case DRUMDRUM_SYSEX_SET_LENGTH:
        if (len >= 3) {
            uint8_t v = body[2];
            if (v < 1) v = 1;
            if (v > 8) v = 8;
            gState.seqLength = v;
        }
        break;

    case DRUMDRUM_SYSEX_SET_PLAYING:
        if (len >= 3) {
            gState.playing = body[2] ? 1 : 0;
        }
        break;

    case DRUMDRUM_SYSEX_REQUEST_DUMP:
        send_full_dump();
        // Bring our mirror back in sync so we don't immediately re-send
        // every parameter as a "change".
        memcpy(mirror.pitch,    (const void*)gState.pitch,    8);
        memcpy(mirror.velocity, (const void*)gState.velocity, 8);
        mirror.seqLength   = gState.seqLength;
        mirror.playing     = gState.playing;
        mirror.currentStep = gState.currentStep;
        mirror.tickEpoch   = gState.tickEpoch;
        break;

    default:
        break;
    }
}

static void feed_byte(uint8_t b)
{
    if (b == 0xF0) {
        rx_in_msg = true;
        rx_len    = 0;
    } else if (b == 0xF7) {
        if (rx_in_msg) apply_sysex(rx_buf, rx_len);
        rx_in_msg = false;
        rx_len    = 0;
    } else if (rx_in_msg) {
        if (rx_len < sizeof(rx_buf)) {
            rx_buf[rx_len++] = b;
        } else {
            // Overflow — drop the rest of this message
            rx_in_msg = false;
            rx_len    = 0;
        }
    }
}

// ── Public entry ──────────────────────────────────────────────
void midi_device_task(void)
{
    // 1. Detect host (re)connection — re-send the full dump so the
    //    browser can populate its UI from a known starting point.
    bool mounted_now = tud_midi_mounted();
    if (mounted_now && !mirror.was_mounted) {
        mirror.dumped = false;
    }
    mirror.was_mounted = mounted_now;

    if (mounted_now && !mirror.dumped) {
        send_full_dump();
        memcpy(mirror.pitch,    (const void*)gState.pitch,    8);
        memcpy(mirror.velocity, (const void*)gState.velocity, 8);
        mirror.seqLength   = gState.seqLength;
        mirror.playing     = gState.playing;
        mirror.currentStep = gState.currentStep;
        mirror.tickEpoch   = gState.tickEpoch;
        mirror.dumped      = true;
    }

    // 2. Drain inbound MIDI bytes, byte-stream API.
    uint8_t in_buf[64];
    while (tud_midi_available()) {
        uint32_t n = tud_midi_stream_read(in_buf, sizeof(in_buf));
        for (uint32_t i = 0; i < n; i++) feed_byte(in_buf[i]);
    }

    if (!mounted_now) return;

    // 3. Tick notification — fires whenever core 0 advances a step.
    if (gState.tickEpoch != mirror.tickEpoch) {
        mirror.tickEpoch   = gState.tickEpoch;
        mirror.currentStep = gState.currentStep;
        send_tick(mirror.currentStep);
    }

    // 4. Param diff — pushes panel-knob and switch changes to the browser.
    for (int i = 0; i < 8; i++) {
        uint8_t v = gState.pitch[i];
        if (v != mirror.pitch[i]) {
            mirror.pitch[i] = v;
            send_param_pitch((uint8_t)i);
        }
    }
    for (int i = 0; i < 8; i++) {
        uint8_t v = gState.velocity[i];
        if (v != mirror.velocity[i]) {
            mirror.velocity[i] = v;
            send_param_velocity((uint8_t)i);
        }
    }
    if (gState.seqLength != mirror.seqLength) {
        mirror.seqLength = gState.seqLength;
        send_param_length();
    }
    if (gState.playing != mirror.playing) {
        mirror.playing = gState.playing;
        send_param_playing();
    }
}
