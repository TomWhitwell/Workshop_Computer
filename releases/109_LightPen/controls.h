// controls.h — debounced toggle switch: mode cycling and the Down button.
//
// Up and Middle are latching positions; Down is momentary. Up advances the
// mode when the switch ARRIVES there, so staying in Up is just "playing".

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

// Same order as ComputerCard::Switch, so main.cpp can static_cast between them.
enum class Sw : uint8_t { Down = 0, Middle = 1, Up = 2 };

class Controls
{
public:
	/// A position must read steadily for this long before it counts. The
	/// library's switch decode is a threshold on a smoothed ADC channel with no
	/// hysteresis, so contact bounce near a threshold can read as several
	/// transitions; without this, one click could advance two modes.
	static constexpr int kDebounceTicks = kCtrlRate / 50;   // 20ms

	static constexpr uint8_t kEvModeNext    = 1;
	static constexpr uint8_t kEvDownPress   = 2;
	static constexpr uint8_t kEvDownRelease = 4;
	static constexpr uint8_t kEvDownHold    = 8;

	/// Call once, when boot is over. If the switch is still held Down (the
	/// calibration hold), that hold is swallowed until it is released, so it
	/// never reaches the first mode as a press.
	void Init(Sw sw)
	{
		stable_ = candidate_ = sw;
		count_ = kDebounceTicks;
		ignoreDown_ = (sw == Sw::Down);
	}

	/// Control rate. Returns a mask of kEv* events.
	uint8_t Tick(Sw raw)
	{
		uint8_t ev = 0;

		if (raw != candidate_)
		{
			candidate_ = raw;
			count_ = 0;
		}
		else if (count_ < kDebounceTicks && ++count_ == kDebounceTicks && candidate_ != stable_)
		{
			Sw prev = stable_;
			stable_ = candidate_;

			if (prev == Sw::Down)
			{
				if (!ignoreDown_) ev |= kEvDownRelease;
				ignoreDown_ = false;
			}
			if (stable_ == Sw::Up) ev |= kEvModeNext;
			if (stable_ == Sw::Down && !ignoreDown_)
			{
				ev |= kEvDownPress;
				downTicks_ = 0;
				holdFired_ = false;
			}
		}

		if (DownHeld() && !holdFired_ && ++downTicks_ >= kHoldTicks)
		{
			holdFired_ = true;
			ev |= kEvDownHold;
		}
		return ev;
	}

	bool DownHeld() const { return stable_ == Sw::Down && !ignoreDown_; }

	/// How long Down has been held, in control ticks, saturating at kHoldTicks
	/// (the counter stops once the hold has fired). Both the press and the
	/// release are reported after the same 20ms of debounce, so the two lags
	/// cancel and this measures the physical press to within a tick.
	int PressTicks() const { return downTicks_; }

private:
	Sw   stable_     = Sw::Middle;
	Sw   candidate_  = Sw::Middle;
	int  count_      = 0;
	int  downTicks_  = 0;
	bool holdFired_  = false;
	bool ignoreDown_ = false;
};

} // namespace lp
