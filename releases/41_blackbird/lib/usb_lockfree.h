#ifndef USB_LOCKFREE_H
#define USB_LOCKFREE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lock-free ring buffer for USB RX messages
#define USB_RX_QUEUE_SIZE 32
#define USB_RX_MSG_MAX_LENGTH 256

// Lock-free ring buffer for USB TX messages. Nothing posts to it at present
// (all output is written to the CDC FIFO directly, via __wrap_tud_cdc_n_write
// in main.cpp), so it's kept minimal: 32 slots cost 8KB of Lua heap.
#define USB_TX_QUEUE_SIZE 2
#define USB_TX_MSG_MAX_LENGTH 256

typedef struct {
    char data[USB_RX_MSG_MAX_LENGTH];
    uint16_t length;
    uint32_t timestamp_us;
} usb_rx_message_t;

typedef struct {
    char data[USB_TX_MSG_MAX_LENGTH];
    uint16_t length;
    bool needs_flush;  // Set true for last message in batch
} usb_tx_message_t;

// RX queue functions
void usb_rx_lockfree_init(void);
bool usb_rx_lockfree_post(const char* data, uint16_t length);
bool usb_rx_lockfree_get(usb_rx_message_t* msg);
uint32_t usb_rx_lockfree_pending_count(void);
uint32_t usb_rx_lockfree_drop_count(void);

// TX queue functions
void usb_tx_lockfree_init(void);
bool usb_tx_lockfree_post(const char* data, uint16_t length, bool needs_flush);
bool usb_tx_lockfree_get(usb_tx_message_t* msg);
uint32_t usb_tx_lockfree_pending_count(void);
uint32_t usb_tx_lockfree_drop_count(void);

// Combined init
void usb_lockfree_init(void);

#ifdef __cplusplus
}
#endif

#endif // USB_LOCKFREE_H
