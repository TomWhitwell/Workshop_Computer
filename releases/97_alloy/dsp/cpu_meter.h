// On-hardware CPU load meter for the 48 kHz ProcessSample() callback.
//
// Usage per sample:
//   meter.BeginSample();
//   ... audio work ...
//   meter.EndSample();
//
// Headroom() returns 0..4095 (4095 = fully idle, 0 = at/over budget), based
// on the peak sample duration over the last window. Overrun() latches true
// forever once any single sample exceeds the sample period.

#ifndef ALLOY_DSP_CPU_METER_H_
#define ALLOY_DSP_CPU_METER_H_

#include <cstdint>

#include "pico/time.h"

namespace xmod {

class CpuMeter
{
public:
	// budget_us: the sample period (1e6 / sample rate), ~20us at 48kHz.
	explicit CpuMeter(uint32_t budget_us) : budget_us_(budget_us) {}

	void BeginSample()
	{
		start_us_ = time_us_32();
	}

	void EndSample()
	{
		uint32_t elapsed = time_us_32() - start_us_;
		if (elapsed > window_peak_us_) window_peak_us_ = elapsed;
		if (elapsed > budget_us_) overrun_ = true;

		if (++window_count_ >= kWindowSamples)
		{
			peak_us_ = window_peak_us_;
			window_peak_us_ = 0;
			window_count_ = 0;
		}
	}

	// 0..4095; 4095 = no load, 0 = peak sample used the whole budget (or more).
	int32_t Headroom() const
	{
		uint32_t used = peak_us_ > budget_us_ ? budget_us_ : peak_us_;
		return static_cast<int32_t>(((budget_us_ - used) * 4095) / budget_us_);
	}

	bool Overrun() const { return overrun_; }

private:
	// ~0.17s at 48kHz: fast enough to feel live on an LED.
	static constexpr uint32_t kWindowSamples = 8192;

	uint32_t budget_us_;
	uint32_t start_us_ = 0;
	uint32_t window_peak_us_ = 0;
	uint32_t window_count_ = 0;
	uint32_t peak_us_ = 0;
	bool overrun_ = false;
};

} // namespace xmod

#endif // ALLOY_DSP_CPU_METER_H_
