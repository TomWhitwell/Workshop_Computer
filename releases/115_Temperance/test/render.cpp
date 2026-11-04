// Offline renders: `make -C test render` writes stereo WAVs to test/out/,
// played by the same code as the firmware, for listening on a computer.
// Also prints how often each render drives the output's soft clip, which
// is how the mix levels were set.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include "Sim.h"

using namespace tem;

static void WriteWav(const std::string &path, const std::vector<int16_t> &l, const std::vector<int16_t> &r)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	uint32_t n = (uint32_t)l.size(), bytes = n * 4;
	auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);
	u16(2);
	u32(48000);
	u32(48000 * 4);
	u16(4);
	u16(16);
	fwrite("data", 1, 4, f);
	u32(bytes);
	for (uint32_t i = 0; i < n; i++)
	{
		u16((uint16_t)l[i]);
		u16((uint16_t)r[i]);
	}
	fclose(f);
	int64_t loud = 0;
	for (uint32_t i = 0; i < n; i++)
		loud += std::abs(l[i] / 16) > 1536 || std::abs(r[i] / 16) > 1536;
	printf("wrote %s (%.0fs, soft clip on %.2f%% of samples)\n", path.c_str(), n / 48000.0, 100.0 * (double)loud / n);
}

// A plucked melody, the sort of thing a micro looper eats: a pentatonic
// tune, a note every 250ms, each a decaying tone with a little second
// harmonic, about +-3.5V at its peak, with a 1.5ms attack. `seed` picks the
// tune. From `stopAt` on, no new notes: the last one rings out, as a player
// stops. (Cutting the input dead would be a click in its own right.)
static std::function<int32_t(uint64_t)> Melody(int seed, uint64_t stopAt = UINT64_MAX)
{
	return [seed, stopAt](uint64_t t) {
		static const int kNotes[] = {0, 3, 5, 7, 10, 12, 15, 17};
		uint64_t note = (t < stopAt ? t : stopAt) / 12000, at = t - note * 12000;
		int n = kNotes[(note * 5 + (uint64_t)seed * 3 + (note * note) % 7) % 8];
		double hz = 220.0 * pow(2.0, n / 12.0);
		double env = exp(-(double)at / 4000.0) * (at < 72 ? (double)at / 72.0 : 1.0);
		double ph = 2.0 * M_PI * hz * (double)(note * 12000 + at) / 48000.0;
		return (int32_t)lrint(1200.0 * env * (sin(ph) + 0.25 * sin(2.0 * ph)));
	};
}

// Knob value for the middle of each zone.
static int32_t ClockKnob(int32_t step) { return (9 - step) * 410 + 205; }
static int32_t SpeedKnob(int32_t zone) { return zone * 683 + 341; }

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : "out";

	{
		// The Mood move: a two-second phrase into both loops at 16kHz, A
		// forward at x1, B reversed at half speed; then the clock walks
		// down a step every four seconds, and back up.
		Sim s(ClockKnob(3), SpeedKnob(4), SpeedKnob(2));
		s.SetAlt(2048, 4095, 0);
		s.source[0] = Melody(1);
		s.keep = true;
		s.Tap();
		s.RunMs(2000 - 35);
		s.Tap();
		s.source[0] = Melody(1, s.t);
		for (int step : {3, 4, 5, 6, 5, 4, 3, 2})
		{
			s.in.knob[0] = ClockKnob(step);
			s.RunMs(4000);
		}
		WriteWav(dir + "/mood.wav", s.outL, s.outR);
	}
	{
		// Grit: 3kHz, crushed to 6 bits, the loops an octave apart, little
		// wash. The dry input stays clean while the loops are ruined.
		Sim s(ClockKnob(8), SpeedKnob(4), SpeedKnob(5));
		s.SetAlt(800, 4095, 2900);
		s.source[0] = Melody(2);
		s.keep = true;
		s.Tap();
		s.RunMs(6000 - 35);
		s.Tap();
		s.source[0] = Melody(2, s.t);
		s.RunMs(20000);
		WriteWav(dir + "/grit.wav", s.outL, s.outR);
	}
	{
		// Wash: 12kHz, full wash, both loops at half speed (the resting
		// position), the input still playing over them.
		Sim s(ClockKnob(4), SpeedKnob(3), SpeedKnob(2));
		s.SetAlt(4095, 3000, 0);
		s.source[0] = Melody(3);
		s.keep = true;
		s.Tap();
		s.RunMs(2500 - 35);
		s.Tap();
		s.RunMs(10000);
		s.source[0] = Melody(3, s.t);
		s.RunMs(20000);
		WriteWav(dir + "/wash.wav", s.outL, s.outR);
	}
	{
		// Two lengths: Pulse ins record A (1.5s) and B (2s) on their own at
		// 24kHz, so the loops phase against each other. No dry: loops only.
		Sim s(ClockKnob(2), SpeedKnob(4), SpeedKnob(4));
		s.SetAlt(2400, 0, 0);
		s.source[0] = Melody(4);
		s.keep = true;
		s.Pulse(0);
		s.Pulse(1);
		s.RunMs(1500 - 20);
		s.Pulse(0);
		s.RunMs(500 - 10);
		s.Pulse(1);
		s.source[0] = Melody(4, s.t);
		s.RunMs(24000);
		WriteWav(dir + "/phasing.wav", s.outL, s.outR);
	}
	return 0;
}
