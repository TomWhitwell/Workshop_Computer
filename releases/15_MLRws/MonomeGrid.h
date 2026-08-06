/**
 * MonomeGrid.h — Friendly C++ wrapper for monome_ws.
 *
 * Provides readable methods for common grid interaction patterns.
 * Include this instead of (or alongside) monome_ws.h in your card.
 *
 * Usage:
 *   MonomeGrid grid;          // in your card class
 *   grid.begin();             // in constructor (calls monome_ws_init + launches core 1)
 *   grid.poll();              // at top of ProcessSample()
 *   grid.led(x, y, 15);      // set an LED
 *   if (grid.keyDown())  ...  // check for key events
 */

#ifndef MONOME_GRID_HPP
#define MONOME_GRID_HPP

#include "pico/multicore.h"
#include "bsp/board.h"
#include "tusb.h"
#include <string.h>

extern "C" {
#include "monome_ws.h"
#ifdef MLR_PERF_PROFILING
void mlr_perf_count_grid_frame_drop(void);
void mlr_perf_note_grid_poll(uint32_t processed, uint32_t backlog_before, uint32_t backlog_after);
#endif
}

class MonomeGrid
{
public:
	MonomeGrid()
	{
		clear();
	}

	/* ---- lifecycle ---- */

	/** Initialise the monome_ws driver and launch USB host on core 1. */
	void begin()
	{
		monome_ws_init(MONOME_WS_TRANSPORT_HOST, 0);
		multicore_launch_core1(usb_core);
	}

	/** True once the grid has reported its dimensions. */
	bool ready() const { return monome_ws_grid_ready(); }

	/** True if a grid/arc is physically connected. */
	bool connected() const { return monome_ws_connected(); }

	/** Grid width in columns (0 before discovery). */
	uint8_t cols() const { return monome_ws_grid_cols(); }

	/** Grid height in rows (0 before discovery). */
	uint8_t rows() const { return monome_ws_grid_rows(); }

	/** True if the attached grid supports 0-15 LED levels. */
	bool supportsLevels() const { return monome_ws_grid_supports_levels(); }

	/** Protocol backend currently selected for the attached device. */
	monome_ws_protocol_t protocol() const { return monome_ws_protocol(); }

	/* ---- events ---- */

	/**
	 * Drain the event queue.  Call once at the top of ProcessSample().
	 * After calling, use keyDown()/keyUp()/keyHeld()/lastKey() to
	 * inspect what happened this sample.
	 */
	void poll(uint8_t max_events = 0)
	{
		key_down_this_sample = false;
		key_up_this_sample   = false;

#ifdef MLR_PERF_PROFILING
		uint32_t backlog_before = monome_ws_event_backlog();
#endif
		monome_ws_event_t ev;
		uint8_t processed = 0;
		while ((max_events == 0 || processed < max_events) && monome_ws_event_pop(&ev)) {
			processed++;
			if (ev.type == MONOME_WS_EVENT_GRID_KEY) {
				last_event_x = ev.grid.x;
				last_event_y = ev.grid.y;

				if (ev.grid.z) {
					key_down_this_sample = true;
					any_key_held_count++;
					if (ev.grid.x < MONOME_WS_GRID_MAX_X && ev.grid.y < MONOME_WS_GRID_MAX_Y) {
						held_row_mask_[ev.grid.y] |= (uint16_t)(1u << ev.grid.x);
						held_col_mask_[ev.grid.x] |= (uint16_t)(1u << ev.grid.y);
					}
				} else {
					key_up_this_sample = true;
					if (any_key_held_count > 0) any_key_held_count--;
					if (ev.grid.x < MONOME_WS_GRID_MAX_X && ev.grid.y < MONOME_WS_GRID_MAX_Y) {
						held_row_mask_[ev.grid.y] &= (uint16_t)~(1u << ev.grid.x);
						held_col_mask_[ev.grid.x] &= (uint16_t)~(1u << ev.grid.y);
					}
				}
			}
		}
#ifdef MLR_PERF_PROFILING
		mlr_perf_note_grid_poll(processed, backlog_before, monome_ws_event_backlog());
#endif
	}

	/** True if any key was pressed down this sample. */
	bool keyDown() const { return key_down_this_sample; }

	/** True if any key was released this sample. */
	bool keyUp() const { return key_up_this_sample; }

	/** True if at least one key is currently held. */
	bool anyHeld() const { return any_key_held_count > 0; }

	/** True if the key at (x, y) is currently held down. */
	bool held(uint8_t x, uint8_t y) const
	{
		if (x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return false;
		return (held_row_mask_[y] >> x) & 1u;
	}

	/** Bitmask of held columns in row y (bit x set => (x, y) is held). */
	uint16_t heldRowMask(uint8_t y) const
	{
		if (y >= MONOME_WS_GRID_MAX_Y) return 0;
		return held_row_mask_[y];
	}

	/** Bitmask of held rows in column x (bit y set => (x, y) is held). */
	uint16_t heldColMask(uint8_t x) const
	{
		if (x >= MONOME_WS_GRID_MAX_X) return 0;
		return held_col_mask_[x];
	}

	/** X coordinate of the most recent key event. */
	uint8_t lastX() const { return last_event_x; }

	/** Y coordinate of the most recent key event. */
	uint8_t lastY() const { return last_event_y; }

	/* ---- LEDs ---- */

	/** Set a single LED brightness (0–15). */
	__attribute__((noinline, noclone)) void led(uint8_t x, uint8_t y, uint8_t level)
	{
		if (x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return;
		if (level > 15) level = 15;
		frame_[y * MONOME_WS_GRID_MAX_X + x] = level;
		frame_dirty_ = true;
	}

	/** Turn a single LED fully on. */
	void ledOn(uint8_t x, uint8_t y) { led(x, y, 15); }

	/** Turn a single LED off. */
	void ledOff(uint8_t x, uint8_t y) { led(x, y, 0); }

	/** Toggle an LED between off and full brightness. */
	void ledToggle(uint8_t x, uint8_t y)
	{
		if (x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return;
		uint8_t cur = frame_[y * MONOME_WS_GRID_MAX_X + x];
		led(x, y, cur ? 0 : 15);
	}

	/** Set LED on/off based on a boolean. */
	void ledSet(uint8_t x, uint8_t y, bool on)
	{
		led(x, y, on ? 15 : 0);
	}

	/** Set all LEDs to a level (0–15). */
	void all(uint8_t level)
	{
		if (level > 15) level = 15;
		for (int i = 0; i < MONOME_WS_GRID_MAX_X * MONOME_WS_GRID_MAX_Y; i++)
			frame_[i] = level;
		frame_dirty_ = true;
	}

	/** Turn all LEDs off. */
	void clear() { all(0); }

	/** Fill a rectangular region with a level. */
	void fill(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t level)
	{
		for (uint8_t y = y0; y <= y1 && y < MONOME_WS_GRID_MAX_Y; y++)
			for (uint8_t x = x0; x <= x1 && x < MONOME_WS_GRID_MAX_X; x++)
				led(x, y, level);
	}

	/** Fill a row with a level. */
	void row(uint8_t y, uint8_t level)
	{
		uint8_t w = cols() > 0 ? cols() : MONOME_WS_GRID_MAX_X;
		for (uint8_t x = 0; x < w; x++)
			led(x, y, level);
	}

	/** Fill a column with a level. */
	void col(uint8_t x, uint8_t level)
	{
		uint8_t h = rows() > 0 ? rows() : MONOME_WS_GRID_MAX_Y;
		for (uint8_t y = 0; y < h; y++)
			led(x, y, level);
	}

	/** Set global LED intensity (0–15). */
	void intensity(uint8_t level) { monome_ws_grid_led_intensity(level); }

	/** Read back the current LED level at (x, y). */
	uint8_t ledGet(uint8_t x, uint8_t y) const
	{
		if (x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return 0;
		return frame_[y * MONOME_WS_GRID_MAX_X + x];
	}

	/** Light LEDs under all currently held keys. */
	void showHeld(uint8_t level = 15)
	{
		if (!ready()) return;
		uint16_t col_mask = (cols() >= 16) ? 0xFFFFu : (uint16_t)((1u << cols()) - 1u);
		for (uint8_t y = 0; y < rows(); y++) {
			uint16_t m = (uint16_t)(held_row_mask_[y] & col_mask);
			while (m) {
				uint8_t x = (uint8_t)__builtin_ctz(m);
				led(x, y, level);
				m &= (uint16_t)(m - 1);
			}
		}
	}

	/* ---- batched frame writes ---- */

	/** Clear the local frame buffer without submitting yet. */
	void frameClear()
	{
		memset(frame_, 0, sizeof(frame_));
	}

	/** Set a local frame LED for redraw code that will submit once at the end. */
	void frameLed(int x, int y, uint8_t level)
	{
		if (x < 0 || y < 0 || x >= MONOME_WS_GRID_MAX_X || y >= MONOME_WS_GRID_MAX_Y) return;
		if (level > 15) level = 15;
		frame_[y * MONOME_WS_GRID_MAX_X + x] = level;
	}

	/** Set a known-good local frame LED inside a frameClear()/submitFrame() batch. */
	void frameLedUnchecked(int x, int y, uint8_t level)
	{
		frame_[y * MONOME_WS_GRID_MAX_X + x] = level;
	}

	/** Submit the accumulated local frame when core 1 can accept it. */
	void submitFrame()
	{
		frame_dirty_ = true;
		submitFrameIfPossible();
	}

	/** Raw pointer to the LED buffer (MONOME_WS_GRID_MAX_X * MONOME_WS_GRID_MAX_Y). */
	uint8_t *ledBuffer()
	{
		frame_dirty_ = true;
		return frame_;
	}

	/** Mark all quadrants dirty (call after directly writing ledBuffer). */
	void markDirty() { submitFrame(); }

private:
	void submitFrameIfPossible()
	{
		if (!frame_dirty_)
			return;
		if (monome_ws_grid_frame_submit(frame_))
			frame_dirty_ = false;
#ifdef MLR_PERF_PROFILING
		else
			mlr_perf_count_grid_frame_drop();
#endif
	}

	static void usb_core()
	{
		board_init();
		tusb_init();
		while (true) {
			monome_ws_task();
		}
	}

	bool    key_down_this_sample = false;
	bool    key_up_this_sample   = false;
	uint8_t last_event_x = 0;
	uint8_t last_event_y = 0;
	uint8_t any_key_held_count = 0;
	/* held_row_mask_[y]: bit x set => (x, y) currently held.
	 * held_col_mask_[x]: bit y set => (x, y) currently held.
	 * Both are updated together in poll() so they stay coherent. */
	uint16_t held_row_mask_[MONOME_WS_GRID_MAX_Y] = {};
	uint16_t held_col_mask_[MONOME_WS_GRID_MAX_X] = {};
	uint8_t frame_[MONOME_WS_GRID_MAX_X * MONOME_WS_GRID_MAX_Y] = {};
	bool    frame_dirty_ = false;
};

#endif /* MONOME_GRID_HPP */
