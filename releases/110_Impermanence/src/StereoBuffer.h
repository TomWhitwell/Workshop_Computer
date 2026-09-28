#pragma once
#include <cstdint>

namespace imp
{

// Three quarters of a second of stereo audio, one 16-bit value per sample,
// left and right side by side.
//
// Why 0.75 seconds: the playheads read this buffer for every sounding voice
// on every sample, inside the audio interrupt. An earlier version held a
// full second by packing two 12-bit samples into every three bytes, which
// fit the same 144KB -- but unpacking cost ~85 cycles per voice per sample,
// the biggest single cost in the interrupt's worst moments. Plain 16-bit
// samples read in a couple of instructions. Same memory, a quarter less
// loop, far more timing headroom.
class StereoBuffer
{
public:
	static constexpr int32_t kFrames = 36000; // 0.75 seconds at 48kHz

	void Write(int32_t frame, int32_t left, int32_t right)
	{
		int16_t *p = samples_ + frame * 2;
		p[0] = (int16_t)Clamp12(left);
		p[1] = (int16_t)Clamp12(right);
	}

	void Read(int32_t frame, int32_t &left, int32_t &right) const
	{
		const int16_t *p = samples_ + frame * 2;
		left = p[0];
		right = p[1];
	}

	// Frame i and the frame after it (wrapping to 0 at `length`), for
	// interpolation.
	void ReadPair(int32_t i, int32_t length, int32_t &l0, int32_t &r0, int32_t &l1, int32_t &r1) const
	{
		const int16_t *p = samples_ + i * 2;
		const int16_t *q = i + 1 < length ? p + 2 : samples_;
		l0 = p[0];
		r0 = p[1];
		l1 = q[0];
		r1 = q[1];
	}

	// Silence kClearBlockFrames frames starting at `frame` (a multiple of
	// kClearBlockFrames): sixteen word writes, cheap enough to clear the whole
	// buffer a block per sample (2250 samples, ~47ms) without a burst.
	static constexpr int32_t kClearBlockFrames = 16;
	void ClearBlock(int32_t frame)
	{
		uint32_t *w = reinterpret_cast<uint32_t *>(samples_ + frame * 2);
		for (int i = 0; i < kClearBlockFrames; i++)
			w[i] = 0;
	}

	// Scale one frame by gainQ12 (0..4096), for fading a take's ends.
	void ScaleFrame(int32_t frame, int32_t gainQ12)
	{
		int16_t *p = samples_ + frame * 2;
		p[0] = (int16_t)((p[0] * gainQ12) >> 12);
		p[1] = (int16_t)((p[1] * gainQ12) >> 12);
	}

	// Recording clamps to the Computer's 12-bit audio range.
	static int32_t Clamp12(int32_t x)
	{
		return x < -2048 ? -2048 : (x > 2047 ? 2047 : x);
	}

private:
	static_assert(kFrames % kClearBlockFrames == 0, "buffer must be whole clear blocks");
	alignas(4) int16_t samples_[kFrames * 2];
};

} // namespace imp
