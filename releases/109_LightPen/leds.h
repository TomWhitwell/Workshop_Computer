// leds.h — what the six LEDs show. main.cpp does the writes.
//
// Layout (three rows of two):   0 1
//                               2 3
//                               4 5
// Left column = the mode, as a 3-bit number, LED 0 the fours and LED 4 the
// ones. It counts from ONE: Mode 1 lights one LED, Mode 7 lights all three.
// No mode is ever a dark column, which is what makes the display readable at
// a glance rather than something to decode.
//
// Three bits run out at 7, so modes past that light the same three LEDs at
// HALF BRIGHTNESS and count again from one: Mode 8 is a dim 1, Mode 9 a dim 2.
// Brightness is the fourth bit. That leaves room to 14 without touching the
// right column, which belongs to the sensor.
//
// Right column = live R (1), G (3), B (5) after the global sensitivity and
// slew.
//
// On a mode change the left column blinks its pattern for a second. A mode's
// own option change (chord, rotation, scale, kit) instead blinks that option's
// number as the first N LEDs in reading order.

#pragma once
#include <cstdint>
#include "sensors.h"

namespace lp {

class Leds
{
public:
	static constexpr int kFlashTicks = kCtrlRate;   // 1s
	/// The modes past 7. Not 2048: an LED's perceived brightness is far from
	/// linear in its duty cycle, and half the number reads as nearly as bright.
	/// This is the level that actually looks like half next to kFullMode.
	static constexpr uint16_t kHalfMode = 1100;
	static constexpr uint16_t kFullMode = 4095;

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
		// mode is the index, 0-based; the display counts from one.
		int n = mode + 1;
		uint16_t on = kFullMode;
		if (n > 7)
		{
			n -= 7;             // count again, dimly
			on = kHalfMode;
		}
		if (!showMode) on = 0;
		level[0] = (n & 4) ? on : 0;
		level[2] = (n & 2) ? on : 0;
		level[4] = (n & 1) ? on : 0;
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
