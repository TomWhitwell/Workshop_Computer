/**
 * device_mode.c — USB device-side services.
 *
 * Sample manager: handles sample upload/download commands over CDC.
 * Protocol detection: examines the first incoming byte to determine
 * whether the host is a sample manager or a mext grid proxy.
 *
 * Flash-mutating or flash-streaming commands are only accepted when the
 * MLR engine is idle, to avoid starving ring-buffer refill on RP2040.
 */

#include "device_mode.h"
#include "mlr.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/time.h"
#include "tusb.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000
#endif

#define SAMPLE_CDC_ITF 0u
#define SAMPLE_READ_CHUNK_SIZE 1024u
#define SAMPLE_READ_ACK 'A'

#ifndef MLR_FIRMWARE_VERSION
#error "MLR_FIRMWARE_VERSION must be defined by the build system"
#endif

typedef enum {
    SAMPLE_RX_WAIT_CMD = 0,
    SAMPLE_RX_WAIT_TRACK,
    SAMPLE_RX_WAIT_CV1_PITCH,
    SAMPLE_RX_WAIT_WRITE_LEN,
    SAMPLE_RX_WAIT_WRITE_DATA,
    SAMPLE_RX_DRAIN_DATA,
} sample_rx_state_t;

typedef enum {
    SAMPLE_OP_NONE = 0,
    SAMPLE_OP_READ_STREAM,
    SAMPLE_OP_WRITE_STREAM,
} sample_op_t;

typedef struct {
    sample_rx_state_t rx_state;
    sample_op_t       op;
    uint8_t           pending_cmd;
    uint8_t           track;
    uint8_t           len_buf[4];
    uint8_t           len_got;
    uint32_t          total_len;
    uint32_t          bytes_done;
    uint32_t          flash_off;
    uint32_t          next_erase;
    uint32_t          drain_remaining;
    uint8_t           page_buf[256] __attribute__((aligned(4)));
    uint16_t          page_fill;
    uint8_t           prefix[4];
    uint8_t           prefix_sent;
    uint16_t          read_chunk_len;
    uint16_t          read_chunk_sent;
    bool              read_wait_ack;
    absolute_time_t   data_deadline;    /* write/drain data timeout */
    bool              was_connected;
    uint8_t           injected_byte;    /* byte pre-read during detection */
    bool              has_injected;     /* true if injected_byte is valid */
} sample_mgr_state_t;

static sample_mgr_state_t g_sample_mgr;
static uint8_t g_header_sector[MLR_SECTOR_SIZE] __attribute__((aligned(4)));

static bool __attribute__((section(".flashdata.devmode"))) cdc_write_all(const void *data, uint32_t len)
{
    if (!tud_cdc_n_connected(SAMPLE_CDC_ITF))
        return false;

    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sent = 0;
    absolute_time_t deadline = make_timeout_time_ms(3000);

    while (sent < len && tud_cdc_n_connected(SAMPLE_CDC_ITF) && !time_reached(deadline)) {
        uint32_t avail = tud_cdc_n_write_available(SAMPLE_CDC_ITF);
        if (avail == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            tud_task();
            continue;
        }

        uint32_t chunk = len - sent;
        if (chunk > avail)
            chunk = avail;

        uint32_t written = tud_cdc_n_write(SAMPLE_CDC_ITF, bytes + sent, chunk);
        if (written == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            tud_task();
            continue;
        }
        sent += written;
    }

    tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
    return sent == len;
}

static bool __attribute__((section(".flashdata.devmode"))) cdc_write_str(const char *s)
{
    return cdc_write_all(s, (uint32_t)strlen(s));
}

static bool __attribute__((section(".flashdata.devmode"))) cdc_read_byte(uint8_t *out)
{
    if (g_sample_mgr.has_injected) {
        *out = g_sample_mgr.injected_byte;
        g_sample_mgr.has_injected = false;
        return true;
    }
    return tud_cdc_n_read(SAMPLE_CDC_ITF, out, 1) == 1;
}

static bool __attribute__((section(".flashdata.devmode"))) mlr_flash_idle(void)
{
    if (mlr_rec_track >= 0 || mlr_flushing || mlr_scene_saving)
        return false;

    for (int t = 0; t < MLR_NUM_TRACKS; t++) {
        if (mlr_tracks[t].playing)
            return false;
    }

    return true;
}

/** Force-stop all playing tracks so flash operations can proceed.
 *  Returns true if tracks were successfully stopped (or already idle).
 *  Returns false if recording or flushing is in progress (can't override). */
static bool __attribute__((section(".flashdata.devmode"))) mlr_force_idle(void)
{
    if (mlr_rec_track >= 0 || mlr_flushing || mlr_scene_saving)
        return false;

    for (int t = 0; t < MLR_NUM_TRACKS; t++) {
        if (mlr_tracks[t].playing) {
            mlr_tracks[t].playing = false;
            mlr_tracks[t].pcm.r = mlr_tracks[t].pcm.w;
        }
    }

    return true;
}

static void __attribute__((section(".flashdata.devmode"))) sample_mgr_reset_session(void)
{
    g_sample_mgr.rx_state = SAMPLE_RX_WAIT_CMD;
    g_sample_mgr.pending_cmd = 0;
    g_sample_mgr.track = 0;
    g_sample_mgr.len_got = 0;
    g_sample_mgr.total_len = 0;
    g_sample_mgr.bytes_done = 0;
    g_sample_mgr.flash_off = 0;
    g_sample_mgr.next_erase = 0;
    g_sample_mgr.drain_remaining = 0;
    g_sample_mgr.page_fill = 0;
    g_sample_mgr.prefix_sent = 0;
    g_sample_mgr.read_chunk_len = 0;
    g_sample_mgr.read_chunk_sent = 0;
    g_sample_mgr.read_wait_ack = false;
    g_sample_mgr.data_deadline = at_the_end_of_time;
    g_sample_mgr.op = SAMPLE_OP_NONE;
}

static bool __attribute__((section(".flashdata.devmode"))) sample_mgr_send_info(void)
{
    char info[512];
    uint32_t used = 0;

    int n = snprintf(info + used, sizeof(info) - used, "MLR1 %d FW %s\n",
                     MLR_NUM_CHANNELS, MLR_FIRMWARE_VERSION);
    if (n < 0 || (uint32_t)n >= sizeof(info) - used)
        return false;
    used += (uint32_t)n;

    for (int t = 0; t < MLR_NUM_TRACKS; t++) {
        uint32_t flash_off = MLR_TRACK_OFFSET(t);
        const mlr_track_header_t *hdr =
            (const mlr_track_header_t *)(XIP_BASE + flash_off);
        unsigned cv1_pitch_enabled = mlr_tracks[t].has_content && mlr_tracks[t].cv1_pitch_enabled ? 1u : 0u;

        if (hdr->magic == MLR_MAGIC) {
            n = snprintf(info + used, sizeof(info) - used, "T%d %lu %lu %lu %d %u %u\n",
                 t,
                 (unsigned long)hdr->sample_count,
                 (unsigned long)hdr->adpcm_bytes,
                 (unsigned long)hdr->num_keyframes,
                 (int)hdr->record_speed_shift,
                 (unsigned)(hdr->recorded_channel & 0x01),
                 cv1_pitch_enabled);
        } else {
            n = snprintf(info + used, sizeof(info) - used, "T%d 0 0 0 0 0 %u\n", t, cv1_pitch_enabled);
        }

        if (n < 0 || (uint32_t)n >= sizeof(info) - used)
            return false;
        used += (uint32_t)n;
    }

    n = snprintf(info + used, sizeof(info) - used, "END\n");
    if (n < 0 || (uint32_t)n >= sizeof(info) - used)
        return false;
    used += (uint32_t)n;

    if (!cdc_write_all(&used, sizeof(used)))
        return false;
    return cdc_write_all(info, used);
}

static bool __attribute__((section(".flashdata.devmode"))) sample_mgr_begin_read(uint8_t track)
{
    /* Reads stream from XIP (no flash writes), so only block if
     * a recording or flash-write operation is in progress. */
    if (mlr_rec_track >= 0 || mlr_flushing || mlr_scene_saving)
        return cdc_write_str("BUSY\n");

    if (track >= MLR_NUM_TRACKS)
        return cdc_write_str("ERR\n");

    uint32_t flash_off = MLR_TRACK_OFFSET(track);
    const mlr_track_header_t *hdr =
        (const mlr_track_header_t *)(XIP_BASE + flash_off);

    if (hdr->magic != MLR_MAGIC || hdr->sample_count == 0) {
        uint32_t zero = 0;
        return cdc_write_all(&zero, sizeof(zero));
    }

    g_sample_mgr.op = SAMPLE_OP_READ_STREAM;
    g_sample_mgr.flash_off = flash_off;
    g_sample_mgr.total_len = MLR_HEADER_SIZE + hdr->adpcm_bytes;
    g_sample_mgr.bytes_done = 0;
    g_sample_mgr.prefix_sent = 0;
    g_sample_mgr.read_chunk_len = 0;
    g_sample_mgr.read_chunk_sent = 0;
    g_sample_mgr.read_wait_ack = false;
    memcpy(g_sample_mgr.prefix, &g_sample_mgr.total_len, sizeof(g_sample_mgr.prefix));
    return true;
}

static bool __attribute__((section(".flashdata.devmode"))) sample_mgr_begin_write(uint8_t track, uint32_t total_len)
{
    if (!mlr_force_idle())
    {
        g_sample_mgr.rx_state = SAMPLE_RX_DRAIN_DATA;
        g_sample_mgr.drain_remaining = total_len;
        g_sample_mgr.data_deadline = make_timeout_time_ms(10000);
        return cdc_write_str("BUSY\n");
    }

    if (track >= MLR_NUM_TRACKS || total_len < MLR_HEADER_SIZE || total_len > MLR_TRACK_FLASH_SIZE) {
        g_sample_mgr.rx_state = SAMPLE_RX_DRAIN_DATA;
        g_sample_mgr.drain_remaining = total_len;
        g_sample_mgr.data_deadline = make_timeout_time_ms(10000);
        return cdc_write_str("ERR\n");
    }

    g_sample_mgr.op = SAMPLE_OP_WRITE_STREAM;
    g_sample_mgr.flash_off = MLR_TRACK_OFFSET(track);
    g_sample_mgr.next_erase = g_sample_mgr.flash_off;
    g_sample_mgr.total_len = total_len;
    g_sample_mgr.bytes_done = 0;
    g_sample_mgr.page_fill = 0;
    g_sample_mgr.rx_state = SAMPLE_RX_WAIT_WRITE_DATA;
    g_sample_mgr.data_deadline = make_timeout_time_ms(5000);
    cdc_write_str("OK\n");          /* write-ready ACK */
    return true;
}

static bool __attribute__((section(".flashdata.devmode"))) sample_mgr_erase_track(uint8_t track)
{
    if (!mlr_force_idle())
        return cdc_write_str("BUSY\n");

    if (track >= MLR_NUM_TRACKS)
        return cdc_write_str("ERR\n");

    flash_range_erase(MLR_TRACK_OFFSET(track), MLR_SECTOR_SIZE);
    return cdc_write_str("OK\n");
}

static bool __attribute__((section(".flashdata.devmode"))) sample_mgr_set_cv1_pitch(uint8_t track, bool enabled)
{
    if (track >= MLR_NUM_TRACKS)
        return cdc_write_str("ERR\n");

    if (!mlr_tracks[track].has_content)
        return cdc_write_str("ERR\n");

    mlr_tracks[track].cv1_pitch_enabled = enabled;
    if (mlr_tracks[track].has_content) {
        uint32_t flash_off = MLR_TRACK_OFFSET(track);
        memcpy(g_header_sector, (const void *)(XIP_BASE + flash_off), sizeof(g_header_sector));
        mlr_track_header_t *hdr = (mlr_track_header_t *)g_header_sector;
        if (hdr->magic != MLR_MAGIC)
            return cdc_write_str("ERR\n");

        hdr->cv1_pitch_mode = enabled ? MLR_CV1_PITCH_ENABLED_MODE : MLR_CV1_PITCH_DISABLED_MODE;
        flash_range_erase(flash_off, MLR_SECTOR_SIZE);
        flash_range_program(flash_off, g_header_sector, sizeof(g_header_sector));
    }

    return cdc_write_str("OK\n");
}

static void __attribute__((section(".flashdata.devmode"))) sample_mgr_finish_write(void)
{
    if (g_sample_mgr.page_fill > 0) {
        memset(g_sample_mgr.page_buf + g_sample_mgr.page_fill, 0xFF,
               sizeof(g_sample_mgr.page_buf) - g_sample_mgr.page_fill);

        if (g_sample_mgr.flash_off + g_sample_mgr.bytes_done >= g_sample_mgr.next_erase) {
            flash_range_erase(g_sample_mgr.next_erase, MLR_SECTOR_SIZE);
            g_sample_mgr.next_erase += MLR_SECTOR_SIZE;
        }

        flash_range_program(g_sample_mgr.flash_off + g_sample_mgr.bytes_done,
                            g_sample_mgr.page_buf,
                            sizeof(g_sample_mgr.page_buf));
        g_sample_mgr.bytes_done += g_sample_mgr.page_fill;
        g_sample_mgr.page_fill = 0;
    }

    if (cdc_write_str("OK\n"))
        sample_mgr_reset_session();
}

static void __attribute__((section(".flashdata.devmode"))) sample_mgr_pump_read(void)
{
    if (g_sample_mgr.op != SAMPLE_OP_READ_STREAM)
        return;

    while (g_sample_mgr.prefix_sent < sizeof(g_sample_mgr.prefix)) {
        uint32_t avail = tud_cdc_n_write_available(SAMPLE_CDC_ITF);
        if (avail == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            return;
        }

        uint32_t remain = sizeof(g_sample_mgr.prefix) - g_sample_mgr.prefix_sent;
        uint32_t chunk = remain < avail ? remain : avail;
        uint32_t written = tud_cdc_n_write(SAMPLE_CDC_ITF,
                                           g_sample_mgr.prefix + g_sample_mgr.prefix_sent,
                                           chunk);
        if (written == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            return;
        }
        g_sample_mgr.prefix_sent += (uint8_t)written;
    }

    if (g_sample_mgr.read_wait_ack) {
        while (tud_cdc_n_available(SAMPLE_CDC_ITF) > 0) {
            uint8_t byte = 0;
            tud_cdc_n_read(SAMPLE_CDC_ITF, &byte, 1);
            if (byte == SAMPLE_READ_ACK) {
                g_sample_mgr.bytes_done += g_sample_mgr.read_chunk_len;
                g_sample_mgr.read_chunk_len = 0;
                g_sample_mgr.read_chunk_sent = 0;
                g_sample_mgr.read_wait_ack = false;
                break;
            }

            sample_mgr_reset_session();
            cdc_write_str("SYNC\n");
            return;
        }

        if (g_sample_mgr.read_wait_ack)
            return;
    }

    if (g_sample_mgr.bytes_done >= g_sample_mgr.total_len) {
        cdc_write_str("DONE\n");
        sample_mgr_reset_session();
        return;
    }

    if (g_sample_mgr.read_chunk_len == 0) {
        uint32_t remain = g_sample_mgr.total_len - g_sample_mgr.bytes_done;
        g_sample_mgr.read_chunk_len = (uint16_t)(remain < SAMPLE_READ_CHUNK_SIZE ? remain : SAMPLE_READ_CHUNK_SIZE);
        g_sample_mgr.read_chunk_sent = 0;
    }

    if (g_sample_mgr.read_chunk_sent < g_sample_mgr.read_chunk_len) {
        uint32_t avail = tud_cdc_n_write_available(SAMPLE_CDC_ITF);
        if (avail == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            return;
        }

        uint32_t remain = g_sample_mgr.read_chunk_len - g_sample_mgr.read_chunk_sent;
        uint32_t chunk = remain;
        if (chunk > avail) chunk = avail;
        if (chunk > 64)    chunk = 64;

        const uint8_t *src = (const uint8_t *)(XIP_BASE + g_sample_mgr.flash_off +
                                               g_sample_mgr.bytes_done +
                                               g_sample_mgr.read_chunk_sent);
        uint32_t written = tud_cdc_n_write(SAMPLE_CDC_ITF, src, chunk);
        if (written == 0) {
            tud_cdc_n_write_flush(SAMPLE_CDC_ITF);
            return;
        }
        g_sample_mgr.read_chunk_sent += (uint16_t)written;
    }

    tud_cdc_n_write_flush(SAMPLE_CDC_ITF);

    if (g_sample_mgr.read_chunk_sent >= g_sample_mgr.read_chunk_len)
        g_sample_mgr.read_wait_ack = true;
}

static void __attribute__((section(".flashdata.devmode"))) sample_mgr_pump_write(void)
{
    if (g_sample_mgr.op != SAMPLE_OP_WRITE_STREAM)
        return;

    while (g_sample_mgr.bytes_done + g_sample_mgr.page_fill < g_sample_mgr.total_len &&
           tud_cdc_n_available(SAMPLE_CDC_ITF) > 0) {
        uint32_t remain = g_sample_mgr.total_len - (g_sample_mgr.bytes_done + g_sample_mgr.page_fill);
        uint32_t room = sizeof(g_sample_mgr.page_buf) - g_sample_mgr.page_fill;
        uint32_t chunk = remain < room ? remain : room;
        uint32_t avail = tud_cdc_n_available(SAMPLE_CDC_ITF);
        if (chunk > avail)
            chunk = avail;

        if (chunk == 0)
            break;

        uint32_t got = tud_cdc_n_read(SAMPLE_CDC_ITF, g_sample_mgr.page_buf + g_sample_mgr.page_fill, chunk);
        g_sample_mgr.page_fill += (uint16_t)got;
        if (got > 0)
            g_sample_mgr.data_deadline = make_timeout_time_ms(5000);

        if (g_sample_mgr.page_fill >= sizeof(g_sample_mgr.page_buf)) {
            if (g_sample_mgr.flash_off + g_sample_mgr.bytes_done >= g_sample_mgr.next_erase) {
                flash_range_erase(g_sample_mgr.next_erase, MLR_SECTOR_SIZE);
                g_sample_mgr.next_erase += MLR_SECTOR_SIZE;
            }

            flash_range_program(g_sample_mgr.flash_off + g_sample_mgr.bytes_done,
                                g_sample_mgr.page_buf,
                                sizeof(g_sample_mgr.page_buf));
            g_sample_mgr.bytes_done += sizeof(g_sample_mgr.page_buf);
            g_sample_mgr.page_fill = 0;
        }
    }

    if (g_sample_mgr.bytes_done + g_sample_mgr.page_fill >= g_sample_mgr.total_len)
        sample_mgr_finish_write();
}

static void __attribute__((section(".flashdata.devmode"))) sample_mgr_drain_rejected_write(void)
{
    while (g_sample_mgr.drain_remaining > 0 && tud_cdc_n_available(SAMPLE_CDC_ITF) > 0) {
        uint8_t scratch[32];
        uint32_t chunk = g_sample_mgr.drain_remaining;
        if (chunk > sizeof(scratch)) chunk = sizeof(scratch);
        uint32_t avail = tud_cdc_n_available(SAMPLE_CDC_ITF);
        if (chunk > avail) chunk = avail;
        uint32_t got = tud_cdc_n_read(SAMPLE_CDC_ITF, scratch, chunk);
        g_sample_mgr.drain_remaining -= got;
        if (got > 0)
            g_sample_mgr.data_deadline = make_timeout_time_ms(5000);
    }

    if (g_sample_mgr.drain_remaining == 0)
        g_sample_mgr.rx_state = SAMPLE_RX_WAIT_CMD;
}

void __attribute__((section(".flashdata.devmode"))) device_mode_init(void)
{
    memset(&g_sample_mgr, 0, sizeof(g_sample_mgr));
    sample_mgr_reset_session();
}

void __attribute__((section(".flashdata.devmode"))) device_mode_inject_byte(uint8_t byte)
{
    g_sample_mgr.injected_byte = byte;
    g_sample_mgr.has_injected = true;
}

void __attribute__((section(".flashdata.devmode"))) device_mode_task(void)
{
    bool connected = tud_cdc_n_connected(SAMPLE_CDC_ITF);
    if (!connected) {
        g_sample_mgr.was_connected = false;
        sample_mgr_reset_session();
        return;
    }

    if (!g_sample_mgr.was_connected) {
        g_sample_mgr.was_connected = true;
    }

    if (g_sample_mgr.op == SAMPLE_OP_READ_STREAM) {
        sample_mgr_pump_read();
        return;
    }

    if (g_sample_mgr.op == SAMPLE_OP_WRITE_STREAM) {
        if (time_reached(g_sample_mgr.data_deadline)) {
            sample_mgr_reset_session();
            cdc_write_str("TIMEOUT\n");
            return;
        }
        sample_mgr_pump_write();
        return;
    }

    if (g_sample_mgr.rx_state == SAMPLE_RX_DRAIN_DATA) {
        if (time_reached(g_sample_mgr.data_deadline)) {
            sample_mgr_reset_session();
            return;
        }
        sample_mgr_drain_rejected_write();
        return;
    }

    while (g_sample_mgr.has_injected || tud_cdc_n_available(SAMPLE_CDC_ITF) > 0) {
        uint8_t byte = 0;
        if (!cdc_read_byte(&byte))
            return;

        switch (g_sample_mgr.rx_state) {
        case SAMPLE_RX_WAIT_CMD:
            switch (byte) {
            case 'I':
                sample_mgr_send_info();
                return;
            case 'S':
                if (mlr_flash_idle()) cdc_write_str("OK\n");
                else                 cdc_write_str("BUSY\n");
                return;
            case 'X':
                sample_mgr_reset_session();
                cdc_write_str("SYNC\n");
                return;
            case 'R':
            case 'E':
            case 'W':
            case 'P':
                g_sample_mgr.pending_cmd = byte;
                g_sample_mgr.rx_state = SAMPLE_RX_WAIT_TRACK;
                break;
            default:
                /* Unknown byte — ignore (sample-manager mode only) */
                continue;
            }
            break;

        case SAMPLE_RX_WAIT_TRACK:
            if (byte == 'X') {
                sample_mgr_reset_session();
                cdc_write_str("SYNC\n");
                return;
            }
            g_sample_mgr.track = byte;
            if (g_sample_mgr.pending_cmd == 'R') {
                sample_mgr_begin_read(byte);
                if (g_sample_mgr.op != SAMPLE_OP_READ_STREAM)
                    sample_mgr_reset_session();
                return;
            }
            if (g_sample_mgr.pending_cmd == 'E') {
                sample_mgr_erase_track(byte);
                sample_mgr_reset_session();
                return;
            }
            if (g_sample_mgr.pending_cmd == 'P') {
                g_sample_mgr.rx_state = SAMPLE_RX_WAIT_CV1_PITCH;
                break;
            }
            g_sample_mgr.rx_state = SAMPLE_RX_WAIT_WRITE_LEN;
            g_sample_mgr.len_got = 0;
            break;

        case SAMPLE_RX_WAIT_CV1_PITCH:
            if (byte == 'X') {
                sample_mgr_reset_session();
                cdc_write_str("SYNC\n");
                return;
            }
            sample_mgr_set_cv1_pitch(g_sample_mgr.track, byte != 0);
            sample_mgr_reset_session();
            return;

        case SAMPLE_RX_WAIT_WRITE_LEN:
            if (byte == 'X' && g_sample_mgr.len_got == 0) {
                sample_mgr_reset_session();
                cdc_write_str("SYNC\n");
                return;
            }
            g_sample_mgr.len_buf[g_sample_mgr.len_got++] = byte;
            if (g_sample_mgr.len_got >= sizeof(g_sample_mgr.len_buf)) {
                memcpy(&g_sample_mgr.total_len, g_sample_mgr.len_buf, sizeof(g_sample_mgr.total_len));
                sample_mgr_begin_write(g_sample_mgr.track, g_sample_mgr.total_len);
                if (g_sample_mgr.op != SAMPLE_OP_WRITE_STREAM && g_sample_mgr.rx_state == SAMPLE_RX_WAIT_WRITE_LEN)
                    sample_mgr_reset_session();
                return;
            }
            break;

        case SAMPLE_RX_WAIT_WRITE_DATA:
        case SAMPLE_RX_DRAIN_DATA:
            /* handled in dedicated pump functions */
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Protocol detection (runs on core 0 before core 1 is launched)      */
/* ------------------------------------------------------------------ */

device_detect_result_t device_mode_detect_protocol(uint32_t timeout_ms)
{
    device_detect_result_t result = { DEVICE_PROTO_NONE, 0, false };

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    /* Phase 1: wait for CDC connection */
    while (!time_reached(deadline)) {
        tud_task();
        if (tud_cdc_n_connected(SAMPLE_CDC_ITF))
            break;
    }

    if (!tud_cdc_n_connected(SAMPLE_CDC_ITF))
        return result;  /* timeout — nothing connected */

    /* Phase 2: wait for first incoming byte (shorter timeout) */
    uint32_t byte_timeout = timeout_ms > 1000 ? 1000 : timeout_ms;
    absolute_time_t byte_deadline = make_timeout_time_ms(byte_timeout);

    while (!time_reached(byte_deadline)) {
        tud_task();
        if (tud_cdc_n_available(SAMPLE_CDC_ITF) > 0)
            break;
    }

    if (tud_cdc_n_available(SAMPLE_CDC_ITF) == 0) {
        /* Connected but no data — assume mext (grid proxy waiting for queries) */
        result.protocol = DEVICE_PROTO_MEXT;
        return result;
    }

    /* Consume the first byte and classify */
    uint8_t byte;
    tud_cdc_n_read(SAMPLE_CDC_ITF, &byte, 1);

    result.first_byte = byte;
    result.has_pending_byte = true;

    if (byte == 'I' || byte == 'S' || byte == 'R' || byte == 'E' || byte == 'W' || byte == 'P' || byte == 'X') {
        result.protocol = DEVICE_PROTO_SAMPLE_MGR;
    } else {
        result.protocol = DEVICE_PROTO_MEXT;
    }

    return result;
}
