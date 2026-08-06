#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SysEx command IDs. Manufacturer ID is 0x7D (educational/non-commercial).
// All messages: F0 7D <cmd> <payload> F7
//
// Inbound (browser → card):
//   0x01  set step pitch       payload: step (0..7), pitch (0..127)
//   0x02  set step velocity    payload: step (0..7), v_hi (0..15), v_lo (0..15)
//   0x03  set sequence length  payload: length (2..8)
//   0x04  set play/pause       payload: playing (0/1)
//   0x05  request full dump    payload: (empty)
//
// Outbound (card → browser):
//   0x10  full state dump      payload: 26 bytes (see midi_send_full_dump)
//   0x11  current step tick    payload: step (0..7)
//   0x12  parameter update     payload: cmd echo (0x01..0x04 + their args)

#define DRUMDRUM_SYSEX_MFR              0x7D

#define DRUMDRUM_SYSEX_SET_PITCH        0x01
#define DRUMDRUM_SYSEX_SET_VELOCITY     0x02
#define DRUMDRUM_SYSEX_SET_LENGTH       0x03
#define DRUMDRUM_SYSEX_SET_PLAYING      0x04
#define DRUMDRUM_SYSEX_REQUEST_DUMP     0x05

#define DRUMDRUM_SYSEX_FULL_DUMP        0x10
#define DRUMDRUM_SYSEX_TICK             0x11
#define DRUMDRUM_SYSEX_PARAM_UPDATE     0x12

// Called from the Core 1 USB loop. Drains incoming MIDI, parses SysEx,
// applies updates to gState, and sends outbound state messages to the
// browser when the host is mounted.
void midi_device_task(void);

#ifdef __cplusplus
}
#endif
