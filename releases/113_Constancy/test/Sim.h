#pragma once
// Runs the instrument on a computer: a pretend panel (knobs, switch, jacks)
// driven one sample at a time. Used by host_tests.cpp and render.cpp.
//
// Each sample runs core 0's Audio() and then core 1's ControlTick(), which
// does its work once every 48 samples, as on the hardware.
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Constancy.h"

struct Sim
{
	std::unique_ptr<constancy::Constancy> inst;
	constancy::Inputs in{};
	constancy::Outputs out{};
	int32_t led[6] = {};
	std::vector<int16_t> outL, outR; // everything played, for renders
	bool keep = false;
	bool pulse[2] = {false, false};
	uint64_t t = 0;

	explicit Sim(uint32_t seed = 1234, int32_t main = 2048, int32_t x = 2048, int32_t y = 2048,
				 int32_t sw = constancy::kSwitchMiddle)
	{
		inst = std::make_unique<constancy::Constancy>();
		in.knob[0] = main;
		in.knob[1] = x;
		in.knob[2] = y;
		in.sw = sw;
		// Both Pulse Ins start unpatched; sending a pulse patches one.
		in.pulseConnected[0] = false;
		in.pulseConnected[1] = false;
		inst->Seed(seed);
		// The card waits 100ms for knob readings to settle before it
		// takes them; get past that so tests start from a live panel.
		Run(4800 + 100);
	}

	void Step()
	{
		for (int i = 0; i < 2; i++)
		{
			in.pulseRise[i] = pulse[i];
			if (pulse[i])
				in.pulseConnected[i] = true;
		}
		pulse[0] = pulse[1] = false;
		inst->Audio(in, out);
		inst->ControlTick(led);
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

	const constancy::Control &control() const { return inst->GetControl(); }
};
