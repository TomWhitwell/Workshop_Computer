#pragma once
// Runs the instrument on a computer: a pretend panel (knobs, switch, jacks)
// driven one sample at a time. Used by host_tests.cpp and render.cpp.
//
// Each sample runs core 0's Audio() and then core 1's Poll(), which renders
// a block whenever the queue has room -- the same order of events as on the
// card, minus the timing.
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Abeyance.h"

struct Sim
{
	std::unique_ptr<ab::Abeyance> inst;
	ab::Inputs in{};
	ab::Outputs out{};
	int32_t led[6] = {};
	std::vector<int16_t> outL, outR; // everything played, for renders
	bool keep = false;
	bool pulse = false;
	uint64_t t = 0;

	explicit Sim(uint32_t seed = 1234, int32_t main = 2048, int32_t x = 2048, int32_t y = 2048)
	{
		static bool tablesReady = false;
		if (!tablesReady)
		{
			ab::Tables::Init();
			ab::Piano::Init();
			ab::Choir::Init();
			tablesReady = true;
		}
		inst = std::make_unique<ab::Abeyance>();
		in.knob[0] = main;
		in.knob[1] = x;
		in.knob[2] = y;
		in.sw = ab::kSwitchMiddle;
		in.pulseConnected = true;
		inst->Seed(seed);
		// The card waits 100ms for knob readings to settle before it
		// takes them; get past that so tests start from a live panel.
		RunMs(110);
	}

	void Step()
	{
		in.pulseRise = pulse;
		pulse = false;
		inst->Audio(in, out);
		inst->Poll(led);
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

	// Tap the switch down for 100ms, then back to middle.
	void Tap()
	{
		int32_t was = in.sw;
		in.sw = ab::kSwitchDown;
		RunMs(100);
		in.sw = was == ab::kSwitchDown ? ab::kSwitchMiddle : was;
		RunMs(50);
	}

	const ab::Control &control() const { return inst->GetControl(); }
};
