// =============================================================================
//  qi_engine.h : the QiGong Timer, with no hardware dependency (testable on a PC).
//  AI-assisted (Claude / Anthropic).
//
//  Timer state machine, Z switch (taps, triple tap, hold), set-up knobs (notches, take-over) with LED display and
//  soft clicks, pulses, preview mode, CV progress ramps, LEDs.  The effects and the silence keeper are in qi_fx.h:
//  two independent reverb + delay chains, edited on three pages in Z middle (Main picks the page, X and Y edit it).
//
//  Timeline of a session (all counted in audio samples, so it is as accurate as the card's crystal):
//    tap Z down (acted on 0.4 s after release)  ->  10 s settling period (silent, LEDs count down)
//    -> Pulse 1, 0.6 s later Pulse 1 again (the "double chime": the session has started; interval 1 starts at the
//    first) -> interval 1 -> Pulse 1 -> ... -> interval N -> Pulse 2 (end).
//  Tap again = pause / resume (the settling period can be paused too).  Hold Z down 3 s = stop.
//  Triple tap while stopped = PREVIEW mode: chime, 1 s, bell, 1 s, ... so the modules can be tuned; a tap ends it.
//
//  TWO ENTRY POINTS (the official guidance: audio on core 0, UI on core 1):
//    Process()  - core 0, every sample (48 kHz): audio, effects, keeper, clicks, the timer, the Z switch, pulses.
//    Control()  - core 1, once every 32 samples (1.5 kHz), paced by core 0's sample count (SampleCount()): knobs,
//                 set-up, LEDs, effect pages and smoothing, reverb wander, keeper hum learning.
//  Where one core starts something the other runs (a pulse's LED flash, the end glow, the pause blink, a click), it
//  sends an EVENT COUNTER, never a write to the other core's working variable, so neither can undo the other's update.
//  On the PC the tests call Control() every 32 samples in the same thread, so everything stays exact and repeatable.
// =============================================================================
#pragma once
#include <stdint.h>
#include "cardkit_dsp.h"
#include "qi_config.h"
#include "qi_fx.h"

namespace qi {
using namespace cardkit;

struct Inputs  { int32_t main = 0, x = 0, y = 0; int32_t sw = 1; int32_t audio1 = 0, audio2 = 0; };   // sw: 0 Down, 1 Middle, 2 Up
struct Outputs { int16_t audio1 = 0, audio2 = 0; bool pulse1 = false, pulse2 = false; uint16_t led[6] = {0, 0, 0, 0, 0, 0}; };   // (CV Out 1/2: held at 0 V, see qi_card.h)

enum State : uint8_t { ST_STOPPED, ST_SETTLING, ST_RUNNING, ST_PREVIEW };
enum Field : uint8_t { F_INTERVALS, F_MINUTES, F_SECONDS };          // (also the click index: kClickHz[field])
enum Page  : uint8_t { PG_NONE, PG_REVERB, PG_MIX, PG_DELAY };
// The six effect settings (soft-pickup knobs): page REVERB = X time, Y brightness; MIX = X reverb level, Y delay level;
// DELAY = X echo time, Y feedback.
enum FxKnob : uint8_t { FK_REV_TIME, FK_REV_BRIGHT, FK_REV_MIX, FK_DEL_MIX, FK_DEL_TIME, FK_DEL_FB, FK_COUNT };

struct Settings
{
	int32_t minutes = kDefaultMinutes, secIdx = kDefaultSecIdx, intervals = kDefaultIntervals;
	int32_t Seconds() const { return secIdx * kSecondsStep; }
	int32_t IntervalSeconds() const { int32_t s = minutes * 60 + Seconds(); return s < kMinIntervalSeconds ? kMinIntervalSeconds : s; }
};

// Event names for the PC tests.  (Declared in every build; only the test build installs a hook that hears them.)
enum HostEvent { HE_START, HE_SETTLED, HE_PULSE1, HE_PULSE2, HE_PAUSE, HE_RESUME, HE_STOP, HE_FINISH, HE_CLICK, HE_SETTING, HE_PREVIEW };

// How the set-up LEDs show a number from 1 to 36 (the owner's design, "rule A"):
//   1-6: that many MEDIUM LEDs.  Above 6, each FULL LED stands for a six: value = 6 x (full LEDs) + the rest, where
//   the rest (1-6) is shown by blinking full LED number 1..(full LEDs) if it fits there, otherwise by medium LEDs after
//   the full ones.  So every six has exactly six pictures, e.g. 7 = 1 full blinking, 8 = 1 full + 1 medium,
//   13 = 2 full (first blinking), 14 = 2 full (second blinking), 15 = 2 full + 1 medium.   LED states: 0 off, 1 medium,
//   2 full, 3 full blinking.
static inline void RuleA(int32_t v, uint8_t st[6])
{
	if (v <= 0) { for (int i = 0; i < 6; ++i) st[i] = 0; return; }   // 0 (minutes): no LEDs
	int32_t f = (v - 1) / 6, r = (v - 1) % 6;
	for (int i = 0; i < 6; ++i)
	{
		if (i < f) st[i] = (r < f && i == r) ? 3 : 2;
		else st[i] = (r >= f && i < f + (r - f + 1)) ? 1 : 0;
	}
}
#ifdef QI_HOST_TEST
typedef void (*HostEventFn)(void* ctx, HostEvent e, int32_t a, int32_t b);
#endif

// Which notch (0..count-1) a knob is in, with hysteresis around the CURRENT notch:
// to leave notch `cur` the knob must go kNotchHyst past the edge of its slice.
static inline int32_t NotchFor(int32_t k, int32_t cur, int32_t count)
{
	int32_t n = Clamp((k * count) >> 12, 0, count - 1);
	if (n == cur) return cur;
	int32_t lo = (cur * 4096) / count - kNotchHyst;          // control rate only: the RP2040 has a hardware divider
	int32_t hi = ((cur + 1) * 4096) / count + kNotchHyst;
	return (k < lo || k >= hi) ? n : cur;
}

// A set-up knob: armed (inactive) until deliberately moved, then its position picks the notch directly.
struct NotchKnob
{
	int32_t count = 1, idx = 0, armPos = 0;
	bool armed = true;
	void Arm(int32_t k) { armPos = k; armed = true; }
	bool Update(int32_t k)                                   // true when the value changed
	{
		int32_t n;
		if (armed)
		{
			if (Abs(k - armPos) < kTakeoverMove) return false;
			armed = false;
			n = Clamp((k * count) >> 12, 0, count - 1);       // take over: the knob's position is the value from now on
		}
		else n = NotchFor(k, idx, count);
		if (n == idx) return false;
		idx = n;
		return true;
	}
};

class Engine
{
public:
	void Init(uint32_t seed, bool fillTables = true)        // (fillTables = false only for the cycle-count bench, which loads them itself)
	{
		(void)seed;                                          // (no randomness needed yet)
		if (fillTables)
		{
			InitTables();                                    // sine table (start-up only; uses floats)
			InitFxTables();                                  // knob -> effect parameter tables (start-up only; uses floats)
		}
		const int32_t def[FK_COUNT] = {kDefRevTime, kDefRevBright, kDefRevMix, kDefDelMix, kDefDelTime, kDefDelFb};
		for (int i = 0; i < FK_COUNT; ++i) { pk_[i].value = def[i]; pk_[i].engaged = false; }
		fxp_ = Targets();                                    // start at the defaults (no fade-in)
		fx_[0].Init(0, fxp_);
		fx_[1].Init(1, fxp_);
		n_ = 0;
	}

	void Process(const Inputs& in, Outputs& out)
	{
		n_ = n_ + 1;                                         // (volatile: core 1 paces itself by this count)
		TickTimer();                                         // before the switch, so a Start at sample t0 gives Pulse 1 at exactly t0 + 10 s
		HandleSwitch(in.sw);
		uint32_t cmd = clickCmd_;                            // a click requested by core 1 (sequence << 2 | which setting)
		if (cmd != clickSeen_) { clickSeen_ = cmd; clickAge_ = 0; clickPh_ = 0; clickInc_ = (uint32_t)kClickHz[cmd & 3] * 89478u; }   // 2^32/48000

		// ---- audio: dry (never changed) + wet (softly limited to the headroom the dry signal leaves) ----
		// The two channels never mix.  Their half-rate effects take turns (even / odd samples) to spread the CPU load.
		int32_t x1 = Clamp(in.audio1, -2048, 2047), x2 = Clamp(in.audio2, -2048, 2047);
		bool odd = (n_ & 1) != 0;
		int32_t w1 = fx_[0].Process(x1, !odd, fxp_);
		int32_t w2 = fx_[1].Process(x2, odd, fxp_);
		if (keeperDry_) { x1 = fx_[0].GateDry(x1); x2 = fx_[1].GateDry(x2); }   // (owner's choice: true silence between sounds)
		if (clickAge_ < kClickLength) { int32_t c = ClickSample(); x1 = Clamp(x1 + c, -2048, 2047); x2 = Clamp(x2 + c, -2048, 2047); }
		out.audio1 = (int16_t)AddWet(x1, (w1 + (1 << (kFxQ - 1))) >> kFxQ);   // wet is Q6: round to 12-bit
		out.audio2 = (int16_t)AddWet(x2, (w2 + (1 << (kFxQ - 1))) >> kFxQ);

		out.pulse1 = pulse1_ > 0; if (pulse1_ > 0) --pulse1_;
		out.pulse2 = pulse2_ > 0; if (pulse2_ > 0) --pulse2_;
	}

	// Core 1, every 32 samples: all the slow work (was one job per sample on core 0 before; same 1.5 kHz rate).
	void Control(const Inputs& in)
	{
		SetupKnobs(in);
		FxKnobs(in);
		FxSmooth();
		fx_[0].Wander(); fx_[1].Wander();
		fx_[0].KeeperLearn(); fx_[1].KeeperLearn();
		UpdateLeds();
	}
	// The LED values Control() worked out (core 1 writes them to the hardware).
	void ReadControl(Outputs& out) const
	{
		for (int i = 0; i < 6; ++i) out.led[i] = led_[i];
	}
	uint32_t SampleCount() const { return n_; }

	// ---- read-only views (for the glue, the LEDs and the tests) ----
	State   GetState() const { return state_; }
	bool    Paused() const { return paused_; }
	bool    SetupActive() const { return setupActive_; }
	Field   Display() const { return display_; }
	const Settings& GetSettings() const { return set_; }
	int32_t IntervalIndex() const { return idx_; }
	Page    GetPage() const { return page_; }
	const Pickup& FxSetting(int i) const { return pk_[i]; }
	const FxParams& FxNow() const { return fxp_; }
#ifdef QI_HOST_TEST
	FxChannel& Channel(int i) { return fx_[i]; }
	void SetKeeper(bool on, bool dry) { fx_[0].SetKeeper(on); fx_[1].SetKeeper(on); keeperDry_ = on && dry; }
	void SetFxMixForTest() { fxp_.revGain = fxp_.delGain = 0; pk_[FK_REV_MIX].value = pk_[FK_DEL_MIX].value = 0; }   // mute the wet signal
#endif

#ifdef QI_HOST_TEST
	void SetHostHook(HostEventFn fn, void* ctx) { hook_ = fn; hookCtx_ = ctx; }
#endif

private:
	// ------------------------------------------------------------------
	// The timer (runs every sample: just a counter and a compare)
	// ------------------------------------------------------------------
	void TickTimer()
	{
		if (state_ == ST_PREVIEW)
		{
			if (++pos_ == kPreviewStep) FirePulse(2);            // the bell, 1 s after the chime
			else if (pos_ >= 2 * kPreviewStep) { pos_ = 0; FirePulse(1); }
			return;
		}
		if (state_ == ST_STOPPED || paused_) return;
		if (state_ == ST_SETTLING)
		{
			if (++pos_ >= kSettleSamples)
			{
				pos_ = 0; idx_ = 0; state_ = ST_RUNNING;
				FirePulse(1);                                // "the session has started"
				Host(HE_SETTLED, 0, 0);
			}
			return;
		}
		if (idx_ == 0 && pos_ + 1 == kDoubleChimeGap) FirePulse(1);  // the second chime of the start (interval 1 is running)
		if (++pos_ >= len_)
		{
			pos_ = 0;
			if (idx_ + 1 < count_) { ++idx_; FirePulse(1); }  // end of intervals 1 .. N-1: Interval alert
			else { FirePulse(2); Finish(); }                 // end of interval N: End alert only
		}
	}

	void FirePulse(int which)
	{
		if (which == 1) { pulse1_ = kPulseSamples; fx_[0].Alert(); Host(HE_PULSE1, idx_, 0); }   // the chime (on In 1) is coming
		else            { pulse2_ = kPulseSamples; fx_[1].Alert(); Host(HE_PULSE2, idx_, 0); }   // the bell (on In 2) is coming
		pulseSeq_ = pulseSeq_ + 1;                           // (core 1 starts the LED flash)
	}

	void Start()
	{
		len_ = set_.IntervalSeconds() * kSampleRate;        // max 36:60 = 2220 s = 106,560,000 samples (fits easily)
		count_ = set_.intervals;
		total_ = (uint32_t)len_ * (uint32_t)count_;         // max 36 x 37 min = 22.2 h = 3,836,160,000 samples:
		                                                     // fits UNSIGNED 32-bit (limit 4,294,967,295), not signed
		state_ = ST_SETTLING; paused_ = false; pos_ = 0; idx_ = 0;
		Host(HE_START, len_, count_);
	}

	void Stop()   { state_ = ST_STOPPED; paused_ = false; pos_ = 0; idx_ = 0; Host(HE_STOP, 0, 0); }
	void EnterPreview() { state_ = ST_PREVIEW; pos_ = 0; FirePulse(1); Host(HE_PREVIEW, 1, 0); }
	void ExitPreview()  { state_ = ST_STOPPED; pos_ = 0; Host(HE_PREVIEW, 0, 0); }
	void Finish() { state_ = ST_STOPPED; paused_ = false; pos_ = 0; finishSeq_ = finishSeq_ + 1; Host(HE_FINISH, 0, 0); }   // (core 1 starts the end glow)

	// ------------------------------------------------------------------
	// Z switch.  Down is momentary: a tap is a press of 10 ms .. 0.8 s.  Taps less than 0.4 s apart are counted
	// together; 0.4 s after the last release they act: 3 or more while stopped = preview mode; otherwise one tap =
	// start / pause / resume (or leave preview).  Held 3 s = stop (fires while still held; the release is ignored).
	// Up is a latched position: set-up mode (only takes effect while stopped).
	// ------------------------------------------------------------------
	void HandleSwitch(int32_t sw)
	{
		if (bootGuard_ > 0)
		{
			--bootGuard_; lastSw_ = (int)sw; setupMode_ = (sw == 2);
			if (bootGuard_ == 0 && sw == 0) ignoreRelease_ = true;   // held down at power-up: ignore that press entirely
			return;
		}
		if (tapWait_ > 0 && sw != 0 && --tapWait_ == 0) ResolveTaps();   // (the wait pauses while Z is held down)
		if (sw == 0)
		{
			if (lastSw_ != 0) { downCount_ = 0; holdFired_ = false; }
			else
			{
				if (downCount_ < kHoldSamples) ++downCount_;
				if (!holdFired_ && !ignoreRelease_ && downCount_ >= kHoldSamples) { holdFired_ = true; OnHold(); }
			}
		}
		else if (lastSw_ == 0)
		{
			if (!holdFired_ && !ignoreRelease_ && downCount_ >= kBounceSamples && downCount_ < kTapMaxSamples) OnTap();   // a long press let go early = cancelled
			ignoreRelease_ = false; downCount_ = 0;
		}
		lastSw_ = (int)sw;

		bool up = (sw == 2);                                 // set-up mode, debounced
		if (up != setupMode_) { if (++modeCount_ >= kModeStable) { setupMode_ = up; modeCount_ = 0; } }
		else modeCount_ = 0;
	}

	void OnTap() { ++tapCount_; tapWait_ = kTapGap; }
	void ResolveTaps()
	{
		int n = tapCount_;
		tapCount_ = 0;
		if (state_ == ST_PREVIEW) ExitPreview();            // any tap ends preview mode
		else if (n >= 3 && state_ == ST_STOPPED) EnterPreview();
		else if (state_ == ST_STOPPED) Start();
		else { paused_ = !paused_; pauseSeq_ = pauseSeq_ + 1; Host(paused_ ? HE_PAUSE : HE_RESUME, 0, 0); }   // resuming never repeats the settling period
	}
	void OnHold()                                           // holding while stopped does nothing
	{
		tapCount_ = 0; tapWait_ = 0;                        // (taps just before a hold are forgotten)
		if (state_ == ST_PREVIEW) ExitPreview();
		else if (state_ != ST_STOPPED) Stop();
	}

	// ---- set-up (Z up, only while stopped): Main = intervals, X = minutes, Y = seconds ----
	// Each knob is armed on entering set-up and changes nothing until deliberately moved (then its position is the
	// value).  Every change gives a soft click (a different pitch per setting) and shows that setting on the LEDs.
	void SetupKnobs(const Inputs& in)
	{
		bool active = setupMode_ && state_ == ST_STOPPED;
		if (active && !setupActive_) EnterSetup(in);
		setupActive_ = active;
		if (!active) return;
		if (knobI_.Update(in.main)) { set_.intervals = kMinIntervals + knobI_.idx; Changed(F_INTERVALS); }
		if (knobM_.Update(in.x))    { set_.minutes   = kMinMinutes   + knobM_.idx; Changed(F_MINUTES); }
		if (knobS_.Update(in.y))    { set_.secIdx    = knobS_.idx;                 Changed(F_SECONDS); }
	}
	void EnterSetup(const Inputs& in)
	{
		knobI_.count = kMaxIntervals - kMinIntervals + 1; knobI_.idx = set_.intervals - kMinIntervals; knobI_.Arm(in.main);
		knobM_.count = kMaxMinutes - kMinMinutes + 1;     knobM_.idx = set_.minutes - kMinMinutes;     knobM_.Arm(in.x);
		knobS_.count = kSecondsNotches;                   knobS_.idx = set_.secIdx;                    knobS_.Arm(in.y);
		display_ = F_INTERVALS;                              // the LEDs start by showing the number of intervals
		setBlink_ = 0;
	}
	void Changed(Field f)
	{
		display_ = f;
		setBlink_ = 0;                                       // (a blinking LED restarts bright, so a new value is seen at once)
		clickCmd_ = ((++clickSeq_) << 2) | (uint32_t)f;      // ask core 0 for a click: ONE 32-bit store (sequence + setting)
		Host(HE_CLICK, f, FieldValue(f));
		Host(HE_SETTING, f, FieldValue(f));
	}
	int32_t FieldValue(Field f) const { return f == F_MINUTES ? set_.minutes : f == F_SECONDS ? set_.Seconds() : set_.intervals; }

	// One sample of the set-up click: a short sine burst, gentle 0.67 ms attack, ~5 ms fade.
	inline int32_t ClickSample()
	{
		int32_t age = clickAge_++;
		if (age == 0) clickEnv_ = 32768;
		else if (age >= kClickAttack) clickEnv_ -= (clickEnv_ >> kClickDecay) + 1;
		if (clickEnv_ < 0) clickEnv_ = 0;
		int32_t a = (kClickLevel * clickEnv_) >> 15;
		if (age < kClickAttack) a = (a * age) / kClickAttack;
		clickPh_ += clickInc_;
		return (SineLookup(clickPh_) * a) >> 11;
	}

	// ---- effect pages (Z middle, any time: stopped, running or paused) ----
	// Main picks the page (fully CCW = reverb, 12 o'clock = mix, fully CW = delay; between = nothing changes).
	// X and Y use SOFT PICKUP: a stored value holds until the knob passes through it, so nothing ever jumps.
	// Pickups are re-armed when Main enters a page and when Z comes back from set-up (Up) -- not after a tap on Z Down.
	void FxKnobs(const Inputs& in)
	{
		if (bootGuard_ > 0) return;
		if (setupMode_) { fxEdit_ = false; return; }          // Z up belongs to the timer set-up
		if (!fxEdit_) { fxEdit_ = true; page_ = PageFor(in.main, PG_NONE); ArmPage(in); }
		if (lastSw_ == 0) return;                             // while Z is held down, the knobs rest
		Page pg = PageFor(in.main, page_);
		if (pg != page_)
		{
			page_ = pg; ArmPage(in);
			if (pg != PG_NONE) { fxShow_ = kPageShowSamples; fxShowPage_ = true; }
		}
		if (page_ == PG_NONE) return;
		int a = ((int)page_ - 1) * 2;
		pk_[a].Update(in.x);
		pk_[a + 1].Update(in.y);
		if (Abs(in.x - xRef_) > 24) { xRef_ = in.x; ShowParam(a); }
		if (Abs(in.y - yRef_) > 24) { yRef_ = in.y; ShowParam(a + 1); }
	}
	void ArmPage(const Inputs& in)
	{
		xRef_ = in.x; yRef_ = in.y;
		if (page_ == PG_NONE) return;
		int a = ((int)page_ - 1) * 2;
		pk_[a].Arm(in.x);
		pk_[a + 1].Arm(in.y);
	}
	void ShowParam(int i) { fxShow_ = kFxShowSamples; fxShowPage_ = false; fxShowParam_ = i; }
	static Page PageFor(int32_t m, Page cur)
	{
		int32_t revTop = kPageRevTop    + (cur == PG_REVERB ? kPageHyst : -kPageHyst);
		int32_t mixLo  = kPageMixLo     + (cur == PG_MIX ? -kPageHyst : kPageHyst);
		int32_t mixHi  = kPageMixHi     + (cur == PG_MIX ? kPageHyst : -kPageHyst);
		int32_t delBot = kPageDelBottom + (cur == PG_DELAY ? -kPageHyst : kPageHyst);
		if (m < revTop) return PG_REVERB;
		if (m > delBot) return PG_DELAY;
		if (m > mixLo && m < mixHi) return PG_MIX;
		return PG_NONE;
	}
	FxParams Targets() const
	{
		return FxTargets(pk_[FK_REV_TIME].value, 4095 - pk_[FK_REV_BRIGHT].value, pk_[FK_REV_MIX].value,   // (Y = brightness)
		                 pk_[FK_DEL_MIX].value, pk_[FK_DEL_TIME].value, pk_[FK_DEL_FB].value);
	}
	// Smooth every parameter at 1.5 kHz (about 5 ms) so turning a knob never clicks.  Always reaches the exact target.
	static inline void SmoothTo(int32_t& v, int32_t t)
	{
		int32_t d = t - v, s = d >> 3;
		if (s == 0) s = (d > 0) - (d < 0);
		v += s;
	}
	void FxSmooth()
	{
		FxParams t = Targets();
		SmoothTo(fxp_.revFb, t.revFb);   SmoothTo(fxp_.revIn, t.revIn);   SmoothTo(fxp_.revDamp, t.revDamp);
		SmoothTo(fxp_.revGain, t.revGain); SmoothTo(fxp_.delGain, t.delGain); SmoothTo(fxp_.delFb, t.delFb);
		fxp_.delTimeQ8 = t.delTimeQ8;                         // (the delay glides on its own)
	}

	// num / den * full, without overflowing 32 bits (control rate only)
	static int32_t Ratio(uint32_t num, uint32_t den, int32_t full)
	{
		while (den > 65535u) { num >>= 1; den >>= 1; }
		if (den == 0) return 0;
		if (num > den) num = den;
		return (int32_t)((num * (uint32_t)full) / den);
	}

	// ---- LEDs (see the legend in docs/LED_LEGEND.md) ----
	void Bar(int32_t fillQ12, int32_t level)                 // fillQ12: 0 .. 6*4096 (how many LEDs are lit, fractionally)
	{
		for (int i = 0; i < 6; ++i)
		{
			int32_t part = Clamp(fillQ12 - i * 4096, 0, 4096);
			led_[kLedOrder[i]] = (uint16_t)((level * part) >> 12);
		}
	}
	void UpdateLeds()
	{
		// events from core 0
		uint32_t ps = pulseSeq_, fs = finishSeq_, bs = pauseSeq_;
		if (ps != pulseSeen_) { pulseSeen_ = ps; if (kFlashOnPulse) flash_ = kFlashSamples; }
		if (fs != finishSeen_) { finishSeen_ = fs; doneGlow_ = kDoneGlowSamples; }
		if (bs != pauseSeen_) { pauseSeen_ = bs; blink_ = 0; }
		if (state_ != ST_STOPPED) doneGlow_ = 0;
		if (flash_ > 0)    flash_    = flash_    > 32 ? flash_ - 32 : 0;
		if (doneGlow_ > 0) doneGlow_ = doneGlow_ > 32 ? doneGlow_ - 32 : 0;
		blink_ += 32; if (blink_ >= 2 * kBlinkSamples) blink_ = 0;
		breath_ += 32; if (breath_ >= 5 * kSampleRate) breath_ = 0;
		setBlink_ += 32; if (setBlink_ >= 2 * kSetupBlinkSamples) setBlink_ = 0;

		if (state_ != ST_STOPPED && lastSw_ == 0 && downCount_ > kHoldShowAfter && !holdFired_ && !ignoreRelease_)
		{
			// holding Z down to stop: the bar fills over the 3 s so you can feel it working
			Bar(Ratio((uint32_t)(downCount_ - kHoldShowAfter), (uint32_t)(kHoldSamples - kHoldShowAfter), 6 * 4096), kLedBright);
			return;
		}
		if (fxShow_ > 0)
		{
			fxShow_ = fxShow_ > 32 ? fxShow_ - 32 : 0;
			for (int i = 0; i < 6; ++i) led_[i] = 0;
			if (fxShowPage_)
			{
				// which page, by ROW, in the order of Main's travel (owner, 19 Oct): reverb = top, mix = middle, delay = bottom
				static const uint8_t pat[4][6] = {{0,0,0,0,0,0}, {1,1,0,0,0,0}, {0,0,1,1,0,0}, {0,0,0,0,1,1}};
				for (int i = 0; i < 6; ++i) led_[i] = pat[page_][i] ? (uint16_t)kLedBright : 0;
				return;
			}
			// the stored value as a bar on a faint background; it flickers until the knob has picked it up
			const Pickup& pk = pk_[fxShowParam_];
			int32_t level = kLedBright;
			if (!pk.engaged && ((n_ >> 12) & 1)) level = level / 5;
			Bar(Ratio((uint32_t)pk.value, 4095u, 6 * 4096), level);
			for (int i = 0; i < 6; ++i) if (led_[i] < kLedFaint / 2) led_[i] = (uint16_t)(kLedFaint / 2);
			return;
		}
		if (state_ == ST_STOPPED)
		{
			if (doneGlow_ > 0) { Bar(6 * 4096, Ratio((uint32_t)doneGlow_, (uint32_t)kDoneGlowSamples, kLedBright)); return; }
			if (setupActive_)
			{
				uint8_t st[6] = {0, 0, 0, 0, 0, 0};
				if (display_ == F_SECONDS) { for (int i = 0; i < set_.secIdx && i < 6; ++i) st[i] = 1; }   // one medium LED per 10 s
				else RuleA(display_ == F_MINUTES ? set_.minutes : set_.intervals, st);
				bool low = setBlink_ >= kSetupBlinkSamples;
				for (int i = 0; i < 6; ++i)
					led_[i] = (uint16_t)(st[i] == 1 ? kLedMedium : st[i] == 2 ? kLedBright : st[i] == 3 ? (low ? kLedMedium / 4 : kLedBright) : 0);
				return;
			}
			// ready: ALL SIX LEDs breathe slowly and dimly (owner, 19 Oct)
			int32_t ph = Ratio((uint32_t)breath_, (uint32_t)(5 * kSampleRate), 4096);
			int32_t tri = ph < 2048 ? ph * 2 : (4095 - ph) * 2;
			uint16_t b = (uint16_t)((kLedReady * Clamp(tri, 0, 4095)) >> 12);
			for (int i = 0; i < 6; ++i) led_[i] = b;
			return;
		}
		if (state_ == ST_PREVIEW)
		{
			// preview: the left column for the chime's second, the right column for the bell's (+ the soft flash)
			bool bellSide = pos_ >= kPreviewStep;
			for (int i = 0; i < 6; ++i) led_[i] = ((i & 1) == (bellSide ? 1 : 0)) ? (uint16_t)kLedRun : 0;
		}
		else
		{
		int32_t fill, level;
		if (state_ == ST_SETTLING) { fill = Ratio((uint32_t)(kSettleSamples - pos_), (uint32_t)kSettleSamples, 6 * 4096); level = kLedBright; }   // counts DOWN
		else { fill = Ratio((uint32_t)idx_ * (uint32_t)len_ + (uint32_t)pos_, total_, 6 * 4096); level = kLedRun; }                              // fills UP
		if (paused_ && blink_ >= kBlinkSamples) level = level * kLedPausedLow / 100;
		Bar(fill, level);
		}
		if (flash_ > 0)
		{
			int32_t add = Ratio((uint32_t)flash_, (uint32_t)kFlashSamples, kLedBright);
			for (int i = 0; i < 6; ++i) led_[i] = (uint16_t)Clamp(led_[i] + add, 0, 4095);
		}
	}

	inline void Host(int e, int32_t a, int32_t b)
	{
#ifdef QI_HOST_TEST
		if (hook_) hook_(hookCtx_, (HostEvent)e, a, b);
#else
		(void)e; (void)a; (void)b;
#endif
	}

	// ------------------------------------------------------------------
	// State
	// ------------------------------------------------------------------
	volatile uint32_t n_ = 0;                                // samples processed (core 1 paces itself by this)
	// events from core 0 to core 1 (counters: core 1 notices the change) and from core 1 to core 0 (the click request)
	volatile uint32_t pulseSeq_ = 0, finishSeq_ = 0, pauseSeq_ = 0, clickCmd_ = 0;
	uint32_t pulseSeen_ = 0, finishSeen_ = 0, pauseSeen_ = 0, clickSeen_ = 0, clickSeq_ = 0;
	// timer
	State state_ = ST_STOPPED;
	bool paused_ = false;
	int32_t pos_ = 0, len_ = 0, idx_ = 0, count_ = 0;   // pos_ = samples into the settling period or the current interval
	uint32_t total_ = 1;
	int32_t pulse1_ = 0, pulse2_ = 0;
	// switch
	int32_t bootGuard_ = kBootGuard, downCount_ = 0, modeCount_ = 0;
	int lastSw_ = 1;
	bool holdFired_ = false, ignoreRelease_ = false, setupMode_ = false;
	int tapCount_ = 0;
	int32_t tapWait_ = 0;
	// set-up
	Settings set_;
	bool setupActive_ = false;
	Field display_ = F_INTERVALS;
	NotchKnob knobI_, knobM_, knobS_;
	int32_t setBlink_ = 0;
	int32_t clickAge_ = kClickLength, clickEnv_ = 0;
	uint32_t clickPh_ = 0, clickInc_ = 0;
	// outputs
	int32_t flash_ = 0, doneGlow_ = 0, blink_ = 0, breath_ = 0;
	// effects
	FxChannel fx_[2];
	bool keeperDry_ = kKeeperOn && kKeeperOnDry;
	FxParams fxp_;                                           // the smoothed parameters actually in use
	Pickup pk_[FK_COUNT];
	Page page_ = PG_NONE;
	bool fxEdit_ = false, fxShowPage_ = false;
	int32_t xRef_ = 0, yRef_ = 0, fxShow_ = 0;
	int fxShowParam_ = 0;
	uint16_t led_[6] = {0, 0, 0, 0, 0, 0};
#ifdef QI_HOST_TEST
	HostEventFn hook_ = nullptr;
	void* hookCtx_ = nullptr;
#endif
};

}  // namespace qi
