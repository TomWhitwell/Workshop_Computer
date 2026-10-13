#pragma once
// Runs the instrument on a computer: a pretend panel (knobs, switch, jacks)
// driven one sample at a time. Used by host_tests.cpp and render.cpp.
//
// Each sample runs core 0's Audio() and then core 1's ControlTick(), as if
// core 1 kept pace exactly. `controlEvery` > 1 runs core 1 less often, to
// check the card still behaves when core 1 falls behind.
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Impermanence.h"

struct Sim
{
	std::unique_ptr<imp::Impermanence> inst = std::make_unique<imp::Impermanence>();
	imp::Inputs in{};
	imp::Outputs out{};
	std::vector<int16_t> outL, outR; // everything played, for renders
	bool keep = false;
	bool pulse1 = false, pulse2 = false;
	uint64_t t = 0;
	int controlEvery = 1;

	explicit Sim(uint32_t seed = 1234, int32_t main = 20, int32_t x = 2048, int32_t y = 20)
	{
		in.knob[0] = main;
		in.knob[1] = x;
		in.knob[2] = y;
		in.sw = imp::kSwitchMiddle;
		in.audio2Connected = true;
		in.pulse1Connected = true;
		in.pulse2Connected = true;
		inst->Seed(seed);
		// The card waits 100ms for knob readings to settle before it
		// takes them; get past that so tests start from a live panel.
		Run(4800 + 10);
	}

	// Set to a frequency to record a steady tone instead of the arpeggio
	// (for measuring pitch).
	static inline float tone = 0.0f;

	// Source audio for Audio In 1/2: a plucked minor arpeggio with a noise
	// tick on each note -- plenty of transients to hear the slicing.
	static void Source(uint64_t n, int32_t &l, int32_t &r)
	{
		if (tone > 0.0f)
		{
			float t = (float)n / 48000.0f;
			int32_t v = (int32_t)(1000.0f * sinf(2.0f * 3.14159265f * tone * t));
			l = v;
			r = v;
			return;
		}
		static const float kNotes[] = {220.0f, 261.63f, 329.63f, 392.0f, 440.0f, 329.63f};
		uint64_t step = n / 8000; // 6 notes per second
		float f = kNotes[step % 6];
		float tt = (float)(n % 8000) / 48000.0f;
		float env = expf(-tt * 9.0f);
		float s = sinf(2.0f * 3.14159265f * f * tt) + 0.3f * sinf(2.0f * 3.14159265f * 2.0f * f * tt);
		uint32_t h = (uint32_t)(n * 2654435761u);
		float click = (n % 8000) < 200 ? ((float)(h >> 20) / 2048.0f - 1.0f) * 0.5f : 0.0f;
		l = (int32_t)((s * env + click) * 1100.0f);
		r = (int32_t)((s * env * 0.8f - click) * 1100.0f);
	}

	void Step()
	{
		Source(t, in.audio[0], in.audio[1]);
		in.pulse1Rise = pulse1;
		in.pulse2Rise = pulse2;
		pulse1 = pulse2 = false;
		inst->Audio(in, out);
		if (t % controlEvery == 0)
			inst->ControlTick(out.led);
		if (keep)
		{
			outL.push_back((int16_t)(out.audio[0] * 16));
			outR.push_back((int16_t)(out.audio[1] * 16));
		}
		t++;
	}

	void Run(uint64_t n)
	{
		for (uint64_t i = 0; i < n; i++)
			Step();
	}

	// Pulse In 1 and let a whole take record. (The card waits 20ms to
	// confirm a Pulse In edge before acting on it.)
	void Record()
	{
		pulse1 = true;
		Run(960 + imp::StereoBuffer::kFrames + 10);
	}
};
