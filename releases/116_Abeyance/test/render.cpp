// Offline renders: what the card plays, as WAV files, for listening before
// flashing.
//   make -C test render        writes test/out/*.wav
//
//   opening.wav     the first 3 minutes after power-up (the original piece)
//   new_piece.wav   2 minutes after a tap down
//   drift_full.wav  2 minutes with drift and tape at full
//   input.wav       30s of a plucked tone into the audio inputs
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include "Sim.h"

static void WriteWav(const std::string &path, const std::vector<int16_t> &l, const std::vector<int16_t> &r)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
	{
		perror(path.c_str());
		exit(1);
	}
	uint32_t n = (uint32_t)l.size(), bytes = n * 4, rate = 48000, brate = rate * 4, fmtLen = 16;
	uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
	uint32_t riff = 36 + bytes;
	fwrite("RIFF", 1, 4, f);
	fwrite(&riff, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmtLen, 4, 1, f);
	fwrite(&fmt, 2, 1, f);
	fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f);
	fwrite(&brate, 4, 1, f);
	fwrite(&align, 2, 1, f);
	fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&bytes, 4, 1, f);
	for (uint32_t i = 0; i < n; i++)
	{
		fwrite(&l[i], 2, 1, f);
		fwrite(&r[i], 2, 1, f);
	}
	fclose(f);
}

// Peak, RMS and clipping for one render.
static void Report(const char *name, const Sim &s)
{
	double sum = 0;
	int peak = 0, clipped = 0;
	for (size_t i = 0; i < s.outL.size(); i++)
	{
		int a = std::abs((int)s.outL[i]), b = std::abs((int)s.outR[i]);
		peak = std::max(peak, std::max(a, b));
		clipped += (a >= 2047 * 16) + (b >= 2047 * 16);
		sum += (double)s.outL[i] * s.outL[i] + (double)s.outR[i] * s.outR[i];
	}
	double rms = std::sqrt(sum / (2.0 * s.outL.size()));
	printf("%-14s peak %5.1f dBFS  rms %5.1f dBFS  clipped samples %d  underruns %u\n", name,
		   20 * std::log10(peak / 32768.0 + 1e-9), 20 * std::log10(rms / 32768.0 + 1e-9), clipped, s.inst->Underruns());
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : "out";
	{
		Sim s(11);
		s.keep = true;
		s.RunMs(180 * 1000);
		Report("opening", s);
		WriteWav(dir + "/opening.wav", s.outL, s.outR);
	}
	{
		// 10s of the opening, the tap at 0:10 (the 15s fade-out), then
		// the new piece.
		Sim s(12);
		s.RunMs(20 * 1000);
		s.keep = true;
		s.RunMs(10 * 1000);
		s.Tap();
		s.RunMs(120 * 1000);
		Report("new_piece", s);
		WriteWav(dir + "/new_piece.wav", s.outL, s.outR);
	}
	{
		// Drift and tape at full: switch up, take X and Y over, back down.
		Sim s(13, 0, 0, 0);
		s.in.sw = ab::kSwitchUp;
		s.in.knob[0] = ab::kStartVolume;
		s.in.knob[1] = 4095;
		s.in.knob[2] = 4095;
		s.RunMs(100);
		s.in.knob[1] = 0;
		s.in.knob[2] = 0;
		s.RunMs(20);
		s.in.knob[1] = 4095;
		s.in.knob[2] = 4095;
		s.RunMs(100);
		s.in.sw = ab::kSwitchMiddle;
		s.keep = true;
		s.RunMs(120 * 1000);
		Report("drift_full", s);
		WriteWav(dir + "/drift_full.wav", s.outL, s.outR);
	}
	{
		// Main fully wet: the reverb macro's long, dark wash.
		Sim s(16);
		s.in.knob[0] = 4095;
		s.RunMs(100);
		s.keep = true;
		s.RunMs(120 * 1000);
		Report("wash", s);
		WriteWav(dir + "/wash.wav", s.outL, s.outR);
	}
	{
		// The choir alone (piano level at zero), to hear its voice.
		Sim s(15);
		s.in.knob[1] = 4095;
		s.RunMs(20);
		s.in.knob[1] = 0;
		s.RunMs(100);
		s.keep = true;
		s.RunMs(120 * 1000);
		Report("choir", s);
		WriteWav(dir + "/choir.wav", s.outL, s.outR);
	}
	{
		// A plucked 220Hz tone every 3s into both audio inputs.
		Sim s(14);
		s.keep = true;
		for (int i = 0; i < 30 * 48000; i++)
		{
			int t = i % (3 * 48000);
			double env = std::exp(-t / 9600.0);
			int v = (int)(1200 * env * std::sin(2 * M_PI * 220 * t / 48000.0));
			s.in.audio[0] = v;
			s.in.audio[1] = v;
			s.Step();
		}
		Report("input", s);
		WriteWav(dir + "/input.wav", s.outL, s.outR);
	}
	return 0;
}
