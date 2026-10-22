// Host-side tests of the Weights engine (weights.h) and the 8mu
// pages and LEDs (ui.h).  No hardware needed: build and run with
// test/run_tests.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../weights.h"
#include "../ui.h"

using namespace weights;

static int checks = 0, failures = 0;

static int ScaleNamed(const char *name)
{
	for (int i = 0; i < kNumScales; i++) if (!strcmp(kScales[i].name, name)) return i;
	printf("no scale %s\n", name);
	return 0;
}
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void SetPage(Engine &e, int page, const int v[8])
{
	for (int i = 0; i < 8; i++) e.faders[page][i] = uint8_t(v[i]);
	e.Rescan();
}
static void ClearPage(Engine &e, int page)
{
	for (int i = 0; i < 8; i++) e.faders[page][i] = 0;
	e.Rescan();
}

// Run until n more steps have started (internal clock unless told otherwise)
static void RunSteps(Engine &e, int n)
{
	uint8_t start = e.stepCount;
	while (uint8_t(e.stepCount - start) < n) e.Tick(false);
	e.Tick(false);   // voice 2 draws a sample after voice 1
}

//----------------------------------------------------------------------------
static void TestChoose()
{
	uint32_t rng = 12345;
	// Proportional to the faders, zeros never chosen
	{
		volatile uint8_t f[8] = {100, 0, 50, 0, 25, 0, 0, 0};
		int count[8] = {};
		const int n = 175000;
		for (int i = 0; i < n; i++) count[Choose(f, 0xFF, NextRandom(rng))]++;
		CHECK(count[1] == 0 && count[3] == 0 && count[5] == 0 && count[6] == 0 && count[7] == 0, "zero faders chosen");
		CHECK(abs(count[0] - 100000) < 1500, "fader 1 share %d of 100000", count[0]);
		CHECK(abs(count[2] - 50000) < 1200, "fader 3 share %d of 50000", count[2]);
		CHECK(abs(count[4] - 25000) < 900, "fader 5 share %d of 25000", count[4]);
	}
	// Faders at 1 or 2 count as zero; at 3 they count
	{
		volatile uint8_t f[8] = {2, 1, 3, 0, 0, 0, 0, 0};
		for (int i = 0; i < 2000; i++) CHECK(Choose(f, 0xFF, NextRandom(rng)) == 2, "near-zero fader chosen");
	}
	// All zero: every active fader equally likely
	{
		volatile uint8_t f[8] = {};
		int count[8] = {};
		const int n = 80000;
		for (int i = 0; i < n; i++) count[Choose(f, 0xFF, NextRandom(rng))]++;
		for (int i = 0; i < 8; i++) CHECK(abs(count[i] - 10000) < 500, "uniform fader %d: %d", i, count[i]);
	}
	// Only active faders: a 5-note scale ignores faders 6-8, even when up
	{
		volatile uint8_t f[8] = {10, 10, 10, 10, 10, 127, 127, 127};
		for (int i = 0; i < 5000; i++) CHECK(Choose(f, 0x1F, NextRandom(rng)) < 5, "inactive fader chosen");
		volatile uint8_t z[8] = {0, 0, 0, 0, 0, 127, 127, 127};
		int count[5] = {};
		for (int i = 0; i < 50000; i++) { int c = Choose(z, 0x1F, NextRandom(rng)); CHECK(c < 5, "inactive"); if (c < 5) count[c]++; }
		for (int i = 0; i < 5; i++) CHECK(abs(count[i] - 10000) < 600, "uniform among active %d: %d", i, count[i]);
	}
}

static void TestScales()
{
	for (int s = 0; s < kNumScales; s++)
	{
		const Scale &sc = kScales[s];
		CHECK(sc.n >= 1 && sc.n <= 24, "scale %s size", sc.name);
		CHECK(sc.tet == 0 || sc.tet == sc.n, "scale %s tet", sc.name);
		int prev = -1;
		for (int k = 0; k < sc.n; k++)
		{
			int c = NoteCents(sc, k);
			CHECK(c > prev && c < 1200, "scale %s note %d cents %d", sc.name, k, c);
			prev = c;
		}
		CHECK(NoteCents(sc, 0) == 0, "scale %s starts at root", sc.name);
		// Every note belongs to exactly one fader, and the faders that cover
		// notes are exactly the active ones
		int covered[24] = {};
		uint8_t active = ActiveNoteFaders(sc);
		for (int f = 0; f < 8; f++)
		{
			int first, count;
			FaderNotes(sc, f, first, count);
			CHECK((count > 0) == bool((active >> f) & 1), "scale %s fader %d active", sc.name, f);
			for (int k = first; k < first + count; k++) { covered[k]++; CHECK(FaderOfNote(sc, k) == f, "scale %s FaderOfNote %d", sc.name, k); }
		}
		for (int k = 0; k < sc.n; k++) CHECK(covered[k] == 1, "scale %s note %d covered %d times", sc.name, k, covered[k]);
	}
	// Particular tunings
	CHECK(NoteCents(kScales[ScaleNamed("5-TET")], 1) == 240, "5-TET step");
	CHECK(NoteCents(kScales[ScaleNamed("7-TET")], 1) == 171, "7-TET step");
	CHECK(NoteCents(kScales[ScaleNamed("8-TET")], 1) == 150 && kScales[ScaleNamed("8-TET")].n == 8, "8-TET");
	CHECK(NoteCents(kScales[ScaleNamed("Raga Todi")], 2) == 294, "Todi's low third");
	CHECK(kScales[ScaleNamed("Raga Marwa")].n == 6, "Marwa: six notes");
	// No scale has more than eight notes: one fader each
	for (int i = 0; i < kNumScales; i++) CHECK(kScales[i].n <= 8, "%s has %d notes", kScales[i].name, kScales[i].n);
	CHECK(kNumScales <= 63, "scale numbers fit the six LEDs");
	// Octaves: fader 1 is -2V, fader 3 0V, fader 8 +5V
	const Scale &maj = kScales[ScaleNamed("Major")];
	CHECK(ScaleNamed("Major") == 0, "Major first (the default)");
	CHECK(NoteMillivolts(maj, 0, 0) == -2000, "lowest octave %d", NoteMillivolts(maj, 0, 0));
	CHECK(NoteMillivolts(maj, 0, 2) == 0, "0V octave");
	CHECK(NoteMillivolts(maj, 0, 7) == 5000, "highest octave");
	CHECK(NoteMillivolts(maj, 4, 2) == 583, "fifth = 7/12 V, got %d", NoteMillivolts(maj, 4, 2));
	CHECK(NoteMillivolts(maj, 6, 7) == 5917, "top note %d", NoteMillivolts(maj, 6, 7));
}

static void TestKnobs()
{
	CHECK(MapKnob(0) == 0 && MapKnob(128) == 0 && MapKnob(20) == 0, "bottom dead zone");
	CHECK(MapKnob(4095) == 4095 && MapKnob(3967) == 4095 && MapKnob(4050) == 4095, "top dead zone");
	CHECK(MapKnob(2048) > 2030 && MapKnob(2048) < 2066, "middle %d", MapKnob(2048));
	CHECK(ScaleFromKnob(MapKnob(30)) == 0 && ScaleFromKnob(MapKnob(4080)) == kNumScales - 1, "scale ends");
	CHECK(RatioFromKnob(MapKnob(30)) == 0 && RatioFromKnob(MapKnob(4080)) == kNumRatios - 1, "ratio ends");
	CHECK(RatioFromKnob(MapKnob(2048)) == kUnityRatio, "ratio centre is x1");
	for (int z = 0; z < kNumScales; z++) CHECK(ScaleFromKnob(KnobForZone(z, kNumScales)) == z, "scale zone %d", z);
	for (int z = 0; z < kNumRatios; z++) CHECK(RatioFromKnob(KnobForZone(z, kNumRatios)) == z, "ratio zone %d", z);
	// Slew, two-sided: off round the centre, 1ms just outside it, ~2s at
	// either end; left of centre only ties slew
	CHECK(SlewFromKnob(2048) == 1 << 24 && SlewFromKnob(2048 + kSlewCentreZone - 1) == 1 << 24 && SlewFromKnob(2048 - kSlewCentreZone + 1) == 1 << 24, "slew off at the centre");
	CHECK(abs(SlewFromKnob(2048 + kSlewCentreZone) - 349525) < 400 && abs(SlewFromKnob(2048 - kSlewCentreZone) - 349525) < 400, "slew 1ms either side");
	{
		double up = 16777216.0 / SlewFromKnob(4095) / 48000.0, down = 16777216.0 / SlewFromKnob(0) / 48000.0;
		CHECK(up > 1.8 && up < 2.2 && down > 1.8 && down < 2.2, "slew ends %.2fs %.2fs", up, down);
	}
	CHECK(SlewTiesOnly(0) && SlewTiesOnly(1000) && !SlewTiesOnly(3000), "left of centre: ties only");
	// Envelope times
	{
		double a0 = 2147483648.0 / AttackFromKnob(0) / 48.0, a1 = 2147483648.0 / AttackFromKnob(4095) / 48000.0;
		double d0 = 2147483648.0 / DecayFromKnob(0) / 48.0, d1 = 2147483648.0 / DecayFromKnob(4095) / 48000.0;
		CHECK(a0 > 0.45 && a0 < 0.55, "attack min %.2fms", a0);
		CHECK(a1 > 1.9 && a1 < 2.1, "attack max %.2fs", a1);
		CHECK(d0 > 9.5 && d0 < 10.5, "decay min %.2fms", d0);
		CHECK(d1 > 3.8 && d1 < 4.2, "decay max %.2fs", d1);
	}
	CHECK(PeriodFromBpm(120) == 24000 && PeriodFromBpm(5) == PeriodFromBpm(40) && PeriodFromBpm(999) == PeriodFromBpm(300), "tempo");
	// Switch down + Main: 40 BPM at the bottom, 300 at the top, exponential
	CHECK(BpmFromKnob(0) == 40 && BpmFromKnob(4095) == 300, "tempo knob ends %d %d", BpmFromKnob(0), BpmFromKnob(4095));
	CHECK(abs(BpmFromKnob(2048) - 110) <= 1, "tempo knob middle %d", BpmFromKnob(2048));
	for (int b = 40; b <= 300; b++) CHECK(abs(BpmFromKnob(KnobFromBpm(b)) - b) <= 1, "tempo %d round trip %d", b, BpmFromKnob(KnobFromBpm(b)));
	{
		int prev = 0;
		bool mono = true;
		for (int v = 0; v <= 4095; v++) { if (BpmFromKnob(v) < prev) mono = false; prev = BpmFromKnob(v); }
		CHECK(mono, "tempo rises with the knob");
	}
}

//----------------------------------------------------------------------------
static void TestInternalClock()
{
	Engine e;
	int starts = 0, last = -1, gaps = 0;
	for (int t = 0; t < 24000 * 10 + 10; t++)
	{
		uint8_t c = e.stepCount;
		e.Tick(false);
		if (e.stepCount != c)
		{
			if (last >= 0 && t - last != 24000) gaps++;
			last = t;
			starts++;
		}
	}
	CHECK(starts == 10, "120 BPM: %d steps in 5s", starts);
	CHECK(gaps == 0, "internal steps evenly spaced");
	CHECK(!e.clocked, "not clocked without Pulse In 1");
	// Tempo change
	e.internalPeriod = PeriodFromBpm(60);
	RunSteps(e, 1);
	int t0 = 1;   // RunSteps ends a sample into the step
	while (true) { uint8_t c = e.stepCount; e.Tick(false); t0++; if (e.stepCount != c) break; }
	CHECK(t0 == 48000, "60 BPM step %d samples", t0);
}

// Run with an external clock of the given period, counting the samples at
// which steps start
static int ClockedSteps(Engine &e, int period, int clocks, int *stepTimes, int maxSteps)
{
	int n = 0;
	for (int t = 0; t < period * clocks; t++)
	{
		uint8_t c = e.stepCount;
		e.Tick(t % period == 0);
		if (e.stepCount != c && n < maxSteps) stepTimes[n++] = t;
	}
	return n;
}

static void TestExternalClock()
{
	// x1: a step on every clock, from the second
	{
		Engine e;
		e.internalPeriod = 1000000; // keep the internal clock out of the way
		int times[64];
		int n = ClockedSteps(e, 10000, 10, times, 64);
		CHECK(e.clocked, "clock followed");
		CHECK(n == 9, "x1: %d steps for 10 clocks", n);
		CHECK(times[0] == 10000 && times[1] == 20000, "x1 steps on the clock %d %d", times[0], times[1]);
		CHECK(e.stepLen == 10000, "x1 step length %d", e.stepLen);
	}
	// x3: three evenly spaced steps a clock
	{
		Engine e;
		e.internalPeriod = 1000000;
		e.ratio = 7;
		int times[64];
		int n = ClockedSteps(e, 9000, 6, times, 64);
		// first clock: nothing (period unknown); then 3 a clock for 5 clocks
		CHECK(n == 15, "x3: %d steps", n);
		bool even = true;
		for (int i = 1; i < n; i++) if (abs((times[i] - times[i - 1]) - 3000) > 1) even = false;
		CHECK(even, "x3 steps evenly spaced");
		CHECK(e.stepLen == 3000, "x3 step length %d", e.stepLen);
	}
	// /4: a step every four clocks
	{
		Engine e;
		e.internalPeriod = 1000000;
		e.ratio = 2;
		int times[64];
		int n = ClockedSteps(e, 5000, 17, times, 64);
		CHECK(n == 4, "/4: %d steps", n);
		for (int i = 1; i < n; i++) CHECK(times[i] - times[i - 1] == 20000, "/4 spacing %d", times[i] - times[i - 1]);
		CHECK(e.stepLen == 20000, "/4 step length %d", e.stepLen);
	}
	// Clock stops: back to the internal clock after 4 periods (at least 2s)
	{
		Engine e;
		int times[8];
		ClockedSteps(e, 4800, 5, times, 8);
		CHECK(e.clocked, "clocked");
		int t = 0;
		while (e.clocked && t < 200000) { e.Tick(false); t++; }
		CHECK(t > 2 * 48000 - 4800 * 2 && t <= 2 * 48000, "fast clock times out after 2s (%d)", t);
		Engine f;
		ClockedSteps(f, 48000, 3, times, 8);
		t = 0;
		while (f.clocked && t < 400000) { f.Tick(false); t++; }
		CHECK(t > 3 * 48000 && t <= 4 * 48000, "slow clock times out after 4 periods (%d)", t);
		// and the internal clock carries on
		uint8_t c = f.stepCount;
		for (int i = 0; i < 24001; i++) f.Tick(false);
		CHECK(f.stepCount != c, "internal clock resumes");
	}
}

//----------------------------------------------------------------------------
// Ratchets: hits a step, where they fall, held or new notes
static void TestRatchets()
{
	for (int fader = 0; fader < 8; fader++)
	{
		Engine e;
		int r[8] = {};
		r[fader] = 100;
		SetPage(e, Ratchets, r);
		int g[8] = {0, 0, 0, 100, 0, 0, 0, 0}; // gates 3/7: a clean gate each hit
		SetPage(e, Gates, g);
		RunSteps(e, 1); // now on the first tick of a step
		int changedSteps = 0;
		for (int step = 0; step < 50; step++)
		{
			// Through one step, up to (not including) the next step's start:
			// count the gate's rising edges and changes of note
			uint8_t c = e.stepCount;
			int rises = e.gate[0] ? 1 : 0;   // the first hit, just now
			bool prev = e.gate[0], changed = false;
			int32_t note = e.voice[0].targetMv;
			while (true)
			{
				e.Tick(false);
				if (e.stepCount != c) break;
				if (e.gate[0] && !prev) rises++;
				prev = e.gate[0];
				if (e.voice[0].targetMv != note) { changed = true; note = e.voice[0].targetMv; }
			}
			CHECK(rises == kHits[SpeedOf(fader)], "fader %d: %d gates in a step, expected %d", fader, rises, kHits[SpeedOf(fader)]);
			if (changed) changedSteps++;
		}
		// Held ratchets keep the step's note through the step; new-note ones
		// change it at the hits
		if (!NewNoteEachHit(fader) || SpeedOf(fader) == 0)
			CHECK(changedSteps == 0, "fader %d: note changed within a step (%d)", fader, changedSteps);
		else
			CHECK(changedSteps > 20, "fader %d: new-note ratchet rarely changed note (%d of 50)", fader, changedSteps);
	}
	// Hit positions: 1.5x at 0 and 2/3, 2x at 1/2, 3x at 1/3 and 2/3
	{
		Engine e;
		e.stepLen = 24000;
		CHECK(e.HitPosition(1, 1) == 16000, "1.5x second hit %d", e.HitPosition(1, 1));
		CHECK(e.HitPosition(2, 1) == 12000, "2x second hit");
		CHECK(e.HitPosition(3, 1) == 8000 && e.HitPosition(3, 2) == 16000, "3x hits");
		CHECK(e.HitPosition(4, 1) == 6000 && e.HitPosition(4, 2) == 12000 && e.HitPosition(4, 3) == 18000, "4x hits");
		// Faders: 1x 1.5x 2x 3x held, then 1.5x 2x 3x 4x new
		const int hits[8] = {1, 2, 2, 3, 2, 2, 3, 4};
		for (int f = 0; f < 8; f++) CHECK(kHits[SpeedOf(f)] == hits[f] && NewNoteEachHit(f) == (f >= 4), "ratchet fader %d", f);
	}
	// The envelope fires on every hit (gates not tied)
	{
		Engine e;
		int r[8] = {0, 0, 0, 100, 0, 0, 0, 0}; // 3x
		SetPage(e, Ratchets, r);
		int g[8] = {0, 0, 100, 0, 0, 0, 0, 0};
		SetPage(e, Gates, g);
		e.attackInc = AttackFromKnob(0);
		e.decayInc = DecayFromKnob(0);
		RunSteps(e, 1);
		int peaks = 0;
		bool rising = false;
		int16_t prevA = e.audio[0];
		uint8_t c = e.stepCount;
		while (e.stepCount == c)
		{
			e.Tick(false);
			if (e.audio[0] > prevA) rising = true;
			if (rising && e.audio[0] < prevA) { peaks++; rising = false; }
			prevA = e.audio[0];
		}
		CHECK(peaks == 3, "3x: %d envelope peaks in a step", peaks);
	}
}

//----------------------------------------------------------------------------
// Gates: rest, lengths, the gap before the next hit, tied notes
static void TestGates()
{
	const int L = 24000;
	for (int gf = 0; gf < 8; gf++)
	{
		Engine e;
		int g[8] = {};
		g[gf] = 100;
		SetPage(e, Gates, g);
		int r[8] = {100, 0, 0, 0, 0, 0, 0, 0};
		SetPage(e, Ratchets, r);
		RunSteps(e, 2);
		int high = 0, lowRunAtEnd = 0, rises = 0, envMax = 0;
		bool prev = e.gate[0];
		uint8_t c = e.stepCount;
		for (int t = 0; t < 4 * L; t++)
		{
			e.Tick(false);
			if (e.gate[0]) { high++; lowRunAtEnd = 0; } else lowRunAtEnd++;
			if (e.gate[0] && !prev) rises++;
			if (e.stepCount != c)
			{
				c = e.stepCount;
				// at the moment of a new step the gate had been down a while
				if (gf != kTiedFader && gf != kRestFader)
					CHECK(lowRunAtEnd >= kGateGap || e.gate[0], "gate %d: gap %d before next hit", gf, lowRunAtEnd);
			}
			prev = e.gate[0];
			if (e.audio[0] > envMax) envMax = e.audio[0];
		}
		if (gf == kRestFader)
		{
			CHECK(high == 0, "rest: gate high %d samples", high);
			CHECK(envMax == 0, "rest: envelope %d", envMax);
		}
		else if (gf == kTiedFader)
		{
			CHECK(high == 4 * L, "tied: gate high %d of %d samples", high, 4 * L);
			CHECK(rises == 0, "tied: no new rising edges (%d)", rises);
		}
		else
		{
			int expect = 4 * ((L * gf) / 7);
			if (expect > 4 * (L - kGateGap)) expect = 4 * (L - kGateGap);
			CHECK(abs(high - expect) <= 8, "gate %d/7: high %d, expected %d", gf, high, expect);
			CHECK(rises == 4, "gate %d: %d rises in 4 steps", gf, rises);
			CHECK(envMax > 1600 && envMax <= 1706, "gate %d: envelope peak %d", gf, envMax);
		}
	}
	// Tied notes don't retrigger the envelope, and change pitch legato
	{
		Engine e;
		int g[8] = {0, 0, 0, 0, 0, 0, 0, 100};
		SetPage(e, Gates, g);
		e.attackInc = AttackFromKnob(0);
		e.decayInc = DecayFromKnob(4095); // 4s decay: a retrigger would show
		RunSteps(e, 1);
		int16_t prevA = e.audio[0];
		bool rose = false;
		for (int t = 0; t < 5 * 24000; t++)
		{
			e.Tick(false);
			if (t > 100 && e.audio[0] > prevA) rose = true;
			prevA = e.audio[0];
		}
		CHECK(!rose, "tied notes retriggered the envelope");
	}
	// Tied then rest: the gate falls at the rest
	{
		Engine e;
		int g[8] = {0, 0, 0, 0, 0, 0, 0, 100};
		SetPage(e, Gates, g);
		RunSteps(e, 2);
		CHECK(e.gate[0], "tied gate up");
		int r2[8] = {100, 0, 0, 0, 0, 0, 0, 0};
		SetPage(e, Gates, r2);
		RunSteps(e, 1);
		e.Tick(false);
		CHECK(!e.gate[0], "gate fell at a rest after a tie");
	}
	// A gate cut short by an early clock still gets a 1ms gap
	{
		Engine e;
		e.internalPeriod = 1000000;
		int g[8] = {0, 0, 0, 0, 0, 0, 100, 0}; // 6/7
		SetPage(e, Gates, g);
		// clock at 10000 then faster, at 5000: the second gate is still up
		int edges[] = {0, 10000, 20000, 25000, 30000};
		int lowRun = 0, minLowBeforeRise = 1 << 30;
		bool prev = false;
		int k = 0;
		for (int t = 0; t < 40000; t++)
		{
			bool edge = k < 5 && t == edges[k];
			if (edge) k++;
			e.Tick(edge);
			if (e.gate[0] && !prev && t > 20000 && lowRun < minLowBeforeRise) minLowBeforeRise = lowRun;
			lowRun = e.gate[0] ? 0 : lowRun + 1;
			prev = e.gate[0];
		}
		CHECK(minLowBeforeRise >= kGateGap - 1, "forced gap %d samples", minLowBeforeRise);
	}
}

//----------------------------------------------------------------------------
// Envelope shape
static void TestEnvelope()
{
	Envelope env;
	uint32_t a = AttackFromKnob(2000), d = DecayFromKnob(2000);
	double at = 2147483648.0 / a, dt = 2147483648.0 / d;
	env.Trigger();
	int t = 0, peakAt = -1, endAt = -1, prev = 0;
	bool monoUp = true, monoDown = true;
	while (t < 1000000)
	{
		int o = env.Tick(a, d, 2048);
		if (peakAt < 0) { if (o < prev) monoUp = false; if (o == 32767) peakAt = t; }
		else { if (o > prev) monoDown = false; if (o == 0 && env.stage == Envelope::Idle) { endAt = t; break; } }
		prev = o;
		t++;
	}
	CHECK(monoUp && monoDown, "envelope rises then falls");
	CHECK(fabs(peakAt - at) < 3, "attack %d samples, expected %.0f", peakAt, at);
	CHECK(fabs(endAt - peakAt - dt) < 3, "decay %d samples, expected %.0f", endAt - peakAt, dt);
	// Pluckiness: at a quarter of the way through the decay, a pluck has
	// fallen further than a soft decay
	int32_t soft = Envelope::Bend(24576, 0), mid = Envelope::Bend(24576, 2048), pluck = Envelope::Bend(24576, 4095);
	CHECK(soft == 24576, "pluck 0 is a straight line");
	CHECK(soft > mid && mid > pluck, "pluckier decays fall faster: %d %d %d", soft, mid, pluck);
	CHECK(pluck < 3500, "full pluck drops to x^8 (%d)", pluck);
	CHECK(Envelope::Bend(32767, 4095) >= 32760 && Envelope::Bend(0, 4095) == 0, "bend keeps the ends");
	// Retrigger climbs from where it is: no jump
	Envelope r;
	for (int i = 0; i < 2000; i++) r.Tick(a, d, 0);
	int before = r.out;
	r.Trigger();
	int after = r.Tick(a, d, 0);
	CHECK(after >= before && after - before < 200, "retrigger from %d jumped to %d", before, after);
}

//----------------------------------------------------------------------------
// Slew on the pitch
static void TestSlew()
{
	Engine e;
	int n[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, Notes, n);
	int o[8] = {0, 0, 100, 0, 0, 0, 0, 0}; // 0V
	SetPage(e, Octaves, o);
	RunSteps(e, 2);
	CHECK(e.cvMv[0] == 0, "root at 0V: %d", e.cvMv[0]);
	// jump to +5V with no slew: immediate
	int o2[8] = {0, 0, 0, 0, 0, 0, 0, 100};
	SetPage(e, Octaves, o2);
	RunSteps(e, 1);
	e.Tick(false);
	CHECK(e.cvMv[0] == 5000, "no slew: straight to 5V (%d)", e.cvMv[0]);
	// with 100ms slew: 63% of the way after 100ms
	e.slewAlpha = int32_t(16777216.0 / (0.1 * 48000));
	SetPage(e, Octaves, o);
	RunSteps(e, 1);
	for (int i = 0; i < 4800; i++) e.Tick(false);
	CHECK(abs(e.cvMv[0] - 1839) < 60, "slew 100ms: %d mV after 100ms", e.cvMv[0]);

	// Ties only: a note after a tie glides, other notes jump
	Engine t;
	int n1[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(t, Notes, n1);
	int r1[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(t, Ratchets, r1);
	int lo[8] = {0, 0, 100, 0, 0, 0, 0, 0}, hi[8] = {0, 0, 0, 0, 0, 0, 0, 100};
	int plain[8] = {0, 0, 0, 100, 0, 0, 0, 0}, tied[8] = {0, 0, 0, 0, 0, 0, 0, 100};
	t.slewAlpha = int32_t(16777216.0 / (0.1 * 48000));
	t.slewTiesOnly = true;
	SetPage(t, Octaves, lo);
	SetPage(t, Gates, plain);
	RunSteps(t, 2);
	SetPage(t, Octaves, hi);
	RunSteps(t, 1);
	t.Tick(false);
	CHECK(t.cvMv[0] == 5000, "ties only: an untied note jumps (%d)", t.cvMv[0]);
	// now tie into the next note
	SetPage(t, Gates, tied);
	RunSteps(t, 1);                 // a tied note at +5V
	SetPage(t, Octaves, lo);
	RunSteps(t, 1);                 // the next note, 0V, comes in through the tie
	for (int i = 0; i < 4800; i++) t.Tick(false);
	CHECK(t.voice[0].glide && abs(t.cvMv[0] - (5000 - 3161)) < 80, "ties only: the note after a tie glides (%d)", t.cvMv[0]);
	// and with the knob right of centre every note glides
	t.slewTiesOnly = false;
	SetPage(t, Gates, plain);
	RunSteps(t, 2);
	SetPage(t, Octaves, hi);
	RunSteps(t, 1);
	t.Tick(false);
	CHECK(t.cvMv[0] < 200, "right of centre: untied notes glide too (%d)", t.cvMv[0]);
}

//----------------------------------------------------------------------------
// Voice 2: shares each first page while its second page is all zero
static void TestSharing()
{
	Engine e;
	// Shared at the start: both voices draw from page 1
	for (int p = 0; p < kPairs; p++) CHECK(e.Shared(p), "pair %d shared by default", p);
	// different lines from the same probabilities
	int same = 0;
	for (int s = 0; s < 200; s++)
	{
		RunSteps(e, 1);
		if (e.voice[0].targetMv == e.voice[1].targetMv) same++;
	}
	CHECK(same < 120, "shared voices play different lines (%d of 200 the same)", same);

	// Page 1 notes only fader 2 (voice 1), page 2 notes only fader 6
	int n1[8] = {0, 100, 0, 0, 0, 0, 0, 0}, n2[8] = {0, 0, 0, 0, 0, 100, 0, 0};
	SetPage(e, Notes, n1);
	for (int s = 0; s < 50; s++)
	{
		RunSteps(e, 1);
		CHECK(e.voice[0].noteFader == 1 && e.voice[1].noteFader == 1, "shared notes: %d %d", e.voice[0].noteFader, e.voice[1].noteFader);
	}
	SetPage(e, kPairs + Notes, n2);
	CHECK(!e.Shared(Notes) && e.Shared(Octaves), "notes independent, octaves shared");
	int o1[8] = {0, 0, 100, 0, 0, 0, 0, 0};
	SetPage(e, Octaves, o1);
	for (int s = 0; s < 50; s++)
	{
		RunSteps(e, 1);
		CHECK(e.voice[0].noteFader == 1 && e.voice[1].noteFader == 5, "independent notes: %d %d", e.voice[0].noteFader, e.voice[1].noteFader);
		CHECK(e.voice[0].octaveFader == 2 && e.voice[1].octaveFader == 2, "octaves still shared");
	}
	// A fader at 1 or 2 on a second page still counts as zero
	ClearPage(e, kPairs + Notes);
	e.faders[kPairs + Notes][3] = 2;
	CHECK(e.Shared(Notes), "near-zero second page still shared");
	RunSteps(e, 1);
	CHECK(e.voice[1].noteFader == 1, "voice 2 back on the first page");
	// Ratchets and gates on voice 2's own pages
	int r2[8] = {0, 0, 0, 100, 0, 0, 0, 0}, g2[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, kPairs + Ratchets, r2);
	SetPage(e, kPairs + Gates, g2);
	int r1[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, Ratchets, r1);
	for (int s = 0; s < 20; s++)
	{
		RunSteps(e, 1);
		CHECK(e.voice[0].ratchetFader == 0 && e.voice[1].ratchetFader == 3, "ratchets per voice");
		CHECK(e.voice[1].gateFader == 0, "voice 2 gates");
	}
	// Notes ignore faders past the end of a short scale
	e.scale = ScaleNamed("Fifths"); // 2 notes
	int n3[8] = {0, 0, 0, 0, 0, 0, 0, 127};
	SetPage(e, Notes, n3);
	for (int s = 0; s < 100; s++) { RunSteps(e, 1); CHECK(e.voice[0].noteFader < 2, "short scale: fader %d", e.voice[0].noteFader); }
	// Eight-note scales: every note can come up, one per fader
	e.scale = ScaleNamed("Subharmonic series");
	ClearPage(e, Notes);
	bool seen[8] = {};
	for (int s = 0; s < 400; s++) { RunSteps(e, 1); seen[e.voice[0].note] = true; CHECK(e.voice[0].note == e.voice[0].noteFader, "one note per fader"); }
	int all = 0;
	for (int k = 0; k < 8; k++) all += seen[k];
	CHECK(all == 8, "subharmonic series: %d of 8 notes played", all);
}

//----------------------------------------------------------------------------
// The 8mu's pages, pickup and LEDs
static void TestUi()
{
	CHECK(EightMuUi::NextPage(0, 0) == 4 && EightMuUi::NextPage(4, 0) == 8 && EightMuUi::NextPage(8, 0) == 12 && EightMuUi::NextPage(12, 0) == 0,
		"A: NOTES, NOTES 2, ATTACK, ATTACK 2, NOTES");
	CHECK(EightMuUi::NextPage(0, 1) == 1 && EightMuUi::NextPage(5, 1) == 9 && EightMuUi::NextPage(13, 2) == 2 && EightMuUi::NextPage(14, 3) == 3, "other button: its first page");
	CHECK(EightMuUi::NextPage(3, 3) == 7 && EightMuUi::NextPage(7, 3) == 11 && EightMuUi::NextPage(11, 3) == 15 && EightMuUi::NextPage(15, 3) == 3, "D: RATCHETS ... DEPTH 2");
	// Page numbers and kinds
	CHECK(KindOf(8) == Attack && KindOf(9) == Decay && KindOf(10) == Pluck && KindOf(11) == Depth && KindOf(15) == Depth && KindOf(6) == Gates, "kinds");
	CHECK(FirstPage(Attack) == 8 && SecondPage(Attack) == 12 && SecondPage(Notes) == 4 && FirstPage(Depth) == 11, "pages");
	CHECK(VoiceOfPage(12) == 1 && VoiceOfPage(8) == 0 && VoiceOfPage(5) == 1, "voice of page");

	Engine e;
	EightMuUi ui;
	int32_t f[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	bool none[4] = {}, a[4] = {true, false, false, false}, b[4] = {false, true, false, false};
	uint8_t stored1 = e.faders[Notes][0]; // 127 by default
	// Pickup: fader 1 at 0 does nothing until it reaches the stored value
	ui.Update(e, f, none);
	CHECK(e.faders[Notes][0] == stored1 && !ui.Latched(0), "fader not picked up");
	f[0] = 60; ui.Update(e, f, none);
	CHECK(e.faders[Notes][0] == stored1, "moving below the value: still not picked up");
	f[0] = 125; ui.Update(e, f, none);
	CHECK(ui.Latched(0) && e.faders[Notes][0] == 125, "picked up near the value");
	f[0] = 40; ui.Update(e, f, none);
	CHECK(e.faders[Notes][0] == 40, "then follows");
	// faders already sitting at their value latch at once; one crossing it
	// between two readings too
	f[2] = 70; ui.Update(e, f, none); // stored 80: crossing? no, from 0 to 70
	CHECK(!ui.Latched(2), "fader 3 not there yet");
	f[2] = 90; ui.Update(e, f, none);
	CHECK(ui.Latched(2) && e.faders[Notes][2] == 90, "crossed the value: picked up");
	// A button: the page changes and every fader must pick up again
	ui.Update(e, f, a);  // A pressed: NOTES 2
	CHECK(ui.page == 4, "A again: NOTES 2 (%d)", ui.page);
	ui.Update(e, f, none);
	CHECK(!ui.Latched(0) && e.faders[4][0] == 0, "new page: unlatched; NOTES 2 untouched");
	ui.Update(e, f, b);
	CHECK(ui.page == 1, "B: OCTAVES");
	ui.Update(e, f, none);

	// LEDs: probabilities for 3s after a touch, then what's playing
	ui.SetPage(Notes);
	int32_t led[8];
	f[5] = 10; ui.Update(e, f, none); // a touch
	CHECK(ui.showProbs > 0, "touch shows probabilities");
	int n[8] = {100, 50, 0, 25, 0, 0, 0, 127};
	SetPage(e, Notes, n);
	e.scale = ScaleNamed("Major"); // fader 8 ignored
	ui.Leds(e, led);
	CHECK(led[0] == 4095 && led[1] == 2047 && led[2] == 0 && led[3] == 1023, "brightness = probability: %d %d %d %d", led[0], led[1], led[2], led[3]);
	CHECK(led[7] == 0, "ignored fader dark (%d)", led[7]);
	// ...then after 3 seconds the playing note
	for (int i = 0; i < EightMuUi::kShowTicks; i++) ui.Update(e, f, none);
	CHECK(ui.showProbs == 0, "probabilities time out after 3s");
	e.voice[0].noteFader = 3;
	e.voice[1].noteFader = 1;
	ui.Leds(e, led);
	CHECK(led[3] == 4095, "playing note lit");
	CHECK(led[1] == EightMuUi::kDim, "voice 2 shares: its note dim (%d)", led[1]);
	CHECK(led[0] == 0 && led[2] == 0, "others dark");
	// voice 2 independent: not shown on the first page
	e.faders[kPairs + Notes][4] = 100;
	ui.Leds(e, led);
	CHECK(led[1] == 0, "independent voice 2 not on the first page");
	ui.SetPage(kPairs + Notes);
	ui.Leds(e, led);
	CHECK(led[1] == 4095 && led[3] == 0, "second page shows voice 2");
	// Gates: a bar
	ui.SetPage(Gates);
	e.voice[0].gateFader = 3;
	ui.Leds(e, led);
	CHECK(led[0] == 4095 && led[3] == 4095 && led[4] == 0, "gate bar to fader 4");
	e.voice[0].gateFader = 0;
	e.voice[1].gateFader = 0;
	ui.Leds(e, led);
	CHECK(led[0] > 0 && led[0] < 4095 && led[1] == 0, "rest: first LED dim (%d)", led[0]);
	e.voice[0].gateFader = 7;
	ui.Leds(e, led);
	bool allLit = true;
	for (int i = 0; i < 8; i++) if (led[i] != 4095) allLit = false;
	CHECK(allLit, "tied: all lit");
	// All-zero pages: a first page means all equal, a second page unused
	ClearPage(e, Octaves);
	ui.SetPage(Octaves);
	ui.showProbs = 100;
	ui.Leds(e, led);
	CHECK(led[0] == 4095 && led[7] == 4095, "all-zero first page: all equally likely");
	ui.SetPage(kPairs + Octaves);
	ui.Leds(e, led);
	CHECK(led[0] == 0 && led[7] == 0, "all-zero second page: dark");
	// Ratchets flash at their own speed in time with the step
	ui.SetPage(Ratchets);
	int r[8] = {100, 100, 100, 100, 0, 0, 0, 100};
	SetPage(e, Ratchets, r);
	e.stepLen = 24000;
	int onCount[8] = {};
	for (int t = 0; t < 24000; t += 10)
	{
		e.sinceStep = t;
		ui.Leds(e, led);
		for (int i = 0; i < 8; i++) if (led[i]) onCount[i]++;
	}
	int fl = EightMuUi::FlashLength(24000) / 10;
	CHECK(abs(onCount[0] - fl) <= 1 && abs(onCount[1] - 2 * fl) <= 1 && abs(onCount[2] - 2 * fl) <= 1 && abs(onCount[3] - 3 * fl) <= 1,
		"flash counts %d %d %d %d (flash %d)", onCount[0], onCount[1], onCount[2], onCount[3], fl);
	CHECK(onCount[4] == 0 && abs(onCount[7] - 4 * fl) <= 1, "zero faders don't flash; fader 8 flashes 4x (%d)", onCount[7]);
	// ...and after 3s, the ratchet in use flashes at each hit
	ui.showProbs = 0;
	e.voice[0].ratchetFader = 2;
	e.voice[0].sinceHit = 10;
	ui.Leds(e, led);
	CHECK(led[2] == 4095, "ratchet in use flashes");
	e.voice[0].sinceHit = 20000;
	ui.Leds(e, led);
	CHECK(led[2] == 0, "between hits, dark");
}

//----------------------------------------------------------------------------
// The envelope pages: ATTACK, DECAY, PLUCK and DEPTH
static int32_t PeakOfNextStep(Engine &e)
{
	RunSteps(e, 1);
	int32_t peak = 0;
	uint8_t c = e.stepCount;
	while (e.stepCount == c) { e.Tick(false); if (e.audio[0] > peak) peak = e.audio[0]; }
	return peak;
}

static void TestEnvelopePages()
{
	Engine e;
	int g[8] = {0, 0, 0, 100, 0, 0, 0, 0};
	SetPage(e, Gates, g);
	int r[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, Ratchets, r);
	e.attackInc = AttackFromKnob(2048);
	e.decayInc = DecayFromKnob(2048);
	e.pluck = 2048;
	// All zero: off, the knobs' envelope exactly, at full depth
	RunSteps(e, 2);
	for (int k = 0; k < kEnvKinds; k++) CHECK(e.voice[0].dev[k] == -1 && e.voice[1].dev[k] == -1, "envelope page %d off by default", k);
	e.Tick(false);
	CHECK(e.voice[0].attackInc == e.attackInc && e.voice[0].decayInc == e.decayInc, "no deviation: the knobs' rates");
	CHECK(PeakOfNextStep(e) >= 1700, "full depth by default");
	// ATTACK fader 8: four times as long; fader 1: a quarter
	int hi[8] = {0, 0, 0, 0, 0, 0, 0, 100}, lo[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, FirstPage(Attack), hi);
	RunSteps(e, 1); e.Tick(false);
	double ratio = double(e.attackInc) / e.voice[0].attackInc;
	CHECK(e.voice[0].dev[0] == 7 && ratio > 3.9 && ratio < 4.1, "ATTACK fader 8: x4 the time (%.2f)", ratio);
	SetPage(e, FirstPage(Attack), lo);
	RunSteps(e, 1); e.Tick(false);
	ratio = double(e.voice[0].attackInc) / e.attackInc;
	CHECK(e.voice[0].dev[0] == 0 && ratio > 3.9 && ratio < 4.1, "ATTACK fader 1: a quarter of the time (%.2f)", ratio);
	// ...kept within the attack's range
	e.attackInc = AttackFromKnob(0);
	e.Tick(false);
	CHECK(e.voice[0].attackInc == AttackFromKnob(0), "no faster than the fastest attack");
	e.attackInc = AttackFromKnob(2048);
	ClearPage(e, FirstPage(Attack));
	// DECAY: each fader its own spread, 4 and 5 a little either side
	int d5[8] = {0, 0, 0, 0, 100, 0, 0, 0};
	SetPage(e, FirstPage(Decay), d5);
	RunSteps(e, 1); e.Tick(false);
	ratio = double(e.decayInc) / e.voice[0].decayInc;
	CHECK(e.voice[0].dev[1] == 4 && ratio > 1.2 && ratio < 1.25, "DECAY fader 5: a little longer (%.3f)", ratio);
	// A knob change is heard straight away, with the same deviation
	e.decayInc = DecayFromKnob(1000);
	e.Tick(false);
	ratio = double(e.decayInc) / e.voice[0].decayInc;
	CHECK(ratio > 1.2 && ratio < 1.25, "knob change keeps the deviation (%.3f)", ratio);
	ClearPage(e, FirstPage(Decay));
	// PLUCK: drawn like the others
	int p1[8] = {0, 100, 0, 0, 0, 0, 0, 0};
	SetPage(e, FirstPage(Pluck), p1);
	RunSteps(e, 1);
	CHECK(e.voice[0].dev[2] == 1 && e.voice[1].dev[2] == 1, "PLUCK fader 2 for both voices");
	ClearPage(e, FirstPage(Pluck));
	// DEPTH: fader 1 an eighth, fader 8 full, faders 1 and 8 equally likely
	int dep[8] = {100, 0, 0, 0, 0, 0, 0, 0};
	SetPage(e, FirstPage(Depth), dep);
	int32_t pk = PeakOfNextStep(e);
	CHECK(pk > 200 && pk <= 214, "DEPTH fader 1: an eighth (%d)", pk);
	int dep2[8] = {100, 0, 0, 0, 0, 0, 0, 100};
	SetPage(e, FirstPage(Depth), dep2);
	int low = 0, full = 0;
	for (int i = 0; i < 200; i++) { pk = PeakOfNextStep(e); if (pk <= 214) low++; else if (pk >= 1700) full++; }
	CHECK(low + full == 200 && low > 70 && full > 70, "DEPTH 1 or 8: %d low, %d full", low, full);
	// Voice 2: shares ATTACK until ATTACK 2 has a fader up
	SetPage(e, FirstPage(Attack), hi);
	RunSteps(e, 1);
	CHECK(e.voice[0].dev[0] == 7 && e.voice[1].dev[0] == 7, "voice 2 shares ATTACK");
	SetPage(e, SecondPage(Attack), lo);
	CHECK(!e.Shared(Attack) && e.Shared(Decay), "ATTACK 2 in use, DECAY 2 still shared");
	for (int i = 0; i < 20; i++)
	{
		RunSteps(e, 1);
		CHECK(e.voice[0].dev[0] == 7 && e.voice[1].dev[0] == 0, "independent ATTACK: %d %d", e.voice[0].dev[0], e.voice[1].dev[0]);
	}
	// An ATTACK page off for voice 1 but not voice 2
	ClearPage(e, FirstPage(Attack));
	RunSteps(e, 1);
	CHECK(e.voice[0].dev[0] == -1 && e.voice[1].dev[0] == 0, "ATTACK off, ATTACK 2 on");
	// Drawn with the note: held ratchets keep it through the step; new-note
	// ratchets draw again at each hit
	ClearPage(e, SecondPage(Attack));
	int spread[8] = {100, 100, 100, 100, 100, 100, 100, 100};
	SetPage(e, FirstPage(Attack), spread);
	for (int held = 0; held < 2; held++)
	{
		int rr[8] = {};
		rr[held ? 3 : 7] = 100;   // 3x on one note, or 4x new notes
		SetPage(e, Ratchets, rr);
		RunSteps(e, 1);
		int changes = 0;
		for (int s = 0; s < 30; s++)
		{
			uint8_t c = e.stepCount;
			int d = e.voice[0].dev[0];
			while (true) { e.Tick(false); if (e.stepCount != c) break; if (e.voice[0].dev[0] != d) { changes++; d = e.voice[0].dev[0]; } }
		}
		if (held) CHECK(changes == 0, "held ratchet changed its envelope within a step (%d)", changes);
		else CHECK(changes > 30, "new-note ratchets draw new envelopes (%d)", changes);
	}

	// The 8mu's LEDs on an envelope page
	EightMuUi ui;
	int32_t led[8];
	Engine f;
	ui.SetPage(FirstPage(Decay));
	ui.showProbs = 100;
	ui.Leds(f, led);
	bool dark = true;
	for (int i = 0; i < 8; i++) if (led[i]) dark = false;
	CHECK(dark, "an envelope page all at zero is dark (off)");
	int dd[8] = {0, 50, 0, 0, 0, 0, 0, 100};
	SetPage(f, FirstPage(Decay), dd);
	ui.Leds(f, led);
	CHECK(led[7] == 4095 && led[1] == 2047 && led[0] == 0, "probabilities %d %d", led[7], led[1]);
	ui.showProbs = 0;
	f.voice[0].dev[1] = 1;
	f.voice[1].dev[1] = 7;
	ui.Leds(f, led);
	CHECK(led[1] == 4095 && led[7] == EightMuUi::kDim, "playing: voice 1's decay lit, voice 2's dim (%d %d)", led[1], led[7]);
	f.voice[0].dev[1] = -1;
	f.voice[1].dev[1] = -1;
	ui.Leds(f, led);
	CHECK(led[1] == 0 && led[7] == 0, "nothing lit with no deviation");
	ui.SetPage(SecondPage(Depth));
	f.voice[1].dev[3] = 2;
	ui.Leds(f, led);
	CHECK(led[2] == 4095, "DEPTH 2 shows voice 2's depth");
}

int main()
{
	TestChoose();
	TestScales();
	TestKnobs();
	TestInternalClock();
	TestExternalClock();
	TestRatchets();
	TestGates();
	TestEnvelope();
	TestSlew();
	TestSharing();
	TestUi();
	TestEnvelopePages();
	printf("engine: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
