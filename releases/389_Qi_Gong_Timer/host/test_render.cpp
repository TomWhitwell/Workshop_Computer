// =============================================================================
//  test_render.cpp : PC tests for the QiGong Timer engine (stage 1).
//  Runs the REAL engine (and the real glue, against a mock) sample by sample.
//  Usage: ./test_render_fast <test>|all|quick|fx|demo   (quick = everything except the 10-hour run and the demos)
//  AI-assisted (Claude / Anthropic).
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "qi_engine.h"
#include "qi_card.h"

using namespace qi;

static int gFails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++gFails; std::printf("    FAIL: " __VA_ARGS__); std::printf("   [%s:%d]\n", __FILE__, __LINE__); } } while (0)

struct Ev { uint64_t t; int e; int32_t a, b; int state; };

// ----------------------------------------------------------------------------------------------------------
// Sim: the engine plus a recording of everything it does
// ----------------------------------------------------------------------------------------------------------
struct Sim
{
	Engine eng;
	Inputs in;
	Outputs out;
	uint64_t t = 0;                                   // index of the sample being processed
	std::vector<Ev> ev;
	std::vector<uint64_t> rise1, rise2;
	std::vector<int64_t> width1, width2;
	bool l1 = false, l2 = false;
	uint64_t r1 = 0, r2 = 0, startT = 0;
	int32_t ledMax = 0;
	bool audioOk = true;

	Sim() { ClearFxMemory(); eng.Init(1); eng.SetHostHook(&Hook, this); in.sw = 1; in.main = in.x = in.y = 2048; }
	// The effect memories are global arrays (as on the card, where there is one engine); tests start each Sim clean.
	static void ClearFxMemory() { std::memset(gFdn, 0, sizeof gFdn); std::memset(gAp, 0, sizeof gAp); std::memset(gDel, 0, sizeof gDel); }
	static void Hook(void* c, HostEvent e, int32_t a, int32_t b)
	{
		Sim* s = (Sim*)c;
		s->ev.push_back({s->t, (int)e, a, b, (int)s->eng.GetState()});
		if (e == HE_START) s->startT = s->t;
	}
	void Step()
	{
		eng.Process(in, out);
		if ((eng.SampleCount() & 31) == 0) eng.Control(in);          // core 1's work, exactly as the card paces it
		eng.ReadControl(out);
		if (out.pulse1 && !l1) { rise1.push_back(t); r1 = t; }
		if (!out.pulse1 && l1) width1.push_back((int64_t)(t - r1));
		if (out.pulse2 && !l2) { rise2.push_back(t); r2 = t; }
		if (!out.pulse2 && l2) width2.push_back((int64_t)(t - r2));
		l1 = out.pulse1; l2 = out.pulse2;
		for (int i = 0; i < 6; ++i) if (out.led[i] > ledMax) ledMax = out.led[i];
		if (out.audio1 < -2048 || out.audio1 > 2047 || out.audio2 < -2048 || out.audio2 > 2047) audioOk = false;   // the DAC's range
		++t;
	}
	void Run(uint64_t n) { for (uint64_t i = 0; i < n; ++i) Step(); }
	void RunSec(double s) { Run((uint64_t)(s * kSampleRate)); }
	void Boot() { Run(kBootGuard + 100); }
	// Press Z down for `ms`, then release.  Returns the sample index of the release.
	uint64_t Press(int ms) { in.sw = 0; Run((uint64_t)ms * kMs); in.sw = 1; uint64_t rel = t; Step(); return rel; }
	// A single tap: press, release, and wait the 0.4 s after which a tap acts.  Returns the release sample.
	uint64_t Tap() { uint64_t r = Press(150); Run(kTapGap); return r; }
	// Turn a knob smoothly to `target` over `ms`, then wait `waitMs`
	void Turn(int32_t& knob, int32_t target, int ms, int waitMs = 0)
	{
		int32_t from = knob; int64_t n = (int64_t)ms * kMs;
		for (int64_t i = 1; i <= n; ++i) { knob = from + (int32_t)(((int64_t)(target - from) * i) / n); Step(); }
		knob = target;
		Run((uint64_t)waitMs * kMs);
	}
	void SwitchTo(int pos, int waitMs = 50) { in.sw = pos; Run((uint64_t)waitMs * kMs); }
	int Count(int e, uint64_t after = 0) const { int c = 0; for (auto& x : ev) if (x.e == e && x.t >= after) ++c; return c; }
	const Ev* Last(int e) const { for (int i = (int)ev.size() - 1; i >= 0; --i) if (ev[i].e == e) return &ev[i]; return nullptr; }
	// Settings changes recorded after time `after`
	std::vector<int32_t> Changes(int field, uint64_t after) const
	{ std::vector<int32_t> v; for (auto& x : ev) if (x.e == HE_SETTING && x.a == field && x.t >= after) v.push_back(x.b); return v; }
};

// Knob position at the centre of notch `idx` of `count`
static int32_t NotchCentre(int idx, int count) { return (int32_t)(((2 * idx + 1) * 4096) / (2 * count)); }

// Turn a set-up knob to a value as a person would: if it is already close to the target (so it could not take over),
// nudge it away first, then turn to the target.  (A real user hears nothing and does the same.)
static void TurnTo(Sim& s, int32_t& knob, int32_t target)
{
	if (std::abs(knob - target) < 3 * kTakeoverMove) s.Turn(knob, target < 2048 ? target + 400 : target - 400, 200, 100);
	s.Turn(knob, target, 400, 500);
}
// Set the timer with the knobs, as a person would (Z up; Main = intervals, X = minutes, Y = seconds index (x10 s); Z middle)
static void SetUp(Sim& s, int minutes, int secIdx, int intervals)
{
	s.SwitchTo(2, 100);
	TurnTo(s, s.in.main, NotchCentre(intervals - kMinIntervals, kMaxIntervals - kMinIntervals + 1));
	TurnTo(s, s.in.x, NotchCentre(minutes - kMinMinutes, kMaxMinutes - kMinMinutes + 1));
	TurnTo(s, s.in.y, NotchCentre(secIdx, kSecondsNotches));
	s.SwitchTo(1, 100);
}

// ----------------------------------------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------------------------------------
static void TestSwitch()
{
	std::printf("[switch] boot guard, bounce, tap (acts 0.4 s after release), long press, 3 s hold\n");
	{
		Sim s; s.in.sw = 0; s.Boot(); s.RunSec(1.0); s.in.sw = 1; s.RunSec(1.0);   // held down at power-up
		CHECK(s.eng.GetState() == ST_STOPPED && s.Count(HE_START) == 0, "a press held through power-up must be ignored");
		std::printf("    held through power-up: starts=%d\n", s.Count(HE_START));
	}
	Sim s; s.Boot();
	s.Press(5); s.RunSec(1.0);
	CHECK(s.Count(HE_START) == 0, "a 5 ms bounce must be ignored");
	uint64_t rel = s.Tap();
	CHECK(s.Count(HE_START) == 1 && s.startT == rel + (uint64_t)kTapGap, "tap must start exactly 0.4 s after release (start %llu, release %llu)",
		(unsigned long long)s.startT, (unsigned long long)rel);
	s.Tap(); CHECK(s.eng.Paused(), "second tap pauses");
	s.Tap(); CHECK(!s.eng.Paused(), "third tap resumes");
	s.Press(2000); s.RunSec(1.0);
	CHECK(!s.eng.Paused() && s.eng.GetState() != ST_STOPPED, "a 2 s press let go early does nothing (cancelled hold)");
	uint64_t press = s.t; s.in.sw = 0; s.RunSec(3.5); s.in.sw = 1; s.RunSec(1.0);
	const Ev* st = s.Last(HE_STOP);
	CHECK(st && st->t - press == (uint64_t)kHoldSamples, "hold must stop at exactly 3.000 s");
	CHECK(s.eng.GetState() == ST_STOPPED && s.Count(HE_START) == 1, "the release after a hold must not start again");
	std::printf("    5 ms bounce ignored; tap starts %.3f s after release; pause/resume; 2 s press = nothing; hold stops after %.3f s; release after hold ignored\n",
		(double)(s.startT - rel) / kSampleRate, st ? (double)(st->t - press) / kSampleRate : -1.0);
	s.in.sw = 0; s.RunSec(3.5); s.in.sw = 1; s.RunSec(1.0);
	CHECK(s.eng.GetState() == ST_STOPPED && s.Count(HE_START) == 1, "holding while stopped does nothing");
}

static void CheckTimeline(int minutes, int secIdx, int n)
{
	Sim s; s.Boot();
	SetUp(s, minutes, secIdx, n);
	const Settings& g = s.eng.GetSettings();
	CHECK(g.minutes == minutes && g.secIdx == secIdx && g.intervals == n, "knob set-up gave %d:%02d x %d, wanted %d:%02d x %d",
		g.minutes, g.Seconds(), g.intervals, minutes, secIdx * kSecondsStep, n);
	s.Tap();
	int32_t secs = minutes * 60 + secIdx * kSecondsStep; if (secs < kMinIntervalSeconds) secs = kMinIntervalSeconds;   // (0:00 -> 0:10)
	uint64_t t0 = s.startT, L = (uint64_t)secs * kSampleRate, S = kSettleSamples;
	s.Run(S + L * n + kSampleRate * 2 - (s.t - t0));
	std::vector<uint64_t> want1 = {t0 + S, t0 + S + kDoubleChimeGap};          // the double chime at the start
	for (int k = 1; k < n; ++k) want1.push_back(t0 + S + L * k);                // then one at the end of intervals 1 .. N-1
	bool ok = s.rise1 == want1 && s.rise2.size() == 1 && s.rise2[0] == t0 + S + L * n;
	for (auto w : s.width1) ok = ok && w == kPulseSamples;
	for (auto w : s.width2) ok = ok && w == kPulseSamples;
	CHECK(ok, "timeline wrong for %d:%02d x %d", minutes, secIdx * kSecondsStep, n);
	CHECK(s.eng.GetState() == ST_STOPPED, "must stop after the end pulse");
	std::printf("    %2d:%02d x %2d : Pulse 1 at +%.3f and +%.3f s (double chime), %zu more Pulse 1, Pulse 2 at +%.3f s (expected +%.3f), widths 100 ms  %s\n",
		minutes, secIdx * kSecondsStep, n, s.rise1.size() > 0 ? (double)(s.rise1[0] - t0) / kSampleRate : -1.0,
		s.rise1.size() > 1 ? (double)(s.rise1[1] - t0) / kSampleRate : -1.0, s.rise1.size() > 2 ? s.rise1.size() - 2 : 0,
		s.rise2.empty() ? -1.0 : (double)(s.rise2[0] - t0) / kSampleRate, (double)(S + L * n) / kSampleRate, ok ? "OK" : "WRONG");
}

static void TestTimeline()
{
	std::printf("[timeline] 10 s settling, double chime, N-1 interval pulses, end pulse (exact sample times)\n");
	CheckTimeline(1, 0, 3);
	CheckTimeline(1, 1, 1);
	CheckTimeline(2, 3, 2);
	CheckTimeline(1, 6, 4);
	CheckTimeline(0, 3, 3);                                           // seconds only (owner, 19 Oct)
	CheckTimeline(0, 0, 2);                                           // 0:00 becomes 0:10
}

static void TestPause()
{
	std::printf("[pause] pause in the settling period and in an interval; stop and restart\n");
	Sim s; s.Boot(); SetUp(s, 1, 0, 2);
	s.Tap(); uint64_t t0 = s.startT;
	s.RunSec(5);
	uint64_t tp = s.Tap(); s.RunSec(7); uint64_t tr = s.Tap();
	uint64_t shift1 = tr - tp;
	s.RunSec(20);
	uint64_t tp2 = s.Tap(); s.RunSec(20);
	int32_t ledLo = 4095, ledHi = 0;
	for (int i = 0; i < 3 * kSampleRate; ++i) { s.Step(); ledLo = std::min<int32_t>(ledLo, s.out.led[0]); ledHi = std::max<int32_t>(ledHi, s.out.led[0]); }
	uint64_t tr2 = s.Tap();
	uint64_t shift2 = tr2 - tp2;
	s.RunSec(140);
	const uint64_t S = kSettleSamples, M = 60ull * kSampleRate;
	std::vector<uint64_t> want1 = {t0 + S + shift1, t0 + S + shift1 + kDoubleChimeGap, t0 + S + shift1 + shift2 + M};
	bool ok = s.rise1 == want1 && s.rise2.size() == 1 && s.rise2[0] == t0 + S + shift1 + shift2 + 2 * M;
	CHECK(ok, "pauses must shift the pulses by exactly the paused time");
	CHECK(s.Count(HE_START) == 1, "resume must not restart the settling period");
	CHECK(ledHi > 0 && ledLo < ledHi / 2, "paused bar should blink (%d..%d)", ledLo, ledHi);
	std::printf("    paused %.3f s in settling and %.3f s in interval 1: pulses moved by exactly that; LED blinks %d..%d  %s\n",
		(double)shift1 / kSampleRate, (double)shift2 / kSampleRate, ledLo, ledHi, ok ? "OK" : "WRONG");

	Sim u; u.Boot(); SetUp(u, 1, 0, 3);
	u.Tap(); u.RunSec(40);
	u.in.sw = 0; u.RunSec(3.2); u.in.sw = 1; u.RunSec(1);
	size_t p1 = u.rise1.size();
	u.RunSec(200);
	CHECK(u.rise1.size() == p1 && u.rise2.empty(), "no pulses after stop");
	u.Tap(); uint64_t t1 = u.startT; u.RunSec(11);
	bool rs = u.rise1.size() == p1 + 2 && u.rise1[p1] == t1 + S;
	CHECK(rs, "restart must repeat the 10 s settling period (and the double chime)");
	std::printf("    hold-to-stop at 40 s: no pulses in the next 200 s; restart gives its double chime after 10.000 s  %s\n", rs ? "OK" : "WRONG");
}

// Sweep one set-up knob over its whole range and back; then rest it on every boundary with jitter.
static void SweepField(const char* name, int32_t Inputs::*knob, int field, int lo, int hi, int step)
{
	Sim s; s.Boot(); s.SwitchTo(2, 100);
	(s.in.*knob) = 0; s.Run(100 * kMs);                               // (a jump while armed: takes over at the bottom)
	s.Turn(s.in.*knob, 4095, 100, 400);
	s.Turn(s.in.*knob, 0, 100, 400);
	uint64_t t1 = s.t;
	s.Turn(s.in.*knob, 4095, 9000, 400);
	std::vector<int32_t> up = s.Changes(field, t1);
	uint64_t t2 = s.t;
	s.Turn(s.in.*knob, 0, 9000, 400);
	std::vector<int32_t> down = s.Changes(field, t2);
	bool okU = (int)up.size() == (hi - lo) / step, okD = (int)down.size() == (hi - lo) / step;
	for (size_t i = 0; okU && i < up.size(); ++i) okU = up[i] == lo + step * (int)(i + 1);
	for (size_t i = 0; okD && i < down.size(); ++i) okD = down[i] == hi - step * (int)(i + 1);
	CHECK(okU && okD, "%s sweep not clean", name);
	int flick = 0, count = (hi - lo) / step + 1;
	for (int b = 1; b < count; ++b)
	{
		int32_t edge = (b * 4096) / count;
		s.Turn(s.in.*knob, edge, 200, 300);
		uint64_t tj = s.t;
		uint32_t r = 12345u + (uint32_t)b;
		for (int i = 0; i < kSampleRate; ++i) { r = r * 1664525u + 1013904223u; (s.in.*knob) = edge + (int32_t)((r >> 16) % 61) - 30; s.Step(); }
		flick += (int)s.Changes(field, tj).size();
	}
	CHECK(flick == 0, "%s: %d accidental changes while resting on boundaries with jitter", name, flick);
	std::printf("    %-9s sweep up: %zu steps %d->%d, down: %zu steps; ends reached; jitter on %d boundaries: %d accidental changes  %s\n",
		name, up.size(), lo, up.empty() ? lo : up.back(), down.size(), count - 1, flick, (okU && okD && flick == 0) ? "OK" : "WRONG");
}

static void TestNotches()
{
	std::printf("[notch] every value reachable in order, both ends, no flicker on boundaries (+/-30 jitter)\n");
	SweepField("intervals", &Inputs::main, F_INTERVALS, kMinIntervals, kMaxIntervals, 1);
	SweepField("minutes", &Inputs::x, F_MINUTES, kMinMinutes, kMaxMinutes, 1);
	SweepField("seconds", &Inputs::y, F_SECONDS, 0, (kSecondsNotches - 1) * kSecondsStep, kSecondsStep);
}

static void TestSetupKnobs()
{
	std::printf("[setup] each knob sets only its own value; Z middle and a running session leave the settings alone\n");
	Sim s; s.Boot(); s.SwitchTo(2, 100);
	uint64_t t1 = s.t;
	s.Turn(s.in.main, 0, 300, 100); s.Turn(s.in.main, 3000, 600, 200);
	bool mainOnly = s.Changes(F_MINUTES, t1).empty() && s.Changes(F_SECONDS, t1).empty() && !s.Changes(F_INTERVALS, t1).empty();
	uint64_t t2 = s.t;
	s.Turn(s.in.x, 0, 300, 100); s.Turn(s.in.x, 3000, 600, 200);
	bool xOnly = s.Changes(F_INTERVALS, t2).empty() && s.Changes(F_SECONDS, t2).empty() && !s.Changes(F_MINUTES, t2).empty();
	uint64_t t3 = s.t;
	s.Turn(s.in.y, 0, 300, 100); s.Turn(s.in.y, 3000, 600, 200);
	bool yOnly = s.Changes(F_INTERVALS, t3).empty() && s.Changes(F_MINUTES, t3).empty() && !s.Changes(F_SECONDS, t3).empty();
	CHECK(mainOnly && xOnly && yOnly, "knob ownership: Main %d, X %d, Y %d", mainOnly, xOnly, yOnly);
	s.SwitchTo(1, 100); uint64_t t4 = s.t;
	s.Turn(s.in.main, 0, 500); s.Turn(s.in.y, 0, 500); s.Turn(s.in.x, 0, 500); s.Turn(s.in.main, 4095, 500);
	bool midOk = s.Count(HE_SETTING, t4) == 0;
	CHECK(midOk, "knobs must not change settings in Z middle");
	Sim u; u.Boot(); SetUp(u, 1, 0, 1);
	u.Tap(); u.RunSec(20); u.SwitchTo(2, 100);
	uint64_t t5 = u.t;
	u.Turn(u.in.x, 0, 300); u.Turn(u.in.main, 4095, 500); u.Turn(u.in.y, 4095, 500);
	bool frozen = u.Count(HE_SETTING, t5) == 0 && !u.eng.SetupActive() && u.eng.GetSettings().minutes == 1;
	CHECK(frozen, "Z up while running must be ignored");
	u.RunSec(60);
	CHECK(u.rise2.size() == 1 && u.eng.SetupActive(), "after the end with Z up, set-up must activate");
	std::printf("    Main -> intervals only, X -> minutes only, Y -> seconds only: %s; Z middle: %s; Z up while running: %s; after the end: set-up active  %s\n",
		(mainOnly && xOnly && yOnly) ? "yes" : "NO", midOk ? "no changes" : "CHANGED", frozen ? "ignored" : "NOT ignored",
		(mainOnly && xOnly && yOnly && midOk && frozen) ? "OK" : "WRONG");
}

static void TestTakeover()
{
	std::printf("[takeover] flipping Z up never changes a value; a deliberate turn takes over at once\n");
	Sim s; s.Boot();
	s.in.main = 3000; s.in.x = 200; s.in.y = 3500; s.Run(100);       // knobs far from the stored 6 x 5:00
	uint64_t t1 = s.t;
	s.SwitchTo(2, 300);
	uint32_t r = 99;
	for (int i = 0; i < kSampleRate; ++i)
	{
		r = r * 1664525u + 1013904223u;
		s.in.main = 3000 + (int32_t)((r >> 16) % 61) - 30; s.in.x = 200 + (int32_t)((r >> 4) % 61) - 30; s.in.y = 3500 + (int32_t)((r >> 8) % 61) - 30;
		s.Step();
	}
	int changes = s.Count(HE_SETTING, t1);
	const Settings& g = s.eng.GetSettings();
	CHECK(changes == 0 && g.intervals == kDefaultIntervals && g.minutes == kDefaultMinutes && g.secIdx == kDefaultSecIdx,
		"entering set-up / small jitter must not change values (%d)", changes);
	s.in.main = 3000; s.Turn(s.in.main, 3150, 150, 400);
	int32_t v = s.eng.GetSettings().intervals, at = ((3150 * 36) >> 12) + 1;
	CHECK(v == at || v == at - 1, "a deliberate turn must take over at the knob's position (got %d, knob says %d)", v, at);
	std::printf("    entering set-up with all three knobs elsewhere + 1 s of +/-30 jitter: %d changes; a 150-unit turn of Main took over (intervals = %d)  %s\n",
		changes, v, changes == 0 ? "OK" : "WRONG");
}

static void TestClicks()
{
	std::printf("[clicks] one soft click per step, a different pitch per setting; nothing outside set-up\n");
	Sim s; s.Boot(); s.SwitchTo(2, 200);
	uint64_t t1 = s.t;
	s.Turn(s.in.main, 0, 300, 300); s.Turn(s.in.main, 4095, 3000, 300);
	int nSet = 0, nClick = 0;
	for (auto& e : s.ev) if (e.t >= t1) { nSet += e.e == HE_SETTING; nClick += e.e == HE_CLICK && e.a == F_INTERVALS; }
	CHECK(nClick == nSet && nSet >= 35, "every change must click once (%d changes, %d clicks)", nSet, nClick);
	// one isolated click per setting: level, length, pitch (zero crossings)
	const char* names[3] = {"intervals", "minutes", "seconds"};
	int32_t Inputs::*knobs[3] = {&Inputs::main, &Inputs::x, &Inputs::y};
	bool good = true;
	for (int f = 0; f < 3; ++f)
	{
		int32_t& k = s.in.*knobs[f];
		s.Turn(k, 2048, 300, 300);
		int32_t target = f == 2 ? k + 700 : k + 200;                  // one step (past the boundary + hysteresis)
		k = target; s.Run(64);                                       // (jump: the click starts within one control tick)
		std::vector<int32_t> w;
		int32_t peak = 0, first = -1, last = -1;
		for (int i = 0; i < 100 * kMs; ++i)
		{
			s.Step();
			int32_t v = s.out.audio1;
			w.push_back(v);
			if (v != 0) { if (first < 0) first = i; last = i; }
			peak = std::max(peak, std::abs(v));
		}
		double len = last >= first && first >= 0 ? (double)(last - first + 1) / kSampleRate : 0;
		// pitch: upward zero crossings in the first 8 ms of the click (while it is clearly audible)
		int ups = 0, firstUp = -1, lastUp = -1;
		for (int i = std::max(first, 1); first >= 0 && i < first + 8 * kMs && i < (int)w.size(); ++i)
			if (w[i - 1] < 0 && w[i] >= 0) { if (firstUp < 0) firstUp = i; lastUp = i; ++ups; }
		double hz = ups > 1 ? (double)(ups - 1) * kSampleRate / (double)(lastUp - firstUp) : 0;
		bool ok = peak > 0 && peak <= kClickLevel && len < 0.045 && std::fabs(hz - kClickHz[f]) < kClickHz[f] * 0.15;
		good = good && ok;
		std::printf("    %-9s click: peak %d (%.1f dBFS), audible for %.1f ms, pitch ~%.0f Hz (set %d Hz)  %s\n",
			names[f], peak, 20.0 * std::log10(peak / 2048.0), len * 1000.0, hz, kClickHz[f], ok ? "OK" : "WRONG");
	}
	CHECK(good, "click shape");
	// no clicks in Z middle or while running
	s.SwitchTo(1, 100); uint64_t t2 = s.t;
	s.Turn(s.in.main, 0, 400); s.Turn(s.in.x, 0, 400); s.Turn(s.in.y, 0, 400);
	int32_t mx = 0; for (int i = 0; i < kSampleRate; ++i) { s.Step(); mx = std::max<int32_t>(mx, std::abs(s.out.audio1)); }
	bool quiet = s.Count(HE_CLICK, t2) == 0 && mx == 0;
	CHECK(quiet, "clicks outside set-up");
	std::printf("    sweep of Main: %d steps, %d clicks; Z middle: %d clicks, output peak %d  %s\n", nSet, nClick, s.Count(HE_CLICK, t2), mx, quiet ? "OK" : "WRONG");
}

static void TestPreview()
{
	std::printf("[preview] triple tap while stopped = chime, 1 s, bell, 1 s ...; a tap ends it\n");
	Sim s; s.Boot();
	for (int k = 0; k < 3; ++k) { s.Press(100); s.Run(150 * kMs); }
	s.Run(kTapGap);
	bool in = s.eng.GetState() == ST_PREVIEW;
	const Ev* pv = s.Last(HE_PREVIEW);
	uint64_t t0 = pv ? pv->t : 0;
	s.RunSec(6.2);
	bool pulses = s.rise1.size() == 4 && s.rise2.size() == 3;
	for (size_t i = 0; pulses && i < s.rise1.size(); ++i) pulses = s.rise1[i] == t0 + 2ull * kPreviewStep * i;
	for (size_t i = 0; pulses && i < s.rise2.size(); ++i) pulses = s.rise2[i] == t0 + kPreviewStep + 2ull * kPreviewStep * i;
	s.Tap();
	bool out = s.eng.GetState() == ST_STOPPED;
	size_t n1 = s.rise1.size(), n2 = s.rise2.size();
	s.RunSec(5);
	bool silent = s.rise1.size() == n1 && s.rise2.size() == n2 && s.Count(HE_START) == 0;
	CHECK(in && pulses && out && silent, "preview: in %d pulses %d out %d silent %d", in, pulses, out, silent);
	// triple tap while running = one tap (pause); double tap while stopped = one tap (start)
	Sim u; u.Boot(); SetUp(u, 1, 0, 2);
	u.Press(100); u.Run(150 * kMs); u.Press(100); u.Run(kTapGap + 10);
	bool dbl = u.Count(HE_START) == 1 && u.eng.GetState() != ST_PREVIEW;
	u.RunSec(3);
	for (int k = 0; k < 3; ++k) { u.Press(100); u.Run(150 * kMs); }
	u.Run(kTapGap);
	bool tri = u.eng.Paused() && u.eng.GetState() != ST_PREVIEW;
	// holding Z in preview ends it too
	Sim w; w.Boot();
	for (int k = 0; k < 3; ++k) { w.Press(100); w.Run(150 * kMs); }
	w.Run(kTapGap); w.in.sw = 0; w.RunSec(3.2); w.in.sw = 1; w.RunSec(0.5);
	bool held = w.eng.GetState() == ST_STOPPED;
	CHECK(dbl && tri && held, "double tap %d, triple tap while running %d, hold %d", dbl, tri, held);
	std::printf("    preview: chime at 0/2/4/6 s, bell at 1/3/5 s exactly; a tap ends it; no session started; double tap = start; triple tap while running = pause; hold ends preview  %s\n",
		(in && pulses && out && silent && dbl && tri && held) ? "OK" : "WRONG");
}

static void TestLongest()
{
	std::printf("[longest] 36:60 x 36 = 22 h 12 min (3.84 billion samples), run in full\n");
	Sim s; s.Boot(); SetUp(s, kMaxMinutes, kSecondsNotches - 1, kMaxIntervals);
	s.Tap(); uint64_t t0 = s.startT;
	uint64_t L = (uint64_t)(kMaxMinutes * 60 + (kSecondsNotches - 1) * kSecondsStep) * kSampleRate, S = kSettleSamples;
	s.Run(S + L * kMaxIntervals + kSampleRate - (s.t - t0));
	bool ok = s.rise1.size() == (size_t)kMaxIntervals + 1 && s.rise2.size() == 1 && s.rise2[0] == t0 + S + L * kMaxIntervals;
	for (int k = 1; ok && k < kMaxIntervals; ++k) ok = s.rise1[k + 1] == t0 + S + L * k;
	CHECK(ok, "the 22-hour session timeline is wrong");
	std::printf("    end pulse at sample +%llu (expected +%llu), %zu x Pulse 1 (incl. the double chime)  %s\n",
		s.rise2.empty() ? 0ull : (unsigned long long)(s.rise2[0] - t0), (unsigned long long)(S + L * kMaxIntervals), s.rise1.size(), ok ? "OK" : "WRONG");
}

static void PrintLeds(const char* label, const Outputs& o)
{
	auto c = [](uint16_t v) { return v == 0 ? "  .  " : v < 600 ? " ░░  " : v < 1600 ? " ▒▒  " : " ██  "; };
	std::printf("    %-38s %s%s | %s%s | %s%s   (%4d %4d %4d %4d %4d %4d)\n", label, c(o.led[0]), c(o.led[1]), c(o.led[2]), c(o.led[3]), c(o.led[4]), c(o.led[5]),
		o.led[0], o.led[1], o.led[2], o.led[3], o.led[4], o.led[5]);
}

static void TestLeds()
{
	std::printf("[leds] rule A against the owner's slides, then snapshots of the legend (rows 0 1 | 2 3 | 4 5; ░ dim/blink-low, ▒ medium, █ full)\n");
	// the owner's slides 1-2 (rule A) and what rule A gives for the other drawn values: 0 off, 1 medium, 2 full, 3 blinking
	struct Ex { int v; uint8_t s[6]; };
	const Ex ex[] = {{1, {1,0,0,0,0,0}}, {4, {1,1,1,1,0,0}}, {7, {3,0,0,0,0,0}}, {8, {2,1,0,0,0,0}}, {13, {3,2,0,0,0,0}},
	                 {14, {2,3,0,0,0,0}}, {15, {2,2,1,0,0,0}}, {18, {2,2,1,1,1,1}}, {20, {2,3,2,0,0,0}}, {21, {2,2,3,0,0,0}},
	                 {30, {2,2,2,2,1,1}}, {36, {2,2,2,2,2,1}}};
	bool ruleOk = true;
	for (auto& x : ex) { uint8_t st[6]; RuleA(x.v, st); for (int i = 0; i < 6; ++i) ruleOk = ruleOk && st[i] == x.s[i]; }
	bool unique = true;                                               // every value 1..36 has its own picture
	for (int a = 1; a <= 36; ++a) for (int b = a + 1; b <= 36; ++b) { uint8_t p[6], q[6]; RuleA(a, p); RuleA(b, q); unique = unique && std::memcmp(p, q, 6) != 0; }
	CHECK(ruleOk && unique, "rule A: slides %d, unique %d", ruleOk, unique);
	std::printf("    rule A matches slides 1-2 (1, 4, 7, 8, 13, 14, 15) and the rule for 18, 20, 21, 30, 36: %s; all 36 pictures different: %s\n",
		ruleOk ? "yes" : "NO", unique ? "yes" : "NO");

	Sim s; s.Boot(); s.RunSec(1.25); PrintLeds("stopped, ready (all six breathing)", s.out);
	{                                                                 // ready: all six equal, rising and falling over ~5 s
		Sim r; r.Boot(); int32_t lo = 4095, hi = 0; bool same = true;
		for (int n = 0; n < 6 * kSampleRate; ++n) { r.Step(); for (int i = 1; i < 6; ++i) same = same && r.out.led[i] == r.out.led[0]; lo = std::min<int32_t>(lo, r.out.led[0]); hi = std::max<int32_t>(hi, r.out.led[0]); }
		CHECK(same && hi > lo + 500 && hi <= kLedReady, "ready breath: all six equal %d, range %d..%d", same, lo, hi);
		std::printf("    ready: all six LEDs equal at every moment: %s; breathing between %d and %d (dim)\n", same ? "yes" : "NO", lo, hi);
	}
	s.SwitchTo(2, 300); PrintLeds("set-up: intervals = 6 (default)", s.out);
	TurnTo(s, s.in.main, NotchCentre(12, 36)); s.Run(10 * kMs); PrintLeds("set-up: intervals = 13 (blink phase 1)", s.out);
	s.Run(400 * kMs); PrintLeds("set-up: intervals = 13 (blink phase 2)", s.out);
	TurnTo(s, s.in.x, NotchCentre(15 - kMinMinutes, kMaxMinutes - kMinMinutes + 1)); s.Run(10 * kMs); PrintLeds("set-up: minutes = 15", s.out);
	CHECK(s.eng.GetSettings().minutes == 15, "LED scene: minutes should be 15 (is %d)", s.eng.GetSettings().minutes);
	TurnTo(s, s.in.y, NotchCentre(4, 7)); PrintLeds("set-up: seconds = 40", s.out);
	const Settings& g = s.eng.GetSettings();
	std::printf("    (settings now %d:%02d x %d)\n", g.minutes, g.Seconds(), g.intervals);
	s.SwitchTo(1, 100); s.Tap(); s.RunSec(2); PrintLeds("settling, 2 s in (counting down)", s.out);
	s.RunSec(8.1); PrintLeds("start: double chime (soft flash)", s.out);
	s.RunSec(15.5 * 60 * 6); PrintLeds("running, about half way", s.out);
	s.Tap(); s.RunSec(0.5); PrintLeds("paused (blink: on)", s.out);
	s.RunSec(1.0); PrintLeds("paused (blink: low)", s.out);
	s.Tap(); s.in.sw = 0; s.RunSec(2.0); PrintLeds("holding Z down 2 s (stop filling)", s.out);
	s.in.sw = 1; s.RunSec(1.0);
	s.in.sw = 0; s.RunSec(3.2); s.in.sw = 1; s.RunSec(7.0);                // hold 3 s: stop (and let the end glow pass)
	for (int k = 0; k < 3; ++k) { s.Press(100); s.Run(150 * kMs); }
	s.Run(kTapGap + 300 * kMs); PrintLeds("preview: chime second (left column)", s.out);
	s.RunSec(1.0); PrintLeds("preview: bell second (right column)", s.out);
	s.Tap();
	Sim u; u.Boot(); SetUp(u, 1, 0, 1); u.Tap(); u.RunSec(70.5); PrintLeds("end pulse: all six glow", u.out);
	u.RunSec(4); PrintLeds("4 s after the end (fading)", u.out);
	CHECK(s.ledMax <= 4095 && u.ledMax <= 4095, "LED values out of range");
}

static void TestGlue()
{
	std::printf("[glue] the real QiTimerCard class against the mock ComputerCard\n");
	struct TestCard : public QiTimerCard { };
	const int N = 20 * kSampleRate;
	std::vector<int16_t> o1(N), o2(N);
	auto inA = [](int i) { return (int16_t)((i % 4800) < 200 ? ((i * 37) % 4000 - 2000) : 0); };   // bursts, then silence
	auto inB = [](int i) { return (int16_t)((i % 9600) < 300 ? (-((i * 13) % 4000) + 2000) : 0); };
	auto swAt = [](int i) { return (i > kSampleRate && i < kSampleRate + 200 * kMs) ? 0 : 1; };
	Sim::ClearFxMemory();
	TestCard c;
	c.cvout_[0] = c.cvout_[1] = 1234;                               // (a wrong value: Setup must set both CV outs to 0 V)
	c.Setup();
	bool cv0 = c.cvout_[0] == 0 && c.cvout_[1] == 0;
	bool audio = true; int rises = 0; bool last = false;
	for (int i = 0; i < N; ++i)
	{
		c.ain_[0] = inA(i); c.ain_[1] = inB(i);
		c.sw_ = swAt(i) == 0 ? ComputerCard::Down : ComputerCard::Middle;
		c.knobs_[0] = c.knobs_[1] = c.knobs_[2] = 2048;
		c.MockStep();
		c.Core1Step();                                               // (on the card this runs on core 1)
		cv0 = cv0 && c.cvout_[0] == 0 && c.cvout_[1] == 0;
		o1[i] = c.aout_[0]; o2[i] = c.aout_[1];
		if (c.pout_[0] && !last) ++rises;
		last = c.pout_[0];
	}
	Sim::ClearFxMemory();                                             // the same inputs through a bare engine must give identical audio
	Engine e; e.Init(1); Inputs in; Outputs out;
	for (int i = 0; i < N; ++i)
	{
		in.audio1 = inA(i); in.audio2 = inB(i); in.sw = swAt(i); in.main = in.x = in.y = 2048;
		e.Process(in, out);
		if ((e.SampleCount() & 31) == 0) e.Control(in);
		if (out.audio1 != o1[i] || out.audio2 != o2[i]) audio = false;
	}
	CHECK(audio && rises == 2 && cv0, "glue: audio identical %d, start pulses %d, CV outs at 0 V %d", (int)audio, rises, (int)cv0);
	std::printf("    20 s through the real glue: audio identical to the bare engine: %s; tap at 1 s -> the double chime (%d pulses) at ~11.6 s;\n"
	            "    CV Out 1 and 2 set to 0 V at start-up and never changed: %s  %s\n",
		audio ? "yes" : "NO", rises, cv0 ? "yes" : "NO", (audio && rises == 2 && cv0) ? "OK" : "WRONG");
}

static void TestFuzz(double minutes)
{
	std::printf("[fuzz] %.0f simulated minutes of random knob/switch abuse\n", minutes);
	Sim s; s.Boot();
	uint32_t r = 2024;
	auto rnd = [&](uint32_t n) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r % n; };
	uint64_t end = s.t + (uint64_t)(minutes * 60 * kSampleRate);
	int sessions = 0, notStarted = 0;
	while (s.t < end)
	{
		uint32_t a = rnd(100);
		if (a < 30) { int32_t* k = (a % 3 == 0) ? &s.in.main : (a % 3 == 1) ? &s.in.x : &s.in.y; s.Turn(*k, (int32_t)rnd(4096), 1 + (int)rnd(400)); }
		else if (a < 40) { s.in.main = (int32_t)rnd(4096); s.in.x = (int32_t)rnd(4096); s.in.y = (int32_t)rnd(4096); s.Run(1 + rnd(2000)); }
		else if (a < 55) s.SwitchTo((int)rnd(3), 1 + (int)rnd(600));
		else if (a < 70) { s.Press(1 + (int)rnd(300)); }
		else if (a < 73) { s.Press(2500 + (int)rnd(1500)); }
		else if (a < 80) { int k = 1 + (int)rnd(4); for (int i = 0; i < k; ++i) { s.Press(40 + (int)rnd(150)); s.Run(rnd(350 * kMs)); } }   // quick multi-taps
		else s.Run(rnd(5 * kSampleRate));
		if (s.in.sw != 0 && s.eng.GetState() == ST_STOPPED && rnd(20) == 0)
		{
			// a scripted session amid the abuse.  First let any taps still pending from the random presses resolve:
			// otherwise our tap could become the 2nd or 3rd of a multi-tap (and correctly start preview mode instead).
			s.SwitchTo(1, 50); s.Run(2 * kTapGap);
			if (s.eng.GetState() == ST_PREVIEW) { s.Tap(); }
			if (s.eng.GetState() == ST_STOPPED)
			{
				SetUp(s, 1, 0, 1 + (int)rnd(2));
				int before = s.Count(HE_START);
				s.Tap();
				if (s.Count(HE_START) == before + 1) { s.RunSec(10.0 + 60.0 * s.eng.GetSettings().intervals + 1.0); ++sessions; }
				else { ++notStarted; std::printf("    (scripted tap did not start a session: state %d)\n", (int)s.eng.GetState()); }
			}
		}
		s.in.audio1 = (int32_t)rnd(6000) - 3000; s.in.audio2 = (int32_t)rnd(6000) - 3000;
	}
	const Settings& g = s.eng.GetSettings();
	bool range = g.minutes >= kMinMinutes && g.minutes <= kMaxMinutes && g.secIdx >= 0 && g.secIdx < kSecondsNotches
		&& g.intervals >= kMinIntervals && g.intervals <= kMaxIntervals;
	int badSet = 0; for (auto& e : s.ev) if (e.e == HE_SETTING && e.state != ST_STOPPED) ++badSet;
	int badW = 0; for (auto w : s.width1) badW += w != kPulseSamples; for (auto w : s.width2) badW += w != kPulseSamples;
	int pulses = 0; for (auto& e : s.ev) if (e.e == HE_PULSE1 || e.e == HE_PULSE2) ++pulses;
	bool ok = range && badSet == 0 && badW == 0 && s.audioOk && s.ledMax <= 4095 && pulses == (int)(s.rise1.size() + s.rise2.size())
		&& s.eng.Channel(0).clampHits_ == 0 && s.eng.Channel(1).clampHits_ == 0
		&& s.rise2.size() >= (size_t)sessions;
	CHECK(ok, "fuzz invariants broken");
	std::printf("    %d scripted sessions completed amid the abuse (%d scripted taps did not start one)\n", sessions, notStarted);
	CHECK(notStarted == 0, "a scripted tap failed to start a session");
	CHECK(s.Count(HE_PREVIEW) > 0, "the abuse never reached preview mode (it should, via random triple taps)");
	std::printf("    %llu samples; starts %d, pulses %zu+%zu, all 100 ms: %s; settings changed while running: %d; settings in range: %s; audio in range: %s; reverb clamps %d/%d  %s\n",
		(unsigned long long)s.t, s.Count(HE_START), s.rise1.size(), s.rise2.size(), badW ? "NO" : "yes", badSet, range ? "yes" : "NO",
		s.audioOk ? "yes" : "NO", s.eng.Channel(0).clampHits_, s.eng.Channel(1).clampHits_, ok ? "OK" : "WRONG");
}

#include "test_fx.inc"
#include "test_keeper.inc"

int main(int argc, char** argv)
{
	std::string w = argc > 1 ? argv[1] : "quick";
	bool all = w == "all", quick = w == "quick" || all;
	if (quick || w == "switch")   TestSwitch();
	if (quick || w == "timeline") TestTimeline();
	if (quick || w == "pause")    TestPause();
	if (quick || w == "notch")    TestNotches();
	if (quick || w == "setup")    TestSetupKnobs();
	if (quick || w == "takeover") TestTakeover();
	if (quick || w == "clicks")   TestClicks();
	if (quick || w == "preview")  TestPreview();
	if (quick || w == "leds")     TestLeds();
	if (quick || w == "glue")     TestGlue();
	if (quick || w == "fuzz")     TestFuzz(argc > 2 ? std::atof(argv[2]) : 20.0);
	bool fx = quick || w == "fx";
	if (fx || w == "fxdry")      TestFxDry();
	if (fx || w == "fxreverb")   TestFxReverb();
	if (fx || w == "fxdelay")    TestFxDelay();
	if (fx || w == "fxstable")   TestFxStable();
	if (fx || w == "fxchannels") TestFxChannels();
	if (fx || w == "fxpages")    TestFxPages();
	if (fx || w == "mainzones")  TestMainZones();
	if (fx || w == "fxclicks")   TestFxClicks();
	if (fx || w == "fxlevels")   TestFxLevels();
	bool kp = quick || w == "keeperall";
	if (kp || w == "keeper")     TestKeeper();
	if (kp || w == "keeperhand") TestKeeperHand();
	if (kp || w == "cvwhine")    TestCvWhine();
	if (kp || w == "stoppedsilence") TestStoppedSilence();
	if (w == "demo")             { Demos(); KeeperDemos(); SimplifiedDemos(); }
	if (w == "demo2")            SimplifiedDemos();
	if (all || w == "longest")    TestLongest();
	std::printf("\n%s (%d failure%s)\n", gFails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", gFails, gFails == 1 ? "" : "s");
	return gFails ? 1 : 0;
}
