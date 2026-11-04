#pragma once
#include <cstdint>
#include "Hook.h"
#include "Looper.h"
#include "Params.h"

namespace tem
{

// Everything on the panel that isn't audio, once a millisecond on core 1:
// the knobs on their two layers, the switch, the CV ins, the Wash macro,
// the dry level, the reverb's LFOs, the random CV outs and triggers, and the
// LEDs.
//
//   Play (switch middle)  Main = clock, X = Loop A speed, Y = Loop B speed.
//   Alt  (switch up)      Main = wash, X = dry level, Y = crush.
//   Down (a tap)          start recording both loops; tap again to play.
//
// Each setting keeps its value while its knob does the other job, and
// waits for the knob to come back to it before following (pickup), so
// nothing jumps when the switch moves.
class Ui
{
public:
	// One update. `p` is the parameter block core 0 will play next; it
	// starts as a copy of the one core 0 is playing now.
	void Tick(const Status &s, EngineParams &p, int32_t led[6])
	{
		for (int i = 0; i < 6; i++)
			led[i] = 0;
		if (!started_)
		{
			if (s.samples < kSettleSamples)
				return;
			Start(s, p);
		}
		ms_++;

		UpdateSwitch(s, p);
		for (int k = 0; k < 3; k++)
			hook_[layer_][k].Update(s.knob[k]);
		UpdatePlay(s, p);
		UpdateAlt(s, p);
		UpdateLfos(p);
		UpdateRandom(s, p);
		DrawLeds(s, p, led);
	}

	void Seed(uint32_t seed) { rng_ = seed ? seed : 1; }

	int32_t Wash() const { return wash_; } // 0..4095, knob plus CV in 2

private:
	static constexpr int32_t kPlayLayer = 0, kAltLayer = 1;

	// ---- Start and switch --------------------------------------------------

	void Start(const Status &s, EngineParams &p)
	{
		started_ = true;
		p.running = true;
		lastSwitch_ = s.sw;
		layer_ = s.sw == kSwitchUp ? kAltLayer : kPlayLayer;
		// The layer the switch starts on takes its knobs as they are; the
		// other layer's settings wait at their defaults for pickup.
		for (int k = 0; k < 3; k++)
			hook_[layer_][k].Grab(s.knob[k]);
		mainAnchor_ = s.knob[0];
		UpdatePlay(s, p);
		UpdateAlt(s, p);
		nextPulseMs_ = NextPulseGap();
	}

	void UpdateSwitch(const Status &s, EngineParams &p)
	{
		if (s.sw == lastSwitch_)
			return;
		// A tap: Down from anywhere. Taps closer than 50ms are the switch
		// bouncing, not a finger.
		if (s.sw == kSwitchDown && ms_ - lastTapMs_ >= kBounceMs)
		{
			lastTapMs_ = ms_;
			p.tapSerial++;
		}
		// Down is a tap on the Play layer; only up changes layer.
		int32_t layer = s.sw == kSwitchUp ? kAltLayer : kPlayLayer;
		if (layer != layer_)
		{
			layer_ = layer;
			for (int k = 0; k < 3; k++)
				hook_[layer_][k].Unhook(s.knob[k]);
			clockViewUntil_ = 0;
			mainAnchor_ = s.knob[0];
		}
		lastSwitch_ = s.sw;
	}

	// ---- The Play layer ----------------------------------------------------

	void UpdatePlay(const Status &s, EngineParams &p)
	{
		// Clock, Main: ten zones, 2kHz fully left to 48kHz fully right.
		static constexpr int32_t kClockEdges[] = {410, 819, 1229, 1638, 2048, 2458, 2867, 3277, 3686};
		clockZone_ = Zone(hook_[kPlayLayer][0].Value(), clockZone_, kClockEdges, 9, kClockMargin);

		// CV in 1 moves the clock a step per volt, +1V one step faster.
		// Smoothed (8ms) and with hysteresis: it changes step three
		// quarters of a volt past the last one, so a CV resting on a
		// boundary doesn't flicker between two clocks. (The CV ins have
		// about 9 bits of real resolution.) The smoothed CV is kept x 16.
		cvSmooth_ += (s.cv[0] * 16 - cvSmooth_) >> 3;
		int32_t cv = cvSmooth_ >> 4;
		int32_t dist = cv - cvSteps_ * kCountsPerVolt;
		if (dist > kCvHysteresis || dist < -kCvHysteresis)
			cvSteps_ = (cv + (cv >= 0 ? kCountsPerVolt / 2 : -kCountsPerVolt / 2)) / kCountsPerVolt;
		int32_t step = (kClockSteps - 1 - clockZone_) - cvSteps_;
		p.clockStep = step < 0 ? 0 : (step > kClockSteps - 1 ? kClockSteps - 1 : step);

		// Turning Main shows the clock on the LEDs for 1.5s.
		if (layer_ == kPlayLayer)
		{
			int32_t d = s.knob[0] - mainAnchor_;
			if (d > kMoved || d < -kMoved)
			{
				mainAnchor_ = s.knob[0];
				if (started_ && ms_ > kBootMs)
					clockViewUntil_ = ms_ + kClockViewMs;
			}
		}

		// Speed, X and Y: six zones each, with about 3% hysteresis.
		static constexpr int32_t kSpeedEdges[] = {683, 1365, 2048, 2731, 3413};
		zone_[0] = Zone(hook_[kPlayLayer][1].Value(), zone_[0], kSpeedEdges, 5, kSpeedMargin);
		zone_[1] = Zone(hook_[kPlayLayer][2].Value(), zone_[1], kSpeedEdges, 5, kSpeedMargin);
		p.zone[0] = zone_[0];
		p.zone[1] = zone_[1];
	}

	// ---- The Alt layer -----------------------------------------------------

	void UpdateAlt(const Status &s, EngineParams &p)
	{
		// Wash, Main plus CV in 2 (+5V is the whole knob).
		int32_t w = hook_[kAltLayer][0].Value() + s.cv[1] * 4095 / 1707;
		wash_ = w < 0 ? 0 : (w > 4095 ? 4095 : w);
		SetWash(wash_, p);

		// Dry level, X: the clean input under the loops, from none (loops
		// only) fully left to unity fully right.
		int32_t x = hook_[kAltLayer][1].Value();
		p.dryQ12 = x >= 4095 ? 4096 : x;

		// Crush, Y: 12 bits (clean) fully left, down to 4 fully right.
		static constexpr int32_t kCrushEdges[] = {455, 910, 1365, 1820, 2275, 2731, 3186, 3641};
		crushZone_ = Zone(hook_[kAltLayer][2].Value(), crushZone_, kCrushEdges, 8, kClockMargin);
		int32_t bits = 12 - crushZone_;
		if (kCrushFollowsClock)
			bits -= p.clockStep / 2;
		p.crushBits = bits < 4 ? 4 : bits;
	}

	// The Wash macro: one control for the reverb's send, decay and damping,
	// so more wash is longer and darker at once.
	//
	//   wash   send   decay   damping
	//     0%   0      0.50    0.20
	//    50%   0.5    0.80    0.40
	//   100%   1.0    0.93    0.60
	static void SetWash(int32_t w, EngineParams &p)
	{
		p.sendQ15 = w * 8;
		if (w <= 2048)
			p.decayQ15 = 16384 + w * 9830 / 2048; // 0.50 .. 0.80
		else
			p.decayQ15 = 26214 + (w - 2048) * 4260 / 2047; // 0.80 .. 0.93
		p.dampQ15 = 26214 - w * 13107 / 4095; // 1 - damping: 0.80 .. 0.40
	}

	// The tank's two LFOs: 0.3 and 0.37Hz, +-8 samples, a third of a turn
	// apart, so the two halves of the tank never drift in step.
	void UpdateLfos(EngineParams &p)
	{
		lfoPhase_[0] += kLfoInc[0];
		lfoPhase_[1] += kLfoInc[1];
		p.lfoQ8[0] = Sine(lfoPhase_[0]) >> 4; // Q15 / 16: +-2048, 8 samples in 1/256ths
		p.lfoQ8[1] = Sine(lfoPhase_[1] + 0x55555555u) >> 4;
	}

	// One turn is 2^32; a millisecond at 0.3 and 0.37Hz.
	static constexpr uint32_t kLfoInc[2] = {1288490, 1589137};

	// A sine (Q15) from a phase (2^32 a turn), in integers: a parabola,
	// 4x(1 - |x|), then one refining step that brings it within 0.1% of a
	// true sine -- plenty for an LFO. (No floats: on the RP2040 sinf goes
	// through the bootrom's float tables, which the cycle bench's emulator
	// doesn't have, and core 1 doesn't need them.)
	static int32_t Sine(uint32_t phase)
	{
		int32_t x = (int32_t)(phase >> 16) - 32768; // -1..1, Q15
		int32_t ax = x < 0 ? -x : x;
		int32_t y = (4 * x * (32768 - ax)) >> 15;
		y = y > 32767 ? 32767 : y;
		int32_t ay = y < 0 ? -y : y;
		return y + ((7373 * (((y * ay) >> 15) - y)) >> 15); // 7373 = 0.225 x 32768
	}

	// ---- The random CV outs and Pulse out 1 --------------------------------
	//
	// CV out 1: a stepped random voltage, tied to Loop A. At each start of
	// the loop it jumps to a new whole volt between -2 and +2V, never the
	// same one twice running, so every start is a move. Whole volts because
	// CV in 1 moves the clock a step per volt: patched back there, each
	// start of Loop A jumps both loops by a fifth, a fourth, an octave...
	// and since the clock sets how long the loop lasts, the jumps feed back
	// into their own timing.
	//
	// CV out 2: a slow random wander, tied to Loop B. At each start of the
	// loop it picks a new level between 0 and +5V, and eases there over one
	// pass, so a short loop breathes quickly and a long one slowly. Patched
	// into CV in 2, the wash comes and goes around the Wash knob.
	//
	// Pulse out 1: a trigger at random, 200ms to 2s after the last one, loop
	// or no loop.
	//
	// With no loop playing, its CV out holds its last voltage.
	void UpdateRandom(const Status &s, EngineParams &p)
	{
		const LoopStatus &a = s.loop[0];
		if (a.passes != passes_[0])
		{
			passes_[0] = a.passes;
			// One of the four other whole volts.
			int32_t v = (int32_t)(Next() % 4) - 2;
			if (v >= step_)
				v++;
			step_ = v;
		}
		p.cvMv[0] = step_ * 1000;

		const LoopStatus &b = s.loop[1];
		if (b.passes != passes_[1])
		{
			passes_[1] = b.passes;
			from_ = wander_;
			to_ = (int32_t)(Next() % (kWanderMv + 1));
			phase_ = 0;
		}
		if (b.state == Looper::kPlaying && b.len > 0 && phase_ < kPhaseEnd)
		{
			// Through the pass at its current pace -- the clock, the speed and
			// the loop's length, as they are now -- so turning the clock or
			// the speed mid-pass bends the glide rather than making it jump.
			// A pass takes len / |speed| ticks; the phase runs 0 to 2^24.
			// Every clock is a whole number of kHz, which keeps this in 32
			// bits (at most 48 x 131072 x 256, about 1.6 x 10^9) and one
			// hardware divide. A 64-bit divide is a long software routine on
			// the Cortex-M0+.
			int32_t speed = kZoneSpeed[p.zone[1]];
			uint32_t perMs = (uint32_t)(kClockHz[p.clockStep] / 1000) * (uint32_t)(speed < 0 ? -speed : speed) * 256u;
			phase_ += (int32_t)(perMs / (uint32_t)b.len);
			if (phase_ > kPhaseEnd)
				phase_ = kPhaseEnd;
		}
		// Smoothstep, t*t*(3-2t), so each glide sets off and arrives gently.
		int32_t t = (int32_t)(phase_ >> 12); // Q12
		int32_t ease = (((t * t) >> 12) * (12288 - 2 * t)) >> 13;
		wander_ = from_ + (((to_ - from_) * ease) >> 12);
		p.cvMv[1] = wander_;

		if ((int32_t)(ms_ - nextPulseMs_) >= 0)
		{
			p.pulseSerial++;
			nextPulseMs_ = ms_ + NextPulseGap();
		}
	}

	uint32_t NextPulseGap() { return 200 + Next() % 1801; } // 200ms to 2s

	// xorshift32: plenty random for music, and cheap.
	uint32_t Next()
	{
		rng_ ^= rng_ << 13;
		rng_ ^= rng_ >> 17;
		rng_ ^= rng_ << 5;
		return rng_;
	}

	static constexpr int32_t kWanderMv = 5000;
	static constexpr int32_t kPhaseEnd = 1 << 24;

	// Which zone a knob is in, with hysteresis: it has to go `margin` past
	// an edge to cross it, so a knob resting on an edge doesn't chatter
	// between two settings.
	static int32_t Zone(int32_t v, int32_t zone, const int32_t *edges, int32_t nEdges, int32_t margin)
	{
		while (zone < nEdges && v > edges[zone] + margin)
			zone++;
		while (zone > 0 && v < edges[zone - 1] - margin)
			zone--;
		return zone;
	}

	static constexpr int32_t kSpeedMargin = 123; // 3% of the knob
	static constexpr int32_t kClockMargin = 48;	 // knobs jitter by about +/-10
	static constexpr int32_t kMoved = 40;		 // a deliberate turn, not jitter
	static constexpr int32_t kCountsPerVolt = 341; // 2048 counts is about 6V
	static constexpr int32_t kCvHysteresis = 256;	 // three quarters of a volt
	static constexpr uint32_t kBounceMs = 50;

	// ---- The LEDs ----------------------------------------------------------
	//
	//   0 1    Loop A state     Loop B state
	//   2 3    Loop A speed     Loop B speed
	//   4 5    Loop A playhead  Loop B playhead
	//
	// Overlays, highest first: event flashes, the clock view (for 1.5s after
	// Main turns), the alt view (switch up), then the home view above.

	void DrawLeds(const Status &s, const EngineParams &p, int32_t led[6])
	{
		Events(s);
		if (ms_ <= kBootMs)
		{
			// Boot: one slow chase down the grid, 0 to 5.
			led[(ms_ - 1) * 6 / kBootMs] = 4095;
			return;
		}
		if (ms_ < clockViewUntil_)
			DrawClock(p, led);
		else if (layer_ == kAltLayer)
			DrawAlt(led);
		else
			DrawHome(s, led);

		for (int k = 0; k < 2; k++)
		{
			if (ms_ < flashUntil_[k])
				led[k] = 4095;
			if (ms_ < clipUntil_[k])
				led[4 + k] = 4095;
		}
	}

	void Events(const Status &s)
	{
		for (int k = 0; k < 2; k++)
		{
			const LoopStatus &l = s.loop[k];
			if (l.starts != starts_[k])
			{
				// Recording: the blink starts on, so the first flash marks the tap.
				starts_[k] = l.starts;
				blinkFrom_[k] = ms_;
				flashUntil_[k] = ms_ + kBlinkMs / 2;
			}
			if (l.stops != stops_[k])
			{
				stops_[k] = l.stops;
				flashUntil_[k] = ms_ + kStopFlashMs;
			}
			if (s.clips[k] != clips_[k])
			{
				clips_[k] = s.clips[k];
				clipUntil_[k] = ms_ + kClipFlashMs;
			}
			// The input's level, for the bottom row while a loop is empty or
			// recording: a peak that falls away over about 64ms.
			int32_t a = s.audio[k] < 0 ? -s.audio[k] : s.audio[k];
			level_[k] -= level_[k] >> 6;
			if (a * 16 > level_[k])
				level_[k] = a * 16;
		}
	}

	void DrawHome(const Status &s, int32_t led[6])
	{
		for (int k = 0; k < 2; k++)
		{
			const LoopStatus &l = s.loop[k];
			// Top: off when empty, a fast blink (6Hz) while recording, on
			// while playing.
			if (l.state == Looper::kRecording)
				led[k] = (ms_ - blinkFrom_[k]) % kBlinkMs < kBlinkMs / 2 ? 4095 : 0;
			else if (l.state == Looper::kPlaying)
				led[k] = kSolid;

			// Middle: the speed. 25% at x1/2, 60% at x1, full at x2.
			static constexpr int32_t kSpeedLed[kSpeedZones] = {4095, 2458, 1024, 1024, 2458, 4095};
			led[2 + k] = kSpeedLed[zone_[k]];

			// Bottom: the playhead, bright at the loop's start and fading
			// to its end. Forward, that's a flash and a decay (a falling
			// saw); in reverse the head runs from the end to the start, so
			// it's a ramp up and a snap off. Empty or recording: the input
			// level.
			if (l.state == Looper::kPlaying && l.len > 0)
				led[4 + k] = 4095 - l.pos * 4095 / l.len; // at most 40000 x 4095: fits in 32 bits
			else
			{
				int32_t v = level_[k] >> 2; // full at half scale (about 3V)
				led[4 + k] = v > 4095 ? 4095 : v;
			}
		}
	}

	// The clock: fills from the bottom row up, half an LED per step down
	// from 48kHz (one LED) to 2kHz (five and a half). Slower looks heavier.
	void DrawClock(const EngineParams &p, int32_t led[6])
	{
		static constexpr int32_t kOrder[6] = {4, 5, 2, 3, 0, 1};
		int32_t halves = p.clockStep + 2;
		for (int i = 0; i < 6; i++)
		{
			int32_t h = halves - 2 * i;
			led[kOrder[i]] = h >= 2 ? 4095 : (h == 1 ? kHalf : 0);
		}
	}

	// Switch up: each row is a two-LED bar for one knob -- wash, dry
	// level, crush -- the left LED covering 0 to 50%, the right 50 to 100%.
	// A row breathes (1Hz) until its knob picks up its stored value.
	void DrawAlt(int32_t led[6])
	{
		uint32_t t = ms_ % 1000;
		int32_t tri = (int32_t)(t < 500 ? t : 1000 - t); // 0..500
		int32_t breath = 1024 + tri * 6;				 // 1024..4024, of 4096
		for (int row = 0; row < 3; row++)
		{
			const Hook &h = hook_[kAltLayer][row];
			int32_t v = h.Value() * 2;
			int32_t left = v > 4095 ? 4095 : v;
			int32_t right = v > 4095 ? v - 4095 : 0;
			if (!h.Hooked())
			{
				// Never fully dark, so the breathing shows even at zero.
				left = ((left < kDim ? kDim : left) * breath) >> 12;
				right = (right * breath) >> 12;
			}
			led[row * 2] = left;
			led[row * 2 + 1] = right;
		}
	}

	// LedBrightness squares its value, so these look about as they read:
	// 2048 is about half as bright as 4095.
	static constexpr int32_t kHalf = 2048;
	static constexpr int32_t kSolid = 3000; // playing: on, but leaves room for the stop flash
	static constexpr int32_t kDim = 900;
	static constexpr uint32_t kBlinkMs = 166; // 6Hz
	static constexpr uint32_t kStopFlashMs = 80;
	static constexpr uint32_t kClipFlashMs = 100;
	static constexpr uint32_t kClockViewMs = 1500;
	static constexpr uint32_t kBootMs = 720; // 120ms an LED

	bool started_ = false;
	uint32_t ms_ = 0;
	int32_t lastSwitch_ = kSwitchMiddle;
	int32_t layer_ = kPlayLayer;
	uint32_t lastTapMs_ = 0;
	// hook_[layer][knob]: each layer's three settings, starting at their
	// defaults: 48kHz, both loops forward at half speed; a little wash, the
	// dry input at unity, no crush.
	Hook hook_[2][3] = {{Hook(4095), Hook(2389), Hook(2389)}, {Hook(1229), Hook(4095), Hook(0)}};
	int32_t clockZone_ = 0, zone_[2] = {0, 0}, crushZone_ = 0;
	int32_t cvSmooth_ = 0, cvSteps_ = 0;
	int32_t wash_ = 0;
	uint32_t lfoPhase_[2] = {0, 0};
	int32_t mainAnchor_ = 0;
	uint32_t clockViewUntil_ = 0;
	uint32_t starts_[2] = {0, 0}, stops_[2] = {0, 0}, clips_[2] = {0, 0};
	uint32_t blinkFrom_[2] = {0, 0}, flashUntil_[2] = {0, 0}, clipUntil_[2] = {0, 0};
	int32_t level_[2] = {0, 0};
	uint32_t rng_ = 1;
	uint32_t passes_[2] = {0, 0};
	int32_t step_ = 0; // CV out 1, in volts
	int32_t from_ = 0, to_ = 0, wander_ = 0; // CV out 2, millivolts
	int32_t phase_ = kPhaseEnd;				 // through Loop B's pass, 0 to 2^24
	uint32_t nextPulseMs_ = 0;
};

} // namespace tem
