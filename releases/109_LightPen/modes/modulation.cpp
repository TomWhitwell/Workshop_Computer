#include "modes/modulation.h"
#include "pico.h"

namespace lp {

namespace {

// 0.0625Hz at black through 8Hz at white — seven octaves.
constexpr int32_t kRateBase = static_cast<int32_t>(HzToInc(1) >> 4);   // 0.0625Hz
constexpr int32_t kRateOct  = 7 * 4096;                                // Q12

} // namespace

void ModulationMode::OnEnter()
{
	line_.Init(0, kLineLen);
	clearPos_ = 0;
	lfoPhase_ = 0;
	fbState_ = 0;
	lfoQ15_ = 0;
	for (int i = 0; i < kStages; i++) ap_[i] = Ap1{};
}

void ModulationMode::OnDownPress()
{
	rotation_ = (rotation_ + 1) % 3;
	rotationChanged_ = true;
}

void ModulationMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	if (clearPos_ < kFxLen) clearPos_ = FxClearChunk(clearPos_);

	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t rateU  = u[rotation_];
	int32_t depthU = u[(rotation_ + 1) % 3];
	int32_t fbU    = u[(rotation_ + 2) % 3];

	lfoInc_ = static_cast<uint32_t>(pow2_scale(kRateBase, (rateU * kRateOct) >> 16));
	depth_  = depthU;
	// Short of unity: a flanger at exactly 1.0 never stops ringing.
	fb_ = (fbU * 30000) >> 16;

	// Main: the phaser fades out over the first third, then the delay lengthens
	// from flanger to chorus across the rest.
	phaseW_ = (c.main < kPhaseEnd) ? 32767 - ((c.main * 32767) / kPhaseEnd) : 0;
	int32_t up = (c.main > kPhaseEnd) ? c.main - kPhaseEnd : 0;
	base_ = kMinBase + ((up * (kMaxBase - kMinBase)) / (4095 - kPhaseEnd));

	// The LFO as CV, so the rest of the rack can move with it.
	out.cv2 = q16_to_cv5v(clamp_i32(lfoQ15_ + 32768, 0, 65535));
	out.pulse1 = lfoQ15_ > 0;

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(ModulationMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	lfoPhase_ += lfoInc_;
	const int32_t lfo = fast_sin(lfoPhase_);     // +/-32767
	lfoQ15_ = lfo;

	// depth_ is halved before it meets the LFO: lfo * depth_ at full scale is
	// 2147385345, which fits int32 with 98302 to spare. Not a margin to build on.
	const int32_t modQ15 = (lfo * (depth_ >> 1)) >> 15;   // +/-32767, scaled by depth

	const int32_t dry = in.audio1;
	const int32_t x = clamp_i32(dry + ((fbState_ * fb_) >> 15), -2047, 2047);

	// Phaser: sweep the allpass coefficient over about 0.15..0.85, which walks
	// four notches across the band without the cascade going unstable.
	int32_t aQ15 = clamp_i32(16384 + (modQ15 >> 2), 4915, 27852);
	int32_t ph = x;
	for (int i = 0; i < kStages; i++) ph = ap_[i].Process(ph, aQ15);
	// The notches only exist in the sum with the input.
	ph = (x + ph) >> 1;

	int32_t del = 0;
	if (clearPos_ >= kFxLen)
	{
		// Swing either side of the base time by up to half of it.
		int32_t t = base_ + ((base_ * modQ15) >> 16);
		t = clamp_i32(t, 2, static_cast<int32_t>(kLineLen) - 2);
		del = line_.TapQ16(t * 65536);
		line_.Write(x);
	}
	else
	{
		line_.Write(0);
	}

	const int32_t wet = del + (((ph - del) * phaseW_) >> 15);
	fbState_ = wet;

	int32_t s = clamp12((dry + wet) >> 1);
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;
}

} // namespace lp
