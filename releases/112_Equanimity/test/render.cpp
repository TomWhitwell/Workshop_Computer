// Offline renders: `make -C test render` writes WAVs to test/out/ so the
// card can be heard before flashing. Each file is one scripted performance.
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

// Switch up and pick a tone with Main (0 Harp .. 5 Bell), then back to the
// middle with Main returning to `mix`.
static void PickTone(Sim &s, int tone)
{
	s.in.sw = eq::kSwitchUp;
	s.RunMs(5);
	s.in.knob[0] = tone * 683 + 341;
	s.RunMs(5);
	s.in.sw = eq::kSwitchMiddle;
	s.RunMs(5);
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : ".";

	{
		// The six tones in turn, 30s each, with the tide held high so notes
		// come often: harp, marimba, xylophone, glockenspiel, vibraphone,
		// bell. Mostly dry, a little feedback.
		Sim s(1, 1200, 1800, 1800);
		s.inst->GetControl().HoldTide(3500);
		s.keep = true;
		for (int t = 0; t < 6; t++)
		{
			PickTone(s, t);
			s.in.knob[0] = 1200; // wet/dry picks up again on the way back
			s.RunMs(30000);
		}
		WriteWav(dir + "/tones.wav", s);
	}
	{
		// Ten minutes with the tide doing its work: wet/dry a little past the
		// middle, feedback at about two thirds and three quarters.
		Sim s(5, 2600, 2700, 3000);
		s.keep = true;
		s.RunMs(600000);
		WriteWav(dir + "/tide.wav", s);
	}
	{
		// Feedback fully up, fully wet: the delays as a slowly evolving pad.
		Sim s(4, 4095, 4095, 4095);
		s.keep = true;
		s.RunMs(180000);
		WriteWav(dir + "/delays_full.wav", s);
	}
	{
		// A change of set: 60s, then Pulse In 2, then a minute of the new set.
		Sim s(8, 2600, 3200, 3400);
		s.keep = true;
		s.RunMs(60000);
		s.pulse[1] = true;
		s.RunMs(90000);
		WriteWav(dir + "/changeover.wav", s);
	}
	{
		// The drone on its own (melody volume down), dry, tide held high so
		// it moves: three minutes.
		Sim s(6, 0, 2048, 2048);
		s.inst->GetControl().HoldTide(4096);
		s.in.sw = eq::kSwitchUp;
		s.RunMs(5);
		s.in.knob[1] = 3800; // pick up melody volume...
		s.RunMs(5);
		s.in.knob[1] = 0; // ...and turn it down
		s.RunMs(5);
		s.in.sw = eq::kSwitchMiddle;
		s.keep = true;
		s.RunMs(180000);
		WriteWav(dir + "/drone.wav", s);
	}
	return 0;
}
