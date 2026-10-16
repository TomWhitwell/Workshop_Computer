#pragma once
#include <cstdint>
#include "Config.h"
#include "Drone.h"
#include "Params.h"
#include "Hook.h"
#include "Loops.h"
#include "Notes.h"
#include "Random.h"
#include "SmoothRandom.h"
#include "Tables.h"
#include "Tide.h"
#include "TideTriggers.h"

namespace eq
{

// Everything that runs once a millisecond, on core 1: the knobs and switch,
// pickup, the Pulse Ins, the tide, the note loops and births, the change to
// a new set, the tape wobble, the drone's slow cycles, the wander CVs, the
// triggers, and the LEDs.
//
// Controls:
//   Switch middle   Main = wet/dry, X / Y = feedback of the 1.1s / 9.7s line
//   Switch up       Main = tone (one of six), X = melody volume,
//                   Y = drone volume
//   Switch down     tap: a new set (as Pulse In 2). New notes come from
//                   Pulse In 1.
class Control
{
public:
	void Seed(uint32_t seed)
	{
		rng_.Seed(seed);
		tide_.Seed(rng_);
		// Tape wobble: each read point drifts by up to a few ms, slowly.
		for (int i = 0; i < 4; i++)
		{
			int32_t depth = i < 2 ? kShortWobbleMs * 24 << 12 : kLongWobbleMs * 8 << 12; // line samples, Q12
			wobble_[i].Configure(0, depth, kWobbleIntervalMinMs, kWobbleIntervalMaxMs, kWobbleStageMs);
		}
		wander_.Configure(0, kWanderMax, kWanderIntervalMinMs, kWanderIntervalMaxMs, kWanderStageMs);
		PickSet();
	}

	// Where a new set gets its fresh seed (on the card: the RP2040's
	// hardware random numbers). Without one, it comes from the current
	// generator -- repeatable, for tests.
	void SetEntropy(uint32_t (*entropy)()) { entropy_ = entropy; }

	// For tests: hold the tide at a level (Q12), or -1 to let it move.
	void HoldTide(int32_t level) { heldTide_ = level; }

	// One control update. `p` is the parameter block core 0 will play next;
	// it starts as a copy of the one core 0 is playing now.
	void Tick(const Status &s, EngineParams &p, int32_t led[6])
	{
		if (!started_)
		{
			if (s.samples < kSettleSamples)
			{
				for (int i = 0; i < 6; i++)
					led[i] = 0;
				return;
			}
			Start(s, p);
		}
		ms_++;
		tideLevel_ = heldTide_ >= 0 ? heldTide_ : tide_.At(s.samples);

		UpdateSwitch(s);
		UpdateKnobs(s, p);

		// Pulse In 1: a new note. Pulse In 2: a new set (ignored while the
		// last change is still under way).
		for (int n = pulseIn_[0].Confirmed(s.pulseEdges[0], s.pulseConnected[0]); n > 0; n--)
			Birth(s, p);
		if (pulseIn_[1].Confirmed(s.pulseEdges[1], s.pulseConnected[1]) > 0)
			BeginChangeover();

		// Notes: the loops play on, and now and then a new one is born --
		// except during a change of set.
		uint32_t strikes = loops_.Strikes();
		loops_.Tick(p, kTone[tone_]);
		UpdateChangeover(s, p);
		if (changeover_ == kNoChange && rng_.Chance24(BirthChance()))
			Birth(s, p);
		if (loops_.Strikes() != strikes)
			triggerMs_[0] = kTriggerMs; // Pulse Out 1: every melody strike

		UpdateWobble(p);
		UpdateOutputs(p);
		drone_.Tick(p, tideLevel_);
		UpdateLeds(led);
	}

	// For tests.
	int32_t Root() const { return root_; }
	int32_t ScaleIndex() const { return scale_; }
	int32_t ToneIndex() const { return tone_; }
	int32_t Mix() const { return mix_.Value(); }
	bool Changing() const { return changeover_ != kNoChange; }
	uint32_t Births() const { return births_; }
	uint32_t Taps() const { return taps_; }
	uint32_t NewSets() const { return newSets_; }
	int32_t TideLevel() const { return tideLevel_; }
	const Loops &GetLoops() const { return loops_; }

private:
	// ---- Pulse Ins ----------------------------------------------------------

	// A Pulse In's rising edges count only once they have waited 20ms with
	// the jack still patched: pulling a cable out makes ComputerCard's jack
	// detection feed ~11ms of random edges into the input, which would
	// otherwise birth notes (or start a new set) on every unplug.
	struct PulseIn
	{
		uint32_t seen = 0;
		int32_t waiting[4] = {};
		int32_t count = 0;

		// Once a millisecond: how many edges have just been confirmed.
		int32_t Confirmed(uint32_t edges, bool connected)
		{
			uint32_t fresh = edges - seen;
			seen = edges;
			if (!connected)
			{
				count = 0;
				return 0;
			}
			for (; fresh > 0 && count < 4; fresh--)
				waiting[count++] = kPulseConfirmMs;
			int32_t done = 0;
			for (int i = 0; i < count; i++)
				done += --waiting[i] <= 0 ? 1 : 0;
			// Edges arrive in order, so the confirmed ones are at the front.
			for (int i = done; i < count; i++)
				waiting[i - done] = waiting[i];
			count -= done;
			return done;
		}
	};

	// ---- Start, sets and the changeover ---------------------------------------

	void Start(const Status &s, EngineParams &p)
	{
		started_ = true;
		lastSwitch_ = s.sw;
		// The page the switch starts on takes its knobs as they are; the
		// other page's settings wait at their defaults for pickup.
		if (s.sw == kSwitchUp)
		{
			volume_[0].Grab(s.knob[1]);
			volume_[1].Grab(s.knob[2]);
			toneAnchor_ = s.knob[0];
		}
		else
		{
			mix_.Grab(s.knob[0]);
			feedback_[0].Grab(s.knob[1]);
			feedback_[1].Grab(s.knob[2]);
		}
		p.running = true;
		// The card speaks as soon as it's turned on.
		Birth(s, p);
		drone_.Start(root_, rng_);
	}

	// A set: a random root, scale and tone.
	void PickSet()
	{
		root_ = rng_.Below(12);
		scale_ = rng_.Below(kScales);
		tone_ = rng_.Below(kTones);
	}

	// Pulse In 2, or a tap down: let the old set go. Its loops
	// play out a last repeat or two, the drone fades, and the delays'
	// feedback falls away so the old key drains out of them.
	void BeginChangeover()
	{
		if (changeover_ != kNoChange)
			return;
		newSets_++;
		changeover_ = kFading;
		loops_.BeginNewSet();
		drone_.FadeOut();
	}

	void UpdateChangeover(const Status &s, EngineParams &p)
	{
		// The feedback scale: down to nothing during a change, then back up.
		if (changeover_ != kNoChange)
			feedbackScale_ = feedbackScale_ > kDuckStep ? feedbackScale_ - kDuckStep : 0;
		else
			feedbackScale_ = feedbackScale_ < kFullScale - kRestoreStep ? feedbackScale_ + kRestoreStep : kFullScale;

		if (changeover_ == kFading && loops_.OldSetDone(s.voiceAmp))
		{
			// The old notes have rung out; give the delays time to empty.
			changeover_ = kDraining;
			drainMs_ = kChangeoverDrainMs;
		}
		else if (changeover_ == kDraining && --drainMs_ <= 0)
		{
			// The new set: a fresh seed, root, scale and tone, a first note,
			// and the drone on the new root.
			changeover_ = kNoChange;
			rng_.Seed(entropy_ ? entropy_() : rng_.Next());
			PickSet();
			toneAnchor_ = s.knob[0]; // Main doesn't drag the new tone back
			toneArmed_ = false;
			Birth(s, p);
			drone_.Start(root_, rng_);
		}
	}

	// ---- Switch -----------------------------------------------------------

	void UpdateSwitch(const Status &s)
	{
		bool down = s.sw == kSwitchDown;
		bool wasDown = lastSwitch_ == kSwitchDown;

		// Tap down: a new set (as Pulse In 2). It acts the moment the switch
		// goes down -- nothing ever depends on holding it there, since Down
		// is the spring-loaded position.
		if (down && !wasDown)
		{
			taps_++;
			BeginChangeover();
		}

		// The knobs change jobs between up and the other two positions:
		// the new page's settings wait for each knob to come back to them.
		bool up = s.sw == kSwitchUp, wasUp = lastSwitch_ == kSwitchUp;
		if (up && !wasUp)
		{
			volume_[0].Unhook(s.knob[1]);
			volume_[1].Unhook(s.knob[2]);
			toneAnchor_ = s.knob[0]; // the tone waits for Main to move
			toneArmed_ = false;
		}
		if (!up && wasUp)
		{
			mix_.Unhook(s.knob[0]);
			feedback_[0].Unhook(s.knob[1]);
			feedback_[1].Unhook(s.knob[2]);
		}
		lastSwitch_ = s.sw;
	}

	// ---- Knobs ------------------------------------------------------------

	void UpdateKnobs(const Status &s, EngineParams &p)
	{
		if (s.sw == kSwitchUp)
		{
			UpdateTone(s.knob[0]);
			volume_[0].Update(s.knob[1]);
			volume_[1].Update(s.knob[2]);
		}
		else
		{
			mix_.Update(s.knob[0]);
			feedback_[0].Update(s.knob[1]);
			feedback_[1].Update(s.knob[2]);
		}

		// Wet/dry: an equal-power crossfade, so the overall loudness holds
		// steady as you turn. A quarter turn of a sine wave: dry follows
		// cos, wet follows sin. (Phase 2^30 is a quarter cycle.)
		uint32_t phase = (uint32_t)mix_.Value() << 18;
		p.dryGain = Sine(phase + (1u << 30)) >> 3;
		p.wetGain = ((Sine(phase) >> 3) * kWetLevelQ12) >> 12;

		// Volumes: knob position squared, so the knobs feel even to the ear.
		p.melodyGain = (volume_[0].Value() * volume_[0].Value()) >> 12;
		p.droneGain = (volume_[1].Value() * volume_[1].Value()) >> 12;

		// Feedback: X (1.1s line) and Y (9.7s line), each plus its CV input
		// in every switch position. The CV is smoothed a little (~16ms) so it
		// can't zipper in the loop; +5V adds most of a knob's travel, and
		// negative voltage takes away. During a change of set it all fades
		// to nothing (feedbackScale_).
		int32_t scale = feedbackScale_ >> 8; // Q12
		for (int i = 0; i < 2; i++)
		{
			cvSmooth_[i] += (s.cv[i] * 32 - cvSmooth_[i]) >> 4; // 2 x CV, Q4
			int32_t amount = feedback_[i].Value() + (cvSmooth_[i] >> 4);
			amount = amount < 0 ? 0 : (amount > 4095 ? 4095 : amount);
			int32_t gain = (((amount * kFeedbackCeilingQ12) >> 12) * scale) >> 12;
			if (i == 0)
				p.shortFeedback = gain;
			else
				p.longFeedback = gain;
		}
	}

	// Main with the switch up: six equal zones, left to right, one per tone.
	// It only takes over once it has moved, and changes zone only once it is
	// clearly inside the next one, so knob jitter at a boundary can't
	// flicker.
	void UpdateTone(int32_t main)
	{
		if (!toneArmed_)
		{
			int32_t moved = main - toneAnchor_;
			if (moved > kToneMoveThreshold || moved < -kToneMoveThreshold)
				toneArmed_ = true;
			else
				return;
		}
		int32_t zone = (main * 6) >> 12;
		if (zone == tone_)
			return;
		int32_t centre = (tone_ * 4096 + 2048) / 6;
		int32_t distance = main > centre ? main - centre : centre - main;
		if (distance > 4096 / 12 + 32)
			tone_ = zone > 5 ? 5 : zone;
	}

	// ---- Births -----------------------------------------------------------

	// No new notes during a change of set: they would be in the old key.
	void Birth(const Status &s, EngineParams &p)
	{
		if (changeover_ != kNoChange)
			return;
		births_++;
		flickerMs_ = kBirthFlickerMs;
		loops_.Birth(p, rng_, root_, scale_, kTone[tone_], tideLevel_, s.voiceAmp);
	}

	// Chance of a new note this millisecond (out of 2^24): about one every
	// 12s at low tide, rising to one every 2.5s at high tide. The tide is
	// squared first, so the still passages last longer and the busy ones
	// feel earned.
	int32_t BirthChance() const
	{
		constexpr int32_t kLow = (1 << 24) / kBirthEveryMsLowTide;
		constexpr int32_t kHigh = (1 << 24) / kBirthEveryMsHighTide;
		return kLow + (((kHigh - kLow) * TideCurve()) >> 12);
	}

	// The tide squared. Three sines summed spend most of their time near the
	// middle and only now and then visit the extremes; squaring stretches
	// the quiet passages and saves the busy ones for the real high tides.
	int32_t TideCurve() const { return (tideLevel_ * tideLevel_) >> 12; }

	// ---- Tape wobble ------------------------------------------------------

	void UpdateWobble(EngineParams &p)
	{
		for (int i = 0; i < 2; i++)
		{
			p.shortDelay[i] = (kShortDelaySamples << 12) + wobble_[i].Tick(rng_, 4096);
			p.longDelay[i] = (kLongDelaySamples << 12) + wobble_[2 + i].Tick(rng_, 4096);
		}
	}

	// ---- CV and Pulse Outs, tremolo ------------------------------------------

	void UpdateOutputs(EngineParams &p)
	{
		// CV Out 2: the wander, lazy at low tide, restless at high tide.
		// CV Out 1: its mirror image, the ebb to its flow.
		int32_t speed = kWanderSpeedLowTideQ12 + (((kWanderSpeedHighTideQ12 - kWanderSpeedLowTideQ12) * tideLevel_) >> 12);
		wander_.Tick(rng_, speed);
		p.cv[1] = wander_.ValueFine();
		p.cv[0] = kWanderMax * 128 - p.cv[1];

		// Pulse Out 2: tide triggers. Both Pulse Outs give 10ms triggers.
		if (tideTriggers_.Tick(rng_, TideCurve()))
			triggerMs_[1] = kTriggerMs;
		for (int i = 0; i < 2; i++)
		{
			p.trigger[i] = triggerMs_[i] > 0;
			if (triggerMs_[i] > 0)
				triggerMs_[i]--;
		}

		// The vibraphone's tremolo fades in and out (~300ms) with the tone.
		int32_t target = kTone[tone_].tremolo ? kTremoloDepthQ12 : 0;
		int32_t step = kTremoloDepthQ12 / 300 + 1;
		int32_t &d = p.tremoloDepth;
		d = d < target ? (d + step > target ? target : d + step) : (d - step < target ? target : d - step);
	}

	// ---- LEDs -------------------------------------------------------------

	void UpdateLeds(int32_t led[6])
	{
		for (int i = 0; i < 6; i++)
			led[i] = 0;

		if (lastSwitch_ == kSwitchUp)
		{
			// Tone select: one LED, in panel order (top left is Harp).
			led[tone_] = 4095;
			return;
		}
		// Play: the tide, as a meter filling from the bottom row to the top.
		// Each new note gives the lit LEDs a brief, soft flicker.
		Meter(tideLevel_, led);
		if (flickerMs_ > 0)
		{
			flickerMs_--;
			int32_t dip = 4096 - ((2458 * flickerMs_) / kBirthFlickerMs); // down to 40%, recovering
			for (int i = 0; i < 6; i++)
				led[i] = (led[i] * dip) >> 12;
		}
	}

	// Six LEDs as a bar, bottom row first, each fading in as the level
	// passes through its sixth of the range.
	static void Meter(int32_t level, int32_t led[6])
	{
		static constexpr int kOrder[6] = {4, 5, 2, 3, 0, 1};
		int32_t six = level * 6; // 0..6 x 4096
		for (int k = 0; k < 6; k++)
		{
			int32_t b = six - k * 4096;
			b = b < 0 ? 0 : (b > 4096 ? 4096 : b);
			led[kOrder[k]] = Gamma(b);
		}
	}

	// LED brightness is linear in power, but eyes aren't: squaring makes a
	// fade look even.
	static int32_t Gamma(int32_t b) { return (b * b) >> 12 > 4095 ? 4095 : (b * b) >> 12; }

	// Changeover stages, and the feedback scale (Q20) that falls during one.
	static constexpr int32_t kNoChange = 0, kFading = 1, kDraining = 2;
	static constexpr int32_t kFullScale = 1 << 20;
	static constexpr int32_t kDuckStep = kFullScale / kChangeoverDuckMs;
	static constexpr int32_t kRestoreStep = kFullScale / kChangeoverRestoreMs;

	Random rng_;
	bool started_ = false;
	uint32_t ms_ = 0;

	int32_t lastSwitch_ = kSwitchMiddle;

	// Switch middle: wet/dry and the two feedbacks. Switch up: the two
	// volumes (and the tone, which has no pickup -- see UpdateTone).
	Hook mix_{2048};
	Hook feedback_[2] = {Hook(2048), Hook(2048)};
	Hook volume_[2] = {Hook(kMelodyVolumeStart), Hook(kDroneVolumeStart)};
	int32_t cvSmooth_[2] = {0, 0};

	int32_t root_ = 0;	// 0 = C .. 11 = B
	int32_t scale_ = 0; // index into kScaleMask
	int32_t tone_ = 0;	// index into kTone
	int32_t toneAnchor_ = 0;
	bool toneArmed_ = false;

	uint32_t births_ = 0;
	uint32_t taps_ = 0;
	Loops loops_;
	int32_t flickerMs_ = 0;

	int32_t changeover_ = kNoChange;
	int32_t drainMs_ = 0;
	int32_t feedbackScale_ = kFullScale;
	uint32_t newSets_ = 0;

	Tide tide_;
	int32_t tideLevel_ = 2048; // Q12, this millisecond
	int32_t heldTide_ = -1;

	SmoothRandom wobble_[4]; // short L, short R, long L, long R
	Drone drone_;
	SmoothRandom wander_;
	TideTriggers tideTriggers_;
	int32_t triggerMs_[2] = {0, 0};

	PulseIn pulseIn_[2];
	uint32_t (*entropy_)() = nullptr;
};

} // namespace eq
