//
// Copyright(C) 2026 ws-doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//   USB CDC transport: framed packets to the web app, key events back.
//

#include "config.h"

#include <string.h>

#include "pico/stdlib.h"
#include "ws_stream.h"

#if !WS_USB
// USB compiled out; standalone play needs none of this.
void ws_stream_init(void) {}
void ws_stream_poll(void) {}
bool ws_stream_active(void) { return false; }
void ws_stream_packet(uint8_t type, const void *payload, uint16_t len) {
    (void) type; (void) payload; (void) len;
}
void ws_stream_status_tick(void) {}
void ws_stream_unwedge(void) {}
void ws_log(const char *fmt, ...) { (void) fmt; }
#else

#include <stdio.h>
#include <stdarg.h>

#include "hardware/timer.h"
#include "tusb.h"

#include "doomtype.h"
#include "d_event.h"
#include "doomstat.h"
#include "d_items.h"
#include "ws_hw.h"

// tud_task is pumped from thread context only (ws_stream_poll): the boot
// gate services it continuously until the player starts the game, and the
// game loop paths keep an established connection alive afterwards.
void ws_stream_init(void) {
    tusb_init();
}

// suppress streaming until this time if the host stalls
static uint64_t ws_muted_until;

bool ws_stream_active(void) {
    return tud_inited() && tud_cdc_connected() && time_us_64() >= ws_muted_until;
}

// ---------------------------------------------------------------- RX

static void ws_handle_key(uint8_t key, uint8_t down) {
    event_t ev;
    ev.type = down ? ev_keydown : ev_keyup;
    ev.data1 = key;
    ev.data2 = down ? key : 0;
    // give menus the typed character for save-game names etc.
    ev.data3 = (down && key >= 32 && key < 127) ? key : 0;
    D_PostEvent(&ev);
}

static bool ws_settings_announce;

static void ws_parse_rx(void) {
    // packets: BA <type> <a> <b>
    static uint8_t buf[4];
    static uint8_t have;
    while (tud_cdc_available()) {
        uint8_t c;
        if (tud_cdc_read(&c, 1) != 1) break;
        if (have == 0 && c != 0xBA) continue;
        buf[have++] = c;
        if (have == 4) {
            have = 0;
            switch (buf[1]) {
                case 0x01: // key event
                    ws_handle_key(buf[2], buf[3]);
                    break;
                case 0x02: // music volume
                    ws_settings_set(0, buf[2]);
                    ws_settings_announce = true;
                    break;
                case 0x03: // effects volume
                    ws_settings_set(1, buf[2]);
                    ws_settings_announce = true;
                    break;
                case 0x04: { // warp to episode/map
                    extern void G_DeferedInitNew(skill_t skill, int episode, int map, boolean net);
                    int ep = buf[2] < 1 ? 1 : buf[2];
                    int map = buf[3] < 1 ? 1 : (buf[3] > 9 ? 9 : buf[3]);
                    G_DeferedInitNew(sk_medium, ep, map, false);
                    break;
                }
                case 0x05: // commit settings to flash
                    ws_settings_request_save();
                    break;
                case 0x06: // turn sensitivity
                    ws_settings_set(2, buf[2]);
                    ws_settings_announce = true;
                    break;
                case 0x07: // move sensitivity
                    ws_settings_set(3, buf[2]);
                    ws_settings_announce = true;
                    break;
                case 0x08: { // refresh: resend palette + settings
                    extern void ws_video_resend_palette(void);
                    ws_video_resend_palette();
                    ws_settings_announce = true;
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------- log ring

#define WS_LOG_SLOTS 14
#define WS_LOG_LEN 72
static char ws_log_ring[WS_LOG_SLOTS][WS_LOG_LEN];
static uint8_t ws_log_head, ws_log_count;
static bool ws_host_seen;

void ws_log(const char *fmt, ...) {
    char *slot = ws_log_ring[(ws_log_head + ws_log_count) % WS_LOG_SLOTS];
    if (ws_log_count < WS_LOG_SLOTS) {
        ws_log_count++;
    } else {
        ws_log_head = (ws_log_head + 1) % WS_LOG_SLOTS;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(slot, WS_LOG_LEN, fmt, args);
    va_end(args);
    printf("[ws] %s\n", slot);
    if (tud_inited() && tud_cdc_connected()) {
        uint16_t n = 0;
        while (slot[n] && n < WS_LOG_LEN - 1) n++;
        ws_stream_packet(WS_PKT_TEXT, slot, n);
    }
}

static void ws_log_replay(void) {
    for (uint8_t i = 0; i < ws_log_count; i++) {
        const char *slot = ws_log_ring[(ws_log_head + i) % WS_LOG_SLOTS];
        uint16_t n = 0;
        while (slot[n] && n < WS_LOG_LEN - 1) n++;
        ws_stream_packet(WS_PKT_TEXT, slot, n);
    }
}

// ---------------------------------------------------------------- poll

static bool in_poll;

// called by ws_panic: if the fatal error hit while a poll was in flight,
// the re-entry guard would silently disable USB in the panic loop
void ws_stream_unwedge(void) {
    in_poll = false;
}

void ws_stream_poll(void) {
    if (in_poll || !tud_inited()) return;
    in_poll = true;
    tud_task();
    ws_parse_rx();
    bool connected = tud_cdc_connected();
    in_poll = false;
    // replay the log backlog once per connection (outside the in_poll
    // guard: the packet writer re-enters ws_stream_poll while draining)
    if (connected && !ws_host_seen) {
        ws_host_seen = true;
        ws_log_replay();
        ws_settings_announce = true;
        // the palette only streams on change; a fresh host needs it now
        extern void ws_video_resend_palette(void);
        ws_video_resend_palette();
    } else if (!connected) {
        ws_host_seen = false;
    }
    if (connected && ws_settings_announce) {
        ws_settings_announce = false;
        uint8_t pkt[4] = {ws_music_vol, ws_sfx_vol, ws_turn_sens, ws_move_sens};
        ws_stream_packet(0x07 /* SETTINGS */, pkt, sizeof(pkt));
    }
}

// ---------------------------------------------------------------- TX

extern void I_UpdateSound(void);

// write raw bytes, pumping USB and the doom mixer while waiting for space
static bool ws_write_all(const uint8_t *data, uint32_t len) {
    uint32_t spins = 0;
    uint64_t stall_deadline = time_us_64() + 500000;
    while (len) {
        uint32_t n = tud_cdc_write_available();
        if (n) {
            if (n > len) n = len;
            tud_cdc_write(data, n);
            data += n;
            len -= n;
            tud_cdc_write_flush();
            stall_deadline = time_us_64() + 500000;
        } else if (time_us_64() > stall_deadline) {
            // host holds the port open but isn't reading: give up and
            // keep the game running; retry streaming in a couple seconds
            ws_muted_until = time_us_64() + 2000000;
            return false;
        }
        ws_stream_poll();
        if (!tud_cdc_connected()) return false;
        // keep the audio buffers fed during long frame writes
        if (++spins >= 64) {
            spins = 0;
            I_UpdateSound();
        }
    }
    return true;
}

// set when a write gave up partway through a packet: the host's parser is
// now desynced and, worse, may have swallowed frame bytes into a palette
static bool ws_tx_torn;

bool ws_stream_take_torn(void) {
    bool t = ws_tx_torn;
    ws_tx_torn = false;
    return t;
}

void ws_stream_packet(uint8_t type, const void *payload, uint16_t len) {
    if (!ws_stream_active()) return;
    uint8_t hdr[5] = {0xAB, 0xCD, type, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    if (!ws_write_all(hdr, sizeof(hdr))) {
        ws_tx_torn = true;
        return;
    }
    if (len && !ws_write_all(payload, len)) {
        ws_tx_torn = true;
    }
}

// ---------------------------------------------------------------- status

void ws_stream_status_tick(void) {
    static uint32_t last_ms;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_ms < 250) return;
    last_ms = now;

    int16_t health = 0, armor = 0, ammo = 0;
    uint8_t weapon = 0, level = 0;
    if (gamestate == GS_LEVEL && playeringame[consoleplayer]) {
        player_t *p = &players[consoleplayer];
        health = p->health;
        armor = p->armorpoints;
        weapon = p->readyweapon;
        ammotype_t at = weaponinfo[p->readyweapon].ammo;
        ammo = (at != am_noammo) ? p->ammo[at] : -1;
        level = (uint8_t)((gameepisode << 4) | (gamemap & 0xF));
    }
    uint8_t pkt[8];
    pkt[0] = health & 0xFF; pkt[1] = (uint16_t)health >> 8;
    pkt[2] = armor & 0xFF;  pkt[3] = (uint16_t)armor >> 8;
    pkt[4] = ammo & 0xFF;   pkt[5] = (uint16_t)ammo >> 8;
    pkt[6] = weapon;
    pkt[7] = level;
    ws_stream_packet(WS_PKT_STATUS, pkt, sizeof(pkt));
}

#endif // WS_USB
