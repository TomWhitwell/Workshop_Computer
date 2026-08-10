#pragma once

#include <cstdint>

namespace xmod
{

// Sample-domain tap clock. Taps are expected to be forwarded as manual clock
// events by the caller; this class starts free-running after the second valid
// tap and suppresses its own edges while an external clock is patched.
class TapClock
{
public:
	static constexpr uint32_t kSampleRate = 48000;
	static constexpr uint32_t kDefaultPeriod = kSampleRate / 2; // 120 BPM

	constexpr bool Process(bool external_clock_patched)
	{
		++sample_count_;

		if (!running_)
		{
			return false;
		}

		if (external_clock_patched)
		{
			samples_since_edge_ = 0;
			return false;
		}

		++samples_since_edge_;
		if (samples_since_edge_ >= period_samples_)
		{
			samples_since_edge_ = 0;
			return true;
		}
		return false;
	}

	constexpr void Tap()
	{
		if (have_previous_tap_)
		{
			const uint64_t interval = sample_count_ - previous_tap_sample_;
			if (interval >= kMinimumPeriod && interval <= kMaximumPeriod)
			{
				period_samples_ = static_cast<uint32_t>(interval);
				running_ = true;
			}
		}

		previous_tap_sample_ = sample_count_;
		have_previous_tap_ = true;
		samples_since_edge_ = 0;
	}

	constexpr void Stop()
	{
		running_ = false;
		have_previous_tap_ = false;
		samples_since_edge_ = 0;
	}

	constexpr bool running() const { return running_; }
	constexpr uint32_t period_samples() const { return period_samples_; }

private:
	// 20..300 BPM keeps accidental double taps and very stale taps out.
	static constexpr uint32_t kMinimumPeriod = (kSampleRate * 60U) / 300U;
	static constexpr uint32_t kMaximumPeriod = (kSampleRate * 60U) / 20U;

	uint64_t sample_count_ = 0;
	uint64_t previous_tap_sample_ = 0;
	uint32_t period_samples_ = kDefaultPeriod;
	uint32_t samples_since_edge_ = 0;
	bool have_previous_tap_ = false;
	bool running_ = false;
};

} // namespace xmod
