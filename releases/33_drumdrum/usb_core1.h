#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB role decided by main() before Core 1 launches. Read once on the
// USB-C CC pins via USBPowerState() — see WORKSHOP_COMPUTER_AI_DIRECTIVE.
//   0 = device (browser WebMIDI editor)
//   1 = host   (Monome Grid)
extern volatile uint8_t gUsbHostMode;

// Run on Core 1. Reads gUsbHostMode and enters the matching loop.
void core1_entry(void);

#ifdef __cplusplus
}
#endif
