#include "ii_bus.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

#define II_BAUD_SLOW 100000
#define II_BAUD_FAST 400000

// A transaction's time limit: its bits at the bus rate (plus start/stop),
// with room for followers that stretch the clock, and at least 5ms
#define II_TIMEOUT_MIN_US 5000u

static ii_bus_config_t g_cfg;
#define II_BUS (g_cfg.i2c)
#define II_SDA_PIN (g_cfg.sda_pin)
#define II_SCL_PIN (g_cfg.scl_pin)

static ii_bus_status_t g_status = II_BUS_IDLE;
static uint8_t g_rx_len = 0;
static uint32_t g_start_us = 0;
static uint32_t g_timeout_us = 0;
static uint32_t g_baud = II_BAUD_SLOW;

// Follower state (shared with the I2C interrupt)
#define II_FOLLOW_QUEUE 8
static uint8_t g_follow_address = 0;           // 0: not following
static ii_bus_request_fn g_request = NULL;
static volatile bool g_following = false;      // controller in follower mode now
static volatile bool g_in_transfer = false;    // a leader is talking to us
static volatile bool g_deferred = false;       // a read waiting for ii_bus_reply
static uint8_t g_cur[II_BUS_MAX_BYTES];        // the message being written to us
static volatile uint8_t g_cur_len = 0;
static volatile bool g_cur_used = false;       // ... was the start of a read (not queued)
static uint8_t g_last[II_BUS_MAX_BYTES];       // the last message written to us
static volatile uint8_t g_last_len = 0;
static uint8_t g_q[II_FOLLOW_QUEUE][II_BUS_MAX_BYTES];
static uint8_t g_q_len[II_FOLLOW_QUEUE];
static volatile uint8_t g_q_head = 0, g_q_count = 0;
volatile uint32_t g_ii_bus_follow_drops = 0;   // messages lost to a full queue

void ii_bus_init(const ii_bus_config_t* config) {
    g_cfg = *config;
    g_baud = II_BAUD_SLOW;
    i2c_set_baudrate(II_BUS, g_baud);
    g_status = II_BUS_IDLE;
}

bool ii_bus_idle(void) {
    return g_status == II_BUS_IDLE;
}

void ii_bus_set_fast(bool fast) {
    g_baud = fast ? II_BAUD_FAST : II_BAUD_SLOW;
    if (g_status == II_BUS_IDLE) {
        i2c_set_baudrate(II_BUS, g_baud);  // (otherwise applied by the next start)
    }
}

void ii_bus_set_pullups(bool on) {
    gpio_set_pulls(II_SDA_PIN, on, false);
    gpio_set_pulls(II_SCL_PIN, on, false);
}

// Free a bus a follower is holding (SDA stuck low mid-byte): clock SCL by hand
// until it lets go, then send a STOP. As crow does on I2C errors.
static void bus_recover(void) {
    gpio_set_function(II_SDA_PIN, GPIO_FUNC_SIO);
    gpio_set_function(II_SCL_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(II_SDA_PIN, GPIO_IN);
    for (int i = 0; i < 9 && !gpio_get(II_SDA_PIN); i++) {
        // open drain: drive low, or release to the pull-ups
        gpio_set_dir(II_SCL_PIN, GPIO_OUT); gpio_put(II_SCL_PIN, 0); busy_wait_us(5);
        gpio_set_dir(II_SCL_PIN, GPIO_IN);                           busy_wait_us(5);
    }
    // STOP: SDA low -> high while SCL is high
    gpio_set_dir(II_SDA_PIN, GPIO_OUT); gpio_put(II_SDA_PIN, 0); busy_wait_us(5);
    gpio_set_dir(II_SCL_PIN, GPIO_IN);                           busy_wait_us(5);
    gpio_set_dir(II_SDA_PIN, GPIO_IN);                           busy_wait_us(5);
    gpio_set_function(II_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(II_SCL_PIN, GPIO_FUNC_I2C);
}

// --- follower ---

static void follower_mode(bool on) {
    i2c_hw_t* hw = i2c_get_hw(II_BUS);
    uint irq = I2C0_IRQ + i2c_hw_index(II_BUS);
    if (on) {
        // RX_FIFO_FULL_HLD_CTRL (set by the SDK): a full FIFO stretches the
        // clock rather than dropping bytes
        i2c_set_slave_mode(II_BUS, true, g_follow_address);
        (void)hw->clr_intr;
        hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS
                      | I2C_IC_INTR_MASK_M_TX_ABRT_BITS | I2C_IC_INTR_MASK_M_STOP_DET_BITS;
        g_following = true;
        irq_set_enabled(irq, true);
    } else {
        irq_set_enabled(irq, false);
        g_following = false;
        // A fresh controller to lead: as a follower it watched other traffic,
        // and could believe the bus still busy (then never start, as at the
        // pin handover). The reset also unmasks every interrupt: mask them,
        // or the level-triggered ones (TX_EMPTY) would fire forever.
        i2c_init(II_BUS, g_baud);
        hw->intr_mask = 0;
    }
}

// A transfer to us ended: queue what was written (unless a read used it)
static void finish_transfer(void) {
    if (g_cur_len && !g_cur_used) {
        memcpy(g_last, g_cur, g_cur_len);  // for a two-step query's read
        g_last_len = g_cur_len;
        if (g_q_count < II_FOLLOW_QUEUE) {
            uint8_t i = (g_q_head + g_q_count) % II_FOLLOW_QUEUE;
            memcpy(g_q[i], g_cur, g_cur_len);
            g_q_len[i] = g_cur_len;
            g_q_count = g_q_count + 1;
        } else {
            g_ii_bus_follow_drops = g_ii_bus_follow_drops + 1;
        }
    }
    g_cur_len = 0;
    g_cur_used = false;
    g_deferred = false;  // if a held read was never answered, the leader has given up
    g_in_transfer = false;
}

static void follower_irq(void) {
    i2c_hw_t* hw = i2c_get_hw(II_BUS);
    if (!g_following) {  // leading: nothing of ours to handle
        hw->intr_mask = 0;
        return;
    }
    uint32_t stat = hw->intr_stat;
    if (stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        (void)hw->clr_tx_abrt;  // a read we answered ended early: the FIFO's flushed
    }
    if (stat & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        while (hw->rxflr) {
            uint32_t d = hw->data_cmd;
            if (d & I2C_IC_DATA_CMD_FIRST_DATA_BYTE_BITS) {  // a new message
                g_cur_len = 0;
                g_cur_used = false;
            }
            if (g_cur_len < II_BUS_MAX_BYTES) g_cur[g_cur_len++] = (uint8_t)d;
        }
        g_in_transfer = true;
    }
    if (stat & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        (void)hw->clr_rd_req;
        g_in_transfer = true;
        uint8_t reply[II_BUS_MAX_BYTES];
        // written in this transfer, or (a read on its own) the last message
        const uint8_t* msg = g_cur_len ? g_cur : g_last;
        uint8_t len = g_cur_len ? g_cur_len : g_last_len;
        int n = g_request ? g_request(msg, len, reply) : 0;
        g_cur_used = true;
        if (n == II_BUS_DEFER) {
            g_deferred = true;  // the clock stays stretched until data is written
        } else {
            if (n <= 0) { reply[0] = 0; n = 1; }  // never leave the leader stuck
            for (int i = 0; i < n; i++) hw->data_cmd = reply[i];
        }
    }
    if (stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        finish_transfer();
    }
}

void ii_bus_follow(uint8_t address, ii_bus_request_fn request) {
    uint irq = I2C0_IRQ + i2c_hw_index(II_BUS);
    irq_set_enabled(irq, false);
    g_follow_address = address;
    g_request = request;
    g_in_transfer = g_deferred = false;
    g_cur_len = 0;
    if (address) {
        if (!irq_get_exclusive_handler(irq)) irq_set_exclusive_handler(irq, follower_irq);
        if (g_status == II_BUS_IDLE) follower_mode(true);  // (otherwise when the leader transaction ends)
    } else if (g_following) {
        follower_mode(false);
    }
}

uint8_t ii_bus_receive(uint8_t* msg) {
    uint32_t irq = save_and_disable_interrupts();
    uint8_t n = 0;
    if (g_q_count) {
        n = g_q_len[g_q_head];
        memcpy(msg, g_q[g_q_head], n);
        g_q_head = (g_q_head + 1) % II_FOLLOW_QUEUE;
        g_q_count = g_q_count - 1;
    }
    restore_interrupts(irq);
    return n;
}

void ii_bus_reply(const uint8_t* reply, uint8_t len) {
    uint32_t irq = save_and_disable_interrupts();
    if (g_deferred && g_following) {
        i2c_hw_t* hw = i2c_get_hw(II_BUS);
        if (len == 0) hw->data_cmd = 0;
        for (uint8_t i = 0; i < len; i++) hw->data_cmd = reply[i];
        g_deferred = false;
    }
    restore_interrupts(irq);
}

// A leader transaction of ours ended: back to answering at our address
static ii_bus_status_t leader_done(ii_bus_status_t s) {
    g_status = II_BUS_IDLE;
    if (g_follow_address) follower_mode(true);
    return s;
}

// --- leader ---

bool ii_bus_start(uint8_t address, const uint8_t* tx, uint8_t tx_len, uint8_t rx_len) {
    if (g_status != II_BUS_IDLE) return false;
    if (tx_len + rx_len == 0 || tx_len + rx_len > II_BUS_MAX_BYTES) return false;

    i2c_hw_t* hw = i2c_get_hw(II_BUS);
    if (g_following) {
        // Not while a leader is talking to us (or waiting on our reply)
        if (g_in_transfer || g_deferred || (hw->status & I2C_IC_STATUS_SLV_ACTIVITY_BITS)) return false;
        follower_mode(false);
    }
    if (hw->status & I2C_IC_STATUS_ACTIVITY_BITS) {  // still finishing the last one
        if (g_follow_address) follower_mode(true);
        return false;
    }

    // The target address can only change while the controller is disabled
    hw->enable = 0;
    hw->tar = address;
    hw->enable = I2C_IC_ENABLE_ENABLE_BITS;
    (void)hw->clr_tx_abrt;
    (void)hw->clr_stop_det;
    while (hw->rxflr) (void)hw->data_cmd;  // stale read bytes

    // Load the whole transaction: the writes, then a read command per byte
    // (restart before the first), STOP on the very last. If the FIFO ever ran
    // dry before STOP, the controller would hold SCL low, not end early.
    for (uint8_t i = 0; i < tx_len; i++) {
        bool last = (i + 1 == tx_len) && rx_len == 0;
        hw->data_cmd = tx[i] | (last ? I2C_IC_DATA_CMD_STOP_BITS : 0);
    }
    for (uint8_t i = 0; i < rx_len; i++) {
        hw->data_cmd = I2C_IC_DATA_CMD_CMD_BITS
                     | ((i == 0 && tx_len) ? I2C_IC_DATA_CMD_RESTART_BITS : 0)
                     | ((i + 1 == rx_len) ? I2C_IC_DATA_CMD_STOP_BITS : 0);
    }

    uint32_t bits = (uint32_t)(tx_len + rx_len + 1) * 9 + 4;
    g_timeout_us = bits * 1000000u / g_baud * 4;
    if (g_timeout_us < II_TIMEOUT_MIN_US) g_timeout_us = II_TIMEOUT_MIN_US;
    g_rx_len = rx_len;
    g_start_us = time_us_32();
    g_status = II_BUS_BUSY;
    return true;
}

ii_bus_status_t ii_bus_poll(uint8_t* rx) {
    if (g_status != II_BUS_BUSY) {
        ii_bus_status_t s = g_status;
        g_status = II_BUS_IDLE;  // report a result once
        return s;
    }
    i2c_hw_t* hw = i2c_get_hw(II_BUS);
    uint32_t raw = hw->raw_intr_stat;

    if (raw & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
        uint32_t source = hw->tx_abrt_source;
        (void)hw->clr_tx_abrt;  // the controller has flushed the FIFO and sent STOP
        (void)hw->clr_stop_det;
        if (source & (I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS | I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS)) {
            return leader_done(II_BUS_NAK);
        }
        return leader_done(II_BUS_ERROR);
    }

    if (raw & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        if (hw->rxflr < g_rx_len) return leader_done(II_BUS_ERROR);
        for (uint8_t i = 0; i < g_rx_len; i++) {
            uint8_t b = (uint8_t)hw->data_cmd;
            if (rx) rx[i] = b;
        }
        return leader_done(II_BUS_DONE);
    }

    if ((time_us_32() - g_start_us) > g_timeout_us) {
        // Abort (sends STOP if it can), then free the bus if a follower still holds it
        hw->enable = I2C_IC_ENABLE_ENABLE_BITS | I2C_IC_ENABLE_ABORT_BITS;
        uint32_t t0 = time_us_32();
        while ((hw->enable & I2C_IC_ENABLE_ABORT_BITS) && (time_us_32() - t0) < 1000) {}
        (void)hw->clr_tx_abrt;
        (void)hw->clr_stop_det;
        hw->enable = 0;
        bus_recover();
        i2c_init(II_BUS, g_baud);  // reset: the pin toggling may have looked like another leader
        hw->intr_mask = 0;
        return leader_done(II_BUS_TIMEOUT);
    }
    return II_BUS_BUSY;
}
