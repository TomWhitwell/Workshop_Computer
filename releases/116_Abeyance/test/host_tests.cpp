// Host tests: the instrument on a computer, checked against the build
// guide's test checklist wherever a computer can check it.
//   make -C test
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "Sim.h"

static int failures = 0;
#define CHECK(cond, ...)                                    \
	do                                                      \
	{                                                       \
		if (!(cond))                                        \
		{                                                   \
			failures++;                                     \
			printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
			printf(__VA_ARGS__);                            \
			printf("\n");                                   \
		}                                                   \
	} while (0)

using ab::Panel;

// ---- Stage 1: panel and I/O ---------------------------------------------

static void TestStartupValues()
{
	// Knobs all at zero: nothing moves until each knob comes round to its
	// stored start-up value.
	Sim s(1, 0, 0, 0);
	s.RunMs(200);
	const Panel &p = s.control().GetPanel();
	CHECK(p.Linear(Panel::kVolume) == ab::kStartVolume, "volume %d", p.Linear(Panel::kVolume));
	CHECK(p.Linear(Panel::kReverb) == ab::kStartReverb, "reverb %d", p.Linear(Panel::kReverb));
	CHECK(p.Linear(Panel::kPiano) == ab::kStartPiano, "piano %d", p.Linear(Panel::kPiano));
	CHECK(p.Linear(Panel::kChoir) == ab::kStartChoir, "choir %d", p.Linear(Panel::kChoir));
	CHECK(p.Linear(Panel::kDrift) == ab::kStartDrift, "drift %d", p.Linear(Panel::kDrift));
	CHECK(p.Linear(Panel::kTape) == ab::kStartTape, "tape %d", p.Linear(Panel::kTape));
	CHECK(p.Waiting(), "middle-row knobs should be waiting to take over");
}

static void TestSoftTakeover()
{
	Sim s(1, 0, 0, 0);
	const Panel &p = s.control().GetPanel();
	// Main below the stored reverb value: no change.
	s.in.knob[0] = 1000;
	s.RunMs(200);
	CHECK(p.Linear(Panel::kReverb) == ab::kStartReverb, "reverb moved early: %d", p.Linear(Panel::kReverb));
	// Past it: takes over, then follows.
	s.in.knob[0] = 3000;
	s.RunMs(10);
	s.in.knob[0] = 1000;
	s.RunMs(300);
	CHECK(std::abs(p.Linear(Panel::kReverb) - 1000) < 8, "reverb should follow: %d", p.Linear(Panel::kReverb));

	// Flip Z up: X is now drift, and it must not jump to where X is.
	s.in.knob[1] = 4000;
	s.in.sw = ab::kSwitchUp;
	s.RunMs(300);
	CHECK(p.Linear(Panel::kDrift) == ab::kStartDrift, "drift jumped: %d", p.Linear(Panel::kDrift));
	CHECK(p.Linear(Panel::kPiano) == ab::kStartPiano, "piano moved in up mode: %d", p.Linear(Panel::kPiano));
	CHECK(p.Mode() == ab::kSwitchUp, "mode");
	// Main in up mode is volume; reverb must stay where it was left.
	s.in.knob[0] = 3500;
	s.RunMs(300);
	CHECK(std::abs(p.Linear(Panel::kReverb) - 1000) < 8, "reverb moved in up mode: %d", p.Linear(Panel::kReverb));
	CHECK(std::abs(p.Linear(Panel::kVolume) - 3500) < 8, "volume should have taken over: %d", p.Linear(Panel::kVolume));

	// Back to middle: Main (at 3500) waits for the stored 1000.
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(300);
	CHECK(std::abs(p.Linear(Panel::kReverb) - 1000) < 8, "reverb jumped on return: %d", p.Linear(Panel::kReverb));
}

static void TestGlide()
{
	// A jump in the knob reaches the setting over ~50ms, not at once.
	Sim s(1, ab::kStartReverb, 0, 0);
	const Panel &p = s.control().GetPanel();
	s.RunMs(50);
	s.in.knob[0] = 0;
	s.RunMs(10);
	int32_t mid = p.Linear(Panel::kReverb);
	CHECK(mid > 500 && mid < ab::kStartReverb - 200, "10ms into the glide: %d", mid);
	s.RunMs(150);
	CHECK(p.Linear(Panel::kReverb) < 8, "after 160ms: %d", p.Linear(Panel::kReverb));
}

static void TestTapAndPulse()
{
	Sim s(1);
	// A long hold fires once.
	s.in.sw = ab::kSwitchDown;
	s.RunMs(2000);
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(100);
	CHECK(s.control().Pieces() == 1, "long hold: %u pieces", s.control().Pieces());
	// Bounce: Down, out for 10ms, Down again -- one tap.
	s.in.sw = ab::kSwitchDown;
	s.RunMs(20);
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(10);
	s.in.sw = ab::kSwitchDown;
	s.RunMs(20);
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(100);
	CHECK(s.control().Pieces() == 2, "bounce: %u pieces", s.control().Pieces());
	// Pulse In 1: a new piece 20ms later.
	s.pulse = true;
	s.RunMs(15);
	CHECK(s.control().Pieces() == 2, "pulse fired before confirmation");
	s.RunMs(15);
	CHECK(s.control().Pieces() == 3, "pulse: %u pieces", s.control().Pieces());
	// An edge followed by an unplug within 20ms: ignored.
	s.pulse = true;
	s.RunMs(5);
	s.in.pulseConnected = false;
	s.RunMs(50);
	CHECK(s.control().Pieces() == 3, "unplug edge counted: %u pieces", s.control().Pieces());
}

static void TestCvIns()
{
	// CV In 1 at +5V with Main at zero: full reverb mix.
	Sim s(1, 0, 0, 0);
	const Panel &p = s.control().GetPanel();
	s.in.knob[0] = 4095; // take over...
	s.RunMs(20);
	s.in.knob[0] = 0;	 // ...and bring Main to zero
	s.RunMs(300);
	CHECK(p.Linear(Panel::kReverb) < 8, "reverb at zero: %d", p.Linear(Panel::kReverb));
	s.in.cv[0] = 1706;
	s.RunMs(300);
	CHECK(p.Linear(Panel::kReverb) > 4080, "CV1 5V: %d", p.Linear(Panel::kReverb));

	// CV In 2 at +/-2.5V: drift +/-50% around the knob's 30%.
	s.in.cv[1] = 853;
	s.RunMs(300);
	CHECK(std::abs(p.Linear(Panel::kDrift) - (ab::kStartDrift + 2048)) < 12, "CV2 +2.5V: %d", p.Linear(Panel::kDrift));
	s.in.cv[1] = -853;
	s.RunMs(300);
	CHECK(p.Linear(Panel::kDrift) < 8, "CV2 -2.5V clamps at 0: %d", p.Linear(Panel::kDrift));
}

static void TestCvOuts()
{
	// Ten minutes: CV Out 1 inside 0..5V, CV Out 2 inside +/-5V, both
	// moving over most of their range, and not moving together.
	Sim s(7);
	int32_t lo[2] = {100000, 100000}, hi[2] = {-100000, -100000};
	double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
	int n = 0;
	for (int i = 0; i < 600 * 100; i++)
	{
		s.RunMs(10);
		for (int c = 0; c < 2; c++)
		{
			lo[c] = std::min(lo[c], s.out.cvMv[c]);
			hi[c] = std::max(hi[c], s.out.cvMv[c]);
		}
		double x = s.out.cvMv[0], y = s.out.cvMv[1];
		sx += x, sy += y, sxx += x * x, syy += y * y, sxy += x * y, n++;
	}
	CHECK(lo[0] >= 0 && hi[0] <= 5000, "CV1 range %d..%d", lo[0], hi[0]);
	CHECK(lo[1] >= -5000 && hi[1] <= 5000, "CV2 range %d..%d", lo[1], hi[1]);
	CHECK(hi[0] - lo[0] > 3000, "CV1 barely moved: %d..%d", lo[0], hi[0]);
	CHECK(hi[1] - lo[1] > 6000, "CV2 barely moved: %d..%d", lo[1], hi[1]);
	double r = (n * sxy - sx * sy) / std::sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
	CHECK(std::fabs(r) < 0.4, "CV outs correlated: r = %.2f", r);
	// CV Out 2 is centred on 0V: over ten minutes its average sits near it.
	CHECK(std::fabs(sy / n) < 1500, "CV2 not centred: mean %.0f mV", sy / n);
	printf("  CV1 %d..%d mV, CV2 %d..%d mV, correlation %.2f\n", lo[0], hi[0], lo[1], hi[1], r);
}

static void TestLeds()
{
	Sim s(1, 0, 0, 0);
	// Waiting knobs: the middle row pulses.
	int32_t lo = 4095, hi = 0;
	for (int i = 0; i < 150; i++)
	{
		s.RunMs(10);
		lo = std::min(lo, s.led[2]);
		hi = std::max(hi, s.led[2]);
	}
	CHECK(lo < 200 && hi > 3800, "waiting row should pulse: %d..%d", lo, hi);
	// Z up: the top row lights for 0.8s.
	s.in.sw = ab::kSwitchUp;
	s.RunMs(100);
	CHECK(s.led[0] == 4095 && s.led[1] == 4095, "Z flash: %d %d", s.led[0], s.led[1]);
	s.RunMs(800);
	// New piece: sweep top to bottom.
	s.in.sw = ab::kSwitchDown;
	s.RunMs(50);
	CHECK(s.led[0] == 4095 && s.led[4] == 0, "sweep start");
	s.RunMs(350);
	CHECK(s.led[4] == 4095 && s.led[0] == 0, "sweep end");
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(100);
}

// ---- Stage 2: scheduler ---------------------------------------------------

static void TestOpeningPiece()
{
	Sim s(1);
	const ab::Scheduler &sc = s.control().GetScheduler();
	CHECK(sc.Root() == 61 && sc.ScaleIndex() == ab::kMajor, "opening key %d scale %d", sc.Root(), sc.ScaleIndex());
	// The guide's table, low to high.
	const int notes[13][3] = {{49, 56, 0}, {56}, {60}, {61}, {63}, {65}, {63, 68}, {68}, {68}, {72}, {72, 75}, {77, 80, 75}, {84}};
	const int pans[13] = {0, -11, 23, -25, 31, -37, 38, -45, 45, -56, 60, -71, 90};
	for (int i = 0; i < 13; i++)
	{
		for (int n = 0; n < ab::kLoopDefs[i].count; n++)
			CHECK(sc.BaseNote(i, n) == notes[i][n], "loop %d note %d: %d, want %d", i, n, sc.BaseNote(i, n), notes[i][n]);
		int pan = (int)std::lround(sc.Pan(i) * 100.0 / 32767.0);
		int tol = 3;
		CHECK(std::abs(pan - pans[i]) <= tol, "loop %d pan %d, table %d", i, pan, pans[i]);
		CHECK(sc.Length(i) == ab::kLoopDefs[i].lengthCs * 480, "loop %d length", i);
	}
	// The first note sounds within 2s of power-up.
	Sim t(1);
	uint32_t before = t.control().ChoirNotes() + t.control().PianoNotes();
	t.RunMs(1900 - 110);
	CHECK(t.control().ChoirNotes() + t.control().PianoNotes() > before, "nothing played in the first 2s");
}

static void TestScales()
{
	// Hirajoshi (0 2 3 7 8): steps 1 2 3 5 7 become 0 2 2 7 8 -- the rub
	// moves to 5 against flat 6.
	const ab::Scale &h = ab::kScaleDefs[8];
	const int deg[5] = {0, 1, 2, 4, 6}, want[5] = {60, 62, 62, 67, 68};
	for (int i = 0; i < 5; i++)
		CHECK(ab::DegreeToNote(deg[i], 60, h) == want[i], "Hirajoshi degree %d: %d", deg[i], ab::DegreeToNote(deg[i], 60, h));
	// Low 7 and high 7 in a pentatonic: the scale's top step, an octave apart.
	const ab::Scale &mp = ab::kScaleDefs[5];
	CHECK(ab::DegreeToNote(-1, 60, mp) == 57, "major pent low 7: %d", ab::DegreeToNote(-1, 60, mp));
	CHECK(ab::DegreeToNote(13, 60, mp) == 81, "major pent high 7: %d", ab::DegreeToNote(13, 60, mp));
	CHECK(ab::DegreeToNote(-3, 60, mp) == 55, "major pent low 5: %d", ab::DegreeToNote(-3, 60, mp));
}

static double Rms(Sim &s, int ms);
static void SetUp(Sim &s, int knob, int32_t value);

static constexpr int kFadeMs = ab::kNewPieceFadeBlocks * 2 / 3;

// A new piece waits for everything to fade out: the old piece plays on
// under a 15s fade of the whole output -- voices, echo and reverb -- then the
// echo and reverb are emptied, and the new piece starts in a clean room.
static void TestNewPieceFade()
{
	Sim s(21); // start-up settings: reverb mix 65%
	SetUp(s, 1, 0); // drift off, so the new piece's first pass can't be skipped
	s.RunMs(40000);
	const ab::Scheduler &sc = s.control().GetScheduler();
	int scale = sc.ScaleIndex();
	double before = Rms(s, 1000);
	s.Tap();
	CHECK(s.control().Fading(), "not fading after a tap");
	uint32_t notes = s.control().PianoNotes() + s.control().ChoirNotes();
	double half = 0;
	s.RunMs(kFadeMs / 2 - 150);
	half = Rms(s, 300);
	s.RunMs(kFadeMs / 2 - 800);
	double end = Rms(s, 300); // the last 300ms of the fade
	CHECK(s.control().PianoNotes() + s.control().ChoirNotes() > notes, "the old piece stopped under its fade");
	notes = s.control().PianoNotes() + s.control().ChoirNotes();
	CHECK(sc.ScaleIndex() == scale, "new piece began before the fade ended");
	CHECK(half < before * 0.5 && half > before * 0.05, "fade halfway: %.0f vs %.0f before", half, before);
	CHECK(end < before * 0.01, "fade: %.0f at its end vs %.0f before", end, before);
	// The flush: silence, then the new piece.
	s.RunMs(250);
	CHECK(!s.control().Fading() && sc.ScaleIndex() != scale, "no new piece after the fade");
	CHECK(s.control().GetChoir().Active() + s.control().GetPiano().Active() == 0, "old voices left sounding");
	// Before the new piece's first note (0.25s in), nothing of the old
	// piece is left: only the tape hiss, coming back up (about 40 here).
	double after = Rms(s, 150);
	CHECK(after < 60, "old echo or reverb heard in the new piece: %.0f", after);
	s.RunMs(400);
	CHECK(s.control().PianoNotes() + s.control().ChoirNotes() > notes, "new piece silent 0.5s after the fade");
	printf("  between pieces: rms %.0f before, %.0f halfway through the %.0fs fade, %.0f at its end, %.0f before the "
		   "first new note\n",
		   before, half, kFadeMs / 1000.0, end, after);
}

static void TestNewPieces()
{
	Sim s(5);
	const ab::Scheduler &sc = s.control().GetScheduler();
	int lowP = 127, highP = 0, lowC = 127, highC = 0;
	int last = sc.ScaleIndex();
	float tlo = 9, thi = 0;
	for (int k = 0; k < 400; k++)
	{
		s.Tap();
		s.RunMs(kFadeMs + 100); // the old piece fades out first
		CHECK(sc.ScaleIndex() != last, "scale repeated");
		last = sc.ScaleIndex();
		CHECK(sc.Root() >= 57 && sc.Root() <= 68, "root %d", sc.Root());
		float tempo = sc.TempoQ12() / 4096.0f;
		tlo = std::min(tlo, tempo), thi = std::max(thi, tempo);
		for (int i = 0; i < 13; i++)
		{
			float len = sc.Length(i) / 48000.0f, orig = ab::kLoopDefs[i].lengthCs / 100.0f;
			CHECK(len >= orig / tempo * 0.879f && len <= orig / tempo * 1.121f, "loop %d length %.2f", i, len);
			for (int n = 0; n < ab::kLoopDefs[i].count; n++)
			{
				int note = sc.BaseNote(i, n);
				if (ab::kLoopDefs[i].layer == ab::kPiano)
					lowP = std::min(lowP, note), highP = std::max(highP, note);
				else
					lowC = std::min(lowC, note), highC = std::max(highC, note);
			}
		}
	}
	CHECK(tlo >= 0.699f && thi <= 1.4f && tlo < 0.75f && thi > 1.35f, "tempo %.2f..%.2f", tlo, thi);
	CHECK(lowP == 45 && highP == 91, "piano %d..%d", lowP, highP);
	CHECK(lowC >= 52 && highC <= 81, "choir %d..%d", lowC, highC);
	printf("  400 pieces: tempo %.2f..%.2f, piano %d..%d, choir %d..%d\n", tlo, thi, lowP, highP, lowC, highC);
}

static void TestPulseOuts()
{
	// Five minutes: one Pulse Out 1 trigger per piano note (closer than 15ms
	// merge), none for choir notes; Pulse Out 2 drops low between notes.
	Sim s(9);
	uint32_t p0 = s.control().PianoNotes();
	int triggers = 0, gateFalls = 0, longestHigh = 0, high = 0;
	bool last0 = false, last1 = false;
	for (int i = 0; i < 300 * 1000; i++)
	{
		s.Run(48);
		if (s.out.pulse[0] && !last0)
			triggers++;
		if (!s.out.pulse[1] && last1)
			gateFalls++;
		high = s.out.pulse[0] ? high + 1 : 0;
		longestHigh = std::max(longestHigh, high);
		last0 = s.out.pulse[0];
		last1 = s.out.pulse[1];
	}
	uint32_t notes = s.control().PianoNotes() - p0;
	CHECK(triggers > 0 && (uint32_t)triggers <= notes && (uint32_t)triggers >= notes * 9 / 10, "%d triggers for %u piano notes", triggers, notes);
	CHECK(longestHigh <= 11, "trigger held %d ms", longestHigh);
	CHECK(gateFalls > 10, "gate fell only %d times", gateFalls);
	printf("  5 minutes: %u piano notes, %d triggers, gate fell %d times, %u choir notes\n", notes, triggers, gateFalls,
		   s.control().ChoirNotes());
}

// ---- Stage 3: piano -------------------------------------------------------

// Loudness of the output over the next `ms`, RMS in 16-bit units.
static double Rms(Sim &s, int ms)
{
	double sum = 0;
	int n = ms * 48;
	for (int i = 0; i < n; i++)
	{
		s.Step();
		sum += (double)s.out.audio[0] * s.out.audio[0] + (double)s.out.audio[1] * s.out.audio[1];
	}
	return std::sqrt(sum / (2.0 * n)) * 16;
}

static void TestPianoNote()
{
	// One voice, straight into a buffer: the pitch, the 3-cent beat, the
	// decay rate.
	ab::Piano p;
	int32_t gl[13], gr[13];
	for (int i = 0; i < 13; i++)
		gl[i] = gr[i] = 23170; // centre
	p.NoteOn(69, 0, 4096);	   // A4
	std::vector<int32_t> out;
	for (int b = 0; b < 1500 * 4; b++)
	{
		int32_t l[32] = {}, r[32] = {};
		p.Render(l, r, gl, gr, 0);
		out.insert(out.end(), l, l + 32);
	}
	// Pitch: the autocorrelation peak, 0.5s in, should be at 48000/440 =
	// 109.1 samples.
	int bestLag = 0;
	double best = -1e30;
	for (int lag = 80; lag < 140; lag++)
	{
		double c = 0;
		for (int i = 24000; i < 24000 + 9600; i++)
			c += (double)out[i] * out[i + lag];
		if (c > best)
			best = c, bestLag = lag;
	}
	CHECK(bestLag == 109, "A4 period %d samples", bestLag);
	// Decay: T60 for A4 (note 69) is 16 x (3/16)^(24/46) = 6.68s.
	double t60 = -3.0 / std::log10(ab::Piano::decayFund[69] / 2147483648.0) / 1500.0;
	CHECK(std::fabs(t60 - 6.68) < 0.05, "A4 T60 %.2fs", t60);
	double t60lo = -3.0 / std::log10(ab::Piano::decayFund[45] / 2147483648.0) / 1500.0;
	double t60hi = -3.0 / std::log10(ab::Piano::decayFund[91] / 2147483648.0) / 1500.0;
	CHECK(std::fabs(t60lo - 16) < 0.2 && std::fabs(t60hi - 3) < 0.1, "T60 A2 %.2f G6 %.2f", t60lo, t60hi);
	// And the voice frees itself once it has died away.
	CHECK(p.Active() == 1, "voice ended too soon");
	for (int b = 0; b < 1500 * 20; b++)
	{
		int32_t l[32] = {}, r[32] = {};
		p.Render(l, r, gl, gr, 0);
	}
	CHECK(p.Active() == 0, "voice still active after 24s");
	printf("  A4: period %d samples, T60 %.1fs (A2 %.1fs, G6 %.1fs)\n", bestLag, t60, t60lo, t60hi);
}

static void TestLayerLevels()
{
	// Piano level at zero: no piano at all (the choir still plays).
	Sim s(4, 0, 0, 0);
	s.in.knob[1] = 4095;
	s.RunMs(20);
	s.in.knob[1] = 0;
	s.RunMs(200);
	CHECK(s.control().GetPanel().Linear(Panel::kPiano) == 0, "piano level %d", s.control().GetPanel().Linear(Panel::kPiano));
	// Count frames where the piano send is non-zero over 60s.
	s.RunMs(60000);
	CHECK(s.control().PianoNotes() > 5, "no piano notes played");

	// Master volume at zero: silence.
	Sim v(4, 0, 0, 0);
	v.in.sw = ab::kSwitchUp;
	v.in.knob[0] = 4095;
	v.RunMs(20);
	v.in.knob[0] = 0;
	v.RunMs(300);
	CHECK(Rms(v, 20000) == 0, "volume at zero is not silent");
}

// ---- Stage 4: choir -------------------------------------------------------

// Amplitude of harmonic k in a 1024-point wavetable (a single DFT bin).
static double Harmonic(const int16_t *t, int k)
{
	double re = 0, im = 0;
	for (int i = 0; i < 1024; i++)
	{
		re += t[i] * std::cos(2 * M_PI * k * i / 1024);
		im += t[i] * std::sin(2 * M_PI * k * i / 1024);
	}
	return std::sqrt(re * re + im * im) / 512;
}

static void TestWavetables()
{
	// Each table peaks at 30,000, and is band-limited: nothing at or above
	// the harmonic that would pass 16kHz at the table's top note.
	const int harmonics[5] = {68, 48, 34, 24, 17};
	for (int v = 0; v < 2; v++)
		for (int c = 0; c < 5; c++)
		{
			const int16_t *t = ab::Choir::table[v][c];
			int peak = 0;
			double sq = 0;
			for (int i = 0; i < 1024; i++)
				peak = std::max(peak, std::abs((int)t[i])), sq += (double)t[i] * t[i];
			double crest = peak / std::sqrt(sq / 1024);
			CHECK(std::abs(peak - 30000) <= 2, "table %d/%d peak %d", v, c, peak);
			CHECK(crest < 3.0, "table %d/%d spiky: crest %.2f", v, c, crest);
			double above = Harmonic(t, harmonics[c] + 1);
			CHECK(above < 2, "table %d/%d has harmonic %d: %.1f", v, c, harmonics[c] + 1, above);
			CHECK(Harmonic(t, harmonics[c]) > 2, "table %d/%d lacks its top harmonic", v, c);
		}
	// The vowels: in the G3 table (196Hz), "ah" is strongest near 730Hz
	// (harmonics 3-4) and "oo" near 320Hz (harmonic 2). "ah" is brighter.
	const int16_t *ah = ab::Choir::table[0][0], *oo = ab::Choir::table[1][0];
	CHECK(Harmonic(ah, 4) > Harmonic(ah, 2), "ah: H4 %.0f H2 %.0f", Harmonic(ah, 4), Harmonic(ah, 2));
	CHECK(Harmonic(oo, 2) > Harmonic(oo, 4) * 2, "oo: H2 %.0f H4 %.0f", Harmonic(oo, 2), Harmonic(oo, 4));
	double ahHigh = 0, ooHigh = 0;
	for (int k = 10; k < 68; k++)
		ahHigh += Harmonic(ah, k), ooHigh += Harmonic(oo, k);
	CHECK(ahHigh > ooHigh * 1.3, "ah not brighter: %.0f vs %.0f", ahHigh, ooHigh);
}

static void TestChoirNote()
{
	ab::Choir c;
	int32_t gl[13], gr[13];
	for (int i = 0; i < 13; i++)
		gl[i] = gr[i] = 23170;
	ab::Random rng(7);
	c.NoteOn(61, 0, 4096, 0, 4096, rng); // D-flat 4, tempo 1
	std::vector<int32_t> out;
	for (int b = 0; b < 1500 * 17; b++)
	{
		int32_t l[32] = {}, r[32] = {};
		c.Render(l, r, gl, gr, 0);
		out.insert(out.end(), l, l + 32);
	}
	auto level = [&](double from, double len) {
		double sq = 0;
		int a = (int)(from * 48000), n = (int)(len * 48000);
		for (int i = a; i < a + n; i++)
			sq += (double)out[i] * out[i];
		return std::sqrt(sq / n);
	};
	// A swell of 2.4-3.4s, held until 4.5-7.5s, then a fade (e every
	// 1.125s) to 60dB down -- silence by 16s at the latest.
	double early = level(0.1, 0.1), mid = level(3.5, 0.9), late = level(13, 0.2);
	CHECK(early < mid * 0.1, "swell: %.0f at 0.1s vs %.0f held", early, mid);
	CHECK(late < mid * 0.02, "fade: %.0f at 13s vs %.0f held", late, mid);
	CHECK(level(16, 0.9) == 0, "still sounding after the release");
	CHECK(c.Active() == 0, "voice not freed");
	// Pitch: autocorrelation peak near 48000 / 277.2 = 173.2 samples.
	int bestLag = 0;
	double best = -1e30;
	for (int lag = 150; lag < 200; lag++)
	{
		double s = 0;
		for (int i = 96000; i < 96000 + 9600; i++)
			s += (double)out[i] * out[i + lag];
		if (s > best)
			best = s, bestLag = lag;
	}
	CHECK(std::abs(bestLag - 173) <= 1, "D-flat 4 period %d", bestLag);
	printf("  choir D-flat 4: period %d samples; held level %.0f\n", bestLag, mid);
}

// ---- Stage 5: reverb, echo, audio in ---------------------------------------

// Take a middle-mode knob (1 = X piano, 2 = Y choir, 0 = Main reverb) over
// and set it.
static void SetMiddle(Sim &s, int knob, int32_t value)
{
	s.in.sw = ab::kSwitchMiddle;
	s.in.knob[knob] = 0;
	s.RunMs(15);
	s.in.knob[knob] = 4095;
	s.RunMs(15);
	s.in.knob[knob] = value;
	s.RunMs(200);
}

// The same for an up-mode knob (0 = Main volume, 1 = X drift, 2 = Y tape).
static void SetUp(Sim &s, int knob, int32_t value)
{
	s.in.sw = ab::kSwitchUp;
	s.in.knob[knob] = 0;
	s.RunMs(15);
	s.in.knob[knob] = 4095;
	s.RunMs(15);
	s.in.knob[knob] = value;
	s.RunMs(200);
	s.in.sw = ab::kSwitchMiddle;
	s.RunMs(10);
}

static void TestLevelsSilenceTails()
{
	// Piano and choir at zero: once the tails already under way have died
	// (the echo's and the reverb's included), true silence.
	Sim s(21);
	s.RunMs(30000); // plenty playing
	SetUp(s, 2, 0); // no tape hiss
	SetMiddle(s, 1, 0);
	SetMiddle(s, 2, 0);
	s.RunMs(40000);
	double rms = Rms(s, 20000);
	CHECK(rms == 0, "piano and choir at zero still sound: rms %.2f", rms);
}

static void TestAudioIn()
{
	// Only the inputs: both layers at zero. Noise bursts into input 1 alone
	// come out of the reverb on both sides at about the same level. (Broad
	// band: a single steady tone lands on the reverb's room modes, which
	// favour one side or the other by a few dB depending on the pitch.)
	Sim s(22);
	SetUp(s, 2, 0); // no tape hiss
	SetMiddle(s, 1, 0);
	SetMiddle(s, 2, 0);
	s.RunMs(40000);
	double l = 0, r = 0;
	ab::Random noise(3);
	for (int i = 0; i < 48000 * 20; i++)
	{
		int t = i % 24000;
		s.in.audio[0] = (int)(std::exp(-t / 4800.0) * (int32_t)((noise.Next() >> 21) - 1024));
		s.Step();
		l += (double)s.out.audio[0] * s.out.audio[0];
		r += (double)s.out.audio[1] * s.out.audio[1];
	}
	double db = 10 * std::log10(l / r);
	CHECK(l > 0 && std::fabs(db) < 1.5, "input 1 not centred: L/R %.1f dB", db);
	printf("  audio in 1 through the reverb: L/R balance %.2f dB\n", db);
	// Reverb mix at zero: the inputs are silent.
	SetMiddle(s, 0, 0);
	s.RunMs(100);
	double sum = 0;
	for (int i = 0; i < 48000 * 2; i++)
	{
		s.in.audio[0] = s.in.audio[1] = (int)(1000 * std::sin(2 * M_PI * 330 * i / 48000.0));
		s.Step();
		sum += std::abs(s.out.audio[0]) + std::abs(s.out.audio[1]);
	}
	CHECK(sum == 0, "inputs heard at zero reverb mix");
}

static void TestEchoAndPlate()
{
	// The echo: a click on the left comes back on the left after 1.15s,
	// then crosses to the right, and fades to silence.
	auto echo = std::make_unique<ab::Echo>();
	std::vector<int32_t> el, er;
	for (int i = 0; i < 48000 * 30; i++)
	{
		int32_t l, r;
		echo->Process(i < 48 ? 20000 : 0, 0, l, r);
		el.push_back(l), er.push_back(r);
	}
	int at = 0;
	for (int i = 1; i < 48000 * 2; i++)
		if (std::abs(el[i]) > std::abs(el[at]))
			at = i;
	CHECK(std::abs(at - 55200) < 200, "first repeat at %d samples", at);
	int at2 = 0;
	for (int i = 1; i < 48000 * 3; i++)
		if (std::abs(er[i]) > std::abs(er[at2]))
			at2 = i;
	CHECK(std::abs(at2 - 110400) < 300, "second repeat (right) at %d samples", at2);
	CHECK(el[48000 * 30 - 1] == 0 && er[48000 * 30 - 1] == 0, "echo never dies: %d %d", el.back(), er.back());

	// The plate: a burst, then the time for its tail to fall 60dB.
	auto plate = std::make_unique<ab::Plate>();
	int32_t lfo[2] = {0, 0};
	std::vector<double> env;
	double sq = 0;
	for (int i = 0; i < 48000 * 40; i++)
	{
		int32_t l, r;
		int32_t x = i < 2400 ? (int32_t)(4000 * std::sin(2 * M_PI * 300 * i / 48000.0)) : 0; // 300Hz
		plate->Process(x, lfo, l, r);
		sq += (double)l * l + (double)r * r;
		if (i % 4800 == 4799)
			env.push_back(10 * std::log10(sq / 9600 + 1e-12)), sq = 0;
	}
	double top = env[1];
	int t60 = 0;
	for (size_t k = 1; k < env.size(); k++)
		if (env[k] > top - 60)
			t60 = (int)k;
	CHECK(t60 >= 60 && t60 <= 200, "reverb T60 %.1fs", t60 / 10.0);
	CHECK(env.back() < -100, "reverb never dies: %.1f dB", env.back());

	// The Main knob's macro at its end: a longer tail, still dying to
	// silence.
	auto big = std::make_unique<ab::Plate>();
	big->Shape(ab::kReverbMaxDecayQ15, ab::kReverbMaxDampQ15, ab::kReverbMaxOutQ10);
	std::vector<double> env2;
	sq = 0;
	for (int i = 0; i < 48000 * 40; i++)
	{
		int32_t l, r;
		int32_t x = i < 2400 ? (int32_t)(4000 * std::sin(2 * M_PI * 300 * i / 48000.0)) : 0;
		big->Process(x, lfo, l, r);
		sq += (double)l * l + (double)r * r;
		if (i % 4800 == 4799)
			env2.push_back(10 * std::log10(sq / 9600 + 1e-12)), sq = 0;
	}
	int t60b = 0;
	for (size_t k = 1; k < env2.size(); k++)
		if (env2[k] > env2[1] - 60)
			t60b = (int)k;
	CHECK(t60b >= 150 && t60b <= 220, "full-macro reverb T60 %.1fs", t60b / 10.0);
	CHECK(env2.back() < -100, "full-macro reverb never dies: %.1f dB", env2.back());
	printf("  echo repeat at %.3fs; reverb falls 60dB in %.1fs (%.1fs at the macro's end)\n", at / 48000.0, t60 / 10.0,
		   t60b / 10.0);
}

// ---- Stage 6: drift and tape ---------------------------------------------

static void TestDrift()
{
	// Drift at zero: every pass plays as written.
	Sim z(31);
	SetUp(z, 1, 0);
	z.RunMs(300000);
	CHECK(z.control().GetScheduler().Skips() == 0 && z.control().GetScheduler().OctaveJumps() == 0, "drift 0 still drifts");

	// Drift at full, over many pieces: skips and jumps happen, and every
	// note stays inside the guide's ranges.
	Sim s(32);
	SetUp(s, 1, 4095);
	for (int k = 0; k < 30; k++)
	{
		s.RunMs(60000);
		s.Tap();
	}
	const ab::Scheduler &sc = s.control().GetScheduler();
	CHECK(sc.Skips() > 50 && sc.OctaveJumps() > 30, "full drift: %u skips, %u jumps", sc.Skips(), sc.OctaveJumps());
	CHECK(s.control().LowestNote(ab::kPiano) >= 45 && s.control().HighestNote(ab::kPiano) <= 91, "piano %d..%d",
		  s.control().LowestNote(ab::kPiano), s.control().HighestNote(ab::kPiano));
	CHECK(s.control().LowestNote(ab::kChoir) >= 52 && s.control().HighestNote(ab::kChoir) <= 81, "choir %d..%d",
		  s.control().LowestNote(ab::kChoir), s.control().HighestNote(ab::kChoir));
	printf("  30 minutes at full drift: %u skips, %u octave jumps; piano %d..%d, choir %d..%d\n", sc.Skips(),
		   sc.OctaveJumps(), s.control().LowestNote(ab::kPiano), s.control().HighestNote(ab::kPiano),
		   s.control().LowestNote(ab::kChoir), s.control().HighestNote(ab::kChoir));
	CHECK(s.inst->Underruns() <= 1, "underruns %u", s.inst->Underruns());

	// CV In 2 at -2.5V with the knob at 30%: no drift.
	Sim c(33);
	c.in.cv[1] = -853;
	c.RunMs(300000);
	CHECK(c.control().GetScheduler().Skips() == 0, "CV2 -2.5V should stop drift: %u skips", c.control().GetScheduler().Skips());
}

// Brightness: the RMS of the first difference over the RMS -- higher when
// there's more treble.
static double Brightness(Sim &s, int ms, int *peaks, int threshold)
{
	double sq = 0, dq = 0;
	int last = 0;
	*peaks = 0;
	bool above = false;
	for (int i = 0; i < ms * 48; i++)
	{
		s.Step();
		int x = s.out.audio[0];
		sq += (double)x * x;
		dq += (double)(x - last) * (x - last);
		last = x;
		bool a = std::abs(x) > threshold;
		*peaks += a && !above;
		above = a;
	}
	return std::sqrt(dq / (sq + 1e-9));
}

static void TestHiss()
{
	// Tape at zero, layers at zero, after the tails: silence. At 40%: a
	// little hiss, about -58dBFS (volume at 75%), and no crackle. At full:
	// hiss about 16dB up (square law), darker, with crackle and pops.
	Sim s(34);
	SetUp(s, 2, 0);
	SetMiddle(s, 1, 0);
	SetMiddle(s, 2, 0);
	s.RunMs(40000);
	CHECK(Rms(s, 5000) == 0, "tape at zero still hisses");
	SetUp(s, 2, 1638);
	double part = 20 * std::log10(Rms(s, 20000) / 32768);
	int peaksPart, peaksHalf, peaksFull;
	// Clicks: peaks over 90 (12-bit), far above any hiss.
	double bright40 = Brightness(s, 20000, &peaksPart, 90);
	SetUp(s, 2, 2048);
	double brightHalf = Brightness(s, 20000, &peaksHalf, 90);
	SetUp(s, 2, 4095);
	double full = 20 * std::log10(Rms(s, 20000) / 32768);
	double brightFull = Brightness(s, 20000, &peaksFull, 90);
	CHECK(part > -61 && part < -55, "hiss at 40%% tape %.1f dBFS", part);
	CHECK(full - part > 13 && full - part < 21, "full tape only %.1f dB above 40%%", full - part);
	CHECK(peaksPart == 0 && peaksHalf == 0, "crackle below 60%% tape: %d, %d", peaksPart, peaksHalf);
	CHECK(peaksFull > 10 && peaksFull < 400, "crackle at full tape: %d clicks in 20s", peaksFull);
	CHECK(brightFull < brightHalf * 0.8, "full tape not darker: %.2f vs %.2f", brightFull, brightHalf);
	printf("  tape: %.1f dBFS at 40%%, %.1f at full (volume 75%%); %d clicks in 20s at full; brightness %.2f -> %.2f\n",
		   part, full, peaksFull, brightHalf, brightFull);
	(void)bright40;

	// The hiss drifts: its level over 1s windows isn't steady.
	SetUp(s, 2, 1638);
	double lo = 1e9, hi = 0;
	for (int k = 0; k < 20; k++)
	{
		double r = Rms(s, 1000);
		lo = std::min(lo, r), hi = std::max(hi, r);
	}
	CHECK(20 * std::log10(hi / lo) > 2, "hiss doesn't drift: %.1f dB over 20s", 20 * std::log10(hi / lo));
}

static void TestNoUnderruns(Sim &s)
{
	CHECK(s.inst->Underruns() <= 1, "core 0 ran out of blocks %u times", s.inst->Underruns());
}

int main()
{
	TestStartupValues();
	TestSoftTakeover();
	TestGlide();
	TestTapAndPulse();
	TestCvIns();
	TestCvOuts();
	TestLeds();
	TestOpeningPiece();
	TestScales();
	TestNewPieceFade();
	TestNewPieces();
	TestPulseOuts();
	TestPianoNote();
	TestLayerLevels();
	TestWavetables();
	TestChoirNote();
	TestLevelsSilenceTails();
	TestAudioIn();
	TestEchoAndPlate();
	TestDrift();
	TestHiss();
	{
		Sim s(3);
		s.RunMs(2000);
		TestNoUnderruns(s);
	}
	if (failures)
	{
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
