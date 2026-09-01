// USB host TinyUSB MIDI callbacks (host RX → shared parser).
#pragma once

#include <cstdint>

// Implemented in usb_role.cpp (tuh_midi_* weak overrides).
void usbRoleRegisterHostCallbacks(); // no-op marker — link unit for callbacks
