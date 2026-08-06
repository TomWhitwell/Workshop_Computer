// render.cpp — offline audio renderer for the Origami wavefolder card.
//
// Compiles the REAL src/origami.cpp against a mock hardware layer, drives it
// with generated test signals at 48kHz, and writes stereo WAV files you can
// listen to without a Workshop Computer.
//
//   Audio Out 1 -> left channel,  Audio Out 2 -> right channel.
//
// Build & run:  ./build.sh     (or see build.sh for the g++ line)

#include "computercard_mock.h"   // must come first: fakes the hardware layer
#include "../src/origami.cpp"    // the actual card under test (its main() is skipped)

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

static constexpr int SR = 48000;

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

static inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

// The card clamps its own output to +/-2047 (12-bit DAC range).
static constexpr int32_t CARD_FULLSCALE = 2047;

// Take raw card output samples (interleaved L/R, each in +/-2047), normalise to
// a comfortable -3 dBFS so every clip is clean and evenly levelled, write the
// WAV, and report what the *card* actually did (peak vs its rail, and how often
// it sat on the rail — which is what would hard-clip on real hardware).
static void finalize(const std::string &path, const char *what,
                     const std::vector<int32_t> &raw)
{
	int32_t peak = 1;
	long railHits = 0;
	for (int32_t v : raw)
	{
		int32_t a = iabs(v);
		if (a > peak) peak = a;
		if (a >= CARD_FULLSCALE) railHits++;
	}
	const int32_t target = (int32_t)(0.707 * 32767); // -3 dBFS
	std::vector<int16_t> out;
	out.reserve(raw.size());
	for (int32_t v : raw)
	{
		int32_t s = (int32_t)((int64_t)v * target / peak);
		if (s > 32767) s = 32767; if (s < -32768) s = -32768;
		out.push_back((int16_t)s);
	}
	writeWav(path, out);

	double pct = 100.0 * peak / CARD_FULLSCALE;
	double railPct = 100.0 * railHits / (raw.size() / 2.0);
	const char *base = path.c_str();
	for (const char *p = path.c_str(); *p; p++) if (*p == '/') base = p + 1;
	printf("  %-20s %-46s card peak=%.0f%% of rail, on-rail %.2f%% of samples%s\n",
	       base, what, pct, railPct,
	       railPct > 1.0 ? "  <- hot: would clip on hardware" : "");
}

// naive sine oscillator (test signal)
struct Sine
{
	double phase = 0.0;
	double next(double hz) { phase += hz / SR; if (phase >= 1.0) phase -= 1.0; return std::sin(2.0 * M_PI * phase); }
};

// ============================ scenarios ============================

// 1) Drive sweep: a steady sine into both channels, fold drive knob swept from
//    zero (near-clean) up to maximum (dense folding). Switch = triangle fold.
static void renderDriveSweep(const std::string &dir)
{
	Origami card;
	card.simSwitch = ComputerCard::Down;              // triangle fold
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simKnob[ComputerCard::Main] = 2048;          // no bias (symmetric)

	Sine o1, o2;
	long N = 8 * SR;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		int32_t k = (int32_t)(4095.0 * n / N);        // drive 0 -> max
		card.simKnob[ComputerCard::X] = k;
		card.simKnob[ComputerCard::Y] = k;
		card.simAudioIn[0] = (int16_t)(o1.next(110.0) * 1700);
		card.simAudioIn[1] = (int16_t)(o2.next(110.5) * 1700); // slight detune = motion
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_drivesweep.wav", "8s, 110Hz sine, triangle fold, drive 0->max", raw);
}

// 2) Character comparison: fixed moderate drive, switch steps through the three
//    fold characters so you can A/B them on identical input.
static void renderModes(const std::string &dir)
{
	Origami card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simKnob[ComputerCard::X] = 1700;             // moderate drive (a few folds)
	card.simKnob[ComputerCard::Y] = 1700;
	card.simKnob[ComputerCard::Main] = 2048;          // no bias

	Sine o1, o2;
	long N = 9 * SR;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		long seg = n / (3 * SR); // 0,1,2
		card.simSwitch = (seg == 0) ? ComputerCard::Down     // triangle
		               : (seg == 1) ? ComputerCard::Middle   // sine
		                            : ComputerCard::Up;       // hard clip
		card.simAudioIn[0] = (int16_t)(o1.next(147.0) * 1600);
		card.simAudioIn[1] = (int16_t)(o2.next(147.0) * 1600);
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_modes.wav", "9s: triangle(0-3) -> sine(3-6) -> clip(6-9)", raw);
}

// 3) Bias sweep: fixed drive, sine fold, Main knob (bias) swept across its range
//    so you hear the harmonic content go from symmetric (odd) to asymmetric (even).
static void renderBiasSweep(const std::string &dir)
{
	Origami card;
	card.simSwitch = ComputerCard::Middle;            // sine fold
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simKnob[ComputerCard::X] = 2400;
	card.simKnob[ComputerCard::Y] = 2400;

	Sine o1, o2;
	long N = 8 * SR;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		card.simKnob[ComputerCard::Main] = (int32_t)(4095.0 * n / N); // bias sweep
		card.simAudioIn[0] = (int16_t)(o1.next(196.0) * 1500);
		card.simAudioIn[1] = (int16_t)(o2.next(196.0) * 1500);
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_biassweep.wav", "8s, 196Hz sine, sine fold, bias sweep", raw);
}

// 4) CV-controlled folding: fixed knob drive, but a triangle LFO on CV In opens
//    and closes the folding rhythmically — the dedicated-card payoff (CV over fold).
static void renderCVFold(const std::string &dir)
{
	Origami card;
	card.simSwitch = ComputerCard::Down;              // triangle fold
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simConnected[ComputerCard::CV1] = true;
	card.simConnected[ComputerCard::CV2] = true;
	card.simKnob[ComputerCard::X] = 1200;             // modest base drive
	card.simKnob[ComputerCard::Y] = 1200;
	card.simKnob[ComputerCard::Main] = 2048;

	Sine o1, o2;
	long N = 8 * SR, lfoPeriod = SR; // 1 Hz fold LFO
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		// 0..+2048 triangle on CV (only positive CV adds folding here)
		long p = n % lfoPeriod;
		int32_t tri = (p < lfoPeriod / 2) ? (int32_t)(2048.0 * p / (lfoPeriod / 2))
		                                  : (int32_t)(2048.0 * (lfoPeriod - p) / (lfoPeriod / 2));
		card.simCVIn[0] = tri;
		card.simCVIn[1] = tri;
		card.simAudioIn[0] = (int16_t)(o1.next(82.41) * 1600);
		card.simAudioIn[1] = (int16_t)(o2.next(82.41) * 1600);
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_cvfold.wav", "8s, 82Hz sine, CV-swept fold (1Hz LFO)", raw);
}

int main(int argc, char **argv)
{
	std::string dir = (argc > 1) ? argv[1] : ".";
	printf("Rendering Origami test signals to: %s\n", dir.c_str());
	renderDriveSweep(dir);
	renderModes(dir);
	renderBiasSweep(dir);
	renderCVFold(dir);
	printf("Done. Open the .wav files in any audio player.\n");
	return 0;
}
