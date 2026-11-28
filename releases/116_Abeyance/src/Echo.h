#pragma once
#include <cstdint>
#include "Dsp.h"

namespace ab
{

// The piano's dark echo: a 1.15s stereo delay with feedback, darkened at
// 2.2kHz, running at 16kHz to save memory (with that low cut, the lower
// rate costs nothing you'd hear). The repeats cross from side to side, each
// one darker than the last.
//
//   piano L --(avg of 3)--> [ line L, 1.15s ] --> low-pass --+--> echo L
//                       ^                                    |
//                       +---- x feedback <-- echo R          |
//   (and the same for R, fed from echo L)                    v
//
// Timing, inside the audio interrupt: the input is averaged over three
// samples (a gentle anti-alias filter for the drop to 16kHz); then the left
// line runs on the first sample of each three, the right on the second,
// nothing on the third. The output steps smoothly back up to 48kHz by
// straight-line interpolation between 16kHz outputs.
//
// 18,400 samples per side: 74KB.
class Echo
{
public:
	static constexpr int32_t kClearCalls = (18400 + 63) / 64; // see ClearSome()

	__attribute__((noinline)) void Process(int32_t inL, int32_t inR, int32_t &outL, int32_t &outR)
	{
		sumL_ += inL;
		sumR_ += inR;
		if (phase_ == 0)
		{
			// The left line, at 16kHz. 21845 / 65536 = 1/3.
			int32_t x = (sumL_ * 21845) >> 16;
			sumL_ = 0;
			Line(bufL_, x, echoR_, lpL_, prevL_, echoL_);
		}
		else if (phase_ == 1)
		{
			int32_t x = (sumR_ * 21845) >> 16;
			sumR_ = 0;
			Line(bufR_, x, echoL_, lpR_, prevR_, echoR_);
			// Both lines have moved on: next position.
			w_ = w_ + 1 >= kLength ? 0 : w_ + 1;
		}
		// Straight lines between the 16kHz outputs. Each side's output is
		// one 16kHz step behind, so it can interpolate towards the newest.
		int32_t k = kStep[phase_];
		outL = prevL_ + (((echoL_ - prevL_) * k) >> 16);
		k = kStep[phase_ == 0 ? 2 : phase_ - 1]; // the right side updates one sample later
		outR = prevR_ + (((echoR_ - prevR_) * k) >> 16);
		phase_ = phase_ == 2 ? 0 : phase_ + 1;
	}

	// Empty the echo a slice at a time: kClearPerCall points of each side
	// per call, round and round. Called every sample between pieces (in
	// place of Process), it's empty after kClearCalls calls.
	static constexpr int32_t kClearPerCall = 64;
	void ClearSome()
	{
		if (clear_ == 0)
			sumL_ = sumR_ = lpL_ = lpR_ = echoL_ = echoR_ = prevL_ = prevR_ = 0;
		for (int32_t i = 0; i < kClearPerCall; i++)
			bufL_[clear_ + i] = bufR_[clear_ + i] = 0;
		clear_ += kClearPerCall;
		if (clear_ >= kLength)
			clear_ = 0;
	}

private:
	static constexpr int32_t kLength = 18400;	   // 1.15s at 16kHz
	static constexpr int32_t kFeedbackQ12 = 1843;  // 0.45: repeats fade out over about 10s
	static constexpr int32_t kDarkQ15 = 18940;	   // one-pole at 2.2kHz, 16kHz rate
	static constexpr int32_t kStep[3] = {21845, 43690, 65536}; // 1/3, 2/3, 1 (Q16)

	// One side: read the oldest sample, darken it, and write the input
	// plus the other side's echo back in.
	// The feedback rounds towards zero and the low-pass settles exactly
	// (Dsp.h), so the repeats fade all the way to silence.
	__attribute__((always_inline)) void Line(int16_t *buf, int32_t x, int32_t crossEcho, int32_t &lp, int32_t &prev,
											 int32_t &echo)
	{
		int32_t oldest = buf[w_];
		prev = echo;
		echo = OnePole(lp, oldest, kDarkQ15);
		buf[w_] = (int16_t)Sat16(x + ScaleTowardZero(crossEcho, kFeedbackQ12));
	}

	int32_t phase_ = 0;
	int32_t w_ = 0;
	int32_t clear_ = 0;
	int32_t sumL_ = 0, sumR_ = 0;
	int32_t lpL_ = 0, lpR_ = 0;
	int32_t echoL_ = 0, echoR_ = 0;
	int32_t prevL_ = 0, prevR_ = 0;
	// Room for the last slice of a clear to run past the end.
	int16_t bufL_[kLength + kClearPerCall] = {};
	int16_t bufR_[kLength + kClearPerCall] = {};
};

} // namespace ab
