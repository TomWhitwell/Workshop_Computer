#pragma once
// Runs the instrument on a computer: a pretend panel (knobs, switch, jacks)
// driven one sample at a time. Used by host_tests.cpp and render.cpp.
//
// Each sample runs core 0's Audio() and then core 1's ControlTick(), which
// does its work once every 48 samples, as on the hardware.
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "Temperance.h"

struct Sim
{
	std::unique_ptr<tem::Temperance> inst;
	tem::Inputs in{};
	tem::Outputs out{};
	int32_t led[6] = {};
	bool pulseIn[2] = {false, false}; // the level on Pulse in 1 / 2
	bool lastPulseIn[2] = {false, false};
	// What plays into Audio in 1 / 2, by sample. Unset: silence.
	std::function<int32_t(uint64_t)> source[2];
	std::vector<int16_t> outL, outR; // everything played, for renders
	bool keep = false;
	uint64_t t = 0;
	std::vector<uint64_t> pulses[2]; // each rising edge on Pulse out 1 / 2
	bool lastPulseOut[2] = {false, false};
	std::vector<uint64_t> loopStarts[2]; // each start of Loop A / B (playback, then every wrap)
	uint32_t lastPasses[2] = {0, 0};

	explicit Sim(int32_t main = 4095, int32_t x = 2389, int32_t y = 2389, int32_t sw = tem::kSwitchMiddle,
				 bool settle = true)
	{
		inst = std::make_unique<tem::Temperance>();
		in.knob[0] = main;
		in.knob[1] = x;
		in.knob[2] = y;
		in.sw = sw;
		// The card waits 100ms for knob readings to settle before it
		// takes them, then plays its boot chase; get past both so tests
		// start from a live panel.
		if (settle)
			RunMs(100 + 800);
	}

	void Step()
	{
		for (int i = 0; i < 2; i++)
		{
			if (source[i])
				in.audio[i] = source[i](t);
			in.rise[i] = pulseIn[i] && !lastPulseIn[i] && in.pulsePatched[i];
			lastPulseIn[i] = pulseIn[i];
		}
		inst->Audio(in, out);
		inst->ControlTick(led);
		for (int i = 0; i < 2; i++)
		{
			if (out.pulse[i] && !lastPulseOut[i])
				pulses[i].push_back(t);
			lastPulseOut[i] = out.pulse[i];
			uint32_t passes = inst->Loop(i).Passes();
			if (passes != lastPasses[i])
				loopStarts[i].push_back(t);
			lastPasses[i] = passes;
		}
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

	void RunMs(uint64_t ms) { Run(ms * 48); }

	// Switch down and back, as a finger does it: 30ms down.
	void Tap()
	{
		int32_t was = in.sw;
		in.sw = tem::kSwitchDown;
		RunMs(30);
		in.sw = was == tem::kSwitchDown ? tem::kSwitchMiddle : was;
		RunMs(5);
	}

	// A 5ms pulse into Pulse in `jack` (0 or 1), patched.
	void Pulse(int jack)
	{
		in.pulsePatched[jack] = true;
		pulseIn[jack] = true;
		RunMs(5);
		pulseIn[jack] = false;
		RunMs(5);
	}

	// Sweep knob `k` end to end (so it picks up whatever is stored), then
	// leave it at `v`.
	void Sweep(int k, int32_t v)
	{
		for (int32_t x = 0; x < 4095 + 64; x += 64)
		{
			in.knob[k] = x < 4095 ? x : 4095; // all the way to the end of travel
			RunMs(1);
		}
		in.knob[k] = v;
		RunMs(5);
	}

	// Set the Alt layer's three knobs (switch up, pick each up and set it),
	// then come back to the Play layer and pick its knobs up where they
	// were.
	void SetAlt(int32_t wash, int32_t dry, int32_t crush)
	{
		int32_t play[3] = {in.knob[0], in.knob[1], in.knob[2]};
		in.sw = tem::kSwitchUp;
		RunMs(5);
		int32_t want[3] = {wash, dry, crush};
		for (int k = 0; k < 3; k++)
			Sweep(k, want[k]);
		in.sw = tem::kSwitchMiddle;
		RunMs(5);
		for (int k = 0; k < 3; k++)
			Sweep(k, play[k]);
		RunMs(50);
	}
};
