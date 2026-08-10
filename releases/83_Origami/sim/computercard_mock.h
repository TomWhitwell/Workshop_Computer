// computercard_mock.h
//
// A host-side fake of the ComputerCard hardware layer, so the *real*
// src/vactrol.cpp can be compiled and run on a normal computer (Mac / Linux /
// Raspberry Pi SBC) to render audio you can listen to — no Workshop Computer
// hardware required.
//
// It implements the same public API as the real ComputerCard.h, but every
// jack/knob/LED is just a variable the renderer pokes. Include this BEFORE
// including ../src/vactrol.cpp.

#ifndef COMPUTERCARD_MOCK_H
#define COMPUTERCARD_MOCK_H

#include <cstdint>

// Make the real (Pico-only) ComputerCard.h self-skip when vactrol.cpp includes
// it: its guard is #ifndef COMPUTERCARD_H.
#define COMPUTERCARD_H
// Tell vactrol.cpp to omit its hardware main().
#define COMPUTERCARD_HOST_SIM
// The Pico "keep this function in RAM" attribute is a no-op on the host.
#define __not_in_flash_func(x) x

class ComputerCard
{
public:
	enum Knob   { Main, X, Y };
	enum Switch { Down, Middle, Up };
	enum Input  { Audio1, Audio2, CV1, CV2, Pulse1, Pulse2 };

	// ---- simulated hardware state, set each sample by the renderer ----
	int32_t  simKnob[3]     = { 2048, 2048, 2048 }; // 0..4095
	Switch   simSwitch      = Middle;
	int16_t  simAudioIn[2]  = { 0, 0 };             // -2048..2047
	int32_t  simCVIn[2]     = { 0, 0 };             // -2048..2047
	bool     simPulse[2]    = { false, false };
	bool     simPulsePrev[2]= { false, false };
	bool     simConnected[6]= { false, false, false, false, false, false };

	// ---- outputs captured from the card ----
	int16_t  simAudioOut[2] = { 0, 0 };
	int32_t  simCVOut[2]    = { 0, 0 };
	uint16_t simLed[6]      = { 0, 0, 0, 0, 0, 0 };

	virtual ~ComputerCard() {}
	virtual void ProcessSample() = 0;

	// Run one 48kHz frame: renderer sets inputs, we tick the card, then latch
	// the pulse history so RisingEdge/FallingEdge work next frame.
	void simStep()
	{
		ProcessSample();
		simPulsePrev[0] = simPulse[0];
		simPulsePrev[1] = simPulse[1];
	}

	// ---- API surface used by cards (matches ComputerCard.h signatures) ----
	int32_t KnobVal(Knob k) { return simKnob[k]; }
	Switch  SwitchVal()     { return simSwitch; }
	bool    SwitchChanged() { return false; }

	int16_t AudioIn(int i)  { return simAudioIn[i]; }
	int16_t AudioIn1()      { return simAudioIn[0]; }
	int16_t AudioIn2()      { return simAudioIn[1]; }
	int16_t CVIn(int i)     { return (int16_t)simCVIn[i]; }
	int16_t CVIn1()         { return (int16_t)simCVIn[0]; }
	int16_t CVIn2()         { return (int16_t)simCVIn[1]; }

	bool PulseIn(int i)            { return simPulse[i]; }
	bool PulseInRisingEdge(int i)  { return simPulse[i] && !simPulsePrev[i]; }
	bool PulseInFallingEdge(int i) { return !simPulse[i] && simPulsePrev[i]; }

	bool Connected(Input i)    { return simConnected[i]; }
	bool Disconnected(Input i)  { return !simConnected[i]; }

	void AudioOut(int i, int16_t v) { simAudioOut[i] = v; }
	void AudioOut1(int16_t v)       { simAudioOut[0] = v; }
	void AudioOut2(int16_t v)       { simAudioOut[1] = v; }
	void CVOut(int i, int16_t v)    { simCVOut[i] = v; }
	void CVOut1(int16_t v)          { simCVOut[0] = v; }
	void CVOut2(int16_t v)          { simCVOut[1] = v; }
	void LedBrightness(uint32_t i, uint16_t v) { simLed[i] = v; }
	void LedOn(uint32_t i, bool v = true)      { simLed[i] = v ? 4095 : 0; }
	void LedOff(uint32_t i)                    { simLed[i] = 0; }

	void EnableNormalisationProbe() {}
	void Run() {}
};

#endif // COMPUTERCARD_MOCK_H
