#pragma once

// Minimal class-compliant USB MIDI host for Music Thing 8mu support.
//
// Pico SDK 2.2.0 ships TinyUSB 0.18, which does not include a MIDI host
// class driver — only a descriptor-parser hint enabled via CFG_TUH_MIDI.
// We register our own driver via TinyUSB's usbh_app_driver_get_cb()
// extension point and read 32-bit USB-MIDI Event Packets directly off
// the bulk-IN endpoint. CC messages are dispatched into gState.
//
// Mapping (channel-agnostic):
//
//   Factory 8mu defaults (work plug-and-play, nothing to configure):
//     CC 34..41    faders 1–8   step pitches (or velocities when mode=1)
//     Note 36 (C2) button 1     toggle pitch ↔ velocity edit mode
//     Note 48 (C3) button 2     toggle play/pause
//     Note 60 (C4) button 3     reset to step 1
//     Note 72 (C5) button 4     randomize all 8 step pitches + velocities
//
//   Configured-in-8mu-web-editor alt mappings (parallel to the factory):
//     CC 50..57    faders       step velocities (always)
//     CC 28        fader        edit cursor (0..7)
//     CC 22..24    buttons      rising edge mirrors of notes 36/48/60
//                               (no CC equivalent of randomize today)
//
//   All other messages (CC 25–27, 29–33, 42–49, other notes, sysex,
//   pitch bend, etc.) are silently dropped.

#ifdef __cplusplus
extern "C" {
#endif

void midi_host_init(void);

#ifdef __cplusplus
}
#endif
