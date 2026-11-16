/// @file usb_editor.cpp
/// @brief Core0 foreground driver of the direct TinyUSB CDC editor link.
///
/// Polls from the vendor COMPUTERCARD_FOREGROUND hook with 1 ms pacing,
/// never from the audio IRQ: `tud_task` (and TinyUSB's errata-wait paths) is
/// preemptible by the audio DMA IRQ and stays off the real-time path. Work
/// is bounded per poll: one completed command (up to 64 parsed bytes), one
/// staged reply drained at CDC write speed, all buffers static and fixed
/// size, no allocation. Restore answers read the program-card journal and
/// successful Save replies write it; connect/disconnect edges bump
/// editorBridge.epoch so queued edits/replies from another session never
/// apply or leak. Application bounds live here, not in USB latency: USB
/// hardware timing remains a physical measurement.
#include "usb_editor.hpp"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "storage.hpp"
#include "tusb.h"
namespace computer {
/// @brief Default-constructed protocol bridge: queues empty, epoch 0.
editor::Bridge editorBridge;
namespace {
/// @brief Protocol line parser state (max 95 printable bytes per command).
editor::Parser parser;
/// @brief Static 8 KiB staging for one formatted reply, written across polls.
char transmit[8192];
/// @brief Reply length and progress cursor for the staged partial writes.
size_t transmitSize = 0, transmitOffset = 0;
/// @brief Last CDC connection state, for connect/disconnect edge handling.
bool wasConnected = false;
/// @brief Absolute time_us_32() due for the next 1 ms poll pacing tick.
uint32_t nextPoll = 0;
/// @brief Stages a formatted reply in the static transmit buffer.
/// @param reply Complete reply structure for editor::formatReply to serialise.
/// @note Runs in core0 foreground only; a too-small buffer formats to
///       nothing (size stays 0, nothing is transmitted).
void prepare(const editor::Reply &reply) {
    transmitOffset = 0;
    transmitSize = editor::formatReply(reply, transmit, sizeof(transmit));
}
} // namespace
/// @brief One-time CDC device setup on core0: init stack, set IRQ priorities.
///
/// Initialises TinyUSB directly (no stdio, no UART noise, no background USB
/// work), then lowers the USB IRQ to 0xc0 beneath the audio DMA IRQ at 0x00
/// so audio always preempts USB work. Contract in usb_editor.hpp.
/// @note Runs once from runInstrument before card.Run() takes over core0.
void startUsbEditor() {
    // Direct device stack: no stdio, GPIO4/UART, background alarm or core1 work.
    tusb_init();
    irq_set_priority(USBCTRL_IRQ, 0xc0);
    irq_set_priority(DMA_IRQ_0, 0x00);
}
/// @brief One bounded unit of editor/USB foreground work, 1 ms-paced.
///
/// Stages, in order: return early unless 1 ms has elapsed; run tud_task();
/// handle connection edges (bump epoch, reset parser, drop stale replies and
/// partial transport); while disconnected keep draining replies; otherwise
/// write whatever fits of the staged reply; pop and format one reply from
/// the bridge (Save replies write the flash journal); lastly parse up to 64
/// bytes, applying Restore (via storage::load) and queueing exactly one
/// finished command toward the control tick, answering local failures
/// (Storage/QueueFull) immediately. Runs on core0 foreground; never called
/// from the audio IRQ.
/// @note Queued commands/replies (8 each) and the 8 KiB stage bound all
///       memory; nothing here can wait on audio, and audio never waits.
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
    // Transmit flight: stream the staged reply before consuming new replies.
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
        // Stamped with the current epoch so the audio side discards a command
        // parsed before its connection's boundary handling.
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
