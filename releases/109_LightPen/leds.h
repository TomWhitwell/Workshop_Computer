// leds.h — what the six LEDs show. main.cpp does the writes.
//
// Layout (three rows of two):   0 1
//                               2 3
//                               4 5
// Left column = the mode, as a 3-bit number, LED 0 the fours and LED 4 the
// ones. It counts from zero, so mode 1 is all three dark and mode 8 is all
// three lit. Right column = live R (1), G (3), B (5) after the global
// sensitivity and slew.
//
// On a mode change the left column blinks that pattern for a second, so a
// change is visible even when it lands on a dark pattern. A mode's own option
// change (chord, rotation, scale) instead blinks its option number as the
// first N LEDs in reading order.

#pragma once
#include <cstdint>
#include "sensors.h"

namespace lp {

class Leds
{
public:
	static constexpr int kFlashTicks = kCtrlRate;   // 1s

	/// An option number, 1..6, blinked over everything else.
	void Flash(int count, bool blink)
	{
		count_ = count;
		blink_ = blink;
		ticks_ = kFlashTicks;
	}

	void FlashMode() { modeTicks_ = kFlashTicks; }

	/// Control rate. Fills level[6], 0..4095.
	void Tick(int mode, const SensorFrame &f, uint16_t level[6])
	{
		phase_++;
		bool fast = (phase_ % 188) < 94;   // 8Hz

		if (ticks_ > 0)
		{
			ticks_--;
			bool lit = !blink_ || fast;
			for (int i = 0; i < 6; i++) level[i] = (lit && i < count_) ? 4095 : 0;
			return;
		}

		bool showMode = true;
		if (modeTicks_ > 0)
		{
			modeTicks_--;
			showMode = fast;
		}
		level[0] = (showMode && (mode & 4)) ? 4095 : 0;
		level[2] = (showMode && (mode & 2)) ? 4095 : 0;
		level[4] = (showMode && (mode & 1)) ? 4095 : 0;
		level[1] = static_cast<uint16_t>(f.ur >> 4);
		level[3] = static_cast<uint16_t>(f.ug >> 4);
		level[5] = static_cast<uint16_t>(f.ub >> 4);
	}

private:
	int      count_ = 0;
	int      ticks_ = 0;
	int      modeTicks_ = 0;
	uint32_t phase_ = 0;
	bool     blink_ = false;
};

} // namespace lp
