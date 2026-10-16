#pragma once
#include <cstdint>
#include "Config.h"
#include "Control.h"
#include "Dsp.h"
#include "IcarusLoop.h"
#include "Params.h"
#include "Saws.h"
#include "Tables.h"
#include "Water.h"

namespace constancy
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
	int32_t cv1;	 // CV Out 1, CVOutPrecise units
	int32_t cv2Note; // CV Out 2, a MIDI note for the calibrated 1V/octave output
	bool pulse[2];
};

// The whole instrument, split across the two cores:
//
//   Audio()        core 0, inside the 48kHz audio interrupt: saws, filters,
//                  the Icarus loop, Water, mix, jacks. Must finish in ~20us including
//                  ComputerCard's own work, every sample.
//   ControlTick()  core 1, in a loop. Does its work once a millisecond.
//
// Handing results from core 1 to core 0: there are two EngineParams
// blocks. Core 0 plays from params_[current_]; core 1 fills the other one
// and then flips current_. Core 0 acknowledges each flip at the start of
// its next sample (ack_), and core 1 won't touch a block again until that
// acknowledgement arrives -- so core 0 never reads a block while core 1 is
// halfway through writing it. (The same scheme as Impermanence and
// Equanimity.)
class Constancy
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
		if (c != playing_)
		{
			// A fresh block from core 1: slide every gain towards it over
			// the next millisecond.
			playing_ = c;
			for (int v = 0; v < kVoices; v++)
			{
				ramps_.Retarget(kGainL + v, p.voice[v].gainL);
				ramps_.Retarget(kGainR + v, p.voice[v].gainR);
			}
			ramps_.Retarget(kDroneGain, p.droneGain);
			ramps_.Retarget(kFeedback, p.feedbackQ12);
			ramps_.Retarget(kDestruct, p.destructQ15);
			ramps_.Retarget(kWaterMix, p.waterMixQ12);
		}
		// The ramps move on even samples, when the loop does its lighter
		// (left) half.
		even_ = !even_;
		if (even_)
			ramps_.Step();
		Report(in);

		// The four melodic voices: the lead on its own, the loops together.
		int32_t leadL = 0, leadR = 0, loopsL = 0, loopsR = 0;
		for (int v = 0; v < kVoices; v++)
		{
			int32_t gl = ramps_.value[kGainL + v], gr = ramps_.value[kGainR + v];
			if (gl == 0 && gr == 0)
				continue; // silent: skip its saws
			int32_t l = 0, r = 0;
			if (v == kLead)
				leadSaws_.Process(p.voice[v].saw, l, r);
			else
				loopSaws_[v - 1].Process(p.voice[v].saw, l, r);
			l = (l * gl) >> 14;
			r = (r * gr) >> 14;
			if (v == kLead)
			{
				leadL = l;
				leadR = r;
			}
			else
			{
				loopsL += l;
				loopsR += r;
			}
		}

		// The CS-80's lowpass, after the voices: two one-pole stages (12dB
		// per octave), one for the loops and one for the lead, whose cutoff
		// swells after each attack.
		loopsL = OnePole(loopsLp_[1], OnePole(loopsLp_[0], Clamp16(loopsL), p.loopsCoef), p.loopsCoef);
		loopsR = OnePole(loopsLp_[3], OnePole(loopsLp_[2], Clamp16(loopsR), p.loopsCoef), p.loopsCoef);
		leadL = OnePole(leadLp_[1], OnePole(leadLp_[0], Clamp16(leadL), p.leadCoef), p.leadCoef);
		leadR = OnePole(leadLp_[3], OnePole(leadLp_[2], Clamp16(leadR), p.leadCoef), p.leadCoef);
		int32_t melodyL = loopsL + leadL, melodyR = loopsR + leadR;

		// The drone: root saws left and right, the pure fifth in the
		// middle, through its own gentle lowpass so it sits behind.
		int32_t droneL = 0, droneR = 0;
		int32_t dg = ramps_.value[kDroneGain];
		if (dg > 0)
		{
			droneSaws_.Process(p.drone, droneL, droneR);
			droneL = OnePole(droneLp_[0], (droneL * dg) >> 14, p.droneCoef);
			droneR = OnePole(droneLp_[1], (droneR * dg) >> 14, p.droneCoef);
		}

		// Into the Icarus loop: the melody, a little of the drone, and Audio
		// In 1 and 2 (unpatched, they read zero).
		int32_t wetL, wetR;
		loop_.Process(melodyL + ((droneL * kDroneSendQ12) >> 12) + in.audio[0] * kAudioInGain,
					  melodyR + ((droneR * kDroneSendQ12) >> 12) + in.audio[1] * kAudioInGain, p,
					  ramps_.value[kFeedback], ramps_.value[kDestruct], wetL, wetR);

		// The dry tap (the voices before the loop) against the loop: Main
		// on the Levels page, from clean and close to fully drenched.
		// The drone joins here, around the loop rather than through it.
		// Halved here, so Water has headroom; the output gain makes it up.
		int32_t mixL = ((melodyL * p.dryGain + wetL * p.wetGain) >> 13) + (droneL >> 1);
		int32_t mixR = ((melodyR * p.dryGain + wetR * p.wetGain) >> 13) + (droneR >> 1);

		// Everything meets at Water before the outputs.
		water_.Process(mixL, mixR, p, ramps_.value[kWaterMix]);
		if (!p.running)
			mixL = mixR = 0;
		// +/-16384 here, +/-2048 at the jacks.
		out.audio[0] = SoftClip((mixL * (kOutputGainQ12 >> 4)) >> 12);
		out.audio[1] = SoftClip((mixR * (kOutputGainQ12 >> 4)) >> 12);
		out.cv1 = p.cv1;
		out.cv2Note = p.turingNote;
		out.pulse[0] = p.pulse[0];
		out.pulse[1] = p.pulse[1];
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

		EngineParams &next = params_[1 - c];
		next = params_[c];
		control_.Tick(s, next, led);
		__sync_synchronize(); // the new block is complete before we flip
		current_ = 1 - c;
		return true;
	}

	const Control &GetControl() const { return control_; }
	Control &GetControl() { return control_; }
	const EngineParams &Params() const { return params_[current_]; } // for tests
	int32_t WaterMode() const { return water_.mode; }				 // for tests
	int32_t WaterMix() const { return ramps_.value[kWaterMix]; }	 // for tests

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
	};

	// Layout matters on the Cortex-M0+: fields near the start of the object
	// are one short load away, so the small, per-sample state comes first
	// and the big buffers come last.
	volatile int32_t current_ = 0;
	volatile int32_t ack_ = 0;
	int32_t playing_ = -1;
	bool even_ = false;
	// Every gain core 1 sets, slid smoothly: voices left and right, the
	// drone, the loop's feedback and its dropouts, and how much of Water's
	// filter is heard.
	static constexpr int kGainL = 0, kGainR = kVoices, kDroneGain = 2 * kVoices, kFeedback = kDroneGain + 1,
						 kDestruct = kFeedback + 1, kWaterMix = kDestruct + 1, kRampCount = kWaterMix + 1;
	Ramps<kRampCount> ramps_;
	SawStack<kLeadSaws> leadSaws_;
	SawStack<kSaws> loopSaws_[kLoopVoices];
	SawStack<kDroneSaws> droneSaws_;
	int32_t droneLp_[2] = {};
	int32_t loopsLp_[4] = {}, leadLp_[4] = {};
	Water water_;
	uint32_t lastTick_ = 0; // core 1 only
	SharedStatus status_;
	EngineParams params_[2];
	Control control_;
	IcarusLoop loop_; // the delay line: ~65KB, last
};

} // namespace constancy
