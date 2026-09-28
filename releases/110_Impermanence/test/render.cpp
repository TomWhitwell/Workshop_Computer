// Offline renders: `make -C test render` writes WAVs to test/out/ so the
// card can be heard before flashing. The source is Sim::Source (a plucked
// arpeggio with clicks); each file is one scripted performance.
#include <cstdio>
#include <string>
#include "Sim.h"

static void WriteWav(const std::string &path, const Sim &s)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
	{
		printf("can't write %s\n", path.c_str());
		return;
	}
	uint32_t frames = (uint32_t)s.outL.size();
	uint32_t dataBytes = frames * 4;
	auto u32 = [f](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto u16 = [f](uint16_t v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f);
	u32(36 + dataBytes);
	fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);
	u16(2);
	u32(48000);
	u32(48000 * 4);
	u16(4);
	u16(16);
	fwrite("data", 1, 4, f);
	u32(dataBytes);
	for (uint32_t i = 0; i < frames; i++)
	{
		fwrite(&s.outL[i], 2, 1, f);
		fwrite(&s.outR[i], 2, 1, f);
	}
	fclose(f);
	printf("wrote %s (%.1fs)\n", path.c_str(), frames / 48000.0);
}

// Turn the Main knob from a to b over n samples.
static void Turn(Sim &s, int k, int32_t a, int32_t b, int n)
{
	for (int i = 0; i < n; i++)
	{
		s.in.knob[k] = a + (int32_t)((int64_t)(b - a) * i / n);
		s.Step();
	}
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : ".";

	{
		// Texture: plain loop, then one small turn at a time, holding each
		// new loop for a few seconds; reverb half wet, long.
		Sim s(1, 20, 3500, 1800);
		s.Record();
		s.keep = true;
		s.Run(48000 * 3);
		for (int32_t c : {900, 1700, 2500, 3300, 4095})
		{
			Turn(s, 0, s.in.knob[0], c, 4800);
			s.Run(48000 * 4);
		}
		WriteWav(dir + "/texture_steps.wav", s);
	}
	{
		// Texture: a slow continuous sweep -- the loop is torn up the whole
		// time the knob moves.
		Sim s(2, 20, 2500, 1200);
		s.Record();
		s.keep = true;
		s.Run(48000 * 2);
		Turn(s, 0, 20, 4095, 48000 * 6);
		s.Run(48000 * 4);
		Turn(s, 0, 4095, 1500, 48000 * 3);
		s.Run(48000 * 4);
		WriteWav(dir + "/texture_sweep.wav", s);
	}
	{
		// Rhythm, clocked at 120bpm 16ths (6000 samples), dry-ish reverb.
		Sim s(3, 2600, 1500, 700);
		s.Record();
		s.in.sw = imp::kSwitchDown;
		s.Run(10);
		s.in.sw = imp::kSwitchMiddle;
		s.keep = true;
		for (int bar = 0; bar < 8; bar++)
		{
			if (bar == 4)
				s.in.knob[0] = 3600; // one flick: a new pattern
			for (int i = 0; i < 48000 * 2; i++)
			{
				if (i % 6000 == 0)
					s.pulse2 = true;
				s.Step();
			}
		}
		WriteWav(dir + "/rhythm_clocked.wav", s);
	}
	{
		// Shimmer: sparse slices into a long, fully wet tail.
		Sim s(4, 1800, 4095, 3400);
		s.Record();
		s.keep = true;
		s.Run(48000 * 12);
		WriteWav(dir + "/shimmer.wav", s);
	}
	return 0;
}
