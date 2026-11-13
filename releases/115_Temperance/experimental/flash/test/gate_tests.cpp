// Gate regressions adapted from Matt Allison Temperance 0.2.0 (MIT).
// Flash integration and transition regressions, 2026-11-13. GPL-3.0 overall.
#include "GateSim.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace tem;
#define CHECK(x) assert(x)
template<class F> void Test(const char *name, F f) { f(); std::printf("PASS %s\n",name); }
static void Probe(Sim &s, int ms)
{
	for (int i = 0; i < ms * 48; i++)
	{
		s.pulseIn[0] = (s.t / 16) & 1;
		s.Step();
	}
}

static void GateTests()
{
	Test("gate: two quick flicks up switch the mode, and back", [] {
		Sim s;
		CHECK(!s.inst->Params().gate); // trigger at power-on
		s.FlickUp();
		CHECK(s.inst->Params().gate);
		s.FlickUp();
		CHECK(!s.inst->Params().gate);
		s.FlickUp(400); // the second flick 500ms after the first: too slow
		CHECK(!s.inst->Params().gate);
	});

	Test("gate: the mode shows for 1.5s, then a glow on empty loops", [] {
		Sim s;
		s.FlickUp();
		for (int i = 0; i < 6; i++)
			CHECK(s.led[i] == 4095); // gate: both columns
		s.RunMs(1500);
		CHECK(s.led[0] == 1000 && s.led[1] == 1000); // empty, waiting for a gate
		s.FlickUp();
		CHECK(s.led[0] == 4095 && s.led[1] == 4095); // trigger: the top row only
		CHECK(s.led[2] == 0 && s.led[3] == 0 && s.led[4] == 0 && s.led[5] == 0);
		s.RunMs(1500);
		CHECK(s.led[0] == 0 && s.led[1] == 0); // empty, trigger mode: off
	});

	Test("gate: a loop is as long as its gate, from its rise", [] {
		Sim s(4095, 3072);
		s.FlickUp();
		uint64_t edge = s.t;
		s.source[0] = [edge](uint64_t t) { return t >= edge + 4800 && t < edge + 4900 ? 2000 : 0; };
		s.Gate(0, 500);
		const Looper &l = s.inst->Loop(0);
		CHECK(l.State() == Looper::kPlaying);
		CHECK(l.Len() == 24000);
		// ADPCM is lossy: verify the encoded/decoded impulse rather than raw PCM.
        temflash::Request(0, 0, 4800, 1, l.Len(), true);
        for (int i = 0; i < 5; ++i) temflash::Service(nullptr);
        assert(temflash::Available(0, 0, 4800));
        assert(temflash::Read(0, 0, 4799) == 0);
        assert(temflash::Read(0, 0, 4850) > 1800);
		CHECK(s.inst->Loop(1).State() == Looper::kEmpty);
	});

	Test("gate: a short gate makes a short loop, not a clear", [] {
		Sim s;
		s.FlickUp();
		s.Gate(0, 100);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 4800);
		// Shorter than the 20ms wait: both edges still come through.
		s.Gate(1, 15);
		CHECK(s.inst->Loop(1).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(1).Len() == 720);
	});

	Test("gate: a gate too short to loop leaves it empty", [] {
		Sim s;
		s.FlickUp();
		s.Gate(0, 5); // 240 ticks at 48kHz; a loop needs 512
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
	});

	Test("gate: a gate over a playing loop records a new one", [] {
		Sim s;
		s.source[0] = [](uint64_t t) { return int(1000 * std::sin(t * 0.03)); };
		s.FlickUp();
		s.Gate(0, 500);
		s.Gate(0, 200);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 9600);
	});

	Test("gate: the switch still taps both loops", [] {
		Sim s;
		s.FlickUp();
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
		s.RunMs(465);
		s.Tap();
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(1).State() == Looper::kPlaying);
	});

	Test("gate: pulling the cable out of a high gate ends the recording there", [] {
		Sim s;
		s.FlickUp();
		s.in.pulsePatched[0] = true;
		s.pulseIn[0] = true;
		s.RunMs(300);
		Probe(s, 11); // pulled out: the probe's pattern, still "patched"
		s.in.pulsePatched[0] = false;
		Probe(s, 100); // declared empty; the probe carries on
		const Looper &l = s.inst->Loop(0);
		CHECK(l.State() == Looper::kPlaying);
		CHECK(std::abs(l.Len() - 14400) <= 32);
	});

	Test("gate: the probe on an empty jack never records, or stops the switch", [] {
		Sim s;
		s.FlickUp();
		Probe(s, 200);
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.in.sw = kSwitchDown;
		Probe(s, 30);
		s.in.sw = kSwitchMiddle;
		Probe(s, 500);
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
	});

	Test("gate: a gate already high when the mode changes waits for its next rise", [] {
		Sim s;
		s.in.pulsePatched[0] = true;
		s.pulseIn[0] = true;
		s.RunMs(50); // in trigger mode: that rise started a recording
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
		s.Tap(); // within 300ms of the pulse's start: a clear
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.FlickUp();
		s.RunMs(100);
		CHECK(s.inst->Loop(0).State() == Looper::kEmpty);
		s.pulseIn[0] = false;
		s.RunMs(50);
		s.Gate(0, 300);
		CHECK(s.inst->Loop(0).State() == Looper::kPlaying);
		CHECK(s.inst->Loop(0).Len() == 14400);
	});

	Test("gate: back in trigger mode, a pulse toggles again", [] {
		Sim s;
		s.FlickUp();
		s.FlickUp();
		s.Pulse(0);
		s.RunMs(20);
		CHECK(s.inst->Loop(0).State() == Looper::kRecording);
	});
}

static void TransitionTests() {
 for (unsigned gap : {1u, 5u, 19u, 20u, 21u}) {
  Sim s;
  s.in.sw=kSwitchUp;s.RunMs(100);
  s.in.sw=kSwitchMiddle;s.RunMs(100-gap);
  s.in.pulsePatched[0]=true;s.pulseIn[0]=true;s.RunMs(gap);
  s.in.sw=kSwitchUp;s.RunMs(2);
  // UI publishes mode at its next millisecond tick. A trigger already
  // delivered before that publication may legitimately have acted.
  int state=s.inst->Loop(0).State();
  if(gap<=5) assert(state==Looper::kEmpty);
  s.RunMs(28);
  assert(s.inst->Params().gate);
  // Mode change itself must not start/restart a recording already in flight.
  assert(s.inst->Loop(0).State()==state);
  s.pulseIn[0]=false;s.RunMs(30);
  if(s.inst->Loop(0).State()==Looper::kRecording) s.Tap();
  s.Gate(0,100);
  assert(s.inst->Loop(0).State()==Looper::kPlaying);
  assert(s.inst->Loop(0).Len()==4800);
 }
 Test("mode change drops old rises but accepts a new rise immediately after entry", [] {
  Sim s;s.in.sw=kSwitchUp;s.RunMs(100);s.in.sw=kSwitchMiddle;s.RunMs(100);
  s.in.sw=kSwitchUp;s.RunMs(2);assert(s.inst->Params().gate);
  s.Gate(0,100);
  assert(s.inst->Loop(0).State()==Looper::kPlaying && s.inst->Loop(0).Len()==4800);
 });
 Test("gate-to-trigger transition drops the pending old rise", [] {
  Sim s;s.FlickUp();s.RunMs(500);
  s.in.sw=kSwitchUp;s.RunMs(100);s.in.sw=kSwitchMiddle;s.RunMs(95);
  s.in.pulsePatched[0]=true;s.pulseIn[0]=true;s.RunMs(5);
  s.in.sw=kSwitchUp;s.RunMs(30);assert(!s.inst->Params().gate);
  assert(s.inst->Loop(0).State()==Looper::kEmpty);
  s.pulseIn[0]=false;s.RunMs(30);s.Pulse(0);s.RunMs(30);
  assert(s.inst->Loop(0).State()==Looper::kRecording);
  s.Tap();s.RunMs(30);
 });
 Test("switch takes ownership and an old gate fall cannot stop its recording", [] {
  Sim s;s.FlickUp();s.in.pulsePatched[0]=true;s.pulseIn[0]=true;s.RunMs(350);
  s.Tap();assert(s.inst->Loop(0).State()==Looper::kPlaying);
  s.RunMs(100);s.Tap();assert(s.inst->Loop(0).State()==Looper::kRecording);
  s.pulseIn[0]=false;s.RunMs(100);assert(s.inst->Loop(0).State()==Looper::kRecording);
  s.RunMs(300);s.Tap();
 });
 Test("gate takes over a switch recording, then its fall stops it", [] {
  Sim s;s.FlickUp();s.Tap();s.RunMs(100);s.Gate(0,100);
  assert(s.inst->Loop(0).State()==Looper::kPlaying);
  assert(s.inst->Loop(1).State()==Looper::kRecording);
  s.RunMs(300);s.Tap();s.RunMs(400);s.Tap();
 });
 Test("rapid short gates re-record into used flash without queue faults", [] {
  Sim s;s.FlickUp();
  for(int i=0;i<40;++i) {s.Gate(0,15,1);s.Gate(1,16,1);}
  s.RunMs(50);
  assert(s.inst->Loop(0).Len()==720 && s.inst->Loop(1).Len()==768);
  assert(temflash::Healthy());
 });
 Test("independent overlapping gates retain their own lengths", [] {
  Sim s;s.FlickUp();s.in.pulsePatched[0]=s.in.pulsePatched[1]=true;
  s.pulseIn[0]=true;s.RunMs(100);s.pulseIn[1]=true;s.RunMs(200);
  s.pulseIn[0]=false;s.RunMs(200);s.pulseIn[1]=false;s.RunMs(50);
  assert(s.inst->Loop(0).Len()==14400 && s.inst->Loop(1).Len()==19200);
 });
 std::puts("PASS mode-boundary windows at 1, 5, 19, 20 and 21ms");
}

int main(int argc, char **argv) {
 unsigned mb=argc>1?std::strtoul(argv[1],nullptr,10):16;
 assert(mb==2 || mb==16);
 temflash::TestFlashSize(mb*1024u*1024u);assert(temflash::Init());
 GateTests();
 TransitionTests();
 assert(temflash::Healthy());
 std::printf("All %uMB gate tests passed.\n",mb);
}
