// Host tests: build and run with `make -C test`.
//
// These check the behaviour the design brief promises, on a computer, before
// anything is flashed: lossless packing, pot catch-up, "a still knob holds
// the loop", "a moved knob can't be undone", noise immunity, CV routing per
// mode, pulse-out timing, reverb stability and output range.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "Sim.h"

static int failures = 0;

// One take (the whole buffer): layer 1's loop length at normal speed.
constexpr int kTake = imp::StereoBuffer::kFrames;
#define CHECK(cond, ...)                                  \
	do                                                    \
	{                                                     \
		if (!(cond))                                      \
		{                                                 \
			failures++;                                   \
			printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);                          \
			printf("\n");                                 \
		}                                                 \
	} while (0)

// Capture n samples of left output.
static std::vector<int32_t> Capture(Sim &s, int n)
{
	std::vector<int32_t> v(n);
	for (int i = 0; i < n; i++)
	{
		s.Step();
		v[i] = s.out.audio[0];
	}
	return v;
}

static int CountReseeds(Sim &s, int n)
{
	uint32_t before = s.inst->Reseeds();
	s.Run(n);
	return (int)(s.inst->Reseeds() - before);
}

static void TestPacking()
{
	printf("buffer stores samples exactly\n");
	static imp::StereoBuffer b;
	int bad = 0;
	for (int32_t v = -2048; v <= 2047; v++)
	{
		int32_t w = -1 - v; // a different value on the other channel
		b.Write((v + 2048) % imp::StereoBuffer::kFrames, v, w);
		int32_t l, r;
		b.Read((v + 2048) % imp::StereoBuffer::kFrames, l, r);
		if (l != v || r != w)
			bad++;
	}
	CHECK(bad == 0, "%d values changed", bad);
	int32_t l, r;
	b.Write(5, 5000, -5000); // out of range clamps rather than wrapping
	b.Read(5, l, r);
	CHECK(l == 2047 && r == -2048, "clamp gave %d %d", l, r);
}

static void TestHook()
{
	printf("pot catch-up\n");
	imp::Hook h(1000);
	h.Unhook(3000);
	h.Update(2900);
	CHECK(h.Value() == 1000 && !h.Hooked(), "followed knob before catching up");
	h.Update(1500);
	CHECK(h.Value() == 1000, "still frozen above the stored value");
	h.Update(900); // crossed 1000
	CHECK(h.Hooked() && h.Value() == 900, "didn't hook on crossing");
	h.Update(1200);
	CHECK(h.Value() == 1200, "didn't follow once hooked");
}

// Levels 2 and 3 off and reverb dry, so the output is exactly layer 1 --
// whose cycle is one take long at speed 1.
static Sim QuietSim(int32_t chaos, uint32_t seed = 99)
{
	Sim s(seed, chaos, 2048, 20);
	s.in.sw = imp::kSwitchUp; // levels page: L1 full, L2/L3 off
	s.in.knob[0] = 4095;
	s.in.knob[1] = 20;
	s.in.knob[2] = 20;
	s.Run(10);
	s.in.knob[0] = 3500; // pass through the stored levels to hook them
	s.in.knob[1] = 2400;
	s.in.knob[2] = 1600;
	s.Run(10);
	s.in.knob[0] = 4095;
	s.in.knob[1] = 20;
	s.in.knob[2] = 20;
	s.Run(10);
	s.in.sw = imp::kSwitchMiddle;
	s.in.knob[0] = chaos; // perform page: chaos stored value is where Init grabbed it
	s.in.knob[1] = 2048;
	s.in.knob[2] = 20;
	s.Run(10);
	return s;
}

static void TestStillKnobHoldsLoop()
{
	printf("a still knob holds the loop (texture)\n");
	for (int32_t chaos : {20, 1500, 3000, 4095})
	{
		Sim s = QuietSim(chaos);
		s.Record();
		s.Run(48000 * 2); // settle
		auto a = Capture(s, kTake);
		auto b = Capture(s, kTake);
		int diff = 0;
		for (int i = 0; i < kTake; i++)
			diff += a[i] != b[i];
		CHECK(diff == 0, "chaos %d: %d samples differ between cycles", chaos, diff);
	}
}

static void TestStillKnobHoldsRhythm()
{
	printf("a still knob holds the pattern (rhythm, internal clock)\n");
	for (int32_t chaos : {20, 2500, 4095})
	{
		Sim s = QuietSim(chaos);
		s.Record();
		s.in.sw = imp::kSwitchDown; // tap Down: Rhythm
		s.Run(10);
		s.in.sw = imp::kSwitchMiddle;
		s.Run(48000 * 2);
		// Layer 1 cycles through 4, 8 or 16 slots of the internal clock --
		// always exactly one take long.
		auto a = Capture(s, kTake);
		auto b = Capture(s, kTake);
		int diff = 0;
		for (int i = 0; i < kTake; i++)
			diff += a[i] != b[i];
		CHECK(diff == 0, "chaos %d: %d samples differ between cycles", chaos, diff);
	}
}

static void TestMovedKnobIsGone()
{
	printf("moving the knob away and back doesn't bring the loop back\n");
	Sim s = QuietSim(2500);
	s.Record();
	s.Run(48000 * 2);
	auto before = Capture(s, kTake);
	s.in.knob[0] = 3500;
	s.Run(2000);
	s.in.knob[0] = 2500;
	s.Run(48000 * 2);
	// Compare against every alignment of one cycle: the loop must differ.
	auto after = Capture(s, kTake);
	int same = 0;
	for (int i = 0; i < kTake; i++)
		same += before[i] == after[i];
	CHECK(same < kTake / 2, "loop came back (%d/%d samples identical)", same, kTake);
}

static void TestJitterDoesNotReseed()
{
	printf("knob noise and CV noise don't reseed\n");
	Sim s(7, 2000);
	s.Run(4800);
	uint32_t before = s.inst->Reseeds();
	for (int i = 0; i < 48000 * 2; i++)
	{
		s.in.knob[0] = 2000 + (int32_t)((i * 7919u) % 21) - 10; // +/-10 jitter
		s.in.cv[0] = (int32_t)((i * 104729u) % 17) - 8;	  // +/-8 CV noise
		s.Step();
	}
	CHECK(s.inst->Reseeds() == before, "%u reseeds from noise", s.inst->Reseeds() - before);
}

static void TestTurningReseeds()
{
	printf("turning the knob reseeds, rate-limited\n");
	Sim s(7, 100);
	s.Run(4800);
	uint32_t before = s.inst->Reseeds();
	for (int i = 0; i < 48000; i++) // 1s sweep, 100 -> 4000
	{
		s.in.knob[0] = 100 + (3900 * i) / 48000;
		s.Step();
	}
	// 3900 of travel / 64 dead-zone = ~60 reseeds; the 20ms holdoff allows
	// up to 50 in one second.
	uint32_t n = s.inst->Reseeds() - before;
	printf("  %u reseeds\n", n);
	CHECK(n >= 40 && n <= 61, "%u reseeds during a 1s sweep", n);
}

static void TestCvRouting()
{
	printf("CV1 reseeds in Texture only; switch flips don't reseed\n");
	Sim s(3, 2000);
	s.Run(4800);
	s.in.cv[0] = 800;
	CHECK(CountReseeds(s, 4800) == 1, "CV1 jump didn't reseed in Texture");

	// Tap Down -> Rhythm.
	s.in.sw = imp::kSwitchDown;
	s.Run(100);
	s.in.sw = imp::kSwitchMiddle;
	s.Run(100);
	CHECK(s.out.led[1] > 0, "tap Down didn't latch Rhythm");
	s.in.cv[0] = -800;
	CHECK(CountReseeds(s, 4800) == 0, "CV1 reseeded in Rhythm");

	// Back to Texture with CV1 already moved: the flip itself mustn't reseed.
	s.in.sw = imp::kSwitchDown;
	s.Run(100);
	s.in.sw = imp::kSwitchMiddle;
	CHECK(CountReseeds(s, 4800) == 0, "switching mode reseeded");

	// Up and back: no reseed either, even with CV1 set.
	s.in.sw = imp::kSwitchUp;
	s.Run(4800);
	s.in.sw = imp::kSwitchMiddle;
	CHECK(CountReseeds(s, 4800) == 0, "Up->Middle reseeded");
}

static void TestCatchUpOnCard()
{
	printf("switch Up doesn't move chaos until the knob catches up\n");
	Sim s(3, 1000);
	s.Run(4800);
	s.in.sw = imp::kSwitchUp;
	s.in.knob[0] = 4000; // Level 1 page: moving Main here must not reseed
	CHECK(CountReseeds(s, 4800) == 0, "Main on Up page reseeded");
	s.in.sw = imp::kSwitchMiddle; // knob now at 4000, chaos stored at 1000
	CHECK(CountReseeds(s, 4800) == 0, "returning to Middle jumped chaos");
	s.in.knob[0] = 1010; // pass back through 1000: hooks, then moves
	s.Run(100);
	s.in.knob[0] = 1300;
	CHECK(CountReseeds(s, 4800) == 1, "no reseed after catching up");
}

static void TestPulseOuts()
{
	printf("end-of-cycle pulses\n");
	// Texture, chaos 0: one slice each. Pulse Out 1 follows L1 (a whole
	// take), Pulse Out 2 follows L3 (2/3 of a take) -- 3:2.
	Sim s(5, 20);
	s.Run(48000);
	std::vector<uint64_t> p1, p2;
	bool l1 = s.out.pulse[0], l2 = s.out.pulse[1]; // a pulse may be mid-way
	for (int i = 0; i < 48000 * 3; i++)
	{
		s.Step();
		if (s.out.pulse[0] && !l1)
			p1.push_back(s.t);
		if (s.out.pulse[1] && !l2)
			p2.push_back(s.t);
		l1 = s.out.pulse[0];
		l2 = s.out.pulse[1];
	}
	CHECK(p1.size() >= 2 && p1[1] - p1[0] == (uint64_t)kTake, "pulse out 1 (L1) period %llu", p1.size() >= 2 ? (unsigned long long)(p1[1] - p1[0]) : 0ull);
	CHECK(p2.size() >= 2 && p2[1] - p2[0] == (uint64_t)(kTake * 2 / 3), "pulse out 2 (L3) period %llu", p2.size() >= 2 ? (unsigned long long)(p2[1] - p2[0]) : 0ull);

	// Rhythm, clocked at 8000 samples: at this chaos L1 cycles in 4 slots
	// and L3 in 3, so the jacks run 4 against 3.
	s.in.sw = imp::kSwitchDown;
	s.Run(10);
	s.in.sw = imp::kSwitchMiddle;
	p1.clear();
	p2.clear();
	for (int i = 0; i < 48000 * 4; i++)
	{
		if (i % 8000 == 0)
			s.pulse2 = true;
		s.Step();
		if (s.out.pulse[0] && !l1)
			p1.push_back(s.t);
		if (s.out.pulse[1] && !l2)
			p2.push_back(s.t);
		l1 = s.out.pulse[0];
		l2 = s.out.pulse[1];
	}
	size_t n1 = p1.size(), n2 = p2.size();
	CHECK(n1 >= 3 && p1[n1 - 1] - p1[n1 - 2] == 32000, "rhythm pulse out 1 (L1) period %llu", n1 >= 2 ? (unsigned long long)(p1[n1 - 1] - p1[n1 - 2]) : 0ull);
	CHECK(n2 >= 3 && p2[n2 - 1] - p2[n2 - 2] == 24000, "rhythm pulse out 2 (L3) period %llu", n2 >= 2 ? (unsigned long long)(p2[n2 - 1] - p2[n2 - 2]) : 0ull);
}

static void TestReverbStability()
{
	printf("shimmer reverb decays at maximum decay\n");
	imp::Shimmer rev;
	rev.SetDecay(28672); // the X knob's maximum: 0.875
	int32_t l, r;
	int64_t early = 0, late = 0;
	int32_t peak = 0;
	for (int i = 0; i < 48000 * 20; i++)
	{
		// Half a second of loud noise, then silence.
		int32_t x = i < 24000 ? (int32_t)((i * 2654435761u) >> 20) - 2048 : 0;
		rev.Process(x, x, l, r);
		int32_t a = l < 0 ? -l : l;
		if (a > peak)
			peak = a;
		if (i >= 48000 && i < 96000)
			early += (int64_t)l * l;
		if (i >= 48000 * 19)
			late += (int64_t)l * l;
	}
	printf("  peak %d, energy 1-2s %lld, 19-20s %lld\n", peak, (long long)early, (long long)late);
	CHECK(late * 1000 < early, "tail not decaying (runaway shimmer?)");
	CHECK(peak < 32767, "reverb saturated");
}

static void TestOutputRangeAndSilence()
{
	printf("outputs stay in range; levels at zero are silent\n");
	Sim s(11, 4095, 4095, 4095); // max chaos, max decay, full wet
	s.Record();
	int32_t lo = 0, hi = 0;
	for (int i = 0; i < 48000 * 5; i++)
	{
		s.in.knob[0] = 2000 + (i / 997 % 2) * 2000; // keep reseeding
		s.Step();
		for (int c = 0; c < 2; c++)
		{
			lo = s.out.audio[c] < lo ? s.out.audio[c] : lo;
			hi = s.out.audio[c] > hi ? s.out.audio[c] : hi;
		}
		CHECK(s.out.cv[0] >= 0 && s.out.cv[0] <= 2047 && s.out.cv[1] >= 0 && s.out.cv[1] <= 2047, "CV out of range");
	}
	CHECK(lo >= -2048 && hi <= 2047, "audio out of range %d..%d", lo, hi);

	Sim q(12, 20, 2048, 20);
	q.Record();
	q.in.sw = imp::kSwitchUp;
	// Sweep all three knobs down through their stored levels (hooking
	// them) to the bottom of their travel.
	for (int32_t v = 4095; v >= 14; v -= 8)
	{
		q.in.knob[0] = q.in.knob[1] = q.in.knob[2] = v;
		q.Step();
	}
	q.Run(48000 * 3);
	int32_t m = 0;
	for (int i = 0; i < 4800; i++)
	{
		q.Step();
		m = abs(q.out.audio[0]) > m ? abs(q.out.audio[0]) : m;
	}
	CHECK(m == 0, "levels at minimum still output %d", m);
}

static void TestSlowControlCore()
{
	printf("core 1 running late: loop still holds, outputs stay sane\n");
	Sim s = QuietSim(3000);
	s.controlEvery = 7;
	s.Record();
	s.Run(48000 * 2);
	auto a = Capture(s, kTake);
	auto b = Capture(s, kTake);
	int diff = 0;
	for (int i = 0; i < kTake; i++)
		diff += a[i] != b[i];
	CHECK(diff == 0, "%d samples differ between cycles", diff);

	int32_t peak = 0;
	uint32_t before = s.inst->Reseeds();
	for (int i = 0; i < 48000 * 2; i++)
	{
		s.in.knob[0] = 500 + (i / 3 % 3500);
		s.Step();
		int32_t m = abs(s.out.audio[0]) > abs(s.out.audio[1]) ? abs(s.out.audio[0]) : abs(s.out.audio[1]);
		peak = m > peak ? m : peak;
	}
	CHECK(s.inst->Reseeds() > before, "no re-cuts with a slow core 1");
	CHECK(peak <= 2047, "output out of range: %d", peak);
}

// Pulling a cable out of a Pulse In: for ~11ms ComputerCard's jack
// detection feeds the input a random bit pattern (changing every 16
// samples) before it reports the jack empty. Simulate exactly that.
static void Unplug(Sim &s, bool pulse1)
{
	uint32_t h = 12345;
	for (int i = 0; i < 528; i++)
	{
		if (i % 16 == 0)
		{
			h = h * 1664525u + 1013904223u;
			if (h >> 31)
			{
				if (pulse1)
					s.pulse1 = true;
				else
					s.pulse2 = true;
			}
		}
		s.Step();
	}
	if (pulse1)
		s.in.pulse1Connected = false;
	else
		s.in.pulse2Connected = false;
}

static void TestUnplugPulse1KeepsLoop()
{
	printf("unplugging Pulse In 1 leaves the loop playing\n");
	Sim s = QuietSim(1500);
	s.Record();
	s.Run(48000 * 2);
	auto a = Capture(s, kTake);
	Unplug(s, true);
	s.Run(kTake - 528 + kTake * 2);
	auto b = Capture(s, kTake);
	int diff = 0;
	for (int i = 0; i < kTake; i++)
		diff += a[i] != b[i];
	CHECK(diff == 0, "loop changed after unplugging Pulse In 1 (%d samples differ)", diff);
}

static void TestUnplugClockKeepsTempo()
{
	printf("unplugging Pulse In 2 keeps the clocked tempo\n");
	Sim s(5, 1500);
	s.Record();
	s.in.sw = imp::kSwitchDown; // Rhythm
	s.Run(10);
	s.in.sw = imp::kSwitchMiddle;
	auto period = [&s](int n, bool clock) {
		std::vector<uint64_t> p1;
		bool last = s.out.pulse[0];
		for (int i = 0; i < n; i++)
		{
			if (clock && i % 6000 == 0)
				s.pulse2 = true;
			s.Step();
			if (s.out.pulse[0] && !last)
				p1.push_back(s.t);
			last = s.out.pulse[0];
		}
		size_t k = p1.size();
		return k >= 2 ? (int64_t)(p1[k - 1] - p1[k - 2]) : -1;
	};
	// Pulse Out 1 follows L1, which has 4 slots at this chaos: 4 clock periods.
	int64_t before = period(48000 * 3, true);
	CHECK(before == 24000, "clocked cycle %lld, expected 24000", (long long)before);
	Unplug(s, false);
	int64_t after = period(48000 * 3, false);
	CHECK(after == 24000, "cycle after unplugging %lld, expected 24000", (long long)after);
}

static void TestCcwClearsThenRecords()
{
	printf("Main fully anticlockwise clears; turning it up records\n");
	Sim s = QuietSim(2000);
	s.Record();
	s.Run(48000);
	s.in.knob[0] = 14; // fully anticlockwise
	s.Run(4800);	   // fade (~5ms) and clear (~60ms)
	int32_t m = 0;
	for (int i = 0; i < 48000; i++)
	{
		s.Step();
		m = abs(s.out.audio[0]) > m ? abs(s.out.audio[0]) : m;
	}
	CHECK(m == 0, "still sounding after clear: %d", m);

	s.in.knob[0] = 1000; // back up: a new take
	s.Run(100);
	CHECK(s.out.led[0] > 0, "recording LED not on after turning Main up");
	s.Run(48000 + 4800);
	int64_t e = 0;
	for (int i = 0; i < 48000; i++)
	{
		s.Step();
		e += (int64_t)s.out.audio[0] * s.out.audio[0];
	}
	CHECK(e > 0, "no sound from the new take");
}

// Energy at one frequency (Goertzel).
static double Energy(const std::vector<int32_t> &x, double f)
{
	double w = 2 * M_PI * f / 48000.0, c = 2 * cos(w), s1 = 0, s2 = 0;
	for (int32_t v : x)
	{
		double s0 = v + c * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	return sqrt(fmax(s1 * s1 + s2 * s2 - c * s1 * s2, 0)) / x.size();
}

static void TestClockDoesNotChangePitch()
{
	printf("a clock changes the timing, not the pitch\n");
	Sim::tone = 1000.0f; // record a steady 1kHz tone
	Sim s(4, 20, 2048, 20);
	s.Record();
	// 13,500 samples per clock makes layer 1's loop 2 clocks long, which
	// used to be played 1.33x fast -- and so a third of an octave sharp.
	const int kClock = 13500;
	for (int i = 0; i < 48000 * 4; i++)
	{
		if (i % kClock == 0)
			s.pulse2 = true;
		s.Step();
	}
	std::vector<int32_t> out;
	std::vector<uint64_t> pulses;
	bool last = s.out.pulse[0];
	for (int i = 0; i < 48000 * 4; i++)
	{
		if (i % kClock == 0)
			s.pulse2 = true;
		s.Step();
		out.push_back(s.out.audio[0]);
		if (s.out.pulse[0] && !last)
			pulses.push_back(s.t);
		last = s.out.pulse[0];
	}
	Sim::tone = 0.0f;
	double at1000 = Energy(out, 1000), at1333 = Energy(out, 1333);
	CHECK(at1000 > 3 * at1333, "pitch moved with the clock: 1000Hz %.1f, 1333Hz %.1f", at1000, at1333);
	// ...and the timing still follows the clock.
	size_t n = pulses.size();
	CHECK(n >= 2 && pulses[n - 1] - pulses[n - 2] == (uint64_t)(2 * kClock),
		  "layer 1 loop %llu, expected 2 clock periods", n >= 2 ? (unsigned long long)(pulses[n - 1] - pulses[n - 2]) : 0ull);
}

int main()
{
	TestPacking();
	TestHook();
	TestStillKnobHoldsLoop();
	TestStillKnobHoldsRhythm();
	TestMovedKnobIsGone();
	TestJitterDoesNotReseed();
	TestTurningReseeds();
	TestCvRouting();
	TestCatchUpOnCard();
	TestPulseOuts();
	TestReverbStability();
	TestOutputRangeAndSilence();
	TestSlowControlCore();
	TestUnplugPulse1KeepsLoop();
	TestUnplugClockKeepsTempo();
	TestCcwClearsThenRecords();
	TestClockDoesNotChangePitch();
	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? 1 : 0;
}
