#pragma once
#include <cstdint>
#include "Clock.h"
#include "Dsp.h"
#include "Looper.h"
#include "Params.h"
#include "Plate.h"
#include "Ui.h"

namespace tem
{

// Everything the card reads from the panel in one sample. Kept separate
// from ComputerCard so the whole instrument can also run on a computer
// (see test/) for testing and offline renders.
struct Inputs
{
	int32_t knob[3];	  // Main, X, Y: 0..4095
	int32_t sw;			  // kSwitchDown / Middle / Up
	int32_t cv[2];		  // CV in 1 and 2: -2048..2047
	int32_t audio[2];	  // Audio in 1 and 2: -2048..2047
	bool rise[2];		  // a rising edge on Pulse in 1 / 2 this sample
	bool high[2];		  // Pulse in 1 / 2 is high (for gate mode)
	bool pulsePatched[2]; // Pulse in 1 / 2 has a cable in it
	bool audio2Patched;	  // Audio in 2 has a cable in it
};

struct Outputs
{
	int32_t audio[2]; // Audio out 1 and 2, -2048..2047
	int32_t cvMv[2];  // CV out 1 and 2, millivolts (calibrated)
	bool pulse[2];	  // Pulse out 1 and 2
};

// The whole instrument, split across the two cores:
//
//   Audio()        core 0, inside the 48kHz audio interrupt. Must finish
//                  in ~20us including ComputerCard's own work, every sample.
//   ControlTick()  core 1, in a loop. Does its work once a millisecond.
//
// The signal path, every sample:
//
//   Audio in 1 --+---------------------------- dry, x level ----------------------+
//                +--> 20ms --> Loop A --+                                         |
//   Audio in 2 --+                      +--> crush --+----------------------> + --+--> Audio out 1, 2
//   (normalled   +--> 20ms --> Loop B --+            |                        ^
//    to in 1)                                        +--> plate, x send ------+
//                |<--- on the sample clock --->|
//
// The loops and the crusher run on the sample clock and hold still between
// its ticks; the plate runs at a fixed 24kHz. The dry input bypasses all of
// it: clean, at 48kHz, at the level alt-layer X sets.
//
// Handing results from core 1 to core 0: there are two EngineParams
// blocks. Core 0 plays from params_[current_]; core 1 fills the other one
// and then flips current_. Core 0 acknowledges each flip at the start of
// its next sample (ack_), and core 1 won't touch a block again until that
// acknowledgement arrives -- so core 0 never reads a block while core 1 is
// halfway through writing it. (The same scheme as Impermanence, Equanimity,
// Constancy, Hubris and Recurrence.)
class Temperance
{
public:
	// ---- core 0 --------------------------------------------------------
	void Audio(const Inputs &in, Outputs &out)
	{
		int32_t c = current_;
		ack_ = c;
		const EngineParams &p = params_[c];
		uint32_t now = status_.samples + 1;
		Report(in, now);

		out.audio[0] = out.audio[1] = 0;
		out.cvMv[0] = out.cvMv[1] = 0;
		out.pulse[0] = out.pulse[1] = false;
		if (!p.running)
			return;

		// The inputs, Audio in 2 normalled to Audio in 1. The dry signal is
		// the mix of the two (just Audio in 1, when 2 is empty).
		int32_t a = in.audio[0];
		int32_t b = in.audio2Patched ? in.audio[1] : a;
		int32_t dry = (a + b) >> 1;
		if (a >= kClipLevel || a <= -kClipLevel)
			status_.clips[0] = status_.clips[0] + 1;
		if (b >= kClipLevel || b <= -kClipLevel)
			status_.clips[1] = status_.clips[1] + 1;
		int32_t b2 = in.audio[1];
		if (kSmooth)
		{
			Smooth(smooth_[0], a, kSmoothQ16[p.clockStep]);
			Smooth(smooth_[1], b2, kSmoothQ16[p.clockStep]);
			a = smooth_[0];
			b2 = smooth_[1];
		}

		// What the loopers hear: the same, 20ms late (see kEventDelay). The
		// normalling is decided on the way out, so the probe's pattern on
		// Audio in 2 just after an unplug never reaches loop B: by the time
		// it comes out, ComputerCard has declared the jack empty.
		int32_t r = r_;
		int32_t lateA = late_[0][r], lateB2 = late_[1][r];
		late_[0][r] = (int16_t)a;
		late_[1][r] = (int16_t)b2;
		r_ = r + 1 >= kEventDelay ? 0 : r + 1;
		int32_t lateB = in.audio2Patched ? lateB2 : lateA;
		status_.audio[0] = lateA;
		status_.audio[1] = lateB;

		// Record taps: the switch taps both loops, each Pulse in its own.
		// All of them act 20ms after they happen, in step with the late
		// input. (Core 1 debounces the switch, so taps are always further
		// apart than that.)
		//
		// The Pulse ins are read both ways all the time, as triggers and as
		// gates, and the mode picks which one acts. So changing mode never
		// lets a stale edge through: a gate already high when gate mode
		// comes on waits for its next rise.
		if (p.tapSerial != tapSeen_)
		{
			tapSeen_ = p.tapSerial;
			tapWait_ = kEventDelay;
		}
		bool gate = p.gate;
		bool tapA = pulse_[0].Process(in.rise[0], in.pulsePatched[0]) && !gate;
		bool tapB = pulse_[1].Process(in.rise[1], in.pulsePatched[1]) && !gate;
		int32_t edgeA = gate_[0].Process(in.high[0], in.pulsePatched[0], r);
		int32_t edgeB = gate_[1].Process(in.high[1], in.pulsePatched[1], r);
		if (tapWait_ > 0 && --tapWait_ == 0)
		{
			tapA = tapB = true;
			open_[0] = open_[1] = false; // the switch takes over both loops
		}
		if (tapA)
			loopA_.Tap(now, p.clockStep);
		if (tapB)
			loopB_.Tap(now, p.clockStep);
		if (gate)
		{
			Gate(loopA_, open_[0], edgeA, now, p.clockStep);
			Gate(loopB_, open_[1], edgeB, now, p.clockStep);
		}
		else
			open_[0] = open_[1] = false;

		// The clocked section, once per tick.
		if (clock_.Process(p.clockStep))
		{
			int32_t la = loopA_.Tick(lateA, kZoneSpeed[p.zone[0]], p.clockStep);
			int32_t lb = loopB_.Tick(lateB, kZoneSpeed[p.zone[1]], p.clockStep);
			// Each loop at 3/4, so two loops over the dry input sit in the
			// mix without leaning on the output's soft clip.
			held_ = Crush(((la + lb) * 3) >> 2, p.crushBits);
			if (loopB_.Wrapped())
				trigger_[1] = kTriggerSamples;
		}

		// The plate, outside the clock, on the loops alone; then the dry
		// input, untouched.
		int32_t wetL, wetR;
		plate_.Process((held_ * p.sendQ15) >> 15, p.decayQ15, p.dampQ15, p.lfoQ8, wetL, wetR);
		int32_t mix = held_ + ((dry * p.dryQ12) >> 12);
		out.audio[0] = SoftClip(mix + wetL);
		out.audio[1] = SoftClip(mix + wetR);

		// The CV outs and Pulse out 1 come from core 1 (random voltages tied
		// to the loops, and random triggers); Pulse out 2 fires at each start
		// of Loop B.
		out.cvMv[0] = p.cvMv[0];
		out.cvMv[1] = p.cvMv[1];
		if (p.pulseSerial != pulseSeen_)
		{
			pulseSeen_ = p.pulseSerial;
			trigger_[0] = kTriggerSamples;
		}
		for (int k = 0; k < 2; k++)
		{
			out.pulse[k] = trigger_[k] > 0;
			if (trigger_[k] > 0)
				trigger_[k]--;
		}

		// The loopers' positions, for the LEDs. Every 16th sample is plenty,
		// one loop at a time.
		if ((now & 15) == 0)
			ReportLoop(status_.loop[0], loopA_);
		else if ((now & 15) == 8)
			ReportLoop(status_.loop[1], loopB_);
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
		for (int k = 0; k < 2; k++)
		{
			s.cv[k] = status_.cv[k];
			s.audio[k] = status_.audio[k];
			s.clips[k] = status_.clips[k];
			const SharedLoop &l = status_.loop[k];
			s.loop[k] = {l.state, l.pos, l.len, l.starts, l.stops, l.passes};
		}

		EngineParams &next = params_[1 - c];
		next = params_[c];
		ui_.Tick(s, next, led);
		__sync_synchronize(); // the new block is complete before we flip
		current_ = 1 - c;
		return true;
	}

	// Hardware entropy at power-on, for the random voltages and triggers.
	void Seed(uint32_t seed) { ui_.Seed(seed); }

	const EngineParams &Params() const { return params_[current_]; } // for tests
	const Ui &GetUi() const { return ui_; }							 // for tests
	const Looper &Loop(int k) const { return k ? loopB_ : loopA_; }	 // for tests

private:
	static constexpr int32_t kClipLevel = 2040;
	static constexpr int32_t kTriggerSamples = 10 * 48; // 10ms

	// The smooth option's input low-pass, x 65536, for each clock step:
	// 1 - e^(-2 pi (clock / 2) / 48000). Open at 48kHz.
	static constexpr int32_t kSmoothQ16[kClockSteps] = {65536, 57466, 51912, 42538, 35656,
														26713, 21284, 15095, 11683, 8041};

	static void Smooth(int32_t &state, int32_t in, int32_t kQ16)
	{
		int32_t d = in - state;
		state += ((d * kQ16) >> 16) + (d > 0 ? 1 : 0);
	}

	// The crusher: keep the top `bits` of 12, rounding to the nearest step
	// (plain masking would round down, adding an offset).
	static int32_t Crush(int32_t x, int32_t bits)
	{
		int32_t shift = 12 - bits;
		int32_t half = (1 << shift) >> 1;
		return (x + half) & ~((1 << shift) - 1);
	}

	// Gate mode, one loop: the gate's rise records (or takes over a
	// recording already running), and its fall stops the recording its rise
	// started or took over. `open` remembers that, so the fall of a gate
	// the switch has since tapped over leaves the switch's recording alone.
	static void Gate(Looper &l, bool &open, int32_t edge, uint32_t now, int32_t clockStep)
	{
		if (edge == RecordGate::kRise)
		{
			l.Record(now, clockStep);
			open = true;
		}
		else if (edge == RecordGate::kFall && open)
		{
			open = false;
			l.Release(clockStep);
		}
	}

	// Tell core 1 what the panel is doing.
	void Report(const Inputs &in, uint32_t now)
	{
		status_.samples = now;
		for (int k = 0; k < 3; k++)
			status_.knob[k] = in.knob[k];
		status_.sw = in.sw;
		status_.cv[0] = in.cv[0];
		status_.cv[1] = in.cv[1];
	}

	struct SharedLoop
	{
		volatile int32_t state = Looper::kEmpty, pos = 0, len = 0;
		volatile uint32_t starts = 0, stops = 0, passes = 0;
	};

	static void ReportLoop(SharedLoop &s, const Looper &l)
	{
		s.state = l.State();
		s.pos = l.Pos();
		s.len = l.Len();
		s.starts = l.Starts();
		s.stops = l.Stops();
		s.passes = l.Passes();
	}

	// Shared between the cores (see the class comment).
	struct SharedStatus
	{
		volatile uint32_t samples = 0;
		volatile int32_t knob[3] = {0, 0, 0};
		volatile int32_t sw = kSwitchMiddle;
		volatile int32_t cv[2] = {0, 0};
		volatile int32_t audio[2] = {0, 0};
		volatile uint32_t clips[2] = {0, 0};
		SharedLoop loop[2];
	};

	// Layout matters on the Cortex-M0+: fields near the start of the object
	// are one short load away, so the small, per-sample state comes first
	// and the big buffers come last.
	volatile int32_t current_ = 0;
	volatile int32_t ack_ = 0;
	int32_t held_ = 0;				 // the loops, crushed, held between ticks
	int32_t trigger_[2] = {0, 0};	 // samples left of each Pulse out's trigger
	uint32_t pulseSeen_ = 0;
	int32_t r_ = 0;					 // the late input's position
	int32_t tapWait_ = 0;			 // samples until a switch tap acts
	uint32_t tapSeen_ = 0;
	bool open_[2] = {false, false};	 // gate mode: this loop's gate is recording it
	int32_t smooth_[2] = {0, 0};
	SampleClock clock_;
	RecordPulse pulse_[2];
	RecordGate gate_[2];
	uint32_t lastTick_ = 0; // core 1 only
	SharedStatus status_;
	EngineParams params_[2];
	Ui ui_;
	int16_t late_[2][kEventDelay] = {}; // 20ms of each input: 3.75KB
	Plate plate_;						// 36KB
	Looper loopA_, loopB_;				// 59KB each
};

} // namespace tem
