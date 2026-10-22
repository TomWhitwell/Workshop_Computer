// HOST-TEST MOCK of ComputerCard (NOT the real library).
// Mirrors the protected API of ComputerCard v0.4.0 (third_party/ComputerCard_v0.4.0.h) for the
// functions this card uses, so qi_card.h can be compile-checked and run on a PC.
// Hardware behaviour (ADC smoothing, DAC, DMA, timing) is NOT modelled.
#pragma once
#include <stdint.h>
#define MOCK_COMPUTERCARD 1

#define DEBUG_1 0
static inline void gpio_put(int, int) {}

class ComputerCard
{
public:
	enum Knob {Main, X, Y};
	enum Switch {Down, Middle, Up};
	enum Input {Audio1, Audio2, CV1, CV2, Pulse1, Pulse2};
	ComputerCard() {}
	virtual ~ComputerCard() {}
	void Run() {}
	void EnableNormalisationProbe() {}
	// test access (not part of the real library)
	void MockStep() { ProcessSample(); }
	int32_t knobs_[3] = {0, 0, 0};
	Switch sw_ = Middle;
	int16_t ain_[2] = {0, 0};
	int16_t aout_[2] = {0, 0}, cvout_[2] = {0, 0};
	bool pout_[2] = {false, false};
	uint16_t led_[6] = {0, 0, 0, 0, 0, 0};
protected:
	virtual void ProcessSample() = 0;
	int32_t KnobVal(Knob k) { return knobs_[k]; }
	Switch SwitchVal() { return sw_; }
	bool SwitchChanged() { return false; }
	int16_t AudioIn1() { return ain_[0]; }
	int16_t AudioIn2() { return ain_[1]; }
	int16_t CVIn1() { return 0; }
	int16_t CVIn2() { return 0; }
	bool PulseIn1() { return false; }
	bool PulseIn2() { return false; }
	void AudioOut1(int16_t v) { aout_[0] = v; }
	void AudioOut2(int16_t v) { aout_[1] = v; }
	void CVOut1(int16_t v) { cvout_[0] = v; }
	void CVOut2(int16_t v) { cvout_[1] = v; }
	void PulseOut1(bool v) { pout_[0] = v; }
	void PulseOut2(bool v) { pout_[1] = v; }
	void LedBrightness(uint32_t i, uint16_t v) { led_[i] = v; }
	uint64_t UniqueCardID() const { return 0x0123456789abcdefULL; }
};
