// engine.h — the contract every LightPen mode implements.
//
// ControlTick runs at 1.5kHz, AudioTick every sample. Both are virtual calls.
// Unlike WorkshopBio, AudioTick is dispatched per sample too: each mode's
// per-sample work is small, so the indirect call is a rounding error in the
// 20.8us budget, and main.cpp stays one uniform loop. Re-measure before
// "optimising" this into a switch.

#pragma once
#include <cstdint>
#include "sensors.h"

namespace lp {

/// Inputs other than the three LDRs, read once per sample by main.cpp.
struct Inputs
{
	int32_t audio1;     // Audio In 1, -2048..2047
	bool    gate;       // Pulse In 1 level
	bool    gateRise;   // Pulse In 1 rising edge, this sample only
};

struct Ctrl
{
	int32_t main;        // Main knob, 0..4095
	bool    downHeld;    // debounced
	int     downTicks;   // how long it has been held, saturating at kHoldTicks
};

/// What a mode wants on the jacks. main.cpp owns the writes, so no mode can
/// touch CV Out 1 (the LDR supply).
struct EngineOut
{
	int16_t audio1  = 0;
	int16_t audio2  = 0;
	int32_t cv2     = 0;      // CVOut2Precise units; used while cv2Note < 0
	int16_t cv2Note = -1;     // >= 0: CV Out 2 carries this calibrated MIDI note
	bool    pulse1  = false;
	bool    pulse2  = false;
	uint8_t ledFlash = 0;     // non-zero: briefly light this many LEDs; main clears it
};

class Engine
{
public:
	/// On switching to this mode. `out` has just been reset to defaults.
	virtual void OnEnter() {}
	virtual void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) = 0;
	virtual void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) = 0;

	virtual void OnDownPress() {}
	/// How long the press lasted, in control ticks, saturating at kHoldTicks.
	/// Under kTapTicks it was a tap; at kHoldTicks it was a hold.
	virtual void OnDownRelease(int ticks) { (void)ticks; }
	virtual void OnDownHold() {}
};

/// Pulse length for triggers generated at control rate: 15 ticks = 10ms.
constexpr int kTrigTicks = 15;

/// 1V/oct on Audio In 1, in Q8 semitones per ADC unit. 9 is exact for the
/// documented +/-6V over +/-2048 (1V = 341.3 units). The input divider is
/// 1% resistors into a shared reference, so expect tens of cents per octave
/// of error until it is trimmed against a real keyboard/quantiser.
constexpr int32_t kSemisQ8PerUnit = 9;

} // namespace lp
