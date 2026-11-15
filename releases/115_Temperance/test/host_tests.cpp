// Host tests: `make -C test`. Each test drives a pretend panel (Sim.h), or
// one part of the card on its own, and checks what it does sample by
// sample, exactly as the firmware runs.
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "Sim.h"

using namespace tem;

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

// A smooth test tone that doesn't divide evenly into any loop.
static int32_t Tone(uint64_t i) { return (int32_t)lrint(1500.0 * sin(2.0 * M_PI * (double)i / 37.3)); }

// A ramp that tells every sample apart, within 12 bits.
static int32_t Ramp(int64_t i) { return (int32_t)((i * 7) % 4000) - 2000; }

// A looper with `n` ticks of `f` recorded, stopped at speed `speed`.
static std::unique_ptr<Looper> Recorded(int n, const std::function<int32_t(int64_t)> &f, int32_t speed,
									   int32_t step = 0)
{
	auto l = std::make_unique<Looper>();
	l->Tap(0, step);
	for (int i = 0; i < n; i++)
		l->Tick(f(i), speed, step);
	l->Tap(kDoubleTap + 1, step);
	return l;
}

// The largest step between neighbouring samples.
static int32_t MaxStep(const std::vector<int32_t> &v)
{
	int32_t m = 0;
	for (size_t i = 1; i < v.size(); i++)
		m = std::max(m, std::abs(v[i] - v[i - 1]));
	return m;
}

// ---- Milestones 2 and 3: the looper and its storage -------------------------

static void StorageTests()
{
	Test("packed: every 12-bit value round-trips, odd and even", [] {
		auto p = std::make_unique<Packed12<8>>();
		bool ok = true;
		for (int32_t v = -2048; v <= 2047; v++)
		{
			for (int i = 0; i < 8; i++)
				p->Write(i, (i * 977) % 4096 - 2048);
			p->Write(3, v);
			p->Write(4, -v - 1);
			ok &= p->Read(3) == v && p->Read(4) == -v - 1;
			// The neighbours sharing their bytes are untouched.
			ok &= p->Read(2) == (2 * 977) % 4096 - 2048 && p->Read(5) == (5 * 977) % 4096 - 2048;
		}
		CHECK(ok);
	});
}

static void LooperTests()
{
	Test("looper: plays back what it recorded, forward", [] {
		const int n = 1000;
		auto l = Recorded(n, Ramp, 65536);
		CHECK(l->State() == Looper::kPlaying && l->Len() == n);
		bool ok = true;
		for (int j = 0; j < 3 * n; j++)
		{
			int32_t out = l->Tick(Ramp(n + j), 65536, 0);
			int i = j % n;
			// Past the first pass's fade-in, and past the seam.
			if (j >= Looper::kSeam && i >= Looper::kSeam)
				ok &= out == Ramp(i);
		}
		CHECK(ok);
	});

	Test("looper: plays back reversed", [] {
		const int n = 1000;
		auto l = Recorded(n, Ramp, -65536);
		bool ok = true;
		for (int j = 0; j < n; j++)
		{
			int32_t out = l->Tick(Ramp(n + j), -65536, 0);
			int i = n - 1 - j;
			if (j >= Looper::kSeam && i >= Looper::kSeam)
				ok &= out == Ramp(i);
		}
		CHECK(ok);
	});

	Test("looper: x2 wraps twice as often, x1/2 half as often", [] {
		const int n = 2000;
		int32_t speeds[] = {131072, 65536, 32768, -32768, -65536, -131072};
		int expect[] = {20, 10, 5, 5, 10, 20};
		for (int s = 0; s < 6; s++)
		{
			auto l = Recorded(n, Tone, speeds[s]);
			int wraps = 0;
			for (int j = 0; j < 10 * n; j++)
			{
				l->Tick(0, speeds[s], 0);
				wraps += l->Wrapped();
			}
			// One more for the start of playback.
			CHECK(wraps == expect[s] + 1 || wraps == expect[s]);
		}
	});

	Test("looper: the seam is smooth, forward and reverse", [] {
		const int n = 1000;
		for (int32_t speed : {65536, -65536})
		{
			auto l = Recorded(n, Tone, speed);
			std::vector<int32_t> out;
			for (int j = 0; j < 5 * n; j++)
			{
				int32_t v = l->Tick(Tone(n + j), speed, 0);
				if (j >= n)
					out.push_back(v);
			}
			// The tone itself moves by at most 253 a sample; a cut at the
			// seam would jump by up to 3000.
			CHECK(MaxStep(out) < 300);
		}
	});

	Test("looper: an input cut just after the stop doesn't click every pass", [] {
		// At 2kHz, the input stops dead 20 ticks (10ms) after the stop tap:
		// a note cut off, a cable pulled. The seam's tail is 16 ticks there,
		// so the cut isn't in it. (With a 256-tick tail it was, and clicked
		// on every pass.)
		const int n = 1000;
		auto l = Recorded(n, Tone, 65536, 9);
		std::vector<int32_t> out;
		for (int j = 0; j < 5 * n; j++)
		{
			int32_t v = l->Tick(j < 20 ? Tone(n + j) : 0, 65536, 9);
			if (j >= n)
				out.push_back(v);
		}
		// The tone moves up to 253 a tick, and a 16-tick crossfade between
		// two unrelated stretches of it adds up to 3000 / 16; the cut, in a
		// 256-tick tail, jumped by up to about 1400.
		CHECK(MaxStep(out) < 253 + 3000 / 16 + 20);
	});

	Test("looper: a speed change glides", [] {
		const int n = 2000;
		auto l = Recorded(n, Tone, 65536);
		std::vector<int32_t> out;
		for (int j = 0; j < 3 * n; j++)
			out.push_back(l->Tick(Tone(n + j), j < 1500 ? 65536 : -131072, 0));
		std::vector<int32_t> after(out.begin() + 1000, out.end());
		CHECK(MaxStep(after) < 2 * 253 + 40); // the tone at x2, plus the fade
	});

	Test("looper: two quick taps clear it", [] {
		auto l = std::make_unique<Looper>();
		l->Tap(0, 0);
		for (int i = 0; i < 4800; i++)
			l->Tick(1000, 65536, 0);
		l->Tap(4800, 0);
		CHECK(l->State() == Looper::kEmpty);
	});

	Test("looper: a full buffer stops recording and plays", [] {
		auto l = std::make_unique<Looper>();
		l->Tap(0, 0);
		for (int i = 0; i < Looper::kSize + 1000; i++)
			l->Tick(Ramp(i), 65536, 0);
		CHECK(l->State() == Looper::kPlaying);
		CHECK(l->Len() == Looper::kMaxLen);
	});

	Test("looper: a full-length loop plays back and wraps, both ways, at x2", [] {
		// Longer than 32,768 samples: the read head needs all 32 bits. (One
		// short of full, which would stop it before the stop tap.)
		const int n = Looper::kMaxLen - 1;
		CHECK(n > 32768);
		for (int32_t speed : {131072, -131072})
		{
			auto l = Recorded(n, Ramp, speed);
			bool ok = true;
			int wraps = 0;
			for (int j = 0; j < 3 * n; j++)
			{
				int32_t out = l->Tick(Ramp(n + j), speed, 0);
				wraps += l->Wrapped();
				// Where the head is: two samples a tick, from the start or the end.
				int64_t i = speed > 0 ? (2 * (int64_t)j) % n : (((int64_t)n - 1 - 2 * (int64_t)j) % n + n) % n;
				if (j >= Looper::kSeam && i >= Looper::kSeam)
					ok &= out == Ramp(i);
			}
			CHECK(ok);
			CHECK(wraps == 6 || wraps == 7); // three passes at x2, plus playback's start
		}
	});

	Test("looper: a tap while playing fades the old loop out over 20ms", [] {
		auto l = Recorded(4800, [](int64_t) { return 1000; }, 65536);
		for (int j = 0; j < 2000; j++)
			l->Tick(1000, 65536, 0);
		l->Tap(10 * kDoubleTap, 0); // 48kHz: 960 ticks of fade
		std::vector<int32_t> out;
		for (int j = 0; j < 1200; j++)
			out.push_back(l->Tick(0, 65536, 0));
		CHECK(l->State() == Looper::kRecording);
		CHECK(out[0] > 990 && out[0] <= 1000);
		CHECK(out[480] > 450 && out[480] < 550);
		CHECK(out[970] == 0 && out[1199] == 0);
		bool falling = true;
		for (size_t i = 1; i < out.size(); i++)
			falling &= out[i] <= out[i - 1] && out[i - 1] - out[i] <= 2;
		CHECK(falling);
	});
}

// ---- Milestone 4: the clock -------------------------------------------------

static void ClockTests()
{
	Test("clock: each step ticks at its rate", [] {
		for (int step = 0; step < kClockSteps; step++)
		{
			SampleClock c;
			int ticks = 0;
			for (int i = 0; i < 3 * 48000; i++)
				ticks += c.Process(step);
			CHECK(std::abs(ticks - 3 * kClockHz[step]) <= 1);
		}
	});

	Test("clock: Main sets it, CV in 1 moves it a step per volt", [] {
		CHECK(Sim(4095).inst->Params().clockStep == 0);
		CHECK(Sim(0).inst->Params().clockStep == 9);
		CHECK(Sim(3072).inst->Params().clockStep == 2);
		Sim s(4095);
		s.in.cv[0] = -682; // -2V: two steps slower
		s.RunMs(50);
		CHECK(s.inst->Params().clockStep == 2);
		s.in.cv[0] = 1023; // +3V from 48kHz: already the fastest
		s.RunMs(50);
		CHECK(s.inst->Params().clockStep == 0);
	});

	Test("clock: slowing it plays the loop lower and longer", [] {
		Sim s(4095, 3072); // Loop A forward at x1
		s.source[0] = Tone;
		s.in.pulsePatched[0] = true;
		s.Pulse(0);
		s.RunMs(500 - 10);
		s.Pulse(0);
		s.RunMs(100);
		int32_t len = s.inst->Loop(0).Len();
		CHECK(std::abs(len - 24000) <= 2);
		// Main to 24kHz: an octave down, each pass twice as long.
		s.in.knob[0] = 3072;
		s.RunMs(1100);
		s.loopStarts[0].clear();
		s.RunMs(3000);
		CHECK(s.loopStarts[0].size() >= 2);
		for (size_t i = 1; i < s.loopStarts[0].size(); i++)
			CHECK(std::abs((int64_t)(s.loopStarts[0][i] - s.loopStarts[0][i - 1]) - 2 * len) <= 2);
	});
}

// ---- Milestone 5: two loops, the Pulse ins, normalling ----------------------

static void RecordTests()
{
	Test("record: a switch tap records both loops, the same length", [] {
		Sim s;
		s.source[0] = Tone;
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
		CHECK(s.inst->Loop(1).State() == Looper::kRecording);
		s.RunMs(500 - 35);
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == s.inst->Loop(1).Len());
		CHECK(std::abs(s.inst->Loop(0).Len() - 24000) <= 48);
	});

	Test("record: a quick double tap clears both", [] {
		Sim s;
		s.source[0] = Tone;
		s.Tap();
		s.RunMs(100);
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		CHECK(s.inst->Loop(1).State() == Looper::kEmpty);
	});

	Test("record: Pulse in 2 records loop B alone", [] {
		Sim s;
		s.source[0] = Tone;
		s.Pulse(1);
		s.RunMs(400);
		s.Pulse(1);
		s.RunMs(20); // the stop acts 20ms after its edge
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		CHECK(s.inst->Loop(1).State() == Looper::kPlaying);
	});

	Test("record: a pulse's loop starts on the edge itself", [] {
		Sim s;
		uint64_t edge = s.t;
		// An impulse 100ms after the edge.
		s.source[0] = [edge](uint64_t t) { return t == edge + 4800 ? 2000 : 0; };
		s.Pulse(0);
		s.RunMs(500 - 10);
		s.Pulse(0);
		s.RunMs(50);
		const Looper &l = s.inst->Loop(0);
		int32_t at = -1;
		for (int32_t i = 0; i < l.Len(); i++)
			if (l.Peek(i) == 2000)
				at = i;
		CHECK(at == 4800);
		CHECK(l.Len() == 24000);
	});

	Test("record: the probe's burst on an unplug records nothing", [] {
		Sim s;
		s.in.pulsePatched[0] = true;
		s.RunMs(50);
		// Pulled out: the probe pattern for 11ms, then declared empty.
		for (int i = 0; i < 11 * 48; i++)
		{
			s.pulseIn[0] = (i / 16) & 1;
			s.Step();
		}
		s.pulseIn[0] = false;
		s.in.pulsePatched[0] = false;
		s.RunMs(100);
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
	});

	Test("record: an empty Audio in 2 is normalled to Audio in 1", [] {
		Sim s;
		s.source[0] = Tone;
		s.Tap();
		s.RunMs(300);
		s.Tap();
		bool same = true, sound = false;
		for (int32_t i = 0; i < s.inst->Loop(0).Len(); i++)
		{
			same &= s.inst->Loop(0).Peek(i) == s.inst->Loop(1).Peek(i);
			sound |= s.inst->Loop(1).Peek(i) != 0;
		}
		CHECK(same && sound);

		Sim p;
		p.source[0] = Tone;
		p.in.audio2Patched = true; // patched, silent
		p.Tap();
		p.RunMs(300);
		p.Tap();
		bool silent = true;
		for (int32_t i = 0; i < p.inst->Loop(1).Len(); i++)
			silent &= p.inst->Loop(1).Peek(i) == 0;
		CHECK(silent);
	});

	Test("record: X and Y set each loop's speed zone", [] {
		Sim s(4095, 300, 3800);
		CHECK(s.inst->Params().zone[0] == 0 && s.inst->Params().zone[1] == 5);
		// Hysteresis: 100 past an edge isn't enough, 130 is.
		s.in.knob[1] = 683 + 100;
		s.RunMs(20);
		CHECK(s.inst->Params().zone[0] == 0);
		s.in.knob[1] = 683 + 130;
		s.RunMs(20);
		CHECK(s.inst->Params().zone[0] == 1);
	});
}

// ---- Milestones 6 and 7: crush, the alt layer, the dry level ----------------

// Record `ms` of whatever is playing into loop A alone (Pulse in 1), then
// let it play.
static void RecordA(Sim &s, int ms)
{
	s.Pulse(0);
	s.RunMs(ms - 10);
	s.Pulse(0);
	s.RunMs(50);
}

static void AltTests()
{
	Test("alt: knobs wait for their stored values (pickup)", [] {
		Sim s;
		CHECK(s.inst->Params().dryQ12 == 4096); // stored at unity
		s.in.sw = kSwitchUp;
		s.in.knob[1] = 0;
		s.RunMs(5);
		for (int32_t x = 0; x < 1500; x += 20)
		{
			s.in.knob[1] = x;
			s.RunMs(1);
		}
		CHECK(s.inst->Params().dryQ12 == 4096); // not reached, so not following
		s.Sweep(1, 1000);
		CHECK(s.inst->Params().dryQ12 == 1000);
	});

	Test("dry: bypasses the clock, the crush and the reverb", [] {
		Sim s(0); // 2kHz
		s.SetAlt(4095, 4095, 4095); // full wash, dry at unity, 4 bits
		s.source[0] = Tone;
		bool same = true;
		for (int i = 0; i < 4800; i++)
		{
			s.Step();
			same &= s.out.audio[0] == Tone(s.t - 1) && s.out.audio[1] == Tone(s.t - 1);
		}
		CHECK(same);
	});

	Test("dry: fully left, only the loops play", [] {
		Sim s;
		s.SetAlt(0, 0, 0);
		s.source[0] = Tone;
		bool silent = true;
		for (int i = 0; i < 4800; i++)
		{
			s.Step();
			silent &= s.out.audio[0] == 0;
		}
		CHECK(silent);
		RecordA(s, 500);
		s.source[0] = nullptr;
		s.in.audio[0] = 0;
		int32_t peak = 0;
		for (int i = 0; i < 48000; i++)
		{
			s.Step();
			peak = std::max(peak, std::abs(s.out.audio[0]));
		}
		CHECK(peak > 1000); // the loop, at 3/4
	});

	Test("alt: crush quantises the loops", [] {
		Sim s;
		s.SetAlt(0, 0, 4095);
		CHECK(s.inst->Params().crushBits == 4);
		s.source[0] = [](uint64_t) { return 1000; };
		RecordA(s, 500);
		s.RunMs(100);
		// The loop at 3/4 is 750; to 4 bits, the nearest step of 256.
		CHECK(s.out.audio[0] == 768 && s.out.audio[1] == 768);
	});
}

// ---- Milestone 8: the plate ---------------------------------------------------

static void PlateTests()
{
	Test("plate: full wash, loud loops, nothing runs away, the tail dies", [] {
		Sim s;
		s.SetAlt(4095, 0, 0);
		s.source[0] = [](uint64_t i) { return (i / 50) & 1 ? 2047 : -2048; };
		s.Tap();
		s.RunMs(500);
		s.Tap();
		s.keep = true;
		s.RunMs(5000);
		int32_t peak = 0;
		for (int16_t v : s.outR)
			peak = std::max(peak, std::abs((int32_t)v / 16));
		CHECK(peak > 1000 && peak <= 2047);
		// Silence in, and a double tap clears both loops.
		s.source[0] = nullptr;
		s.in.audio[0] = 0;
		s.Tap();
		s.RunMs(100);
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty && s.inst->Loop(1).State() == Looper::kEmpty);
		s.keep = false;
		s.RunMs(60000);
		s.keep = true;
		s.outL.clear();
		s.outR.clear();
		s.RunMs(1000);
		bool silent = true;
		for (size_t i = 0; i < s.outL.size(); i++)
			silent &= s.outL[i] == 0 && s.outR[i] == 0;
		CHECK(silent);
	});

	Test("plate: stereo -- the two sides differ", [] {
		Sim s;
		s.SetAlt(3000, 0, 0);
		uint64_t at = s.t;
		s.source[0] = [at](uint64_t t) { return t >= at && t < at + 480 ? 1500 : 0; };
		RecordA(s, 500);
		s.keep = true;
		s.RunMs(2000);
		int64_t diff = 0;
		for (size_t i = 0; i < s.outL.size(); i++)
			diff += std::abs(s.outL[i] - s.outR[i]);
		CHECK(diff > 1000000);
	});

	Test("wash: one knob, send, decay and damping together", [] {
		Sim s;
		s.SetAlt(0, 2048, 0);
		CHECK(s.inst->Params().sendQ15 == 0 && s.inst->Params().decayQ15 == 16384);
		s.SetAlt(4095, 2048, 0);
		CHECK(s.inst->Params().sendQ15 == 32760);
		CHECK(std::abs(s.inst->Params().decayQ15 - 30474) <= 2); // 0.93
		CHECK(std::abs(s.inst->Params().dampQ15 - 13107) <= 2);	 // damping 0.6
		s.SetAlt(0, 2048, 0);
		s.in.cv[1] = 1707; // +5V on CV in 2: full wash from the CV alone
		s.RunMs(20);
		CHECK(s.inst->GetUi().Wash() == 4095);
	});
}

// ---- Milestone 9: CV and pulses (random voltages and triggers) ---------------------------------------------

static void OutputTests()
{
	Test("outs: CV out 1 steps by whole volts at each start of Loop A", [] {
		Sim s(4095, 3072); // Loop A forward at x1
		s.source[0] = Tone;
		RecordA(s, 500);
		s.loopStarts[0].clear();
		std::vector<std::pair<uint64_t, int32_t>> steps;
		int32_t last = s.out.cvMv[0];
		for (int i = 0; i < 48000 * 3; i++)
		{
			s.Step();
			if (s.out.cvMv[0] != last)
			{
				last = s.out.cvMv[0];
				steps.push_back({s.t - 1, last});
			}
		}
		CHECK(s.loopStarts[0].size() >= 5);
		CHECK(steps.size() == s.loopStarts[0].size());
		bool whole = true, moved = true, onTime = true;
		for (size_t i = 0; i < steps.size(); i++)
		{
			int32_t v = steps[i].second;
			whole &= v % 1000 == 0 && v >= -2000 && v <= 2000;
			if (i > 0)
				moved &= v != steps[i - 1].second;
			if (i < s.loopStarts[0].size())
				onTime &= steps[i].first >= s.loopStarts[0][i] && steps[i].first - s.loopStarts[0][i] < 3 * 48;
		}
		CHECK(whole && moved && onTime);
	});

	Test("outs: CV out 2 glides between random levels, one per pass of Loop B", [] {
		Sim s(4095, 2389, 3072); // Loop B forward at x1
		s.source[0] = Tone;
		s.Pulse(1);
		s.RunMs(500 - 10);
		s.Pulse(1);
		s.RunMs(50);
		int32_t lo = 5000, hi = 0, maxStep = 0, last = s.out.cvMv[1];
		for (int i = 0; i < 48000 * 5; i++)
		{
			s.Step();
			int32_t v = s.out.cvMv[1];
			lo = std::min(lo, v);
			hi = std::max(hi, v);
			maxStep = std::max(maxStep, std::abs(v - last));
			last = v;
		}
		CHECK(lo >= 0 && hi <= 5000);
		CHECK(hi - lo > 1000); // it wanders
		// At its steepest a smoothstep is 1.5 times the average slope: at
		// most 5V over a 500ms pass, so 15mV a millisecond.
		CHECK(maxStep <= 16);
	});

	Test("outs: the CVs hold still with no loop playing", [] {
		Sim s;
		s.RunMs(2000);
		CHECK(s.out.cvMv[0] == 0 && s.out.cvMv[1] == 0);
	});

	Test("outs: Pulse out 1 fires at random, 200ms to 2s apart", [] {
		Sim s;
		s.pulses[0].clear();
		s.RunMs(40000);
		CHECK(s.pulses[0].size() >= 20 && s.pulses[0].size() <= 200);
		bool inRange = true, varied = false;
		for (size_t i = 1; i < s.pulses[0].size(); i++)
		{
			uint64_t gap = s.pulses[0][i] - s.pulses[0][i - 1];
			inRange &= gap >= 199 * 48 && gap <= 2001 * 48;
			if (i > 1)
				varied |= gap != s.pulses[0][i - 1] - s.pulses[0][i - 2];
		}
		CHECK(inRange && varied);
	});

	Test("outs: Pulse out 2 fires at each start of Loop B, 10ms", [] {
		Sim s(4095, 2389, 3072); // Loop B forward at x1
		s.source[0] = Tone;
		s.Pulse(1);
		s.RunMs(500 - 10);
		s.Pulse(1);
		s.pulses[1].clear();
		s.RunMs(2100);
		CHECK(s.pulses[1].size() == 5);
		for (size_t i = 1; i < s.pulses[1].size(); i++)
			CHECK(s.pulses[1][i] - s.pulses[1][i - 1] == 24000);
	});
}

// ---- Milestone 10: the LEDs ---------------------------------------------------

static void LedTests()
{
	Test("leds: the boot chase runs down the grid", [] {
		Sim s(4095, 2389, 2389, kSwitchMiddle, false);
		s.RunMs(100);
		for (int i = 0; i < 6; i++)
		{
			s.RunMs(60);
			for (int j = 0; j < 6; j++)
				CHECK(s.led[j] == (j == i ? 4095 : 0));
			s.RunMs(60);
		}
	});

	Test("leds: recording blinks, playing holds, the playhead falls", [] {
		Sim s;
		s.source[0] = Tone;
		s.Tap();
		int changes = 0;
		int32_t last = s.led[0];
		for (int i = 0; i < 500; i++)
		{
			s.RunMs(1);
			changes += s.led[0] != last;
			last = s.led[0];
		}
		CHECK(changes >= 5 && changes <= 7); // 6Hz for half a second
		s.Tap();
		s.RunMs(100);
		CHECK(s.led[0] == 3000 && s.led[1] == 3000);
		CHECK(s.led[2] == 1024); // forward at half speed: 25%
		// The bottom LED: a falling saw, a whole loop long (1s at half speed).
		std::vector<int32_t> saw;
		for (int i = 0; i < 1200; i++)
		{
			s.RunMs(1);
			saw.push_back(s.led[4]);
		}
		int rises = 0;
		for (size_t i = 1; i < saw.size(); i++)
			rises += saw[i] > saw[i - 1] + 1000;
		CHECK(rises == 1);
	});

	Test("leds: turning Main shows the clock, then goes home", [] {
		Sim s;
		s.in.knob[0] = 0; // 2kHz: five and a half LEDs
		s.RunMs(50);
		CHECK(s.led[4] == 4095 && s.led[5] == 4095 && s.led[2] == 4095 && s.led[3] == 4095 && s.led[0] == 4095);
		CHECK(s.led[1] == 2048);
		s.RunMs(1600);
		CHECK(s.led[0] == 0); // home: loop A empty
	});

	Test("leds: switch up shows the alt bars, breathing until picked up", [] {
		Sim s;
		s.in.sw = kSwitchUp;
		s.in.knob[0] = 0;
		s.RunMs(5);
		int32_t lo = 4095, hi = 0;
		for (int i = 0; i < 1000; i++)
		{
			s.RunMs(1);
			lo = std::min(lo, s.led[0]);
			hi = std::max(hi, s.led[0]);
		}
		CHECK(hi - lo > 1000); // breathing
		s.Sweep(0, 3072);	   // picks up: 75%
		CHECK(s.led[0] == 4095 && s.led[1] == 2049);
	});
}

// ---- Gate mode: the Pulse ins record while high ------------------------------

// Run `ms` with the normalisation probe's pattern on Pulse in 1's level
// (it changes every 16 samples), as an empty jack reads on the hardware.
static void Probe(Sim &s, int ms)
{
	for (int i = 0; i < ms * 48; i++)
	{
		s.pulseIn[0] = (s.t / 16) & 1;
		s.Step();
	}
}

static void GateTests()
{
	Test("gate: two quick flicks up switch the mode, and back", [] {
		Sim s;
		CHECK(!s.inst->Params().gate); // trigger at power-on
		s.FlickUp();
		CHECK(s.inst->Params().gate);
		s.FlickUp();
		CHECK(!s.inst->Params().gate);
		s.FlickUp(400); // the second flick 500ms after the first: too slow
		CHECK(!s.inst->Params().gate);
	});

	Test("gate: the mode shows for 1.5s, then a glow on empty loops", [] {
		Sim s;
		s.FlickUp();
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] == 4095); // gate: both columns
		s.RunMs(1500);
		CHECK(s.led[0] == 1000 && s.led[1] == 1000); // empty, waiting for a gate
		s.FlickUp();
		CHECK(s.led[0] == 4095 && s.led[1] == 4095); // trigger: the top row only
		CHECK(s.led[2] == 0 && s.led[3] == 0 && s.led[4] == 0 && s.led[5] == 0);
		s.RunMs(1500);
		CHECK(s.led[0] == 0 && s.led[1] == 0); // empty, trigger mode: off
	});

	Test("gate: a loop is as long as its gate, from its rise", [] {
		Sim s(4095, 3072);
		s.FlickUp();
		uint64_t edge = s.t;
		s.source[0] = [edge](uint64_t t) { return t == edge + 4800 ? 2000 : 0; };
		s.Gate(0, 500);
		const Looper &l = s.inst->Loop(0);
		CHECK(l.State() == Looper::kPlaying);
		CHECK(l.Len() == 24000);
		CHECK(l.Peek(4800) == 2000);
		CHECK(s.inst->Loop(1).State() == Looper::kEmpty);
	});

	Test("gate: a short gate makes a short loop, not a clear", [] {
		Sim s;
		s.FlickUp();
		s.Gate(0, 100);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 4800);
		// Shorter than the 20ms wait: both edges still come through.
		s.Gate(1, 15);
		CHECK(s.inst->Loop(1).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(1).Len() == 720);
	});

	Test("gate: a gate too short to loop leaves it empty", [] {
		Sim s;
		s.FlickUp();
		s.Gate(0, 5); // 240 ticks at 48kHz; a loop needs 512
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
	});

	Test("gate: a gate over a playing loop records a new one", [] {
		Sim s;
		s.source[0] = Tone;
		s.FlickUp();
		s.Gate(0, 500);
		s.Gate(0, 200);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 9600);
	});

	Test("gate: the switch still taps both loops", [] {
		Sim s;
		s.FlickUp();
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
		s.RunMs(465);
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(1).State() == Looper::kPlaying);
	});

	Test("gate: pulling the cable out of a high gate ends the recording there", [] {
		Sim s;
		s.FlickUp();
		s.in.pulsePatched[0] = true;
		s.pulseIn[0] = true;
		s.RunMs(300);
		Probe(s, 11); // pulled out: the probe's pattern, still "patched"
		s.in.pulsePatched[0] = false;
		Probe(s, 100); // declared empty; the probe carries on
		const Looper &l = s.inst->Loop(0);
		CHECK(l.State() == Looper::kPlaying);
		CHECK(std::abs(l.Len() - 14400) <= 32);
	});

	Test("gate: the probe on an empty jack never records, or stops the switch", [] {
		Sim s;
		s.FlickUp();
		Probe(s, 200);
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.in.sw = kSwitchDown;
		Probe(s, 30);
		s.in.sw = kSwitchMiddle;
		Probe(s, 500);
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
	});

	Test("gate: a gate already high when the mode changes waits for its next rise", [] {
		Sim s;
		s.in.pulsePatched[0] = true;
		s.pulseIn[0] = true;
		s.RunMs(50); // in trigger mode: that rise started a recording
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
		s.Tap(); // within 300ms of the pulse's start: a clear
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.FlickUp();
		s.RunMs(100);
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.pulseIn[0] = false;
		s.RunMs(50);
		s.Gate(0, 300);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 14400);
	});

	Test("gate: back in trigger mode, a pulse toggles again", [] {
		Sim s;
		s.FlickUp();
		s.FlickUp();
		s.Pulse(0);
		s.RunMs(20);
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
	});
}

int main()
{
	StorageTests();
	LooperTests();
	ClockTests();
	RecordTests();
	GateTests();
	AltTests();
	PlateTests();
	OutputTests();
	LedTests();
	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? 1 : 0;
}
