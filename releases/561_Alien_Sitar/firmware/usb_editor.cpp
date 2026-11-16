#include "usb_editor.hpp"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "storage.hpp"
#include "tusb.h"
namespace computer {
editor::Bridge editorBridge;
namespace {
editor::Parser parser;
char transmit[8192];
size_t transmitSize = 0, transmitOffset = 0;
bool wasConnected = false;
uint32_t nextPoll = 0;
void prepare(const editor::Reply &reply) {
    transmitOffset = 0;
    transmitSize = editor::formatReply(reply, transmit, sizeof(transmit));
}
} // namespace
void startUsbEditor() {
    // Direct device stack: no stdio, GPIO4/UART, background alarm or core1 work.
    tusb_init();
    irq_set_priority(USBCTRL_IRQ, 0xc0);
    irq_set_priority(DMA_IRQ_0, 0x00);
}
void pollUsbEditor() {
    // Limit application parsing to 64 bytes/ms. tud_task is preemptible by DMA;
    // its hardware errata paths may wait, so it is NEVER called from audio IRQ.
    const uint32_t now = time_us_32();
    if (int32_t(now - nextPoll) < 0) {
        return;
    }
    nextPoll = now + 1000;
    tud_task();
    const bool connected = tud_cdc_connected();
    if (connected != wasConnected) {
        editorBridge.epoch.store(editorBridge.epoch.load(std::memory_order_relaxed) + 1,
                                 std::memory_order_release);
        parser.reset();
        transmitSize = transmitOffset = 0;
        // Commands already applied remain in RAM. The epoch invalidates queued
        // edits; discard partial bytes and old replies across connections.
        editor::Reply discarded;
        while (editorBridge.replies.pop(discarded)) {
        }
        if (!connected) {
            tud_cdc_read_flush();
        }
        tud_cdc_write_clear();
        wasConnected = connected;
    }
    if (!connected) {
        editor::Reply discarded;
        while (editorBridge.replies.pop(discarded)) {
        }
        return;
    }
    if (transmitOffset < transmitSize) {
        const size_t remaining = transmitSize - transmitOffset;
        const size_t available = tud_cdc_write_available();
        const size_t count = remaining < available ? remaining : available;
        if (count) {
            transmitOffset += tud_cdc_write(transmit + transmitOffset, count);
        }
        tud_cdc_write_flush();
        return;
    }
    editor::Reply reply;
    if (editorBridge.replies.pop(reply)) {
        if (reply.epoch == editorBridge.epoch.load(std::memory_order_acquire)) {
            if (reply.operation == editor::Operation::Save && reply.error == editor::Error::None &&
                !storage::save(reply.state)) {
                reply.error = editor::Error::Storage;
            }
            prepare(reply);
        }
        return;
    }
    for (unsigned i = 0; i < 64 && tud_cdc_available(); ++i) {
        const int byte = tud_cdc_read_char();
        if (byte < 0) {
            break;
        }
        editor::Command command;
        editor::Error error;
        if (!parser.feed(char(byte), command, error)) {
            continue;
        }
        command.epoch = editorBridge.epoch.load(std::memory_order_acquire);
        if (error == editor::Error::None && command.operation == editor::Operation::Restore &&
            !storage::load(command.state)) {
            error = editor::Error::Storage;
        }
        if (error == editor::Error::None && !editorBridge.commands.push(command)) {
            error = editor::Error::QueueFull;
        }
        if (error != editor::Error::None) {
            reply = {};
            reply.sequence = command.sequence;
            reply.error = error;
            prepare(reply);
        }
        // One completed command per poll bounds application work and traffic.
        break;
    }
}
} // namespace computer
