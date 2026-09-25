// Mode 12 — Freeze.
//
// Audio In 1 runs continuously into the shared buffer, so the last 426ms is
// always there. HOLD Down and the writing stops: what was in the buffer at that
// instant becomes the source for three granular voices, and you get a sustained
// pad out of a moment that has already gone. Release and it thaws.
//
//   Red   = grain size, 2ms to 400ms
//   Green = pitch, an octave down through unity at noon to an octave up
//   Blue  = density, from stuttering gaps to a solid overlapping cloud
//
// Main is dry/wet. A TAP of Down rotates the colour roles instead of freezing —
// the same tap-versus-hold split as the modes that record, and for the same
// reason: freezing on the press would make every tap a 171ms glitch.
//
// Pulse Out 1 fires as each grain starts, so the cloud clocks the rest of the
// rack. CV Out 2 is an envelope follower on the output.
//
// Grains march forward through the frozen buffer rather than sitting still, so
// a long hold reads as a time-stretch of the captured moment instead of one
// looped fragment.

#pragma once
#include "engine.h"
#include "fx.h"

namespace lp {

class FreezeMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(int ticks) override;

private:
	static constexpr int      kVoices = 3;
	static constexpr int32_t  kMinGrain = 96;      // 2ms
	static constexpr int32_t  kMaxGrain = 19200;   // 400ms

	struct Grain
	{
		bool     active = false;
		uint32_t readIdx = 0;    // absolute index into gFx
		int32_t  readFrac = 0;   // Q16 within the sample
		int32_t  pos = 0;        // output samples elapsed
		int32_t  dur = 1;        // output samples total
	};

	Grain    voice_[kVoices];
	Line     line_;
	uint32_t writeIdx_ = 0;      // our own write head: frozen playback needs
	                             // absolute indices, not taps into the past
	uint32_t nextStart_ = 0;
	int32_t  dur_ = kMinGrain;
	int32_t  rateQ16_ = 65536;
	int32_t  spawnEvery_ = kMinGrain;
	int32_t  spawnCount_ = 0;
	int32_t  rampInv_ = 0;       // Q16 reciprocal of the envelope ramp length
	int32_t  ramp_ = 1;
	int32_t  mix_ = 0;
	int32_t  env_ = 0;
	uint32_t clearPos_ = 0;
	uint32_t rng_ = 0x9E3779B9;
	int      trig_ = 0;

	bool     frozen_ = false;
	bool     armed_ = false;
	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
