#pragma once
#include <cstdint>
#include "Config.h"
#include "Control.h"
#include "Dsp.h"
#include "Echo.h"
#include "Params.h"
#include "Plate.h"
#include "Tables.h"

namespace ab
{

// Everything the card reads from the panel in one sample. Kept separate
// from ComputerCard so the whole instrument can also run on a computer
// (see test/) for testing and offline renders.
struct Inputs
{
	int32_t knob[3]; // Main, X, Y: 0..4095
	int32_t sw;		 // kSwitchDown / Middle / Up
	int32_t audio[2];
	int32_t cv[2];
	bool pulseRise;		 // Pulse In 1 rising edge
	bool pulseConnected; // Pulse In 1 jack detection
};

struct Outputs
{
	int32_t audio[2];
	int32_t cvMv[2];
	bool pulse[2];
};

// The whole instrument, split across the two cores:
//
//   Audio()   core 0, inside the 48kHz audio interrupt: one sample.
//   Poll()    core 1, in a loop: renders the next block whenever there's
//             room for it in the queue.
//
// The queue: four blocks in a ring. Core 1 writes block `written_`, then
// bumps written_; core 0 plays block `read_`, then bumps read_. Core 1 only
// writes while fewer than four blocks are waiting, so it never touches the
// block core 0 is playing; core 0 only plays blocks core 1 has finished.
// Each counter is written by one core only, so no locks are needed.
//
// If core 1 ever falls behind, core 0 plays a block of silence rather than
// a half-written one, and counts it (Underruns()).
class Abeyance
{
public:
	void Seed(uint32_t seed) { control_.Seed(seed); }

	// ---- core 0 --------------------------------------------------------
	void Audio(const Inputs &in, Outputs &out)
	{
		Report(in);
		if (pos_ == 0)
		{
			if (read_ != written_)
			{
				__sync_synchronize(); // read the block only after seeing it's ready
				cur_ = &blocks_[read_ & (kFifoBlocks - 1)];
			}
			else
			{
				cur_ = &silent_;
				underruns_++;
			}
			plate_.Shape(cur_->plateDecayQ15, cur_->plateDampQ15, cur_->plateOutQ10);
		}
		const Block &b = *cur_;
		const Frame &f = b.frame[pos_];

		if (b.flush)
		{
			Flush(b, out);
			return;
		}

		// The piano's echo joins the voices...
		int32_t echoL, echoR;
		echo_.Process(f.pianoL, f.pianoR, echoL, echoR);
		int32_t synthL = f.dryL + ((echoL * kEchoReturnQ12) >> 12);
		int32_t synthR = f.dryR + ((echoR * kEchoReturnQ12) >> 12);

		// ...and all of it, with Audio In 1 + 2 (halved and summed, so
		// they sit in the centre), feeds the reverb. Unpatched inputs read
		// zero, thanks to ComputerCard's jack detection. The inputs are only
		// heard through the reverb, so their level follows the reverb mix.
		int32_t input = (in.audio[0] + in.audio[1]) * 8; // (a + b) / 2, 12-bit to 16-bit
		int32_t wetL, wetR;
		plate_.Process((((synthL + synthR) >> 1) + input) >> 2, b.lfoQ8, wetL, wetR);

		// Reverb mix (an equal-power crossfade), then master volume.
		int32_t mixL = (synthL * b.dryGain + wetL * b.wetGain) >> 12;
		int32_t mixR = (synthR * b.dryGain + wetR * b.wetGain) >> 12;
		// The tape's hiss and the record's crackle (from core 1): on the
		// medium, not in the room, so after the reverb -- and the volume
		// turns them down too.
		mixL += f.noiseL;
		mixR += f.noiseR;

		mixL = (mixL * b.volume) >> 12;
		mixR = (mixR * b.volume) >> 12;

		// 16-bit scale inside the card, 12-bit at the jacks (/ 16), with x 4
		// of output gain: measured over long runs, the mix then peaks around
		// -6dBFS at the start-up volume, and full volume reaches into the
		// soft clip.
		out.audio[0] = SoftClip(mixL >> kOutputShift);
		out.audio[1] = SoftClip(mixR >> kOutputShift);
		Finish(b, out);
	}

	// Between pieces: the output is silent, and the echo and reverb are
	// emptied a slice at a time instead of run. Kept out of line, so it
	// doesn't crowd Audio()'s registers on every other sample.
	__attribute__((noinline)) void Flush(const Block &b, Outputs &out)
	{
		echo_.ClearSome();
		plate_.ClearSome();
		out.audio[0] = out.audio[1] = 0;
		Finish(b, out);
	}

	// The rest of every sample: the CV and pulse outs, and the queue.
	__attribute__((always_inline)) void Finish(const Block &b, Outputs &out)
	{
		out.cvMv[0] = b.cvMv[0];
		out.cvMv[1] = b.cvMv[1];
		out.pulse[0] = b.pulse[0];
		out.pulse[1] = b.pulse[1];

		if (++pos_ == kBlock)
		{
			pos_ = 0;
			if (cur_ != &silent_)
			{
				__sync_synchronize(); // finish reading before handing the block back
				read_ = read_ + 1;
			}
		}
	}

	// ---- core 1 --------------------------------------------------------
	// Renders one block if there's room. Returns true when it did (and has
	// updated led[]).
	bool Poll(int32_t led[6])
	{
		uint32_t w = written_;
		if (w - read_ >= (uint32_t)kFifoBlocks)
			return false;
		Status s;
		s.samples = status_.samples;
		for (int k = 0; k < 3; k++)
			s.knob[k] = status_.knob[k];
		s.sw = status_.sw;
		s.cv[0] = status_.cv[0];
		s.cv[1] = status_.cv[1];
		s.pulseEdges = status_.pulseEdges;
		s.pulseConnected = status_.pulseConnected;
		s.noise = status_.noise;
		control_.Tick(s, blocks_[w & (kFifoBlocks - 1)], led);
		__sync_synchronize(); // the block is complete before it's handed over
		written_ = w + 1;
		return true;
	}

	const Control &GetControl() const { return control_; } // for tests
	void BenchFill() { control_.BenchFill(); }			   // for the bench and tests
	uint32_t Underruns() const { return underruns_; }	   // for tests

private:
	// Tell core 1 what the panel is doing.
	void Report(const Inputs &in)
	{
		status_.samples = status_.samples + 1;
		for (int k = 0; k < 3; k++)
			status_.knob[k] = in.knob[k];
		status_.sw = in.sw;
		status_.cv[0] = in.cv[0];
		status_.cv[1] = in.cv[1];
		if (in.pulseRise)
			status_.pulseEdges = status_.pulseEdges + 1;
		status_.pulseConnected = in.pulseConnected;
		// The jitter in the low bits of the readings: entropy for new pieces.
		status_.noise = (status_.noise << 3) ^ (status_.noise >> 29) ^ (uint32_t)in.knob[0] ^
						((uint32_t)in.knob[1] << 4) ^ ((uint32_t)in.knob[2] << 8) ^ ((uint32_t)in.cv[0] << 12) ^
						((uint32_t)in.cv[1] << 16) ^ ((uint32_t)in.audio[0] << 20) ^ ((uint32_t)in.audio[1] << 24);
	}

	// Samples of flush that empty both the echo and the reverb.
	static constexpr int32_t kFlushSamples = Plate::kClearCalls > Echo::kClearCalls ? Plate::kClearCalls : Echo::kClearCalls;
	static_assert(kFlushBlocks * kBlock >= kFlushSamples + kBlock, "the flush is too short to empty the echo and reverb");

	// / 16 to the jacks' 12-bit, x 4 output gain.
	static constexpr int32_t kOutputShift = 2;

	// How loud the piano's echo comes back, Q12.
	static constexpr int32_t kEchoReturnQ12 = 2048; // 0.5

	struct SharedStatus
	{
		volatile uint32_t samples = 0;
		volatile int32_t knob[3] = {0, 0, 0};
		volatile int32_t sw = kSwitchMiddle;
		volatile int32_t cv[2] = {0, 0};
		volatile uint32_t pulseEdges = 0;
		volatile bool pulseConnected = false;
		volatile uint32_t noise = 0;
	};

	// Layout matters on the Cortex-M0+: fields near the start of the object
	// are one short load away, so the small per-sample state comes first and
	// the big buffers last.
	const Block *cur_ = &silent_;
	int32_t pos_ = 0;
	volatile uint32_t written_ = 0; // core 1 writes
	volatile uint32_t read_ = 0;	// core 0 writes
	uint32_t underruns_ = 0;
	SharedStatus status_;
	Echo echo_;	  // 74KB
	Plate plate_; // 82KB
	Control control_;
	Block silent_;
	Block blocks_[kFifoBlocks];
};

} // namespace ab
