// render.cpp — offline renderer + self-check for the Ouroboros tape loop.
//
// Compiles the REAL src/ouroboros.cpp against a mock hardware layer, runs
// assertions over the tape mechanics (echo period, splice pulse, freeze hold,
// reverse, feedback runaway/decay), then renders demo WAVs you can listen to
// without a Workshop Computer.
//
//   Audio Out 1 (wet) -> left channel,  Audio Out 2 (dry) -> right channel.
//
// Build & run:  ./build.sh

#include "computercard_mock.h"   // must come first: fakes the hardware layer
#include "../src/ouroboros.cpp"  // the actual card under test (its main() is skipped)

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

// Normalise interleaved wet/dry card output (each +/-2047) to -3 dBFS and write.
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

// The mirror of the card's length law: knob -> cells. Keep in sync.
static int32_t lenForKnob(int32_t k) { return 2400 + ((((k * k) >> 12) * 5990) >> 8); }

// A pluck: decaying sine burst added into an input timeline.
static void pluck(std::vector<int16_t> &in, int at, float hz, float amp = 1400.0f)
{
	for (int i = 0; i < SR; i++)
	{
		size_t t = (size_t)(at + i);
		if (t >= in.size()) break;
		float v = amp * expf(-4.0f * i / SR) * sinf(2.0f * 3.14159265f * hz * i / SR);
		int32_t s = in[t] + (int32_t)v;
		if (s > 2047) s = 2047; if (s < -2048) s = -2048;
		in[t] = (int16_t)s;
	}
}

static double rms(const std::vector<int32_t> &v, size_t a, size_t b)
{
	double acc = 0;
	for (size_t i = a; i < b && i < v.size(); i++) acc += (double)v[i] * v[i];
	return sqrt(acc / (double)(b - a));
}

// ============================ self-checks ============================

static void testEchoPeriod()
{
	printf("[1] echo period & splice pulse\n");
	Ouroboros card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simKnob[ComputerCard::Main] = 0;
	card.simKnob[ComputerCard::X] = 2048;
	card.simKnob[ComputerCard::Y] = 2048;

	for (int i = 0; i < SR; i++) card.simStep();       // let the length glide settle

	const int32_t L = lenForKnob(2048);                // cells
	const int expect = 2 * L;                          // ticks: 0.5 cells/tick

	// a 200-sample click, then silence; find when it comes off the tape
	std::vector<int32_t> wet;
	int lastRise = -1; std::vector<int> riseGaps; bool prevP = false;
	for (int i = 0; i < 4 * expect; i++)
	{
		card.simAudioIn[0] = (i < 200) ? 1500 : 0;
		card.simStep();
		wet.push_back(card.simAudioOut[0]);
		bool p = card.simPulseOut[0];
		if (p && !prevP) { if (lastRise >= 0) riseGaps.push_back(i - lastRise); lastRise = i; }
		prevP = p;
	}
	int onset = -1;
	for (int i = 1000; i < (int)wet.size(); i++)
		if (wet[i] > 300 || wet[i] < -300) { onset = i; break; }
	CHECK(onset > 0, "echo came back (onset %d)", onset);
	CHECK(abs(onset - expect) < 1500, "echo after one rotation: %d ticks, expected ~%d", onset, expect);
	CHECK(!riseGaps.empty(), "splice pulses seen (%d gaps)", (int)riseGaps.size());
	CHECK(abs(riseGaps.back() - expect) < 1500, "splice period %d ticks, expected ~%d", riseGaps.back(), expect);
}

static void testFreezeHolds()
{
	printf("[2] freeze holds the loop\n");
	Ouroboros card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simKnob[ComputerCard::Main] = 1067;           // moderate feedback
	card.simKnob[ComputerCard::X] = 0;                 // shortest tape: 2400 cells
	for (int i = 0; i < SR; i++) card.simStep();

	const int rot = 2 * 2400;
	unsigned rng = 12345;
	for (int i = 0; i < rot; i++)                      // one rotation of noise
	{
		rng = rng * 1664525u + 1013904223u;
		card.simAudioIn[0] = (int16_t)(((rng >> 16) & 0xFFF) - 2048);
		card.simStep();
	}
	card.simSwitch = ComputerCard::Up;                 // freeze
	card.simAudioIn[0] = 0;
	std::vector<int32_t> wet;
	for (int i = 0; i < 12 * rot; i++) { card.simStep(); wet.push_back(card.simAudioOut[0]); }

	double early = rms(wet, 3 * rot, 4 * rot);
	double late  = rms(wet, 11 * rot, 12 * rot);
	CHECK(early > 50, "frozen loop is audible (rms %.0f)", early);
	CHECK(late > 0.9 * early, "no decay while frozen (rms %.0f -> %.0f)", early, late);
}

static void testReverse()
{
	printf("[3] reverse plays the tape backwards\n");
	Ouroboros card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simKnob[ComputerCard::Main] = 0;
	card.simKnob[ComputerCard::X] = 0;
	for (int i = 0; i < SR; i++) card.simStep();

	const int rot = 2 * 2400;
	for (int i = 0; i < rot; i++)                      // record a rising ramp
	{
		card.simAudioIn[0] = (int16_t)(-1000 + (2000 * i) / rot);
		card.simStep();
	}
	card.simSwitch = ComputerCard::Down;               // reverse (playback only)
	card.simAudioIn[0] = 0;
	for (int i = 0; i < rot / 2; i++) card.simStep();
	int desc = 0, asc = 0, prev = card.simAudioOut[0];
	for (int i = 0; i < rot - 200; i++)
	{
		card.simStep();
		int v = card.simAudioOut[0], d = v - prev;
		if (d != 0 && d > -200 && d < 200) { if (d < 0) desc++; else asc++; }
		prev = v;
	}
	CHECK(desc > 2 * asc, "ramp comes back descending (%d down vs %d up)", desc, asc);
}

static void testRunawayBoundedAndDecays()
{
	printf("[4] runaway feedback saturates, then dies with the knob down\n");
	Ouroboros card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simKnob[ComputerCard::Main] = 4095;           // ~112%
	card.simKnob[ComputerCard::X] = 1024;
	for (int i = 0; i < SR; i++) card.simStep();

	std::vector<int16_t> in(4 * SR, 0);
	pluck(in, 0, 220.0f); pluck(in, SR / 3, 330.0f); pluck(in, 2 * SR / 3, 277.0f);
	std::vector<int32_t> wet;
	for (size_t i = 0; i < in.size(); i++)
	{
		card.simAudioIn[0] = in[i];
		card.simStep();
		int32_t w = card.simAudioOut[0];
		if (w > 2047 || w < -2048) { printf("  FAIL: wet out of range: %d\n", w); exit(1); }
		wet.push_back(w);
	}
	double sustained = rms(wet, wet.size() - SR / 2, wet.size());
	CHECK(sustained > 100, "self-oscillates at full feedback (rms %.0f)", sustained);

	card.simKnob[ComputerCard::Main] = 0;
	card.simAudioIn[0] = 0;
	std::vector<int32_t> tail;
	for (int i = 0; i < 2 * SR; i++) { card.simStep(); tail.push_back(card.simAudioOut[0]); }
	double late = rms(tail, tail.size() - SR / 4, tail.size());
	CHECK(late < sustained / 2, "decays once feedback is cut (rms %.0f -> %.0f)", sustained, late);
}

// ============================ demo renders ============================

// Run the card over an input timeline, with a per-tick hook for automation.
template <typename F>
static std::vector<int32_t> run(Ouroboros &card, const std::vector<int16_t> &in, F auto_)
{
	std::vector<int32_t> out;
	out.reserve(in.size() * 2);
	for (size_t i = 0; i < in.size(); i++)
	{
		auto_(card, (int)i);
		card.simAudioIn[0] = in[i];
		card.simStep();
		out.push_back(card.simAudioOut[0]);
		out.push_back(card.simAudioOut[1]);
	}
	return out;
}

static Ouroboros *freshCard(int mainK, int xK, int yK = 2048)
{
	Ouroboros *c = new Ouroboros();
	c->simConnected[ComputerCard::Audio1] = true;
	c->simKnob[ComputerCard::Main] = mainK;
	c->simKnob[ComputerCard::X] = xK;
	c->simKnob[ComputerCard::Y] = yK;
	for (int i = 0; i < SR; i++) c->simStep();
	return c;
}

int main(int, char **argv)
{
	std::string dir = argv[1] ? argv[1] : ".";
	printf("Ouroboros offline render\n\nself-checks:\n");
	testEchoPeriod();
	testFreezeHolds();
	testReverse();
	testRunawayBoundedAndDecays();
	printf("\ndemos:\n");

	{	// plain tape echo, ~1.1s rotation, feedback ~60%
		Ouroboros *c = freshCard(2184, 2048);
		std::vector<int16_t> in(10 * SR, 0);
		pluck(in, SR / 2, 220); pluck(in, 3 * SR, 330); pluck(in, 5 * SR + SR / 2, 262);
		finalize(dir + "/out_echo.wav", run(*c, in, [](Ouroboros &, int) {}));
		delete c;
	}
	{	// feedback past unity: three plucks smear into saturated self-oscillation
		Ouroboros *c = freshCard(4095, 1500);
		std::vector<int16_t> in(14 * SR, 0);
		pluck(in, SR / 2, 220); pluck(in, SR, 277); pluck(in, 3 * SR / 2, 330);
		finalize(dir + "/out_runaway.wav", run(*c, in, [](Ouroboros &card, int t) {
			if (t == 10 * SR) card.simKnob[ComputerCard::Main] = 1500;   // let it die
		}));
		delete c;
	}
	{	// record a phrase, freeze it, then drag the tape speed down an octave
		Ouroboros *c = freshCard(2000, 2048);
		std::vector<int16_t> in(16 * SR, 0);
		pluck(in, SR / 2, 262); pluck(in, SR, 330); pluck(in, 3 * SR / 2, 392); pluck(in, 2 * SR, 494);
		finalize(dir + "/out_freeze_varispeed.wav", run(*c, in, [](Ouroboros &card, int t) {
			if (t == 3 * SR) card.simSwitch = ComputerCard::Up;          // freeze
			if (t > 5 * SR && t <= 9 * SR)                               // 4s sweep down
				card.simKnob[ComputerCard::Y] = 2048 - (2048 * (t - 5 * SR)) / (4 * SR);
			if (t == 12 * SR) card.simKnob[ComputerCard::Y] = 2048;      // snap back
		}));
		delete c;
	}
	{	// record, freeze, then rock the tape back and forth
		Ouroboros *c = freshCard(2000, 2048);
		std::vector<int16_t> in(12 * SR, 0);
		pluck(in, SR / 2, 330); pluck(in, SR + SR / 4, 415); pluck(in, 2 * SR, 494);
		finalize(dir + "/out_reverse.wav", run(*c, in, [](Ouroboros &card, int t) {
			if (t == 7 * SR / 2) card.simSwitch = ComputerCard::Up;      // freeze
			bool rev = (t >= 9 * SR / 2 && t < 13 * SR / 2) || (t >= 17 * SR / 2 && t < 21 * SR / 2);
			card.simPulse[1] = rev;                                      // reverse gate
			card.simConnected[ComputerCard::Pulse2] = true;
		}));
		delete c;
	}

	printf("\nall checks passed\n");
	return 0;
}
