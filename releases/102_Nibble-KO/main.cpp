// main.cpp — NIBBLE-KO. Card entry point, mode machine, UI, output routing.
//
// A program card for the Music Thing Modular Workshop System Computer, built
// on Chris Johnson's header-only ComputerCard library.
//
// NIBBLE-KO reads ONE output of the Workshop System's FOUR VOLTAGES module on
// CV In 1, learns which voltage each button combination produces, and turns
// the combinations into drum hits. See levels.h for the ghost rule, which is
// the piece of this card that cannot be guessed from the code around it.
//
// ---------------------------------------------------------------------------
// THE CONTROL SURFACE
// ---------------------------------------------------------------------------
//
// The switch is a MODE SELECTOR, not a trigger. Its three positions:
//
//   DOWN    choose a mode. The four buttons pick one; the choice is committed
//           when the switch is RELEASED, and then LATCHES.
//   MIDDLE  play the latched mode.
//   UP      play the latched mode AND record it into the loop.
//
// Latching is what makes every mode recordable through one mechanism: mode
// selection is a transient Down-gesture that has finished by the time you
// reach Up, so Down and Up never need to be held at once.
//
//   switch+A  DRUMS    the kit (power-on default)
//   switch+B  MUTE     three mute-group toggles
//   switch+C  FX       twelve performance effects
//   switch+D  PATTERN  four stored loops: tap recalls, hold stores
//
// Inside a mode, THE MODE'S OWN BUTTON IS THE SHIFT — the same hold-and-tap
// mechanic the kit is played with, so the whole card is one gesture:
//
//   MUTE     hold B, tap A / C / D    toggle mute group 1 / 2 / 3
//   PATTERN  hold D, tap A / B / C    recall that slot, instantly
//            ...with the switch UP    store the live loop into it instead
//
// The SWITCH is the verb in PATTERN mode. Up already means "commit this to
// the loop" everywhere else on the card, and storing a pattern is the same
// idea one level up — so it costs no new gesture to learn.
//
// A hold-to-store gesture would not work here, and the reason is the quirk
// this whole card is built around: Four Voltages LATCHES. A held button is a
// level that sits there indefinitely, so any "held for N ticks" test passes
// eventually and every recall becomes a store.
//
// A recall does NOT move the playhead: the new loop picks up wherever in the
// bar you already are, so switching mid-bar reads as the band changing part
// rather than a stop and start. Recall DISCARDS whatever you were playing,
// which is what makes it possible to try something over a pattern and
// abandon it.
//
// FX1 goes further: ANY single is a shift there, so it carries TWELVE
// effects rather than three — four shifts by three taps, exactly as the kit
// gets twelve voices from the same four buttons. One family per shift, so
// the layout is learnable as four groups rather than twelve pairings:
//
//   hold A + B/C/D   filters      low-pass / high-pass / band sweep
//   hold B + A/C/D   destruction  bit-crush / decimate / wavefold
//   hold C + A/B/D   rhythmic     stutter / flam / gate
//   hold D + A/B/C   transport    reverse / tape-stop / silence
//
// The D bank means DIFFERENT THINGS to a sampled voice and a synthesised
// one, which is why those three are grouped: the gesture is "play it wrong
// on purpose", and what that means depends on what the voice is made of.
// Reverse plays a recording backwards, and runs a synth voice's ENVELOPE
// backwards — a swell instead of a decay. Tape-stop winds a recording's rate
// down to nothing, and a synth voice's PITCH down to nothing. Both are armed
// at trigger time, so they catch hits that start while the effect is held
// and leave ones already sounding alone.
//
// HOLD A SHIFT ALONE and turn Main to draw that lane's PARAMETER CURVE. It
// needs no gesture of its own because it is the absence of one — you are
// already holding the shift, so letting go of the tap hands the knob to the
// curve. Each shift therefore owns two recorded lanes, an effect and a
// parameter, performed independently: sweep a curve once, then pop the three
// effects in and out over the top of it across later passes.
//
// The curve is the single source of depth. An effect reads it whether it is
// live or replayed, so what you hear performing is exactly what records.
//
// Main sets each effect's depth. See fx.h for the table and fx.cpp for the
// DSP. FX2 is deliberately left at three as a placeholder — if one bank
// holds twelve, whether a second bank should exist at all is a real
// question, and worth answering after playing these rather than before.
//
// While FX1 is the mode, Main is the effect DEPTH and NOT the DJ filter —
// one knob cannot do both, and sweeping a crush depth would sweep the filter
// with it. The filter latches where it was, and picks up again only when the
// knob is turned back THROUGH that value, so leaving the mode never jumps
// it. Same convention as a DJ mixer recalling a patch. See SoftPickup.
//
// A bare press does nothing in these modes. That is not a restriction, it is
// the only thing that WORKS: to press A twice you must pass back through the
// rest voltage, so a bare-press toggle can mute a group and never unmute it.
// Holding the shift makes each tap a pair, and the ghost rule silences the
// release back onto the shift — so tap-tap-tap toggles as often as you like.
//
//   switch+AB  UNDO          switch+AC  QUANTISE (cycle the record grid)
//   switch+CD  PLAY/STOP     switch+BD  enter the USB/browser setup
//
// The adjacent pairs carry what you reach for mid-take; the WebUI takes the
// awkward diagonal because it is a setup activity done with both hands free.
//
// WHY SINGLES COMMIT ON RELEASE BUT PAIRS FIRE IMMEDIATELY. This asymmetry
// looks like an inconsistency and is not — both halves fall out of the ghost
// rule, pointing in opposite directions:
//
//   Four Voltages does not return to a rest voltage when you let go, so the
//   CV sits whereever it was last put. If it is ALREADY on A's level and you
//   press A to select DRUMS, the level never changes and no Trigger is ever
//   produced — a press-driven select would simply never fire, and the card
//   would look broken. Reading the STATE on release works regardless.
//
//   A pair cannot get stuck that way. Releasing a pair falls back onto one of
//   its own members, and levels.cpp sets current_ to that SINGLE (the
//   GhostArmed branch). So the resting state is always a single, a pair press
//   is always a genuine transition, and acting on the Trigger is safe.
//
// ---------------------------------------------------------------------------
// PANEL
// ---------------------------------------------------------------------------
//
//   CV In 1      Four Voltages output          (the instrument itself)
//   Pulse In 1   external clock, one edge per beat
//
//   Main   DJ filter (LP | bypass | HP)   /  FX depth in an effects mode
//   X      tempo, 40-240bpm (unless clocked)
//   Y      kit character
//
//   Audio Out 1  drum bus                 Audio Out 2  the same drum bus
//   Pulse Out 1  gate on every hit        Pulse Out 2  click, one per beat
//   CV Out 1/2   unassigned for now
//
// Calibration is the ALT-BOOT mode — hold the switch down at power-on — with
// the learned levels saved to flash (calibstore.h) and reloaded on every
// normal boot, so a normal power-up is playable within the splash. See the
// splash handler in ProcessSample().
//
// DURING a calibration the switch has a different job, as it does in NIBBLE:
//
//   tap        capture the voltage of the combo you are holding
//   hold 2s    throw this calibration away — reverts to the last SAVED
//              calibration if one exists, otherwise starts the learn again
//
// Capture is a TAP rather than the button press itself because the press and
// the voltage arriving are the same event — the settle detector cannot have
// trusted the reading yet. Holding the combo and tapping separates the two.
//
// ---------------------------------------------------------------------------
// TWO CORES, AND WHY
// ---------------------------------------------------------------------------
//
// Core 0 plays the card: the drum voices, one SVF, the CV stage — a few
// hundred cycles against a budget of 4000 at 192MHz. That never needed a
// second core and still does not.
//
// USB does. TinyUSB's tud_task() measured ~36000 cycles on WorkshopBio, i.e.
// 14x the entire 20.8us audio budget, so it cannot share the audio interrupt.
// It lives on core 1 (see core1Entry at the bottom of this file), and it is
// MODAL: core 1 spins doing nothing until switch+B+D sets WebUI::usbMode, so
// on a card that is being played the stack is never even initialised.
//
// When an upload starts, core 0 parks inside ProcessSample() and core 1 owns
// the machine — including the LEDs. See ShowUploadProgress().

#include "ComputerCard.h"

#include "nibbleko.h"
#include "levels.h"
#include "drums.h"
#include "looper.h"
#include "fx.h"
#include "fastmath.h"
#include "calibstore.h"
#include "webui.h"

#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

using namespace nko;

namespace {

// ---------------------------------------------------------------------------
// Learn-mode constants
// ---------------------------------------------------------------------------

/// Ergonomic learn order: singles, then rows, columns, diagonals.
///
/// Deliberately NOT index order. The hand alternates shape instead of
/// re-forming similar grips, and on the 2x2 LED block it draws a geometric
/// sequence — top row, bottom row, left column, right column, then the two
/// diagonals — which the player reads off the panel without counting.
constexpr uint8_t kLearnOrder[kNumLevels] = { kA, kB, kC, kD,
                                              kAB, kCD, kAC, kBD, kAD, kBC };

/// Abandon a learn that has stalled. Generous, because the learn is self-paced
/// — a player may well be re-reading the panel. Its only job is to stop a card
/// being left permanently in learn.
constexpr int32_t kLearnTimeoutTicks = 30 * kCtrlRate;

constexpr int32_t kCaptureFlashTicks   = kCtrlRate / 5;    // 200ms
constexpr int32_t kCollisionFlashTicks = kCtrlRate / 2;    // 500ms, ~3 blinks
constexpr int32_t kNotSettledTicks     = kCtrlRate / 3;    // 330ms
constexpr int32_t kDoneFlashTicks      = (kCtrlRate * 2) / 5;
constexpr int32_t kFailFlashTicks      = 3 * kCtrlRate / 2;   // 1.5s

/// A learn is REJECTED outright if the ten captured levels do not span at
/// least this much of the input range.
///
/// The case this exists for is calibrating with nothing patched into CV In 1:
/// every step captures the same floating value, the card cheerfully builds a
/// table of ten identical levels, and then plays one note forever. That looks
/// like a working calibration and a broken card.
///
/// 400 units is about 1.2V. Ten levels genuinely spread across a Four Voltages
/// output cover several volts, so this rejects only the degenerate cases.
constexpr int32_t kMinLearnSpan = 400;

/// Gate length for hit events, in samples. 5ms is long enough for anything
/// downstream to see it and short enough not to smear fast playing.
constexpr int32_t kGateSamples = kSampleRate / 200;

/// Click-track pulse length. Shorter than a gate — it is a metronome tick, not
/// a note, and at 240bpm the beats are only a quarter of a second apart.
constexpr int32_t kClickSamples = kSampleRate / 500;   // 2ms

/// How long a replayed effect survives without a fresh event, in LOOP TICKS.
///
/// The FX lane samples every kKnobSampleTicks (8) while an effect is held, so
/// a gap longer than that means the hold ended. Two sample intervals gives
/// slack for the recording and playback grids not lining up, without leaving
/// the effect hanging audibly past where it was released.
///
/// LOOP ticks, not control ticks, and the distinction is not academic: eight
/// loop ticks is 42ms at 240bpm but 250ms at 40bpm, which is 125 versus 750
/// control ticks. A fixed control-tick figure would either expire instantly
/// at slow tempos or hang on for a quarter of a second at fast ones. This is
/// therefore counted down where the loop advances, not on the control tick.
constexpr int32_t kFxPlaybackHold = 2 * kKnobSampleTicks;

// There is no hold-to-store threshold, and there cannot be one: Four Voltages
// LATCHES, so a button held is a level that sits there indefinitely and any
// "held for N ticks" test passes eventually. Storing is the SWITCH's job
// instead — see PatternPress.

/// How long a store is acknowledged on the pads.
///
/// A store has no audible result whatsoever — the loop carries on playing
/// exactly as it was — so without this there is no way to tell it happened
/// from the gesture having missed.
constexpr int32_t kPatternFlashTicks = (kCtrlRate * 2) / 5;   // 400ms

/// How long a TAPE STOP takes to wind down, in audio samples.
///
/// 2400 is 50ms — an abrupt brake, the sort a finger on a capstan gives. The
/// top of the range is 2.4 seconds, long enough that a hit becomes a slow
/// descending groan rather than a stop. Both ends are musical; the middle is
/// the classic effect.
constexpr int32_t kTapeStopMin   = 2400;
constexpr int32_t kTapeStopRange = 115200;

/// The gap between a flam's two strikes, in control ticks, across the knob.
///
/// 15 ticks is 5ms — barely two attacks, more a thickening of one hit. 105 is
/// 35ms, where the two are clearly separate and it starts to read as a fast
/// echo rather than a grace note. The classic flam sits around the middle.
constexpr int32_t kFlamMin   = 15;
constexpr int32_t kFlamRange = 90;

/// Stutter division, in control ticks: the fastest repeat and how much slower
/// the knob can make it. 30 ticks is 10ms (a buzz), 30+270 is 100ms (a
/// sixteenth at 150bpm).
constexpr int32_t kStutterMin   = 30;
constexpr int32_t kStutterRange = 270;

/// How long an UNDO is announced on the status LEDs.
///
/// Undo is invisible otherwise: the pattern simply reverts, and if you undid
/// a pass that added little you may not hear the difference for a whole loop.
/// Without an acknowledgement there is no way to tell "it worked" from "the
/// gesture missed", which are the two things you actually need to separate.
constexpr int32_t kUndoFlashTicks = (kCtrlRate * 2) / 5;   // 400ms

/// How long the QUANTISE grid is shown on the status LEDs after a change.
///
/// STEADY, not blinking — the grid is displayed as a pattern across LEDs 4
/// and 5 (see the display code), so it only has to be held long enough to
/// look at. Long enough to catch out of the corner of an eye mid-take;
/// short enough that it hands the LEDs back to the beat and record markers
/// before you need them again.
constexpr int32_t kQuantFlashTicks = (kCtrlRate * 4) / 5;   // 800ms

/// How long a mode-select gesture must have the switch down before the release
/// counts as a deliberate selection.
///
/// Without this, the smallest knock against the switch would re-latch the mode
/// to whatever the CV happens to be sitting on. Short enough to feel
/// instantaneous; long enough that only an intended press qualifies.
constexpr int32_t kSelectMinTicks = kCtrlRate / 20;    // 50ms

// ---------------------------------------------------------------------------

enum class UiMode : uint8_t { Play, Learn };

/// What the learn machine is doing between captures.
enum class LearnPhase : uint8_t {
	Waiting, Confirm, Collision, NotSettled, Done, Failed, Aborted
};

/// One automated knob: recorded playback, and a live hand that overrides it.
///
/// The rule: MOVING the knob mutes that lane's playback, for as long as you
/// keep moving and for a short hold afterwards. Stop, and the recorded sweep
/// takes over again from wherever it has got to.
///
/// That makes the knob a performance override rather than a mode: grab it,
/// ride the filter through a section, let go, and the pattern carries on
/// exactly as recorded. Nothing is destroyed by touching it.
///
/// What this replaces: a design that "handed control back" on a move and then
/// handed it forward again on the next recorded event, so control alternated
/// between hand and playback every few ticks whenever both were active.
/// Muting for a held window instead means only ONE of them is ever driving.
struct AutoKnob
{
	int32_t playback_  = -1;      ///< last value playback asked for, -1 = none
	int32_t smooth_    = 0;       ///< de-dithered knob reading
	int32_t last_      = -9999;   ///< reading the move detector compares against
	int32_t holdTicks_ = 0;       ///< >0 while the hand owns the knob

	/// How long the hand keeps the knob after it stops moving. Long enough to
	/// bridge the gaps in a slow deliberate turn; short enough that letting go
	/// feels immediate.
	static constexpr int32_t kHold = kCtrlRate / 4;   // 250ms

	bool HandOwns() const { return holdTicks_ > 0; }

	/// Control-rate update. Returns the value to use this tick.
	int32_t Update(int32_t live)
	{
		// Smoothed, because the move test is against a threshold and raw ADC
		// dither would otherwise trip it on a stationary knob.
		smooth_ = slew_exact(smooth_, live, 4);

		if (last_ < -9000) last_ = smooth_;      // first call: seed, do not move

		int32_t d = smooth_ - last_;
		if (d > kKnobMoveThresh || d < -kKnobMoveThresh)
		{
			last_      = smooth_;
			holdTicks_ = kHold;
		}
		else if (holdTicks_ > 0)
		{
			holdTicks_--;
		}

		if (HandOwns() || playback_ < 0) return smooth_;
		return playback_;
	}

	void Playback(int32_t v) { playback_ = v; }
	void Forget()            { playback_ = -1; }

	/// The value Update() last settled on — whichever of hand or playback is
	/// currently driving. For readers that need it outside the update.
	int32_t Value() const
	{
		return (HandOwns() || playback_ < 0) ? smooth_ : playback_;
	}
};

/// SOFT PICKUP for a knob that two things want.
///
/// In FX1 the Main knob sets the effect's depth, so it cannot also be driving
/// the DJ filter — sweeping a crush depth would sweep the filter with it. The
/// filter therefore LATCHES at whatever it was when the mode was entered.
///
/// Handing it back is the interesting half. Snapping the filter to wherever
/// the knob physically ended up would jump it, often drastically, at the
/// moment you leave the mode — the one moment you are least expecting a
/// change. So the filter stays latched until the knob is turned back THROUGH
/// the latched value, and only then does it start following again. That is
/// the same convention a DJ mixer or a hardware synth uses when recalling a
/// patch, and it is the only one that never jumps.
///
/// The catch, and the reason this is a struct rather than two lines: "passes
/// through" needs the SIDE the knob was on when control was taken away. Only
/// comparing for equality would never match — the knob moves in steps of
/// several units and can step straight over the target.
struct SoftPickup
{
	int32_t held_    = 2048;   ///< the value being held while parked
	bool    parked_  = false;  ///< something else owns the knob
	bool    above_   = false;  ///< which side of held_ the knob was on

	/// Take the knob away, latching `current` as the value to hold.
	void Park(int32_t current, int32_t knob)
	{
		if (parked_) return;
		held_   = current;
		above_  = (knob >= current);
		parked_ = true;
	}

	/// One control tick. Returns the value the owner should use.
	///
	/// `active` is false once the other duty has finished with the knob; the
	/// latch then persists only until the crossing happens.
	int32_t Update(bool active, int32_t knob)
	{
		if (active)
		{
			// Still parked. Keep tracking which side we are on, so letting go
			// and immediately nudging picks up from the right direction.
			if (parked_) above_ = (knob >= held_);
			return held_;
		}

		if (!parked_) return knob;

		// Waiting for the knob to come back through the latched value.
		const bool nowAbove = (knob >= held_);
		if (nowAbove == above_) return held_;    // still on the same side

		parked_ = false;                         // crossed: resume following
		return knob;
	}

	bool Parked() const { return parked_; }
};

} // namespace

// ===========================================================================

class NibbleKoCard : public ComputerCard
{
public:
	NibbleKoCard()
	{
		// NOTHING that touches hardware may happen here. This constructor runs
		// during ComputerCard's own construction, before Run() has set the
		// peripherals up, and heavy work here wedges the chip — a lesson paid
		// for in all three sibling cards. Plain field initialisation only.
		levels_.InitDefault();
	}

	virtual void __not_in_flash_func(ProcessSample)() override
	{
		// ---- An upload is starting: park HERE, in RAM, and never return --
		//
		// Writing flash drops XIP, and core 0 cannot survive that: this
		// function is RAM-resident but its CALLER is not — ComputerCard's
		// AudioCallback and BufferFull live in flash, and the CV output runs a
		// second flash-resident ISR (PWM_IRQ_WRAP). Any of them executing when
		// the erase begins is a hard fault.
		//
		// So core 0 stops inside this function, mutes the outputs, says it has
		// arrived, and spins until the reboot. It NEVER RETURNS, so the
		// flash-resident caller never runs again. Masking DMA_IRQ_0 alone is
		// NOT sufficient and that mistake has hung this family of cards before
		// — see docs/LESSONS.md and webui.cpp's EnterUploadMode().
		//
		// There is no resumable version of this park, and the attempt to add
		// one hung the card: every flash write has to mask USBCTRL_IRQ, and
		// once it is masked TinyUSB cannot be resumed. See
		// webui.cpp's CommitHeaderAndReboot().
		if (WebUI::uploadMode)
		{
			AudioOut1(0);
			AudioOut2(0);
			PulseOut1(false);
			PulseOut2(false);
			WebUI::core0Parked = true;
			for (;;) tight_loop_contents();
		}

		// ---- Boot window ------------------------------------------------
		//
		// The switch is NOT readable straight away, and reads Down until it
		// settles: ComputerCard derives it from knobs[3], off a ~60Hz
		// smoothing filter starting at zero, and zero decodes as Down. So for
		// the first few milliseconds of EVERY boot the card reports Down
		// wherever the switch actually is.
		//
		// Latching on "Down seen at any point in the window" therefore latches
		// on every boot — both WorkshopZX and WorkshopBio shipped exactly that
		// bug. Take ONE reading once settled instead.
		if (bootPhase_ < kBootWindowSamples)
		{
			if (++bootPhase_ == kBootWindowSamples)
			{
				calibrateBoot_ = (SwitchVal() == Switch::Down);
				splash_        = kSplashSamples;
			}
			return;
		}

		// ---- Boot splash -------------------------------------------------
		if (splash_ > 0)
		{
			if (--splash_ == 0)
			{
				for (int i = 0; i < kNumLeds; i++) LedOff(i);

				// Only the ALT-boot (switch held Down through the settle
				// window) forces a fresh learn. A normal boot restores the
				// saved levels from flash and goes straight to Play — that is
				// the whole point of persisting them: the card is playable
				// within the splash, not after a ten-tap learn every time.
				//
				// A normal boot with NOTHING saved yet (a fresh card, before
				// its first-ever learn) has no calibration to restore, so it
				// falls back to Learn same as an explicit alt-boot — there is
				// no useful default to play on instead.
				if (!calibrateBoot_ && HaveSavedCalibration())
				{
					int32_t saved[kNumLevels];
					LoadSavedCalibration(saved);
					levels_.LearnFrom(saved);
					levels_.ResetHeld();
					ui_ = UiMode::Play;
				}
				else
				{
					EnterLearn();
				}

				// The switch may still be held Down from an alt-boot, with its
				// release still to come. Swallow that: otherwise the release
				// reads as a mode-select gesture the moment the splash clears,
				// and the hold itself would abort the learn it just started.
				// abortLatched_ starts true and is only cleared by a release,
				// which is what makes the second half safe.
				selectArmed_ = false;
				downTicks_   = 0;
			}
			else
			{
				// An uncalibrated CV out cannot track 1V/oct, and the player
				// should learn that from the card rather than by blaming their
				// own learn pass. Blink the splash instead of holding it.
				bool show = CVOutsCalibrated() || ((splash_ >> 12) & 1);
				for (int i = 0; i < kNumLeds; i++)
					LedOn(i, show && (((i & 1) == 1) == calibrateBoot_));
			}
		}

		// ---- Pulse edges: LATCH here, at the full 48kHz ------------------
		//
		// PulseIn1RisingEdge() is true for exactly ONE sample. Polling it from
		// the 3kHz control tick therefore caught it only when the edge
		// happened to land on the 1-in-16 sample the tick ran on — about 6% of
		// the time, which is why NIBBLE's external clock could never lock.
		// Latch it at audio rate and let the control tick consume the flag.
		if (PulseIn1RisingEdge()) pulse1Edge_ = true;

		// ---- Control tick ------------------------------------------------
		//
		// NOTE: this runs INLINE, inside a DMA interrupt handler. The divider
		// lowers the AVERAGE load but does NOT relax the deadline: on the
		// sample where it fires, everything below must still finish within
		// this one 20.83us slot.
		if (++ctrlDiv_ >= kCtrlDiv) ctrlDiv_ = 0;
		if (ctrlDiv_ == 0)      ControlTick();
		else if (ctrlDiv_ == 8) UiTick();

		// ---- Audio rate --------------------------------------------------
		AudioTick();

		PulseOut1(gateTimer_ > 0);
		if (gateTimer_ > 0) gateTimer_--;

		PulseOut2(clickTimer_ > 0);
		if (clickTimer_ > 0) clickTimer_--;

		// The glitch gates, same countdown idiom as the two above. Written
		// every sample rather than only on a change: CVOut1/2 are cheap
		// register writes and RAM-resident, unlike CVOutMillivolts() which
		// CLAUDE.md flags as flash-resident and unsafe in the hot loop.
		CVOut1(glitch1Timer_ > 0 ? kCvGateHigh : kCvGateLow);
		if (glitch1Timer_ > 0) glitch1Timer_--;

		CVOut2(glitch2Timer_ > 0 ? kCvGateHigh : kCvGateLow);
		if (glitch2Timer_ > 0) glitch2Timer_--;
	}

	/// Upload progress, driven from CORE 1 while core 0 is parked.
	///
	/// Must stay RAM-resident and touch nothing but the LED PWM registers:
	/// it runs during flash erases, with XIP down, so a call into anything
	/// flash-resident here would fault the only core still alive.
	///
	/// LEDs 0-3 count WebUI::stage in binary — the stages are numbered in
	/// webui.cpp, and seeing which one it stopped at is the only diagnostic
	/// available once USB is gone. LEDs 4-5 fill in as a coarse progress bar.
	///
	/// `progress` is passed in rather than read from the WebUI object because
	/// core 1 is the caller and already holds it; this keeps the card class
	/// from needing to know the USB stack exists.
	void __not_in_flash_func(ShowUploadProgress)(int32_t progress)
	{
		const uint8_t st = WebUI::stage;
		for (int i = 0; i < 4; i++)
			LedBrightness(i, (st & (1u << i)) ? kLedFull : 0);

		LedBrightness(4, progress > (kQ16One / 3)     ? kLedHalf : 0);
		LedBrightness(5, progress > (kQ16One * 2 / 3) ? kLedHalf : 0);
	}

private:
	// =======================================================================
	// Control rate
	// =======================================================================

	void __not_in_flash_func(ControlTick)()
	{
		if (pulse1Edge_)
		{
			pulse1Edge_ = false;
			loop_.ClockPulse();
		}

		ReadSwitch();

		if (ui_ == UiMode::Learn) { LearnTick(); return; }

		// --- level detection ---
		int8_t idx = kComboNone;
		LevelEvent ev = levels_.Step(CVIn1(), idx);

		// While the switch is Down the buttons are choosing a mode, not
		// playing. Pairs act on the Trigger immediately (see the file header
		// for why that is safe); singles are read as STATE when the switch is
		// released, so nothing is dispatched for them here.
		//
		// Only the BUTTON dispatch is suppressed. PlayControl() still runs, so
		// the loop keeps playing while you pick a mode — an early return here
		// froze the transport for as long as a finger was on the switch, which
		// is not something a performer would ever ask for mid-set.
		if (selectArmed_)
		{
			if (ev == LevelEvent::Trigger && idx >= kNumSingles)
				FireAction(kActionForPair[idx - kNumSingles]);
		}
		else if (ev == LevelEvent::Trigger)
		{
			FireCombo(idx);
		}

		PlayControl();
	}

	/// The switch, as a mode selector.
	///
	/// Down arms a selection and Middle/Up commits it — but only if the switch
	/// was down long enough to be deliberate, and only for a SINGLE. Pairs
	/// have already fired their action from ControlTick() by the time the
	/// release arrives, and must not also re-latch the mode on the way out.
	void __not_in_flash_func(ReadSwitch)()
	{
		Switch sw = SwitchVal();
		tapped_ = false;

		if (sw == Switch::Down)
		{
			if (!selectArmed_)
			{
				selectArmed_ = true;
				downTicks_   = 0;
				actionFired_ = false;
			}
			// Counts to the LONG threshold, not the select one. It used to
			// saturate at kSelectMinTicks, which made the two gestures below
			// indistinguishable — see the abort test.
			if (downTicks_ < kHoldTicks) downTicks_++;

			// HOLDING Down during a learn throws it away and starts again.
			//
			// The threshold is kHoldTicks (2s), NOT kSelectMinTicks (50ms).
			// At 50ms every tap aborted the calibration — reported from the
			// bench as "as soon as I tap the momentary switch I'm back to the
			// start". A tap is how you select a mode, so the abort has to be a
			// gesture a tap cannot reach.
			//
			// abortLatched_ makes it fire ONCE per press. Without it the abort
			// re-fires on every tick past the threshold, and since an abort
			// RESTARTS the learn that would loop: restart, abort, restart, for
			// as long as a finger rests on the switch.
			if (ui_ == UiMode::Learn && downTicks_ >= kHoldTicks
			 && !abortLatched_)
			{
				abortLatched_ = true;
				AbortLearn();
			}
			return;
		}

		abortLatched_ = false;

		if (!selectArmed_) return;
		selectArmed_ = false;

		// A TAP is a press released before the hold threshold. Firing it here,
		// on release, rather than on the way down is what keeps tap and hold
		// distinguishable on one control: beginning a 2s hold would otherwise
		// fire a capture on its way past, so every restart would also stamp a
		// spurious level into the table it was about to throw away.
		bool wasTap = (downTicks_ < kHoldTicks);

		if (ui_ == UiMode::Learn)
		{
			// In calibration the switch is the CAPTURE control: tap to store
			// the voltage of the combo being held. LearnTick consumes this.
			tapped_ = wasTap;
			return;
		}

		// Outside calibration the switch selects a mode. It never selects
		// anything during a learn — without the branch above, releasing the
		// switch mid-learn would quietly latch a mode from whichever combo the
		// current learn STEP was sitting on, so the mode you came out in was
		// decided by where the calibration got to.

		// Too brief to be meant, or a pair already consumed this gesture.
		if (downTicks_ < kSelectMinTicks || actionFired_) return;

		// Commit whatever level the CV is SITTING ON, not a transition. This
		// is the half of the design that survives the CV already resting on
		// the button you are trying to pick — see the file header.
		int8_t cur = levels_.Current();
		if (cur >= 0 && cur < kNumSingles) SetMode(kModeForSingle[cur]);
	}

	void SetMode(Mode m)
	{
		if (m == mode_) return;
		mode_ = m;

		// Leaving a mode drops anything it was holding, so an effect cannot be
		// left stuck on by switching away mid-press.
		fxHeld_ = 0;

		// Entering the WebUI hands the card to USB, permanently: core 1 is
		// waiting on this flag, brings TinyUSB up when it flips, and stays
		// there. The card does not appear on USB at all until this point,
		// which is the whole reason USB is modal — an uninitialised stack
		// arms no flash-resident interrupt to compete with the audio path.
		//
		// One-way. The way back is MSG_PLAY from the browser, which reboots.
		if (m == Mode::WebUi)
		{
			playing_ = false;
			recording_ = false;
			WebUI::usbMode = true;
		}
	}

	/// One of the switch+pair actions.
	void __not_in_flash_func(FireAction)(Action a)
	{
		actionFired_ = true;
		switch (a)
		{
		case Action::PlayStop:
			playing_ = !playing_;
			break;

		case Action::Undo:
			// Puts back whatever the pattern was before the last time record
			// was armed. Fails silently if there is nothing to go back to,
			// which is the honest response — there is no state to show for
			// "you have already undone that".
			undoFlash_ = loop_.Undo() ? kUndoFlashTicks : 0;
			break;

		case Action::Quantise:
			// Cycles the RECORD grid (16th -> 12th -> 8th -> 16th), which
			// applies to hits recorded from now on. Anything already in the
			// loop keeps the grid it was captured against — see looper.h's
			// QuantGrid, and note this is NOT a one-shot "snap these hits"
			// action. quantFlash_ holds the LED readout for a moment so the
			// landed grid can be read off LEDs 4 and 5.
			quantGrid_    = loop_.CycleQuantGrid();
			quantFlash_   = kQuantFlashTicks;
			break;

		case Action::EnterWebUi:
			SetMode(Mode::WebUi);
			break;

		case Action::None:
		default:
			// A reserved pair. Deliberately silent rather than doing something
			// arbitrary — an unassigned gesture that acts is worse than one
			// that does not.
			break;
		}
	}

	/// A genuine new press from the level detector, dispatched by mode.
	void __not_in_flash_func(FireCombo)(int8_t combo)
	{
		if (combo < 0 || combo >= kNumLevels) return;

		switch (mode_)
		{
		case Mode::Drums:   DrumsPress(combo);   break;
		case Mode::Mute:    MutePress(combo);    break;
		case Mode::Pattern: PatternPress(combo); break;
		// FX are momentary, so they are read as state in FxUpdate() rather
		// than on this edge.
		case Mode::Fx:
		case Mode::WebUi:
		default:                                 break;
		}
	}

	// --- DRUMS ----------------------------------------------------------

	void __not_in_flash_func(DrumsPress)(int8_t combo)
	{
		// A single button is a SHIFT, not a sound.
		//
		// Percussion wants repeated hits on the same instrument, and that is
		// exactly what a keyboard reading of the buttons cannot give you: to
		// play AC twice you must pass through C, and if C is itself a sound
		// then every repeat is interrupted by a spurious one. Making the four
		// singles silent turns them into bank-selects that can be HELD — hold
		// C and tap A, B or D, over and over.
		if (combo < kNumSingles) { FlashCombo(combo); return; }

		// A pair. WHICH pair is not enough — hold-A-tap-B and hold-B-tap-A
		// close the same two switches and produce an identical voltage, but
		// they are different gestures and get different sounds. The shift
		// button recovers the ordering the voltage threw away, doubling the
		// kit from six voices to twelve. See LevelTracker::Shift().
		int8_t shift = levels_.Shift();
		int8_t tap   = OtherMember(combo, shift);
		int8_t voice = VoiceForGesture(shift, tap);

		// No shift latched: the pair was reached without passing through one
		// of its own buttons — from another pair, or as the first press after
		// a calibration. Pick the voice either member would give rather than
		// going silent.
		if (voice < 0)
		{
			const uint8_t *m = kPairMembers[combo - kNumSingles];
			voice = VoiceForGesture(static_cast<int8_t>(m[0]),
			                        static_cast<int8_t>(m[1]));
		}
		if (voice < 0) return;

		TriggerVoice(voice);

		// The loop records the VOICE, not the gesture. A pattern is a list of
		// sounds — "kick", "snare" — and how each one was played is a property
		// of the performance. It also means re-arranging the gesture map later
		// cannot silently change what an existing loop plays.
		if (recording_) loop_.RecordHit(voice, 100);

		FlashCombo(combo);
	}

	/// Fire a voice, unless its mute group is silenced.
	///
	/// Muting is applied HERE, at the point of sounding, and never touches the
	/// loop's stored events — un-muting instantly restores exactly what was
	/// recorded. Destroying events to mute them would violate the same
	/// principle that keeps overdub lossless.
	void __not_in_flash_func(TriggerVoice)(int8_t voice)
	{
		if (voice < 0 || voice >= kNumVoices) return;

		const uint8_t grp = MuteGroupOf(voice);

		// Flash the group whether or not it sounds. Mute mode shows a muted
		// group's hits DIMMER rather than hiding them, so you can see the
		// pattern you have silenced still going past — which is what makes it
		// possible to time bringing it back in.
		groupFlash_[grp] = kCtrlRate / 8;

		if (muted_ & (1u << grp)) return;

		// The D-bank transport effects are armed HERE rather than in the audio
		// path, because both change how a voice is produced: reverse starts it
		// at the far end, tape-stop starts it with a rate that will wind down.
		//
		// This is what catches hits that START while the effect is held —
		// KO/PO-12 behaviour, and the only way REVERSE can work at all, since
		// it has to know where a recording ends. Tape stop ALSO reaches back
		// and brakes voices already sounding; that happens on the effect's
		// rising edge in PlayControl(), not here. See DrumKit::TapeStopAll().
		const Fx tfx = fx_.TriggerFx();
		const bool reverse = (tfx == Fx::Reverse);
		int32_t stopSamples = 0;
		if (tfx == Fx::TapeStop)
		{
			// The knob sets how long the fall takes: a fast brake at one end,
			// a long wind-down at the other. Read from D's own parameter lane
			// so a recorded curve drives it exactly as it drives everything
			// else — see FxSlotDepth.
			stopSamples = kTapeStopMin
			            + ((FxSlotDepth(kD) * kTapeStopRange) >> 12);
		}

		drums_.TriggerVoice(voice, toneKnob_, reverse, stopSamples);
		gateTimer_ = kGateSamples;
	}

	/// Which mute group a voice belongs to.
	///
	/// TODO(webui): this is assigned per voice from the browser and stored in
	/// flash. Until that exists, split the kit three ways by index so the mode
	/// is testable on hardware: 0-3 low, 4-7 hats/metal, 8-11 the rest.
	static uint8_t MuteGroupOf(int8_t voice)
	{
		if (voice < 4) return 0;
		if (voice < 8) return 1;
		return 2;
	}

	// --- MUTE -----------------------------------------------------------

	void __not_in_flash_func(MutePress)(int8_t combo)
	{
		// HOLD B, TAP A/C/D. The mode's own button is the shift.
		//
		// A bare press cannot work here, and the reason is the same one that
		// makes the singles shifts in DRUMS: to press A twice you must pass
		// back through the rest voltage, so a bare-press toggle can mute a
		// group but can never UNMUTE it — the second press is unreachable.
		// Reported from the bench exactly that way.
		//
		// Holding the shift makes each tap a PAIR, which is a fresh trigger
		// every time (the ghost rule silences the release back onto the shift,
		// so tap-tap-tap on one group toggles it as many times as you like).
		if (combo < kNumSingles) return;         // bare press: nothing

		int8_t slot = SlotForShiftedTap(combo, kB);
		if (slot < 0) return;

		muted_ ^= static_cast<uint8_t>(1u << slot);
		FlashCombo(combo);

		// MUTES ARE DELIBERATELY NOT RECORDED, and this is a decision rather
		// than an unfinished edge.
		//
		// A mute is a MIXER move, not part of the music: it says "not this
		// group, right now", which is about the arrangement you are playing
		// rather than the pattern you wrote. Recording it would bake a live
		// judgement into the loop, so every subsequent pass would re-mute for
		// you and you would have to fight the recording to change your mind.
		//
		// The corollary, and the reason this matters: `muted_` is card state
		// rather than loop state, so it PERSISTS ACROSS PATTERN RECALLS. Drop
		// the hats and swap patterns and the hats stay dropped, which is what
		// makes mutes usable as an arrangement layer sitting above the
		// patterns rather than something each pattern carries its own copy of.
	}

	/// Which 0..2 slot a "hold `shift`, tap the other one" gesture names.
	///
	/// The three usable taps are the singles OTHER than the shift, in panel
	/// order — so in Mute mode (shift B) A/C/D are groups 0/1/2, and in FX
	/// bank 1 (shift C) A/B/D are effects 0/1/2. Returns -1 if `combo` is not
	/// a pair containing `shift`, or if the shift is not actually held.
	///
	/// Deliberately derived from the shift rather than tabulated: the mapping
	/// is "the remaining buttons, in order", and writing that as data would
	/// need a table per mode that could drift out of step with the mode list.
	int8_t SlotForShiftedTap(int8_t combo, int8_t shift) const
	{
		if (combo < kNumSingles || combo >= kNumLevels) return -1;

		// The gesture is only this one if the shift really was the held
		// button. LevelTracker::Shift() is what recovers that — the voltage
		// for B+A is identical whichever went down first.
		if (levels_.Shift() != shift) return -1;

		int8_t tap = OtherMember(combo, shift);
		if (tap < 0) return -1;

		// Rank the tap among the three non-shift singles, in panel order.
		int8_t slot = 0;
		for (int8_t i = 0; i < kNumSingles; i++)
		{
			if (i == shift) continue;
			if (i == tap)   return slot;
			slot++;
		}
		return -1;
	}

	/// The shift button for the current mode: the one that selected it.
	int8_t ModeShift() const
	{
		switch (mode_)
		{
		case Mode::Mute: return kB;
		case Mode::Fx:   return kC;
		default:         return kComboNone;
		}
	}

	// --- FX -------------------------------------------------------------

	// Effects are MOMENTARY — held, not toggled — so unlike mutes they are not
	// driven from the trigger edge at all. FxUpdate() below reads the held
	// state every control tick. Nothing to do on a press.
	//
	// TODO(step 5/6): actually apply the effect. Bank 2 (timing:
	// flam/stutter/triplet) acts on the loop's firing; bank 1 (audio:
	// reverse/tape-stop/pitch) needs the PCM backend that drums.h still lists
	// as TODO. Until then this only lights the LED, which is enough to test
	// the gesture routing on hardware.
	//
	// IDEA, not yet decided: a mode currently has ONE shift (its own button)
	// and three taps, so three effects. But the shift need not be fixed —
	// Shift() already tells hold-C-tap-A from hold-A-tap-C, and DRUMS gets its
	// twelve voices from exactly that. Allowing all four singles as shifts
	// would give TWELVE effects per mode for no new detection: this function
	// already takes the shift as a parameter, so it becomes a
	// kGestureVoice-style [shift][tap] table lookup.
	//
	// Two things to settle first, neither technical: whether twelve effects
	// per bank is more than a player can hold in their head, and whether FX2
	// should then exist at all — if FX1 alone holds twelve, switch+D is free
	// for something else. Also note the mode-reminder LED pulses the mode's
	// single shift button, which stops meaning anything with four of them.

	/// The MAIN knob, or CV In 2 if something is patched there.
	///
	/// CV In 2 overrides the knob entirely rather than offsetting it — the
	/// same convention Pulse In 1 already uses for the tempo knob, so "a
	/// cable wins" is one rule across the card rather than two.
	///
	/// Every Main read goes through here, and that matters: Main means
	/// different things in different modes (DJ filter, or an effect's depth
	/// in FX), and those two paths diverge at the RAW read rather than after
	/// it. Without a single funnel the override would have had to be
	/// duplicated at each site, and the two would drift.
	///
	/// CV in is bipolar (-2048..2047), knobs are unipolar (0..4095), so a
	/// full-swing bipolar CV covers the knob's whole travel.
	int32_t __not_in_flash_func(MainVal)()
	{
		if (!Connected(Input::CV2)) return KnobVal(Knob::Main);
		int32_t v = CVIn2() + 2048;
		return (v < 0) ? 0 : (v > 4095) ? 4095 : v;
	}

	/// CHAOS, 0..4095. Audio In 2 if patched, else a deliberate trickle.
	///
	/// Unpatched sits at kChaosDefault rather than zero so CV Out 1/2 always
	/// do SOMETHING — a card whose glitch outputs are silent until you patch
	/// a control voltage into them looks broken. One in twenty divisions is
	/// sparse enough to read as an accent rather than a fault.
	int32_t __not_in_flash_func(ChaosVal)()
	{
		if (!Connected(Input::Audio2)) return kChaosDefault;
		int32_t v = AudioIn2() + 2048;
		return (v < 0) ? 0 : (v > 4095) ? 4095 : v;
	}

	/// The two glitch gate streams, one loop tick each.
	///
	/// Both roll against the SAME chaos value but ask different questions, so
	/// one is an accent track and the other is mayhem:
	///
	///   CV Out 1  sparse. Only strong divisions, weighted hard toward the
	///             downbeat, long gates. Musical on its own.
	///   CV Out 2  dense. Off-beats and finer subdivisions, short gates.
	///             This is the one to patch into Pulse In 2 for chaos.
	///
	/// Divisions come from the LIVE quantise grid, so the glitching always
	/// agrees with what the card is recording to — change the grid with
	/// switch+A+C and these follow. Chaos widens the pool as well as raising
	/// the odds: at the bottom only beats are candidates, at the top every
	/// half-division is, so more chaos is finer AND busier rather than just
	/// denser.
	///
	/// Gate width is a FRACTION OF THE BEAT, converted to samples here and
	/// counted down at 48kHz beside gateTimer_.
	///
	/// It was fixed milliseconds and that was wrong. A trigger should be
	/// tempo-independent, but these gates are not triggers: patched into
	/// Pulse In 2 their width IS how long each random effect is applied, so a
	/// 20ms gate applied an effect for a click and nothing was audible.
	/// Samples remain the COUNTING unit — loop ticks would make the countdown
	/// itself tempo-dependent twice over — but the length is now derived from
	/// Looper::SamplesPerBeat().
	void __not_in_flash_func(GlitchTick)()
	{
		const int32_t chaos = ChaosVal();
		const uint16_t pos  = loop_.Position();
		const int32_t spb   = loop_.SamplesPerBeat();

		// Ticks per grid division: 12 (16th), 16 (12th), 24 (8th).
		const int32_t div = kTicksPerBeat
		                  / kQuantNotesPerBeat[static_cast<int>(loop_.CurrentQuantGrid())];

		const bool onBeat     = (pos % kTicksPerBeat) == 0;
		const bool onDivision = (pos % div) == 0;
		const bool onHalfDiv  = (div >= 2) && ((pos % (div / 2)) == 0);

		// --- CV Out 1: sparse, beat-anchored ------------------------------
		//
		// Candidates are beats always, and other divisions only as chaos
		// rises. The downbeat gets a big weighting bonus so the stream keeps
		// implying the bar even when it is firing often.
		if (onBeat || (onDivision && chaos > kChaosDivisionOpens))
		{
			// Chaos IS the probability, not half of it: the spec's "1 in
			// twenty" has to mean 5% at the default or the stream is silent
			// for bars at a time. The ceiling widens the pool instead — see
			// kChaosDivisionOpens — so the knob still does more than get
			// louder.
			int32_t odds = chaos;
			if (onBeat)  odds += odds >> 1;                      // favour the beat
			if (pos == 0) odds += odds >> 1;                     // favour bar one

			// Never certain. Bar one at full chaos would otherwise hit every
			// single time, which turns the least predictable setting into a
			// metronome.
			if (odds > kGlitchMaxOdds) odds = kGlitchMaxOdds;

			// Sized against the interval until the NEXT candidate, which is a
			// whole beat while only beats are in play and one division once
			// chaos has opened them up. Sizing both cases against the beat
			// would leave the gate still high when the next division fired.
			const int32_t span = (chaos > kChaosDivisionOpens)
			                   ? (spb / kQuantNotesPerBeat[
			                        static_cast<int>(loop_.CurrentQuantGrid())])
			                   : spb;

			if (rand_q16(rng_) < ((odds * 65536) / 4096))
				glitch1Timer_ = (span * kGateLongNum) / kGateLongDen;
		}

		// --- CV Out 2: dense, syncopated ----------------------------------
		//
		// Deliberately SKIPS the beat itself: the sparse stream already owns
		// the downbeat, and a dense track that also lands there just thickens
		// it. Off-beats and half-divisions are what make it read as glitch.
		const bool candidate2 = onHalfDiv && !onBeat;
		if (candidate2)
		{
			// Same scale as the sparse stream, but every off-beat and
			// half-division is a candidate rather than only the strong ones,
			// so the same chaos value lands far more hits here.
			int32_t odds = chaos;
			if (odds > kGlitchMaxOdds) odds = kGlitchMaxOdds;
			// Ratchets: at high chaos a hit sometimes becomes a short burst,
			// which is the thing that actually sounds like a machine breaking
			// rather than like a busier pattern.
			// Candidates here are HALF-divisions, so that is the span to fit
			// inside.
			const int32_t half = spb
			                   / (kQuantNotesPerBeat[
			                        static_cast<int>(loop_.CurrentQuantGrid())] * 2);

			if (rand_q16(rng_) < ((odds * 65536) / 4096))
				glitch2Timer_ = (chaos > kChaosRatchetOpens
				              && (xorshift32(rng_) & 3u) == 0)
				              ? (half * kGateRatchetNum) / kGateRatchetDen
				              : (half * kGateShortNum)   / kGateShortDen;
		}
	}

	/// Depth for the RANDOM effect: Audio In 1 if patched, else the slot's
	/// own curve.
	///
	/// Audio In 1 is bipolar like every other input here, so it is folded to
	/// the same 0..4095 the knobs use. Using it as a slow CV is the intended
	/// case; feeding it actual audio makes the depth judder at signal rate,
	/// which is either a mistake or exactly what you wanted.
	int32_t __not_in_flash_func(RandomFxDepth)(int32_t fallback)
	{
		if (!Connected(Input::Audio1)) return fallback;
		int32_t v = AudioIn1() + 2048;
		return (v < 0) ? 0 : (v > 4095) ? 4095 : v;
	}

	/// One slot's parameter value: the hand's if that slot's shift is held,
	/// otherwise whatever the loop is replaying for it.
	///
	/// The single place depth is resolved, so the audio chain and the timing
	/// effects cannot disagree about it. They did: stutter read the raw Main
	/// knob, so drawing a curve under one shift re-rated a timing effect
	/// recorded under another.
	int32_t __not_in_flash_func(FxSlotDepth)(int8_t slot)
	{
		if (slot < 0 || slot >= kNumFxSlots) return 2048;
		return (slot == fxParShift_) ? MainVal()
		                             : fxParPlayback_[slot];
	}

	/// Which effect is held right now. Control rate.
	///
	/// ANY single may be the shift, not just the mode's own button: four
	/// shifts times three taps is twelve gestures, exactly as DRUMS gets
	/// twelve voices from the same four buttons. Shift() is what recovers
	/// which of the pair was held first, and the whole thing costs one table
	/// lookup — see fx.h.
	void __not_in_flash_func(FxUpdate)()
	{
		fxHeld_    = 0;
		fxCurrent_ = Fx::None;

		// Not while the switch is down — those presses are choosing a mode.
		// fxLiveSlot_ is already -1, so the caller falls through to playback.
		if (selectArmed_) return;

		if (mode_ == Mode::Fx)
		{
			// TWO gestures share the four buttons here, told apart by whether
			// a pair or a bare single is held:
			//
			//   shift + tap   run one of that shift's three effects
			//   shift alone   draw that shift's PARAMETER curve with Main
			//
			// The second needs no new control precisely because it is the
			// absence of the first — you are already holding the shift, so
			// letting go of the tap hands the knob to the curve.
			const int8_t combo = levels_.Current();

			if (combo >= 0 && combo < kNumSingles)
			{
				// A bare single: this shift's parameter is what Main writes.
				fxParShift_ = combo;
				return;
			}

			// CURRENT(), not Sounding() — an effect must die on release. See
			// the long note below, which is still the reason.
			const int8_t shift = levels_.Shift();
			if (combo >= kNumSingles && shift >= 0 && shift < kNumSingles)
			{
				const int8_t tap = OtherMember(combo, shift);
				if (tap >= 0)
				{
					fxCurrent_ = kFxForGesture[shift][tap];
					// Light the TAP's pad. The shift already shows itself as
					// the steady mode marker, so lighting it too would say the
					// same thing twice and lose which effect is running.
					if (fxCurrent_ != Fx::None)
						fxHeld_ = static_cast<uint8_t>(1u << tap);
				}

				// Main still writes the lane's curve while an effect is held,
				// so there is ONE source of truth for depth: what you hear
				// while performing is exactly what gets recorded, and the
				// effect reads the same curve whether it is live or replayed.
				fxParShift_ = shift;

				// The SHIFT names the slot, so which layer a gesture writes
				// to is fixed by how it is played rather than by which effect
				// it is.
				if (fxCurrent_ != Fx::None) fxLiveSlot_ = shift;
			}
			return;
		}

		// --- FX2: still the old single-shift form ------------------------
		//
		// Mode D keeps three effects on its own button as a PLACEHOLDER. It
		// is deliberately not expanded to twelve alongside FX1: if one bank
		// can hold twelve, the honest question is whether a second bank
		// should exist at all, or whether switch+D is better spent on
		// something else entirely. That is a decision to make once FX1's
		// twelve have been played, not before.
		//
		// CURRENT(), not Sounding(). This is the whole difference between an
		// effect and a hit, and getting it wrong broke two things at once.
		//
		// Sounding() reports the PAIR for as long as the ghost is armed — that
		// is, for as long as the CV sits on the shift after the tapping finger
		// has lifted. That is right for a drum, where the pair named a hit
		// that is still ringing. It is exactly wrong for an effect, which must
		// stop when you let go: the effect stayed latched after release, and
		// re-tapping the same button produced no new trigger to relatch it,
		// which is why bank C never registered D at all (release D, ghost back
		// to C, Sounding() still says CD, nothing ever changes).
		//
		// Current() follows the voltage itself, so the pair is reported only
		// while both buttons are genuinely down. Release the tap and it falls
		// to the bare shift, the slot lookup fails, and the effect ends —
		// which is the behaviour asked for.
		int8_t slot = SlotForShiftedTap(levels_.Current(), ModeShift());
		if (slot >= 0) fxHeld_ = static_cast<uint8_t>(1u << slot);

		// FX2 lights its pads and does nothing else — it has no effects of its
		// own yet, and deliberately so: FX1 now holds twelve, which makes
		// "should a second bank exist at all, or is switch+D better spent on
		// something else" a live question. Wiring three arbitrary effects in
		// here would answer it by accident. See the file header.
	}

	// --- PATTERN ---------------------------------------------------------

	/// Four pattern slots: HOLD D and tap A/B/C. The SWITCH says which verb.
	///
	///   switch Middle + D + tap    RECALL that slot, instantly
	///   switch Up     + D + tap    STORE the live loop into it
	///
	/// A hold-to-store gesture cannot work on this hardware, and the reason is
	/// the same quirk the whole card is built around: Four Voltages LATCHES.
	/// Hold A and the CV sits at A's level for as long as you like, so a
	/// "held for a second" test always passes eventually — every recall would
	/// have become a store. Timing the press is measuring the wrong thing.
	///
	/// Using the switch instead is both correct and consistent: Up already
	/// means "commit this to the loop" everywhere else on the card, and
	/// storing a pattern is the same idea one level up. It also costs no new
	/// gesture to learn.
	///
	/// Edge-triggered through the same shift-and-tap path as MUTE, so the
	/// ghost rule gives a fresh trigger every time and tapping one slot
	/// repeatedly works.
	void __not_in_flash_func(PatternPress)(int8_t combo)
	{
		if (combo < kNumSingles) return;          // bare press: nothing

		const int8_t slot = SlotForShiftedTap(combo, kD);
		if (slot < 0) return;

		// SwitchVal(), not recording_.
		//
		// recording_ is updated in PlayControl(), which runs AFTER FireCombo()
		// on the same tick — so this would read the PREVIOUS tick's value.
		// Harmless for anything that merely plays, and destructive here: a
		// stale-true reading turns a RECALL into a STORE, silently
		// overwriting the slot you meant to recall with whatever is playing.
		// If that happened to be a short or empty loop, recalling the slot
		// afterwards sounded like the pattern had been muted.
		//
		// Reading the switch directly removes the ordering dependency
		// altogether rather than moving the assignment and hoping.
		if (SwitchVal() == Switch::Up)
		{
			// Switch Up: commit. What you stored is what is now playing.
			// An empty loop is refused rather than wiping the slot, and the
			// confirmation flash only fires if something was actually saved.
			if (loop_.StorePattern(slot))
			{
				patLive_       = slot;
				patLastStored_ = slot;
				patStoreFlash_ = kPatternFlashTicks;
			}
		}
		else
		{
			// Switch Middle: recall. An empty slot is left alone rather than
			// clearing the loop — silently wiping what you are playing is not
			// something a tap should ever do.
			if (loop_.RecallPattern(slot)) patLive_ = slot;
		}
		FlashCombo(combo);
	}

	// --- shared control -------------------------------------------------

	void __not_in_flash_func(PlayControl)()
	{
		// Arming or releasing record re-seeds the knob references, so the
		// first sample after the transition cannot be mistaken for a move.
		bool nowRecording = (SwitchVal() == Switch::Up);
		if (nowRecording != recording_)
		{
			loop_.ArmKnobs();

			// Take the undo snapshot on the way IN, so what Undo restores is
			// the pattern as it stood before this pass touched it. On the way
			// out would snapshot the result instead, which undoes nothing.
			if (nowRecording) loop_.Snapshot();
		}
		recording_ = nowRecording;

		// FX are momentary: an effect lasts exactly as long as its button is
		// held, so it is read as STATE every tick rather than latched on an
		// edge.
		fxLiveSlot_ = -1;
		fxParShift_ = -1;
		if (mode_ == Mode::Fx) FxUpdate();

		if (patStoreFlash_ > 0) patStoreFlash_--;

		// (A recorded effect expires in LOOP ticks, counted down where the
		// loop advances — see kFxPlaybackHold.)

		// Each slot resolves independently: THE HAND WINS on the slot it is
		// holding, and every other slot keeps replaying whatever was recorded
		// under its shift. That is what makes the layers layer — holding a
		// crush under B does not silence the gate recorded under D.
		//
		// Depth comes from the slot's own PARAMETER curve, whichever way the
		// effect arrived. One source of truth: a replayed effect sweeps
		// exactly as it did when performed, and a live one reads the same
		// curve the hand is drawing.
		// PULSE IN 2 fires a random effect for as long as it is HIGH.
		//
		// Level-sensitive, not edge-triggered: the incoming gate's WIDTH is
		// the effect's duration, which is what makes CV Out 1 -> Pulse In 2
		// work as a self-glitching patch. The effect is rolled once on the
		// rising edge and held — re-rolling every tick would be noise rather
		// than a glitch.
		if (Connected(Input::Pulse2))
		{
			const bool now = PulseIn2();
			if (now && !randomFxOn_)
				randomFxEffect_ = RandomFx(rng_, randomFxSlot_);
			else if (!now)
				randomFxEffect_ = Fx::None;
			randomFxOn_ = now;
		}
		else
		{
			randomFxEffect_ = Fx::None;
			randomFxOn_ = false;
		}

		for (int8_t s = 0; s < kNumFxSlots; s++)
		{
			const int32_t depth = FxSlotDepth(s);

			// The random effect sits BELOW both the hand and playback, so a
			// patched Pulse In 2 colours the slots nobody is using rather
			// than fighting for one. Its depth comes from Audio In 1 when
			// that is patched, else from the slot's own curve like anything
			// else.
			const bool randomHere = (randomFxEffect_ != Fx::None && s == randomFxSlot_);

			if (s == fxLiveSlot_)
				fx_.SetSlot(s, fxCurrent_, depth);
			else if (fxPlayback_[s] != 0)
				fx_.SetSlot(s, static_cast<Fx>(FxOf(fxPlayback_[s])), depth);
			else if (randomHere)
				fx_.SetSlot(s, randomFxEffect_, RandomFxDepth(depth));
			else
				fx_.SetSlot(s, Fx::None, 0);
		}

		// TAPE STOP reaches back and brakes what is already ringing, on the
		// RISING EDGE of the effect. Edge, not level: SetTapeStop() installs a
		// fresh full-scale ramp each call, so doing this every tick would keep
		// resetting the wind-down and the kit would never actually stop.
		//
		// Hits that start DURING the hold are handled separately, in
		// TriggerVoice() — between the two, holding D+B brakes everything.
		{
			const bool stopNow = (fx_.TriggerFx() == Fx::TapeStop);
			if (stopNow && !tapeStopWasOn_)
				drums_.TapeStopAll(kTapeStopMin
				                 + ((FxSlotDepth(kD) * kTapeStopRange) >> 12));
			tapeStopWasOn_ = stopNow;
		}

		loop_.SetTempo(KnobVal(Knob::X));

		// Each lane decides for itself whether the hand or the recording is
		// driving this tick. Moving the knob mutes that lane's playback while
		// you move it and for a moment after; letting go hands it back.
		const int32_t mainVal = filterLane_.Update(MainVal());
		const int32_t toneVal = toneLane_.Update(KnobVal(Knob::Y));

		// In FX1 the Main knob is the effect's DEPTH, so it must not also be
		// sweeping the DJ filter — the filter latches where it was and only
		// starts following again once the knob is turned back through that
		// value. See SoftPickup: snapping it to wherever the knob happened to
		// end up would jump the filter at the exact moment you leave the mode.
		const bool mainIsDepth = (mode_ == Mode::Fx);
		if (mainIsDepth) filterPickup_.Park(djFilterVal_, mainVal);
		djFilterVal_ = filterPickup_.Update(mainIsDepth, mainVal);

		djFilter_.SetKnob(djFilterVal_);
		toneKnob_ = toneVal;

		// Record only what the HAND is doing. Recording the value that came
		// out of the lane would re-record playback on top of itself.
		//
		// The filter lane is silenced while FX1 owns the Main knob: that
		// movement is the effect's depth, and writing it to the filter lane
		// would replay a filter sweep that never actually happened. The
		// effect itself is recorded instead, as a PARAMETER — see below.
		if (recording_)
		{
			// Only the slot the hand is holding writes. The other three keep
			// whatever earlier passes put in them, which is exactly why there
			// are four lanes instead of one.
			uint8_t fxPacked[kNumFxSlots] = {};
			if (fxLiveSlot_ >= 0)
				fxPacked[fxLiveSlot_] = PackFx(static_cast<uint8_t>(fxCurrent_));

			// The parameter lane writes only when a SHIFT is held. Without one
			// the knob is ambiguous — there are four parameter curves and
			// nothing says which you meant — so turning it in FX1 with no
			// button down deliberately does nothing at all. fxParShift_ is
			// -1 then, and RecordKnobs ignores the value.
			loop_.RecordKnobs(filterLane_.HandOwns() && !mainIsDepth,
			                  MainVal(),
			                  toneLane_.HandOwns(), KnobVal(Knob::Y),
			                  fxPacked,
			                  fxParShift_, MainVal(),
			                  filterLane_.HandOwns() && fxParShift_ >= 0);
		}

		if (!playing_) return;

		// The rhythmic effects schedule EXTRA hits (stutter, flam); they do
		// not move the playhead.
		//
		// Half-time and double-time used to, by skipping or doubling this
		// Advance() call, and that is why they are gone: engaging one off-beat
		// displaced the loop's phase permanently, because releasing simply
		// carried on from wherever the playhead had reached. Nothing pulled it
		// back. Every effect on the card is now a filter on the way out rather
		// than something that can desynchronise the pattern.
		const FxTiming timing = fx_.Timing();

		if (loop_.Advance())
		{
			// A replayed effect expires in LOOP ticks, so it is aged HERE
			// rather than on the control tick — see kFxPlaybackHold for why
			// the distinction matters across the tempo range.
			for (int8_t s = 0; s < kNumFxSlots; s++)
				if (fxPlaybackTicks_[s] > 0 && --fxPlaybackTicks_[s] == 0)
					fxPlayback_[s] = 0;

			// Pulse Out 2 is a CLICK TRACK: one blip per crotchet, so you have
			// something to record along to. Driven from BeatEdge() rather than
			// OnBeat() — the latter is a level that stays true for the whole
			// tick, which at 40bpm is dozens of control steps and would give a
			// click that is on more than it is off.
			if (loop_.BeatEdge()) clickTimer_ = kClickSamples;

			GlitchTick();

			int8_t  voices[Looper::kMaxFirePerTick];
			uint8_t vel[Looper::kMaxFirePerTick];
			int32_t knob[kNumLanes] = {};
			bool    haveKnob[kNumLanes] = {};

			int n = loop_.Fire(voices, vel, knob, haveKnob);

			// Hand the recorded values to the lanes. Whether they are actually
			// USED is the lane's decision — if the player is moving that knob,
			// this is remembered but muted until they let go.
			if (haveKnob[kLaneFilter]) filterLane_.Playback(knob[kLaneFilter]);
			if (haveKnob[kLaneTone])   toneLane_.Playback(knob[kLaneTone]);

			// The FX lanes are LEVELS, not edges: an effect is recorded for
			// every tick it was held, so a tick that carries no event for a
			// lane means that lane's effect had stopped. Latch what arrives
			// and let it expire, so the gap between recorded samples does not
			// chatter the effect on and off at the sampling rate.
			for (int8_t s = 0; s < kNumFxSlots; s++)
			{
				const uint8_t lane = FxLaneForShift(s);
				if (haveKnob[lane])
				{
					fxPlayback_[s]      = static_cast<uint8_t>(knob[lane] >> 4);
					fxPlaybackTicks_[s] = kFxPlaybackHold;
				}

				// The PARAMETER lane does not expire the way the effect lane
				// does. A curve is a position, not a hold — it stays wherever
				// it was last set until the next recorded point moves it,
				// exactly as the filter and tone lanes behave.
				const uint8_t par = ParLaneForShift(s);
				if (haveKnob[par]) fxParPlayback_[s] = knob[par];
			}

			for (int i = 0; i < n; i++)
			{
				TriggerVoice(voices[i]);
				// A looped hit knows its VOICE, not which buttons would have
				// played it. Flash the pads that gesture uses, looked up from
				// the map, so playback lights what the performance did.
				FlashVoice(voices[i]);

				// FLAM: schedule a second strike of the same voice a few ms
				// later. One repeat, not a roll — a flam is a grace note.
				if (timing == FxTiming::Flam && flamCount_ == 0)
				{
					flamVoice_ = voices[i];
					// The gap is the rhythmic lane's parameter: a tight
					// thickening at one end, a distinct doubled hit at the
					// other. It used to be a fixed 13ms, which left C+B with
					// a parameter lane that did nothing.
					flamTicks_ = kFlamMin
					           + ((FxSlotDepth(kFxRhythmSlot) * kFlamRange) >> 12);
					flamCount_ = 1;
				}
			}

			// STUTTER: re-fire the most recent hit at a fixed division, so a
			// held button turns whatever just played into a roll. Remembered
			// per tick rather than per hit so a chord stutters its last voice
			// rather than all of them at once.
			if (n > 0) lastVoice_ = voices[n - 1];
		}

		// Deferred strikes: the flam's second hit, and the stutter's repeats.
		// Both run at control rate, outside the Advance() branch, because they
		// are measured in real time rather than in loop ticks — a flam that
		// scaled with tempo would stop being a flam at 40bpm.
		if (flamTicks_ > 0 && --flamTicks_ == 0)
		{
			TriggerVoice(flamVoice_);
			FlashVoice(flamVoice_);
			flamCount_ = 0;
		}

		if (timing == FxTiming::Stutter && lastVoice_ >= 0)
		{
			if (--stutterTicks_ <= 0)
			{
				// The division comes from the TIMING SLOT's own parameter
				// curve, not from the live knob.
				//
				// It used to read filterLane_.Value() — the raw Main knob,
				// whichever shift was held — so drawing a distortion curve
				// under shift B silently re-rated the stutter under shift C
				// as well. The timing effects were bypassing the per-slot
				// parameter system that the audio effects already used.
				stutterTicks_ = kStutterMin
				              + ((4095 - FxSlotDepth(kFxRhythmSlot))
				                 * kStutterRange >> 12);
				TriggerVoice(lastVoice_);
				FlashVoice(lastVoice_);
			}
		}
		else
		{
			stutterTicks_ = 0;   // re-arm, so the next stutter starts at once
		}
	}

	// =======================================================================
	// Learn
	// =======================================================================

	void EnterLearn()
	{
		ui_          = UiMode::Learn;
		learnStep_   = 0;
		learnPhase_  = LearnPhase::Waiting;
		collisionsThisLearn_ = 0;
		learnTimer_  = kLearnTimeoutTicks;
		phaseTimer_  = 0;

		// Recalibrating CLEARS the loop. The levels are about to change, so
		// the combos an existing pattern refers to may not survive — and a
		// pattern that plays the wrong drums is worse than no pattern.
		loop_.Clear();
		filterLane_.Forget();
		toneLane_.Forget();
	}

	/// Throw away the captures in progress.
	///
	/// This only ANNOUNCES the abort; LearnTick() decides what happens next
	/// once the LED flash has played out — exit to Play on the last SAVED
	/// calibration if one exists, or restart the learn if this card has never
	/// completed one. Deciding here directly would skip the flash entirely,
	/// and that flash is the only feedback that the gesture worked.
	void AbortLearn()
	{
		if (learnPhase_ == LearnPhase::Aborted) return;
		learnPhase_ = LearnPhase::Aborted;
		phaseTimer_ = kCaptureFlashTicks * 2;
	}

	void __not_in_flash_func(LearnTick)()
	{
		// Keep the detector running so Settled()/SettledValue() are live.
		int8_t idx = kComboNone;
		LevelEvent ev = levels_.Step(CVIn1(), idx);

		if (phaseTimer_ > 0)
		{
			if (--phaseTimer_ == 0 && learnPhase_ != LearnPhase::Waiting)
			{
				// Only a SUCCESSFUL learn leaves calibration. Confirm and
				// Collision are per-step feedback and fall back to waiting.
				//
				// Failed and Aborted exit to Play on the calibration ALREADY
				// SAVED in flash, if one exists — aborting means "keep what I
				// had", a real choice now that there is something to keep.
				// With nothing saved yet (a card's very first-ever learn, or
				// one that failed before completing once), there is still
				// nowhere useful to exit to: the evenly-spaced default cannot
				// actually be played, so restart is the only sensible
				// destination in that case only.
				if (learnPhase_ == LearnPhase::Done)
				{
					// Drop whatever combo the learn left "held", or the first
					// real press could match it and be swallowed as no-change.
					levels_.ResetHeld();
					ui_ = UiMode::Play;
				}
				else if (learnPhase_ == LearnPhase::Failed
				      || learnPhase_ == LearnPhase::Aborted)
				{
					if (HaveSavedCalibration())
					{
						int32_t saved[kNumLevels];
						LoadSavedCalibration(saved);
						levels_.LearnFrom(saved);
						levels_.ResetHeld();
						ui_ = UiMode::Play;
					}
					else
					{
						EnterLearn();
					}
				}
				else
				{
					learnPhase_ = LearnPhase::Waiting;
					learnTimer_ = kLearnTimeoutTicks;
				}
			}
			return;
		}

		if (--learnTimer_ <= 0) { AbortLearn(); return; }

		// A capture is confirmed by TAPPING THE SWITCH while holding the
		// combination, as NIBBLE does.
		//
		// This build briefly tried to capture on the button press itself, to
		// leave the switch free — and that cannot work, because the press and
		// the voltage arriving are the SAME event. The settle detector has by
		// definition not had time to trust the reading yet, so every capture
		// either raced the slew or was rejected as unsettled.
		//
		// Holding the combo and then tapping separates the two: you hold, the
		// voltage settles, and the tap says "now". That is the whole reason
		// NIBBLE's calibration works this way.
		if (!tapped_) return;
		(void)ev;
		(void)idx;

		// A tap that arrived mid-transition would capture a voltage the
		// player is not actually holding. Reject it and say so, rather than
		// silently recording a number from the middle of a slew.
		if (!levels_.Settled())
		{
			learnPhase_ = LearnPhase::NotSettled;
			phaseTimer_ = kNotSettledTicks;
			return;
		}

		captured_[kLearnOrder[learnStep_]] = levels_.SettledValue();

		// Warn if this level is too close to one already taken. The capture is
		// still KEPT: a learn that completes with a warning is far more useful
		// than one that refuses, and the player can move the Four Voltages
		// knob and run it again.
		bool collided = false;
		for (int j = 0; j < learnStep_; j++)
		{
			int32_t d = captured_[kLearnOrder[learnStep_]]
			          - captured_[kLearnOrder[j]];
			if (d < 0) d = -d;
			if (d < kCollisionMin) collided = true;
		}
		// Counted for every step INCLUDING the last, which is why this sits
		// above the branch. The final step gets no per-step flash of its own,
		// but it still has to be counted or the end-of-learn warning would
		// under-report a collision on the tenth capture.
		if (collided) collisionsThisLearn_++;

		learnStep_++;
		if (learnStep_ >= kNumLevels)
		{
			// Refuse a degenerate calibration rather than installing it. Ten
			// captures that all landed on the same voltage means nothing was
			// patched in — accepting that gives a card that looks calibrated
			// and plays one note forever.
			int32_t lo = captured_[0], hi = captured_[0];
			for (int i = 1; i < kNumLevels; i++)
			{
				if (captured_[i] < lo) lo = captured_[i];
				if (captured_[i] > hi) hi = captured_[i];
			}

			if (hi - lo < kMinLearnSpan)
			{
				learnPhase_ = LearnPhase::Failed;
				phaseTimer_ = kFailFlashTicks;
			}
			else
			{
				levels_.LearnFrom(captured_);
				SaveCalibration(captured_, levels_.CollisionCount());
				learnPhase_ = LearnPhase::Done;
				phaseTimer_ = kDoneFlashTicks;
			}
		}
		else
		{
			learnPhase_ = collided ? LearnPhase::Collision : LearnPhase::Confirm;
			phaseTimer_ = collided ? kCollisionFlashTicks : kCaptureFlashTicks;
		}
	}

	// =======================================================================
	// Audio rate
	// =======================================================================

	void __not_in_flash_func(AudioTick)()
	{
		int32_t dry = drums_.Step();
		int32_t wet = djFilter_.Step(dry);

		// The performance effect sits AFTER the DJ filter, so it has the last
		// word — a gate or a mute must be able to cut whatever the filter is
		// passing, not be smoothed back open by it.
		wet = fx_.Step(wet);

		int16_t out = clamp12(wet);

		// Mono bus to both outs: the kit is not panned, and a player patching
		// one output should get the whole kit rather than half of it.
		AudioOut1(out);
		AudioOut2(out);

		// CV Out 1 and 2 are unassigned. NIBBLE put a bassline on them; this
		// card deliberately does not. Left idle rather than given a job
		// nobody asked for — see the plan's open questions.
	}

	// =======================================================================
	// LEDs
	// =======================================================================

	/// Flash every LED belonging to a combo.
	///
	/// Note this masks the COMBO through ComboLedMask rather than indexing
	/// ledFlash_ by the combo directly: singles happen to map to themselves,
	/// but every pair would light the wrong pad — BC (index 7) would light
	/// LED 3, button D, which is not even in the combo.
	void __not_in_flash_func(FlashCombo)(int8_t combo)
	{
		uint8_t mask = ComboLedMask(combo);
		for (int i = 0; i < 4; i++)
			if (mask & (1u << i)) ledFlash_[i] = kCtrlRate / 8;
	}

	/// Flash the pads for a recorded VOICE, by finding a gesture that plays it.
	void __not_in_flash_func(FlashVoice)(int8_t voice)
	{
		for (int sh = 0; sh < kNumSingles; sh++)
			for (int tp = 0; tp < kNumSingles; tp++)
				if (VoiceForGesture(static_cast<int8_t>(sh),
				                    static_cast<int8_t>(tp)) == voice)
				{
					ledFlash_[sh] = kCtrlRate / 8;
					ledFlash_[tp] = kCtrlRate / 8;
					return;
				}
	}

	void __not_in_flash_func(UiTick)()
	{
		if (splash_ > 0) return;

		for (int i = 0; i < 4; i++)
			if (ledFlash_[i] > 0) ledFlash_[i]--;
		for (int i = 0; i < kNumMuteGroups; i++)
			if (groupFlash_[i] > 0) groupFlash_[i]--;
		if (undoFlash_ > 0) undoFlash_--;
		if (quantFlash_ > 0) quantFlash_--;

		uiTicks_++;

		if (ui_ == UiMode::Learn) { LearnLeds(); return; }

		// --- choosing a mode ---------------------------------------------
		//
		// While the switch is down the LEDs show the PENDING selection, live,
		// so the player can see what releasing now would pick and move to
		// another button first. This is what makes commit-on-release legible
		// rather than mysterious — in particular it makes the "CV was already
		// sitting on that button" case visible.
		if (selectArmed_)
		{
			uint8_t mask = ComboLedMask(levels_.Current());
			bool    blink = ((uiTicks_ >> kBlinkFast) & 1) != 0;
			for (int i = 0; i < 4; i++)
				LedBrightness(i, (mask & (1u << i))
				                 ? (blink ? kLedFull : kLedDim) : 0);
			LedOff(4);
			LedOff(5);
			return;
		}

		// --- normal play ---------------------------------------------------
		//
		// There is deliberately NO mode-change animation. One used to play a
		// per-mode pattern across LEDs 0-3 on every latch, and it read as
		// noise rather than information — "a bit odd" from the bench. The
		// pulsing shift button below says which mode you are in continuously,
		// which is strictly more useful than a pattern you have to catch.
		ModeLeds();
	}

	/// The mode reminder, shown on the shift button of the current mode.
	///
	/// STEADY at half brightness, not a pulse. Steady is what makes it
	/// readable at a glance — you see which pad is lit and you know the mode,
	/// without waiting for a blink phase to come round.
	///
	/// It shares kLedHalf with "this mute group is muted", which sounds like a
	/// collision and is not: the shift button is never itself a group, and a
	/// group's half-bright flash is BRIEF where this is continuous. Steady
	/// versus flashing separates them more clearly than brightness would.
	///
	/// This replaced the mode-change animation. A continuous reminder beats a
	/// transient one: it answers "which mode am I in" at any moment rather
	/// than only in the second after you changed it.
	static uint16_t ModeReminder() { return kLedHalf; }

	/// The steady per-mode display.
	void __not_in_flash_func(ModeLeds)()
	{
		switch (mode_)
		{
		case Mode::Fx:
		{
			// fxHeld_ is a mask of BUTTONS here, not slots — any single can be
			// the shift, so there is no fixed slot order to walk. C stays lit
			// as the steady mode marker.
			for (int8_t i = 0; i < 4; i++)
			{
				uint16_t b = (fxHeld_ & (1u << i)) ? kLedFull : 0;
				if (b == 0 && i == kC) b = ModeReminder();
				LedBrightness(i, b);
			}
			break;
		}

		case Mode::Pattern:
		{
			// THREE slots, on A/B/C — D is the shift, as in every other mode,
			// so it shows the steady mode reminder rather than a slot.
			//
			// Three states per slot, which is as much as a single-colour LED
			// can carry and exactly what is needed:
			//
			//   full   the pattern currently playing
			//   half   a slot with something stored in it
			//   dark   an empty slot
			//
			// The distinction that matters live is "which of these can I jump
			// to", and half-versus-dark answers it at a glance.
			for (int8_t i = 0; i < 4; i++)
			{
				if (i == kD) { LedBrightness(i, ModeReminder()); continue; }

				uint16_t b;
				if (patStoreFlash_ > 0 && i == patLastStored_)
					b = ((patStoreFlash_ >> kBlinkFast) & 1) ? kLedFull : 0;
				else if (i == patLive_)          b = kLedFull;
				else if (loop_.PatternStored(i))  b = kLedHalf;
				else                              b = 0;
				LedBrightness(i, b);
			}
			break;
		}

		case Mode::Mute:
		{
			// Each group shows its HITS GOING PAST, on the button that toggles
			// it — full brightness when the group is sounding, half when it is
			// muted. Seeing a muted group still ticking is what lets you time
			// bringing it back in; hiding it would leave three dark pads and
			// no sense of the pattern underneath.
			const int8_t shift = ModeShift();
			int8_t slot = 0;
			for (int8_t i = 0; i < 4; i++)
			{
				if (i == shift) { LedBrightness(i, ModeReminder()); continue; }

				const bool muted = (muted_ & (1u << slot)) != 0;
				uint16_t b = 0;
				if (groupFlash_[slot] > 0) b = muted ? kLedHalf : kLedFull;
				LedBrightness(i, b);
				slot++;
			}
			break;
		}

		case Mode::WebUi:
			// Waiting for the browser: all four pads breathe together, slowly,
			// so the card visibly says "I am on USB and doing nothing yet"
			// rather than looking dead.
			//
			// Once an UPLOAD starts this display stops running entirely —
			// core 0 has parked and core 1 drives ShowUploadProgress() instead.
			for (int i = 0; i < 4; i++)
				LedBrightness(i, ((uiTicks_ >> 9) & 1) ? kLedGlow : 0);
			break;

		case Mode::Drums:
		default:
		{
			// ONLY the hit flashes. Nothing is shown for a merely-held combo.
			//
			// NIBBLE lit the sounding combo dim, which made sense there — a
			// held single is a bank-select you are mid-way through using, so
			// showing it was showing your hand position. Here it reads as a
			// fault: Four Voltages holds the last-released single forever, so
			// after a calibration (or any release) a pad sits lit with nobody
			// touching anything. Reported from the bench as a hanging LED on
			// first entering DRUMS, which is exactly the CV resting where the
			// last learn step left it.
			//
			// The flash on each hit is the information that matters anyway —
			// it says what you PLAYED, where the dim glow only said what the
			// voltage happens to be.
			for (int i = 0; i < 4; i++)
				LedBrightness(i, ledFlash_[i] > 0 ? kLedFull : 0);
			break;
		}
		}

		// An UNDO takes both status LEDs briefly, blinking fast. It borrows
		// them rather than owning a pad because it is an event, not a state —
		// and because the pads are showing the pattern, which is exactly what
		// you are looking at to see whether the undo did what you wanted.
		if (undoFlash_ > 0)
		{
			bool on = ((undoFlash_ >> kBlinkFast) & 1) != 0;
			LedBrightness(4, on ? kLedFull : 0);
			LedBrightness(5, on ? kLedFull : 0);
			return;
		}

		// QUANTISE borrows the same two LEDs and shows the grid as a PATTERN
		// rather than a count:
		//
		//   LED 4 alone    16th
		//   LED 4 and 5    12th (triplet)
		//   LED 5 alone    8th
		//
		// Better than blinking one/two/three because there is nothing to
		// count — you read it in one glance, the way you read the mute pads,
		// and the middle setting sits visually between the outer two, which
		// is also where it sits musically.
		if (quantFlash_ > 0)
		{
			const bool led4 = (quantGrid_ != QuantGrid::k8th);
			const bool led5 = (quantGrid_ != QuantGrid::k16th);
			LedBrightness(4, led4 ? kLedFull : 0);
			LedBrightness(5, led5 ? kLedFull : 0);
			return;
		}

		// LED 5 is the CLICK, LED 4 is RECORD — in every mode, so the two
		// things you need mid-performance never move.
		//
		// The click sits at half brightness rather than full: it runs
		// constantly, and something that bright pulsing in the corner of your
		// eye all night competes with the pads for attention it does not
		// deserve. Record is the opposite — it is either happening or it is
		// not, and you want to see that instantly, so it is full.
		LedBrightness(5, (playing_ && loop_.OnBeat()) ? kLedHalf : 0);
		LedBrightness(4, recording_ ? kLedFull : 0);
	}

	void __not_in_flash_func(LearnLeds)()
	{
		switch (learnPhase_)
		{
		case LearnPhase::Confirm:
			for (int i = 0; i < kNumLeds; i++) LedOn(i, true);
			return;

		case LearnPhase::NotSettled:
			// "The voltage was still moving." Distinct from a collision: the
			// BUTTON leds flutter rather than the phase markers, which reads
			// as "your hand, not the card" — hold it steady and press again.
			LedOff(4);
			LedOff(5);
			for (int i = 0; i < 4; i++)
				LedOn(i, ((phaseTimer_ >> kBlinkFast) & 1) != 0);
			return;

		case LearnPhase::Collision:
			// Only the phase markers flash, so a collision reads differently
			// from a clean capture without stopping the learn.
			for (int i = 0; i < 4; i++) LedOff(i);
			LedOn(4, ((phaseTimer_ >> kBlinkSlow) & 1) != 0);
			LedOn(5, ((phaseTimer_ >> kBlinkSlow) & 1) != 0);
			return;

		case LearnPhase::Done:
		{
			// A clean learn ramps all six up and fades. A learn that completed
			// but recorded collisions ramps the same way with LEDs 4 and 5
			// flashing over the top — so the warning is delivered ONCE, here,
			// where you can act on it, instead of blinking forever afterwards.
			uint16_t b = static_cast<uint16_t>(
				kLedFull - (phaseTimer_ * kLedFull) / kDoneFlashTicks);
			for (int i = 0; i < 4; i++) LedBrightness(i, b);

			if (collisionsThisLearn_)
			{
				bool f = ((phaseTimer_ >> kBlinkSlow) & 1) != 0;
				LedBrightness(4, f ? kLedFull : 0);
				LedBrightness(5, f ? kLedFull : 0);
			}
			else
			{
				LedBrightness(4, b);
				LedBrightness(5, b);
			}
			return;
		}

		case LearnPhase::Failed:
			// Nothing usable came in — almost always nothing patched into
			// CV In 1. An urgent, unmistakably different pattern: the two
			// COLUMNS alternating, fast.
			for (int i = 0; i < kNumLeds; i++)
				LedOn(i, (((phaseTimer_ >> kBlinkFast) & 1) != 0)
				         == ((i & 1) == 0));
			return;

		case LearnPhase::Aborted:
			for (int i = 0; i < kNumLeds; i++)
				LedOn(i, ((phaseTimer_ >> kBlinkSlow) & 1) != 0);
			return;

		case LearnPhase::Waiting:
		default:
			break;
		}

		// Waiting for the player to HOLD the combination being asked for and
		// tap the switch to confirm it.
		uint8_t want  = ComboLedMask(static_cast<int8_t>(kLearnOrder[learnStep_]));
		bool    blink = ((uiTicks_ >> kBlinkFast) & 1) != 0;

		for (int i = 0; i < 4; i++)
		{
			if (want & (1u << i))  LedBrightness(i, blink ? kLedFull : kLedDim);
			else if (Captured(i))  LedBrightness(i, kLedGlow);
			else                   LedOff(i);
		}

		// Phase marker: LED 4 during the four singles, LED 5 during the pairs.
		bool pairs = (learnStep_ >= kNumSingles);
		LedBrightness(4, pairs ? 0 : kLedDim);
		LedBrightness(5, pairs ? kLedDim : 0);
	}

	/// Has button `b` appeared in any combo captured so far? Used only to give
	/// already-visited buttons a dim glow during the singles phase.
	bool Captured(int b) const
	{
		for (int s = 0; s < learnStep_; s++)
			if (ComboLedMask(static_cast<int8_t>(kLearnOrder[s])) & (1u << b))
				return true;
		return false;
	}

	// =======================================================================
	// State
	// =======================================================================

	LevelTracker levels_;
	DrumKit      drums_;
	DjFilter     djFilter_;
	Looper       loop_;

	Mode     mode_       = Mode::Drums;   // the power-on default
	UiMode   ui_         = UiMode::Play;
	int32_t  bootPhase_  = 0;
	int32_t  splash_     = 0;
	int32_t  ctrlDiv_    = 0;
	uint32_t uiTicks_    = 0;
	bool     calibrateBoot_ = false;

	// mode select
	bool    selectArmed_  = false;  ///< switch is Down, a selection is pending
	bool    actionFired_  = false;  ///< a pair already consumed this gesture
	/// Set for ONE control tick when a short press is released. The capture
	/// gesture during calibration; unused elsewhere.
	bool    tapped_       = false;
	/// One abort per press. Starts TRUE so the alt-boot hold — which is still
	/// down when the first learn begins — cannot abort it on the way out.
	bool    abortLatched_ = true;
	int32_t downTicks_    = 0;

	// learn
	int32_t    captured_[kNumLevels] = {};
	int        learnStep_  = 0;
	LearnPhase learnPhase_ = LearnPhase::Waiting;
	int32_t    learnTimer_ = 0;
	int32_t    phaseTimer_ = 0;
	uint8_t    collisionsThisLearn_ = 0;

	// performance
	bool     playing_   = true;
	bool     recording_ = false;
	uint8_t  muted_     = 0;        ///< bit per mute group
	uint8_t  fxHeld_    = 0;        ///< which PAD to light, bit per button

	// --- performance effects (FX1) ---------------------------------------
	FxRack   fx_;
	/// Which SHIFT the hand is currently holding an effect under, or -1.
	/// That shift names the slot AND the automation lane, so one index
	/// carries both.
	int8_t   fxLiveSlot_ = -1;
	Fx       fxCurrent_  = Fx::None;   ///< the effect the HAND is holding
	/// Which lane's PARAMETER curve the Main knob is drawing, or -1. Set by
	/// holding a shift — with or without a tap, since the curve is the single
	/// source of depth either way.
	int8_t   fxParShift_ = -1;
	/// Which effect the LOOP is replaying per slot, or 0 for none.
	uint8_t  fxPlayback_[kNumFxSlots]      = {};
	int32_t  fxPlaybackTicks_[kNumFxSlots] = {};
	/// Each slot's replayed parameter curve. A position, not a hold, so it
	/// does not expire — it stays until the next recorded point moves it.
	int32_t  fxParPlayback_[kNumFxSlots] = { 2048, 2048, 2048, 2048 };
	/// Was TAPE STOP on last tick? Its brake fires on the rising edge only —
	/// see the note where DrumKit::TapeStopAll() is called.
	bool     tapeStopWasOn_ = false;

	/// The effect Pulse In 2 is currently forcing, and where it lives.
	/// Rolled once per rising edge and held while the gate is high.
	Fx       randomFxEffect_ = Fx::None;
	int8_t   randomFxSlot_   = kD;
	bool     randomFxOn_     = false;

	/// This card's PRNG state. DrumKit owns its own for noise; the CV
	/// features need one that is not being consumed at audio rate, or the
	/// two would correlate. Seeded to anything non-zero (xorshift32 never
	/// returns 0 and must never be given it).
	uint32_t rng_ = 0xC0FFEEu;
	/// The last voice the loop fired, for STUTTER to repeat.
	int8_t   lastVoice_    = -1;
	int32_t  stutterTicks_ = 0;
	/// FLAM's deferred second strike.
	int8_t   flamVoice_ = -1;
	int32_t  flamTicks_ = 0;
	int32_t  flamCount_ = 0;
	AutoKnob filterLane_;           ///< Main knob: the DJ filter
	AutoKnob toneLane_;             ///< Y knob: kit character
	int32_t  toneKnob_  = 2048;     ///< the Y value voices are struck with

	/// The DJ filter's own value, which is NOT simply the Main knob: in FX1
	/// that knob is the effect depth, so the filter latches and waits to be
	/// picked up again. See SoftPickup.
	int32_t    djFilterVal_ = 2048;
	SoftPickup filterPickup_;

	// outputs
	int32_t gateTimer_  = 0;
	int32_t clickTimer_ = 0;
	/// The two glitch gates on CV Out 1/2. Counted down in SAMPLES so their
	/// width does not follow the tempo — see GlitchTick().
	int32_t glitch1Timer_ = 0;
	int32_t glitch2Timer_ = 0;

	/// Set at 48kHz when a Pulse In 1 edge arrives, consumed by the 3kHz
	/// control tick. See ProcessSample for why this cannot be polled directly.
	bool    pulse1Edge_ = false;

	int32_t ledFlash_[4] = {};
	/// Per mute-group hit flash, so Mute mode can show the pattern going past.
	/// Set for every hit whether or not the group is audible — see
	/// TriggerVoice().
	int32_t groupFlash_[kNumMuteGroups] = {};
	/// Counts down while an UNDO is being acknowledged on the status LEDs.
	int32_t undoFlash_ = 0;

	// --- quantise grid ----------------------------------------------------
	/// The grid the LOOPER is currently recording against. Mirrored here only
	/// so the LED display can read it without querying the looper every tick.
	QuantGrid quantGrid_       = QuantGrid::k16th;
	int32_t   quantFlash_      = 0;   ///< counts down while acknowledging

	// --- pattern slots ----------------------------------------------------
	/// Which slot is playing. Starts at 0 so the first store has an obvious
	/// home and the display is not blank before anything is saved.
	int8_t  patLive_       = 0;
	int8_t  patLastStored_ = -1;   ///< which pad to flash
	int32_t patStoreFlash_ = 0;
};

// ===========================================================================

/// The USB stack, and the card core 1 needs a handle on for the LED display.
///
/// FILE SCOPE, not a member of NibbleKoCard, and the reason is size: WebUI
/// carries a 160KB RAM staging buffer (see webui.h's kUploadMax), which is
/// most of this chip's 256KB. Keeping it out of the card object keeps that
/// allocation obvious in the map file rather than buried in a class whose
/// other members are a few hundred bytes.
static WebUI        gWebUI;
static NibbleKoCard *gCard = nullptr;

/// Set once core 0 has finished booting and published gCard, so core 1 never
/// dereferences it early.
static volatile bool gUsbReady = false;

/// Core 1: idle until the WebUI mode is entered, then nothing but USB.
///
/// It must not touch anything core 0 owns. Before usbMode flips there is
/// genuinely nothing for it to do — unlike WorkshopBio, whose core 1 runs an
/// ecosystem simulation while it waits — so this is a plain spin.
///
/// TinyUSB is initialised HERE rather than at boot, which is the point of the
/// modal design: with the stack down the card does not enumerate and
/// USBCTRL_IRQ (whose handler lives in flash) is never armed, so nothing
/// competes with the audio path while the card is being played.
static void __not_in_flash_func(core1Entry)()
{
	while (!gUsbReady) tight_loop_contents();
	while (!WebUI::usbMode) tight_loop_contents();

	gWebUI.Init();
	for (;;)
	{
		gWebUI.Task();

		// Once an upload starts, core 0's audio interrupt is off and this is
		// the only core still running — so the progress display has to be
		// driven from here. LedBrightness only writes a PWM register and is
		// RAM-safe, so it survives mid-erase.
		if (WebUI::uploadMode && gCard)
			gCard->ShowUploadProgress(gWebUI.Progress());
	}
}

int main()
{
	// Overclock before anything else. 192MHz at 1.15V is proven on this exact
	// hardware by the sibling cards; the brief settle after the voltage change
	// is what stops the PLL relock from landing on an unstable rail.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(192000, true);

	// Restore which sound each voice plays, from the saved map in flash. Must
	// happen BEFORE the card starts playing, or the first hits come out of the
	// baked defaults and change under the player a moment later.
	//
	// Reading flash is safe here: XIP is up, nothing is writing, and the audio
	// interrupt has not started.
	LoadSlotSources();

	static NibbleKoCard card;

	// Core 1 is launched HERE, not in the card's constructor. That constructor
	// runs during ComputerCard's own construction, before the peripherals are
	// up, and this card's rule is that nothing touching hardware happens there
	// — see NibbleKoCard's constructor comment.
	gCard = &card;
	multicore_launch_core1(core1Entry);
	gUsbReady = true;

	// Jack detection. Without this Connected() always answers false, and
	// every CV feature below silently never engages — the whole expansion
	// depends on knowing which sockets have cables in them.
	//
	// Must precede Run(), which never returns.
	card.EnableNormalisationProbe();

	card.Run();   // never returns
}
