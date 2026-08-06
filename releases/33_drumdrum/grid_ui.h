#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// One-shot init — clears LEDs, sets the global intensity to max.
// Safe to call before the grid has been discovered; the actual LED
// writes won't go out until mext_grid_ready() is true.
void grid_ui_init(void);

// Drain any pending key events from the mext queue and apply them to
// gState. Call from the Core 1 USB loop.
void grid_ui_process_input(void);

// Repaint LEDs from gState whenever the displayed state has changed.
// Call from the Core 1 USB loop. Internally rate-limited so it's cheap
// to call every iteration.
void grid_ui_render(void);

#ifdef __cplusplus
}
#endif
