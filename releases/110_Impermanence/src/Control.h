#pragma once
#include <cstdint>
#include "Params.h"
#include "Hook.h"
#include "Random.h"

namespace imp
{

// Switch positions, numbered the same as ComputerCard's Switch enum.
constexpr int32_t kSwitchDown = 0;
constexpr int32_t kSwitchMiddle = 1;
constexpr int32_t kSwitchUp = 2;

// What core 0 reports to core 1: the latest panel readings and the state
// of the audio side. Core 0 writes these every sample; core 1 reads them.
struct Status
{
	uint32_t samples;		 // samples since power-on: core 1's clock
	int32_t knob[3];		 // Main, X, Y: 0..4095
	int32_t sw;				 // kSwitchDown / Middle / Up
	int32_t cv[2];
	int32_t clockPeriod;	 // Pulse In 2 period, samples
	bool clockActive;		 // a clock has been seen (its tempo is held after unplugging)
	int32_t takeLength;		 // length of the latest finished take
	bool recording;
	uint32_t cycles[2];		 // end-of-cycle counts: Pulse Out 1 (L1), Pulse Out 2 (L3)
	int32_t envelope;		 // CV Out 2 level
};

// Everything that runs at control rate, on core 1: the knobs and switch,
// catch-up, deciding when Chaos has moved enough to re-cut, making slice
// maps, turning settings into the per-grain numbers the audio engine
// needs, CV Out 1's wander, and the LEDs.
//
// Controls:
//   Switch Middle (and Down)  Main = Chaos, X = reverb decay, Y = wet/dry
//   Switch Up                 Main / X / Y = Level of layers 1 / 2 / 3
//   Tap Down                  toggle Texture <-> Rhythm (LED 1 on = Rhythm)
//   Main fully anticlockwise  clear the buffer; turning it back up records
class Control
{
public:
	void Seed(uint32_t seed)
	{
		rng_.Seed(seed);
		wanderRng_.Seed(seed * 2654435761u + 1);
	}

	// Plain loop to start with, until the knobs have settled.
	void InitMaps(SliceMap *maps)
	{
		SliceMap &m = maps[0];
		m.exp = 0;
		m.fadeQ12 = kMinFadeQ12;
		m.decayQ12 = 4096;
		for (int l = 0; l < kLayers; l++)
			for (int i = 0; i < kMaxSlots; i++)
				m.slot[l][i] = Slot{(uint8_t)i, 0, 0, 0};
	}

	// One control update. `current` is the parameter block core 0 is
	// playing; `next` is filled in for core 1 to publish. `maps` may be
	// written only at index 1 - current.map.
	void Tick(const Status &s, const EngineParams &current, EngineParams &next, SliceMap *maps, int32_t led[6])
	{
		uint32_t elapsed = s.samples - lastSamples_;
		lastSamples_ = s.samples;
		if (elapsed > 64)
			elapsed = 64;

		next = current;
		if (!started_)
		{
			// The Computer smooths knob and switch readings, which start at
			// zero and take ~10ms to settle after power-on. Wait 100ms
			// before trusting them.
			if (s.samples < kSettleSamples)
			{
				Publish(s, next, maps[next.map], elapsed);
				LedsStartup(s, led);
				return;
			}
			Start(s, next, maps);
		}

		UpdateControls(s, next);
		UpdateChaos(s, next, maps);
		Publish(s, next, maps[next.map], elapsed);
		UpdateLeds(s, next, led);
	}

	// Slice maps made since power-on (for tests).
	uint32_t Reseeds() const { return reseeds_; }

private:
	static constexpr int32_t kPagePerform = 0; // Middle and Down
	static constexpr int32_t kPageLevels = 1;  // Up

	static constexpr uint32_t kSettleSamples = 4800; // 100ms
	static constexpr int32_t kChaosDeadZone = 64;	 // movement needed to re-cut
	static constexpr uint32_t kReseedHoldoff = 960;	 // at most one re-cut per 20ms
	static constexpr uint32_t kFlashSamples = 2880;	 // 60ms LED flashes
	static constexpr int32_t kMinFadeQ12 = 512;
	static constexpr int32_t kMinFade = 192; // 4ms: every grain fades in and out at least this long

	// Main below kClearEnter (knobs bottom out ~14) clears the buffer; back
	// above kClearExit records a new take. The gap stops knob jitter at the
	// bottom of travel from clearing and recording over and over.
	static constexpr int32_t kClearEnter = 40;
	static constexpr int32_t kClearExit = 120;

	// Rhythm-mode slot counts for 4, 8 and 16 slices. Roughly 1 : 3/4 : 2/3,
	// so layer 1 and layer 2 cycle as a 4-against-3 polyrhythm.
	static constexpr int32_t kRhythmCounts[3][kLayers] = {{4, 3, 3}, {8, 6, 5}, {16, 12, 11}};

	static int32_t PageFor(int32_t sw) { return sw == kSwitchUp ? kPageLevels : kPagePerform; }
	static int32_t Clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

	// Knobs bottom out around 14, not 0. Treat the lowest sliver of travel
	// as zero so fully-down really means silent (levels), calm (chaos) or
	// dry (wet/dry).
	static int32_t LevelGain(int32_t knob) { return Clamp(((knob - 40) * 4137) >> 12, 0, 4096); }
	static int32_t KnobAmount(int32_t knob) { return Clamp(((knob - 40) * 4136) >> 12, 0, 4095); }

	void Start(const Status &s, EngineParams &p, SliceMap *maps)
	{
		started_ = true;
		page_ = PageFor(s.sw);
		lastSwitch_ = s.sw;
		Hook *h = page_ == kPageLevels ? levels_ : perf_;
		for (int k = 0; k < 3; k++)
			h[k].Grab(s.knob[k]);
		// Starting at the bottom arms the clear gesture without firing it.
		inClearZone_ = perf_[0].Value() < kClearEnter;
		// The starting loop reflects wherever Chaos is set at power-on.
		anchor_ = EffectiveChaos(s);
		NewMap(s, p, maps, KnobAmount(anchor_));
	}

	// The Chaos knob, plus CV1 in Texture mode. In Rhythm mode, and while
	// the switch is Up (where Main is Level 1), CV1 is ignored.
	int32_t EffectiveChaos(const Status &s) const
	{
		int32_t c = perf_[0].Value();
		if (page_ == kPagePerform && !rhythm_)
			c += s.cv[0] * 2; // +5V covers most of the knob's range
		return Clamp(c, 0, 4095);
	}

	void UpdateControls(const Status &s, EngineParams &p)
	{
		bool rebase = false;

		// Up <-> Middle: the knobs swap jobs, so unhook the new page's
		// parameters until each knob catches up with its stored value.
		int32_t page = PageFor(s.sw);
		if (page != page_)
		{
			page_ = page;
			Hook *h = page_ == kPageLevels ? levels_ : perf_;
			for (int k = 0; k < 3; k++)
				h[k].Unhook(s.knob[k]);
			rebase = true;
		}

		// A tap on Down (the momentary position) toggles Texture/Rhythm.
		// Down shares Middle's knob jobs, so no catch-up is needed.
		if (s.sw == kSwitchDown && lastSwitch_ != kSwitchDown)
		{
			rhythm_ = !rhythm_;
			rebase = true;
		}
		lastSwitch_ = s.sw;
		p.rhythm = rhythm_;

		Hook *h = page_ == kPageLevels ? levels_ : perf_;
		for (int k = 0; k < 3; k++)
			h[k].Update(s.knob[k]);

		// Changing page or mode switches CV1 in or out of the chaos sum.
		// That's not a gesture, so it mustn't re-cut: move the anchor.
		if (rebase)
			anchor_ = EffectiveChaos(s);

		// Main fully anticlockwise: clear the buffer (core 0 fades the loop
		// out first). Turning it back up past the bottom records a new
		// 0.75-second take. Only on the Middle/Down page, only once Main has
		// caught up, and never from CV1 -- it's a hand gesture.
		if (page_ == kPagePerform && perf_[0].Hooked())
		{
			int32_t v = perf_[0].Value();
			if (!inClearZone_ && v < kClearEnter)
			{
				inClearZone_ = true;
				p.clearSeq++;
			}
			else if (inClearZone_ && v > kClearExit)
			{
				inClearZone_ = false;
				p.recordSeq++;
			}
		}
	}

	// The heart of the card: a new slice map only when the effective chaos
	// (knob + CV1) has moved far enough from where the last one was made.
	// Holding still holds the loop; any real movement destroys it.
	void UpdateChaos(const Status &s, EngineParams &p, SliceMap *maps)
	{
		int32_t chaos = EffectiveChaos(s);
		int32_t moved = chaos - anchor_;
		bool far = moved >= kChaosDeadZone || moved <= -kChaosDeadZone;
		if (far && s.samples - lastReseed_ >= kReseedHoldoff)
		{
			anchor_ = chaos;
			NewMap(s, p, maps, KnobAmount(chaos));
		}
	}

	// Make a whole new slice map in the slot core 0 isn't using, and point
	// the next parameter block at it.
	void NewMap(const Status &s, EngineParams &p, SliceMap *maps, int32_t c)
	{
		int target = 1 - p.map;
		SliceMap &m = maps[target];
		int32_t maxExp = (c * 5) >> 12; // 0..4 -> 1..16 slices
		int32_t exp = maxExp;
		if (exp > 0 && rng_.Chance(1365)) // 1 in 3: one step fewer slices
			exp--;
		m.exp = (uint8_t)exp;
		// Texture crossfade: 1/8 of a slot at low chaos (close to the plain
		// loop), up to 1/2 at high chaos (smeared, overlapping grains).
		m.fadeQ12 = kMinFadeQ12 + ((c * 1536) >> 12);
		// Rhythm hit length: the whole pulse at low chaos, down to 30% --
		// clipped, percussive hits -- at high chaos.
		m.decayQ12 = 4096 - ((c * 2867) >> 12);

		for (int l = 0; l < kLayers; l++)
		{
			// Each playhead as a whole may run backwards (up to 1 in 3 at
			// full chaos); individual slices can flip again on top of that.
			uint8_t layerReverse = rng_.Chance(c / 3) ? 1 : 0;
			for (int i = 0; i < kMaxSlots; i++)
			{
				Slot &slot = m.slot[l][i];
				// Reorder: with probability chaos, play any slice here.
				slot.src = (uint8_t)(rng_.Chance(c) ? rng_.Below(kMaxSlots) : i);
				// Reverse: up to a 50% chance at full chaos.
				slot.reverse = (rng_.Chance(c >> 1) ? 1 : 0) ^ layerReverse;
				// Pitch scatter, whole octaves only: up to 75% of slots,
				// reaching further as chaos rises -- first an octave up,
				// then an octave either way, then two octaves either way.
				slot.pitch = 0;
				if (rng_.Chance((c * 3) >> 2))
				{
					int32_t depth = 1 + ((c * 4) >> 12);
					if (depth > 4)
						depth = 4;
					slot.pitch = (uint8_t)(1 + rng_.Below(depth));
				}
				// Start jitter grows with chaos squared: none near zero, up
				// to a quarter of a slice at full chaos.
				int32_t maxJitter = (c * c) >> 16;
				slot.jitter = (int16_t)(maxJitter ? rng_.Below(2 * maxJitter + 1) - maxJitter : 0);
			}
		}
		p.map = target;
		p.reseedSeq++;
		lastReseed_ = s.samples;
		reseedFlashUntil_ = s.samples + kFlashSamples;
		reseeds_++;
	}

	// Turn the settings into the numbers core 0 plays from. All the
	// divisions on the card happen here, never in the audio interrupt.
	void Publish(const Status &s, EngineParams &p, const SliceMap &m, uint32_t elapsed)
	{
		int32_t n = s.takeLength;
		p.recLength = n;

		// Slot speed: with a clock on Pulse In 2, layer 1's loop lasts a whole
		// power-of-two number of clock periods (1, 2, 4... or 1/2, 1/4),
		// whichever needs the smallest change, gliding rather than jumping.
		// Only the timing follows the clock -- the playheads keep reading at
		// their own rate, so the pitch doesn't move with the tempo.
		int32_t target = 1 << 16; // Q16 unity
		if (s.clockActive)
		{
			int32_t lo = (n * 181) >> 8; // 0.707 x loop length
			int32_t hi = (n * 362) >> 8; // 1.414 x loop length
			int32_t kp = s.clockPeriod;
			while (kp < lo)
				kp <<= 1;
			while (kp >= hi && kp > 1)
				kp >>= 1;
			target = Clamp((int32_t)(((uint32_t)n << 16) / (uint32_t)kp), 1 << 15, 1 << 17);
		}
		// ~40ms glide: close 1/2048 of the gap per sample.
		speedState_ += ((target - (speedState_ >> 11)) * (int32_t)elapsed);
		p.slotSpeed = speedState_ >> 11;
		p.readRate = 4096; // unity: the clock never re-pitches the audio

		// Layer geometry. Texture: layers loop over the whole take, its
		// last 3/4 and its last 2/3 -- three loops that phase.
		int32_t regionLen[kLayers] = {n, (n * 3) >> 2, (int32_t)(((uint32_t)n * 43691u) >> 16)};
		int32_t regionStart[kLayers] = {0, n >> 2, (int32_t)(((uint32_t)n * 21845u) >> 16)};
		int32_t rhythmExp = m.exp < 2 ? 2 : m.exp;
		for (int l = 0; l < kLayers; l++)
		{
			LayerParams &lp = p.layer[l];
			lp.regionStart = regionStart[l];
			if (p.rhythm)
			{
				lp.count = kRhythmCounts[rhythmExp - 2][l];
				lp.sliceLen = n >> rhythmExp;
				lp.srcMask = (1 << rhythmExp) - 1;
			}
			else
			{
				lp.count = 1 << m.exp;
				lp.sliceLen = regionLen[l] >> m.exp;
				lp.srcMask = (1 << m.exp) - 1;
			}
			if (lp.sliceLen < 64)
				lp.sliceLen = 64;
			lp.slotQ = (uint32_t)lp.sliceLen << 16;
			lp.level = LevelGain(levels_[l].Value());

			// Texture grain: fade in over `fade`, hold to the end of the
			// slot (at the current speed), then fade out over `fade` while
			// the next grain fades in.
			int32_t slotOut = (int32_t)(lp.slotQ / (uint32_t)p.slotSpeed);
			int32_t fade = (slotOut * m.fadeQ12) >> 12;
			if (fade < kMinFade)
				fade = kMinFade;
			lp.texHold = slotOut;
			lp.texAttackStep = 65536 / fade + 1;
			lp.texReleaseStep = lp.texAttackStep;
		}
		p.internalPeriod = n >> rhythmExp;
		if (p.recLength != published_.recLength || p.map != published_.map || p.rhythm != published_.rhythm ||
			p.layer[0].count != published_.layer[0].count || p.layer[1].count != published_.layer[1].count ||
			p.layer[2].count != published_.layer[2].count)
		{
			p.epoch++;
			// The slot tables depend only on these, so they're rebuilt here
			// rather than every tick.
			for (int l = 0; l < kLayers; l++)
			{
				LayerParams &lp = p.layer[l];
				for (int i = 0; i < kMaxSlots; i++)
				{
					const Slot &slot = m.slot[l][i];
					int32_t start = lp.regionStart + (slot.src & lp.srcMask) * lp.sliceLen +
									((lp.sliceLen * slot.jitter) >> 10);
					int32_t pitch = kPitch[slot.pitch];
					if (slot.reverse)
					{
						start += lp.sliceLen - 1; // begin at the slice's end
						pitch = -pitch;
					}
					while (start >= n)
						start -= n;
					while (start < 0)
						start += n;
					lp.slotStart[i] = start;
					lp.slotPitch[i] = pitch;
				}
			}
			published_.recLength = p.recLength;
			published_.map = p.map;
			published_.rhythm = p.rhythm;
			for (int l = 0; l < kLayers; l++)
				published_.layer[l].count = p.layer[l].count;
		}

		// Rhythm hit: a short fade in (4ms, or a quarter of the period if
		// the clock is very fast), then a straight decay lasting a fraction
		// of the clock period (shorter at high chaos).
		int32_t period = s.clockActive ? s.clockPeriod : p.internalPeriod;
		int32_t attack = period >> 2;
		if (attack > kMinFade)
			attack = kMinFade;
		int32_t decay = (int32_t)(((uint32_t)period * (uint32_t)m.decayQ12) >> 12);
		int32_t release = decay - attack < kMinFade ? kMinFade : decay - attack;
		p.rhyHold = attack;
		p.rhyAttackStep = 65536 / attack + 1;
		p.rhyReleaseStep = 65536 / release + 1;

		// Reverb and CV Out 1.
		p.decay = 8192 + ((perf_[1].Value() * 20480) >> 12); // 0.25..0.875
		// Wet/dry holds its setting in every switch position -- including Up,
		// so the reverb carries on while you set the levels -- and CV2 always
		// applies (+5V moves wet/dry most of its range).
		int32_t mix = KnobAmount(perf_[2].Value()) + s.cv[1] * 2;
		p.mix = Clamp(mix, 0, 4095);
		p.wander = Wander(elapsed);
	}

	// CV Out 1: every 1-5 seconds pick a new random level (0 to ~+5V) and
	// glide towards it through two smoothing stages, so the voltage moves
	// in soft S-curves with no corners.
	int32_t Wander(uint32_t elapsed)
	{
		wanderCountdown_ -= (int32_t)elapsed;
		if (wanderCountdown_ <= 0)
		{
			wanderTarget_ = wanderRng_.Below(1701) << 16;
			wanderCountdown_ = 48000 + wanderRng_.Below(4096) * 47;
		}
		for (uint32_t i = 0; i < elapsed; i++)
		{
			wander1_ += (wanderTarget_ - wander1_) >> 15; // ~0.7s per stage
			wander2_ += (wander1_ - wander2_) >> 15;
		}
		return wander2_ >> 16;
	}

	void UpdateLeds(const Status &s, const EngineParams &p, int32_t led[6])
	{
		for (int i = 0; i < 2; i++)
		{
			if (s.cycles[i] != lastCycles_[i])
			{
				lastCycles_[i] = s.cycles[i];
				cycleFlashUntil_[i] = s.samples + kFlashSamples;
			}
		}
		bool reseedFlash = (int32_t)(reseedFlashUntil_ - s.samples) > 0;
		led[0] = (s.recording || reseedFlash) ? 4095 : 0;
		led[1] = rhythm_ ? 4095 : 0;
		led[2] = (int32_t)(cycleFlashUntil_[0] - s.samples) > 0 ? 4095 : 0;
		led[3] = (int32_t)(cycleFlashUntil_[1] - s.samples) > 0 ? 4095 : 0;

		// LEDs 4/5 normally show the two CV outs. While a knob on this page
		// is still catching up, they blink alternately instead.
		const Hook *h = page_ == kPageLevels ? levels_ : perf_;
		bool catching = !h[0].Hooked() || !h[1].Hooked() || !h[2].Hooked();
		if (catching)
		{
			bool phase = (s.samples & 16383) < 8192; // ~3Hz
			led[4] = phase ? 4095 : 0;
			led[5] = phase ? 0 : 4095;
		}
		else
		{
			led[4] = Clamp(p.wander * 2, 0, 4095);
			led[5] = Clamp(s.envelope * 2, 0, 4095);
		}
	}

	void LedsStartup(const Status &s, int32_t led[6])
	{
		for (int i = 0; i < 6; i++)
			led[i] = 0;
		led[0] = s.recording ? 4095 : 0;
	}

	Random rng_, wanderRng_;
	bool started_ = false;
	uint32_t lastSamples_ = 0;

	int32_t page_ = kPagePerform;
	int32_t lastSwitch_ = kSwitchMiddle;
	bool rhythm_ = false;
	Hook perf_[3] = {Hook(0), Hook(2048), Hook(0)};		 // chaos, decay, wet
	Hook levels_[3] = {Hook(3500), Hook(2400), Hook(1600)}; // layer levels

	int32_t anchor_ = 0; // effective chaos at the last re-cut
	bool inClearZone_ = false;
	uint32_t lastReseed_ = 0;
	uint32_t reseeds_ = 0;
	uint32_t reseedFlashUntil_ = 0;
	uint32_t lastCycles_[2] = {0, 0};
	uint32_t cycleFlashUntil_[2] = {0, 0};

	int32_t speedState_ = (1 << 16) << 11; // Q16 slot speed, 11 more bits for the glide
	EngineParams published_; // structural fields as of the last epoch bump

	int32_t wanderTarget_ = 0;
	int32_t wanderCountdown_ = 0;
	int32_t wander1_ = 0, wander2_ = 0; // Q16
};

} // namespace imp
