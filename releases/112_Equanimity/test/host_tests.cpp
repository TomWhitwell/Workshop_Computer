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

// ---- Milestone 1: controls ----------------------------------------------------

static void flip(Sim &s, int32_t sw)
{
	s.in.sw = sw;
	s.RunMs(5);
}

static void Controls()
{
	Test("middle: wet/dry and feedback follow the knobs", [] {
		Sim s(1, 1000, 1000, 1000);
		s.in.knob[0] = 3000;
		s.in.knob[1] = 4095;
		s.in.knob[2] = 0;
		s.RunMs(5);
		CHECK(s.control().Mix() == 3000);
		CHECK(s.inst->Params().shortFeedback == (4095 * eq::kFeedbackCeilingQ12) >> 12);
		CHECK(s.inst->Params().longFeedback == 0);
	});

	Test("up: X / Y set melody / drone volume; middle's settings hold", [] {
		Sim s(1, 1000, 1000, 1000);
		flip(s, eq::kSwitchUp);
		int32_t fb = s.inst->Params().shortFeedback;
		// Volumes wait for pickup: start values are 3700 / 3000.
		s.in.knob[1] = 3800; // passes 3700: picked up
		s.in.knob[2] = 3100; // passes 3000
		s.RunMs(5);
		s.in.knob[1] = 2048;
		s.in.knob[2] = 4095;
		s.RunMs(5);
		CHECK(s.inst->Params().melodyGain == (2048 * 2048) >> 12);
		CHECK(s.inst->Params().droneGain == (4095 * 4095) >> 12);
		CHECK(s.inst->Params().shortFeedback == fb); // feedback held
	});

	Test("pickup: back in the middle, each knob waits for its setting", [] {
		Sim s(1, 1000, 1000, 1000);
		flip(s, eq::kSwitchUp);
		s.in.knob[0] = 3500;
		s.in.knob[1] = 3500;
		s.RunMs(5);
		flip(s, eq::kSwitchMiddle);
		CHECK(s.control().Mix() == 1000);
		int32_t fb = s.inst->Params().shortFeedback;
		s.in.knob[0] = 2000; // towards 1000, not there yet
		s.in.knob[1] = 2000;
		s.RunMs(5);
		CHECK(s.control().Mix() == 1000);
		CHECK(s.inst->Params().shortFeedback == fb);
		s.in.knob[0] = 900; // passes through: picked up
		s.in.knob[1] = 900;
		s.RunMs(5);
		CHECK(s.control().Mix() == 900);
		CHECK(s.inst->Params().shortFeedback == (900 * eq::kFeedbackCeilingQ12) >> 12);
	});

	Test("tone select: flipping up alone changes nothing", [] {
		for (uint32_t seed = 1; seed < 6; seed++)
		{
			Sim s(seed, 4000);
			int32_t tone = s.control().ToneIndex();
			flip(s, eq::kSwitchUp);
			s.RunMs(20);
			CHECK(s.control().ToneIndex() == tone);
			for (int i = 0; i < 6; i++)
				CHECK(s.led[i] == (i == tone ? 4095 : 0));
		}
	});

	Test("tone select: Main picks one of six, LED shows it", [] {
		Sim s(1, 2048);
		flip(s, eq::kSwitchUp);
		const int32_t centres[6] = {341, 1024, 1707, 2389, 3072, 3755};
		for (int z = 5; z >= 0; z--)
		{
			s.in.knob[0] = centres[z];
			s.RunMs(5);
			CHECK(s.control().ToneIndex() == z);
			for (int i = 0; i < 6; i++)
				CHECK(s.led[i] == (i == z ? 4095 : 0));
		}
		s.in.knob[0] = 4095;
		s.RunMs(5);
		CHECK(s.control().ToneIndex() == 5);
	});

	Test("tone select: jitter at a zone boundary doesn't flicker", [] {
		Sim s(1, 2048);
		flip(s, eq::kSwitchUp);
		s.in.knob[0] = 1000;
		s.RunMs(5);
		CHECK(s.control().ToneIndex() == 1);
		for (int i = 0; i < 200; i++)
		{
			s.in.knob[0] = 1365 + (i % 2 ? 12 : -12);
			s.RunMs(1);
			CHECK(s.control().ToneIndex() == 1);
		}
	});

	Test("tap down: a new set, at once; no notes from the switch", [] {
		Sim s;
		uint32_t births = s.control().Births();
		s.in.sw = eq::kSwitchDown;
		s.RunMs(5);
		CHECK(s.control().Changing());
		CHECK(s.control().NewSets() == 1);
		CHECK(s.control().Births() == births);
		s.in.sw = eq::kSwitchMiddle;
		s.RunMs(100);
		// Another tap while the change is under way is ignored.
		s.in.sw = eq::kSwitchDown;
		s.RunMs(30);
		s.in.sw = eq::kSwitchMiddle;
		s.RunMs(30);
		CHECK(s.control().NewSets() == 1);
		CHECK(s.control().Taps() == 2);
	});

	Test("power-on: root, scale and tone are random", [] {
		int roots[12] = {}, scales[6] = {}, tones[6] = {};
		for (uint32_t seed = 1; seed <= 300; seed++)
		{
			Sim s(seed);
			roots[s.control().Root()]++;
			scales[s.control().ScaleIndex()]++;
			tones[s.control().ToneIndex()]++;
		}
		for (int i = 0; i < 12; i++)
			CHECK(roots[i] > 8);
		for (int i = 0; i < 6; i++)
			CHECK(scales[i] > 25 && tones[i] > 25);
	});
}

// ---- Milestone 2: the bell ----------------------------------------------------

static void Bell()
{
	// The bell bank on its own, struck directly.
	auto strike = [](eq::BellBank &bank, eq::EngineParams &p, int v, int32_t note) {
		eq::VoiceCmd c = eq::MakeStrike(note, 4096, 2048, eq::kTone[5]);
		c.seq = p.voice[v].seq + 1;
		p.voice[v] = c;
		bank.Accept(p);
	};
	auto peak = [](eq::BellBank &bank, int samples) {
		int32_t pk = 0;
		for (int i = 0; i < samples; i++)
		{
			int32_t x = bank.Process();
			pk = std::max(pk, std::abs(x));
		}
		return pk;
	};

	Test("bell: a strike rings and falls silent", [&] {
		eq::Tables::Init();
		auto bank = std::make_unique<eq::BellBank>();
		eq::EngineParams p;
		strike(*bank, p, 0, 72);
		int32_t early = peak(*bank, 48 * 200);
		peak(*bank, 48 * 1800);
		int32_t late = peak(*bank, 48 * 500); // 2 seconds in
		CHECK(early > 4000 && early < 9000); // a quarter of full scale
		CHECK(late < early / 8);			   // ~4s to -60dB: 2s is -30dB
		peak(*bank, 48 * 3000);
		CHECK(peak(*bank, 480) == 0); // reaches true silence
		CHECK(bank->Amp(0) == 0);
	});

	Test("voices: every tone, note and level goes idle", [&] {
		for (int t = 0; t < eq::kTones; t++)
			for (int32_t note = 60; note <= 95; note += 5)
				for (int32_t level : {4096, 1000, 200, 40})
				{
					auto bank = std::make_unique<eq::BellBank>();
					eq::EngineParams p;
					eq::VoiceCmd c = eq::MakeStrike(note, level, 4096, eq::kTone[t]);
					c.seq = 1;
					p.voice[0] = c;
					bank->Accept(p);
					peak(*bank, 48 * 9000);
					CHECK(bank->Amp(0) == 0);
				}
	});

	Test("tones: decay times, brightness and octaves as designed", [&] {
		// Time for a C5 strike to fall 40dB, per tone.
		int32_t ms[eq::kTones];
		for (int t = 0; t < eq::kTones; t++)
		{
			auto bank = std::make_unique<eq::BellBank>();
			eq::EngineParams p;
			eq::VoiceCmd c = eq::MakeStrike(72, 4096, 2048, eq::kTone[t]);
			c.seq = 1;
			p.voice[0] = c;
			bank->Accept(p);
			peak(*bank, 480);
			int32_t top = bank->Amp(0);
			ms[t] = 10;
			while (bank->Amp(0) > top / 100 && ms[t] < 20000)
			{
				peak(*bank, 48);
				ms[t]++;
			}
		}
		printf("    (to -40dB: harp %d, marimba %d, xylophone %d, glock %d, vibes %d, bell %d ms)\n", ms[0], ms[1],
			   ms[2], ms[3], ms[4], ms[5]);
		CHECK(ms[2] < ms[1] && ms[1] < ms[0]); // xylophone < marimba < harp
		CHECK(ms[0] < ms[3] && ms[3] < ms[5]); // harp < glock < bell
		CHECK(ms[4] > ms[5]);				   // vibes ring longest
		CHECK(ms[0] > 1000 && ms[0] < 2000);   // harp ~2s (to -60dB)
		CHECK(ms[1] > 500 && ms[1] < 900);	   // marimba ~1s
		// Glockenspiel and xylophone sound an octave up.
		CHECK(eq::MakeStrike(72, 4096, 0, eq::kTone[3]).carInc == eq::Tables::noteInc[84]);
		CHECK(eq::MakeStrike(72, 4096, 0, eq::kTone[2]).carInc == eq::Tables::noteInc[84]);
		CHECK(eq::MakeStrike(72, 4096, 0, eq::kTone[0]).carInc == eq::Tables::noteInc[72]);
		// The marimba's brightness is a click; the vibraphone's is steady.
		CHECK(eq::MakeStrike(72, 4096, 0, eq::kTone[1]).indexDecay > 100 * eq::MakeStrike(72, 4096, 0, eq::kTone[4]).indexDecay);
	});

	Test("bell: a new pitch on a ringing voice doesn't click", [&] {
		// The biggest step between samples, over 50ms.
		auto steps = [](eq::BellBank &bank) {
			int32_t last = bank.Process(), jump = 0;
			for (int i = 0; i < 48 * 50; i++)
			{
				int32_t x = bank.Process();
				jump = std::max(jump, std::abs(x - last));
				last = x;
			}
			return jump;
		};
		// A fresh strike of the new note on its own...
		auto fresh = std::make_unique<eq::BellBank>();
		eq::EngineParams p1;
		strike(*fresh, p1, 0, 79);
		int32_t alone = steps(*fresh);
		// ...against the same strike replacing a note that's still ringing.
		auto bank = std::make_unique<eq::BellBank>();
		eq::EngineParams p;
		strike(*bank, p, 0, 60);
		peak(*bank, 48 * 300);
		strike(*bank, p, 0, 79);
		int32_t replacing = steps(*bank);
		CHECK(replacing <= alone + alone / 10);
	});

	Test("bell: no sound for the first 100ms", [] {
		// Sim starts after the settling time, so run an instrument by hand
		// from power-on instead.
		auto inst = std::make_unique<eq::Equanimity>();
		inst->Seed(5);
		eq::Inputs in{};
		in.sw = eq::kSwitchDown; // even with the switch held down
		eq::Outputs out{};
		int32_t led[6];
		int32_t pk = 0;
		for (int i = 0; i < 4800; i++)
		{
			inst->Audio(in, out);
			inst->ControlTick(led);
			pk = std::max(pk, std::abs(out.audio[0]));
		}
		CHECK(pk == 0);
	});
}

// ---- Milestone 3: note loops ------------------------------------------------------

static void LoopTests()
{
	Test("loops: the card speaks at power-on", [] {
		Sim s(7, 0);
		CHECK(s.control().GetLoops().ActiveCount() == 1);
	});

	Test("loops: notes in the set's scale and root, two octaves; periods 5-25s", [] {
		int checked = 0;
		for (uint32_t seed = 11; seed < 17; seed++)
		{
			Sim s(seed, 2048);
			int32_t root = s.control().Root(), scale = s.control().ScaleIndex();
			for (int sec = 0; sec < 120; sec++)
			{
				s.RunMs(1000);
				const eq::Loops &l = s.control().GetLoops();
				CHECK(l.ActiveCount() <= 8);
				for (int i = 0; i < 8; i++)
				{
					if (!l.Active(i))
						continue;
					int32_t n = l.Note(i);
					CHECK(n >= 60 + root && n <= 84 + root);
					CHECK(eq::InScale(scale, root, n));
					if (!s.control().Changing())
						CHECK(l.Period(i) >= eq::kLoopPeriodMinMs && l.Period(i) <= eq::kLoopPeriodMaxMs);
					checked++;
				}
			}
		}
		CHECK(checked > 300);
	});

	Test("scales: Dorian and Lydian", [] {
		// D Dorian is all white notes; so is F Lydian.
		for (int32_t n = 60; n < 72; n++)
		{
			bool white = (0b101010110101 >> (n % 12)) & 1;
			CHECK(eq::InScale(4, 2, n) == white);
			CHECK(eq::InScale(5, 5, n) == white);
		}
	});

	Test("loops: a loop repeats on its own period, then ends", [] {
		// The loops on their own, with no other births to retire them.
		for (uint32_t seed = 1; seed <= 20; seed++)
		{
			eq::Random rng(seed);
			eq::Loops loops;
			eq::EngineParams p;
			int32_t silent[8] = {};
			loops.Birth(p, rng, 0, 0, eq::kTone[5], 2048, silent);
			int slot = -1;
			for (int i = 0; i < 8; i++)
				if (loops.Active(i))
					slot = i;
			int32_t period = loops.Period(slot);
			CHECK(period >= eq::kLoopPeriodMinMs && period <= eq::kLoopPeriodMaxMs);
			uint32_t seq = p.voice[slot].seq;
			int strikes = 1, ms = 0, lastStrike = 0;
			bool regular = true;
			while (loops.Active(slot) && ms < 600000)
			{
				loops.Tick(p, eq::kTone[5]);
				ms++;
				if (p.voice[slot].seq != seq)
				{
					seq = p.voice[slot].seq;
					regular &= ms - lastStrike == period;
					lastStrike = ms;
					strikes++;
				}
			}
			CHECK(regular);
			CHECK(strikes >= 4 && strikes <= 10);
			CHECK(!loops.Active(slot));
		}
	});

	Test("loops: a tone change applies from each loop's next strike", [] {
		Sim s(21, 2048);
		s.RunMs(20000);
		s.in.sw = eq::kSwitchUp;
		s.RunMs(5);
		int32_t want = (s.control().ToneIndex() + 3) % 6;
		s.in.knob[0] = want * 683 + 341;
		s.RunMs(5);
		CHECK(s.control().ToneIndex() == want);
		// Within one loop period, every loop still playing has been struck
		// with the new tone's modulator ratio.
		s.RunMs(eq::kLoopPeriodMaxMs + 100);
		const eq::EngineParams &p = s.inst->Params();
		for (int i = 0; i < 8; i++)
			if (s.control().GetLoops().Active(i))
				CHECK(p.voice[i].modInc == (p.voice[i].carInc >> 8) * (uint32_t)eq::kTone[want].ratioQ8);
	});

	Test("loops: same seed, same melody; new seed, new melody", [] {
		auto notes = [](uint32_t seed) {
			Sim s(seed, 2048);
			std::vector<int32_t> v;
			for (int i = 0; i < 60; i++)
			{
				s.RunMs(1000);
				for (int k = 0; k < 8; k++)
					v.push_back(s.control().GetLoops().Active(k) ? s.control().GetLoops().Note(k) : 0);
			}
			return v;
		};
		CHECK(notes(5) == notes(5));
		CHECK(notes(5) != notes(6));
	});

	Test("loops: a new note with all eight busy retires one", [] {
		Sim s(9, 2048);
		for (int i = 0; i < 12; i++)
		{
			s.pulse[0] = true; // Pulse In 1
			s.RunMs(60);
		}
		CHECK(s.control().GetLoops().ActiveCount() == 8);
	});
}

// ---- Milestone 4: delays -----------------------------------------------------------

static void DelayTests()
{
	// The delays on their own: an impulse in, then watch what comes back.
	struct Rig
	{
		std::unique_ptr<eq::Delays> d = std::make_unique<eq::Delays>();
		eq::EngineParams p;
		int32_t l = 0, r = 0;
		void Step(int32_t in) { d->Process(in, p, l, r); }
	};

	Test("delays: echoes at 1.1s and 9.7s", [] {
		Rig rig;
		rig.p.shortFeedback = rig.p.longFeedback = 0;
		// A 2ms burst of 1kHz, low enough to pass the long line's lowpass.
		std::vector<int32_t> out;
		for (int i = 0; i < 48000 * 11; i++)
		{
			int32_t in = i < 96 ? (int32_t)(20000 * sin(2 * 3.14159265 * 1000 * i / 48000.0)) : 0;
			rig.Step(in);
			out.push_back(rig.l);
		}
		auto energyAround = [&](double sec) {
			int64_t e = 0;
			int c = (int)(sec * 48000);
			for (int i = c - 480; i < c + 480; i++)
				e += (int64_t)out[i] * out[i];
			return e;
		};
		CHECK(energyAround(1.1) > 1000 * energyAround(0.6));
		CHECK(energyAround(9.7) > 1000 * energyAround(5.0));
		CHECK(energyAround(2.2) == 0); // no feedback: no second echo
	});

	Test("delays: echoes die away to true silence", [] {
		Rig rig;
		rig.p.shortFeedback = rig.p.longFeedback = (int32_t)(0.8 * 4096);
		for (int i = 0; i < 48000; i++)
			rig.Step((int32_t)(12000 * sin(2 * 3.14159265 * 440 * i / 48000.0)));
		bool silent = false;
		for (int sec = 0; sec < 600 && !silent; sec++)
		{
			silent = true;
			for (int i = 0; i < 48000; i++)
			{
				rig.Step(0);
				if (rig.l != 0 || rig.r != 0)
					silent = false;
			}
		}
		CHECK(silent);
	});

	Test("delays: the DC blocker removes offsets and settles at exactly zero", [] {
		eq::DcBlocker b;
		int32_t y = 0;
		for (int i = 0; i < 48000; i++)
			y = b.Process(500 + (int32_t)(3000 * sin(2 * 3.14159265 * 300 * i / 48000.0)));
		int64_t sum = 0;
		for (int i = 0; i < 48000; i++)
			sum += b.Process(500 + (int32_t)(3000 * sin(2 * 3.14159265 * 300 * i / 48000.0)));
		CHECK(std::abs(sum / 48000) <= 1); // the 500 offset is gone
		for (int i = 0; i < 48000 * 2; i++)
			y = b.Process(0);
		bool zero = true;
		for (int i = 0; i < 48000; i++)
			zero &= b.Process(0) == 0;
		CHECK(zero && y == 0);
	});

	Test("delays: full feedback holds for a long time without running away", [] {
		Rig rig;
		rig.p.shortFeedback = rig.p.longFeedback = eq::kFeedbackCeilingQ12;
		// Two minutes of loud input.
		int32_t peak = 0;
		for (int i = 0; i < 48000 * 120; i++)
		{
			rig.Step((int32_t)(16000 * sin(2 * 3.14159265 * 330 * i / 48000.0)));
			peak = std::max(peak, std::abs(rig.l));
		}
		CHECK(peak <= 65534); // two lines, each held within +/-32767
		// Then silence in: still sounding 30s later.
		int64_t e = 0;
		for (int i = 0; i < 48000 * 31; i++)
		{
			rig.Step(0);
			if (i >= 48000 * 30)
				e += (int64_t)rig.l * rig.l;
		}
		double rms = sqrt((double)e / 48000);
		printf("    (rms 30s after the input stops: %.0f of 32768)\n", rms);
		CHECK(rms > 300);
	});
}

// ---- Milestone 5: the tide -------------------------------------------------------------

static void TideTests()
{
	Test("tide: 0..1, slow, and new every power-on", [] {
		eq::Random r1(1), r2(2);
		eq::Tide a, b;
		a.Seed(r1);
		b.Seed(r2);
		int32_t lo = 4096, hi = 0, maxStep = 0, differ = 0, extremes = 0;
		int32_t last = a.At(0);
		for (uint32_t sec = 0; sec < 3 * 3600; sec++)
		{
			int32_t t = a.At(sec * 48000);
			lo = std::min(lo, t);
			hi = std::max(hi, t);
			maxStep = std::max(maxStep, std::abs(t - last));
			last = t;
			differ += std::abs(t - b.At(sec * 48000)) > 400 ? 1 : 0;
			extremes += (t < 820 || t > 3276) ? 1 : 0;
		}
		CHECK(lo >= 0 && hi <= 4096);
		CHECK(lo < 600 && hi > 3500); // it does reach the extremes...
		CHECK(extremes < 3 * 3600 / 4); // ...but spends most of its time between
		CHECK(maxStep < 600);			// at most ~15% in a second
		CHECK(differ > 3600);
	});

	auto birthsPerMinute = [](int32_t tide) {
		Sim s(17, 2048);
		s.inst->GetControl().HoldTide(tide);
		uint32_t before = s.control().Births();
		s.RunMs(20 * 60000);
		return (s.control().Births() - before) / 20.0;
	};
	Test("tide: births every ~12s at low tide, ~2.5s at high", [&] {
		double low = birthsPerMinute(0), high = birthsPerMinute(4096), mid = birthsPerMinute(2048);
		printf("    (births per minute: low %.1f, middle %.1f, high %.1f)\n", low, mid, high);
		CHECK(low > 3.5 && low < 6.5);
		CHECK(high > 19 && high < 29);
		CHECK(mid < (low + high) / 2); // the curve: the middle stays calm
	});

	Test("tide: notes born at high tide are brighter", [] {
		eq::VoiceCmd lowTide = eq::MakeStrike(72, 4096, 0, eq::kTone[5]);
		eq::VoiceCmd highTide = eq::MakeStrike(72, 4096, 4096, eq::kTone[5]);
		CHECK(highTide.peakIndex > 5 * lowTide.peakIndex);
		CHECK(highTide.lpCoef > 3 * lowTide.lpCoef);
	});

	Test("tide: LED meter fills from the bottom", [] {
		Sim s(1);
		auto &c = s.inst->GetControl();
		c.HoldTide(0);
		s.RunMs(500);
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] == 0);
		c.HoldTide(2048); // half: bottom and middle-left full, the rest off
		s.RunMs(500);
		CHECK(s.led[4] == 4095 && s.led[5] == 4095 && s.led[2] == 4095);
		CHECK(s.led[3] == 0 && s.led[0] == 0 && s.led[1] == 0);
		c.HoldTide(4096);
		s.RunMs(500);
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] == 4095);
	});

	Test("tide: a birth flickers the LEDs", [] {
		Sim s(1);
		s.inst->GetControl().HoldTide(4096);
		s.RunMs(500);
		s.pulse[0] = true; // Pulse In 1: a note, 20ms later
		s.RunMs(40);
		CHECK(s.led[0] < 4095);
		s.RunMs(300);
		CHECK(s.led[0] == 4095);
	});

	Test("wobble: read points drift within their depth, L and R apart", [] {
		Sim s(8);
		int32_t lo = INT32_MAX, hi = 0, apart = 0;
		for (int i = 0; i < 60000; i++)
		{
			s.RunMs(1);
			const eq::EngineParams &p = s.inst->Params();
			int32_t w = p.longDelay[0] - (eq::kLongDelaySamples << 12);
			lo = std::min(lo, w);
			hi = std::max(hi, w);
			apart = std::max(apart, std::abs(p.shortDelay[0] - p.shortDelay[1]));
		}
		int32_t depth = eq::kLongWobbleMs * 8 << 12;
		CHECK(lo >= 0 && hi <= depth);
		CHECK(hi - lo > depth / 3);
		CHECK(apart > (eq::kShortWobbleMs * 24 << 12) / 4);
	});
}

// ---- Milestone 6: drone and tuning mode ------------------------------------------------

static void DroneTests()
{
	// Pearson correlation of two series.
	auto correlation = [](const std::vector<double> &x, const std::vector<double> &y) {
		double n = (double)x.size(), sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
		for (size_t i = 0; i < x.size(); i++)
		{
			sx += x[i];
			sy += y[i];
			sxx += x[i] * x[i];
			syy += y[i] * y[i];
			sxy += x[i] * y[i];
		}
		return (n * sxy - sx * sy) / sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
	};

	Test("drone: four layers on the root -- root, 5th, octave, upper 5th, detuned", [] {
		for (uint32_t seed = 40; seed < 46; seed++)
		{
			Sim s(seed, 2048);
			s.RunMs(5000);
			int32_t base = eq::kDroneBottom + s.control().Root();
			const int32_t intervals[4] = {0, 7, 12, 19};
			for (int i = 0; i < 4; i++)
			{
				uint32_t exact = eq::Tables::noteInc[base + intervals[i]];
				uint32_t inc = s.inst->DroneInc(i);
				double cents = 1200 * log2((double)inc / exact);
				double want = 1200 * log2(1 + eq::kDroneLayer[i].detuneQ16 / 65536.0);
				CHECK(std::abs(cents - want) < 0.5);
			}
		}
	});

	Test("drone: each layer drifts on its own -- volume and timbre", [&] {
		Sim s(33, 2048);
		s.inst->GetControl().HoldTide(2048);
		std::vector<double> gain[4], index[4];
		for (int ms = 0; ms < 600000; ms += 100) // ten minutes
		{
			s.RunMs(100);
			for (int i = 0; i < 4; i++)
			{
				gain[i].push_back(s.inst->Params().droneLayer[i].gain);
				index[i].push_back(s.inst->Params().droneLayer[i].index);
			}
		}
		for (int i = 0; i < 4; i++)
		{
			// Each sweeps its full range...
			const eq::DroneLayer &L = eq::kDroneLayer[i];
			double lo = *std::min_element(gain[i].begin(), gain[i].end());
			double hi = *std::max_element(gain[i].begin(), gain[i].end());
			CHECK(hi >= L.weightQ12 * 0.98 && lo <= (L.weightQ12 * L.floorQ12 / 4096.0) * 1.05 + 2);
			double ilo = *std::min_element(index[i].begin(), index[i].end());
			double ihi = *std::max_element(index[i].begin(), index[i].end());
			CHECK(ihi > 2 * ilo);
			// ...independently of the others.
			for (int j = 0; j < i; j++)
			{
				CHECK(std::abs(correlation(gain[i], gain[j])) < 0.5);
				CHECK(std::abs(correlation(index[i], index[j])) < 0.5);
			}
			CHECK(std::abs(correlation(gain[i], index[i])) < 0.5);
		}
	});

	Test("drone: the colour shifts -- a different layer leads at different times", [] {
		Sim s(34, 2048);
		int leads[4] = {};
		for (int ms = 0; ms < 1200000; ms += 500) // twenty minutes
		{
			s.RunMs(500);
			// Which of the upper three layers is strongest, relative to its weight?
			int best = 1;
			for (int i = 2; i < 4; i++)
				if (s.inst->Params().droneLayer[i].gain * eq::kDroneLayer[best].weightQ12 >
					s.inst->Params().droneLayer[best].gain * eq::kDroneLayer[i].weightQ12)
					best = i;
			leads[best]++;
		}
		for (int i = 1; i < 4; i++)
			CHECK(leads[i] > 2400 / 10);
	});

	Test("drone: pitch changes only with a new set, and only in silence", [] {
		Sim s(31, 2048);
		s.inst->GetControl().HoldTide(4096);
		uint32_t inc = s.inst->DroneInc(0);
		int32_t lastAmp = s.inst->DroneAmp();
		int changes = 0, changesBeforeSet = 0;
		bool badChange = false;
		for (int i = 0; i < 48 * 300000; i++) // five minutes, a new set halfway
		{
			if (i == 48 * 150000)
				s.pulse[1] = true;
			s.Step();
			if (s.inst->DroneInc(0) != inc)
			{
				badChange |= lastAmp != 0; // silent until the moment it changed
				inc = s.inst->DroneInc(0);
				changes++;
				if (i < 48 * 150000)
					changesBeforeSet++;
			}
			lastAmp = s.inst->DroneAmp();
		}
		CHECK(!badChange);
		CHECK(changesBeforeSet == 0);
		CHECK(changes <= 1); // one, unless the new root happens to be the same
		CHECK(s.inst->DroneAmp() == eq::kDronePeakQ12 * 4096);
	});

	Test("drone: the filter drifts slowly, and opens with the tide", [] {
		auto range = [](int32_t tide, int32_t &lo, int32_t &hi, int32_t &maxStep) {
			Sim s(35, 2048);
			s.inst->GetControl().HoldTide(tide);
			s.RunMs(100); // past the jump to the held tide
			lo = INT32_MAX, hi = 0, maxStep = 0;
			int32_t last = s.inst->Params().droneCutoff;
			for (int ms = 0; ms < 180000; ms++)
			{
				s.RunMs(1);
				int32_t c = s.inst->Params().droneCutoff;
				lo = std::min(lo, c);
				hi = std::max(hi, c);
				maxStep = std::max(maxStep, std::abs(c - last));
				last = c;
			}
		};
		int32_t lo0, hi0, step0, lo1, hi1, step1;
		range(0, lo0, hi0, step0);
		range(4096, lo1, hi1, step1);
		CHECK(hi0 > 2 * lo0);					  // the cycle sweeps over an octave
		CHECK(lo1 > 3 * lo0 && hi1 > 3 * hi0);	  // the tide opens it further
		printf("    (cutoff, Q15: low tide %d..%d, high tide %d..%d; biggest step per ms %d / %d)\n", lo0, hi0, lo1, hi1,
			   step0, step1);
		CHECK(step0 * 1000 <= hi0 && step1 * 1000 <= hi1); // slowly: steps under 0.1%
	});

	Test("drone: its volume is the Y knob alone (the ebb CV is free)", [] {
		Sim s(36, 2048);
		s.RunMs(10000);
		int32_t g = s.inst->Params().droneGain;
		for (int ms = 0; ms < 20000; ms++)
		{
			s.RunMs(1);
			CHECK(s.inst->Params().droneGain == g);
		}
		CHECK(g == (eq::kDroneVolumeStart * eq::kDroneVolumeStart) >> 12);
	});
}

// ---- Milestone 7: wander CV and tide gate ---------------------------------------------------

static void OutputTests()
{
	auto triggersPerMinute = [](int32_t curve, int minutes) {
		eq::Random rng(4);
		eq::TideTriggers t;
		int count = 0;
		for (int64_t ms = 0; ms < (int64_t)minutes * 60000; ms++)
			count += t.Tick(rng, curve) ? 1 : 0;
		return count / (double)minutes;
	};

	Test("tide triggers: sparse at low tide, busy at high", [&] {
		double low = triggersPerMinute(0, 600), high = triggersPerMinute(4096, 60);
		printf("    (low tide: %.1f/min; high tide: %.0f/min)\n", low, high);
		CHECK(low > 2 && low < 4.5);
		CHECK(high > 120);
	});

	Test("Pulse Out 1: a 10ms trigger on every melody strike", [] {
		Sim s(9, 2048);
		s.inst->GetControl().HoldTide(4096);
		uint32_t strikes = s.control().GetLoops().Strikes();
		int rises = 0, highMs = 0;
		bool last = s.out.pulse[0];
		for (int ms = 0; ms < 120000; ms++)
		{
			s.RunMs(1);
			rises += s.out.pulse[0] && !last ? 1 : 0;
			highMs += s.out.pulse[0] ? 1 : 0;
			last = s.out.pulse[0];
		}
		int struck = (int)(s.control().GetLoops().Strikes() - strikes);
		// Strikes in the same few ms merge into one trigger.
		CHECK(rises > struck * 8 / 10 && rises <= struck);
		CHECK(highMs >= rises * 10 && highMs <= struck * 10 + 10);
	});

	Test("Pulse Out 2: 10ms triggers", [] {
		Sim s(9, 2048);
		s.inst->GetControl().HoldTide(4096);
		int rises = 0, highMs = 0;
		bool last = false;
		for (int ms = 0; ms < 60000; ms++)
		{
			s.RunMs(1);
			rises += s.out.pulse[1] && !last ? 1 : 0;
			highMs += s.out.pulse[1] ? 1 : 0;
			last = s.out.pulse[1];
		}
		CHECK(rises > 60);
		// 10ms each (two that land within 10ms of each other merge).
		CHECK(highMs >= rises * 10 && highMs <= rises * 10 + 50);
	});

	Test("CV Out 1 and 2: ebb and flow, 0 to ~5V, faster at high tide", [] {
		auto run = [](int32_t tide, int32_t &lo, int32_t &hi, int64_t &travel) {
			Sim s(6, 2048);
			s.inst->GetControl().HoldTide(tide);
			lo = INT32_MAX, hi = INT32_MIN, travel = 0;
			int32_t last = s.out.cv[1];
			for (int i = 0; i < 120000; i++)
			{
				s.RunMs(1);
				CHECK(s.out.cv[0] + s.out.cv[1] == eq::kWanderMax * 128); // mirror images
				lo = std::min(lo, s.out.cv[1]);
				hi = std::max(hi, s.out.cv[1]);
				CHECK(std::abs(s.out.cv[1] - last) < 128 * 8); // no jumps
				travel += std::abs(s.out.cv[1] - last);
				last = s.out.cv[1];
			}
		};
		int32_t lo, hi;
		int64_t slow, fast;
		run(0, lo, hi, slow);
		CHECK(lo >= 0 && hi <= eq::kWanderMax * 128);
		CHECK(hi - lo > eq::kWanderMax * 128 / 3);
		run(4096, lo, hi, fast);
		CHECK(fast > 3 * slow);
	});

	Test("vibraphone: tremolo only on that tone", [] {
		Sim s(2, 2048);
		s.in.sw = eq::kSwitchUp;
		s.RunMs(5);
		s.in.knob[0] = 4 * 683 + 341; // vibraphone
		s.RunMs(500);
		CHECK(s.inst->Params().tremoloDepth == eq::kTremoloDepthQ12);
		s.in.knob[0] = 5 * 683 + 341; // bell
		s.RunMs(500);
		CHECK(s.inst->Params().tremoloDepth == 0);
	});
}

// ---- Milestone 8: inputs ---------------------------------------------------------------

static void InputTests()
{
	Test("Pulse In 1: a note, 20ms after the edge", [] {
		Sim s(12, 2048);
		s.inst->GetControl().HoldTide(0);
		uint32_t births = s.control().Births();
		s.pulse[0] = true;
		s.RunMs(15);
		CHECK(s.control().Births() == births);
		s.RunMs(10);
		CHECK(s.control().Births() == births + 1);
	});

	Test("Pulse In 1: unplugging (a burst of probe edges) births nothing", [] {
		Sim s(12, 2048);
		s.inst->GetControl().HoldTide(0);
		uint32_t births = s.control().Births();
		// ~11ms of random edges, then jack detection reports the jack empty.
		for (int i = 0; i < 11 * 48; i++)
		{
			s.pulse[0] = (i % 37) == 0;
			s.Step();
		}
		s.in.pulseConnected[0] = false;
		s.RunMs(100);
		CHECK(s.control().Births() == births);
	});

	Test("Pulse In 2: old set fades, delays drain, then a new set", [] {
		Sim s(14, 2048, 4095, 4095);
		s.inst->GetControl().HoldTide(4096); // busy: plenty of loops
		s.RunMs(30000);
		const eq::Loops &l = s.control().GetLoops();
		CHECK(l.ActiveCount() >= 4);
		int32_t root = s.control().Root(), scale = s.control().ScaleIndex(), tone = s.control().ToneIndex();
		uint32_t births = s.control().Births();
		s.pulse[1] = true;
		s.RunMs(25);
		CHECK(s.control().NewSets() == 1 && s.control().Changing());
		s.pulse[1] = true; // ignored during the change
		s.RunMs(25);
		CHECK(s.control().NewSets() == 1);
		// The feedback falls away and the drone fades.
		s.RunMs(2000);
		CHECK(s.inst->Params().shortFeedback == 0 && s.inst->Params().longFeedback == 0);
		CHECK(s.inst->DroneAmp() == 0);
		// No new notes (Pulse In 1 included) and no second new set (a tap)
		// until this one is done.
		s.pulse[0] = true;
		s.in.sw = eq::kSwitchDown;
		s.RunMs(30);
		s.in.sw = eq::kSwitchMiddle;
		CHECK(s.control().NewSets() == 1);
		int ms = 2080;
		for (; ms < 60000 && s.control().Changing(); ms++)
		{
			s.RunMs(1);
			if (s.control().Changing())
				CHECK(s.control().Births() == births);
		}
		printf("    (the change took %.1fs)\n", ms / 1000.0);
		CHECK(!s.control().Changing());
		CHECK(ms > eq::kChangeoverDrainMs);
		// A new set begins with a note, a new seed, and the feedback returns.
		CHECK(s.control().Births() == births + 1);
		CHECK(s.control().Root() != root || s.control().ScaleIndex() != scale || s.control().ToneIndex() != tone);
		s.RunMs(eq::kChangeoverRestoreMs + 100);
		CHECK(s.inst->Params().longFeedback == (4095 * eq::kFeedbackCeilingQ12) >> 12);
		CHECK(s.inst->DroneAmp() > 0);
	});

	Test("Pulse In 2: the delays really are empty before the new set", [] {
		Sim s(15, 4095, 4095, 4095); // fully wet, full feedback
		s.inst->GetControl().HoldTide(4096);
		s.RunMs(60000);
		s.pulse[1] = true;
		uint32_t births = s.control().Births();
		// The loudest output in the last 10ms before the new set's first note.
		int32_t recent[480] = {};
		int n = 0;
		while (true)
		{
			s.Step();
			if (s.control().Births() != births)
				break; // this sample belongs to the new set
			recent[n++ % 480] = std::abs(s.out.audio[0]);
		}
		int32_t lastPeak = *std::max_element(recent, recent + 480);
		CHECK(lastPeak <= 1);
	});

	Test("Audio In 1 and 2: heard through the delays only", [] {
		// The same performance twice, with and without a tone on the inputs.
		auto render = [](int32_t main, bool tone) {
			Sim s(16, main, 2000, 2000);
			std::vector<int32_t> v;
			for (int i = 0; i < 48000 * 3; i++)
			{
				int32_t x = tone ? (int32_t)(800 * sin(2 * 3.14159265 * 220 * i / 48000.0)) : 0;
				s.in.audio[0] = x;
				s.in.audio[1] = -x / 2;
				s.Step();
				v.push_back(s.out.audio[0]);
			}
			return v;
		};
		CHECK(render(0, true) == render(0, false)); // fully dry: inputs silent
		std::vector<int32_t> with = render(2048, true), without = render(2048, false);
		// Half wet: identical for the first 1.1s, then the echo arrives.
		bool sameBefore = std::equal(with.begin(), with.begin() + 48000, without.begin());
		int64_t diffAfter = 0;
		for (int i = 60000; i < 100000; i++)
			diffAfter += std::abs(with[i] - without[i]);
		CHECK(sameBefore);
		CHECK(diffAfter > 40000 * 50);
	});
}

int main()
{
	Controls();
	Bell();
	LoopTests();
	DelayTests();
	TideTests();
	DroneTests();
	OutputTests();
	InputTests();
	if (failures)
	{
		printf("\n%d check(s) failed\n", failures);
		return 1;
	}
	printf("\nall tests passed\n");
	return 0;
}
