/*
 * Dot Dash - a Morse code USB keyboard card for the Music Thing Modular
 * Workshop Computer.
 *
 * Plug a USB keyboard into the Workshop Computer, type letters, and the card
 * sends them as Morse code:
 *   - Audio Out 1  : a square-wave beep for each dot and dash
 *   - Audio Out 2  : a triangle-wave melody voice at the note pitch
 *   - CV Out 1     : one pitch per character (A lowest, rising through Z and 0-9)
 *   - CV Out 2     : the current speed, as a voltage
 *   - Pulse Out 1  : a gate, high for exactly as long as each dot or dash lasts
 *   - Pulse Out 2  : a short trigger at the start of each symbol
 *   - CV In 1      : transpose the notes (1 V/oct)
 *   - CV In 2      : modulate the speed
 *   - Pulse In 1   : external clock - one rising edge per Morse unit
 *   - Pulse In 2   : clear the typed queue on a rising edge
 *   - Knob Main    : audio volume
 *   - Knob X       : speed, 5-40 words per minute (ignored while clocked)
 *   - Knob Y       : beep pitch, 300-2000 Hz
 *
 * Patch a clock into Pulse In 1 and the Morse locks to it: every rising edge
 * advances one unit (dot = 1 edge, dash = 3, gaps 1 and 3, word gap 7). While a
 * clock is running, Knob X is ignored. Pull the cable or stop the clock and
 * after about two seconds the card reverts to the Knob X speed.
 *
 * If no keyboard is plugged in, the card loops "... --- ..." (SOS) on the
 * audio and gate outputs and flashes all six LEDs in that rhythm, so you can
 * see at a glance that it is alive but waiting for a keyboard.
 *
 * Two cores are used:
 *   - Core 0 runs the TinyUSB host stack (this file's main loop + callbacks).
 *   - Core 1 runs the ComputerCard audio engine (ProcessSample at 48 kHz).
 * They talk through a small lock-free ring buffer of characters and a couple
 * of flags, all marked volatile.
 */

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "morse_table.h"

#include <math.h>

// How many typed characters we can hold while the current ones are being sent.
// 64 is about twelve words of typing - plenty of buffer for fast typists.
#define DD_QUEUE_SIZE 64

// Maximum number of HID report descriptions we remember per USB device.
#define MAX_REPORT 4

// Parsed report-descriptor info for generic (non-boot-protocol) HID devices.
// Most keyboards use the simple "boot protocol", but some send a fuller
// descriptor that we have to interpret to know a report is a keyboard.
static struct
{
	uint8_t report_count;
	tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];


class DotDash : public ComputerCard
{
public:
	DotDash()
	{
		state = StIdle;
		pattern = nullptr;
		patternIndex = 0;
		unitSamples = 4800;        // samples per Morse unit at the default speed
		stateUnitSamples = 4800;   // unit length latched when the current state began
		subTimer = 0;              // samples left within the current unit
		unitsLeft = 0;             // units left in the current state
		currentSymbolIsDash = false;
		env = 0;
		phase = 0;
		phaseInc = 0;
		phase2 = 0;
		melodyInc = 8000;
		lastMelodyNote = 0xFF;
		currentNote = kBaseNote;
		beaconIndex = 0;
		lastKnobX = -1;
		lastKnobY = -1;
		lastKnobMain = -1;
		audioAmp = kAmplitude;
		triggerTimer = 0;
		lastWpm = -1;
		baseWpm = 20;
		clocked = false;
		clockRunning = false;
		samplesSinceEdge = 0;
		clockPeriodSamples = 0;
	}

	// ---- Core 1 entry point -------------------------------------------------
	// Core 1 runs the audio engine; this function never returns.
	static void core1()
	{
		card->Run();
	}

	static void setCard(DotDash *c) { card = c; }

	// ---- Called from Core 0 (USB callbacks) --------------------------------

	// Add a character to the send queue. Returns false (and asks the LEDs to
	// flash) if the queue is already full.
	static bool Enqueue(char c)
	{
		uint8_t next = (uint8_t)((queue_head + 1u) % DD_QUEUE_SIZE);
		if (next == queue_tail)
		{
			overflowFlash = 4800; // about 0.1 s of LED flash
			return false;
		}
		key_queue[queue_head] = c;
		queue_head = next;
		return true;
	}

	// Take one character out of the queue. Returns false if empty.
	static bool Dequeue(char *c)
	{
		if (queue_head == queue_tail)
			return false;
		*c = key_queue[queue_tail];
		queue_tail = (uint8_t)((queue_tail + 1u) % DD_QUEUE_SIZE);
		return true;
	}

	// Is a key down in this report?
	static bool KeyInReport(hid_keyboard_report_t const *r, uint8_t kc)
	{
		for (int i = 0; i < 6; i++)
			if (r->keycode[i] == kc)
				return true;
		return false;
	}

	// Turn a raw keyboard report into queued characters. We only react to keys
	// that were not already held (edge detection), so holding a key sends one
	// character rather than an auto-repeated stream.
	static void ProcessKeyboard(hid_keyboard_report_t const *r)
	{
		bool shift = (r->modifier & DD_MOD_SHIFTMASK) != 0;
		for (int i = 0; i < 6; i++)
		{
			uint8_t kc = r->keycode[i];
			if (kc == 0)
				continue;
			if (!KeyInReport(&prev_report, kc))
			{
				char c = hidToAscii(kc, shift);
				if (c != 0)
					Enqueue(c);
				// Keys with no Morse meaning (arrows, Escape, ...) are
				// silently ignored.
			}
		}
		prev_report = *r;
	}

	static void ResetKeyboard() { prev_report = {}; }

	// Track how many mounted HID interfaces are keyboards, so the beacon only
	// runs when there is genuinely no keyboard attached.
	static void SetKeyboardConnected(uint8_t instance, bool on)
	{
		if (instance >= CFG_TUH_HID)
			return;
		if (on)
		{
			if (!kbInstance[instance]) { kbInstance[instance] = true; kbCount++; }
		}
		else
		{
			if (kbInstance[instance]) { kbInstance[instance] = false; kbCount--; }
		}
		keyboard_connected = (kbCount > 0);
	}

	// ---- Core 1: audio processing -------------------------------------------

	virtual void ProcessSample()
	{
		// --- Read the controls -------------------------------------------------
		// Knobs must only be read inside ProcessSample for interrupt safety.
		// Knobs read roughly 0-4095 but never quite reach zero, so we clamp.
		int32_t knobX = KnobVal(Knob::X);
		int32_t knobY = KnobVal(Knob::Y);
		int32_t knobMain = KnobVal(Knob::Main);
		if (knobX < 0) knobX = 0;
		if (knobX > 4095) knobX = 4095;
		if (knobY < 0) knobY = 0;
		if (knobY > 4095) knobY = 4095;
		if (knobMain < 0) knobMain = 0;
		if (knobMain > 4095) knobMain = 4095;

		// CV inputs. With the normalisation probe enabled, an unpatched CV jack
		// reads exactly 0, so no transposition or speed change unless a cable is
		// actually plugged in.
		int32_t cvIn1 = CVIn1();
		int32_t cvIn2 = CVIn2();

		// --- Transpose (CV In 1) ---------------------------------------------
		// Treat the input as 1 V/oct: a semitone is about 28 counts of the
		// -2048..2047 range. Clamped to +/-2 octaves so the notes stay usable.
		int32_t semis = cvIn1 / 28;
		if (semis < -24) semis = -24;
		if (semis > 24) semis = 24;

		// --- External clock (Pulse In 1) -------------------------------------
		// One rising edge = one Morse unit. We measure the gap between edges so
		// we can report the clocked speed, and time out if the clock stops.
		bool edge = PulseIn1RisingEdge();
		if (edge)
		{
			// Only trust the gap if the clock was already running; the first
			// edge after a timeout just restarts the stopwatch.
			if (clockRunning && samplesSinceEdge > 0)
				clockPeriodSamples = samplesSinceEdge;
			samplesSinceEdge = 0;
			clockRunning = true;
		}
		else if (samplesSinceEdge < kClockMaxGap)
		{
			samplesSinceEdge++;
		}
		else
		{
			clockRunning = false; // no edge for ~2 s: hand tempo back to the knob
		}
		// A patched, still-running clock owns the tempo.
		clocked = Connected(Input::Pulse1) && clockRunning;

		// --- Speed (Knob X, modulated by CV In 2) ----------------------------
		// A Morse "unit" is one dot long; the classic relationship is
		// dot_ms = 1200 / wpm, and at 48 kHz that is 57600 / wpm samples.
		//
		// The RP2040's Cortex-M0+ has no hardware divide, so we compute the
		// divide-free base speed only when Knob X moves, and the unit length
		// only when the resulting speed changes.
		if (knobX != lastKnobX)
		{
			lastKnobX = knobX;
			baseWpm = 5 + (knobX * 35) / 4095; // 5..40
		}
		int32_t wpm;
		if (clocked && clockPeriodSamples > 0)
		{
			// Derive the speed from the measured clock period, for CV Out 2.
			wpm = 57600 / clockPeriodSamples;
			if (wpm < 5) wpm = 5;
			if (wpm > 60) wpm = 60;
		}
		else
		{
			// Knob X, plus up to +/-20 WPM from CV In 2.
			wpm = baseWpm + ((cvIn2 * 20) >> 11);
			if (wpm < 5) wpm = 5;
			if (wpm > 60) wpm = 60;
		}
		if (wpm != lastWpm)
		{
			lastWpm = wpm;
			unitSamples = 57600 / wpm;

			// Speed CV (CV Out 2): 5 WPM = 0 V, 60 WPM = ~+5 V, calibrated.
			CVOut2Millivolts(((wpm - 5) * 5000) / 55);
		}

		// --- Volume (Knob Main) ----------------------------------------------
		// Main is the master level for both audio outputs. Pots only reach
		// about 14 at the minimum, so the lowest bit of travel is silence.
		if (knobMain != lastKnobMain)
		{
			lastKnobMain = knobMain;
			int32_t m = (knobMain < 64) ? 0 : knobMain;
			audioAmp = (kAmplitude * m) / 4095;
		}

		// --- Clear (Pulse In 2) ----------------------------------------------
		// A rising edge throws away anything typed but not yet sent, and
		// returns to idle. Handy for aborting a long wrong message.
		if (PulseIn2RisingEdge())
		{
			queue_tail = queue_head;
			state = StIdle;
			pattern = nullptr;
			triggerTimer = 0;
			beaconIndex = 0;
		}

		// --- Advance the Morse state machine ---------------------------------
		// Clocked, one edge per unit; internally, one sample per sample.
		TickStep(edge);

		// --- Beep pitch ------------------------------------------------------
		// Knob Y -> 300..2000 Hz. We turn frequency into a 32-bit phase step
		// (2^32 / 48000 = 89478 per Hz), so the square wave needs no floats.
		// As with Knob X, only recompute the division when the knob moves.
		if (knobY != lastKnobY)
		{
			lastKnobY = knobY;
			int32_t freq = 300 + (knobY * 1700) / 4095;
			phaseInc = (uint32_t)freq * 89478u;
		}

		bool symbolActive = (state == StSymbol);

		// --- Outputs ----------------------------------------------------------
		// The beep (Audio Out 1), melody (Audio Out 2) and pitch CV (CV Out 1)
		// are all separate jacks, so they play together; Knob Main is the audio
		// master volume.

		// Pitch CV: one note per character, rising with the alphabet. We hold
		// that note through the character's own dots/dashes and its trailing
		// letter gap (so a new letter changes pitch smoothly), and drop to 0 V
		// at a word gap or when idle so words separate clearly.
		uint8_t note = (uint8_t)ClampNote((int32_t)currentNote + semis);
		if (state == StIdle || state == StWordGap)
		{
			CVOut1(0);
		}
		else
		{
			if (CVOutsCalibrated())
				CVOut1MIDINote(note); // precise, 1V/oct calibrated
			else
				CVOut1((int16_t)RawNote(note)); // rough fallback
		}

		// Melody voice: a triangle wave at the same character pitch. Recompute
		// the phase step only when the note actually changes.
		if (note != lastMelodyNote)
		{
			lastMelodyNote = note;
			melodyInc = NoteToPhaseInc(note);
		}

		// Shared click-free envelope for both audio voices.
		int32_t target = symbolActive ? audioAmp : 0;
		if (env < target) { env += kEnvStep; if (env > target) env = target; }
		else if (env > target) { env -= kEnvStep; if (env < target) env = target; }

		// Beep: square wave.
		phase += phaseInc;
		int32_t sq = (phase & 0x80000000u) ? 1 : -1;
		AudioOut1((int16_t)(env * sq));

		// Melody: triangle wave folded out of the top 16 bits of the phase.
		phase2 += melodyInc;
		int32_t p16 = (int32_t)(phase2 >> 16); // 0..65535
		int32_t tri = (p16 < 32768) ? (p16 * 2 - 32767) : (98303 - p16 * 2);
		AudioOut2((int16_t)((env * tri) >> 15));

		// --- Gates ------------------------------------------------------------
		// Pulse Out 1: high for exactly the length of each dot or dash.
		// Pulse Out 2: a short trigger fired at the start of each symbol.
		PulseOut1(symbolActive);
		PulseOut2(triggerTimer > 0);
		if (triggerTimer > 0)
			triggerTimer--;

		// --- LEDs -------------------------------------------------------------
		if (!keyboard_connected)
		{
			// No keyboard: all six LEDs flash together in the SOS rhythm.
			bool on = symbolActive;
			for (uint32_t i = 0; i < 6; i++)
				LedOn(i, on);
		}
		else
		{
			LedOn(0, symbolActive && !currentSymbolIsDash); // dot
			LedOn(1, symbolActive && currentSymbolIsDash);  // dash
			LedOn(2, state != StIdle);                      // transmitting
			LedOn(3, overflowFlash > 0);                    // queue overflow
			LedOn(4, clocked);                             // external clock active
			LedOn(5, true);                                // keyboard connected
		}
		if (overflowFlash > 0)
			overflowFlash--;

	}

private:
	// ---- State machine ------------------------------------------------------
	enum St { StIdle, StSymbol, StSymbolGap, StLetterGap, StWordGap };

	// The Morse timing constants, in 48 kHz samples. These are recomputed every
	// sample from Knob X but only *used* when a symbol starts, so turning the
	// knob never stretches a dot or dash that is already playing.
	static const int32_t kAmplitude = 1700; // below full scale, keeps some headroom
	static const int32_t kEnvStep = 64;     // ~0.5 ms click-free ramp
	static const int32_t kTriggerSamples = 96; // ~2 ms symbol-start trigger
	static const int32_t kClockMaxGap = 96000;  // ~2 s at 48 kHz: clock timeout
	static const uint8_t kBaseNote = 60;    // middle C - the pitch of 'A'

	St state;
	const char *pattern;   // current character's dots and dashes
	int32_t patternIndex;  // which symbol of the pattern we are on
	int32_t unitSamples;        // samples per Morse unit (from the knob/CV speed)
	int32_t stateUnitSamples;   // unit length latched when the state began
	int32_t subTimer;           // samples left within the current unit (internal)
	int32_t unitsLeft;          // Morse units left in the current state
	bool currentSymbolIsDash;
	int32_t env;           // current audio amplitude during the click-free ramp
	uint32_t phase;        // beep square-wave phase accumulator
	uint32_t phaseInc;
	uint32_t phase2;       // melody triangle-wave phase accumulator
	uint32_t melodyInc;    // step per sample for the current melody note
	uint8_t lastMelodyNote; // note the melody phase step was computed for
	uint8_t currentNote;   // pitch of the character being sent (held through gaps)
	uint8_t beaconIndex;   // position in the looping "SOS " beacon
	int32_t lastKnobX;     // cached raw knob readings, so we only recompute
	int32_t lastKnobY;     // the speed/pitch maths when a knob actually moves
	int32_t lastKnobMain;  // cached raw Main reading (volume)
	int32_t audioAmp;      // audio amplitude after the Main volume knob
	int32_t triggerTimer;  // samples left on the Pulse Out 2 symbol trigger
	int32_t lastWpm;       // cached speed, so we only recompute the unit on change
	int32_t baseWpm;       // Knob X speed before CV In 2 modulation
	bool clocked;              // an external clock is currently driving the card
	bool clockRunning;         // edges are still arriving (false after a timeout)
	int32_t samplesSinceEdge;  // samples since the last Pulse In 1 rising edge
	int32_t clockPeriodSamples; // measured clock period, in samples

	// Is there something to send right now? (The beacon never runs dry.)
	static bool CharAvailable()
	{
		if (!keyboard_connected)
			return true;
		return queue_head != queue_tail;
	}

	// Fetch the next character: from the keyboard queue if a keyboard is
	// attached, otherwise from the repeating "SOS " beacon.
	bool NextCharacter(char *c)
	{
		if (keyboard_connected)
			return Dequeue(c);
		static const char beacon[] = "SOS ";
		*c = beacon[beaconIndex & 3u];
		beaconIndex = (uint8_t)((beaconIndex + 1u) & 3u);
		return true;
	}

	// Enter a state that lasts a given number of Morse units. We latch the
	// current unit length here, so changing the speed mid-symbol never stretches
	// a dot or dash that is already playing.
	void EnterState(St s, int32_t units)
	{
		state = s;
		unitsLeft = units;
		stateUnitSamples = unitSamples;
		subTimer = stateUnitSamples;
	}

	// Begin the symbols of the current pattern.
	void StartSymbol()
	{
		currentSymbolIsDash = (pattern[patternIndex] == '-');
		triggerTimer = kTriggerSamples; // Pulse Out 2: short trigger on the edge
		EnterState(StSymbol, currentSymbolIsDash ? 3 : 1);
	}

	// Load the next character and either start it, or produce a word gap if it
	// is a space.
	void LoadNext()
	{
		char c;
		if (!NextCharacter(&c)) { state = StIdle; return; }
		if (c == ' ')
		{
			EnterState(StWordGap, 4); // 4 extra on top of the 3-unit letter gap
			return;
		}
		pattern = morseFor(c);
		if (pattern == nullptr) { state = StIdle; return; } // unmapped char
		// Each character has its own pitch. Punctuation has no note of its own,
		// so charToNote returns -1 and we keep the previous character's pitch.
		int note = charToNote(c);
		if (note >= 0)
			currentNote = (uint8_t)note;
		patternIndex = 0;
		StartSymbol();
	}

	// Run out of the current state and move to the next.
	void AdvanceState()
	{
		switch (state)
		{
			case StSymbol:
				if (pattern[patternIndex + 1] != '\0')
				{
					// More symbols in this letter: a 1-unit gap.
					patternIndex++;
					EnterState(StSymbolGap, 1);
				}
				else
				{
					// Letter finished: a 3-unit letter gap.
					EnterState(StLetterGap, 3);
				}
				break;

			case StSymbolGap:
				StartSymbol();
				break;

			case StLetterGap:
			case StWordGap:
				LoadNext();
				break;

			case StIdle:
				break;
		}
	}

	// One step of the state machine, called once per audio sample.
	//
	// Internal mode counts samples within each unit. Clocked mode ignores the
	// sample clock and steps only on a Pulse In 1 rising edge, so one edge is
	// exactly one Morse unit. `edge` is true on the sample an edge arrived.
	void TickStep(bool edge)
	{
		if (state == StIdle)
		{
			// Start the next character. In clocked mode this happens on an edge,
			// so the first symbol lands on the beat.
			if (clocked ? edge : true)
			{
				if (CharAvailable())
					LoadNext();
			}
			return;
		}

		if (clocked)
		{
			if (!edge)
				return;
			if (--unitsLeft <= 0)
				AdvanceState();
			return;
		}

		if (--subTimer <= 0)
		{
			subTimer = stateUnitSamples;
			if (--unitsLeft <= 0)
				AdvanceState();
		}
	}

	// Rough note-to-CV fallback for boards without stored calibration.
	// 0 V is treated as MIDI note 60; a semitone is about 28 counts.
	static int32_t RawNote(uint8_t note)
	{
		int32_t v = ((int32_t)note - 60) * 28;
		if (v < -2048) v = -2048;
		if (v > 2047) v = 2047;
		return v;
	}

	// Keep a note number inside the valid MIDI range after transposition.
	static int32_t ClampNote(int32_t note)
	{
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		return note;
	}

	// Turn a MIDI note number into a 32-bit phase step for the melody voice.
	// A4 (note 69) is 440 Hz, each semitone is 2^(1/12). 2^32 / 48000 = 89478,
	// so phaseInc = frequency * 89478. This uses single-precision float, but is
	// only called when the note changes - never per sample.
	static uint32_t NoteToPhaseInc(uint8_t note)
	{
		float semis = (float)((int32_t)note - 69) * (1.0f / 12.0f);
		float freq = 440.0f * exp2f(semis);
		return (uint32_t)(freq * 89478.0f);
	}

	// ---- Shared state between the two cores ---------------------------------
	static volatile char key_queue[DD_QUEUE_SIZE];
	static volatile uint8_t queue_head;
	static volatile uint8_t queue_tail;
	static volatile bool keyboard_connected;
	static volatile int32_t overflowFlash;
	static hid_keyboard_report_t prev_report;
	static bool kbInstance[CFG_TUH_HID];
	static uint8_t kbCount;
	static DotDash *card;
};

// ---- Static member definitions ----------------------------------------------
volatile char DotDash::key_queue[DD_QUEUE_SIZE];
volatile uint8_t DotDash::queue_head = 0;
volatile uint8_t DotDash::queue_tail = 0;
volatile bool DotDash::keyboard_connected = false;
volatile int32_t DotDash::overflowFlash = 0;
hid_keyboard_report_t DotDash::prev_report = {};
bool DotDash::kbInstance[CFG_TUH_HID] = {};
uint8_t DotDash::kbCount = 0;
DotDash *DotDash::card = nullptr;


//////////////////////////////////////////////////////////////////////////////
// TinyUSB host HID callbacks

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
	uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);

	if (proto == HID_ITF_PROTOCOL_NONE)
	{
		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(
		    hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
	}

	if (proto == HID_ITF_PROTOCOL_KEYBOARD)
	{
		DotDash::SetKeyboardConnected(instance, true);
	}
	else if (proto == HID_ITF_PROTOCOL_NONE)
	{
		// Generic HID: check the parsed descriptor for keyboard usage.
		for (uint8_t i = 0; i < hid_info[instance].report_count; i++)
		{
			tuh_hid_report_info_t *info = &hid_info[instance].report_info[i];
			if (info->usage_page == HID_USAGE_PAGE_DESKTOP && info->usage == HID_USAGE_DESKTOP_KEYBOARD)
				DotDash::SetKeyboardConnected(instance, true);
		}
	}

	tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)dev_addr;
	DotDash::SetKeyboardConnected(instance, false);
	DotDash::ResetKeyboard();
	if (instance < CFG_TUH_HID)
		hid_info[instance] = {};
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
	uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);

	if (proto == HID_ITF_PROTOCOL_KEYBOARD)
	{
		DotDash::ProcessKeyboard((hid_keyboard_report_t const *)report);
	}
	else if (proto == HID_ITF_PROTOCOL_NONE)
	{
		// Generic HID: find the report by ID, then check it is a keyboard.
		uint8_t rpt_count = hid_info[instance].report_count;
		tuh_hid_report_info_t *arr = hid_info[instance].report_info;
		tuh_hid_report_info_t *info = nullptr;

		if (rpt_count == 1 && arr[0].report_id == 0)
		{
			info = &arr[0];
		}
		else if (len > 0)
		{
			for (uint8_t i = 0; i < rpt_count; i++)
			{
				if (report[0] == arr[i].report_id)
				{
					info = &arr[i];
					report++;
					len--;
					break;
				}
			}
		}

		if (info && info->usage_page == HID_USAGE_PAGE_DESKTOP &&
		    info->usage == HID_USAGE_DESKTOP_KEYBOARD && len >= sizeof(hid_keyboard_report_t))
		{
			DotDash::ProcessKeyboard((hid_keyboard_report_t const *)report);
		}
	}

	tuh_hid_receive_report(dev_addr, instance);
}


//////////////////////////////////////////////////////////////////////////////

int main()
{
	// 144 MHz divides exactly to the 48 MHz USB clock and keeps the audio ADC
	// happy (fewer tonal artefacts than 125 MHz).
	set_sys_clock_khz(144000, true);

	sleep_ms(50);

	// Construct the card on core 0 so it exists before core 1 starts using it,
	// then hand the audio engine to core 1.
	static DotDash card;
	DotDash::setCard(&card);

	// Enable jack detection, so unpatched CV/pulse inputs read as zero rather
	// than floating noise. This must happen before Run() starts on core 1.
	card.EnableNormalisationProbe();

	multicore_launch_core1(DotDash::core1);
	sleep_ms(50);

	// Core 0 becomes the USB host: it watches for a keyboard and passes keys
	// to the audio core through the character queue.
	tusb_rhport_init_t host_init = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUH_RHPORT, &host_init);

	while (1)
	{
		tuh_task();
	}
}
