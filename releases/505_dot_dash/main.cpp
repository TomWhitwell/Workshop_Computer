/*
 * Dot Dash - a Morse code USB keyboard card for the Music Thing Modular
 * Workshop Computer.
 *
 * Plug a USB keyboard into the Workshop Computer, type letters, and the card
 * sends them as Morse code:
 *   - Audio Out 1  : a square-wave beep for each dot and dash
 *   - CV Out 1     : in "pitch" mode, a note per symbol (dot = high, dash = low)
 *   - Pulse Out 1  : a gate, high for exactly as long as each dot or dash lasts
 *   - Knob X       : speed, 5-40 words per minute
 *   - Knob Y       : beep pitch, 300-2000 Hz
 *   - Switch Up    : audio beep mode
 *   - Switch Middle: pitch CV mode
 *   - Switch Down  : momentary "shift" - hold it to flip to the other mode
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
		timer = 0;
		dotSamples = 4800;
		dashSamples = 14400;
		symbolGapSamples = 1600;
		letterGapSamples = 4800;
		wordGapExtraSamples = 6400;
		currentSymbolIsDash = false;
		env = 0;
		phase = 0;
		phaseInc = 0;
		lastNote = kDotNote;
		beaconIndex = 0;
		lastKnobX = -1;
		lastKnobY = -1;
		lastKnobMain = -1;
		beepAmp = kAmplitude;
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

		// Knob X -> 5..40 words per minute. A Morse "unit" is one dot long;
		// the classic relationship is dot_ms = 1200 / wpm, and at 48 kHz that
		// is 57600 / wpm samples.
		//
		// The RP2040's Cortex-M0+ has no hardware divide, so we only recompute
		// these divisions when the knob has actually moved. That keeps the
		// per-sample cost to a couple of compares in the common case.
		if (knobX != lastKnobX)
		{
			lastKnobX = knobX;
			int32_t wpm = 5 + (knobX * 35) / 4095;
			if (wpm < 5) wpm = 5;
			if (wpm > 40) wpm = 40;
			int32_t unit = 57600 / wpm;
			dotSamples = unit;
			dashSamples = unit * 3;
			symbolGapSamples = unit;
			letterGapSamples = unit * 3;
			wordGapExtraSamples = unit * 4; // letter gap (3) + 4 = 7
		}

		// Knob Main -> beep volume. The beep and the pitch CV are on separate
		// jacks (Audio Out 1 and CV Out 1), so they always play together; Main
		// simply sets how loud the beep is. Pots only reach about 14 at the
		// minimum, so the lowest bit of travel is treated as true silence.
		if (knobMain != lastKnobMain)
		{
			lastKnobMain = knobMain;
			int32_t m = (knobMain < 64) ? 0 : knobMain;
			beepAmp = (kAmplitude * m) / 4095;
		}

		// --- Advance the Morse state machine ---------------------------------
		Tick();

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
		// Audio Out 1 (beep) and CV Out 1 (pitch) are separate jacks, so both
		// are driven every sample. Patch whichever you want; use Knob Main to
		// silence the beep if you only want the CV.

		// Pitch CV: dot and dash are two different notes. We hold the last
		// note through the gaps (the gate tells you when it is sounding) and
		// drop to 0 V when idle.
		if (state == StIdle)
		{
			CVOut1(0);
		}
		else
		{
			if (symbolActive)
				lastNote = currentSymbolIsDash ? kDashNote : kDotNote;
			if (CVOutsCalibrated())
				CVOut1MIDINote(lastNote); // precise, 1V/oct calibrated
			else
				CVOut1((int16_t)RawNote(lastNote)); // rough fallback
		}

		// Audio beep: a square wave, gated on during dots and dashes and
		// scaled by Knob Main. The short envelope ramp avoids a click when
		// the beep starts and stops.
		int32_t target = symbolActive ? beepAmp : 0;
		if (env < target) { env += kEnvStep; if (env > target) env = target; }
		else if (env > target) { env -= kEnvStep; if (env < target) env = target; }

		phase += phaseInc;
		int32_t sq = (phase & 0x80000000u) ? 1 : -1;
		AudioOut1((int16_t)(env * sq));

		// --- Gate -------------------------------------------------------------
		// High for exactly the length of each dot or dash, low during gaps.
		PulseOut1(symbolActive);

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
			LedOn(4, false);                                // (beacon indicator)
			LedOn(5, true);                                 // keyboard connected
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
	static const uint8_t kDotNote = 72;     // C5 - the high note
	static const uint8_t kDashNote = 67;    // G4 - a fifth below

	St state;
	const char *pattern;   // current character's dots and dashes
	int32_t patternIndex;  // which symbol of the pattern we are on
	int32_t timer;         // samples left in the current state
	int32_t dotSamples;
	int32_t dashSamples;
	int32_t symbolGapSamples;
	int32_t letterGapSamples;
	int32_t wordGapExtraSamples;
	bool currentSymbolIsDash;
	int32_t env;           // current beep amplitude during the click-free ramp
	uint32_t phase;        // square-wave phase accumulator
	uint32_t phaseInc;
	uint8_t lastNote;      // last pitch sent in CV mode
	uint8_t beaconIndex;   // position in the looping "SOS " beacon
	int32_t lastKnobX;     // cached raw knob readings, so we only recompute
	int32_t lastKnobY;     // the speed/pitch maths when a knob actually moves
	int32_t lastKnobMain;  // cached raw Main reading (beep volume)
	int32_t beepAmp;       // beep amplitude after the Main volume knob

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

	// Begin the symbols of the current pattern.
	void StartSymbol()
	{
		currentSymbolIsDash = (pattern[patternIndex] == '-');
		timer = currentSymbolIsDash ? dashSamples : dotSamples;
		state = StSymbol;
	}

	// Load the next character and either start it, or produce a word gap if it
	// is a space.
	void LoadNext()
	{
		char c;
		if (!NextCharacter(&c)) { state = StIdle; return; }
		if (c == ' ')
		{
			state = StWordGap;
			timer = wordGapExtraSamples; // 4 extra on top of the 3-unit gap
			return;
		}
		pattern = morseFor(c);
		if (pattern == nullptr) { state = StIdle; return; } // unmapped char
		patternIndex = 0;
		StartSymbol();
	}

	// One tick of the state machine, called once per audio sample.
	void Tick()
	{
		switch (state)
		{
			case StIdle:
				if (CharAvailable())
					LoadNext();
				break;

			case StSymbol:
				if (--timer <= 0)
				{
					if (pattern[patternIndex + 1] != '\0')
					{
						// More symbols in this letter: a 1-unit gap.
						patternIndex++;
						state = StSymbolGap;
						timer = symbolGapSamples;
					}
					else
					{
						// Letter finished: a 3-unit letter gap.
						state = StLetterGap;
						timer = letterGapSamples;
					}
				}
				break;

			case StSymbolGap:
				if (--timer <= 0)
					StartSymbol();
				break;

			case StLetterGap:
				if (--timer <= 0)
					LoadNext();
				break;

			case StWordGap:
				if (--timer <= 0)
					LoadNext();
				break;
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
