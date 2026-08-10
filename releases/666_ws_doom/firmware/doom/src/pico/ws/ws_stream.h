//
// Copyright(C) 2026 ws-doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//   USB CDC frame/status streaming to the companion web app,
//   and key-event reception from it.
//

#ifndef WS_STREAM_H
#define WS_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// device -> host packet types (framed AB CD <type> <len16le> <payload>)
#define WS_PKT_PALETTE    0x02  // 768 bytes RGB
#define WS_PKT_STATUS     0x03  // health i16, armor i16, ammo i16, weapon u8, level u8
#define WS_PKT_TEXT       0x04  // utf-8 log
#define WS_PKT_LINE       0x05  // line u8, then RLE pairs (count u8, index u8)
#define WS_PKT_FRAME_END  0x06  // frame counter u8

void ws_stream_init(void);

// pump tinyusb + parse received key packets; core 0 thread context only
void ws_stream_poll(void);

// host connected (DTR set)?
bool ws_stream_active(void);

// write one framed packet; blocks pumping USB (and doom audio) until queued
void ws_stream_packet(uint8_t type, const void *payload, uint16_t len);

// rate-limited player status packet; call once per frame
void ws_stream_status_tick(void);

// printf-style log line: buffered in a small ring, sent as TEXT packets;
// the backlog is replayed whenever the host connects
void ws_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
