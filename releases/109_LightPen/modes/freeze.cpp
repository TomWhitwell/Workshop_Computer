#include "modes/freeze.h"
#include "pico.h"

namespace lp {

namespace {

/// Interpolated read straight out of the shared buffer. This mode owns all of
/// gFx while frozen, and granular playback needs absolute positions rather than
/// Line's taps-into-the-past, so it indexes directly.
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
	writeIdx_ = 0;
	clearPos_ = 0;
	frozen_ = false;
	armed_ = false;
	toggled_ = false;
	env_ = 0;
	trig_ = 0;
	spawnCount_ = 0;
	nextRight_ = false;
	for (int i = 0; i < kVoices; i++) voice_[i].active = false;
}

void FreezeMode::OnDownPress()
{
	// Arm only. The toggle happens once the press has outlived a tap, so a tap
	// is free to mean something else.
	armed_ = true;
	toggled_ = false;
}

void FreezeMode::OnDownRelease(int ticks)
{
	armed_ = false;
	if (ticks < kTapTicks && !toggled_)
	{
		rotation_ = (rotation_ + 1) % 3;
		rotationChanged_ = true;
	}
	toggled_ = false;
}

void FreezeMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	if (clearPos_ < kFxLen) clearPos_ = FxClearChunk(clearPos_);

	// A hold TOGGLES. Latching matters here: holding the switch down uses the
	// hand that should be moving the wand, which is most of the instrument.
	if (armed_ && !toggled_ && c.downHeld && c.downTicks == kTapTicks)
	{
		toggled_ = true;
		frozen_ = !frozen_;
		if (frozen_)
		{
			// Start a whole buffer behind the write head — the oldest sample
			// still held — so the pad walks forward through the captured moment.
			nextStart_ = writeIdx_;
			spawnCount_ = 0;
		}
		else
		{
			for (int i = 0; i < kVoices; i++) voice_[i].active = false;
		}
		out.ledFlash = 6;
	}

	const int32_t u[3] = { f.ur, f.ug, f.ub };
	int32_t sizeU  = u[rotation_];
	int32_t pitchU = u[(rotation_ + 1) % 3];
	int32_t densU  = u[(rotation_ + 2) % 3];

	dur_ = kMinGrain + (((kMaxGrain - kMinGrain) * sizeU) >> 16);
	// Two octaves centred on unity: 0.5x at black, 1x at mid grey, 2x at white.
	rateQ16_ = pow2_scale(kQ16One, ((pitchU - 32768) * 4096) >> 15);

	// Spawn interval from half a grain down to a third. Never LONGER than the
	// grain: at 100% the triangular envelopes would meet at zero and the pad
	// would pulse to silence at every join. Half is where two of them sum flat,
	// so even a dark wand gives a continuous pad.
	int32_t scaleQ16 = 32768 - ((densU * 10923) >> 16);
	spawnEvery_ = (dur_ * scaleQ16) >> 16;
	if (spawnEvery_ < 1) spawnEvery_ = 1;

	ramp_ = dur_ >> 2;
	if (ramp_ < 1) ramp_ = 1;
	rampInv_ = (32767 * 65536) / ramp_;

	scatter_ = c.main << 4;              // 0..4095 -> 0..65520
	if (scatter_ > 65535) scatter_ = 65535;

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
	int32_t l, r;

	if (!frozen_ || clearPos_ < kFxLen)
	{
		// Thawed: keep the buffer fed so a freeze always has 426ms behind it.
		gFx[writeIdx_] = static_cast<int16_t>(clamp_i32(dry, -32767, 32767));
		if (++writeIdx_ >= kFxLen) writeIdx_ = 0;
		l = r = dry;
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
				voice_[i].right = nextRight_;
				nextRight_ = !nextRight_;
				// Scatter: forward by half a grain, plus up to the whole buffer
				// as Main opens. Bottom is an orderly time-stretch, top a cloud.
				uint32_t jump = static_cast<uint32_t>(dur_ >> 1);
				if (scatter_ > 0)
					jump += xorshift32(rng_) % (1u + ((static_cast<uint32_t>(scatter_)
					                                   * kFxLen) >> 16));
				nextStart_ = (nextStart_ + jump) % kFxLen;
				trig_ = kTrigTicks;
				break;
			}
		}

		int32_t accL = 0, accR = 0;
		for (int i = 0; i < kVoices; i++)
		{
			Grain &g = voice_[i];
			if (!g.active) continue;

			int32_t e = (g.pos < ramp_)         ? (g.pos * rampInv_) >> 16
			          : (g.pos > g.dur - ramp_) ? ((g.dur - g.pos) * rampInv_) >> 16
			                                    : 32767;
			if (e < 0) e = 0;
			int32_t v = mul_q15(ReadAbs(g.readIdx, g.readFrac), e);
			accL += mul_q15(v, g.right ? kFar : kNear);
			accR += mul_q15(v, g.right ? kNear : kFar);

			g.readFrac += rateQ16_;
			g.readIdx += static_cast<uint32_t>(g.readFrac >> 16);
			g.readFrac &= 0xFFFF;
			if (++g.pos >= g.dur) g.active = false;
		}
		// Entirely wet while frozen: blending the live input back in is what
		// made the first version of this sound like nothing was happening.
		//
		// No output gain at all. The pan weights and the 50%-overlap envelope
		// already sum to unity: measured in lpsim across every density, grain
		// size and pitch, a full-scale frozen source peaks at 1996 of 2047.
		// A 1.25x boost — which looks harmless — clips at 2495.
		l = accL;
		r = accR;
	}

	int32_t sl = clamp12(l);
	int32_t sr = clamp12(r);
	out.audio1 = static_cast<int16_t>(sl);
	out.audio2 = static_cast<int16_t>(sr);

	int32_t mag = (sl < 0) ? -sl : sl;
	env_ = (mag > env_) ? slew_exact(env_, mag, 5) : slew_exact(env_, mag, 11);
	out.cv2 = q16_to_cv5v(clamp_i32(env_ * 32, 0, 65535));
}

} // namespace lp
