#include "modes/colourfilter.h"
#include "pico.h"

namespace lp {

namespace {

/// Cubic soft clip, x in +/-4096, out in +/-2047.
///
/// f(x) = x - x^3/(3L^2) with L = 4096, which is flat-topped exactly at L and
/// worth 2L/3 there; the final (*3)>>2 maps that ceiling onto full scale. L is
/// 4096 rather than full scale so the shifts are exact — x*x reaches 2^24 and
/// stays inside int32 — and the 0.75 that costs is folded into the drive
/// constant, so the bottom of the drive travel is unity for small signals.
inline int32_t __attribute__((always_inline)) SoftClip(int32_t x)
{
	// Beyond L the cubic turns over and folds back, so limit first: past here
	// the shape is a hard ceiling, which is what full drive should sound like.
	x = clamp_i32(x, -4096, 4096);
	int32_t q = (x * x) >> 12;          // Q12 of (x/L)^2, 0..4096
	int32_t c = (x * q) >> 12;          // x^3/L^2, +/-4096
	int32_t y = ((x - c / 3) * 3) >> 2;
	// The ceiling lands on 2048 and the floor on -2049 — the shift floors, and
	// c/3 truncates toward zero, so the two ends miss by a LSB in opposite
	// directions. Trim to a real sample: the Svf's overflow headroom is argued
	// for inputs inside +/-2048, and an odd shaper should stay odd.
	return clamp_i32(y, -2047, 2047);
}

} // namespace

void ColourFilterMode::OnEnter()
{
	svf_.Reset();
	env_ = 0;
	gate_ = false;
}

void ColourFilterMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void ColourFilterMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t cutU  = u[rotation_];
	int32_t resU  = u[(rotation_ + 1) % 3];
	int32_t typeU = u[(rotation_ + 2) % 3];

	// Compressed into an audible band, NOT handed to the Svf raw. Raw, a dark
	// reading is a 40Hz low-pass — 42dB down at 440Hz — and since the wand's
	// resting state in ordinary room light IS dark, the mode read as broken
	// rather than as closed. The floor puts that rest state at 228Hz instead,
	// which sounds like a shut filter but still plays. 11/16 keeps the top at
	// exactly 65535 while staying inside int32; the obvious
	// (cutU * (65536 - floor)) >> 16 overflows at full scale.
	svf_.Set(kCutFloor + ((cutU * 11) >> 4), resU);
	// SvfBlend reads a 0..4095 knob; the colour is unipolar Q16.
	blend_.Set(typeU >> 4);
	// Three octaves of drive, 1x to 8x. 5461 = 4096 * 4/3 undoes the 0.75 the
	// soft clip's output scaling costs, so the bottom of the travel is unity.
	drive_ = pow2_scale(5461, c.main * 3);

	// Hysteresis, or a decaying tail chatters the gate as it crosses.
	if (gate_) { if (env_ < kGateOff) gate_ = false; }
	else       { if (env_ > kGateOn)  gate_ = true; }
	out.pulse1 = gate_;

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(ColourFilterMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	// 2048 * 43690 (8x) is 89M, well inside int32.
	int32_t v = SoftClip((in.audio1 * drive_) >> 12);

	svf_.Process(v);
	int32_t s = clamp12(blend_.Render(svf_));
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;

	// Rectified one-pole, fast up and slow down: ~0.7ms attack, ~43ms release,
	// so it tracks a transient but reads as a level rather than as the waveform.
	int32_t mag = (s < 0) ? -s : s;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
