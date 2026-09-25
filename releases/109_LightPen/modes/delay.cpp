#include "modes/delay.h"
#include "pico.h"

namespace lp {

namespace {

/// Unipolar Q16 -> delay in Q16 samples, exponential over the line's range.
/// Even in octaves, so a hand moving at a steady speed bends the pitch at a
/// steady rate — a linear map crowds every musical time into the first third.
inline int32_t TimeQ16(int32_t u, int32_t lo, int32_t hi)
{
	// log2(hi/lo) octaves across the travel, in Q12.
	int32_t octQ12 = (log2_q16(static_cast<uint32_t>(hi))
	                  - log2_q16(static_cast<uint32_t>(lo))) >> 4;
	int32_t t = pow2_scale(lo * 256, (u * octQ12) >> 16) * 256;
	// pow2_scale interpolates LINEARLY inside an octave, and 2^x is convex, so
	// it always reads high — by up to 6%, which here asks for 441ms from a
	// 416ms line. Tap() would wrap that modulo the buffer and hand back a
	// completely different delay time. Clamp; the curve is close enough
	// everywhere else and this is the only place it has to be bounded.
	return clamp_i32(t, lo * 65536, hi * 65536);
}

} // namespace

void DelayMode::OnEnter()
{
	line_.Init(0, kLen);
	tone_.Reset();
	clearPos_ = 0;
	env_ = 0;
	clockCount_ = 0;
	trig_ = 0;
}

void DelayMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void DelayMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	// Wipe the line over the first 20 ticks; the wet path stays muted until
	// then, so entering the mode never plays back whatever the last one left.
	if (clearPos_ < kFxLen) clearPos_ = FxClearChunk(clearPos_);

	const int32_t u[3] = { f.ur, f.ug, f.ub };
	targetQ16_ = TimeQ16(u[rotation_], kMinTime, kMaxTime);
	fb_  = (u[(rotation_ + 1) % 3] * kMaxFb) >> 16;
	mix_ = u[(rotation_ + 2) % 3] >> 1;              // Q16 -> Q15

	// Main: dark and tape-like at the bottom, undamped and digital at the top.
	tone_.SetDamp(((4095 - c.main) * 26000) >> 12);

	out.pulse1 = trig_ > 0;
	if (trig_ > 0) trig_--;

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(DelayMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	// Glide to the new time rather than jumping to it: this is what bends the
	// pitch of the material already in the line, and it is the mode's voice.
	timeQ16_ = slew_exact(timeQ16_, targetQ16_, 10);

	int32_t dry = in.audio1;
	int32_t wet = 0;

	if (clearPos_ >= kFxLen)
	{
		wet = line_.TapQ16(timeQ16_);
		line_.Write(dry + ((tone_.Process(wet) * fb_) >> 15));

		// One pulse per delay period, reloaded from the live time so the clock
		// follows the hand.
		if (--clockCount_ <= 0)
		{
			clockCount_ = timeQ16_ >> 16;
			if (clockCount_ < 1) clockCount_ = 1;
			trig_ = kTrigTicks;
		}
	}
	else
	{
		// Still wiping: pass the input straight through so the mode is usable
		// from the instant you arrive, and keep the line fed with silence.
		line_.Write(0);
	}

	int32_t s = clamp12(mix_q15(dry, wet, mix_));
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;

	int32_t mag = (s < 0) ? -s : s;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
