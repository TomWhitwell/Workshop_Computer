// Offline renders: `make -C test render` writes WAVs to test/out/ so the
// card can be heard before flashing. Each file is one scripted performance.
#include <cstdio>
#include <string>
#include "Sim.h"

using namespace constancy;

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

// Turn Main steadily from `from` to `to` over `ms`.
static void Sweep(Sim &s, int knob, int32_t from, int32_t to, int ms)
{
	for (int i = 0; i < ms; i++)
	{
		s.in.knob[knob] = from + (to - from) * i / ms;
		s.RunMs(1);
	}
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : ".";

	{
		// The whole morning in 90 seconds: Main from first light to two
		// hours after sunrise.
		Sim s(7, 0);
		s.keep = true;
		Sweep(s, 0, 0, 4095, 90000);
		s.RunMs(5000);
		WriteWav(dir + "/morning.wav", s);
	}
	{
		// Sun (X) from left to right over a minute, parked just after
		// sunrise: from short, clean echoes to a burning, saturated wash.
		Sim s(8, 2600, 0);
		s.keep = true;
		Sweep(s, 1, 0, 4095, 60000);
		s.RunMs(10000);
		WriteWav(dir + "/sun.wav", s);
	}
	{
		// Water (Y): from open, all the way left (drowning), back through
		// the centre and all the way right (thinning), and back.
		Sim s(9, 3000, 2600);
		s.keep = true;
		s.RunMs(8000);
		Sweep(s, 2, 2048, 0, 15000);
		Sweep(s, 2, 0, 4095, 30000);
		Sweep(s, 2, 4095, 2048, 15000);
		s.RunMs(5000);
		WriteWav(dir + "/water.wav", s);
	}
	{
		// The pad: a minute each at first light (high tide: long swells),
		// at low water just after sunrise (plucks), and at the end of the
		// window. A reseed (tap down) halfway through the second minute.
		Sim s(10, 0, 1900);
		s.keep = true;
		s.RunMs(60000);
		s.in.knob[0] = 2700;
		s.RunMs(30000);
		s.in.sw = kSwitchDown;
		s.RunMs(40);
		s.in.sw = kSwitchMiddle;
		s.RunMs(30000);
		s.in.knob[0] = 4095;
		s.RunMs(60000);
		WriteWav(dir + "/loops.wav", s);
	}
	{
		// Sunrise: three minutes from civil dawn to the sun well up. The lead
		// enters as the sun clears the horizon, more often as it climbs.
		Sim s(11, 1520, 1900);
		s.keep = true;
		Sweep(s, 0, 1520, 3300, 180000);
		s.RunMs(10000);
		WriteWav(dir + "/sunrise.wav", s);
	}
	{
		// The Levels page, mid-morning: the melody down and back, then the
		// drone up.
		Sim s(12, 3300, 1700);
		s.keep = true;
		s.RunMs(10000);
		s.in.sw = kSwitchUp;
		Sweep(s, 1, 2048, kMelodyLevelStart, 200);
		Sweep(s, 1, kMelodyLevelStart, 0, 8000);
		s.RunMs(10000);
		Sweep(s, 2, 2048, kDroneLevelStart, 200);
		Sweep(s, 2, kDroneLevelStart, 4095, 8000);
		s.RunMs(10000);
		Sweep(s, 1, 0, kMelodyLevelStart, 8000);
		s.RunMs(10000);
		WriteWav(dir + "/levels.wav", s);
	}
	return 0;
}
