// simulate.cpp — host-side harness for the physics engines.
//
// Compiles the REAL engines.cpp/fastmath.cpp natively and runs each mode across
// the knob range, reporting triggers/second per agent. This is how we check the
// models behave musically before burning a hardware trip: rates in the right
// ballpark, no mode silent, no mode machine-gunning, spook doing something.
//
//   g++ -O2 -I.. -o simulate simulate.cpp ../engines.cpp ../fastmath.cpp
//
// Not part of the firmware build.

#include <cstdio>
#include <cstring>
#include "biomimicry.h"
#include "engines.h"

using namespace bio;

static const char *kModeName[kNumModes] = {
	"Horses", "Geese", "Frogs", "Rain", "Meteors"
};

// Run one engine for `seconds` at the real control rate and report.
static void run(Engine &e, const char *label, int32_t physics, int32_t chaos,
                int population, double seconds, bool spookEvery = false)
{
	e.reset(0x1234u);

	Ctrl c;
	c.physics = physics;
	c.chaos = chaos;
	c.population = population;
	c.spook = false;
	c.clock = false;

	EngineOut out;
	int ticks = static_cast<int>(seconds * kCtrlRate);
	int fires[kNumAgents] = {0, 0, 0, 0};
	int simultaneous = 0;
	int64_t stateSum[kNumAgents] = {0, 0, 0, 0};

	for (int t = 0; t < ticks; t++)
	{
		// Optionally fire the spook input once per second.
		c.spook = spookEvery && (t % kCtrlRate == 0) && t > 0;

		memset(&out, 0, sizeof(out));
		e.tick(c, out);

		int n = 0;
		for (int i = 0; i < kNumAgents; i++)
		{
			if (out.triggers & (1 << i)) { fires[i]++; n++; }
			stateSum[i] += out.state[i];
		}
		if (n >= 2) simultaneous++;
	}

	printf("  %-28s ", label);
	for (int i = 0; i < kNumAgents; i++)
		printf("%6.2f ", fires[i] / seconds);
	printf(" | clusters/s %5.2f | state avg %5.2f\n",
	       simultaneous / seconds,
	       static_cast<double>(stateSum[0]) / ticks / 65536.0);
}

int main()
{
	HorsesEngine horses;
	GeeseEngine geese;
	FrogsEngine frogs;
	RainEngine rain;
	MeteorsEngine meteors;
	Engine *engines[kNumModes] = {&horses, &geese, &frogs, &rain, &meteors};

	const int32_t Q = kQ16One;

	printf("Trigger rates (per second, per agent), 4 agents, 30s runs\n");
	printf("%-30s %6s %6s %6s %6s\n", "", "ag0", "ag1", "ag2", "ag3");

	for (int m = 0; m < kNumModes; m++)
	{
		printf("\n%s\n", kModeName[m]);
		char buf[64];
		// Sweep the physics knob across its range.
		const int32_t pts[5] = {0, Q / 4, Q / 2, (3 * Q) / 4, Q - 1};
		const char *names[5] = {"physics 0.00", "physics 0.25", "physics 0.50",
		                        "physics 0.75", "physics 1.00"};
		for (int p = 0; p < 5; p++)
		{
			snprintf(buf, sizeof(buf), "%s", names[p]);
			run(*engines[m], buf, pts[p], 0, 4, 30.0);
		}
		// Chaos and spook checks at mid physics.
		run(*engines[m], "physics 0.50 chaos 1.00", Q / 2, Q - 1, 4, 30.0);
		run(*engines[m], "physics 0.50 + spook/sec", Q / 2, 0, 4, 30.0, true);
		// Population must gate agents off.
		run(*engines[m], "physics 0.50 population 2", Q / 2, 0, 2, 30.0);
	}
	return 0;
}
