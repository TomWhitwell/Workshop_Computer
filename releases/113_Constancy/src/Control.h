#pragma once
#include <cstdint>
#include "Config.h"
#include "Day.h"
#include "Drone.h"
#include "Envelope.h"
#include "Hook.h"
#include "Lead.h"
#include "Loops.h"
#include "Notes.h"
#include "Params.h"
#include "Random.h"
#include "Tables.h"
#include "Turing.h"
#include "Voice.h"
#include "Wander.h"

namespace constancy
{

// Everything that runs once a millisecond, on core 1: the knobs and switch,
// the Time position and the morning's data, the notes (loops, lead,
// drone), every voice's pitch and envelope, the loop's settings, the
// Turing machine, the CVs and the LEDs.
class Control
{
public:
	void Seed(uint32_t seed)
	{
		rng_.Seed(seed);
		for (VoiceControl &v : voices_)
			v.Seed(rng_);
		voices_[kLead].SetLead();
		// Cutoffs as pitches, so the sun can glide between them evenly.
		voiceCutoff_[0] = HzToPitchQ8(kVoiceCutoffDawnHz);
		voiceCutoff_[1] = HzToPitchQ8(kVoiceCutoffSunriseHz);
		voiceCutoff_[2] = HzToPitchQ8(kVoiceCutoffMorningHz);
		ladderCutoff_[0] = HzToPitchQ8(kLadderDawnHz);
		ladderCutoff_[1] = HzToPitchQ8(kLadderSunriseHz);
		ladderCutoff_[2] = HzToPitchQ8(kLadderMorningHz);
		// Delay times as log2(ms), so the sun moves them evenly by ear.
		delayLog_[0] = Log2(kDelayDawnMs);
		delayLog_[1] = Log2(kDelaySunriseMs);
		delayLog_[2] = Log2(kDelayMorningMs);
		droneCoef_ = CutoffCoef(HzToPitchQ8(kDroneCutoffHz));
		waterLp_[0] = HzToPitchQ8(kWaterLowpassOpenHz);
		waterLp_[1] = HzToPitchQ8(kWaterLowpassClosedHz);
		waterHp_[0] = HzToPitchQ8(kWaterHighpassOpenHz);
		waterHp_[1] = HzToPitchQ8(kWaterHighpassClosedHz);
		PickKey();
		turing_.Seed(rng_);
	}

	void SetEntropy(uint32_t (*entropy)()) { entropy_ = entropy; }

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

		UpdateSwitch(s);
		UpdateKnobs(s);
		UpdateTime(s);

		// Pulse In 1: the Turing machine's clock. Pulse In 2: a lead phrase.
		for (int32_t n = pulseIn_[0].Confirmed(s.pulseEdges[0], s.pulseConnected[0]); n > 0; n--)
			StepTuring();
		if (pulseIn_[1].Confirmed(s.pulseEdges[1], s.pulseConnected[1]) > 0)
			lead_.Request();
		UpdateReseed();

		// The notes: the three loops and the lead, then every voice's
		// pitch and envelope.
		bool settled = reseed_ == kSettled;
		loops_.Tick(rng_, voices_, root_, scale_, morning_.tide, settled);
		if (lead_.Tick(rng_, voices_[kLead], root_, scale_, morning_.tide, SunUp(), settled))
			leadTriggerMs_ = kTriggerMs;
		int32_t melody = (((kVoicePeakQ14 * Squared(melody_.Value())) >> 12) * MelodyMakeup()) >> 12;
		voices_[kLead].Tick(rng_, p.voice[kLead], (melody * kLeadLevelQ12) >> 12);
		for (int v = 1; v < kVoices; v++)
			voices_[v].Tick(rng_, p.voice[v], melody);
		drone_.Tick(rng_, p, (kDronePeakQ14 * Squared(droneLevel_.Value())) >> 12);
		p.droneCoef = droneCoef_;

		UpdateBrightness(p);
		UpdateLoop(p);
		UpdateMix(p);
		UpdateWater(s, p);
		UpdateOutputs(p);
		UpdateLeds(led);
	}

	// For tests.
	const Morning &GetMorning() const { return morning_; }
	const Loops &GetLoops() const { return loops_; }
	const Lead &GetLead() const { return lead_; }
	const Drone &GetDrone() const { return drone_; }
	bool Reseeding() const { return reseed_ != kSettled; }
	uint32_t Reseeds() const { return reseeds_; }
	const TuringMachine &GetTuring() const { return turing_; }
	uint32_t TuringSteps() const { return turingSteps_; }
	int32_t TuringKnob() const { return turingKnob_.Value(); }
	int32_t MelodyKnob() const { return melody_.Value(); }
	int32_t DroneKnob() const { return droneLevel_.Value(); }
	int32_t SwoopMs() const { return swoopMs_; }
	int32_t TimeQ8() const { return (timeQ8_ + 128) >> 8; }
	int32_t Root() const { return root_; }
	int32_t ScaleIndex() const { return scale_; }
	const VoiceControl &Voice(int v) const { return voices_[v]; }

private:
	// ---- Pulse Ins ----------------------------------------------------------

	// A Pulse In's rising edges count only once they have waited 20ms with
	// the jack still patched: pulling a cable out makes ComputerCard's jack
	// detection feed ~11ms of random edges into the input, which would
	// otherwise reseed (or start a phrase) on every unplug. (From
	// Equanimity.)
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

	// ---- Start, the switch and reseeding ----------------------------------------

	void Start(const Status &s, EngineParams &p)
	{
		started_ = true;
		lastSwitch_ = s.sw;
		// The page the switch starts on takes its knobs as they are; the
		// other page's settings wait at their defaults for pickup.
		if (s.sw == kSwitchUp)
		{
			turingKnob_.Grab(s.knob[0]);
			melody_.Grab(s.knob[1]);
			droneLevel_.Grab(s.knob[2]);
		}
		else
		{
			time_.Grab(s.knob[0]);
			sunKnob_.Grab(s.knob[1]);
			waterKnob_.Grab(s.knob[2]);
		}
		// Straight to the knob's moment of the morning, not a sweep to it.
		timeQ8_ = TimeKnob(s) * kDayEndQ8 / 4095 * 256;
		sun_ = sunKnob_.Value() << 8;
		water_ = waterKnob_.Value() << 8;
		// Starting before sunrise, the sunrise swoop is waiting for the
		// horizon; starting after, it has already happened.
		swoopArmed_ = MorningAt(timeQ8_ >> 8).sun < kSunriseSunQ12;
		p.running = true;
		loops_.Start(rng_);
		drone_.Start(root_);
		turingNote_ = turing_.Note(root_, scale_); // CV Out 2 sits on a note of the key
	}

	// A root and one of the six scales.
	void PickKey()
	{
		root_ = rng_.Below(12);
		scale_ = rng_.Below(kScales);
	}

	void UpdateSwitch(const Status &s)
	{
		// Tap down: a reseed. It acts the moment the switch goes down --
		// nothing ever depends on holding it there, since Down is the
		// spring-loaded position.
		if (s.sw == kSwitchDown && lastSwitch_ != kSwitchDown)
		{
			BeginReseed();
		}

		// The knobs change jobs between up (Levels) and the other two
		// positions (Play): the new page's settings wait for each knob to
		// come back to them, so nothing jumps.
		bool up = s.sw == kSwitchUp, wasUp = lastSwitch_ == kSwitchUp;
		if (up && !wasUp)
		{
			turingKnob_.Unhook(s.knob[0]);
			melody_.Unhook(s.knob[1]);
			droneLevel_.Unhook(s.knob[2]);
		}
		if (!up && wasUp)
		{
			time_.Unhook(s.knob[0]);
			sunKnob_.Unhook(s.knob[1]);
			waterKnob_.Unhook(s.knob[2]);
		}
		lastSwitch_ = s.sw;
	}

	void UpdateKnobs(const Status &s)
	{
		if (s.sw == kSwitchUp)
		{
			turingKnob_.Update(s.knob[0]);
			melody_.Update(s.knob[1]);
			droneLevel_.Update(s.knob[2]);
		}
		else
		{
			time_.Update(s.knob[0]);
			sunKnob_.Update(s.knob[1]);
			waterKnob_.Update(s.knob[2]);
		}
	}

	// The melody's loudness makeup for this moment of the morning (Q12), from
	// kMelodyMakeupDbX10: blended in dB, then 2^(dB / 6.02).
	int32_t MelodyMakeup() const
	{
		int32_t sun = morning_.sun;
		int32_t i = sun >> 9, f = sun & 511;
		if (i >= 8)
		{
			i = 7;
			f = 512;
		}
		int32_t dbX10 = kMelodyMakeupDbX10[i] + (((kMelodyMakeupDbX10[i + 1] - kMelodyMakeupDbX10[i]) * f) >> 9);
		return Exp2(dbX10 * 68) >> 4; // 4096 / 60.2 = 68 per tenth of a dB
	}

	// Levels are knob position squared, so the knobs feel even to the ear.
	static int32_t Squared(int32_t knob) { return (knob * knob) >> 12; }

	// A new key: every melodic note stops (those sounding fade as they
	// would), the loop's feedback dips so the old key drains out of it, and
	// the drone glides straight to the new root. (Ignored while one is
	// already under way.)
	void BeginReseed()
	{
		if (reseed_ != kSettled)
			return;
		reseed_ = kFading;
		reseedMs_ = 0;
		reseeds_++;
		flickerMs_ = kReseedFlickerMs;
		loops_.Stop(voices_);
		lead_.Stop(voices_[kLead]);
		rng_.Seed(entropy_ ? entropy_() : rng_.Next());
		PickKey();
		drone_.GlideTo(root_);
	}

	void UpdateReseed()
	{
		// The feedback scale (Q12): down during a reseed, then back up.
		if (reseed_ == kFading)
		{
			constexpr int32_t kStep = (4096 - kReseedFeedbackQ12) / kReseedDuckMs + 1;
			feedbackScale_ = feedbackScale_ - kStep > kReseedFeedbackQ12 ? feedbackScale_ - kStep : kReseedFeedbackQ12;
		}
		else
		{
			constexpr int32_t kStep = (4096 - kReseedFeedbackQ12) / kReseedRestoreMs + 1;
			feedbackScale_ = feedbackScale_ + kStep < 4096 ? feedbackScale_ + kStep : 4096;
		}

		// New melodic notes only once the drone has reached the new root,
		// the old notes have all ended and the loop has had time to drain.
		if (reseed_ == kFading && ++reseedMs_ >= kReseedDrainMs && !drone_.Gliding() && loops_.Quiet(voices_) &&
			lead_.Resting() && voices_[kLead].Silent())
		{
			reseed_ = kSettled;
			loops_.Restart(rng_);
		}
	}

	// ---- Time ---------------------------------------------------------------

	// Main picks the moment of the morning, plus CV In 1 (+5V moves it
	// about the whole window); the data follows.
	int32_t TimeKnob(const Status &s) const
	{
		int32_t t = time_.Value() + s.cv[0] * 2;
		return t < 0 ? 0 : (t > 4095 ? 4095 : t);
	}

	void UpdateTime(const Status &s)
	{
		int32_t pos = TimeKnob(s) * kDayEndQ8 / 4095;
		Lag(timeQ8_, pos, kTimeLagMs);
		morning_ = MorningAt((timeQ8_ + 128) >> 8);
	}

	// How far the sun is above the horizon as seen, as a fraction (Q12) of
	// the way from sunrise to the top of the window; below it, negative.
	int32_t SunUp() const
	{
		int32_t above = morning_.sun - kSunriseSunQ12;
		return above < 0 ? above : above * 4096 / (4096 - kSunriseSunQ12);
	}

	// A one-pole lag on core 1, once a millisecond: close 1/ms of the gap.
	// `state` keeps 8 more bits than the value, so it settles finely.
	static void Lag(int32_t &state, int32_t target, int32_t ms)
	{
		state += (target * 256 - state) / ms;
	}

	// ---- Brightness -----------------------------------------------------------

	// Somewhere along a three-point curve -- dawn, sunrise, morning -- by
	// the sun (Q12). Before sunrise the first half, after it the second.
	static int32_t SunCurve(int32_t sun, const int32_t point[3])
	{
		if (sun < kSunriseSunQ12)
			return point[0] + (point[1] - point[0]) * sun / kSunriseSunQ12;
		return point[1] + (point[2] - point[1]) * (sun - kSunriseSunQ12) / (4096 - kSunriseSunQ12);
	}

	// The CS-80 lowpass on the voices: muffled before dawn, open by
	// morning. The lead's swells above it.
	void UpdateBrightness(EngineParams &p)
	{
		int32_t base = SunCurve(morning_.sun, voiceCutoff_);
		p.loopsCoef = CutoffCoef(base);
		// Octaves (Q12) to semitones x 256: x 12 x 256 / 4096.
		p.leadCoef = CutoffCoef(base + voices_[kLead].SwellQ12() * 3 / 4);
	}

	// ---- The Icarus loop ---------------------------------------------------

	void UpdateLoop(EngineParams &p)
	{
		// Delay time follows the sun: short and fragile before dawn, long
		// and settled by morning. Lagged by 0.2s, as Icarus does: a change
		// of delay time bends the pitch of everything in the loop.
		int32_t ms = Exp2(SunCurve(morning_.sun, delayLog_)); // ms, Q16
		int32_t target = (ms * 3) >> 5;						  // x 24 x 256 / 65536: loop samples x 256
		target += Swoop() * 24 * 256;
		delayQ8_ += (target - delayQ8_) / kDelayLagMs;
		// Never further back than the line holds.
		constexpr int32_t kLongest = (kDelaySize - 4) << 8;
		p.delayQ8 = delayQ8_ < kLongest ? delayQ8_ : kLongest;

		// Sun (X): feedback from Icarus's gentle 0.4 through 0.8 at the
		// centre to a burning 1.5, and drive into the clipper from 1.0
		// through Icarus's 1.25 at the centre to 2.0. A running tide pushes
		// the feedback a little further towards the sun; at the turn of the
		// tide, nothing.
		Lag(sun_, sunKnob_.Value(), kKnobLagMs);
		int32_t x = (sun_ + 128) >> 8;
		int32_t feedback = SunFeedback(x);
		feedback += (kTideFeedbackNudgeQ12 * morning_.tideRate) >> 12;
		p.feedbackQ12 = (feedback * feedbackScale_) >> 12;
		p.driveQ12 = x < 2048 ? 4096 + 1024 * x / 2048 : 5120 + (kDriveMaxQ12 - 5120) * (x - 2048) / 2047;
		// How hard the loop is burning: its feedback x drive (Q12), against
		// 0.8 x 1.25 = 1 at the Sun knob's centre.
		burnQ12_ = (p.feedbackQ12 * p.driveQ12) >> 12;

		// The ladder opens with the sun. (It runs at the loop's 24kHz, where
		// a coefficient does what it would an octave higher at 48kHz.)
		p.ladderCoef = CutoffCoef(SunCurve(morning_.sun, ladderCutoff_) + 12 * 256);
		p.ladderResQ12 = kLadderResonanceQ12;

		UpdateDestruction(p);
	}

	// The Sun knob's feedback (Q12): two straight lines meeting at the centre.
	static int32_t SunFeedback(int32_t x)
	{
		if (x < 2048)
			return kFeedbackMinQ12 + (kFeedbackCentreQ12 - kFeedbackMinQ12) * x / 2048;
		return kFeedbackCentreQ12 + (kFeedbackMaxQ12 - kFeedbackCentreQ12) * (x - 2048) / 2047;
	}

	// Icarus's destruction: below a quarter-second delay, the loop ducks to
	// half for 0.2s at random moments (SuperCollider's Dust), more often
	// the shorter the delay -- the fragile time before dawn.
	void UpdateDestruction(EngineParams &p)
	{
		int32_t ms = delayQ8_ / (24 * 256);
		if (ms < kDestructionBelowMs)
		{
			int32_t rateQ12 = kDestructionMaxHzQ12 * (kDestructionBelowMs - ms) / (kDestructionBelowMs - kDelayDawnMs);
			// Per millisecond, out of 2^24: rate / 1000 x 2^24 / 4096.
			if (rng_.Chance24((rateQ12 * 4194) >> 10))
				destructMs_ = kDestructionMs;
		}
		int32_t dip = 0;
		if (destructMs_ > 0)
		{
			// Half a sine: down and back up.
			destructMs_--;
			uint32_t phase = (uint32_t)((kDestructionMs - destructMs_) * (0x80000000u / kDestructionMs));
			dip = Sine(phase); // 0..32767
		}
		p.destructQ15 = 32768 - (dip >> 1);
	}

	// The sunrise swoop: as the sun crosses the horizon, the delay
	// stretches and comes back once, like holding Icarus's time key -- the
	// whole loop bends down in pitch and back up. It fires again only after
	// the sun has gone well back below the horizon. Returns the extra delay
	// in ms.
	int32_t Swoop()
	{
		if (morning_.sun < kSunriseSunQ12 - kSwoopRearmQ12)
			swoopArmed_ = true;
		if (swoopArmed_ && morning_.sun >= kSunriseSunQ12)
		{
			swoopArmed_ = false;
			swoopMs_ = 1;
		}
		if (swoopMs_ == 0)
			return 0;
		int32_t t = swoopMs_++;
		uint32_t phase;
		if (t < kSwoopRiseMs)
			phase = (uint32_t)t * (0x80000000u / kSwoopRiseMs); // 0..pi: rising
		else if (t < kSwoopRiseMs + kSwoopFallMs)
			phase = 0x80000000u + (uint32_t)(t - kSwoopRiseMs) * (0x80000000u / kSwoopFallMs); // pi..2pi: falling
		else
		{
			swoopMs_ = 0;
			return 0;
		}
		// (1 - cos) / 2: a smooth rise from 0 to 1 and back.
		int32_t shape = (32768 - Sine(phase + 0x40000000u)) >> 1; // Q15
		return (kSwoopMs * shape) >> 15;
	}

	// ---- The mix ------------------------------------------------------------

	void UpdateMix(EngineParams &p)
	{
		// Dry against the loop: an equal-power crossfade, so the overall
		// loudness holds steady as it turns. A quarter turn of a sine wave:
		// dry follows cos, wet follows sin. (Phase 2^30 is a quarter cycle.)
		uint32_t phase = (uint32_t)kDelayMix << 18;
		p.dryGain = Sine(phase + (1u << 30)) >> 3;
		p.wetGain = ((Sine(phase) >> 3) * kWetLevelQ12) >> 12;
		// Burning (loop gain past 1), trim the wet level by the square root
		// of the excess: 2^(-log2(burn) / 2).
		if (kBurnTrim && burnQ12_ > 4096)
			p.wetGain = (p.wetGain * (Exp2(-(Log2((uint32_t)burnQ12_) - 12 * 4096) / 2) >> 4)) >> 12;
	}

	// ---- Water --------------------------------------------------------------

	// Y (plus CV In 2): centre open, left drowns (the lowpass closes), right
	// thins (the low-cut rises). Each sweeps evenly in pitch, so the knob
	// feels even.
	//
	// No clicks crossing the centre: each filter fades in from nothing over
	// the first part of its side (kWaterFadeIn), and core 0 only swaps one
	// filter for the other once the fade is back at nothing. (Swapping a
	// working filter straight out drops whatever it was holding back -- a
	// low-cut near the centre is still taking out a good part of the bass --
	// in a single sample, and that's a click.) Going straight from one side
	// to the other, the fade passes through nothing for a millisecond.
	void UpdateWater(const Status &s, EngineParams &p)
	{
		int32_t y = waterKnob_.Value() + s.cv[1] * 2;
		Lag(water_, y < 0 ? 0 : (y > 4095 ? 4095 : y), kKnobLagMs);
		y = (water_ + 128) >> 8;
		constexpr int32_t kSpan = 2048 - kWaterDeadZone;
		int32_t mode = kWaterOpen, amount = 0;
		if (y < 2048 - kWaterDeadZone)
		{
			mode = kWaterLowpass;
			amount = (2048 - kWaterDeadZone - y) * 4096 / kSpan; // Q12
		}
		else if (y > 2048 + kWaterDeadZone)
		{
			mode = kWaterHighpass;
			amount = (y - 2048 - kWaterDeadZone) * 4096 / (kSpan - 1);
			if (amount > 4096)
				amount = 4096;
		}
		// From one side straight to the other: through open first.
		if (mode != kWaterOpen && p.waterMode != kWaterOpen && mode != p.waterMode)
			mode = kWaterOpen;

		p.waterMode = mode;
		p.waterMixQ12 = 0;
		if (mode == kWaterLowpass)
			p.waterCoef = CutoffCoef(waterLp_[0] + (((waterLp_[1] - waterLp_[0]) * amount) >> 12));
		else if (mode == kWaterHighpass)
			p.waterCoef = CutoffCoef(waterHp_[0] + (((waterHp_[1] - waterHp_[0]) * amount) >> 12));
		if (mode != kWaterOpen)
		{
			int32_t fade = amount * 4096 / kWaterFadeInQ12;
			p.waterMixQ12 = fade > 4096 ? 4096 : fade;
		}
	}

	// ---- CV and Pulse Outs -----------------------------------------------------

	void UpdateOutputs(EngineParams &p)
	{
		const Morning &m = morning_;
		// CV Out 1, the tide: wandering around the tide's height; still air
		// wanders slowly and narrowly, wind makes it wider and choppier.
		int32_t span = kTideWanderMinQ12 + (((kTideWanderMaxQ12 - kTideWanderMinQ12) * m.wind) >> 12);
		int32_t tide = m.tide + ((tideWander_.Tick(rng_, LogBlend(kTideWanderMsStill, kTideWanderMsWindy, m.wind)) * span) >> 12);
		p.cv1 = Cv(tide);

		// CV Out 2: the Turing machine's note (1V per octave, calibrated on
		// core 0).
		p.turingNote = turingNote_;

		// Pulse Out 1: a trigger on every lead note. Pulse Out 2: the Turing
		// machine's trigger, on steps whose bit is 1.
		p.pulse[0] = leadTriggerMs_ > 0;
		if (leadTriggerMs_ > 0)
			leadTriggerMs_--;
		p.pulse[1] = turingTriggerMs_ > 0;
		if (turingTriggerMs_ > 0)
			turingTriggerMs_--;
	}

	// ---- The Turing machine -------------------------------------------------------

	// A clock pulse: the Turing machine steps, its note goes to CV Out 2 in
	// the current key, and Pulse Out 2 fires if the bit under the playhead
	// is a 1. It keeps running through a reseed (it's for patching, not the
	// card's own voices); the new key is heard from its next step.
	void StepTuring()
	{
		turing_.Step(rng_, turingKnob_.Value());
		turingNote_ = turing_.Note(root_, scale_);
		turingSteps_++;
		if (turing_.Gate())
			turingTriggerMs_ = kTriggerMs;
	}

	// 0..4096 as 0 to about +5V, in ComputerCard's CVOutPrecise units (DAC
	// counts x 128, so the voltage moves without visible steps).
	static int32_t Cv(int32_t level)
	{
		level = level < 0 ? 0 : (level > 4096 ? 4096 : level);
		return (level * kCvMax) >> 5;
	}

	// ---- LEDs -------------------------------------------------------------

	void UpdateLeds(int32_t led[6])
	{
		if (lastSwitch_ == kSwitchUp)
		{
			// Levels. Top row, the Turing machine: how locked it is, on the
			// left for the pendulum side, on the right for forwards (both
			// dark at the centre: fully random). Then a row each for the
			// melody and drone levels.
			int32_t knob = turingKnob_.Value();
			int32_t locked = 4096 - 2 * TuringMachine::FlipChance(knob);
			led[0] = knob < 2048 ? Gamma(locked) : 0;
			led[1] = knob >= 2048 ? Gamma(locked) : 0;
			led[2] = led[3] = Gamma(melody_.Value());
			led[4] = led[5] = Gamma(droneLevel_.Value());
			return;
		}
		Meter(morning_.sun, led);
		// The sunrise swoop lights them all, briefly.
		if (swoopMs_ > 0)
		{
			int32_t t = swoopMs_ < kSwoopRiseMs ? swoopMs_ : kSwoopRiseMs + kSwoopFallMs - swoopMs_;
			int32_t glow = Gamma(t * 4096 / kSwoopRiseMs > 4096 ? 4096 : t * 4096 / kSwoopRiseMs);
			for (int i = 0; i < 6; i++)
				led[i] = led[i] > glow ? led[i] : glow;
		}
		// A reseed flickers the lit LEDs.
		if (flickerMs_ > 0)
		{
			flickerMs_--;
			if ((flickerMs_ / 50) & 1)
				for (int i = 0; i < 6; i++)
					led[i] >>= 3;
		}
	}

	// Six LEDs as a bar, bottom row first, each fading in as the level
	// passes through its sixth of the range. (From Equanimity.)
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

	Random rng_;
	uint32_t (*entropy_)() = nullptr;
	bool started_ = false;

	int32_t timeQ8_ = 0; // table position x 256, lagged (x 256 again)
	Morning morning_;

	int32_t lastSwitch_ = kSwitchMiddle;
	// Switch middle (Play): Time, Sun, Water. Switch up (Levels): delay
	// mix, melody level, drone level. Each keeps its value while the knob
	// does the other job.
	Hook time_{kTimeStart}, sunKnob_{kSunStart}, waterKnob_{kWaterStart};
	Hook turingKnob_{kTuringStart}, melody_{kMelodyLevelStart}, droneLevel_{kDroneLevelStart};
	PulseIn pulseIn_[2];

	int32_t root_ = 0, scale_ = 0;
	VoiceControl voices_[kVoices];
	Loops loops_;
	Lead lead_;
	Drone drone_;
	int32_t droneCoef_ = 32767;
	int32_t leadTriggerMs_ = 0;

	static constexpr int32_t kSettled = 0, kFading = 1;
	int32_t reseed_ = kSettled;
	int32_t reseedMs_ = 0;
	uint32_t reseeds_ = 0;
	int32_t feedbackScale_ = 4096; // Q12
	int32_t flickerMs_ = 0;
	int32_t voiceCutoff_[3] = {};
	int32_t ladderCutoff_[3] = {};
	int32_t delayLog_[3] = {};
	int32_t waterLp_[2] = {}, waterHp_[2] = {};
	int32_t water_ = 2048 << 8; // Y, lagged (x 256)
	int32_t delayQ8_ = (kDelaySunriseMs * 24) << 8;
	int32_t sun_ = 2048 << 8; // X plus CV In 2, lagged (x 256)
	int32_t burnQ12_ = 4096;
	int32_t destructMs_ = 0;
	bool swoopArmed_ = false;
	int32_t swoopMs_ = 0; // ms into the swoop, 0 when not swooping
	Wander tideWander_;
	TuringMachine turing_;
	int32_t turingNote_ = 60; // MIDI: 60 is 0V
	int32_t turingTriggerMs_ = 0;
	uint32_t turingSteps_ = 0;
};

} // namespace constancy
