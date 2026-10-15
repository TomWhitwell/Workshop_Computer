// Host tests: `make -C test`. Each test drives a pretend panel (Sim.h) and
// checks what the card does, sample by sample, exactly as the firmware runs.
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "Sim.h"

using namespace constancy;

static int failures = 0;
static std::string current;

#define CHECK(cond)                                                                    \
	do                                                                                 \
	{                                                                                  \
		if (!(cond))                                                                   \
		{                                                                              \
			printf("  FAIL %s: %s (line %d)\n", current.c_str(), #cond, __LINE__);     \
			failures++;                                                                \
		}                                                                              \
	} while (0)

static void Test(const char *name, const std::function<void()> &fn)
{
	current = name;
	int before = failures;
	fn();
	printf("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

// Loudness (RMS, DAC counts) of the last `ms` of output.
static double Rms(Sim &s, int ms)
{
	double sum = 0;
	size_t n = (size_t)ms * 48;
	size_t from = s.outL.size() > n ? s.outL.size() - n : 0;
	for (size_t i = from; i < s.outL.size(); i++)
		sum += (double)s.outL[i] * s.outL[i] / 256.0 + (double)s.outR[i] * s.outR[i] / 256.0;
	return std::sqrt(sum / (2.0 * (double)(s.outL.size() - from)));
}

// Table position (x 256) of a Main knob value.
static int32_t KnobFor(int32_t posQ8) { return (posQ8 * 4095 + kDayEndQ8 - 1) / kDayEndQ8; }

// ---- The morning's data -------------------------------------------

static void Data()
{
	Test("data: the window runs from night to morning", [] {
		CHECK(MorningAt(0).sun == 0);
		CHECK(MorningAt(kDayEndQ8).sun == 4096);
		// The sun climbs steadily all morning.
		for (int i = 1; i < kDayPoints; i++)
			CHECK(kDay[i].sun > kDay[i - 1].sun);
		// Sunrise falls in the window's middle half.
		CHECK(kSunriseQ8 > kDayEndQ8 / 4 && kSunriseQ8 < kDayEndQ8 * 3 / 4);
		CHECK(MorningAt(kSunriseQ8).sun >= kSunriseSunQ12 - 8 && MorningAt(kSunriseQ8).sun <= kSunriseSunQ12 + 8);
	});

	Test("data: the tide falls to low water, then turns", [] {
		CHECK(MorningAt(0).tide == 4096);
		CHECK(MorningAt(kLowWaterQ8).tide == 0);
		CHECK(MorningAt(kLowWaterQ8).tideRate < 100); // slack water
		CHECK(MorningAt(kDayEndQ8).tide > 1000);	   // flooding again
	});

	Test("data: every stream uses its full range", [] {
		int32_t lo[5] = {4096, 4096, 4096, 4096, 4096}, hi[5] = {};
		for (const DayPoint &d : kDay)
		{
			int32_t v[5] = {d.sun, d.tide, d.tideRate, d.wind, d.cloud};
			for (int k = 0; k < 5; k++)
			{
				lo[k] = std::min(lo[k], v[k]);
				hi[k] = std::max(hi[k], v[k]);
			}
		}
		for (int k = 0; k < 5; k++)
			CHECK(lo[k] == 0 && hi[k] == 4096);
	});

	Test("data: blends smoothly between points", [] {
		for (int32_t pos = 0; pos < kDayEndQ8; pos += 37)
		{
			Morning a = MorningAt(pos), b = MorningAt(pos + 1);
			CHECK(std::abs(a.tide - b.tide) <= 2);
			CHECK(std::abs(a.cloud - b.cloud) <= 2);
		}
	});
}

// ---- A single voice --------------------------------------------------

static void Voice()
{
	Test("saw: the right pitch, centred, with its cliff rounded off", [] {
		SawParams p;
		VoiceControl::SetSaw(p, PitchToInc(69 << 16)); // A4
		uint32_t phase = 0;
		int32_t prev = Saw(phase, p), crossings = 0, lo = 0, hi = 0;
		int64_t sum = 0;
		for (int i = 0; i < 48000; i++)
		{
			int32_t s = Saw(phase, p);
			if (prev > 0 && s <= 0)
				crossings++;
			lo = std::min(lo, s);
			hi = std::max(hi, s);
			sum += s;
			prev = s;
		}
		CHECK(crossings >= 439 && crossings <= 441); // one cliff per cycle
		CHECK(hi <= 16384 && lo >= -16384);
		CHECK(std::llabs(sum / 48000) < 40);
	});

	Test("pitch: notes and fractions of a semitone", [] {
		double a4 = PitchToInc(69 << 16) * 48000.0 / 4294967296.0;
		CHECK(std::fabs(a4 - 440.0) < 0.05);
		double up = PitchToInc((69 << 16) + 32768) * 48000.0 / 4294967296.0; // a quarter tone
		CHECK(std::fabs(up - 440.0 * std::pow(2.0, 0.5 / 12.0)) < 0.2);
	});

	Test("envelope: cubed attack, decay to sustain, release", [] {
		Envelope e;
		EnvelopeTimes t;
		t.attackMs = 100;
		t.decayMs = 200;
		t.sustainRootQ15 = CubeRootQ15(2048); // half
		t.releaseMs = 400;
		e.Trigger(t, 1000);
		int32_t at50 = 0;
		for (int i = 0; i < 50; i++)
			at50 = e.Tick();
		// Halfway through a cubed attack is an eighth of full level.
		CHECK(at50 > 32768 / 8 - 600 && at50 < 32768 / 8 + 600);
		for (int i = 0; i < 400; i++)
			e.Tick();
		CHECK(std::abs(e.Level() - 16384) < 200); // sustain
		for (int i = 0; i < 550; i++)
			e.Tick();
		CHECK(e.Releasing());
		for (int i = 0; i < 420; i++)
			e.Tick();
		CHECK(e.Idle());
		CHECK(e.Level() == 0);
	});

	Test("envelope: a new note swells from where the last one is", [] {
		Envelope e;
		EnvelopeTimes t;
		t.attackMs = 10;
		t.releaseMs = 1000;
		e.Trigger(t, 50);
		for (int i = 0; i < 300; i++)
			e.Tick();
		int32_t before = e.Level();
		CHECK(before > 0);
		e.Trigger(t, 50);
		CHECK(e.Tick() >= before);
	});

	Test("tide: low tide plucks, high tide swells", [] {
		EnvelopeTimes low = TideEnvelope(kLoopEnvelope, 0);
		EnvelopeTimes high = TideEnvelope(kLoopEnvelope, 4096);
		CHECK(low.attackMs == kLoopEnvelope.attackLow + kAttackFloorMs);
		CHECK(high.attackMs == kLoopEnvelope.attackHigh + kAttackFloorMs);
		CHECK(high.releaseMs == kLoopEnvelope.releaseHigh);
		EnvelopeTimes mid = TideEnvelope(kLoopEnvelope, 2048);
		// Exponential: the middle of the tide is the geometric middle.
		int32_t geo = (int32_t)std::sqrt((double)kLoopEnvelope.releaseLow * kLoopEnvelope.releaseHigh);
		CHECK(std::abs(mid.releaseMs - geo) < 5);
	});

	Test("voice: Main is Time; the morning follows it", [] {
		Sim s(1, 0);
		s.RunMs(500);
		CHECK(s.control().GetMorning().sun < 50);
		s.in.knob[0] = 4095;
		s.RunMs(1000);
		CHECK(s.control().GetMorning().sun > 4050);
		s.in.knob[0] = KnobFor(kLowWaterQ8);
		s.RunMs(1000);
		CHECK(s.control().GetMorning().tide < 40);
	});

	Test("voice: plays, and stays inside the DAC's range", [] {
		Sim s(2, KnobFor(kLowWaterQ8));
		s.keep = true;
		s.RunMs(3000);
		CHECK(Rms(s, 3000) > 20);
		int32_t peak = 0;
		for (int16_t v : s.outL)
			peak = std::max(peak, std::abs((int32_t)v / 16));
		CHECK(peak <= 2047);
	});

	Test("voice: plucky at low water, swelling at dawn's high tide", [] {
		// The first loop strikes as the card starts. Its loudness 600ms
		// later, against its peak: at low tide the pluck has gone; at high
		// tide it's still swelling.
		auto shape = [](int32_t knob) {
			Sim s(3, knob);
			int32_t peak = 0, later = 0;
			for (int ms = 0; ms < 3000; ms++)
			{
				s.RunMs(1);
				int32_t l = s.control().Voice(1).Level();
				peak = std::max(peak, l);
				if (ms == 600)
					later = l;
			}
			return std::make_pair(peak, later);
		};
		auto low = shape(KnobFor(kLowWaterQ8));
		auto high = shape(0);
		CHECK(low.second < low.first / 4); // already faded
		CHECK(high.second < high.first);   // still climbing at 600ms...
		CHECK(high.first > 15000);		   // ...to a full swell
	});
}

// ---- The Icarus loop -------------------------------------------------

// A loop on its own, set up as core 1 would.
struct LoopRig
{
	std::unique_ptr<IcarusLoop> loop = std::make_unique<IcarusLoop>();
	EngineParams p;

	LoopRig(int32_t delayMs, int32_t feedbackQ12, int32_t driveQ12 = 5120)
	{
		p.delayQ8 = delayMs * 24 * 256;
		p.feedbackQ12 = feedbackQ12;
		p.driveQ12 = driveQ12;
		p.ladderCoef = CutoffCoef(HzToPitchQ8(8000) + 12 * 256);
		p.ladderResQ12 = kLadderResonanceQ12;
		p.destructQ15 = 32768;
		// Let the read point settle.
		int32_t l, r;
		for (int i = 0; i < 4800; i++)
			Step(0, l, r);
	}

	void Step(int32_t in, int32_t &l, int32_t &r) { loop->Process(in, in, p, p.feedbackQ12, p.destructQ15, l, r); }
};

static void Loop()
{
	Test("loop: the echo comes back at the delay time", [] {
		LoopRig rig(100, 2048);
		int32_t l, r, firstAt = 0;
		for (int i = 0; i < 9600; i++)
		{
			// A short burst (a 1ms click) into the loop.
			rig.Step(i < 48 ? 20000 : 0, l, r);
			if (i > 1000 && firstAt == 0 && std::abs(l) + std::abs(r) > 200)
				firstAt = i;
		}
		// 100ms later, give or take the ladder's and allpasses' smear.
		CHECK(firstAt > 4800 - 48 && firstAt < 4800 + 200);
	});

	Test("loop: well below unity, echoes die to true silence", [] {
		// (Drive 1.25 puts the loop's own gain at unity with feedback a
		// little over 0.8, as in Icarus.)
		for (int32_t fb : {kFeedbackMinQ12, 2400})
		{
			LoopRig rig(250, fb);
			int32_t l, r;
			Random noise(5);
			for (int i = 0; i < 24000; i++)
				rig.Step((int32_t)(noise.Next() >> 17) - 16384, l, r);
			// A whole second of zeros within 30s: nothing left circulating.
			int32_t zeros = 0;
			for (int i = 0; i < 48000 * 30 && zeros < 48000; i++)
			{
				rig.Step(0, l, r);
				zeros = l == 0 && r == 0 ? zeros + 1 : 0;
			}
			CHECK(zeros == 48000);
		}
	});

	Test("loop: past unity it burns, but holds its level", [] {
		LoopRig rig(300, kFeedbackMaxQ12, kDriveMaxQ12);
		int32_t l, r, peak = 0;
		Random noise(6);
		for (int i = 0; i < 4800; i++)
			rig.Step((int32_t)(noise.Next() >> 17) - 16384, l, r);
		int64_t energy = 0;
		for (int i = 0; i < 48000 * 10; i++)
		{
			rig.Step(0, l, r);
			peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
			if (i > 48000 * 9)
				energy += (int64_t)l * l;
		}
		CHECK(peak <= 46000);		  // the clipper and ladder hold it
		CHECK(energy / 48000 > 1000000); // and it's still going after 10s
	});

	Test("loop: the sun sets the delay time", [] {
		Sim s(1, 0);
		s.RunMs(3000);
		int32_t dawn = s.inst->Params().delayQ8 / 256 / 24; // ms
		s.in.knob[0] = 4095;
		s.RunMs(3000);
		int32_t morning = s.inst->Params().delayQ8 / 256 / 24;
		CHECK(std::abs(dawn - kDelayDawnMs) <= 2);
		CHECK(std::abs(morning - kDelayMorningMs) <= 3);
		s.in.knob[0] = KnobFor(kSunriseQ8);
		s.RunMs(3000);
		CHECK(std::abs(s.inst->Params().delayQ8 / 256 / 24 - kDelaySunriseMs) <= 3);
	});

	Test("loop: Sun (X) sets feedback and drive; a running tide nudges it", [] {
		Sim s(1, KnobFor(kLowWaterQ8), 0); // slack water: no nudge
		s.RunMs(500);
		CHECK(s.inst->Params().feedbackQ12 == kFeedbackMinQ12);
		CHECK(s.inst->Params().driveQ12 == 4096);
		s.in.knob[1] = 2048;
		s.RunMs(500);
		CHECK(s.inst->Params().driveQ12 == 5120); // Icarus's 1.25
		CHECK(s.inst->Params().feedbackQ12 == kFeedbackCentreQ12);
		s.in.knob[1] = 4095;
		s.RunMs(500);
		CHECK(s.inst->Params().feedbackQ12 == kFeedbackMaxQ12);
		CHECK(s.inst->Params().driveQ12 == kDriveMaxQ12);
		// At first light the tide is running hardest.
		s.in.knob[0] = 0;
		s.RunMs(1000);
		int32_t rate = s.control().GetMorning().tideRate;
		CHECK(rate > 3000);
		CHECK(s.inst->Params().feedbackQ12 == kFeedbackMaxQ12 + ((kTideFeedbackNudgeQ12 * rate) >> 12));
	});

	Test("loop: CV In 2 leaves Sun alone (it's Water's)", [] {
		Sim s(1, KnobFor(kLowWaterQ8), 1000);
		s.RunMs(500);
		int32_t fb = s.inst->Params().feedbackQ12;
		s.in.cv[1] = 1500;
		s.RunMs(500);
		CHECK(s.inst->Params().feedbackQ12 == fb);
	});
}

// ---- Water ---------------------------------------------------------------

static void WaterTests()
{
	Test("water: open in the centre, a dead zone either side", [] {
		for (int32_t y : {2048, 2048 - 60, 2048 + 60})
		{
			Sim s(1, 2048, 2048, y);
			s.RunMs(500);
			CHECK(s.inst->Params().waterMode == kWaterOpen);
		}
	});

	Test("water: left drowns, right thins, evenly", [] {
		Sim s(1, 2048, 2048, 2048);
		int32_t last = 32768;
		for (int32_t y = 1900; y >= 0; y -= 100)
		{
			s.in.knob[2] = y;
			s.RunMs(400);
			CHECK(s.inst->Params().waterMode == kWaterLowpass);
			CHECK(s.inst->Params().waterCoef < last);
			last = s.inst->Params().waterCoef;
		}
		CHECK(std::abs(last - CutoffCoef(HzToPitchQ8(kWaterLowpassClosedHz))) < 8);
		last = 0;
		for (int32_t y = 2200; y <= 4095; y += 100)
		{
			s.in.knob[2] = y;
			s.RunMs(400);
			CHECK(s.inst->Params().waterMode == kWaterHighpass);
			CHECK(s.inst->Params().waterCoef > last);
			last = s.inst->Params().waterCoef;
		}
		s.in.knob[2] = 4095;
		s.RunMs(500);
		CHECK(std::abs(s.inst->Params().waterCoef - CutoffCoef(HzToPitchQ8(kWaterHighpassClosedHz))) < 8);
	});

	// Two runs alike but for Water: the difference between them is exactly
	// what Water is doing. Y starts at `from` and moves to `to` after 6s;
	// returns the biggest sample-to-sample step in that difference over the
	// next 600ms (a click is a sudden step in it).
	auto waterStep = [](int32_t from, int32_t to) {
		Sim a(4, 2900, 2048, from), b(4, 2900, 2048, 2048);
		a.keep = b.keep = true;
		a.RunMs(6000);
		b.RunMs(6000);
		a.in.knob[2] = to;
		a.RunMs(600);
		b.RunMs(600);
		int32_t worst = 0;
		for (size_t i = 6000 * 48; i < a.outL.size(); i++)
		{
			int32_t d0 = (a.outL[i - 1] - b.outL[i - 1]) / 16, d1 = (a.outL[i] - b.outL[i]) / 16;
			worst = std::max(worst, std::abs(d1 - d0));
		}
		return worst;
	};

	Test("water: crossing the centre doesn't click", [waterStep] {
		// Just inside each side, into the centre and out again. (Before
		// the fade-in, the low-cut side stepped by over 200 counts here.)
		for (int32_t edge : {2048 + 90, 2048 + 200, 2048 - 90, 2048 - 200})
		{
			int32_t held = waterStep(edge, edge);
			CHECK(waterStep(edge, 2048) <= held + 8);
			CHECK(waterStep(2048, edge) <= held + 8);
		}
	});

	Test("water: slammed across, one filter fades out before the other comes in", [] {
		for (int32_t from : {0, 4095})
		{
			Sim s(6, 2900, 2048, from);
			s.RunMs(3000);
			s.in.knob[2] = 4095 - from;
			int32_t mode = s.inst->WaterMode(), sent = s.inst->Params().waterMode;
			for (int i = 0; i < 48 * 1000; i++)
			{
				s.Step();
				int32_t now = s.inst->WaterMode();
				if (now != mode)
				{
					CHECK(mode == kWaterOpen || s.inst->WaterMix() == 0);
					mode = now;
				}
				int32_t p = s.inst->Params().waterMode;
				CHECK(!(p != kWaterOpen && sent != kWaterOpen && p != sent));
				sent = p;
			}
			CHECK(mode == (from == 0 ? kWaterHighpass : kWaterLowpass));
		}
	});

	Test("water: CV In 2 adds to Y", [] {
		Sim s(7, 2048, 2048, 2048);
		s.in.cv[1] = -700; // about -2V: towards drowning
		s.RunMs(500);
		CHECK(s.inst->Params().waterMode == kWaterLowpass);
		s.in.cv[1] = 700;
		s.RunMs(500);
		CHECK(s.inst->Params().waterMode == kWaterHighpass);
		s.in.cv[1] = 0;
		s.RunMs(500);
		CHECK(s.inst->Params().waterMode == kWaterOpen);
	});

	Test("water: fully left is much quieter than open", [] {
		auto level = [](int32_t y) {
			Sim s(4, KnobFor(kSunriseQ8 + 4000), 2048, y);
			s.keep = true;
			s.RunMs(8000);
			return Rms(s, 6000);
		};
		double open = level(2048), drowned = level(0), thin = level(4095);
		CHECK(drowned < open / 3);
		CHECK(thin < open);
	});
}

// ---- The loops and keys ------------------------------------------------------

static void LoopsTests()
{
	Test("loops: three voices, each its own note, all in the key", [] {
		Sim s(11, KnobFor(kSunriseQ8));
		int32_t root = s.control().Root(), scale = s.control().ScaleIndex();
		int seen[3] = {};
		for (int t = 0; t < 120; t++)
		{
			s.RunMs(1000);
			for (int i = 0; i < 3; i++)
			{
				if (!s.control().GetLoops().Active(i))
					continue;
				seen[i]++;
				int32_t n = s.control().GetLoops().Note(i);
				CHECK(InScale(scale, root, n));
				CHECK(n >= kMelodyBottom + root && n <= kMelodyBottom + root + kMelodySpan);
			}
		}
		for (int i = 0; i < 3; i++)
			CHECK(seen[i] > 20); // every loop plays a good share of the time
		CHECK(s.control().GetLoops().Strikes() > 20);
	});

	Test("loops: a new loop waits for its voice to fall silent", [] {
		// Watch every voice: its note only ever changes while it's silent.
		Sim s(12, 0); // dawn: high tide, long swells
		int32_t note[4] = {}, level[4] = {};
		for (int ms = 0; ms < 120000; ms++)
		{
			s.RunMs(1);
			for (int v = 1; v < 4; v++)
			{
				int32_t n = s.control().Voice(v).Note();
				if (n != note[v])
					CHECK(level[v] == 0);
				note[v] = n;
				level[v] = s.control().Voice(v).Level();
			}
		}
	});

	Test("loops: rub notes are rare, and never on high-tide swells", [] {
		// Minor is the only scale with a rub note (its flat 6th).
		Random rng(3);
		int rub = 0, total = 0, rubHigh = 0;
		const int32_t none[1] = {0};
		for (int i = 0; i < 20000; i++)
		{
			int32_t n = LoopNote(rng, 0, 1, 1000, none, 0);
			rub += IsRub(0, n);
			total++;
			rubHigh += IsRub(0, LoopNote(rng, 0, 1, 4000, none, 0));
		}
		// Plain chance would be 2 notes in 15 (13%).
		CHECK(rub * 100 / total < 6);
		CHECK(rubHigh == 0);
	});

	Test("reseed: tap down; notes stop, the drone glides, then the new key", [] {
		for (int32_t knob : {KnobFor(kLowWaterQ8), 0, 4095}) // plucks, swells, and with the lead
		{
			Sim s(13, knob);
			s.RunMs(20000);
			int32_t oldRoot = s.control().Root();
			uint32_t strikes = s.control().GetLoops().Strikes(), leadNotes = s.control().GetLead().Notes();
			s.in.sw = kSwitchDown;
			s.RunMs(30);
			s.in.sw = kSwitchMiddle;
			CHECK(s.control().Reseeding());
			CHECK(s.control().Reseeds() == 1);
			// Every loop ends at once: no last repeats.
			for (int i = 0; i < 3; i++)
				CHECK(!s.control().GetLoops().Active(i));
			// The drone heads for the new root straight away.
			CHECK(s.control().GetDrone().Gliding() || s.control().Root() == oldRoot);
			// No melodic note starts until the drone has arrived and the old
			// notes have died away.
			while (s.control().Reseeding())
			{
				s.RunMs(1);
				CHECK(s.control().GetLoops().Strikes() == strikes);
				CHECK(s.control().GetLead().Notes() == leadNotes);
			}
			CHECK(!s.control().GetDrone().Gliding());
			CHECK((s.control().GetDrone().PitchQ16() >> 16) % 12 == s.control().Root());
			for (int v = 0; v < kVoices; v++)
				CHECK(s.control().Voice(v).Silent());
			// Then the new key plays, soon.
			s.RunMs(3000);
			CHECK(s.control().GetLoops().Strikes() > strikes);
		}
	});

	Test("reseed: the feedback dips while the old key drains", [] {
		Sim s(13, KnobFor(kLowWaterQ8));
		s.RunMs(5000);
		int32_t full = s.inst->Params().feedbackQ12;
		s.in.sw = kSwitchDown;
		s.RunMs(kReseedDuckMs + 30);
		s.in.sw = kSwitchMiddle;
		CHECK(s.inst->Params().feedbackQ12 < full / 2);
		while (s.control().Reseeding())
			s.RunMs(10);
		s.RunMs(kReseedRestoreMs + 100);
		CHECK(s.inst->Params().feedbackQ12 == full);
	});

	Test("reseed: a second tap during one does nothing", [] {
		Sim s(15);
		for (int i = 0; i < 2; i++)
		{
			s.in.sw = kSwitchDown;
			s.RunMs(30);
			s.in.sw = kSwitchMiddle;
			s.RunMs(30);
		}
		CHECK(s.control().Reseeds() == 1);
	});

	Test("pulse ins: pulling a cable out never triggers anything", [] {
		Sim s(14);
		s.RunMs(1000);
		uint32_t phrases = s.control().GetLead().Phrases();
		// Edges, then the jack found empty within 20ms: ignored.
		for (int i = 0; i < 2; i++)
		{
			s.pulse[i] = true;
			s.RunMs(5);
			s.in.pulseConnected[i] = false;
			s.RunMs(50);
		}
		CHECK(s.control().TuringSteps() == 0);
		CHECK(s.control().GetLead().Phrases() == phrases);
		CHECK(s.control().Reseeds() == 0);
	});
}

// ---- The lead -------------------------------------------------------------------

static void LeadTests()
{
	Test("lead: silent before sunrise", [] {
		Sim s(21, KnobFor(kCivilDawnQ8));
		s.RunMs(300000);
		CHECK(s.control().GetLead().Phrases() == 0);
		CHECK(s.control().Voice(kLead).Silent());
	});

	Test("lead: phrases after sunrise, more often as the sun climbs", [] {
		Sim early(22, KnobFor(kSunriseQ8 + 600));
		early.RunMs(600000);
		Sim late(22, 4095);
		late.RunMs(600000);
		uint32_t a = early.control().GetLead().Phrases(), b = late.control().GetLead().Phrases();
		CHECK(a >= 10);
		CHECK(b > a * 2);
	});

	Test("lead: phrases of 3-6 notes, mostly stepwise, in the key", [] {
		Sim s(23, 4095);
		int32_t root = s.control().Root(), scale = s.control().ScaleIndex();
		int32_t notes[kMelodySpan + 1];
		int32_t count = MelodyNotes(root, scale, notes);
		auto degree = [&](int32_t n) {
			for (int i = 0; i < count; i++)
				if (notes[i] == n)
					return i;
			return -100;
		};
		uint32_t lastCount = 0;
		int32_t prev = -1, steps = 0, moves = 0;
		for (int ms = 0; ms < 600000; ms++)
		{
			s.RunMs(1);
			uint32_t c = s.control().GetLead().Notes();
			if (c == lastCount)
				continue;
			lastCount = c;
			int32_t n = s.control().Voice(kLead).Note();
			CHECK(degree(n) >= 0);
			if (prev >= 0 && degree(prev) >= 0)
			{
				int32_t d = std::abs(degree(n) - degree(prev));
				moves++;
				steps += d <= 1 ? 1 : 0;
			}
			prev = n;
		}
		uint32_t phrases = s.control().GetLead().Phrases();
		CHECK(lastCount >= phrases * 3 && lastCount <= phrases * 6);
		CHECK(moves > 50 && steps * 100 / moves > 55); // between phrases can leap
	});

	Test("lead: scrub back to night and it stops", [] {
		Sim s(24, 4095);
		while (s.control().GetLead().Resting())
			s.RunMs(10);
		uint32_t notes = s.control().GetLead().Notes();
		s.in.knob[0] = 0;
		s.RunMs(15000);
		CHECK(s.control().Voice(kLead).Silent());
		CHECK(s.control().GetLead().Notes() == notes);
	});

	Test("lead: Pulse In 2 calls a phrase, even at night", [] {
		Sim s(25, 0);
		s.RunMs(1000);
		s.pulse[1] = true;
		s.RunMs(100);
		CHECK(s.control().GetLead().Phrases() == 1);
		s.RunMs(5000);
		CHECK(s.control().GetLead().Notes() >= 2);
	});

	Test("lead: vibrato eases in as a note holds; the filter swells", [] {
		Sim s(26, 0);
		s.RunMs(500);
		// The last, long note of a phrase (one struck afresh, not glided
		// into): its first 250ms, and 1.6-2.4s in.
		for (int tries = 0; tries < 20; tries++)
		{
			while (!s.control().GetLead().Resting() || !s.control().Voice(kLead).Silent())
				s.RunMs(10);
			s.pulse[1] = true;
			while (!s.control().GetLead().Holding())
				s.RunMs(1);
			if (!s.control().Voice(kLead).Gliding())
				break;
		}
		uint32_t lo1 = UINT32_MAX, hi1 = 0, lo2 = UINT32_MAX, hi2 = 0;
		int32_t coefStart = s.inst->Params().leadCoef, coefPeak = 0;
		for (int ms = 0; ms < 2400; ms++)
		{
			s.RunMs(1);
			uint32_t inc = s.inst->Params().voice[kLead].saw[0].inc;
			coefPeak = std::max(coefPeak, s.inst->Params().leadCoef);
			if (ms < 250)
				lo1 = std::min(lo1, inc), hi1 = std::max(hi1, inc);
			if (ms >= 1600)
				lo2 = std::min(lo2, inc), hi2 = std::max(hi2, inc);
		}
		CHECK(coefPeak > coefStart + 1000); // the brightness swell
		// Early: only the slow drift. Later: the vibrato too, about +/-16
		// cents (a cent is ~0.06% of the step).
		CHECK(hi2 - lo2 > (hi1 - lo1) * 2);
		CHECK((double)(hi2 - lo2) / lo2 > 0.012);
	});
}

// ---- The drone -------------------------------------------------------------------

static void DroneTests()
{
	Test("drone: root two octaves below the melody, fading in", [] {
		Sim s(31);
		CHECK(s.control().GetDrone().PitchQ16() == (kDroneBottom + s.control().Root()) << 16);
		int32_t early = s.inst->Params().droneGain;
		s.RunMs(kDroneFadeInMs + 100);
		CHECK(early < s.inst->Params().droneGain / 10);
		CHECK(s.inst->Params().droneGain > 1000);
	});

	Test("drone: the fifth is pure, 3:2, and still", [] {
		Sim s(32);
		uint32_t lo = UINT32_MAX, hi = 0;
		for (int ms = 0; ms < 20000; ms++)
		{
			s.RunMs(1);
			const EngineParams &p = s.inst->Params();
			double root = (p.drone[1].inc + p.drone[2].inc) / 2.0;
			// Within the roots' few cents of spread and drift of exactly 3:2.
			CHECK(std::fabs(p.drone[0].inc / root - 1.5) < 1.5 * 0.006);
			lo = std::min(lo, p.drone[0].inc);
			hi = std::max(hi, p.drone[0].inc);
		}
		CHECK(hi == lo); // the fifth doesn't drift
		uint32_t fifth = PitchToInc((kDroneBottom + s.control().Root()) << 16) / 2 * 3;
		CHECK(lo == fifth);
	});

	Test("drone: a reseed glides it to the new root, the nearer way", [] {
		for (uint32_t seed = 40; seed < 46; seed++)
		{
			Sim s(seed, KnobFor(kLowWaterQ8));
			s.RunMs(5000);
			int32_t before = s.control().GetDrone().PitchQ16();
			s.in.sw = kSwitchDown;
			s.RunMs(30);
			s.in.sw = kSwitchMiddle;
			// Gliding from the tap, not jumping...
			s.RunMs(kDroneGlideMs / 2 - 30);
			int32_t mid = s.control().GetDrone().PitchQ16();
			s.RunMs(kDroneGlideMs / 2 + 100);
			int32_t after = s.control().GetDrone().PitchQ16();
			CHECK(!s.control().GetDrone().Gliding());
			CHECK((after >> 16) % 12 == s.control().Root() % 12 + (kDroneBottom % 12));
			CHECK(std::abs(after - before) <= 6 << 16);
			if (after != before)
				CHECK((mid > before && mid < after) || (mid < before && mid > after));
		}
	});
}

// ---- The morning's data at work ----------------------------------------------------

static void DataMappings()
{
	Test("destruction: dropouts before dawn, none in the morning", [] {
		auto dips = [](int32_t knob) {
			Sim s(51, knob);
			s.RunMs(1000);
			int count = 0;
			bool in = false;
			for (int ms = 0; ms < 30000; ms++)
			{
				s.RunMs(1);
				bool dip = s.inst->Params().destructQ15 < 30000;
				count += dip && !in;
				in = dip;
			}
			return count;
		};
		int night = dips(0), morning = dips(4095);
		CHECK(night > 30); // about 3 a second at the shortest delay, overlapping
		CHECK(morning == 0);
	});

	Test("swoop: once as the sun clears the horizon", [] {
		Sim s(52, KnobFor(kCivilDawnQ8));
		s.RunMs(1000);
		CHECK(s.control().SwoopMs() == 0);
		int32_t before = s.inst->Params().delayQ8;
		s.in.knob[0] = KnobFor(kSunriseQ8 + 300);
		int32_t peak = 0;
		bool fired = false;
		for (int ms = 0; ms < 4000; ms++)
		{
			s.RunMs(1);
			fired = fired || s.control().SwoopMs() > 0;
			peak = std::max(peak, s.inst->Params().delayQ8);
		}
		CHECK(fired);
		CHECK(s.control().SwoopMs() == 0);
		// The delay stretched by most of the swoop on top of the sun's own move.
		int32_t sunrise = kDelaySunriseMs * 24 * 256;
		CHECK(peak > sunrise + kSwoopMs * 24 * 256 / 2);
		CHECK(before < sunrise);
		// Hovering at the horizon doesn't fire it again...
		for (int i = 0; i < 20; i++)
		{
			s.in.knob[0] = KnobFor(kSunriseQ8 + (i & 1 ? 200 : -200));
			s.RunMs(200);
			CHECK(s.control().SwoopMs() == 0);
		}
		// ...but going well back into the dark and up again does.
		s.in.knob[0] = KnobFor(kCivilDawnQ8);
		s.RunMs(2000);
		s.in.knob[0] = KnobFor(kSunriseQ8 + 300);
		s.RunMs(1000);
		CHECK(s.control().SwoopMs() > 0);
	});

	Test("swoop: none if the card wakes after sunrise", [] {
		Sim s(53, 3500);
		for (int ms = 0; ms < 3000; ms++)
		{
			s.RunMs(1);
			CHECK(s.control().SwoopMs() == 0);
		}
	});

	Test("time: CV In 1 adds to Main", [] {
		Sim s(54, 1000);
		s.in.cv[0] = 1000;
		s.RunMs(1000);
		CHECK(std::abs(s.control().TimeQ8() - 3000 * kDayEndQ8 / 4095) < 8);
		s.in.cv[0] = -2048;
		s.RunMs(1000);
		CHECK(s.control().TimeQ8() == 0);
	});

	Test("cv out 1: around the tide, choppier in wind", [] {
		// Low water, still-ish air vs the end of the window (rising tide,
		// the most wind).
		auto watch = [](int32_t knob, double &mean, double &motion) {
			Sim s(55, knob);
			s.RunMs(2000);
			double sum = 0, move = 0;
			int32_t last = s.inst->Params().cv1;
			const int n = 60000;
			for (int ms = 0; ms < n; ms++)
			{
				s.RunMs(1);
				int32_t v = s.inst->Params().cv1;
				CHECK(v >= 0 && v <= kCvMax * 128);
				sum += v;
				move += std::abs(v - last);
				last = v;
			}
			mean = sum / n / (kCvMax * 128.0) * 4096;
			motion = move / n;
			return s.control().GetMorning();
		};
		double meanLow, motionLow, meanHigh, motionHigh;
		Morning low = watch(KnobFor(kLowWaterQ8), meanLow, motionLow);
		Morning high = watch(4095, meanHigh, motionHigh);
		CHECK(low.tide < 100 && high.tide > 1500);
		CHECK(meanLow < 900);
		CHECK(std::fabs(meanHigh - high.tide) < 700);
		CHECK(high.wind > low.wind);
		CHECK(motionHigh > motionLow * 2);
	});

	Test("pulse outs: lead notes on 1; 2 silent without a clock", [] {
		Sim s(57, 4095);
		int edges = 0;
		bool last = false;
		for (int ms = 0; ms < 120000; ms++)
		{
			s.RunMs(1);
			bool p = s.inst->Params().pulse[0];
			edges += p && !last;
			last = p;
			CHECK(!s.inst->Params().pulse[1]);
		}
		CHECK(edges > 20);
		CHECK(edges == (int)s.control().GetLead().Notes());
	});

	Test("leds: the sun's height, bottom row first", [] {
		Sim s(58, 0);
		s.RunMs(500);
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] < 100);
		s.in.knob[0] = KnobFor(kSunriseQ8);
		s.RunMs(5000); // past the swoop's glow
		CHECK(s.led[4] > 4000 && s.led[5] > 4000); // bottom row lit
		CHECK(s.led[0] == 0 && s.led[1] == 0);	   // top row dark
		s.in.knob[0] = 4095;
		s.RunMs(1000);
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] > 4000);
	});
}

// ---- The Levels page -----------------------------------------------------------------

static void flip(Sim &s, int32_t sw)
{
	s.in.sw = sw;
	s.RunMs(5);
}

static void LevelsTests()
{
	Test("levels: up, Main is the Turing machine, X melody, Y drone; the mix is fixed", [] {
		Sim s(61, 2048, 2048, 2048);
		int32_t dry = s.inst->Params().dryGain, wet = s.inst->Params().wetGain;
		flip(s, kSwitchUp);
		// Each waits for its knob to reach its stored value first.
		s.in.knob[0] = kTuringStart;
		s.in.knob[1] = kMelodyLevelStart;
		s.in.knob[2] = kDroneLevelStart;
		s.RunMs(5);
		s.in.knob[0] = 2048;
		s.in.knob[1] = 0;
		s.in.knob[2] = 0;
		s.RunMs(500);
		CHECK(s.control().TuringKnob() == 2048);
		for (int v = 0; v < kVoices; v++)
			CHECK(s.inst->Params().voice[v].gainL == 0);
		CHECK(s.inst->Params().droneGain == 0);
		CHECK(s.inst->Params().dryGain == dry);
		CHECK(s.inst->Params().wetGain == wet);
		CHECK(wet > dry); // about two thirds wet
	});

	Test("levels: power-on defaults, melody at 80% and the drone at 40%", [] {
		Sim s(66);
		s.RunMs(kDroneFadeInMs + 100);
		// The Levels page untouched: the knobs squared give 80% and 40%.
		auto volume = [](int32_t knob) { return (double)knob * knob / (4095.0 * 4095.0); };
		CHECK(std::fabs(volume(s.control().MelodyKnob()) - 0.8) < 0.002);
		CHECK(std::fabs(volume(s.control().DroneKnob()) - 0.4) < 0.002);
		// The drone's gain: 40% of full, give or take its slow breath.
		double full = kDronePeakQ14, now = s.inst->Params().droneGain;
		CHECK(now > full * 0.4 * (4096 - kDroneBreathQ12) / 4096 - 4);
		CHECK(now < full * 0.4 * (4096 + kDroneBreathQ12) / 4096 + 4);
	});

	Test("levels: the melody about as loud all morning", [] {
		// Melody only (the drone off), at first light, around civil dawn and
		// at the end of the window. Without the makeup the spread measured
		// 16dB; now it should be well under 9.
		auto loudness = [](int32_t time) {
			double sum = 0;
			size_t n = 0;
			for (uint32_t seed : {21u, 22u})
			{
				Sim s(seed, time, 2048, 2048);
				flip(s, kSwitchUp);
				s.in.knob[1] = kMelodyLevelStart;
				s.in.knob[2] = kDroneLevelStart;
				s.RunMs(5);
				s.in.knob[2] = 0;
				flip(s, kSwitchMiddle);
				s.RunMs(5000);
				s.keep = true;
				s.RunMs(40000);
				for (int16_t v : s.outL)
					sum += (double)v * v;
				n += s.outL.size();
			}
			return 10 * std::log10(sum / n);
		};
		double lo = 1e9, hi = -1e9;
		for (int32_t t : {0, 1500, 4095})
		{
			double l = loudness(t);
			lo = std::min(lo, l);
			hi = std::max(hi, l);
		}
		CHECK(hi - lo < 9);
	});

	Test("levels: Time holds while Main moves", [] {
		Sim s(62, 1000);
		int32_t t = s.control().TimeQ8();
		flip(s, kSwitchUp);
		s.in.knob[0] = 3500;
		s.RunMs(1000);
		CHECK(s.control().TimeQ8() == t);
	});

	Test("levels: pickup both ways; nothing jumps", [] {
		Sim s(63, 1000, 1000, 1000);
		flip(s, kSwitchUp);
		s.in.knob[0] = 2000; // towards the Turing knob's 4095: not there
		s.RunMs(5);
		CHECK(s.control().TuringKnob() == kTuringStart);
		s.in.knob[0] = 4095; // reaches it: picked up
		s.RunMs(5);
		s.in.knob[0] = 3500;
		s.RunMs(5);
		CHECK(s.control().TuringKnob() == 3500);
		flip(s, kSwitchMiddle);
		int32_t t = s.control().TimeQ8();
		s.in.knob[0] = 2000; // towards Time's 1000, not there yet
		s.RunMs(500);
		CHECK(s.control().TimeQ8() == t);
		s.in.knob[0] = 900; // passes through: picked up
		s.RunMs(1000);
		CHECK(std::abs(s.control().TimeQ8() - 900 * kDayEndQ8 / 4095) < 4);
		// And back up: the Turing knob waits at 3500.
		flip(s, kSwitchUp);
		s.in.knob[0] = 1500;
		s.RunMs(20);
		CHECK(s.control().TuringKnob() == 3500);
	});

	Test("levels: power on with the switch up", [] {
		Sim s(64, 0, 4095, 4095, kSwitchUp);
		// The Levels page took its knobs; Play waits at its defaults.
		CHECK(s.control().TuringKnob() == 0);
		CHECK(std::abs(s.control().TimeQ8() - kTimeStart * kDayEndQ8 / 4095) < 4);
	});

	Test("levels: the LEDs show the Turing machine and the two levels", [] {
		Sim s(65);
		flip(s, kSwitchUp);
		s.in.knob[0] = kTuringStart;
		s.in.knob[1] = kMelodyLevelStart;
		s.in.knob[2] = kDroneLevelStart;
		s.RunMs(5);
		s.in.knob[1] = 0;
		s.in.knob[2] = 2048;
		s.RunMs(5);
		// Locked forwards: top right lit, top left dark.
		CHECK(s.led[1] > 4090 && s.led[0] == 0);
		CHECK(s.led[2] == 0 && s.led[3] == 0);
		CHECK(s.led[4] == 1024 && s.led[5] == 1024);
		// Fully random: both dark. Locked pendulum: top left.
		s.in.knob[0] = 2048;
		s.RunMs(5);
		CHECK(s.led[0] == 0 && s.led[1] == 0);
		s.in.knob[0] = 0;
		s.RunMs(5);
		CHECK(s.led[0] > 4090 && s.led[1] == 0);
	});
}

// ---- The Turing machine -------------------------------------------------------------------

// A clock pulse into Pulse In 1, and long enough for it to be confirmed.
static void TuringClock(Sim &s)
{
	s.pulse[0] = true;
	s.RunMs(kPulseConfirmMs + 5);
}

// Set Main on the Levels page (picking it up from its default), then back
// to the Play page.
static void SetTuring(Sim &s, int32_t knob)
{
	int32_t main = s.in.knob[0];
	flip(s, kSwitchUp);
	s.in.knob[0] = kTuringStart;
	s.RunMs(5);
	s.in.knob[0] = knob;
	s.RunMs(5);
	flip(s, kSwitchMiddle);
	s.in.knob[0] = main;
	s.RunMs(5);
}

static void TuringTests()
{
	Test("turing: a note of the key on CV Out 2 from the start", [] {
		for (uint32_t seed = 80; seed < 100; seed++)
		{
			Sim s(seed);
			int32_t root = s.control().Root(), n = s.inst->Params().turingNote;
			CHECK(InScale(s.control().ScaleIndex(), root, n));
			CHECK(n >= kTuringBottom + root && n <= kTuringBottom + root + kTuringSpan);
		}
	});

	Test("turing: no clock, no steps; each pulse one step", [] {
		Sim s(81);
		s.RunMs(5000);
		CHECK(s.control().TuringSteps() == 0);
		for (int i = 0; i < 10; i++)
			TuringClock(s);
		CHECK(s.control().TuringSteps() == 10);
	});

	Test("turing: locked clockwise, an 8-step loop that repeats exactly", [] {
		Sim s(82);
		uint32_t bits = s.control().GetTuring().Bits();
		int32_t notes[24];
		for (int i = 0; i < 24; i++)
		{
			TuringClock(s);
			notes[i] = s.inst->Params().turingNote;
		}
		for (int i = 8; i < 24; i++)
			CHECK(notes[i] == notes[i - 8]);
		CHECK(s.control().GetTuring().Bits() == bits);
	});

	Test("turing: locked anticlockwise, a pendulum, 1-8-1", [] {
		Sim s(83);
		SetTuring(s, 0);
		uint32_t bits = s.control().GetTuring().Bits();
		int32_t pos[29], notes[29];
		for (int i = 0; i < 29; i++)
		{
			TuringClock(s);
			pos[i] = s.control().GetTuring().Pos();
			notes[i] = s.inst->Params().turingNote;
		}
		int32_t lo = 8, hi = -1;
		for (int i = 1; i < 29; i++)
		{
			CHECK(std::abs(pos[i] - pos[i - 1]) == 1); // one step at a time, either way
			lo = std::min(lo, pos[i]);
			hi = std::max(hi, pos[i]);
		}
		CHECK(lo == 0 && hi == kTuringSteps - 1);
		for (int i = 14; i < 29; i++)
			CHECK(pos[i] == pos[i - 14] && notes[i] == notes[i - 14]); // there and back: 14 steps
		CHECK(s.control().GetTuring().Bits() == bits);
	});

	Test("turing: the centre is a coin toss every step; in between it slips", [] {
		auto flipRate = [](int32_t knob) {
			Sim s(84);
			SetTuring(s, knob);
			int flips = 0;
			const int n = 1500;
			for (int i = 0; i < n; i++)
			{
				uint32_t before = s.control().GetTuring().Bits();
				TuringClock(s);
				flips += s.control().GetTuring().Bits() != before;
			}
			return flips * 100 / n;
		};
		int centre = flipRate(2048), slip = flipRate(3300), lockedNearly = flipRate(4095 - kTuringLockZone / 2);
		CHECK(centre >= 44 && centre <= 56);
		CHECK(slip > 5 && slip < 30);
		CHECK(lockedNearly == 0);
	});

	Test("turing: Pulse Out 2 fires with the step when its bit is 1", [] {
		Sim s(85);
		SetTuring(s, 2048); // random, so plenty of both
		int ones = 0, fired = 0;
		for (int i = 0; i < 200; i++)
		{
			s.pulse[0] = true;
			s.RunMs(kPulseConfirmMs + 2);
			bool gate = s.control().GetTuring().Gate();
			bool pulse = s.inst->Params().pulse[1];
			CHECK(pulse == gate);
			ones += gate;
			fired += pulse;
			s.RunMs(kTriggerMs + 5);
			CHECK(!s.inst->Params().pulse[1]); // a trigger, not a gate
		}
		CHECK(ones > 60 && ones < 140);
		CHECK(fired == ones);
	});

	Test("turing: a new key re-pitches the same pattern", [] {
		Sim s(86, KnobFor(kLowWaterQ8));
		TuringClock(s);
		uint32_t bits = s.control().GetTuring().Bits();
		int32_t value = s.control().GetTuring().Value();
		s.in.sw = kSwitchDown;
		s.RunMs(30);
		s.in.sw = kSwitchMiddle;
		// It keeps running through the reseed, locked, in the new key.
		for (int i = 0; i < 8; i++)
			TuringClock(s);
		CHECK(s.control().GetTuring().Bits() == bits);
		CHECK(s.control().GetTuring().Value() == value);
		int32_t root = s.control().Root();
		CHECK(s.inst->Params().turingNote == s.control().GetTuring().Note(root, s.control().ScaleIndex()));
		CHECK(InScale(s.control().ScaleIndex(), root, s.inst->Params().turingNote));
	});
}

int main()
{
	Data();
	Voice();
	Loop();
	WaterTests();
	LoopsTests();
	LeadTests();
	DroneTests();
	DataMappings();
	LevelsTests();
	TuringTests();
	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? 1 : 0;
}
