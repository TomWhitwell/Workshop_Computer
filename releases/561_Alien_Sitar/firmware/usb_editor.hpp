#pragma once
#include "../port/editor.hpp"
namespace computer {
// Core0 foreground only; neither function accesses ComputerCard hardware inputs.
void startUsbEditor();
void pollUsbEditor();
extern editor::Bridge editorBridge;
} // namespace computer
