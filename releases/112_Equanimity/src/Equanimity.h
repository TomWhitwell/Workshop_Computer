#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Bell.h"
#include "Control.h"
#include "Delays.h"
#include "DroneVoice.h"
#include "Dsp.h"
#include "Tables.h"

namespace eq
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
	bool pulseRise[2];
	bool pulseConnected[2]; // jack detection (normalisation probe)
};

struct Outputs
{
	int32_t audio[2];
	int32_t cv[2]; // CVOutPrecise units
	bool pulse[2];
};

// The whole instrument, split across the two cores:
//
//   Audio()        core 0, inside the 48kHz audio interrupt. Melody, drone,
//                  delays, mix, jacks. Must finish in ~20us including ComputerCard's
//                  own work, every sample.
//   ControlTick()  core 1, in a loop. Does its work once a millisecond.
//
// Handing results from core 1 to core 0: there are two EngineParams
// blocks. Core 0 plays from params_[current_]; core 1 fills the other one
// and then flips current_. Core 0 acknowledges each flip at the start of
// its next sample (ack_), and core 1 won't touch a block again until that
// acknowledgement arrives -- so core 0 never reads a block while core 1 is
// halfway through writing it. (The same scheme as Impermanence.)
class Equanimity
{
public:
	void Seed(uint32_t seed) { control_.Seed(seed); }
	void SetEntropy(uint32_t (*entropy)()) { control_.SetEntropy(entropy); }

	// ---- core 0 --------------------------------------------------------
	void Audio(const Inputs &in, Outputs &out)
	{
		int32_t c = current_;
		ack_ = c;
		const EngineParams &p = params_[c];

		Report(in);
		if (p.strikeEpoch != strikeEpoch_)
		{
			strikeEpoch_ = p.strikeEpoch;
			bells_.Accept(p);
		}

		// The melody, at its volume, with the vibraphone's tremolo when that
		// tone is on.
		int32_t melody = (bells_.Process() * p.melodyGain) >> 12;
		if (p.tremoloDepth > 0)
		{
			// 5Hz: dips by up to tremoloDepth, never boosts.
			int32_t dip = (p.tremoloDepth * (32768 + Sine(tremoloPhase_))) >> 16;
			tremoloPhase_ += kTremoloInc;
			melody = (melody * (4096 - dip)) >> 12;
		}
		// Each voice's loudness, for core 1 (one voice per sample, in turn).
		int32_t v = (int32_t)(status_.samples & (kVoices - 1));
		status_.voiceAmp[v] = bells_.Amp(v);

		int32_t drone = (drone_.Process(p) * p.droneGain) >> 12;

		// Both delays are fed the melody, a little of the drone (its
		// harmonics, not its bass: drone minus a lowpassed copy of itself),
		// and Audio In 1 and 2 (which are only heard through the delays),
		// with any DC taken out and gently limited. Unpatched inputs read
		// zero, thanks to ComputerCard's jack detection.
		int32_t wetL, wetR;
		int32_t droneHigh = drone - OnePole(droneSendLp_, drone, kDroneSendHighpassQ15);
		int32_t send = melody + ((droneHigh * kDroneSendQ12) >> 12) + (in.audio[0] + in.audio[1]) * 16;
		delays_.Process(Saturate(dcBlocker_.Process(send)), p, wetL, wetR);

		// Wet/dry crossfades the melody against the echoes (stereo). The
		// drone stays out of it -- set by its own volume, in the centre -- so
		// it doesn't vanish with Main fully wet.
		int32_t dry = melody * p.dryGain;
		int32_t mixL = ((dry + wetL * p.wetGain) >> 12) + drone;
		int32_t mixR = ((dry + wetR * p.wetGain) >> 12) + drone;
		if (!p.running)
			mixL = mixR = 0;
		// +/-32768 inside the card, +/-2048 at the jacks: x 1.5 / 16.
		out.audio[0] = SoftClip((mixL * (kOutputGainQ12 >> 4)) >> 12);
		out.audio[1] = SoftClip((mixR * (kOutputGainQ12 >> 4)) >> 12);
		out.cv[0] = p.cv[0];
		out.cv[1] = p.cv[1];
		out.pulse[0] = p.trigger[0];
		out.pulse[1] = p.trigger[1];
	}

	// ---- core 1 --------------------------------------------------------
	// Call as often as you like; it does nothing until a millisecond has
	// passed and core 0 has picked up the last update. Returns true when it
	// has updated the LEDs.
	bool ControlTick(int32_t led[6])
	{
		uint32_t now = status_.samples;
		if (now - lastTick_ < (uint32_t)kTickSamples)
			return false;
		int32_t c = current_;
		if (ack_ != c)
			return false;
		lastTick_ = now;

		Status s;
		s.samples = now;
		for (int k = 0; k < 3; k++)
			s.knob[k] = status_.knob[k];
		s.sw = status_.sw;
		for (int i = 0; i < 2; i++)
		{
			s.cv[i] = status_.cv[i];
			s.pulseEdges[i] = status_.pulseEdges[i];
			s.pulseConnected[i] = status_.pulseConnected[i];
		}
		for (int v = 0; v < kVoices; v++)
			s.voiceAmp[v] = status_.voiceAmp[v];

		EngineParams &next = params_[1 - c];
		next = params_[c];
		control_.Tick(s, next, led);
		__sync_synchronize(); // the new block is complete before we flip
		current_ = 1 - c;
		return true;
	}

	const Control &GetControl() const { return control_; }
	Control &GetControl() { return control_; }
	int32_t VoiceAmp(int v) const { return bells_.Amp(v); } // for tests
	int32_t DroneAmp() const { return drone_.Amp(); }		  // for tests
	uint32_t DroneInc(int layer = 0) const { return drone_.Inc(layer); } // for tests
	const EngineParams &Params() const { return params_[current_]; } // for tests

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
		for (int i = 0; i < 2; i++)
		{
			if (in.pulseRise[i])
				status_.pulseEdges[i] = status_.pulseEdges[i] + 1;
			status_.pulseConnected[i] = in.pulseConnected[i];
		}
	}

	// Shared between the cores (see the class comment).
	struct SharedStatus
	{
		volatile uint32_t samples = 0;
		volatile int32_t knob[3] = {0, 0, 0};
		volatile int32_t sw = kSwitchMiddle;
		volatile int32_t cv[2] = {0, 0};
		volatile uint32_t pulseEdges[2] = {0, 0};
		volatile bool pulseConnected[2] = {false, false};
		volatile int32_t voiceAmp[kVoices] = {};
	};

	// Layout matters on the Cortex-M0+: fields near the start of the object
	// are one short load away, so the small, per-sample state comes first
	// and the big buffers come last.
	volatile int32_t current_ = 0;
	volatile int32_t ack_ = 0;
	uint32_t strikeEpoch_ = 0;
	DcBlocker dcBlocker_;
	int32_t droneSendLp_ = 0;
	uint32_t tremoloPhase_ = 0;
	uint32_t lastTick_ = 0; // core 1 only
	SharedStatus status_;
	BellBank bells_;
	DroneVoice drone_;
	EngineParams params_[2];
	Control control_;
	Delays delays_; // the two delay lines: ~130KB, last
};

} // namespace eq
