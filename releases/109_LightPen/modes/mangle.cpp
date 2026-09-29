#include "modes/mangle.h"
#include "pico.h"

namespace lp {

namespace {

constexpr int32_t kCarrLo = 20;      // Hz
constexpr int32_t kCarrOct = 7 * 4096;   // ~20Hz to 2.5kHz, Q12

} // namespace

void MangleMode::OnEnter()
{
	holdCount_ = 0;
	held_ = 0;
	carrPhase_ = 0;
	env_ = 0;
}

void MangleMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void MangleMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t crushU = u[rotation_];
	int32_t foldU  = u[(rotation_ + 1) % 3];
	int32_t ringU  = u[(rotation_ + 2) % 3];

	// One colour, both crushes. Rate: 1 to 32 samples held. Depth: 12 bits down
	// to 3, as a mask — the signal is 12-bit signed, so dropping n low bits is
	// exactly an n-bit reduction.
	holdN_ = 1 + ((crushU * 31) >> 16);
	int32_t drop = (crushU * 9) >> 16;               // 0..8 bits away
	mask_ = ~((1 << drop) - 1);

	// 1x to 8x into the folder, Q8.
	drive_ = 256 + ((foldU * 1792) >> 16);
	ring_ = ringU >> 1;                              // Q16 -> Q15

	carrInc_ = static_cast<uint32_t>(
		pow2_scale(static_cast<int32_t>(HzToInc(kCarrLo)), (c.main * kCarrOct) >> 12));

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(MangleMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	// Rate crush, then bit crush, on the held value: sampling and quantising in
	// that order is what a cheap converter actually does.
	if (++holdCount_ >= holdN_)
	{
		holdCount_ = 0;
		held_ = in.audio1 & mask_;
	}

	// Fold. The drive is applied first so the folder has something to reflect.
	int32_t v = fold12((held_ * drive_) >> 8);

	// Ring modulation, mixed rather than replacing: at full depth it is a true
	// ring modulator, and below that the carrier reads as tremolo over the
	// original, which is the more usable half of the control.
	carrPhase_ += carrInc_;
	int32_t ringed = mul_q15(v, fast_sin(carrPhase_));
	v = v + (((ringed - v) * ring_) >> 15);

	int32_t s = clamp12(v);
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;

	int32_t mag = (s < 0) ? -s : s;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
