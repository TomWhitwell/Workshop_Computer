#include "modes/freeze.h"
#include "pico.h"

namespace lp {

namespace {

/// Interpolated read straight out of the shared buffer. This mode owns all of
/// gFx while frozen, and granular playback needs absolute positions rather
/// than Line's taps-into-the-past, so it indexes directly.
inline int32_t ReadAbs(uint32_t idx, int32_t fracQ16)
{
	uint32_t i0 = idx % kFxLen;
	uint32_t i1 = i0 + 1;
	if (i1 >= kFxLen) i1 = 0;
	int32_t s0 = gFx[i0];
	int32_t s1 = gFx[i1];
	return s0 + (((s1 - s0) * fracQ16) >> 16);
}

} // namespace

void FreezeMode::OnEnter()
{
	line_.Init(0, kFxLen);
	writeIdx_ = 0;
	clearPos_ = 0;
	frozen_ = false;
	armed_ = false;
	env_ = 0;
	trig_ = 0;
	spawnCount_ = 0;
	for (int i = 0; i < kVoices; i++) voice_[i].active = false;
}

void FreezeMode::OnDownPress()
{
	// Arm only. Freezing here would make every tap a 171ms hole, exactly the
	// trap Modes 4 and 7 hit with StartRecord().
	armed_ = true;
}

void FreezeMode::OnDownRelease(int ticks)
{
	armed_ = false;
	if (ticks < kTapTicks)
	{
		rotation_ = (rotation_ + 1) % 3;
		rotationChanged_ = true;
		return;
	}
	frozen_ = false;
	for (int i = 0; i < kVoices; i++) voice_[i].active = false;
}

void FreezeMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	if (clearPos_ < kFxLen) clearPos_ = FxClearChunk(clearPos_);

	// Freeze once the press has outlived a tap, not on the press itself.
	if (armed_ && c.downHeld && c.downTicks == kTapTicks && !frozen_)
	{
		frozen_ = true;
		// Start reading a whole buffer behind the write head: that is the
		// oldest sample still held, so the pad walks forward through the
		// captured moment rather than starting at its end.
		nextStart_ = writeIdx_;
		spawnCount_ = 0;
		out.ledFlash = 6;
	}

	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t sizeU  = u[rotation_];
	int32_t pitchU = u[(rotation_ + 1) % 3];
	int32_t densU  = u[(rotation_ + 2) % 3];

	dur_ = kMinGrain + (((kMaxGrain - kMinGrain) * sizeU) >> 16);
	// Two octaves centred on unity: 0.5x at black, 1x at mid grey, 2x at white.
	rateQ16_ = pow2_scale(kQ16One, ((pitchU - 32768) * 4096) >> 15);

	// Spawn interval from twice the grain (gaps, so it stutters) down to a
	// third of it (three voices overlapping, so it is solid).
	int32_t scaleQ16 = 131072 - ((densU * 109226) >> 16);
	spawnEvery_ = (dur_ * scaleQ16) >> 16;
	if (spawnEvery_ < 1) spawnEvery_ = 1;

	// Quarter-length ramps at each end of a grain, as a reciprocal so the audio
	// path has no divide.
	ramp_ = dur_ >> 2;
	if (ramp_ < 1) ramp_ = 1;
	rampInv_ = (32767 * 65536) / ramp_;

	mix_ = c.main << 3;                  // 0..4095 -> Q15
	if (mix_ > 32767) mix_ = 32767;

	out.pulse1 = trig_ > 0;
	if (trig_ > 0) trig_--;

	if (rotationChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(rotation_ + 1);
		rotationChanged_ = false;
	}
}

void __not_in_flash_func(FreezeMode::AudioTick)(const SensorFrame &, const Inputs &in, EngineOut &out)
{
	int32_t dry = in.audio1;
	int32_t s;

	if (!frozen_ || clearPos_ < kFxLen)
	{
		// Thawed: keep the buffer fed so a freeze always has 426ms behind it.
		gFx[writeIdx_] = static_cast<int16_t>(clamp_i32(dry, -32767, 32767));
		if (++writeIdx_ >= kFxLen) writeIdx_ = 0;
		s = dry;
	}
	else
	{
		if (--spawnCount_ <= 0)
		{
			spawnCount_ = spawnEvery_;
			for (int i = 0; i < kVoices; i++)
			{
				if (voice_[i].active) continue;
				voice_[i].active = true;
				voice_[i].pos = 0;
				voice_[i].dur = dur_;
				voice_[i].readIdx = nextStart_;
				voice_[i].readFrac = 0;
				// Forward by half a grain each time, with a little jitter so a
				// long hold does not settle into an audible cycle.
				nextStart_ = (nextStart_ + static_cast<uint32_t>(dur_ >> 1)
				              + (xorshift32(rng_) % 64)) % kFxLen;
				trig_ = kTrigTicks;
				break;
			}
		}

		int32_t acc = 0;
		for (int i = 0; i < kVoices; i++)
		{
			Grain &g = voice_[i];
			if (!g.active) continue;

			int32_t e = (g.pos < ramp_)             ? (g.pos * rampInv_) >> 16
			          : (g.pos > g.dur - ramp_)     ? ((g.dur - g.pos) * rampInv_) >> 16
			                                        : 32767;
			if (e < 0) e = 0;
			acc += mul_q15(ReadAbs(g.readIdx, g.readFrac), e);

			g.readFrac += rateQ16_;
			g.readIdx += static_cast<uint32_t>(g.readFrac >> 16);
			g.readFrac &= 0xFFFF;
			if (++g.pos >= g.dur) g.active = false;
		}
		// Three voices at full envelope would be 3x, so hold a third back.
		int32_t wet = (acc * 11) >> 5;
		s = mix_q15(dry, wet, mix_);
	}

	s = clamp12(s);
	out.audio1 = static_cast<int16_t>(s);
	out.audio2 = out.audio1;

	int32_t mag = (s < 0) ? -s : s;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
