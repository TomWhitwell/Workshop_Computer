#pragma once
#include <cstdint>
#include "StereoBuffer.h"
#include "Params.h"
#include "Control.h"
#include "SliceEngine.h"
#include "Shimmer.h"
#include "EnvelopeFollower.h"

namespace imp
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
	bool pulse1Rise;
	bool pulse2Rise;
	bool audio2Connected;
	bool pulse1Connected; // jack detection (normalisation probe)
	bool pulse2Connected;
};

struct Outputs
{
	int32_t audio[2];
	int32_t cv[2];
	bool pulse[2];
	int32_t led[6]; // brightness 0..4095 (filled by ControlTick)
};

// The whole instrument, split across the two cores:
//
//   Audio()        core 0, inside the 48kHz audio interrupt. Recording,
//                  slice engine, reverb, jacks. Must finish in ~20us
//                  including ComputerCard's own work, every sample.
//   ControlTick()  core 1, in a loop. Knobs, catch-up, re-cuts, slice maps,
//                  all divisions, LEDs. No deadline.
//
// Handing results from core 1 to core 0: there are two EngineParams
// blocks. Core 0 plays from params_[current_]; core 1 fills the other one
// and then flips current_. Core 0 acknowledges each flip at the start of
// its next sample (ack_), and core 1 won't touch a block again until
// that acknowledgement arrives -- so core 0 never reads a block while core
// 1 is halfway through writing it. The two slice maps follow the same rule
// through EngineParams::map.
class Impermanence
{
public:
	Impermanence() { control_.InitMaps(maps_); }

	void Seed(uint32_t seed) { control_.Seed(seed); }

	// ---- core 0 --------------------------------------------------------
	void Audio(const Inputs &in, Outputs &out)
	{
		int32_t c = current_;
		ack_ = c;
		const EngineParams &p = params_[c];

		// Report the panel to core 1. It only needs knobs and CVs at control
		// rate, so they take turns, to spread the work over samples.
		uint32_t n = status_.samples + 1;
		status_.samples = n;
		if (n & 1)
		{
			status_.knob[0] = in.knob[0];
			status_.knob[1] = in.knob[1];
			status_.knob[2] = in.knob[2];
		}
		else
		{
			status_.cv[0] = in.cv[0];
			status_.cv[1] = in.cv[1];
		}
		status_.sw = in.sw;

		// Commands from core 1 (Main fully anticlockwise, then back up).
		if (p.clearSeq != clearSeq_)
		{
			clearSeq_ = p.clearSeq;
			Clear();
		}
		if (p.recordSeq != recordSeq_)
		{
			recordSeq_ = p.recordSeq;
			StartRecording();
		}

		UpdateRecording(in);
		engine_.ClockInput(in.pulse2Rise, in.pulse2Connected);

		// Engine -> loop fade -> reverb -> wet/dry mix -> soft clip.
		int32_t dryL, dryR, wetL, wetR;
		uint32_t cycleEnds = engine_.Process(p, dryL, dryR);
		if (loopGain_ != 4096 || loopTarget_ != 4096) // only around a clear
		{
			if (loopGain_ != loopTarget_)
				loopGain_ += loopGain_ < loopTarget_ ? kLoopFadeStep : -kLoopFadeStep;
			dryL = (dryL * loopGain_) >> 12;
			dryR = (dryR * loopGain_) >> 12;
		}
		shimmer_.SetDecay(p.decay);
		shimmer_.Process(dryL, dryR, wetL, wetR);
		out.audio[0] = SoftClip(dryL + (((wetL - dryL) * p.mix) >> 12));
		out.audio[1] = SoftClip(dryR + (((wetR - dryR) * p.mix) >> 12));

		int32_t env = envelope_.Process((dryL + dryR) >> 1);
		out.cv[0] = p.wander;
		out.cv[1] = env;

		// End-of-cycle pulses (10ms): Pulse Out 1 from L1, Pulse Out 2 from L3
		// (bits 0 and 2 of cycleEnds).
		//
		// Why not L2 and L3, which the card used before: their loops are 9:8
		// apart, so the two jacks drift slowly past each other and spend long
		// stretches nearly-but-not-quite together -- flams rather than a
		// rhythm -- and in Rhythm mode at low chaos they have the same slot
		// count and fire together outright. L1 against L3 is 3:2 in Texture
		// and 4:3, 8:5 or 16:11 in Rhythm: the pulses that do land together
		// land exactly together, on a musical cycle.
		static constexpr uint32_t kPulseLayer[2] = {0, 2};
		for (int i = 0; i < 2; i++)
		{
			if (cycleEnds & (1u << kPulseLayer[i]))
			{
				pulseLeft_[i] = kPulseSamples;
				status_.cycles[i] = status_.cycles[i] + 1;
			}
			out.pulse[i] = pulseLeft_[i] > 0;
			if (pulseLeft_[i] > 0)
				pulseLeft_[i]--;
		}

		status_.clockPeriod = engine_.ClockPeriod();
		status_.clockActive = engine_.ClockActive();
		status_.envelope = env;
		status_.recording = recording_;
	}

	// ---- core 1 --------------------------------------------------------
	// Returns false (and does nothing) if core 0 hasn't yet picked up the
	// last update -- just call again.
	bool ControlTick(int32_t led[6])
	{
		int32_t c = current_;
		if (ack_ != c)
			return false;
		Status s;
		s.samples = status_.samples;
		for (int k = 0; k < 3; k++)
			s.knob[k] = status_.knob[k];
		s.sw = status_.sw;
		s.cv[0] = status_.cv[0];
		s.cv[1] = status_.cv[1];
		s.clockPeriod = status_.clockPeriod;
		s.clockActive = status_.clockActive;
		s.takeLength = status_.takeLength;
		s.recording = status_.recording;
		s.cycles[0] = status_.cycles[0];
		s.cycles[1] = status_.cycles[1];
		s.envelope = status_.envelope;

		control_.Tick(s, params_[c], params_[1 - c], maps_, led);
		__sync_synchronize(); // the new block is complete before we flip
		current_ = 1 - c;
		return true;
	}

	// Slice maps made since power-on (for tests).
	uint32_t Reseeds() const { return control_.Reseeds(); }

private:
	static constexpr int32_t kMinTakeFrames = 2400; // shortest take: 50ms
	static constexpr int32_t kPulseSamples = 480;	// 10ms pulse outs
	static constexpr uint32_t kConfirmDelay = 960;	// 20ms: see UpdateRecording
	static constexpr int32_t kDelaySize = 1024;		// > kConfirmDelay, power of two
	static constexpr int32_t kMaxRecEdges = 4;		// power of two
	static constexpr int32_t kLoopFadeStep = 16;	// 4096/16 = 256 samples, ~5ms
	static constexpr int32_t kSealFrames = 192;		// 4ms fades at a take's ends

	// Output limiter: unchanged up to 3/4 of full scale, then squashed 4:1,
	// then a hard stop at the DAC's limit.
	static int32_t SoftClip(int32_t x)
	{
		int32_t a = x < 0 ? -x : x;
		if (a > 1536)
		{
			a = 1536 + ((a - 1536) >> 2);
			if (a > 2047)
				a = 2047;
		}
		return x < 0 ? -a : a;
	}

	// Recording, every sample.
	//
	// Pulse In 1 edges wait kConfirmDelay (20ms) before they count, and are
	// dropped if the jack is reported empty in the meantime: pulling a cable
	// out makes ComputerCard's jack detection feed ~11ms of random edges into
	// the input, which used to toggle recording and overwrite the loop (see
	// SliceEngine::ClockInput). To keep takes starting exactly on the
	// trigger, the audio being recorded is delayed by the same 20ms.
	// Out of line so its state is a short reach from `this` (see
	// SliceEngine::ProcessLayer for why that matters on this chip).
	__attribute__((noinline)) void UpdateRecording(const Inputs &in)
	{
		now_++;
		int32_t right = in.audio2Connected ? in.audio[1] : in.audio[0];
		delay_[delayPos_] = (int32_t)(((uint32_t)in.audio[0] & 0xFFFF) | ((uint32_t)right << 16)); // both channels, one store
		delayPos_ = (delayPos_ + 1) & (kDelaySize - 1);

		if (!in.pulse1Connected)
			recEdgeCount_ = 0;
		else if (in.pulse1Rise && recEdgeCount_ < kMaxRecEdges)
			recEdges_[(recEdgeHead_ + recEdgeCount_++) & (kMaxRecEdges - 1)] = now_;
		while (recEdgeCount_ > 0 && now_ - recEdges_[recEdgeHead_] >= kConfirmDelay)
		{
			recEdgeHead_ = (recEdgeHead_ + 1) & (kMaxRecEdges - 1);
			recEdgeCount_--;
			if (recording_)
				StopRecording();
			else
				StartRecording();
		}

		if (recording_)
		{
			// The playheads keep reading while this overwrites the buffer, so
			// the old loop dissolves into the new take as it's recorded.
			// With nothing in Audio In 2, In 1 is recorded on both sides.
			int32_t d = delay_[(delayPos_ - 1 - (int32_t)kConfirmDelay) & (kDelaySize - 1)];
			buffer_.Write(writePos_, (int16_t)(d & 0xFFFF), d >> 16);
			if (++writePos_ >= StereoBuffer::kFrames)
				StopRecording();
		}

		if (clearing_ || sealLeft_ > 0)
			ClearAndSeal();
	}

	// Occasional work, kept out of the everyday path: clearing the buffer
	// after Main goes fully anticlockwise, and fading a new take's ends.
	__attribute__((noinline)) void ClearAndSeal()
	{
		// Clearing: a block per sample, once the loop has faded out (or
		// straight away, just ahead of the write position, if recording has
		// already started again).
		if (clearing_ && (loopGain_ == 0 || recording_))
		{
			// Blocks are 16 frames. Never clear the block being written
			// into -- start from the next one.
			int32_t ahead = ((writePos_ >> 4) + 1) * StereoBuffer::kClearBlockFrames;
			if (recording_ && clearFrame_ < ahead)
				clearFrame_ = ahead;
			if (clearFrame_ >= StereoBuffer::kFrames)
				clearing_ = false;
			else
			{
				buffer_.ClearBlock(clearFrame_);
				clearFrame_ += StereoBuffer::kClearBlockFrames;
			}
		}

		// Seam: after a take, fade its first and last 4ms (one frame of each
		// per sample) so the loop point doesn't click.
		if (sealLeft_ > 0)
		{
			sealLeft_--;
			int32_t i = kSealFrames - 1 - sealLeft_;
			int32_t gain = (i * 21845) >> 10; // i * 4096 / 192
			buffer_.ScaleFrame(i, gain);
			buffer_.ScaleFrame(sealLength_ - 1 - i, gain);
		}
	}

	void StartRecording()
	{
		recording_ = true;
		writePos_ = 0;
		sealLeft_ = 0;
		loopTarget_ = 4096; // bring the loop back after a clear
	}

	void StopRecording()
	{
		recording_ = false;
		int32_t length = writePos_ < kMinTakeFrames ? kMinTakeFrames : writePos_;
		sealLength_ = length;
		sealLeft_ = kSealFrames;
		// Core 1 picks this up and sends back parameters for the new length.
		status_.takeLength = length;
	}

	// Main fully anticlockwise: fade the loop out (~5ms), then clear the
	// whole buffer, and forget any held clock tempo. A take in progress is
	// abandoned; pending record triggers are dropped.
	void Clear()
	{
		recording_ = false;
		recEdgeCount_ = 0;
		sealLeft_ = 0;
		loopTarget_ = 0;
		clearing_ = true;
		clearFrame_ = 0;
		engine_.ResetClock();
	}

	// Shared between the cores (see the class comment).
	struct SharedStatus
	{
		volatile uint32_t samples = 0;
		volatile int32_t knob[3] = {0, 0, 0};
		volatile int32_t sw = kSwitchMiddle;
		volatile int32_t cv[2] = {0, 0};
		volatile int32_t clockPeriod = 12000;
		volatile bool clockActive = false;
		volatile int32_t takeLength = StereoBuffer::kFrames;
		volatile bool recording = false;
		volatile uint32_t cycles[2] = {0, 0};
		volatile int32_t envelope = 0;
	};

	// Layout matters on the Cortex-M0+: fields near the start of the object
	// are one short load away, so the small, per-sample state comes first
	// and the two big buffers (reverb, then the 144KB audio) come last.
	volatile int32_t current_ = 0;
	volatile int32_t ack_ = 0;
	int32_t writePos_ = 0; // core 0 only, down to delay_
	bool recording_ = false;
	int32_t pulseLeft_[2] = {0, 0};
	int32_t loopGain_ = 4096, loopTarget_ = 4096; // Q12, fades the loop for a clear
	uint32_t clearSeq_ = 0, recordSeq_ = 0;
	bool clearing_ = false;
	int32_t clearFrame_ = 0;
	int32_t sealLeft_ = 0, sealLength_ = 0;
	uint32_t now_ = 0;
	uint32_t recEdges_[kMaxRecEdges] = {};
	int32_t recEdgeHead_ = 0, recEdgeCount_ = 0;
	int32_t delayPos_ = 0;
	EnvelopeFollower envelope_;
	SharedStatus status_;
	EngineParams params_[2];
	SliceMap maps_[2];
	SliceEngine engine_{buffer_};
	Control control_;
	Shimmer shimmer_;
	int32_t delay_[kDelaySize] = {}; // the last ~21ms of input (L low, R high), for recording
	StereoBuffer buffer_;
};

} // namespace imp
