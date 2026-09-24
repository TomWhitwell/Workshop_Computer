// Mode 4 — Tape Scrubber (ColourGrid KAOSS pad).
//
// HOLD Down and Audio In 1 records for as long as you hold it (up to
// Tape::kMaxLen). A TAP of Down instead cycles what Green does with the take:
//
//   SCRUB   Green places the read head anywhere in the take, interpolated,
//           with a little inertia so it scrubs like tape. Silent when your
//           hand is still — a stationary head reads one sample.
//   SLICE   The take is cut into 16, Green picks one, and that slice repeats
//           at normal speed. Beat repeat: it plays whether or not you move.
//   SWEEP   The head free-runs from the start and Green sets the loop LENGTH,
//           from the whole take down to a sixty-fourth of it. Dragging down a
//           gradient shrinks a phrase into a stutter into a pitched buzz.
//
// Green means place, then which slice, then how long — three different jobs
// for one sensor, which is the test a behaviour cycle should pass.
//
// Red is the filter cutoff, Blue its resonance, and Main walks the filter
// LP -> BP -> HP. Before anything is recorded the live input passes through.
//
// The take is shared with Mode 7, which plays it as a moving loop.

#pragma once
#include "engine.h"
#include "svf.h"
#include "tape.h"

namespace lp {

class TapeScrubMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(int ticks) override;

private:
	enum class Act : uint8_t { Scrub, Slice, Sweep, kCount };

	static constexpr int kSlices     = 16;
	static constexpr int kSliceDwell = 45;   // 30ms, as Mode 6's note picker

	int32_t  headQ8_ = 0;      // Scrub: read position, Q8 samples, smoothed
	Tape::Head head_;          // Slice and Sweep: a free-running head
	uint32_t sliceLen_ = 0;
	int      slice_ = 0, pending_ = -1, dwell_ = 0;
	uint32_t loopEnd_ = 0;

	Act      act_ = Act::Scrub;
	bool     actChanged_ = false;
	bool     armed_ = false;   // a press is running but has not become a record

	Svf      svf_;
	SvfBlend blend_;
};

} // namespace lp
