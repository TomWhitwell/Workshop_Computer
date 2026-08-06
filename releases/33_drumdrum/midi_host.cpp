// Minimal class-compliant USB MIDI host driver for Music Thing 8mu.
// See midi_host.h for the CC dispatch table.

#include "midi_host.h"
#include "shared_state.h"

#include "tusb.h"
#include "host/usbh_pvt.h"
#include "class/midi/midi.h"
#include "pico/time.h"

#include <stdint.h>
#include <string.h>

// ── Driver-private state ─────────────────────────────────────────────
// Only one MIDI device tracked at a time. The Workshop Computer's front
// jack is a single port; a hub could in principle bring more, but v1
// only listens to the first MIDI device that mounts.
static volatile bool s_connected     = false;
static uint8_t       s_dev_addr      = 0;
static uint8_t       s_ep_in         = 0;       // bulk-IN endpoint address (with direction bit)
static uint16_t      s_ep_in_size    = 64;      // bulk-IN max packet size
static uint8_t       s_last_itf      = TUSB_INDEX_INVALID_8; // highest interface we claimed

// Per-button rising-edge state (CC value <64 → ≥64 = press).
static uint8_t       s_btn22_prev    = 0;
static uint8_t       s_btn23_prev    = 0;
static uint8_t       s_btn24_prev    = 0;

// USB-MIDI Event Packets are 4 bytes each. 64 bytes = up to 16 events
// per IN transfer, which is plenty: an 8mu sweeps a fader at maybe
// 100 Hz max.
static uint8_t       s_rx_buf[64];

// ── Helpers ──────────────────────────────────────────────────────────
static inline bool button_press_edge(uint8_t* prev, uint8_t value) {
    bool press = (value >= 64) && (*prev < 64);
    *prev = value;
    return press;
}

// Core-1-local xorshift32 PRNG used for the randomize button. Distinct
// from the audio ISR's PRNG on Core 0 — they don't need to share state,
// and a separate generator means a randomize press doesn't perturb the
// white-noise stream. Seeded lazily on first use from the hardware us
// counter, which has effectively unbounded entropy by the time someone
// presses a button.
static uint32_t s_rng = 0;
static uint32_t midi_xorshift(void) {
    if (s_rng == 0) {
        s_rng = time_us_32();
        if (s_rng == 0) s_rng = 0xDEADBEEF;
    }
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

// Re-roll all 8 step pitches and velocities. Matches the constructor's
// boot randomisation in main.cpp: pitches in C2..B4 (the musical sweet
// spot of the MIDI range) and velocities biased toward the upper half
// so every step stays audible. Bumps tickEpoch once at the end so a
// future browser/Grid replug sees the new pattern in one go.
//
// Single-byte writes are atomic on M0+, so the audio ISR reading a
// pitch/velocity mid-randomize may see one new value and one old —
// that's fine. At 48 kHz the whole loop is over in under a hundred
// samples; sonically it's an instant scramble, which is what we want.
static void randomize_pattern(void) {
    for (int i = 0; i < 8; i++) {
        gState.pitch[i]    = (uint8_t)(36 + (midi_xorshift() % 36));
        gState.velocity[i] = (uint8_t)(100 + (midi_xorshift() % 156));
    }
}

static void handle_cc(uint8_t cc, uint8_t v) {
    bool changed = false;
    if (cc >= 34 && cc <= 41) {
        const uint8_t i = (uint8_t)(cc - 34);
        if (gState.midiHostVelocityMode) {
            gState.velocity[i] = (uint8_t)(v << 1);
        } else {
            gState.pitch[i] = v;
        }
        changed = true;
    } else if (cc >= 50 && cc <= 57) {
        gState.velocity[cc - 50] = (uint8_t)(v << 1);
        changed = true;
    } else if (cc == 28) {
        uint8_t step = (uint8_t)((v * 8u) >> 7);
        if (step > 7) step = 7;
        gState.editStep = step;
        changed = true;
    } else if (cc == 22) {
        if (button_press_edge(&s_btn22_prev, v)) {
            gState.midiHostVelocityMode ^= 1;
            changed = true;
        }
    } else if (cc == 23) {
        if (button_press_edge(&s_btn23_prev, v)) {
            gState.playing ^= 1;
            changed = true;
        }
    } else if (cc == 24) {
        if (button_press_edge(&s_btn24_prev, v)) {
            // Mirrors the Pulse In 2 reset: jump to step 1. currentStep
            // is normally written only by Core 0, but a single-byte
            // store from Core 1 is atomic on the M0+ — and the audio
            // ISR's only "reaction" is to display/play step 0, which
            // is exactly what we want.
            gState.currentStep = 0;
            changed = true;
        }
    }
    if (changed) gState.tickEpoch++;
}

// 8mu factory button mappings. The 8mu firmware sends note-on with a
// velocity byte for press and either a note-off OR a note-on with
// velocity 0 (running-status convention) for release. We act on press
// only — drop anything with velocity 0.
static void handle_note_on(uint8_t note, uint8_t vel) {
    if (vel == 0) return;
    bool changed = true;
    switch (note) {
        case 36:  // button 1 — C2
            gState.midiHostVelocityMode ^= 1;
            break;
        case 48:  // button 2 — C3
            gState.playing ^= 1;
            break;
        case 60:  // button 3 — C4 (middle C)
            // Mirrors Pulse In 2 reset (and the CC 24 alt mapping).
            gState.currentStep = 0;
            break;
        case 72:  // button 4 — C5
            randomize_pattern();
            break;
        default:
            changed = false;
            break;
    }
    if (changed) gState.tickEpoch++;
}

static void rearm_rx(void) {
    if (!s_connected || s_ep_in == 0) return;
    if (!usbh_edpt_claim(s_dev_addr, s_ep_in)) return;
    uint16_t const len = (s_ep_in_size > sizeof(s_rx_buf))
        ? (uint16_t)sizeof(s_rx_buf) : s_ep_in_size;
    if (!usbh_edpt_xfer(s_dev_addr, s_ep_in, s_rx_buf, len)) {
        // The internal usbh_edpt_xfer (class-driver-level) does NOT
        // roll back the claim on failure — only the app-level
        // tuh_edpt_xfer does. Without an explicit release here, a
        // transient submit failure would leave the IN endpoint
        // permanently claimed and every subsequent rearm_rx() would
        // bail at the claim step above, silently stopping all MIDI
        // input until the 8mu is unplugged. Caught by Codex review
        // on PR #8.
        usbh_edpt_release(s_dev_addr, s_ep_in);
    }
}

static void clear_state(void) {
    s_connected   = false;
    s_dev_addr    = 0;
    s_ep_in       = 0;
    s_ep_in_size  = 64;
    s_last_itf    = TUSB_INDEX_INVALID_8;
    s_btn22_prev  = 0;
    s_btn23_prev  = 0;
    s_btn24_prev  = 0;
}

// ── Public API ───────────────────────────────────────────────────────
extern "C" void midi_host_init(void) {
    clear_state();
}

// ── TinyUSB class-driver callbacks ───────────────────────────────────
//
// Driver lifecycle:
//   driver_init       — once at host stack init.
//   driver_open       — when a matching interface descriptor arrives
//                       during enumeration. We claim two interfaces
//                       (Audio-Control + MIDIStreaming) by parsing
//                       through to the bulk endpoints and opening them.
//   driver_set_config — once the device is in CONFIGURED state. We
//                       kick off the first IN read here, then signal
//                       the host stack we're done with this interface.
//   driver_xfer_cb    — every time a bulk-IN packet completes.
//   driver_close      — on disconnect.

static bool driver_init(void) {
    clear_state();
    return true;
}

static bool driver_deinit(void) {
    clear_state();
    return true;
}

static bool driver_open(uint8_t rhport, uint8_t dev_addr,
                        tusb_desc_interface_t const* itf_desc, uint16_t max_len) {
    (void)rhport;

    // We only claim Audio-class interfaces. The descriptor parser at
    // usbh.c:1681 groups Audio-Control (subclass 1) + MIDIStreaming
    // (subclass 3) together via assoc_itf_count=2 when CFG_TUH_MIDI=1.
    // Some devices skip Audio-Control entirely, so accept either entry.
    if (itf_desc->bInterfaceClass != TUSB_CLASS_AUDIO) return false;
    if (itf_desc->bInterfaceSubClass != 1 /* AUDIO_SUBCLASS_CONTROL */ &&
        itf_desc->bInterfaceSubClass != 3 /* AUDIO_SUBCLASS_MIDI_STREAMING */) {
        return false;
    }

    // Only one MIDI device at a time.
    if (s_connected) return false;

    uint8_t const* p_desc = (uint8_t const*)itf_desc;
    uint8_t const* p_end  = p_desc + max_len;

    // Walk forward to the MIDIStreaming interface. If we entered on
    // MIDIStreaming directly, the very first iteration matches.
    // Stash its interface number so set_config can tell the host stack
    // to resume past our claimed group — see driver_set_config below.
    uint8_t ms_itf_num = TUSB_INDEX_INVALID_8;
    while (p_desc < p_end) {
        if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) {
            tusb_desc_interface_t const* itf = (tusb_desc_interface_t const*)p_desc;
            if (itf->bInterfaceClass    == TUSB_CLASS_AUDIO &&
                itf->bInterfaceSubClass == 3 /* MIDIStreaming */) {
                ms_itf_num = itf->bInterfaceNumber;
                p_desc = tu_desc_next(p_desc);
                break;
            }
        }
        p_desc = tu_desc_next(p_desc);
    }
    if (ms_itf_num == TUSB_INDEX_INVALID_8) return false;

    // Now scan for the bulk endpoints. Stop at the next interface.
    uint8_t  ep_in = 0, ep_out = 0;
    uint16_t ep_in_size = 64;
    while (p_desc < p_end) {
        uint8_t const dt = tu_desc_type(p_desc);
        if (dt == TUSB_DESC_INTERFACE) {
            break;
        } else if (dt == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const* ep = (tusb_desc_endpoint_t const*)p_desc;
            if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
                if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                    if (!tuh_edpt_open(dev_addr, ep)) return false;
                    ep_in      = ep->bEndpointAddress;
                    ep_in_size = tu_edpt_packet_size(ep);
                } else {
                    if (!tuh_edpt_open(dev_addr, ep)) return false;
                    ep_out = ep->bEndpointAddress;
                }
            }
        }
        p_desc = tu_desc_next(p_desc);
    }
    if (ep_in == 0) return false;  // need at least an IN endpoint
    (void)ep_out; // OUT not used in v1 (no SysEx writes back to 8mu)

    s_dev_addr   = dev_addr;
    s_ep_in      = ep_in;
    s_ep_in_size = ep_in_size;
    s_last_itf   = ms_itf_num;
    return true;
}

static bool driver_set_config(uint8_t dev_addr, uint8_t itf_num) {
    (void)itf_num;
    s_connected = true;
    rearm_rx();
    // Tell the host stack this driver has finished configuring. The
    // stack's loop advances `itf_num++` and resumes searching for the
    // next driver from there (usbh.c:1740). When our driver claimed
    // both AudioControl + MIDIStreaming, both interface numbers map
    // back to us in dev->itf2drv[], so we MUST return the highest
    // claimed interface number — passing TUSB_INDEX_INVALID_8 (0xFF)
    // would wrap to 0, find our driver again, and recurse until the
    // stack overflows. The TinyUSB header note for IAD-binding drivers
    // says exactly this: "should return itf_num + 1 when complete".
    usbh_driver_set_config_complete(dev_addr, s_last_itf);
    return true;
}

static bool driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                           xfer_result_t result, uint32_t xferred_bytes) {
    (void)dev_addr;
    if (ep_addr != s_ep_in) {
        // OUT-completion or stray callback — ignore.
        return true;
    }
    if (result == XFER_RESULT_SUCCESS) {
        // Parse 32-bit USB-MIDI Event Packets. Each packet:
        //   byte 0: (cable_num << 4) | code_index_number
        //   bytes 1..3: MIDI message bytes
        // 8mu has only one cable, but we accept any cable index.
        for (uint32_t i = 0; i + 4 <= xferred_bytes; i += 4) {
            uint8_t const cin = (uint8_t)(s_rx_buf[i] & 0x0F);
            if (cin == MIDI_CIN_CONTROL_CHANGE) {
                handle_cc((uint8_t)(s_rx_buf[i + 2] & 0x7F),
                          (uint8_t)(s_rx_buf[i + 3] & 0x7F));
            } else if (cin == MIDI_CIN_NOTE_ON) {
                // 8mu's factory button defaults send these.
                handle_note_on((uint8_t)(s_rx_buf[i + 2] & 0x7F),
                               (uint8_t)(s_rx_buf[i + 3] & 0x7F));
            }
            // CIN 8 (NOTE_OFF), sysex, pitch bend, etc. are dropped.
        }
    }
    // Always re-arm: a stalled or aborted xfer should still try again.
    rearm_rx();
    return true;
}

static void driver_close(uint8_t dev_addr) {
    if (s_dev_addr == dev_addr) {
        clear_state();
    }
}

// Aggregate-initialised in field order to avoid the C++ designated-init
// extension. Match the order of usbh_class_driver_t in usbh_pvt.h:
//   name, init, deinit, open, set_config, xfer_cb, close.
static usbh_class_driver_t const s_drivers[] = {
    {
        "MIDI",
        driver_init,
        driver_deinit,
        driver_open,
        driver_set_config,
        driver_xfer_cb,
        driver_close,
    },
};

extern "C" usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    *driver_count = (uint8_t)(sizeof(s_drivers) / sizeof(s_drivers[0]));
    return s_drivers;
}
