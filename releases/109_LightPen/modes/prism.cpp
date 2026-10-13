#include "modes/prism.h"
#include "pico.h"

namespace lp {

namespace {

const Wave kPath[3] = { Wave::Sine, Wave::Tri, Wave::Square };

constexpr int32_t kEnvMax      = 1 << 30;
constexpr int32_t kAttackStep  = kEnvMax / 240;   // ~5ms
constexpr uint8_t kReleaseShift = 12;             // ~85ms
constexpr int32_t kVoiceRes    = 14000;

} // namespace

void PrismMode::OnEnter()
{
	svf_.Reset();
	env_ = 0;
	attacking_ = false;
	crushCount_ = 0;
	held_ = 0;
}

void PrismMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void PrismMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t fmU    = u[rotation_];
	int32_t crushU = u[(rotation_ + 1) % 3];
	int32_t cutU   = u[(rotation_ + 2) % 3];

	morph_.Set(kPath, 3, c.main);
	inc_ = note_to_inc((kBaseNote << 8) + ((pitchIn_ * kSemisQ8PerUnit) >> 6));
	svf_.Set(cutU, kVoiceRes);
	// Up to a full cycle of phase modulation: 32767 * 131070 is 2^32, and the
	// multiply below is deliberately modular.
	fmDepth_ = fmU * 2;
	crushN_ = 1 + (crushU >> 10);   // 1..64 samples held

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(PrismMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	pitchIn_ = slew(pitchIn_, in.audio1 * 64, 8);

	if (in.gateRise) attacking_ = true;
	if (attacking_)
	{
		if (env_ >= kEnvMax - kAttackStep) { env_ = kEnvMax; attacking_ = false; }
		else env_ += kAttackStep;
	}
	else if (!in.gate && env_ > 0)
	{
		int32_t step = env_ >> kReleaseShift;
		env_ = (step == 0) ? 0 : env_ - step;   // the tail below -72dB is done
	}

	modPhase_ += inc_ * 2;                      // modulator an octave up
	phase_ += inc_;
	// Unsigned on purpose: the product wraps modulo 2^32, which is exactly
	// what a phase offset wants, including for a negative modulator.
	uint32_t ph = phase_ + static_cast<uint32_t>(fast_sin(modPhase_))
	                     * static_cast<uint32_t>(fmDepth_);

	int32_t v = morph_.Render(ph) >> 4;         // +/-2047

	if (++crushCount_ >= crushN_)
	{
		crushCount_ = 0;
		held_ = v;
	}

	svf_.Process(held_);
	int32_t s = (svf_.Lp() * (env_ >> 15)) >> 15;
	out.audio1 = clamp12(s);
	out.audio2 = out.audio1;
	out.cv2 = q16_to_cv5v(env_ >> 14);
}

} // namespace lp
