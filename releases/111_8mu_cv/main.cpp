// 8mu CV
//
// A playable, unpredictable instrument built around the Music Thing Modular
// 8mu: every control it has - its eight faders, four top buttons, and the
// accelerometer - is put to work driving the Workshop Computer's outputs.
// With the LFOs running and the accelerometer latched in, a tilt of the
// controller steers the sound somewhere you did not plan, which is the point.
// It doubles as a way to test an 8mu channel by channel.  It is meant to be
// used WITH the 8mu; unplug it and the outputs simply hold their last values.
//
// The eight faders become four control voltages and two pulse streams:
//
//   fader 1  ->  Audio Out 1   (bipolar voltage, centre is 0V)
//   fader 2  ->  Audio Out 2   (bipolar voltage, centre is 0V)
//   fader 3  ->  CV Out 1      (bipolar voltage, centre is 0V)
//   fader 4  ->  CV Out 2      (bipolar voltage, centre is 0V)
//   fader 5  ->  Pulse Out 1   rate, 0.1Hz to 20Hz
//   fader 6  ->  Pulse Out 2   rate, 0.1Hz to 20Hz
//   fader 7  ->  Pulse Out 1   width, centre is a 50% square
//   fader 8  ->  Pulse Out 2   width, centre is a 50% square
//
// The audio outputs are DC-coupled on this hardware, so they carry a steady
// voltage just as well as sound does.  That gives four independent voltage
// sources rather than the usual two CV outputs.  All four are uncalibrated,
// which is fine here: this is a source of control voltages, not a pitch
// reference, and the two audio outs could never be calibrated anyway.  The
// useful range is roughly -5V to +5V, with the middle of a fader at 0V.
//
// THE COMPUTER'S SWITCH PICKS THE MODE.
//
//   switch middle  BASIC   faders 1-4 are steady voltages, 5-8 pulse rate/width
//   switch up      LFO     armed outputs oscillate (see below)
//   switch down    -       held half a second, it latches the ACCELEROMETER
//                          on or off (the four gesture axes then replace the
//                          four voltage faders)
//
// ACCELEROMETER.  Holding Down for half a second flips the card between the
// faders and the accelerometer.  When the accelerometer is latched on, the
// four gesture axes take over the four voltage outputs and their LFO rates:
//
//   Pitch -> V1   bipolar   back lifted positive
//   Roll  -> V2   bipolar   right lifted positive
//   Yaw   -> V3   bipolar   clockwise positive (a rate of turn)
//   Flip  -> V4   unipolar  flat = 0, upside-down = max
//
// In BASIC the axis sets a steady voltage; in LFO mode an armed output's axis
// sets its rate instead (a bipolar axis rests at mid rate, Flip rests at the
// slowest rate).  The X knob scales how much the gestures respond, and the Y
// knob smooths them.
//
// Any of the four voltage outputs can be a triangle LFO instead of a steady
// voltage.  A short press on one of the 8mu's four top buttons arms that
// output; it oscillates whenever the switch is Up.  Arming captures the
// voltage it is already sitting at as the centre to swing around, and while
// it is oscillating the fader that set the level instead sets the speed.
//
//   button 1 (C2)  ->  Audio Out 1   LFO arm
//   button 2 (C3)  ->  Audio Out 2   LFO arm
//   button 3 (C4)  ->  CV Out 1      LFO arm
//   button 4 (C5)  ->  CV Out 2      LFO arm
//
// A fader (or gesture axis) whose output is NOT armed is still a level even in
// LFO mode, so the card stays useful with only one LFO running - or none.
//
// The Main knob sets how far the triangle swings: fully anticlockwise is almost
// no swing (the pots do not quite reach zero, so a bare trace remains), fully
// clockwise is the whole distance to the nearer supply rail.  A swing never
// reaches past that rail, so an LFO can never clip however the centre and
// depth are set.  Main only affects armed outputs.
//
// Switching an LFO off - by pressing its button again, or by dropping back to
// BASIC - glides the output to its centre and leaves the level held until the
// fader is moved to meet it, so nothing jumps.
//
// Holding any one of the four buttons for a second and a half disarms all of
// them at once: the quick way to stop everything.
//
// The 8mu is used purely as a MIDI source: this card never talks to its LEDs.
// Plug it into the Computer's front USB-C jack, which needs a Rev 1.1+ board
// and nothing else in that socket.  The Computer is the USB host.

#include "ComputerCard.h"
#include "EightMU.h"
#include "hardware/clocks.h"

#include <math.h>
#include <stdint.h>


class EightMUCVCard : public ComputerCard
{
public:
	EightMUCVCard()
	{
		// Work out the pulse-rate and gesture-sensitivity tables once, before
		// audio starts.  This is the only floating point in the card, and it
		// runs a single time at power-on, so it costs nothing in the audio
		// path.
		InitRateTable();
		InitSensTable();

		// A USB device needs to be powered before it will enumerate, and the
		// Computer's host port takes a moment to settle.  Only start the USB
		// stack if the port really is in host mode; on older revisions, or
		// with a cable plugged into a computer, USBPowerState() says so and
		// there is nothing to do.
		sleep_us(150000);
		if (USBPowerState() == DFP)
		{
			mu.Start();   // claims core 1 and runs the USB host stack there
		}
	}

	virtual void ProcessSample()
	{
		// --- the eight parameters, from the 8mu and nowhere else ------------
		//
		// There is deliberately no panel fallback: this card is a test bed
		// for the 8mu, so with no controller attached the outputs hold their
		// last values rather than being driven by three knobs.
		if (mu.Connected())
		{
			for (int i = 0; i < 8; i++)
			{
				param[i] = mu.Fader(i);
			}
		}

		// --- mode, from the Computer's switch -------------------------------
		//
		// Middle and Down are the basic card; Up is the LFO card.
		//
		// Holding Down for half a second latches the accelerometer on or off.
		// It is a momentary position, so the gesture is timed: a short press
		// does nothing, and a deliberate hold flips the source for the four
		// voltage outputs between their faders and the four gesture axes.
		const Switch sw = SwitchVal();
		const bool lfoMode = (sw == Switch::Up);

		if (sw == Switch::Down)
		{
			if (downHeld < kGestureHoldSamples)
			{
				downHeld++;
				if (downHeld == kGestureHoldSamples)
				{
					accelOn = !accelOn;
					blinkRemaining = kBlinkSamples;
				}
			}
		}
		else
		{
			downHeld = 0;
		}

		// --- the four top buttons arm the LFOs ------------------------------
		//
		// A short tap arms or disarms that output.  A long hold on any one
		// button disarms all four at once.
		//
		// A button held down and then unplugged must NOT read as a tap when
		// the connection drops: EightMU clears its button states on
		// disconnect, and a release edge there would flip the arming as a side
		// effect of pulling the cable.  So edges are only acted on while a
		// controller is actually connected.
		if (mu.Connected())
		{
			for (int i = 0; i < 4; i++)
			{
				const bool down = mu.Button(i);

				if (down)
				{
					if (heldSamples[i] < kLongHoldSamples) heldSamples[i]++;
					else if (!longFired[i])
					{
						// Held long enough: disarm everything.  Only fires once
						// per hold.
						for (int j = 0; j < 4; j++) SetArmed(j, false);
						longFired[i] = true;
					}
				}
				else if (prevDown[i] && !longFired[i])
				{
					// Released before the long-hold threshold: a normal tap.
					SetArmed(i, !lfoArmed[i]);
				}

				if (down) prevDown[i] = true;
				else { prevDown[i] = false; heldSamples[i] = 0; longFired[i] = false; }
			}
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				prevDown[i] = false;
				heldSamples[i] = 0;
				longFired[i] = false;
			}
		}

		// --- the knobs ------------------------------------------------------
		//
		// Main is LFO depth (0..full headroom), X scales how far the
		// accelerometer gestures respond, and Y smooths them.  All three are
		// read here, in the audio context, and used below.
		const int32_t depth = KnobVal(Knob::Main) + 1;   // 1..4096, so full = full headroom
		const int32_t sens  = KnobVal(Knob::X) >> 5;     // 0..127, into the sensitivity table
		const int32_t slew  = KnobVal(Knob::Y);          // 0..4095, into a smoothing shift

		// --- the four gesture axes, if the accelerometer is latched on ------
		//
		// The 8mu already smooths its accelerometer heavily, but the Y knob
		// adds a little more so a shaky hand does not judder the output.  The
		// axes come back as signed differences in roughly the -2032..2032
		// range of the audio and CV jacks; only Flip is made unipolar (the
		// 8mu's "which way up" axis is -2032 flat and +2032 upside down, so
		// it is shifted up so flat reads 0).
		//
		// EightMU keeps its axis values after a disconnect (it only clears the
		// buttons), so reading them unconditionally gives "hold last value" for
		// free when the controller is unplugged.
		int32_t gest[4];
		if (accelOn)
		{
			gest[0] = mu.Pitch();
			gest[1] = mu.Roll();
			gest[2] = mu.Yaw();
			gest[3] = mu.Flip() + 2032;   // flat -> 0, upside-down -> 4064 (clamped later)
		}
		else
		{
			for (int i = 0; i < 4; i++) gest[i] = 0;
		}

		// --- four voltages, steady or oscillating ---------------------------
		//
		// An output oscillates only when it is armed AND the switch is Up.
		// Otherwise it is a steady level.  The level (and, for an armed
		// output in LFO mode, the rate) comes from either its fader or its
		// gesture axis, depending on whether the accelerometer is latched on.
		//
		// The target is slewed gently throughout, which hides the steps in
		// the 7-bit MIDI fader, glides every switch-on and switch-off rather
		// than jumping, and rounds the corners off the triangle.  At the top
		// of the speed range that rounding is a fair fraction of a cycle, so
		// a fast LFO comes out closer to a sine than a triangle - a mellowing
		// that suits modulation sources, and the price of not clicking.
		for (int i = 0; i < 4; i++)
		{
			const bool active = lfoMode && lfoArmed[i];

			int32_t target;
			int32_t rateIndex;

			if (accelOn)
			{
				// --- gesture path -------------------------------------------------
				//
				// The axis is scaled by the X knob, smoothed by the Y knob, and
				// turned into both a voltage and a rate.  A bipolar axis (0..2)
				// rests at 0V and mid rate; Flip (3) is unipolar, so a flat 8mu
				// reads 0V and the slowest rate.
				const int32_t raw = (gest[i] * sensInc[sens]) >> 8;
				gestSmooth[i] += (raw - gestSmooth[i]) >> GestSlewShift(slew);
				const int32_t sm = gestSmooth[i];

				int32_t level;
				if (i == 3) level = sm >> 1;          // 0..2032, flat = 0V
				else        level = sm;              // bipolar
				if (level < -2048) level = -2048;
				if (level > 2047) level = 2047;

				if (i == 3) rateIndex = sm >> 5;      // 0..127, flat = slowest
				else        rateIndex = (sm + 2048) >> 5;  // 0..127, rest = mid
				if (rateIndex < 0) rateIndex = 0;
				if (rateIndex > 127) rateIndex = 127;

				// No pick-up lock for gestures: the axis is position-based, so
				// the same tilt is the level in BASIC and the rate in LFO, and
				// dropping out of LFO simply follows the tilt again.
				levelLocked[i] = false;

				if (active)
				{
					lfoPhase[i] += rateInc[rateIndex];
				}
				else
				{
					target = level;
				}
			}
			else
			{
				// --- fader path (as before) --------------------------------------
				if (!active && prevActive[i])
				{
					levelLocked[i] = true;
					pickupPos[i] = ClampFader(centre[i] + 2048);
				}
				rateIndex = param[i] >> 5;

				if (active)
				{
					lfoPhase[i] += rateInc[rateIndex];
				}
				else if (levelLocked[i])
				{
					target = centre[i];
					if ((prevParam[i] < pickupPos[i] && param[i] >= pickupPos[i]) ||
						(prevParam[i] > pickupPos[i] && param[i] <= pickupPos[i]) ||
						param[i] == pickupPos[i])
					{
						levelLocked[i] = false;
					}
				}
				else
				{
					target = param[i] - 2048;
				}
				prevParam[i] = param[i];
			}

			if (active)
			{
				// The swing is the distance to whichever supply rail is
				// nearer, reduced by the depth knob.  The triangle runs 0 to
				// 65535, so 32768 is the centre and each half is scaled
				// separately to keep every shift operand positive.
				const int32_t headroom = Min(centre[i] + 2048, 2047 - centre[i]);
				const int32_t scaled = (headroom * depth) >> 12;
				const int32_t tri = (int32_t)Triangle(lfoPhase[i]);
				int32_t offset;
				if (tri < 32768) offset = -(((32768 - tri) * scaled) >> 15);
				else             offset =  (((tri - 32768) * scaled) >> 15);
				target = centre[i] + offset;
			}

			if (target < -2048) target = -2048;
			if (target > 2047) target = 2047;

			prevActive[i] = active;

			// One-pole smoothing, held in Q8 so the small differences that
			// remain as it settles do not get truncated away.  Multiplication
			// rather than a left shift, because target can be negative and
			// shifting a negative value is undefined in C++.
			levelQ8[i] += ((target * 256) - levelQ8[i]) >> 8;
		}

		AudioOut1((int16_t)(levelQ8[0] >> 8));
		AudioOut2((int16_t)(levelQ8[1] >> 8));
		CVOut1((int16_t)(levelQ8[2] >> 8));
		CVOut2((int16_t)(levelQ8[3] >> 8));

		// --- two pulse streams ----------------------------------------------
		//
		// A 32-bit phase accumulator: add a per-sample increment and it wraps
		// once per cycle.  The rate increment table covers 0.1Hz to 20Hz, and
		// because the faders are 7-bit it has one entry per possible CC value.
		//
		// The output goes high for the first part of each cycle and low for
		// the rest, so the width fader sets the duty cycle: narrow at one end
		// is a trigger, centred is a square, wide at the other is a gate.  The
		// threshold is a top-20-bit value so it lines up exactly with the
		// phase, and it can never reach 0 or full scale, so there is always
		// some low and some high - a pulse is always produced.
		phase[0] += rateInc[param[4] >> 5];
		phase[1] += rateInc[param[5] >> 5];

		for (int i = 0; i < 2; i++)
		{
			// Bipolar width about the centre: 2048 is a 50% square, and the
			// extremes map to 2% and 98% of the cycle.
			int32_t widthQ12 = 2048 + (((param[6 + i] - 2048) * 1966) / 2048);

			// Slew it like the voltage levels.  Without this a fast fader
			// sweep would momentarily stretch or shorten a pulse, and the
			// pulse LEDs would flicker.
			widthQ8[i] += ((widthQ12 * 256) - widthQ8[i]) >> 8;

			uint32_t threshold = (uint32_t)(widthQ8[i] >> 8) << 20;
			pulseHigh[i] = phase[i] < threshold;
		}

		PulseOut1(pulseHigh[0]);
		PulseOut2(pulseHigh[1]);

		// --- panel feedback -------------------------------------------------
		//
		// LED 1 and LED 5 show the Audio Out 1 and CV Out 1 levels as a steady
		// brightness, which is enough to read where those outputs are.  LED 4
		// shows the mode the switch has selected.  When the accelerometer latch
		// is toggled, all six flash briefly so the change cannot be missed.
		if (blinkRemaining > 0)
		{
			blinkRemaining--;
			// ~3 blinks over half a second: the bit flips every 4096 samples
			// (~85 ms), giving a clear on/off pulse rather than a flicker.
			const bool on = (blinkRemaining >> 12) & 1u;
			for (int i = 0; i < 6; i++) LedOn(i, on);
		}
		else
		{
			LedOn(0, mu.Connected());                           // controller mounted
			LedBrightness(1, LevelLed(levelQ8[0]));
			LedOn(2, pulseHigh[0]);                             // pulse 1 following
			LedOn(3, pulseHigh[1]);                             // pulse 2 following
			LedOn(4, lfoMode);                                  // switch Up = LFO mode
			LedBrightness(5, LevelLed(levelQ8[2]));
		}
	}

private:
	// The 8mu only has 7-bit faders (its CC values are 0-127), so the rate
	// table needs exactly that many entries.  param >> 5 reduces either a
	// fader (0-4064) or a knob (0-4095) to 0-127.
	static constexpr int kRateSteps = 128;

	// Phase increment per sample for a 32-bit accumulator at 48kHz.  The
	// musical choice here is an exponential fader response: equal movements
	// of the fader multiply the rate by the same factor, which is how the ear
	// hears speed, rather than adding a fixed number of Hz.
	int32_t rateInc[kRateSteps];

	// Gesture sensitivity multipliers, Q8 (256 = unity), one per X knob step.
	// Exponential so the knob feels even across the range.
	int32_t sensInc[128];

	// The 8mu fader positions for the eight parameters (0-4064), remembered
	// across samples so the outputs hold when the controller is unplugged.
	// The initial values give a quiet, centred card before anything is moved:
	// 0V on all four outputs and the slowest pulse rate, with the widths at a
	// plain 50% square.
	int32_t param[8] = {2048, 2048, 2048, 2048, 0, 0, 2048, 2048};

	// Smoothed voltage outputs, Q8 (-2048<<8 to 2047<<8).
	int32_t levelQ8[4] = {0, 0, 0, 0};

	// Smoothed pulse widths, Q8 (82<<8 to 4013<<8, a 2% to 98% duty cycle).
	int32_t widthQ8[2] = {2048 << 8, 2048 << 8};

	// Square-wave phase accumulators, one per pulse output.
	uint32_t phase[2] = {0, 0};

	// This sample's pulse outputs, kept so the panel LEDs show the same thing
	// the jacks do.
	bool pulseHigh[2] = {false, false};

	// --- LFO state, one entry per voltage output --------------------------
	//
	// These live in the card, not in the controller, so they survive an 8mu
	// being unplugged mid-performance - see the header comment.
	bool lfoArmed[4] = {false, false, false, false};  // armed to oscillate in LFO mode
	bool prevActive[4] = {false, false, false, false}; // last sample's arm+mode result
	int32_t centre[4] = {0, 0, 0, 0};               // voltage the LFO swings around
	uint32_t lfoPhase[4] = {0, 0, 0, 0};            // triangle phase accumulators

	// When an output stops oscillating its fader is sitting at a rate, not a
	// level, so the output holds its centre until the fader is moved to meet
	// it - otherwise the level would jump to wherever the rate fader was.
	bool levelLocked[4] = {false, false, false, false};
	int32_t pickupPos[4] = {0, 0, 0, 0};            // fader value that takes control back
	int32_t prevParam[4] = {2048, 2048, 2048, 2048};

	// Button edge/long-hold tracking, indexed by top button.
	bool prevDown[4] = {false, false, false, false};
	uint32_t heldSamples[4] = {0, 0, 0, 0};
	bool longFired[4] = {false, false, false, false};

	// 1.5 seconds at 48kHz: long enough that a deliberate hold is clearly not
	// a tap, short enough not to feel stuck.
	static constexpr uint32_t kLongHoldSamples = 72000;

	// --- accelerometer latch state -----------------------------------------
	//
	// Holding the momentary switch Down for half a second flips the four
	// voltage outputs between their faders and the accelerometer.  The state
	// is latched in the card, so it survives an 8mu being unplugged.
	bool accelOn = false;
	uint32_t downHeld = 0;                    // samples spent in the Down position
	static constexpr uint32_t kGestureHoldSamples = 24000;  // 0.5 s at 48kHz

	// Smoothed gesture values, Q8 in the -2032..2032 (or 0..4064 for Flip) range.
	int32_t gestSmooth[4] = {0, 0, 0, 0};

	// All six Computer LEDs flash briefly when the gesture latch toggles, so
	// the state change is unmistakable even though the LEDs otherwise carry
	// live data.
	uint32_t blinkRemaining = 0;
	static constexpr uint32_t kBlinkSamples = 24000;   // 0.5 s at 48kHz, ~3 blinks

	EightMU mu;

	// Arm or disarm one output.  Arming captures the voltage it is already
	// sitting at as the centre the triangle will swing around; disarming is
	// handled by the active-to-inactive edge in ProcessSample, which is the
	// only place that knows whether the output was actually oscillating.
	void SetArmed(int i, bool on)
	{
		if (on == lfoArmed[i]) return;
		lfoArmed[i] = on;

		if (on)
		{
			int32_t c = levelQ8[i] >> 8;
			if (c < -2048) c = -2048;
			if (c > 2047) c = 2047;
			centre[i] = c;
			// Start a quarter of the way in, which is the triangle's zero
			// crossing: the output is already at the centre, so switching on
			// does not first lunge to the bottom of the swing.
			lfoPhase[i] = 0x40000000u;
		}
		// levelLocked is deliberately left alone.  If the output was already
		// holding its centre because its fader is still away at a rate
		// position, arming again in basic mode must keep holding it, or the
		// level would jump the moment the button is pressed.

		pickupPos[i] = ClampFader(centre[i] + 2048);
	}

	// The fader value whose voltage equals a given centre, clamped to the range
	// a fader can actually reach (0-4064).  Without the clamp a centre near a
	// rail would sit at 4095, a value no fader can reach, leaving the level
	// locked for ever.
	static int32_t ClampFader(int32_t v)
	{
		if (v < 0) v = 0;
		if (v > 4064) v = 4064;
		return v;
	}

	// Triangle wave from a 32-bit phase: 0 at the start of the cycle, 65535 at
	// the midpoint, 0 again at the end.  The upper half of the phase mirrors
	// the lower half, which turns a ramp into a triangle with a test and a
	// complement.  Kept unsigned so nothing below has to shift a negative.
	static uint32_t Triangle(uint32_t phase)
	{
		const uint32_t folded = (phase & 0x80000000u) ? ~phase : phase;
		return folded >> 15;   // 0 to 65535, rising then falling
	}

	static int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }

	// Brightness for a voltage-level LED: the absolute level (Q8), scaled to
	// the 0-4095 the LEDs expect.  Clamped at 4095 because LedBrightness
	// squares its argument, and 4096 would overflow the 16-bit product back
	// down to zero - a fully deflected fader would put its LED out.
	static uint16_t LevelLed(int32_t q8)
	{
		int32_t v = q8 < 0 ? -q8 : q8;
		int32_t led = v >> 7;
		if (led > 4095) led = 4095;
		return (uint16_t)led;
	}

	void InitRateTable()
	{
		const float fMin = 0.1f;
		const float fMax = 20.0f;
		const float ratio = powf(fMax / fMin, 1.0f / (float)(kRateSteps - 1));
		const float samplesPerSecond = 48000.0f;
		const float halfCycleToPhase = 4294967296.0f / samplesPerSecond;

		float f = fMin;
		for (int i = 0; i < kRateSteps; i++)
		{
			rateInc[i] = (int32_t)(f * halfCycleToPhase);
			f *= ratio;
		}
	}

	// Gesture sensitivity, 0.25x to 4x across the X knob, exponential and held
	// in Q8.  Index 0 is the minimum and index 127 the maximum, matching the
	// 7-bit resolution of the knob.
	void InitSensTable()
	{
		const float lo = 0.25f;
		const float hi = 4.0f;
		const float ratio = powf(hi / lo, 1.0f / 127.0f);
		float s = lo;
		for (int i = 0; i < 128; i++)
		{
			sensInc[i] = (int32_t)(s * 256.0f);
			s *= ratio;
		}
	}

	// Map the Y knob (0..4095) to a smoothing shift: more clockwise = heavier,
	// slower smoothing.  A shift of 3 is barely any smoothing; 10 is quite
	// settled (about 20 ms time constant).
	static int32_t GestSlewShift(int32_t knob)
	{
		int32_t sh = 3 + (knob >> 9);   // 0..4095 -> 3..10
		if (sh < 3) sh = 3;
		if (sh > 10) sh = 10;
		return sh;
	}
};


int main()
{
	set_sys_clock_khz(200000, true);

	EightMUCVCard card;
	card.Run();
}
