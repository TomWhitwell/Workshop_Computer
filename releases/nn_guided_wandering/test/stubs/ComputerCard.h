// Stand-in for ComputerCard.h, for testing main.cpp on a PC: the test sets
// the knobs, switch and inputs, and reads back the outputs and LEDs.
#pragma once
#include <stdint.h>
#include <string.h>

// The parts of the Pico SDK and TinyUSB that main.cpp calls
extern uint32_t gTimeUs;
extern int gUsbState;   // 0 DFP (host: 8mu or nothing), 1 UFP (a computer)
inline uint32_t time_us_32() {return gTimeUs;}
inline void sleep_us(uint64_t) {}
inline bool set_sys_clock_khz(uint32_t, bool) {return true;}
inline void multicore_launch_core1(void (*)()) {}
inline void board_init() {}
inline bool tuh_init(int) {return true;}
inline bool tud_init(int) {return true;}
inline void tud_task() {}
inline bool tud_mounted() {return true;}
inline uint32_t tud_midi_available() {return 0;}
inline uint32_t tud_midi_stream_read(void *, uint32_t) {return 0;}
// Everything main.cpp sends to the web editor lands here
extern uint8_t gSent[4096];
extern int gSentLen;
inline uint32_t tud_midi_stream_write(uint8_t, const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n && gSentLen < 4096; i++) gSent[gSentLen++] = p[i];
	return n;
}

class ComputerCard
{
public:
	enum Knob {Main, X, Y};
	enum Switch {Down, Middle, Up};
	enum USBPowerState_t {DFP, UFP, Unsupported};
	enum Input {Audio1, Audio2, CV1, CV2, Pulse1, Pulse2};

	virtual ~ComputerCard() {}
	virtual void ProcessSample() = 0;
	void Run() {}

	// Inputs, set by the test
	int32_t knob[3] = {2048, 2048, 2048};
	Switch sw = Middle;
	bool pulseIn1 = false, lastPulseIn1 = false;
	int16_t audioIn[2] = {};
	bool jack[2] = {};       // a jack in Audio In 1, 2
	bool probeEnabled = false;

	// Outputs, read by the test
	int32_t cvMv[2] = {};
	int16_t audioOut[2] = {};
	bool pulseOut[2] = {};
	int32_t led[6] = {};

	// One sample, as the audio interrupt would run it
	void Sample()
	{
		ProcessSample();
		lastPulseIn1 = pulseIn1;
	}

protected:
	int32_t KnobVal(Knob k) {return knob[k];}
	Switch SwitchVal() {return sw;}
	bool PulseIn1RisingEdge() {return pulseIn1 && !lastPulseIn1;}
	int16_t AudioIn1() {return audioIn[0];}
	int16_t AudioIn2() {return audioIn[1];}
	bool Connected(Input i) {return i <= Audio2 && probeEnabled && jack[i];}
	void EnableNormalisationProbe() {probeEnabled = true;}
	USBPowerState_t USBPowerState() {return USBPowerState_t(gUsbState);}
	bool CVOut1Millivolts(int32_t mv) {cvMv[0] = mv; return false;}
	bool CVOut2Millivolts(int32_t mv) {cvMv[1] = mv; return false;}
	void AudioOut1(int16_t v) {audioOut[0] = v;}
	void AudioOut2(int16_t v) {audioOut[1] = v;}
	void PulseOut1(bool v) {pulseOut[0] = v;}
	void PulseOut2(bool v) {pulseOut[1] = v;}
	void LedOn(uint32_t i, bool v = true) {led[i] = v ? 4095 : 0;}
	void LedBrightness(uint32_t i, uint16_t v) {led[i] = v;}
};
