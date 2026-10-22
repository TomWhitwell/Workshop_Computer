// Whole-card tests of Weights: main.cpp, run against stand-ins for
// the Computer, the 8mu and the USB stack (test/stubs), checking the knobs,
// jacks, LEDs, 8mu handling and the web editor's protocol end to end.
// Built by test/run_tests.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

uint32_t gTimeUs = 0;
int gUsbState = 0;
uint8_t gSent[4096];
int gSentLen = 0;

// Let the tests see inside the card
#define private public
#define main card_main
#include "main.cpp"
#undef main
#undef private

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t gSamples = 0;

// Run the card for n samples, with an optional clock at Pulse In 1 (period
// in samples, 0 for none), counting rising edges at Pulse Out 1 and 2
struct Counts {int rises[2]; int32_t cvMin[2], cvMax[2]; int audioMax[2]; int audioMin[2];};
static Counts Run(Weights &c, int n, int clockPeriod = 0)
{
	Counts k = {{0, 0}, {99999, 99999}, {-99999, -99999}, {0, 0}, {99999, 99999}};
	bool prev[2] = {c.pulseOut[0], c.pulseOut[1]};
	for (int i = 0; i < n; i++)
	{
		if (clockPeriod) c.pulseIn1 = (gSamples % uint64_t(clockPeriod)) < 48;
		c.Sample();
		gSamples++;
		gTimeUs = uint32_t((gSamples * 125) / 6);
		for (int v = 0; v < 2; v++)
		{
			if (c.pulseOut[v] && !prev[v]) k.rises[v]++;
			prev[v] = c.pulseOut[v];
			if (c.cvMv[v] < k.cvMin[v]) k.cvMin[v] = c.cvMv[v];
			if (c.cvMv[v] > k.cvMax[v]) k.cvMax[v] = c.cvMv[v];
			if (c.audioOut[v] > k.audioMax[v]) k.audioMax[v] = c.audioOut[v];
			if (c.audioOut[v] < k.audioMin[v]) k.audioMin[v] = c.audioOut[v];
		}
	}
	return k;
}

// Gates on every step (no rests, no ties, no ratchets) so steps can be
// counted at the pulse outputs
static void PlainGates(Weights &c)
{
	for (int p = 0; p < kPages; p++)
		for (int i = 0; i < 8; i++) c.engine.faders[p][i] = 0;
	c.engine.faders[Notes][0] = 100;
	c.engine.faders[Octaves][2] = 100;
	c.engine.faders[Gates][3] = 100;
	c.engine.faders[Ratchets][0] = 100;
}

static void TestStandalone()
{
	gUsbState = 0;
	Weights c;
	CHECK(c.hostMode, "host mode with nothing plugged in");
	// Defaults: plays at 120 BPM by itself, within range
	Counts k = Run(c, 48000 * 4);
	CHECK(k.rises[0] >= 4 && k.rises[0] <= 16, "voice 1: %d gates in 4s at 120 BPM (some rests and ratchets)", k.rises[0]);
	CHECK(k.rises[1] >= 4, "voice 2 plays too (%d)", k.rises[1]);
	for (int v = 0; v < 2; v++)
	{
		CHECK(k.cvMin[v] >= -2000 && k.cvMax[v] <= 5917, "voice %d pitch %d to %d mV", v, k.cvMin[v], k.cvMax[v]);
		CHECK(k.audioMin[v] >= 0 && k.audioMax[v] <= 1706 && k.audioMax[v] > 1000, "voice %d envelope %d to %d", v, k.audioMin[v], k.audioMax[v]);
	}
	PlainGates(c);
	Run(c, 24000);
	k = Run(c, 48000 * 4);
	CHECK(k.rises[0] == 8 && k.rises[1] == 8, "plain gates: %d %d steps in 4s", k.rises[0], k.rises[1]);
	CHECK(k.cvMin[0] == 0 && k.cvMax[0] == 0, "root at 0V");
}

static void TestKnobs()
{
	gUsbState = 0;
	Weights c;
	// Power-up, switch middle: the knobs take decay, attack and pluck as
	// they are; the other position keeps its defaults
	c.sw = ComputerCard::Middle;
	c.knob[0] = 2048; c.knob[1] = 0; c.knob[2] = 4095;
	Run(c, 64);
	CHECK(c.settings[Weights::SetAttack] == 0 && c.settings[Weights::SetPluck] == 4095, "middle knobs taken at power-up");
	CHECK(c.engine.scale == 0 && c.engine.ratio == kUnityRatio, "up position defaults: Major, x1");
	// Switch up: the knobs must pick up their new settings first
	c.sw = ComputerCard::Up;
	Run(c, 64);
	CHECK(c.engine.scale == 0, "scale not moved by a knob that hasn't picked up");
	CHECK(!c.knobLatched[2], "Main waiting");
	// LED 6 blinks fast while waiting
	int on = 0, off = 0;
	for (int i = 0; i < 200; i++) { Run(c, 32); if (c.led[5]) on++; else off++; }
	CHECK(on > 50 && off > 50, "LED 6 blinks while waiting (%d on, %d off)", on, off);
	// Turn Main down past the setting: picks up, then follows
	c.knob[0] = 100; Run(c, 64);
	CHECK(c.knobLatched[2] && c.engine.scale == 0, "picked up: scale 0 at the bottom dead zone (%d)", c.engine.scale);
	c.knob[0] = 4060; Run(c, 64);
	CHECK(c.engine.scale == kNumScales - 1, "top dead zone reaches the last scale (%d)", c.engine.scale);
	// Changing scale shows its number for a second on the Computer's LEDs
	int pattern = 0;
	for (int i = 0; i < 6; i++) if (c.led[i]) pattern |= 1 << i;
	CHECK(pattern == kNumScales, "scale number in binary: %d", pattern);
	Run(c, 48000 + 100);
	// X: clock ratio.  It picks up through the middle (x1), then x8 at the top.
	c.knob[1] = 2048; Run(c, 64);
	c.knob[1] = 4095; Run(c, 64);
	CHECK(c.engine.ratio == kNumRatios - 1, "X at the top: x8");
	PlainGates(c);
	Run(c, 48000);
	Counts k = Run(c, 48000);
	CHECK(k.rises[0] == 16, "x8 of 120 BPM: %d steps a second", k.rises[0]);
	c.knob[1] = 0; Run(c, 64);
	CHECK(c.engine.ratio == 0, "X at the bottom: /8");
	// Y: slew, two-sided: left of centre ties only, centre off, right all
	c.knob[2] = 4095; Run(c, 64);
	c.knob[2] = 60; Run(c, 64);
	CHECK(c.engine.slewAlpha < (1 << 24) && c.engine.slewTiesOnly, "Y left: slew on ties only");
	c.knob[2] = 2048; Run(c, 64);
	CHECK(c.engine.slewAlpha == 1 << 24, "Y centre: slew off");
	c.knob[2] = 3000; Run(c, 64);
	CHECK(c.engine.slewAlpha < (1 << 24) && !c.engine.slewTiesOnly, "Y right: slew on every note");
	// Back to middle: Main (at 4060, decay 2048) and Y (at 3000, pluck
	// 4095) wait for their knobs again; X (at 0, attack 0) is already there
	c.sw = ComputerCard::Middle;
	Run(c, 64);
	CHECK(c.knobLatched[0] && !c.knobLatched[1] && !c.knobLatched[2], "middle knobs waiting");
	// Switch down keeps X and Y on the middle settings
	c.sw = ComputerCard::Down;
	Run(c, 64);
	CHECK(c.knobBank == 1, "switch down keeps the last position's settings");
	c.sw = ComputerCard::Middle;
	Run(c, 64);
	// Decay at the ends
	c.knob[0] = 2048; Run(c, 64);
	CHECK(c.knobLatched[2], "Main picked up decay");
	c.knob[0] = 4095; Run(c, 64);
	CHECK(c.engine.decayInc == DecayFromKnob(4095), "decay at the top: 4s");
}

// Switch held down: Main is the internal tempo, with pickup
static void TestTempoKnob()
{
	gUsbState = 0;
	Weights c;
	c.sw = ComputerCard::Middle;
	c.knob[0] = 2048;
	Run(c, 64);
	int32_t decay = c.settings[Weights::SetDecay];
	PlainGates(c);
	// Hold the switch down: Main (at the middle, ~110 BPM) must reach 120
	// BPM's position before it changes anything
	c.sw = ComputerCard::Down;
	Run(c, 64);
	CHECK(c.mainOnTempo && !c.knobLatched[2] && c.tempo == 120, "tempo waits for Main");
	c.knob[0] = 2400; Run(c, 64);   // past 120 BPM's position (~2280 raw)
	CHECK(c.knobLatched[2], "Main picked up the tempo");
	c.knob[0] = 4095; Run(c, 64);
	CHECK(c.tempo == 300 && c.engine.internalPeriod == 9600, "Main fully up: 300 BPM (%d)", int(c.tempo));
	c.knob[0] = 0; Run(c, 64);
	CHECK(c.tempo == 40, "Main fully down: 40 BPM (%d)", int(c.tempo));
	CHECK(c.settings[Weights::SetDecay] == decay, "decay untouched while the switch is down");
	// While held, the top LEDs flash on the steps
	int on = 0, off = 0;
	for (int i = 0; i < 200; i++) { Run(c, 480); if (c.led[0]) on++; else off++; }
	CHECK(on > 20 && off > 20, "LEDs flash on the beat (%d on, %d off)", on, off);
	// Steps at 40 BPM
	Counts k = Run(c, 48000 * 6);
	CHECK(k.rises[0] == 4, "40 BPM: %d steps in 6s", k.rises[0]);
	// Let go: Main must pick up the decay again, and the tempo stays
	c.sw = ComputerCard::Middle;
	Run(c, 64);
	CHECK(!c.mainOnTempo && !c.knobLatched[2] && c.tempo == 40, "Main waits for decay; tempo kept");
	CHECK(c.settings[Weights::SetDecay] == decay, "decay not jumped to 0");
	c.BuildStatus();
	CHECK(((c.status[0] >> 4) & 1) == 0, "status: switch not down");
	// The web editor's tempo: the knob must pick it up afresh
	c.sw = ComputerCard::Down;
	c.knob[0] = 0; Run(c, 64);
	uint8_t t[2] = {0, 90};
	c.OnSysEx(sysex::Tempo, t, 2);
	Run(c, 64);
	CHECK(c.tempo == 90 && !c.knobLatched[2], "web tempo kept until Main reaches it");
	c.BuildStatus();
	CHECK(((c.status[0] >> 4) & 1) == 1, "status: switch down");
	c.sw = ComputerCard::Middle;
}

// Audio In 1 and 2: with a jack in, Audio Out is the input through the
// envelope; without, the envelope itself
static void TestVca()
{
	gUsbState = 0;
	Weights c;
	PlainGates(c);
	CHECK(c.probeEnabled, "normalisation probe on");
	// Nothing plugged in: the envelopes
	Counts k = Run(c, 48000);
	CHECK(k.audioMax[0] > 1650 && k.audioMin[0] >= 0, "no jack: envelope (%d to %d)", k.audioMin[0], k.audioMax[0]);
	// +1000 into Audio In 1, -800 into Audio In 2
	c.jack[0] = c.jack[1] = true;
	c.audioIn[0] = 1000;
	c.audioIn[1] = -800;
	k = Run(c, 48000);
	CHECK(k.audioMax[0] >= 990 && k.audioMax[0] <= 1000 && k.audioMin[0] >= 0, "VCA 1: 0 to the input at the peak (%d to %d)", k.audioMin[0], k.audioMax[0]);
	CHECK(k.audioMin[1] <= -790 && k.audioMin[1] >= -800 && k.audioMax[1] <= 0, "VCA 2: negative input (%d to %d)", k.audioMin[1], k.audioMax[1]);
	// it follows the envelope: silent where the envelope is
	bool follows = true;
	for (int i = 0; i < 4800; i++)
	{
		Run(c, 1);
		int expect = (1000 * c.engine.level[0]) >> 15;
		if (c.audioOut[0] != expect) follows = false;
	}
	CHECK(follows, "Audio Out 1 = input x envelope");
	// DEPTH scales it too
	c.engine.faders[FirstPage(Depth)][3] = 100;   // half depth
	Run(c, 48000);
	k = Run(c, 48000);
	CHECK(k.audioMax[0] >= 490 && k.audioMax[0] <= 500, "half depth halves it (%d)", k.audioMax[0]);
	// Unplug 1: back to its envelope; 2 still a VCA
	c.jack[0] = false;
	k = Run(c, 48000);
	CHECK(k.audioMax[0] > 800 && k.audioMax[0] <= 853, "unplugged: half-depth envelope again (%d)", k.audioMax[0]);
	CHECK(k.audioMax[1] <= 0, "Audio In 2 still enveloped");
	c.BuildStatus();
	CHECK((c.status[0] & 96) == 64, "status: jack in Audio In 2 only (%d)", c.status[0]);
}

static void TestClock()
{
	gUsbState = 0;
	Weights c;
	PlainGates(c);
	// A clock at Pulse In 1, 10 a second
	Counts k = Run(c, 48000, 4800);
	CHECK(c.engine.clocked, "following the clock");
	k = Run(c, 48000, 4800);
	CHECK(k.rises[0] == 10, "a step a clock: %d", k.rises[0]);
	// Unplugged: back to 120 BPM after 2s
	Run(c, 48000 * 2 + 100);
	CHECK(!c.engine.clocked, "back to the internal clock");
	k = Run(c, 48000 * 2);
	CHECK(k.rises[0] == 4, "120 BPM again: %d", k.rises[0]);
}

static void TestEightMu()
{
	gUsbState = 0;
	Weights c;
	EightMU &mu = c.mu;
	mu.connected = true;
	for (int i = 0; i < 8; i++) mu.fader[i] = 0;
	// For a second after connecting, nothing is read
	mu.button[0] = true;
	Run(c, 32 * 1000);
	CHECK(c.ui.page == 0, "button ignored while connecting");
	mu.button[0] = false;
	Run(c, 32 * 600);
	// A: NOTES 2, ATTACK, ATTACK 2, NOTES; B: OCTAVES; D: RATCHETS 2, DEPTH ...
	auto press = [&](int b) { mu.button[b] = true; Run(c, 32 * 5); mu.button[b] = false; Run(c, 32 * 5); };
	press(0);
	CHECK(c.ui.page == 4, "A pressed again: NOTES 2 (%d)", c.ui.page);
	press(0);
	CHECK(c.ui.page == 8, "and again: ATTACK");
	press(0);
	CHECK(c.ui.page == 12, "and again: ATTACK 2");
	press(0);
	CHECK(c.ui.page == 0, "and back to NOTES");
	press(1);
	CHECK(c.ui.page == 1, "B: OCTAVES");
	press(3); press(3);
	CHECK(c.ui.page == 7, "D twice: RATCHETS 2");
	press(3);
	CHECK(c.ui.page == 11, "D again: DEPTH");
	press(3);
	CHECK(c.ui.page == 15, "D again: DEPTH 2");
	press(3); press(3);
	// Computer LEDs: D's LED blinks for a second page
	int on = 0;
	for (int i = 0; i < 100; i++) { Run(c, 32 * 10); if (c.led[3]) on++; }
	CHECK(on > 20 && on < 80, "second page blinks (%d of 100)", on);
	// Faders pick up: NOTES fader 1 is stored at 127
	press(0);  // from RATCHETS 2, A gives NOTES
	CHECK(c.ui.page == 0, "NOTES");
	mu.fader[0] = 60 << 5; Run(c, 64);
	CHECK(c.engine.faders[Notes][0] == 127, "not picked up yet");
	mu.fader[0] = 127 << 5; Run(c, 64);
	mu.fader[0] = 30 << 5; Run(c, 64);
	CHECK(c.engine.faders[Notes][0] == 30, "picked up, then follows (%d)", c.engine.faders[Notes][0]);
	// LEDs: probabilities just after the touch; after 3s, the playing note
	int32_t m = 0;
	for (int i = 0; i < 8; i++) if (mu.led[i] > m) m = mu.led[i];
	CHECK(m == 4095, "the likeliest is fully lit");
	CHECK(mu.led[7] == 0, "Major: fader 8 dark");
	Run(c, 48000 * 3 + 100);
	int lit = 0, full = -1;
	for (int i = 0; i < 8; i++) { if (mu.led[i]) lit++; if (mu.led[i] == 4095) full = i; }
	CHECK(full == c.engine.voice[0].noteFader, "playing note %d lit (%d)", c.engine.voice[0].noteFader, full);
	CHECK(lit <= 2, "only the two voices' notes lit (%d)", lit);
	// Unplugged and back: pickup again
	mu.connected = false; Run(c, 64);
	mu.connected = true; mu.fader[0] = 0; Run(c, 32 * 1600);
	CHECK(!c.ui.Latched(0), "after reconnecting, faders pick up again");
}

static int FindSent(uint8_t cmd)
{
	for (int i = 0; i + 3 < gSentLen; i++)
		if (gSent[i] == 0xF0 && gSent[i + 1] == 0x7D && gSent[i + 2] == 0x47 && gSent[i + 3] == cmd) return i;
	return -1;
}

static void TestWebEditor()
{
	gUsbState = 1;
	Weights c;
	CHECK(!c.hostMode, "device mode with a computer plugged in");
	Run(c, 64);
	// HELLO: the card replies with its state
	gSentLen = 0;
	c.OnSysEx(sysex::Hello, nullptr, 0);
	int s = FindSent(sysex::State);
	CHECK(s == 0 && gSentLen == sysex::kStateLen, "STATE sent (%d bytes)", gSentLen);
	CHECK(gSent[4] == sysex::kVersion && sysex::Get14(gSent + 6) == 120, "version and tempo");
	CHECK(gSent[8] == 127 && gSent[8 + 8 * 4] == 0 && gSent[8 + 8 * 15 + 7] == 0, "values: NOTES fader 1 127, NOTES 2 fader 1 0, DEPTH 2 fader 8 0");
	CHECK(gSent[gSentLen - 1] == 0xF7, "ends F7");
	for (int i = 1; i < gSentLen - 1; i++) CHECK(gSent[i] < 0x80, "7-bit byte %d", i);
	// SET, with bad ones ignored
	uint8_t set[3] = {2, 3, 99};
	c.OnSysEx(sysex::Set, set, 3);
	CHECK(c.engine.faders[2][3] == 99, "SET");
	uint8_t bad[3] = {8, 0, 50}, bad2[3] = {0, 8, 50};
	c.OnSysEx(sysex::Set, bad, 3);
	c.OnSysEx(sysex::Set, bad2, 3);
	c.OnSysEx(sysex::Set, set, 2);
	CHECK(c.engine.faders[0][0] == 127, "bad SETs ignored");
	// SET_ALL: every value and the tempo; the wrong version is ignored
	uint8_t all[3 + sysex::kNumValues];
	all[0] = sysex::kVersion + 1; all[1] = 0; all[2] = 60;
	for (int i = 0; i < sysex::kNumValues; i++) all[3 + i] = uint8_t(i);
	c.OnSysEx(sysex::SetAll, all, sizeof(all));
	CHECK(c.engine.faders[7][7] == 0 && c.engine.faders[15][7] == 0 && c.tempo == 120, "wrong version ignored");
	all[0] = sysex::kVersion;
	c.OnSysEx(sysex::SetAll, all, sizeof(all));
	Run(c, 64);
	CHECK(c.engine.faders[7][7] == 63 && c.engine.faders[1][0] == 8 && c.engine.faders[15][7] == 127 && c.tempo == 60, "SET_ALL");
	CHECK(c.engine.internalPeriod == 48000, "tempo 60 BPM");
	// TEMPO, clamped
	uint8_t t[2] = {0, 5};
	c.OnSysEx(sysex::Tempo, t, 2);
	Run(c, 64);
	CHECK(c.tempo == 40, "tempo clamped to 40 (%d)", int(c.tempo));
	t[0] = 1; t[1] = 52;   // 180
	c.OnSysEx(sysex::Tempo, t, 2);
	Run(c, 64);
	CHECK(c.tempo == 180, "TEMPO");
	// PAGE, on the Computer's LEDs
	uint8_t pg = 6;
	c.OnSysEx(sysex::Page, &pg, 1);
	Run(c, 64);
	CHECK(c.ui.page == 6, "PAGE");
	// RESET: the defaults, and the state again
	gSentLen = 0;
	c.OnSysEx(sysex::Reset, nullptr, 0);
	CHECK(c.engine.faders[0][0] == 127 && c.engine.faders[7][7] == 0 && c.engine.faders[15][7] == 0, "RESET");
	CHECK(FindSent(sysex::State) == 0, "RESET replies with STATE");
	// PING links the editor: the Computer's LEDs show the page
	c.OnSysEx(sysex::Ping, nullptr, 0);
	CHECK(c.WebLinked(), "linked after a ping");
	Run(c, 2000);
	int ledsOn = 0;
	for (int i = 0; i < 4; i++) if (c.led[i]) ledsOn |= 1 << i;
	CHECK(ledsOn == 0 || ledsOn == 4, "page C's LED (blinking for GATES 2): %d", ledsOn);
	Run(c, 48000 * 4);
	CHECK(!c.WebLinked(), "unlinked once pings stop");
	// STATUS
	c.OnSysEx(sysex::Ping, nullptr, 0);
	PlainGates(c);
	c.engine.faders[kPairs + Notes][4] = 100;
	Run(c, 48000);
	uint8_t st[sysex::kStatusLen];
	int n = c.EncodeStatus(st);
	CHECK(n == sysex::kStatusLen, "STATUS length %d", n);
	CHECK(st[0] == 0xF0 && st[3] == sysex::Status && st[n - 1] == 0xF7, "STATUS framing");
	for (int i = 1; i < n - 1; i++) CHECK(st[i] < 0x80, "STATUS 7-bit byte %d", i);
	const uint8_t *p = st + 4;
	CHECK(p[2] == c.engine.scale && p[3] == c.engine.ratio, "scale and ratio");
	CHECK(((p[1] >> 3) & 15) == 14, "voice 2 shares all but NOTES (%d)", (p[1] >> 3) & 15);
	int ms = (p[8] << 7) | p[9];
	CHECK(ms == 333, "step length at 180 BPM: %d ms", ms);
	const int V = sysex::kVoiceLen;
	CHECK(p[12] == 0 && p[12 + V] == 4, "note faders: voice 1 on 1, voice 2 on 5 (%d %d)", p[12], p[12 + V]);
	int mv2 = ((p[12 + V + 8] << 7) | p[12 + V + 9]) - 8192;
	CHECK(mv2 == c.engine.cvMv[1], "voice 2 pitch %d mV", mv2);
	CHECK(((p[12 + 2 * V] << 7) | p[13 + 2 * V]) == 180, "tempo in STATUS");
	CHECK(p[12 + 10] == 8 && p[12 + 13] == 8, "envelope pages off: 8");
	c.engine.faders[FirstPage(Depth)][2] = 100;
	Run(c, 48000);
	c.EncodeStatus(st);
	CHECK(p[12 + 13] == 2 && p[12 + V + 13] == 2, "DEPTH fader 3 for both voices (%d %d)", p[12 + 13], p[12 + V + 13]);
}

int main()
{
	TestStandalone();
	TestKnobs();
	TestTempoKnob();
	TestVca();
	TestClock();
	TestEightMu();
	TestWebEditor();
	printf("card: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
