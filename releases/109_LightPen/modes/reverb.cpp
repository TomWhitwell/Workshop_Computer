#include "modes/reverb.h"
#include "pico.h"

namespace lp {

namespace {

/// The tank's total footprint in the shared buffer, laid out back to back from
/// offset 0. Checked here rather than trusted: adding a comb or lengthening
/// pre-delay past the buffer would otherwise silently scribble over whatever
/// region follows.
constexpr uint32_t kTankLen = 11998 + 1701 + 4096;
static_assert(kTankLen <= kFxLen, "reverb tank does not fit the shared FX buffer");

/// Input attenuation before the tank. Eight combs at 0.92 feedback each reach
/// about 12x their input and then sum, so the raw signal would be far over full
/// scale by the output.
///
/// Measured in lpsim on the worst case (biggest room, no damping, sustained
/// tone): >>4 leaves the tank 13.8dB down, which is too quiet to hear under the
/// dry signal; >>2 peaks at 2094 and clips. >>3 is -7.8dB with a peak of 1042,
/// so it has 6dB of headroom and is still audibly a reverb.
constexpr int kInShift = 3;

} // namespace

void ReverbMode::OnEnter()
{
	uint32_t at = 0;
	for (int i = 0; i < kCombs; i++)
	{
		comb_[i].Init(at, kCombLen[i]);
		comb_[i].Reset();
		at += kCombLen[i];
	}
	for (int i = 0; i < kAps; i++)
	{
		ap_[i].Init(at, kApLen[i]);
		at += kApLen[i];
	}
	pre_.Init(at, kPreLen);

	clearPos_ = 0;
	env_ = 0;
}

void ReverbMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void ReverbMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	if (clearPos_ < kFxLen) clearPos_ = FxClearChunk(clearPos_);

	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t sizeU  = u[rotation_];
	int32_t brightU = u[(rotation_ + 1) % 3];
	mix_ = u[(rotation_ + 2) % 3] >> 1;              // Q16 -> Q15

	int32_t fb = kFbMin + (((kFbMax - kFbMin) * sizeU) >> 16);
	// Green is BRIGHTNESS, so it runs the damping backwards: dark wand means
	// heavy damping and a dark tail, which is the right thing to hear on
	// arriving in the mode.
	int32_t damp = 26000 - ((brightU * 26000) >> 16);
	for (int i = 0; i < kCombs; i++) comb_[i].Set(fb, damp);

	// Pre-delay from Main, in whole samples: it is a fixed offset, not a swept
	// one, so there is nothing to interpolate.
	preTap_ = (c.main * static_cast<int32_t>(kPreLen - 1)) >> 12;

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(ReverbMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	int32_t dry = in.audio1;
	int32_t wet = 0;

	if (clearPos_ >= kFxLen)
	{
		pre_.Write(dry);
		int32_t x = pre_.Tap(static_cast<uint32_t>(preTap_)) >> kInShift;

		// Parallel combs: each is one set of room modes, and the sum is the
		// tank. Averaged rather than summed, or eight of them would be 18dB up.
		int32_t sum = 0;
		for (int i = 0; i < kCombs; i++) sum += comb_[i].Process(x);
		wet = sum >> 3;

		// Series allpass: diffusion. Turns eight combs' discrete echoes into a
		// wash without colouring the result the way another comb would.
		for (int i = 0; i < kAps; i++) wet = ap_[i].Process(wet);
	}
	else
	{
		pre_.Write(0);
	}

	int32_t s = clamp12(mix_q15(dry, wet, mix_));
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;

	int32_t mag = (s < 0) ? -s : s;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
