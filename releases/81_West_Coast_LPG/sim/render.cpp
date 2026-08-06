// render.cpp — offline audio renderer for the Vactrol card.
//
// Compiles the REAL src/vactrol.cpp against a mock hardware layer, drives it
// with generated test signals at 48kHz, and writes stereo WAV files you can
// listen to without a Workshop Computer.
//
//   Audio Out 1 -> left channel,  Audio Out 2 -> right channel.
//
// Build & run:  ./build.sh     (or see build.sh for the g++ line)

#include "computercard_mock.h"   // must come first: fakes the hardware layer
#include "../src/vactrol.cpp"    // the actual card under test (its main() is skipped)

#include <cstdio>
#include <cstdint>
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
	printf("  %-20s %-44s card peak=%.0f%% of rail, on-rail %.2f%% of samples%s\n",
	       base, what, pct, railPct,
	       railPct > 1.0 ? "  <- hot: would clip on hardware" : "");
}

// true for `width` samples once every `period`, starting at `offset`
static inline bool pulseAt(long n, long period, long width, long offset)
{
	long p = (n - offset) % period;
	if (p < 0) p += period;
	return p < width;
}

// naive sawtooth oscillator (test signal only; aliasing irrelevant here)
struct Saw
{
	float phase = 0.0f;
	float next(float hz) { phase += hz / SR; if (phase >= 1.0f) phase -= 1.0f; return phase * 2.0f - 1.0f; }
};

// ============================ scenarios ============================

// 1) Self-ping percussion: nothing in the audio inputs, so each channel
//    excites itself with an internal noise burst -> resonant drum/marimba.
//    Switch = LPG. Resonance is swept up across the clip.
static void renderSelfPing(const std::string &dir)
{
	Vactrol card;
	card.simSwitch = ComputerCard::Middle;            // LPG
	card.simConnected[ComputerCard::Audio1] = false;  // -> self-ping
	card.simConnected[ComputerCard::Audio2] = false;
	card.simKnob[ComputerCard::X] = 2700;             // ch1 longer decay
	card.simKnob[ComputerCard::Y] = 1300;             // ch2 short, snappy

	long N = 8 * SR, quarter = SR / 2 /*120 BPM*/, eighth = quarter / 2;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		card.simKnob[ComputerCard::Main] = 1400 + (int32_t)(2600.0 * n / N); // reso sweep
		card.simPulse[0] = pulseAt(n, quarter, 48, 0);
		card.simPulse[1] = pulseAt(n, eighth, 48, eighth / 2);
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_selfping.wav", "8s, LPG self-ping, resonance sweep up", raw);
}

// 2) Plucked voice: oscillators patched into both audio inputs, pinged.
//    Switch = LPG. A little stereo arpeggio.
static void renderPluck(const std::string &dir)
{
	Vactrol card;
	card.simSwitch = ComputerCard::Middle;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simKnob[ComputerCard::X] = 2200;
	card.simKnob[ComputerCard::Y] = 2400;
	card.simKnob[ComputerCard::Main] = 2600;

	Saw o1, o2;
	const float arp1[4] = { 220.00f, 261.63f, 329.63f, 440.00f }; // A C E A
	const float arp2[4] = { 110.00f,  98.00f,  82.41f,  65.41f }; // descending bass
	long N = 8 * SR, beat = SR / 2;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		long step = n / beat;
		card.simAudioIn[0] = (int16_t)(o1.next(arp1[step % 4]) * 1500);
		card.simAudioIn[1] = (int16_t)(o2.next(arp2[(step / 2) % 4]) * 1500);
		card.simPulse[0] = pulseAt(n, beat, 48, 0);
		card.simPulse[1] = pulseAt(n, beat * 2, 48, beat); // ch2 half-time, offbeat
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_pluck.wav", "8s, LPG plucking a stereo arpeggio", raw);
}

// 3) Mode comparison: identical pinged oscillator, switch steps through
//    VCA (0-3s) -> LPG (3-6s) -> VCF (6-9s) so you can A/B the three modes.
static void renderModes(const std::string &dir)
{
	Vactrol card;
	card.simConnected[ComputerCard::Audio1] = true;
	card.simConnected[ComputerCard::Audio2] = true;
	card.simKnob[ComputerCard::X] = 2300;
	card.simKnob[ComputerCard::Y] = 2300;
	card.simKnob[ComputerCard::Main] = 3200; // higher reso to make VCF/LPG obvious

	Saw o1, o2;
	long N = 9 * SR, beat = SR / 2;
	std::vector<int32_t> raw; raw.reserve(N * 2);
	for (long n = 0; n < N; n++)
	{
		long seg = n / (3 * SR); // 0,1,2
		card.simSwitch = (seg == 0) ? ComputerCard::Down
		               : (seg == 1) ? ComputerCard::Middle
		                            : ComputerCard::Up;
		card.simAudioIn[0] = (int16_t)(o1.next(220.0f) * 1500);
		card.simAudioIn[1] = (int16_t)(o2.next(110.0f) * 1500);
		card.simPulse[0] = pulseAt(n, beat, 48, 0);
		card.simPulse[1] = pulseAt(n, beat, 48, 0);
		card.simStep();
		raw.push_back(card.simAudioOut[0]);
		raw.push_back(card.simAudioOut[1]);
	}
	finalize(dir + "/out_modes.wav", "9s: VCA(0-3) -> LPG(3-6) -> VCF(6-9)", raw);
}

int main(int argc, char **argv)
{
	std::string dir = (argc > 1) ? argv[1] : ".";
	printf("Rendering Vactrol test signals to: %s\n", dir.c_str());
	renderSelfPing(dir);
	renderPluck(dir);
	renderModes(dir);
	printf("Done. Open the .wav files in any audio player.\n");
	return 0;
}
