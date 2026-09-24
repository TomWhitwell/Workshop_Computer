#include "modes/synesthesia.h"
#include "pico.h"

namespace lp {

namespace {

const Wave kPath[3] = { Wave::Sine, Wave::Saw, Wave::Square };

constexpr int32_t kEnvMax     = 1 << 30;
constexpr int32_t kAttackStep = kEnvMax / 48;   // ~1ms: fast, but not a click
constexpr int32_t kVoiceRes   = 16000;          // a little emphasis, fixed

/// Triangle fold: reflect off +/-2048 as many times as the drive needs.
/// Closed form over one 8192-unit period, so no loop in the interrupt, and
/// the identity for anything already inside the rails.
inline int32_t Fold(int32_t v)
{
	int32_t t = (v + 2048) & 8191;
	if (t > 4096) t = 8192 - t;
	return t - 2048;
}

} // namespace

void SynesthesiaMode::OnEnter()
{
	svf_.Reset();
	env_ = 0;
	attacking_ = false;
}

void SynesthesiaMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void SynesthesiaMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t cutoffU = u[rotation_];
	int32_t foldU   = u[(rotation_ + 1) % 3];
	int32_t decayU  = u[(rotation_ + 2) % 3];

	morph_.Set(kPath, 3, c.main);
	inc_ = note_to_inc((kBaseNote << 8) + ((pitchIn_ * kSemisQ8PerUnit) >> 6));
	svf_.Set(cutoffU, kVoiceRes);
	drive_ = 256 + ((foldU * 1792) >> 16);
	// Decay coefficient, time constant 2^20/k samples: k = 4 (~5.5s) with
	// full light, up ~10 octaves to ~4096 (~5ms) in the dark.
	decayK_ = pow2_scale(4, ((65535 - decayU) * 10) >> 4);

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(SynesthesiaMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	pitchIn_ = slew(pitchIn_, in.audio1 * 64, 8);

	if (in.gateRise) attacking_ = true;
	if (attacking_)
	{
		// Retriggers climb from wherever the envelope is, so no click.
		if (env_ >= kEnvMax - kAttackStep) { env_ = kEnvMax; attacking_ = false; }
		else env_ += kAttackStep;
	}
	else if (env_ > 0)
	{
		int32_t step = ((env_ >> 16) * decayK_) >> 4;
		// Once the step rounds to zero the tail is below -72dB: finish it.
		env_ = (step == 0) ? 0 : env_ - step;
	}

	phase_ += inc_;
	int32_t v = morph_.Render(phase_) >> 4;          // +/-2047
	v = Fold((v * drive_) >> 8);
	svf_.Process(v);

	int32_t s = (svf_.Lp() * (env_ >> 15)) >> 15;    // 32767 * 32768 fits
	out.audio1 = clamp12(s);
	out.audio2 = out.audio1;
	out.cv2 = q16_to_cv5v(env_ >> 14);
}

} // namespace lp
