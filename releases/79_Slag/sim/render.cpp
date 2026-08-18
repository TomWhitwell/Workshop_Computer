// render.cpp — offline renderer + self-check for Slag.
//
// Compiles the REAL src/slag.cpp against a mock hardware layer, asserts the
// sequencer actually does what the panel claims (locking repeats, Off doesn't,
// Spread=0 freezes the timbre, Chance=0 silences, the button hits, the
// outputs stay split), then renders demo WAVs you can listen to
// without a Workshop Computer.
//
//   Audio Out 1 (metal) -> left channel,  Audio Out 2 (kick) -> right channel.
//
// Build & run:  ./build.sh

#include "computercard_mock.h"   // must come first: fakes the hardware layer
#include "../src/slag.cpp"       // the actual card under test (its main() is skipped)

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

static constexpr int SR = 48000;

#define CHECK(cond, ...) do { \
	if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); exit(1); } \
	else         { printf("  ok:   " __VA_ARGS__); printf("\n"); } } while (0)

// ---------- tiny WAV writer (16-bit PCM, interleaved) ----------
static void writeWav(const std::string &path, const std::vector<int16_t> &samples,
                     int channels = 2, int sr = SR)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) { printf("  ! could not open %s for writing\n", path.c_str()); return; }
	uint32_t dataBytes = (uint32_t)(samples.size() * 2);
	uint32_t byteRate  = (uint32_t)(sr * channels * 2);
	uint16_t blockAlign = (uint16_t)(channels * 2);
	uint32_t riff = 36 + dataBytes, fmtsz = 16, srate = sr;
	uint16_t fmt = 1, ch = (uint16_t)channels, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); fwrite(&fmtsz, 4, 1, f); fwrite(&fmt, 2, 1, f);
	fwrite(&ch, 2, 1, f); fwrite(&srate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
	fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
	fwrite(samples.data(), 2, samples.size(), f);
	fclose(f);
}

// Normalise interleaved card output (each +/-2047) to -3 dBFS and write.
static void finalize(const std::string &path, const std::vector<int32_t> &raw)
{
	int32_t peak = 1;
	for (int32_t v : raw) { int32_t a = v < 0 ? -v : v; if (a > peak) peak = a; }
	std::vector<int16_t> out(raw.size());
	for (size_t i = 0; i < raw.size(); i++)
		out[i] = (int16_t)((int64_t)raw[i] * 23197 / peak);   // -3 dBFS
	writeWav(path, out);
	printf("  wrote %s (card peak %d/2047)\n", path.c_str(), peak);
}

// ============================ rig ============================

static void run(Slag &c, int n, std::vector<int32_t> *cap = nullptr)
{
	for (int i = 0; i < n; i++)
	{
		c.simStep();
		if (cap) { cap->push_back(c.simAudioOut[0]); cap->push_back(c.simAudioOut[1]); }
	}
}

// Drive a knob bank through the card's soft takeover: park the knobs far from
// the target so entering the bank latches that, then move to the target, which
// is more than the takeover threshold away.
static void setBank(Slag &c, ComputerCard::Switch sw, int32_t m, int32_t x, int32_t y)
{
	int32_t t[3] = {m, x, y};
	c.simSwitch = sw;
	for (int k = 0; k < 3; k++) c.simKnob[k] = (t[k] < 2048) ? 4095 : 0;
	run(c, 128);
	for (int k = 0; k < 3; k++) c.simKnob[k] = t[k];
	run(c, 128);
}

struct StepObs { int32_t cv; bool fired; };
static bool operator==(const StepObs &a, const StepObs &b) { return a.cv == b.cv && a.fired == b.fired; }

// CV Out 1 is sampled and held, so a skipped step still shows whatever the last
// step that fired left there. When the run-up differs, only compare the steps
// that actually decided a value.
static bool sameStep(const StepObs &a, const StepObs &b)
{
	return a.fired == b.fired && (!a.fired || a.cv == b.cv);
}

// One external clock tick, sampled just after the edge, then the rest of the
// step's period. Returns what the card decided for that step.
static StepObs stepOnce(Slag &c, int period = 6000, std::vector<int32_t> *cap = nullptr)
{
	c.simPulse[0] = true;
	run(c, 8, cap);
	StepObs o { c.simCVOut[0], c.simPulseOut[0] };
	run(c, 40, cap);
	c.simPulse[0] = false;
	run(c, period - 48, cap);
	return o;
}

// A card set up for sequencer tests: external clock, nothing else patched.
static void clocked(Slag &c)
{
	c.simConnected[ComputerCard::Pulse1] = true;
	run(c, 64);
}

// Count Trigger-out rising edges over n samples.
static int countTriggers(Slag &c, int n)
{
	int count = 0; bool prev = c.simPulseOut[0];
	for (int i = 0; i < n; i++)
	{
		c.simStep();
		if (c.simPulseOut[0] && !prev) count++;
		prev = c.simPulseOut[0];
	}
	return count;
}

// Press a "key": jump the pitch input to v, then read the envelope a fixed time
// later. That reading is a fingerprint of the hit — it depends only on the
// decay the roll chose — so the same voltage must give the same number back.
static int32_t pressKey(Slag &c, int32_t v, int *triggers = nullptr)
{
	c.simCVIn[0] = v;
	// A whole number of control ticks (32 samples), so every press samples the
	// decay at the same phase — otherwise the reading drifts by a count or two
	// and two identical hits look different.
	int n = countTriggers(c, 3072);
	if (triggers) *triggers = n;
	return c.simCVOut[1];
}

// Leave a held bank set, and go back to Middle so the voice knobs are live.
static void setHeldBank(Slag &c, int32_t m, int32_t x, int32_t y)
{
	setBank(c, ComputerCard::Down, m, x, y);
	c.simSwitch = ComputerCard::Middle;
	run(c, 128);
}

static double mean(const std::vector<int32_t> &v, size_t stride, size_t off)
{
	double acc = 0; size_t n = 0;
	for (size_t i = off; i < v.size(); i += stride) { acc += v[i]; n++; }
	return n ? acc / (double)n : 0.0;
}

static double rmsWin(const std::vector<int32_t> &v, size_t stride, size_t off,
                     size_t skip, size_t n)
{
	double acc = 0; size_t c = 0;
	for (size_t i = off + skip * stride; i < v.size() && c < n; i += stride)
		{ acc += (double)v[i] * v[i]; c++; }
	return c ? sqrt(acc / c) : 0.0;
}

static double rms(const std::vector<int32_t> &v, size_t stride, size_t off)
{
	double acc = 0; size_t n = 0;
	for (size_t i = off; i < v.size(); i += stride) { acc += (double)v[i] * v[i]; n++; }
	return n ? sqrt(acc / (double)n) : 0.0;
}

// Steps knob position at the centre of each zone. Mirrors the card's ZB[] table
// — keep in sync. Zones: 0=Off 1=2 2=3 3=4 4=5 5=7 6=8 7=10 8=16 9=32
static int32_t stepsKnob(int zone)
{
	static const int32_t ZB[11] = {0,410,819,1229,1638,2048,2458,2867,3277,3686,4096};
	return (ZB[zone] + ZB[zone + 1]) / 2;
}

// ---------- is this percussion, or a beep? ----------
// Spectral flatness (geometric mean / arithmetic mean of the power spectrum) is
// near 0 for a pure tone and near 1 for white noise. A chord of square waves —
// which is what six summed oscillators is — sits low. Struck metal sits high,
// because its partials are dense and inharmonic enough to smear into noise.
// `body` is the share of the energy under 250Hz — thump, as opposed to the
// mid-band rattle that makes a metal voice read as a snare.
struct Spectrum { double flatness, centroid, body; };

static Spectrum analyse(const std::vector<int32_t> &v, size_t stride, size_t off,
                        size_t skip = 256, size_t n = 2048)
{
	std::vector<double> x;
	for (size_t i = off + skip * stride; i < v.size() && x.size() < n; i += stride)
		x.push_back((double)v[i]);
	if (x.size() < n) return {0.0, 0.0, 0.0};

	double logSum = 0, sum = 0, fSum = 0, lowSum = 0;
	int bins = 0;
	for (size_t k = 1; k < n / 2; k++)             // skip DC
	{
		double re = 0, im = 0;
		for (size_t t = 0; t < n; t++)
		{
			double a = 2.0 * M_PI * (double)k * (double)t / (double)n;
			double w = 0.5 - 0.5 * cos(2.0 * M_PI * t / (double)(n - 1));   // Hann
			re += x[t] * w * cos(a);
			im -= x[t] * w * sin(a);
		}
		double p = (re * re + im * im) / (double)(n * n) + 1e-12;
		double hz = (double)k * SR / (double)n;
		logSum += log(p); sum += p;
		fSum   += p * hz;
		if (hz < 250.0) lowSum += p;
		bins++;
	}
	return { exp(logSum / bins) / (sum / bins), fSum / sum, lowSum / sum };
}

// One hit, captured. Returns interleaved out1/out2 starting at the attack.
static std::vector<int32_t> oneHit(int32_t pitch, int32_t decay, int32_t rust,
                                   int32_t tone, int32_t metal)
{
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 0);   // always fire, no spread
	setBank(c, ComputerCard::Middle, pitch, decay, rust);
	setHeldBank(c, 0, tone, metal);

	std::vector<int32_t> cap;
	stepOnce(c, 24000, &cap);   // long enough to have a tail to measure
	return cap;
}

static double clipPct(const std::vector<int32_t> &v, size_t stride, size_t off)
{
	size_t n = 0, c = 0;
	for (size_t i = off; i < v.size(); i += stride)
	{
		int32_t a = v[i] < 0 ? -v[i] : v[i];
		if (a >= 2047) c++;
		n++;
	}
	return n ? 100.0 * c / n : 0.0;
}

static void reportCrossfade()
{
	printf("\nlevel across the Tone crossfade (send, pre-rust)\n");
	for (int k = 0; k <= 4095; k += 585)
	{
		std::vector<int32_t> hit = oneHit(2400, 2400, 0, k, 4095);
		printf("  tone %4d   rms %6.0f\n", k, rms(hit, 2, 0));
	}
}

static void reportVoice()
{
	printf("\nvoice character (flatness 0=pure tone, 1=noise)\n");
	struct Case { const char *name; int32_t pitch, decay, rust, tone, metal; };
	static const Case CASES[] = {
		{"low pitch, tight metal          ", 1200, 2400,   0,    0,    0},
		{"mid, clean, no noise, max metal ", 2400, 2400,   0,    0, 4095},
		{"high, clean, no noise, max metal", 3400, 2400,   0,    0, 4095},
		{"mid pitch, tight metal          ", 2400, 2400,   0,    0,    0},
		{"mid, rusted, no noise, max metal", 2400, 2400, 3000,    0, 4095},
		{"mid, clean, half noise, max met ", 2400, 2400,   0, 2048, 4095},
	};
	for (const Case &t : CASES)
	{
		std::vector<int32_t> hit = oneHit(t.pitch, t.decay, t.rust, t.tone, t.metal);
		Spectrum s = analyse(hit, 2, 0);   // Out 1 = the metal
		// Thump lives in the attack, so measure the body share from sample zero
		// rather than from the settled part of the hit.
		Spectrum a = analyse(hit, 2, 1, 0);   // Out 2 = the kick
		printf("  %s  flat %.3f  cent %5.0f  body %4.1f%%  clip metal %4.1f%% kick %4.1f%%\n",
		       t.name, s.flatness, s.centroid, 100.0 * a.body,
		       clipPct(hit, 2, 0), clipPct(hit, 2, 1));
	}
	printf("\n");
}

// ============================ self-checks ============================

static void testLockRepeats()
{
	printf("[1] a locked pattern repeats, an unlocked one does not\n");

	// Steps = 8, Chance ~50%, Spread ~50% — so both the rhythm and the timbre
	// come from the same random stream and both should lock.
	Slag locked; clocked(locked);
	setBank(locked, ComputerCard::Up, stepsKnob(6), 1024, 2048);

	std::vector<StepObs> a;
	for (int i = 0; i < 24; i++) a.push_back(stepOnce(locked));

	// Third cycle against the second. Not the first: CV Out 1 is sampled and
	// held, so a step skipped during the opening cycle still shows the power-up
	// value rather than anything the pattern chose.
	int same = 0;
	for (int i = 16; i < 24; i++) if (a[i] == a[i - 8]) same++;
	CHECK(same == 8, "every step repeats exactly one cycle later (got %d/8)", same);

	// ...and it is a real pattern, not a constant one.
	int distinct = 0;
	for (int i = 9; i < 16; i++) if (!(a[i] == a[8])) distinct++;
	CHECK(distinct > 0, "the 8 steps are not all identical (%d differ from step 1)", distinct);

	// Same settings, Steps = Off: the stream never repeats.
	Slag free_; clocked(free_);
	setBank(free_, ComputerCard::Up, stepsKnob(0), 1024, 2048);

	std::vector<StepObs> b;
	for (int i = 0; i < 24; i++) b.push_back(stepOnce(free_));

	int freeSame = 0;
	for (int i = 16; i < 24; i++) if (b[i] == b[i - 8]) freeSame++;
	CHECK(freeSame < 8, "Steps=Off does not repeat at period 8 (%d/8 coincidences)", freeSame);
}

static void testResetRealignsPattern()
{
	printf("[2] reset restarts the locked pattern\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(3), 2048, 2048);   // 4 steps

	std::vector<StepObs> first;
	for (int i = 0; i < 4; i++) first.push_back(stepOnce(c));

	// Walk out of phase, then reset mid-pattern.
	stepOnce(c); stepOnce(c);
	c.simPulse[1] = true;  run(c, 8);
	c.simPulse[1] = false; run(c, 40);

	std::vector<StepObs> after;
	for (int i = 0; i < 4; i++) after.push_back(stepOnce(c));

	int match = 0;
	for (int i = 0; i < 4; i++) if (sameStep(after[i], first[i])) match++;
	CHECK(match == 4, "the 4 steps after reset match steps 1-4 (got %d/4)", match);
}

static void testSpreadZeroFreezesTimbre()
{
	printf("[3] Spread at zero stops the voice moving, Chance still thins it\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 1024, 0);      // Off, 50% chance, no spread

	std::vector<StepObs> o;
	for (int i = 0; i < 32; i++) o.push_back(stepOnce(c));

	int cvMoved = 0, fired = 0, ref = -1;
	for (auto &s : o)
	{
		if (!s.fired) continue;              // a skip holds the last CV, ignore it
		fired++;
		if (ref < 0) ref = s.cv; else if (s.cv != ref) cvMoved++;
	}
	CHECK(cvMoved == 0, "every hit has the same pitch CV (%d of %d differed)", cvMoved, fired);
	CHECK(fired > 4 && fired < 28, "the rhythm is still random (%d/32 steps fired)", fired);
}

static void testChanceExtremes()
{
	printf("[4] Chance at zero silences the card, and ghosts fire instead\n");
	Slag quiet; clocked(quiet);
	setBank(quiet, ComputerCard::Up, stepsKnob(0), 0, 2048);  // chance = 0

	std::vector<int32_t> cap;
	int ghosts = 0, hits = 0;
	for (int i = 0; i < 16; i++)
	{
		quiet.simPulse[0] = true;  run(quiet, 8, &cap);
		if (quiet.simPulseOut[1]) ghosts++;
		if (quiet.simPulseOut[0]) hits++;
		quiet.simPulse[0] = false; run(quiet, 5992, &cap);
	}
	CHECK(hits == 0,    "no step triggered (%d hits)", hits);
	CHECK(ghosts == 16, "every skipped step pulsed the ghost out (%d/16)", ghosts);
	CHECK(rms(cap, 2, 0) < 1.0, "the output is silent (rms %.3f)", rms(cap, 2, 0));

	// And at full Chance nothing is skipped.
	Slag loud; clocked(loud);
	setBank(loud, ComputerCard::Up, stepsKnob(0), 4095, 2048);
	int allHits = 0;
	for (int i = 0; i < 16; i++) if (stepOnce(loud).fired) allHits++;
	CHECK(allHits == 16, "Chance at full fires every step (%d/16)", allHits);
}

static void testButtonHits()
{
	printf("[5] tapping the switch down plays the card by hand\n");
	Slag c;
	c.simConnected[ComputerCard::Pulse1] = true;   // clocked, but never clocked
	setBank(c, ComputerCard::Up, stepsKnob(0), 0, 2048);   // Chance = 0: nothing can fire
	setBank(c, ComputerCard::Middle, 2048, 2000, 400);

	std::vector<int32_t> before;
	run(c, 4800, &before);
	CHECK(rms(before, 2, 0) < 1.0, "silent before the press (rms %.3f)", rms(before, 2, 0));

	std::vector<int32_t> after;
	c.simSwitch = ComputerCard::Down;
	run(c, 4800, &after);
	c.simSwitch = ComputerCard::Middle;
	CHECK(rms(after, 2, 0) > 20.0, "pressing it makes a hit despite Chance=0 (rms %.1f)", rms(after, 2, 0));
}

static void testHeldBankDoesNotDisturbTheVoice()
{
	printf("[7] the held bank does not yank the voice knobs on the way back\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 0);    // always fire, no spread
	setBank(c, ComputerCard::Middle, 3000, 2600, 500);
	int32_t before = stepOnce(c).cv;

	// Go and set Tempo/Tone/Metal with the knobs somewhere else entirely.
	setHeldBank(c, 1000, 3500, 200);

	int32_t after = stepOnce(c).cv;
	CHECK(before == after, "Pitch survived the trip (CV %d -> %d)", before, after);
}

static void testTempoRunsTheInternalClock()
{
	printf("[8] Tempo drives the internal clock, and fully anticlockwise stops it\n");
	Slag c;                                                 // no clock patched
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 1200); // always fire
	setHeldBank(c, 2048, 1200, 3000);                       // tempo knob at noon

	int mid = countTriggers(c, SR);
	CHECK(mid >= 3 && mid <= 5, "the knob at noon gives ~4 steps/sec (got %d)", mid);

	setHeldBank(c, 4095, 1200, 3000);
	int fast = countTriggers(c, SR);
	CHECK(fast > 25, "fully clockwise is much faster (got %d steps/sec)", fast);

	setHeldBank(c, 0, 1200, 3000);
	CHECK(countTriggers(c, SR) == 0, "fully anticlockwise, the clock stops");
}

static void testKeyboardOnPitchInput()
{
	printf("[9] with no clock patched, a jump on CV In 1 plays a note\n");
	Slag c;                                                 // no clock patched
	setBank(c, ComputerCard::Up, stepsKnob(0), 4095, 1500);
	setBank(c, ComputerCard::Middle, 2048, 3200, 500);      // long decay: readable tail
	setHeldBank(c, 0, 1200, 3000);                          // internal clock off

	CHECK(countTriggers(c, SR) == 0, "silent with the clock off and the CV still");

	// Four presses, alternating two voltages. Each is a jump, so each is a note.
	int n = 0, total = 0;
	int32_t a1 = pressKey(c, 400,  &n); total += n;
	int32_t b1 = pressKey(c, 1200, &n); total += n;
	int32_t a2 = pressKey(c, 400,  &n); total += n;
	int32_t b2 = pressKey(c, 1200, &n); total += n;
	CHECK(total == 4, "each jump played exactly one note (%d triggers)", total);
	CHECK(a1 == a2 && b1 == b2, "the same voltage replays the same hit (%d/%d, %d/%d)", a1, a2, b1, b2);
	// Two voltages can legitimately draw the same archetype, so sample a spread
	// of them and check the keyboard spans more than one kind of hit rather
	// than demanding any particular pair differ.
	int32_t f[5] = { a1, b1, pressKey(c, -300), pressKey(c, 700), pressKey(c, -900) };
	int distinct = 0;
	for (int i = 0; i < 5; i++)
	{
		bool dup = false;
		for (int j = 0; j < i; j++) if (f[j] == f[i]) dup = true;
		if (!dup) distinct++;
	}
	CHECK(distinct >= 3, "different voltages give different hits (%d distinct of 5)", distinct);

	// Patch a clock and the pitch input goes back to being only a pitch input.
	c.simConnected[ComputerCard::Pulse1] = true;
	run(c, 128);
	int32_t before = c.simCVIn[0];
	c.simCVIn[0] = before + 900;
	CHECK(countTriggers(c, 3000) == 0, "with a clock patched, a CV jump does not trigger");
}

static void testToneMixesMetalAgainstTheSource()
{
	printf("[10] Tone crossfades the metal bank against the noise/external source\n");
	Slag c; clocked(c);
	c.simConnected[ComputerCard::Audio1] = true;
	c.simAudioIn[0] = 1500;                                 // a DC "source" we can see
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 0);    // always fire, no spread
	setBank(c, ComputerCard::Middle, 2048, 3400, 0);        // long decay, no rust

	setHeldBank(c, 0, 4095, 3000);                          // Tone full: all source
	std::vector<int32_t> src;
	for (int i = 0; i < 4; i++) stepOnce(c, 6000, &src);
	CHECK(mean(src, 2, 0) > 400.0, "at full, the send follows the DC source (mean %.0f)", mean(src, 2, 0));

	setHeldBank(c, 0, 0, 3000);                             // Tone off: all metal
	std::vector<int32_t> mtl;
	for (int i = 0; i < 4; i++) stepOnce(c, 6000, &mtl);
	CHECK(mean(mtl, 2, 0) < 100.0, "at zero, the source is gone (mean %.0f)", mean(mtl, 2, 0));
	CHECK(rms(mtl, 2, 0) > 100.0, "...but the metal bank is still ringing (rms %.0f)", rms(mtl, 2, 0));
}

static void testOutputsAreSplit()
{
	printf("[6] the metal and the kick come out of separate jacks\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 0);      // always fire, no spread
	setBank(c, ComputerCard::Middle, 2400, 2400, 800);
	setHeldBank(c, 0, 1200, 3000);

	std::vector<int32_t> cap;
	for (int i = 0; i < 6; i++) stepOnce(c, 12000, &cap);

	CHECK(rms(cap, 2, 0) > 50.0, "Out 1 carries the metal (rms %.0f)", rms(cap, 2, 0));
	CHECK(rms(cap, 2, 1) > 50.0, "Out 2 carries the kick  (rms %.0f)", rms(cap, 2, 1));

	// The whole point of splitting them: each jack can take its own processing
	// without the other one going through it too.
	double kickLow  = analyse(cap, 2, 1, 0).body;
	double metalLow = analyse(cap, 2, 0, 0).body;
	CHECK(kickLow  > 0.90, "Out 2 is all low end     (%.0f%% under 250Hz)", 100 * kickLow);
	CHECK(metalLow < 0.10, "Out 1 has none of it     (%.0f%% under 250Hz)", 100 * metalLow);
}

static void testVoiceIsPercussiveNotTonal()
{
	printf("[11] Metal is a real tonal-to-atonal axis, and neither end is a beep\n");
	// Six *summed* squares measured 0.02-0.03 here — six discrete pitches, which
	// is a chord, which is a videogame. XOR ring-modulates them into a cloud with
	// no pitch at all, which overshot the other way. Metal now crossfades a bare
	// fundamental against that cloud, so this checks the axis rather than a
	// single floor: atonal at the top, pitched at the bottom, beeps at neither.
	double wideMid = analyse(oneHit(2400, 2400, 0, 0, 4095), 2, 0).flatness;
	double wideHi  = analyse(oneHit(3400, 2400, 0, 0, 4095), 2, 0).flatness;
	CHECK(wideMid > 0.30, "Metal at full is atonal, mid pitch  (%.3f > 0.30)", wideMid);
	CHECK(wideHi  > 0.30, "Metal at full is atonal, high pitch (%.3f > 0.30)", wideHi);

	double tightMid = analyse(oneHit(2400, 2400, 0, 0, 0), 2, 0).flatness;
	CHECK(tightMid < wideMid / 2.0,
	      "Metal at zero is audibly more pitched (%.3f vs %.3f)", tightMid, wideMid);
	CHECK(tightMid > 0.05,
	      "...but still metal, not the bare square it started as (%.3f > 0.05)", tightMid);

	// Rust must not undo it. Downsampling too far re-samples the dense XOR back
	// into a coarse periodic staircase, which sounded *more* tonal, not less.
	std::vector<int32_t> rusted = oneHit(2400, 2400, 3400, 0, 4095);
	double rf = analyse(rusted, 2, 0).flatness;
	CHECK(rf > 0.08, "hard rust stays gritty rather than collapsing to a tone (%.3f)", rf);
}

static void testRustHoldsItsLevel()
{
	printf("[12] Rust changes the character, not the level\n");
	// The old chain applied a fixed pre-gain and then hard-clipped, so winding
	// Rust up was really a volume knob. The Pade shaper carries a makeup gain of
	// 1/shaper(drive), so full scale comes out at full scale however hard it is
	// driven. Distortion still fills the waveform in, so RMS rises somewhat —
	// but it should be a character change, not a level jump.
	double lo = 1e30, hi = 0;
	for (int k = 0; k <= 4095; k += 585)
	{
		double r = rms(oneHit(2400, 2400, k, 0, 4095), 2, 0);
		if (r < lo) lo = r;
		if (r > hi) hi = r;
	}
	// Wider than it used to read, because this now measures the bare metal jack
	// rather than a mix that a master saturator was compressing on the way out.
	// The *peak* is still pinned by the makeup gain; RMS rising with drive is
	// just what distortion does, and 5dB across a full sweep is unremarkable.
	CHECK(hi / lo < 2.1, "level spread across the whole Rust knob is %.2fx", hi / lo);
}

static void testHitHasBody()
{
	printf("[14] every hit lands with a thump, not just a rattle\n");
	// At any useful Pitch setting the metal itself sits well above 250Hz, so
	// without the sub-octave body oscillator there is nothing down there to
	// feel at all — mid pitch measured 2.4% before it existed. Measured from
	// sample zero, because the thump is the attack.
	Spectrum tight = analyse(oneHit(2400, 2400, 0, 0,    0), 2, 1, 0);
	Spectrum wide  = analyse(oneHit(2400, 2400, 0, 0, 4095), 2, 1, 0);
	CHECK(tight.body > 0.10, "a pitched hit is %.0f%% under 250Hz (> 10%%)", 100 * tight.body);
	CHECK(wide.body  > 0.05, "even at full Metal there is body: %.0f%% (> 5%%)", 100 * wide.body);

	// And it is a kick, not a drone. It rings for ~270ms now, so the check is
	// that it decays, not that it is gone by 85ms.
	std::vector<int32_t> hit = oneHit(2400, 2400, 0, 0, 0);
	double early = rmsWin(hit, 2, 1, 0, 2048);
	double late  = rmsWin(hit, 2, 1, 20000, 2048);
	CHECK(late < early * 0.45, "the kick decays rather than drones (%.0f -> %.0f)", early, late);
}

static void testCrossfadeHoldsLevel()
{
	printf("[15] the Tone crossfade does not dip in the middle\n");
	// A linear blend of two uncorrelated signals loses 3dB at the centre — this
	// measured a 5dB hole at three-quarters before the constant-power curve.
	// The two ends legitimately differ (a full-scale square carries more RMS
	// than noise at the same peak), so the check is that nothing in between
	// falls below the quieter end.
	std::vector<double> pts;
	for (int k = 0; k <= 4095; k += 585)
		pts.push_back(rms(oneHit(2400, 2400, 0, k, 4095), 2, 0));

	double quieter = pts.front() < pts.back() ? pts.front() : pts.back();
	double dip = pts.front();
	for (double p : pts) if (p < dip) dip = p;
	CHECK(dip >= quieter * 0.98,
	      "no hole in the sweep: lowest %.0f vs quieter end %.0f", dip, quieter);
}

// Clock n steps and count every Trigger-out edge, including ratchet sub-hits.
static int clockAndCount(Slag &c, int steps, int period = 6000)
{
	int n = 0; bool prev = c.simPulseOut[0];
	for (int s = 0; s < steps; s++)
		for (int i = 0; i < period; i++)
		{
			c.simPulse[0] = (i < 8);
			c.simStep();
			if (c.simPulseOut[0] && !prev) n++;
			prev = c.simPulseOut[0];
		}
	return n;
}

static void testRatchetsSubdivideSteps()
{
	printf("[13] the top of Chance subdivides steps rather than just guaranteeing them\n");
	// A sequencer that only ever asks "does this step fire?" is a coin flip on a
	// grid. Past halfway, Chance stops buying density (there is none left to buy)
	// and starts buying subdivision.
	Slag plain; clocked(plain);
	setBank(plain, ComputerCard::Up, stepsKnob(0), 2048, 1200);   // exactly "all fire"
	int flat = clockAndCount(plain, 24);
	CHECK(flat == 24, "at the midpoint every step fires exactly once (%d/24)", flat);

	Slag rolled; clocked(rolled);
	setBank(rolled, ComputerCard::Up, stepsKnob(0), 4095, 1200);  // full ratchets
	int rolls = clockAndCount(rolled, 24);
	CHECK(rolls > 36, "fully clockwise, steps subdivide (%d hits from 24 steps)", rolls);

	// And a locked pattern locks its ratchets: the roll comes from the same hash
	// as everything else, so each repeat of the loop hits the same number of times.
	Slag locked; clocked(locked);
	setBank(locked, ComputerCard::Up, stepsKnob(3), 3600, 1200);  // 4 steps
	clockAndCount(locked, 4);                                     // align to the loop
	int a = clockAndCount(locked, 4), b = clockAndCount(locked, 4);
	CHECK(a == b, "each repeat of a locked loop ratchets identically (%d, %d)", a, b);
}

// How much the low end comes and goes over `n` steps — the spread between the
// most and least kick-heavy hit.
static double bodySwing(Slag &c, int n)
{
	double lo = 1e30, hi = 0;
	for (int i = 0; i < n; i++)
	{
		std::vector<int32_t> cap;
		stepOnce(c, 12000, &cap);
		double b = rms(cap, 2, 1);   // Out 2 = the kick, on its own jack
		if (b < lo) lo = b;
		if (b > hi) hi = b;
	}
	return hi > 0 ? (hi - lo) / hi : 0.0;
}

static void testHitsVaryInKind()
{
	printf("[16] steps are different kinds of hit, not one hit wobbling\n");
	// Nudging every parameter around a common centre gives every step a bit of
	// everything. Drawing an archetype per step means some land as kicks and
	// some as bare clangs — which is what makes a pattern read as a kit.
	Slag varied; clocked(varied);
	setBank(varied, ComputerCard::Up, stepsKnob(0), 2048, 4095);   // all fire, full spread
	setBank(varied, ComputerCard::Middle, 2400, 2400, 400);
	setHeldBank(varied, 0, 1600, 2200);
	double swing = bodySwing(varied, 12);
	CHECK(swing > 0.30, "the low end comes and goes between steps (%.0f%% swing)", 100 * swing);

	// ...and Spread at zero still gives the uniform voice it always did.
	Slag uniform; clocked(uniform);
	setBank(uniform, ComputerCard::Up, stepsKnob(0), 2048, 0);
	setBank(uniform, ComputerCard::Middle, 2400, 2400, 400);
	setHeldBank(uniform, 0, 1600, 2200);
	// Not zero: the oscillators free-run and the noise source is noise, so the
	// spectrum wobbles a little even when every parameter is identical. What
	// matters is that it is nowhere near the swing above.
	double flat = bodySwing(uniform, 12);
	CHECK(flat < 0.15, "at Spread zero the hits stay alike (%.0f%% swing)", 100 * flat);
	CHECK(swing > flat * 2.5, "and Spread genuinely opens that up (%.0f%% vs %.0f%%)",
	      100 * swing, 100 * flat);
}

static void testVoicesTakeTurns()
{
	printf("[17] the kick and the metal take turns rather than always firing together\n");
	// Varying their levels but retriggering both every step made them sound
	// welded. An archetype with a zero in it means that voice sits the step out.
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 2048, 4095);   // all fire, full spread
	setBank(c, ComputerCard::Middle, 2400, 2000, 400);
	setHeldBank(c, 0, 1600, 2200);

	int kickOnly = 0, metalOnly = 0, both = 0;
	for (int i = 0; i < 32; i++)
	{
		std::vector<int32_t> cap;
		stepOnce(c, 24000, &cap);
		// Attack window only, and compared as a ratio: a long kick can still be
		// ringing from an earlier step, which is intended and is not the same
		// thing as the kick firing now.
		double m = rmsWin(cap, 2, 0, 0, 2048);
		double k = rmsWin(cap, 2, 1, 0, 2048);
		if      (k > 3 * m && k > 100) kickOnly++;
		else if (m > 3 * k && m > 100) metalOnly++;
		else if (m > 100 && k > 100)   both++;
	}
	CHECK(kickOnly  > 0, "some steps are the kick alone  (%d of 32)", kickOnly);
	CHECK(metalOnly > 0, "some steps are the metal alone (%d of 32)", metalOnly);
	CHECK(both      > 0, "and some are both together     (%d of 32)", both);
}

static void testVoicesHaveOwnRhythms()
{
	printf("[18] the two voices run their own rhythms, not one shared one\n");
	// This is the property that levels and archetypes could never give: with a
	// single skip roll gating both, every step is offered to both voices, so
	// whichever fires less often is a strict subset of the other. Two rolls
	// against two densities decouples them.
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(0), 1400, 2600);
	setBank(c, ComputerCard::Middle, 2400, 1400, 400);
	setHeldBank(c, 0, 1600, 2200);

	const int N = 96, PERIOD = 12000;
	int nk = 0, nm = 0, nboth = 0;
	double prevK = 0, prevM = 0;
	for (int i = 0; i < N; i++)
	{
		std::vector<int32_t> cap;
		stepOnce(c, PERIOD, &cap);
		// A voice fired this step if its level jumped over where the previous
		// step left it — a long kick still ringing is not the kick firing.
		double kAtk = rmsWin(cap, 2, 1, 0, 512), mAtk = rmsWin(cap, 2, 0, 0, 512);
		bool k = (kAtk > 120 && kAtk > prevK * 1.6);
		bool m = (mAtk > 120 && mAtk > prevM * 1.6);
		if (k) nk++;
		if (m) nm++;
		if (k && m) nboth++;
		prevK = rmsWin(cap, 2, 1, PERIOD - 1024, 512);
		prevM = rmsWin(cap, 2, 0, PERIOD - 1024, 512);
	}

	CHECK(nk > 10 && nm > 10, "both voices are playing (%d kick, %d metal of %d)", nk, nm, N);
	double pk = (double)nk / N, pm = (double)nm / N;
	double shared = pk < pm ? pk : pm;          // what one shared roll would force
	double indep  = pk * pm;                    // what two independent rolls give
	double got    = (double)nboth / N;
	CHECK(got < (shared + indep) / 2.0,
	      "coincidence %.2f sits nearer independent %.2f than shared %.2f",
	      got, indep, shared);
}

// ============================ demos ============================

static void demoFreeVsLocked(const std::string &dir)
{
	printf("[demo] free chaos vs a locked 8-step loop\n");
	for (int locked = 0; locked < 2; locked++)
	{
		Slag c; clocked(c);
		setBank(c, ComputerCard::Up, stepsKnob(locked ? 6 : 0), 2900, 1700);
		setBank(c, ComputerCard::Middle, 2300, 1500, 900);

		std::vector<int32_t> cap;
		for (int i = 0; i < 32; i++) stepOnce(c, 6000, &cap);   // 32 steps @ 8Hz = 4s
		finalize(dir + (locked ? "/out_locked.wav" : "/out_free.wav"), cap);
	}
}

static void demoRustSweep(const std::string &dir)
{
	printf("[demo] rust, swept from clean to aliased garbage\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(6), 3400, 1100);
	setBank(c, ComputerCard::Middle, 2600, 1800, 0);

	std::vector<int32_t> cap;
	for (int i = 0; i < 32; i++)
	{
		c.simKnob[2] = (i * 4095) / 31;      // Y = Rust, already live
		stepOnce(c, 6000, &cap);
	}
	finalize(dir + "/out_rust.wav", cap);
}

static void demoKeyboard(const std::string &dir)
{
	printf("[demo] four voltages played as a keyboard\n");
	Slag c;                                          // no clock patched
	setBank(c, ComputerCard::Up, stepsKnob(0), 4095, 1800);
	setBank(c, ComputerCard::Middle, 2200, 2200, 1000);
	setHeldBank(c, 0, 1300, 3200);                   // internal clock off

	// Four buttons in the ~+/-2V range a 4 Voltages puts out, played as a riff.
	// No two adjacent presses are the same key, or there would be no jump to
	// trigger on — which is exactly how the hardware behaves.
	static const int32_t KEYS[4] = {-620, -180, 250, 690};
	static const int PATTERN[16] = {0,2,1,3, 0,2,1,0, 3,1,2,0, 1,3,0,2};

	std::vector<int32_t> cap;
	for (int i = 0; i < 32; i++)
	{
		c.simCVIn[0] = KEYS[PATTERN[i % 16]];
		run(c, 6000, &cap);
	}
	finalize(dir + "/out_keyboard.wav", cap);
}

static void demoMetalSweep(const std::string &dir)
{
	printf("[demo] metal, swept from a tight bell to a wide scrapyard\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(6), 3400, 900);
	setBank(c, ComputerCard::Middle, 2400, 2200, 300);

	std::vector<int32_t> cap;
	for (int i = 0; i < 32; i++)
	{
		setHeldBank(c, 0, 1000, (i * 4095) / 31);
		stepOnce(c, 6000, &cap);
	}
	finalize(dir + "/out_metal.wav", cap);
}

static void demoExternalSource(const std::string &dir)
{
	printf("[demo] Audio In 1 as the source instead of noise\n");
	Slag c; clocked(c);
	setBank(c, ComputerCard::Up, stepsKnob(5), 3000, 2200);   // 7 steps
	setBank(c, ComputerCard::Middle, 1800, 2000, 1300);
	c.simConnected[ComputerCard::Audio1] = true;

	std::vector<int32_t> cap;
	long t = 0;
	for (int i = 0; i < 32; i++)
	{
		// A slow tone sweep, so you can hear the card chopping and rusting it.
		for (int s = 0; s < 6000; s++, t++)
		{
			double f = 110.0 * pow(2.0, sin(t * 0.0000045) * 1.5);
			c.simAudioIn[0] = (int16_t)(1600.0 * sin(2.0 * M_PI * f * t / SR));
			c.simPulse[0] = (s < 8);
			run(c, 1, &cap);
		}
	}
	finalize(dir + "/out_external.wav", cap);
}

int main(int argc, char **argv)
{
	std::string dir = (argc > 1) ? argv[1] : ".";
	bool analyseOnly = (argc > 2 && std::string(argv[2]) == "--analyse");
	if (analyseOnly) { reportVoice(); reportCrossfade(); return 0; }
	printf("Slag — self-check\n\n");
	testLockRepeats();
	testResetRealignsPattern();
	testSpreadZeroFreezesTimbre();
	testChanceExtremes();
	testButtonHits();
	testOutputsAreSplit();
	testHeldBankDoesNotDisturbTheVoice();
	testTempoRunsTheInternalClock();
	testKeyboardOnPitchInput();
	testToneMixesMetalAgainstTheSource();
	testVoiceIsPercussiveNotTonal();
	testRustHoldsItsLevel();
	testRatchetsSubdivideSteps();
	testHitHasBody();
	testCrossfadeHoldsLevel();
	testHitsVaryInKind();
	testVoicesTakeTurns();
	testVoicesHaveOwnRhythms();
	printf("\nall checks passed\n\n");
	demoFreeVsLocked(dir);
	demoRustSweep(dir);
	demoMetalSweep(dir);
	demoKeyboard(dir);
	demoExternalSource(dir);
	return 0;
}
