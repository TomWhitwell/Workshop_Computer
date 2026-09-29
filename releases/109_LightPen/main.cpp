// LIGHTPEN — an RGB sensor wand for the Workshop System Computer.
//
// Fixed wiring, every mode:
//   CV Out 1  = LDR supply, held at full scale
//   CV In 1   = Red LDR,  CV In 2 = Green LDR,  Audio In 2 = Blue LDR (DC)
//   X knob    = sensitivity,  Y knob = slew
//   Switch Up = next mode,  Down = the mode's action
// Hold Down at power-on to calibrate white and black (calibration.h).

#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"

#include "calibration.h"
#include "calibstore.h"
#include "controls.h"
#include "engine.h"
#include "leds.h"
#include "sensors.h"
#include "modes/mirror.h"
#include "modes/triad.h"
#include "modes/barcode.h"
#include "modes/tapescrub.h"
#include "modes/synesthesia.h"
#include "modes/hueorgan.h"
#include "modes/jog.h"
#include "modes/prism.h"
#include "modes/colourfilter.h"
#include "modes/delay.h"
#include "modes/reverb.h"
#include "modes/freeze.h"
#include "modes/modulation.h"
#include "modes/mangle.h"

using namespace lp;

class LightPen : public ComputerCard
{
public:
	static constexpr int kNumModes = 14;

	/// Before Run(). `saved`: this is a calibration loaded from flash, not
	/// the defaults.
	void SetCalibration(const CalibData &d, bool saved)
	{
		sensors_.SetCalibration(d);
		haveCalibration_ = saved;
	}

	/// After Run() returns: a finished calibration waiting to be written.
	bool SaveRequested() const { return saveRequested_; }
	const CalibData &NewCalibration() const { return calib_.Result(); }

protected:
	void __not_in_flash_func(ProcessSample)() override
	{
		// Latched by the library until overwritten, and nothing else writes it:
		// engines only ever reach CV Out 2 through EngineOut.
		if (!excited_)
		{
			CVOut1Precise(262143);
			excited_ = true;
		}

		RawRGB raw{ CVIn1(), CVIn2(), AudioIn2() };

		// The switch is not readable straight away: ComputerCard derives it
		// from a smoothed ADC channel that starts at zero, and zero decodes as
		// Down. So wait, then take ONE settled reading (WorkshopBio/WorkshopZX's
		// proven pattern) rather than latching on "Down seen at any point".
		// The wait also lets the LDRs settle once CV Out 1 powers them.
		if (bootPhase_ < kBootSettleSamples)
		{
			if (++bootPhase_ == kBootSettleSamples) FinishBoot();
			return;
		}

		if (calibrating_)
		{
			CalibrationSample(raw);
			return;
		}

		bool ctrlTick = (++ctrlCount_ >= kCtrlDiv);
		if (ctrlTick)
		{
			ctrlCount_ = 0;
			sensors_.SetKnobs(KnobVal(X), KnobVal(Y));
		}
		sensors_.Update(raw, frame_);

		if (ctrlTick) ControlTick();

		Inputs in{ AudioIn1(), PulseIn1(), PulseIn1RisingEdge() };
		engines_[mode_]->AudioTick(frame_, in, out_);
		WriteOutputs();
	}

private:
	void ControlTick()
	{
		uint8_t ev = controls_.Tick(static_cast<Sw>(SwitchVal()));
		if (ev & Controls::kEvModeNext)
		{
			mode_ = (mode_ + 1) % kNumModes;
			EnterMode();
		}

		Engine *e = engines_[mode_];
		if (ev & Controls::kEvDownPress)   e->OnDownPress();
		if (ev & Controls::kEvDownHold)    e->OnDownHold();
		if (ev & Controls::kEvDownRelease) e->OnDownRelease(controls_.PressTicks());

		Ctrl c{ KnobVal(Main), controls_.DownHeld(), controls_.PressTicks() };
		e->ControlTick(frame_, c, out_);

		if (out_.ledFlash)
		{
			leds_.Flash(out_.ledFlash, true);
			out_.ledFlash = 0;
		}
		uint16_t level[6];
		leds_.Tick(mode_, frame_, level);
		for (int i = 0; i < 6; i++) LedBrightness(i, level[i]);
	}

	void FinishBoot()
	{
		Sw sw = static_cast<Sw>(SwitchVal());
		// Init swallows a held Down until it is released, so the boot hold
		// is neither the first calibration tap nor a press in Mode 1.
		controls_.Init(sw);
		if (sw == Sw::Down)
		{
			calibrating_ = true;
			calib_.Begin();
			return;
		}
		StartPlaying();
	}

	void StartPlaying()
	{
		calibrating_ = false;
		EnterMode();
		// Over the mode flash: all six blinking means nothing is calibrated;
		// three blinking means a five-point calibration could not be used and
		// its white/black is standing in; otherwise a steady bar reports how
		// well the gels separate colour, 1 (barely) to 5 (cleanly).
		if (!haveCalibration_) leds_.Flash(6, true);
		else if (sensors_.Quality() & kQualLowSep) leds_.Flash(3, true);
		else if (sensors_.SeparationBars() > 0) leds_.Flash(sensors_.SeparationBars(), false);
	}

	void CalibrationSample(const RawRGB &raw)
	{
		calib_.Sample(raw);
		if (++ctrlCount_ < kCtrlDiv) return;
		ctrlCount_ = 0;

		uint8_t ev = controls_.Tick(static_cast<Sw>(SwitchVal()));
		if (ev & Controls::kEvModeNext)
		{
			// Up cancels, keeping whatever calibration was loaded at boot.
			StartPlaying();
			return;
		}
		if (ev & Controls::kEvDownPress) calib_.Tap();
		calib_.Tick();

		uint16_t level[6];
		calib_.Leds(raw, level);
		for (int i = 0; i < 6; i++) LedBrightness(i, level[i]);

		if (calib_.ReadyToSave())
		{
			// Run() returns once this sample is finished; main() writes the
			// flash with no interrupt handlers left, then reboots.
			saveRequested_ = true;
			Abort();
		}
	}

	void EnterMode()
	{
		out_ = EngineOut{};
		lastNote_ = -1;
		engines_[mode_]->OnEnter();
		leds_.FlashMode();
	}

	void __not_in_flash_func(WriteOutputs)()
	{
		AudioOut1(out_.audio1);
		AudioOut2(out_.audio2);
		if (out_.cv2Note >= 0)
		{
			// CVOut2MIDINote goes through MIDIToDAC, which is flash-resident:
			// only call it when the note changes (at most once a control tick).
			if (out_.cv2Note != lastNote_)
			{
				CVOut2MIDINote(static_cast<uint8_t>(out_.cv2Note));
				lastNote_ = out_.cv2Note;
			}
		}
		else
		{
			CVOut2Precise(out_.cv2);
		}
		PulseOut1(out_.pulse1);
		PulseOut2(out_.pulse2);
	}

	Calibration    calib_;
	SensorPipeline sensors_;
	Controls       controls_;
	Leds           leds_;
	SensorFrame    frame_{};
	EngineOut      out_;

	MirrorMode      mirror_;
	TriadMode       triad_;
	BarcodeMode     barcode_;
	TapeScrubMode   tapescrub_;
	SynesthesiaMode synesthesia_;
	HueOrganMode    hueorgan_;
	JogMode         jog_;
	PrismMode       prism_;
	ColourFilterMode colourfilter_;
	DelayMode        delay_;
	ReverbMode       reverb_;
	FreezeMode       freeze_;
	ModulationMode   modulation_;
	MangleMode       mangle_;
	Engine *engines_[kNumModes] = {
		&mirror_, &triad_, &barcode_, &tapescrub_, &synesthesia_, &hueorgan_,
		&jog_, &prism_, &colourfilter_, &delay_, &reverb_, &freeze_, &modulation_,
		&mangle_,
	};

	static constexpr int32_t kBootSettleSamples = kSampleRate / 2;   // ~0.5s

	int     mode_ = 0;
	int     ctrlCount_ = 0;
	int32_t bootPhase_ = 0;
	int16_t lastNote_ = -1;
	bool    excited_ = false;
	bool    calibrating_ = false;
	bool    haveCalibration_ = false;
	bool    saveRequested_ = false;
};

int main()
{
	// Overclock first. 192MHz at 1.15V is proven on this hardware by the
	// sibling cards; the settle after the voltage change keeps the PLL relock
	// off an unstable rail.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(192000, true);

	// Static, not on the stack: the Mode 4 tape alone is 48KB.
	static LightPen card;

	CalibData saved;
	bool haveSaved = LoadCalibration(saved);
	card.SetCalibration(haveSaved ? saved : CalibDefaults(), haveSaved);

	// Must precede Run(). The library then forces a disconnected input to
	// exactly 0, which is what Modes 2 and 5 need: Audio In 1 is their 1V/oct
	// input, the audio inputs are normalled to each other, and Audio In 2
	// always has the Blue LDR in it. Without this, an unpatched Audio In 1
	// could transpose the drone with the light falling on the wand.
	card.EnableNormalisationProbe();

	card.Run();

	// Run() returns only after Abort(), which only a finished calibration
	// calls. Audio is stopped and both library interrupts are gone, so the
	// flash write cannot fault anything. Reboot to start clean with it.
	if (card.SaveRequested()) SaveCalibration(card.NewCalibration());
	watchdog_reboot(0, 0, 0);
	while (true) tight_loop_contents();
}
