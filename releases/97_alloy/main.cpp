// Alloy: a cross-modulator for the Music Thing Workshop System Computer,
// in the spirit of Mutable Instruments Warps and its parasites firmware.
//
// Fixed-point rewrite of the Warps/parasites algorithms for the FPU-less
// RP2040; see dsp/ for the integer implementation.
//
// Signal flow:
//   Audio In 1  -> carrier
//   Audio In 2  -> modulator
//   Audio Out 1 <- cross-modulated signal (DC-blocked)
//   Audio Out 2 <- dry sum of both inputs (aux, as on original Warps);
//                  zone-specific instead in: doppler (right channel),
//                  frequency shifter (opposite sideband), bitcrusher
//                  (bit-degraded dry mix, for the stereo trick)
//   CV In 1     -> adds to Main knob (algorithm)
//   CV In 2     -> adds to Knob X (timbre)
//   Pulse In 1  -> external Turing clock
//   Pulse In 2  -> Turing reset on rising edge
//   CV Out 1    -> quantized Turing pitch (C minor pentatonic)
//   CV Out 2    -> bipolar Turing modulation CV
//   Pulse Outs  -> two related Turing register bit gates
//
// Controls:
//   Main knob -> algorithm sweep across 15 zones, crossfaded at boundaries:
//                xfade -> fold -> ring mod (analog) -> ring mod (digital)
//                -> ring mod morph -> xor -> comparator -> comparator8
//                -> comparator+chebyschev -> chebyschev -> bitcrusher
//                -> frequency shifter -> echo delay -> stereo doppler
//                -> vocoder
//   Knob X    -> timbre, matching the original's modulation parameter per
//                algorithm: xfade position, fold amount, ring-mod gain,
//                xor blend, comparator morphs, chebyschev order (1..16),
//                bitcrusher degradation amount, frequency shift,
//                delay time, doppler x position (L..R), vocoder FORMANT
//                SHIFT (centre = neutral).
//                Secondary parameters ride the Main knob's position WITHIN
//                a zone (as in the original META sweep): fold bias,
//                ring-morph character, freq-shifter feedback, delay
//                feedback, doppler room size, vocoder release time.
//   Switch up     -> Turing edit layer:
//                    Main = feedback probability, X = loop length,
//                    Y = pitch/CV spread
//   Switch middle -> cross-modulator controls; knobs use soft pickup after
//                    editing
//   Switch down   -> tap/clock the Turing sequence; two taps start or update
//                    its internal clock, hold 750ms to stop it
//   Knob Y        -> per-zone third parameter in cross-mod mode: sideband
//                    crossfade (frequency shifter), operator morph
//                    (bitcrusher), depth (doppler); input drive (10%
//                    floor) in all other zones
//
// LEDs:
//   switch up -> first six active Turing register bits
//   otherwise 0/1/2/3 = current algorithm zone (binary, 0..14);
//             all four lit = vocoder freeze (spectral envelope held)
//             4 = latched ON if any sample exceeded the 20us budget
//             5 = CPU headroom (bright = idle, dark = fully loaded)

#include "ComputerCard.h"

#include "hardware/vreg.h"
#include "pico/time.h"

#include "dsp/cpu_meter.h"
#include "dsp/soft_takeover.h"
#include "dsp/tap_clock.h"
#include "dsp/turing_sequencer.h"
#include "dsp/xmod_engine.h"

// Debug taps, readable over the debugger without halting for long.
// Peak absolute effect output (decays slowly) and raw input peaks.
volatile int32_t g_debug_out_peak = 0;
volatile int32_t g_debug_in1_peak = 0;
volatile int32_t g_debug_in2_peak = 0;

class Alloy : public ComputerCard
{
public:
	Alloy()
		: turing_(FoldSeed(UniqueCardID()))
	{
	}

	// The vocoder splits its band processing across both cores; launch its
	// core-1 worker before audio starts.
	void StartVocoderWorker() { engine_.StartVocoderWorker(); }

	virtual void ProcessSample() override
	{
		meter_.BeginSample();

		const Switch sw = SwitchVal();
		const uint16_t main_knob = KnobVal(Knob::Main);
		const uint16_t x_knob = KnobVal(Knob::X);
		const uint16_t y_knob = KnobVal(Knob::Y);

		if (!controls_initialized_)
		{
			held_main_ = main_knob;
			held_timbre_ = x_knob;
			held_drive_ = y_knob;
			controls_initialized_ = true;
		}

		if (previous_switch_ == Switch::Up && sw != Switch::Up)
		{
			main_pickup_.Arm(held_main_, main_knob);
			timbre_pickup_.Arm(held_timbre_, x_knob);
			drive_pickup_.Arm(held_drive_, y_knob);
		}

		if (sw == Switch::Up)
		{
			turing_feedback_ = main_knob;
			const uint8_t length = LengthFromKnob(x_knob);
			if (length != turing_.length())
			{
				turing_.SetLength(length);
				turing_outputs_dirty_ = true;
			}
			if (y_knob != turing_spread_)
			{
				turing_spread_ = y_knob;
				turing_outputs_dirty_ = true;
			}
		}
		else
		{
			if (main_pickup_.Allows(main_knob)) held_main_ = main_knob;
			if (timbre_pickup_.Allows(x_knob)) held_timbre_ = x_knob;
			if (drive_pickup_.Allows(y_knob)) held_drive_ = y_knob;
		}

		bool manual_clock = false;
		if (sw == Switch::Down)
		{
			if (previous_switch_ != Switch::Down)
			{
				tap_clock_.Tap();
				manual_clock = true;
				down_samples_ = 0;
				clock_stopped_this_hold_ = false;
			}
			else if (down_samples_ < kStopHoldSamples)
			{
				++down_samples_;
			}

			if (down_samples_ >= kStopHoldSamples && !clock_stopped_this_hold_)
			{
				tap_clock_.Stop();
				clock_stopped_this_hold_ = true;
			}
		}
		else
		{
			down_samples_ = 0;
			clock_stopped_this_hold_ = false;
		}

		const bool external_clock_patched = Connected(Input::Pulse1);
		const bool external_clock = PulseIn1RisingEdge();
		const bool internal_clock =
			tap_clock_.Process(external_clock_patched || sw == Switch::Down);
		const bool sequence_clock =
			manual_clock || external_clock || internal_clock;
		if (PulseIn2RisingEdge())
		{
			turing_.ResetToCycleStart();
			turing_outputs_dirty_ = true;
		}
		else if (sequence_clock)
		{
			turing_.Clock(turing_feedback_);
			turing_outputs_dirty_ = true;
		}

		UpdateTuringOutputs();

		// CV remains live on top of the held knob values in edit mode.
		const int32_t algo = Clamp04095(held_main_ + CVIn1());
		const int32_t timbre = Clamp04095(held_timbre_ + CVIn2());
		engine_.SetControls(algo, timbre, held_drive_);

		const int32_t carrier = AudioIn1();
		const int32_t modulator = AudioIn2();

		int32_t out = DcBlock(engine_.Process(carrier, modulator));
		AudioOut1(static_cast<int16_t>(out));
		// Out 2: dry sum, except in zones with their own aux signal
		// (doppler right channel, shifter's opposite sideband, bitcrushed
		// dry mix).
		int32_t aux = engine_.has_stereo_aux()
			? engine_.aux()
			: xmod::Clip((carrier + modulator) >> 1);
		AudioOut2(static_cast<int16_t>(aux));

		// Peak-hold debug taps with slow decay (~1s time constant).
		int32_t a = out < 0 ? -out : out;
		if (a > g_debug_out_peak) g_debug_out_peak = a;
		else g_debug_out_peak -= g_debug_out_peak >> 16;
		a = carrier < 0 ? -carrier : carrier;
		if (a > g_debug_in1_peak) g_debug_in1_peak = a;
		else g_debug_in1_peak -= g_debug_in1_peak >> 16;
		a = modulator < 0 ? -modulator : modulator;
		if (a > g_debug_in2_peak) g_debug_in2_peak = a;
		else g_debug_in2_peak -= g_debug_in2_peak >> 16;

		meter_.EndSample();
		UpdateLeds(sw == Switch::Up);
		previous_switch_ = sw;
	}

private:
	static constexpr uint32_t kStopHoldSamples =
		(xmod::TapClock::kSampleRate * 3U) / 4U;

	static uint32_t FoldSeed(uint64_t id)
	{
		uint32_t seed = static_cast<uint32_t>(id)
			^ static_cast<uint32_t>(id >> 32)
			^ 0x9e3779b9U;
		return seed != 0 ? seed : 1U;
	}

	static uint8_t LengthFromKnob(uint16_t value)
	{
		static constexpr uint8_t kLengths[8] = {2, 3, 4, 5, 6, 8, 12, 16};
		const uint32_t index = (static_cast<uint32_t>(value) * 8U) >> 12;
		return kLengths[index < 8 ? index : 7];
	}

	static int32_t Clamp04095(int32_t v)
	{
		if (v < 0) return 0;
		if (v > 4095) return 4095;
		return v;
	}

	void UpdateTuringOutputs()
	{
		if (!turing_outputs_dirty_)
		{
			return;
		}

		CVOut1MIDINote(turing_.MidiNote(turing_spread_));
		CVOut2(turing_.BipolarCv(turing_spread_));
		PulseOut1(turing_.GateA());
		PulseOut2(turing_.GateB());
		turing_outputs_dirty_ = false;
	}

	// One-pole DC blocker (~7Hz): several algorithms (fold, comparators,
	// frozen vocoder) can produce sustained DC which would otherwise go
	// straight to the DC-coupled outputs.
	int32_t DcBlock(int32_t x)
	{
		int32_t y = x - dc_x1_ + dc_y1_ - (dc_y1_ >> 10);
		dc_x1_ = x;
		dc_y1_ = y;
		return xmod::Clip(y);
	}

	int32_t dc_x1_ = 0;
	int32_t dc_y1_ = 0;

	void UpdateLeds(bool turing_edit)
	{
		if (turing_edit)
		{
			for (uint8_t led = 0; led < 6; ++led)
			{
				LedOn(led, turing_.LedBit(led));
			}
			return;
		}

		// Zone in binary on LEDs 0..3. Pattern 15 (all four lit) is unused
		// by the 15 zones, so it doubles as the vocoder freeze indicator:
		// the vocoder zone (14 = LEDs 1/2/3) gains LED 0 while frozen.
		int32_t zone = engine_.zone();
		if (engine_.vocoder_frozen()) zone = 15;
		LedOn(0, zone & 1);
		LedOn(1, zone & 2);
		LedOn(2, zone & 4);
		LedOn(3, zone & 8);
		LedOn(4, meter_.Overrun());
		LedBrightness(5, static_cast<uint16_t>(meter_.Headroom()));
	}

	xmod::Engine engine_;
	xmod::TuringSequencer turing_;
	xmod::TapClock tap_clock_;
	xmod::SoftTakeover main_pickup_;
	xmod::SoftTakeover timbre_pickup_;
	xmod::SoftTakeover drive_pickup_;
	xmod::CpuMeter meter_{20}; // 1/48kHz ~= 20.8us; 20 is a safe budget
	Switch previous_switch_ = Switch::Middle;
	uint16_t held_main_ = 2048;
	uint16_t held_timbre_ = 2048;
	uint16_t held_drive_ = 2048;
	uint16_t turing_feedback_ = 2048;
	uint16_t turing_spread_ = 2048;
	uint32_t down_samples_ = 0;
	bool controls_initialized_ = false;
	bool clock_stopped_this_hold_ = false;
	bool turing_outputs_dirty_ = true;
};

int main()
{
	// Multiple of 48MHz for clean ADC timing (per ComputerCard guidance).
	// 240MHz requires the vreg boost to 1.25V and gives the 20-band
	// vocoder comfortable headroom at 48kHz; without the boost the audio
	// engine fails to run at this clock. Re-test audio if changing either.
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    set_sys_clock_khz(240000, true);
	// Static: the engine owns a ~32KB delay buffer, far too big for the
	// 2KB core-0 stack.
	static Alloy card;
	card.EnableNormalisationProbe();
	card.StartVocoderWorker();
	card.Run();
}
