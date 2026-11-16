/// @file usb_editor.hpp
/// @brief Direct TinyUSB CDC live settings link, included in every firmware build.
///
/// Lives entirely in core0 foreground: startUsbEditor() runs once from
/// runInstrument() (after core1 is up) and pollUsbEditor() is invoked through
/// the generated header's COMPUTERCARD_FOREGROUND hook inside ComputerCard's
/// foreground loop, never from the 48 kHz audio IRQ. It parses editor
/// commands, hands them to the audio control tick via editorBridge, and
/// streams formatted replies back over CDC; no ComputerCard hardware inputs
/// are touched. MTM_USB_EDITOR is enabled by every supported CMake build.
#pragma once
#include "../port/editor.hpp"
namespace computer {
// Core0 foreground only; neither function accesses ComputerCard hardware inputs.
/// @brief One-time TinyUSB CDC setup on core0: tusb_init(), then priorities.
///
/// Lowers USB to 0xc0 and keeps DMA/audio at 0x00, so the audio IRQ always
/// preempts USB; PWM IRQ sits at 0xff beneath both. Called by runInstrument.
/// @note No stdio/UART link is opened; the page connects over plain CDC.
void startUsbEditor();
/// @brief One 1 ms-quantum of CDC/editor foreground work (core0, never IRQ).
///
/// Paced by time_us_32(); drives tud_task(), tracks connect/disconnect edges
/// through editorBridge.epoch, drains stale replies, writes whatever fits of
/// the staged reply, reads/parses at most 64 input bytes (one completed
/// command per poll), applies Restore/Save via storage, and answers errors.
/// @note Preemptible by the audio DMA IRQ at any point; all state here is
///       bounded, static and touched only from core0 foreground.
void pollUsbEditor();
/// @brief Protocol bridge between this foreground and the audio control tick.
///
/// One producer per direction: foreground pushes editor::Command entries and
/// the control tick pushes editor::Reply entries; `epoch` invalidates queued
/// work after a reconnect; replies are formatted only in this foreground.
extern editor::Bridge editorBridge;
} // namespace computer
