// Weights
//
// Random pitch CV for the Music Thing Workshop Computer, with the randomness
// guided by a Music Thing 8mu: its faders are probabilities.  Two voices
// wander at once.  At every step each voice draws a note of the scale, an
// octave, a gate length and a ratchet, each weighted by a page of faders, and
// plays them as a 1V/oct pitch, a gate and an envelope.
//
// The musical engine is in weights.h and the 8mu's pages and LEDs in ui.h;
// this file connects them to the Computer's knobs, jacks and USB.
//
// The 8mu (buttons A-D choose the page; press a button again for its next
// page, round four: its first page, voice 2's, its envelope page, voice 2's)
//
//             First page                         Third page (envelope)
//   Button A  NOTES     the scale's notes        ATTACK  /4 ... x4 the knob
//   Button B  OCTAVES   -2V (fader 1) to +5V     DECAY   /4 ... x4 the knob
//   Button C  GATES     rest, 1/7 ... 6/7, tied  PLUCK   -50% ... +50%
//   Button D  RATCHETS  1x 1.5x 2x 3x on one     DEPTH   1/8 ... full peak
//                       note, then 1.5x 2x 3x 4x
//                       with a new note each hit
// Second and fourth pages: the same, for voice 2 (NOTES 2, ATTACK 2 ...).
//
// Faders are relative probabilities: one at zero is never chosen, and one
// twice as high as another is chosen twice as often.  A first page all at
// zero makes every choice equally likely.  While a second page is all at
// zero, voice 2 draws from the first page too (two different lines with the
// same probabilities); once any of its faders is up, voice 2 uses it instead,
// independently of voice 1.  Each kind of page decides this on its own.
// The envelope pages are chances of straying from the knobs' envelope, drawn
// with each note; an envelope page all at zero is off (the knobs exactly).
//
// After a page change the faders 'pick up': one only takes over once it has
// been moved to (or across) the value already stored.  For 3 seconds after a
// fader or button is touched the 8mu's LEDs show the page's probabilities
// (ratchets flash at their own speed); after that, what is playing.
//
// Panel
//   Switch up    Main = scale, X = clock divide/multiply (/8 ... x1 in the
//                centre ... x8), Y = slew on the pitch: off at the centre;
//                right, every note, 1ms to 2s; left, only notes a tie leads
//                into (the same times)
//   Switch mid   Main = decay (10ms to 4s), X = attack (0.5ms to 2s),
//                Y = pluckiness (the decay's curve, soft to snappy)
//   Switch down  (held) Main = internal tempo, 40-300 BPM; X and Y keep the
//                settings of the position it was pressed from
// Each knob setting keeps its value; after the switch moves, a knob does
// nothing until it reaches its new setting's value (so Main picks up the
// tempo when the switch goes down, and its scale or decay when it comes up).  The knobs have a dead
// zone at each end, so the whole range of each setting can be reached.
//
// Inputs
//   Audio In 1, 2  Optional: a signal plugged in here comes out of Audio Out
//                1 or 2 shaped by that voice's envelope, in place of the
//                envelope itself
//   Pulse In 1   Clock.  Followed from its second pulse; with nothing
//                plugged in (or four periods, at least 2s, without a pulse)
//                the card keeps its own time: 120 BPM, until set by Main with
//                the switch held down, or by the web editor
// Outputs
//   CV Out 1, 2     Voice 1, 2 pitch, 1V/oct, C at 0V (calibrated)
//   Audio Out 1, 2  Voice 1, 2 envelope, 0 to +5V; or, with Audio In 1, 2
//                   patched, that input through the envelope
//   Pulse Out 1, 2  Voice 1, 2 gate
//
// USB, chosen once at power-up
//   Port supplying power (an 8mu, or nothing yet): USB host, reading the 8mu.
//   Computer plugged in: USB MIDI device called "Weights", for the
//   web editor (protocol in sysex.h).  An 8mu plugged into the computer is
//   passed on by the editor.  Either way the card runs on its own.

// First, so TinyUSB is configured for host and device modes before
// EightMU.h supplies its host-only defaults
#include "tusb_config.h"
#include "ComputerCard.h"
#include "EightMU.h"
#include "sysex.h"
#include "weights.h"
#include "ui.h"

using namespace weights;

class Weights;
static Weights *gCard = nullptr;


class Weights : public ComputerCard
{
public:
	Weights()
	{
		// Give the USB power circuitry time to settle, then pick the USB
		// mode once: host if the port is supplying power (an 8mu, or nothing
		// yet), device if a computer is (the web editor).  Boards older than
		// Rev 1.1 can't tell, and are always a device.
		sleep_us(150000);
		gCard = this;
		// Lets Connected() tell whether a jack is in Audio In 1 and 2
		EnableNormalisationProbe();
		hostMode = USBPowerState() == DFP;
		multicore_launch_core1(hostMode ? Core1Host : Core1Device);
	}

	// Runs 48000 times a second, in the audio interrupt.  It must finish
	// well inside the ~20us between samples: if it overruns, the Computer's
	// knob readings stop updating properly.  So the slower control work
	// (knobs, 8mu, LEDs, the web editor's status) is split into four parts,
	// each run every 32 samples on a different sample, rather than all at
	// once, and never on the same sample as the engine drawing new notes.
	virtual void ProcessSample()
	{
		uint32_t startUs = time_us_32();
		bool clockEdge = PulseIn1RisingEdge();
		// The exact moment of each clock is a little unpredictable, which
		// keeps the random lines from repeating after every power-up
		if (clockEdge) engine.Stir(startUs);

		engine.Tick(clockEdge);

		// On a sample where the engine has just drawn new notes, the control
		// work waits a sample, so the two never add up
		int part = engine.busy ? 1 : 0;
		if (!engine.busy)
		{
			if ((controlCount & 7) == 0) part = 2 + (controlCount >> 3);
			switch (controlCount)
			{
			case 0: ControlKnobs(); break;
			case 8: if (hostMode) EightMuInput(); break;
			case 16: ControlLeds(); break;
			case 24: if (WebLinked()) BuildStatus(); break;
			default: break;
			}
			controlCount = (controlCount + 1) & 31;
		}

		CVOut1Millivolts(engine.cvMv[0]);
		CVOut2Millivolts(engine.cvMv[1]);
		// Audio Out 1 and 2: each voice's envelope, or, with a jack in Audio
		// In 1 or 2, that input through the envelope (a VCA): a full
		// envelope passes the input at its own level
		AudioOut1(Connected(Audio1) ? int16_t((AudioIn1() * engine.level[0]) >> 15) : engine.audio[0]);
		AudioOut2(Connected(Audio2) ? int16_t((AudioIn2() * engine.level[1]) >> 15) : engine.audio[1]);
		PulseOut1(engine.gate[0]);
		PulseOut2(engine.gate[1]);

		// The longest this has taken over the last second, for the web
		// editor: overall, and for each part of the work (0 the engine alone,
		// 1 the engine drawing new choices, 2-5 the knobs, the 8mu, the LEDs
		// and the editor's status)
		uint32_t us = time_us_32() - startUs;
		if (us > busiestUs) busiestUs = us;
		if (us > busiestPart[part]) busiestPart[part] = us;
		if (++busySamples >= 48000)
		{
			busySamples = 0;
			shownBusiestUs = busiestUs;
			busiestUs = 0;
			for (int i = 0; i < 6; i++) {shownPart[i] = busiestPart[i]; busiestPart[i] = 0;}
		}
	}

private:
	Engine engine;
	EightMuUi ui;
	EightMU mu;

	int controlCount = 0;
	bool started = false;
	bool waiting = false;     // a knob is waiting to pick up its setting
	uint32_t busiestUs = 0, shownBusiestUs = 0;
	uint32_t busiestPart[6] = {}, shownPart[6] = {};
	int32_t busySamples = 0;

	// Knob settings, with soft takeover when the switch moves.
	// Up: clock ratio (X), slew (Y), scale (Main).
	// Middle: attack (X), pluckiness (Y), decay (Main).
	// Held down: Main is the internal tempo (as a knob position, 40-300 BPM).
	enum Setting {SetScale, SetRatio, SetSlew, SetDecay, SetAttack, SetPluck, SetTempo, kSettings};
	int32_t settings[kSettings] = {
		KnobForZone(0, kNumScales),           // Major
		KnobForZone(kUnityRatio, kNumRatios), // x1
		2048,                                 // no slew (centre)
		2325,                                 // decay ~300ms
		0,                                    // attack 0.5ms
		2048,                                 // halfway to a pluck
		KnobFromBpm(120),                     // tempo
	};
	static constexpr int kKnobSetting[2][3] = {
		{SetRatio, SetSlew, SetScale}, {SetAttack, SetPluck, SetDecay}};
	int knobBank = -1;        // 0 = up, 1 = middle: what X and Y (and Main) set
	bool mainOnTempo = false; // switch held down: Main sets the tempo
	bool knobLatched[3] = {};
	int32_t lastKnob[3] = {};

	int lastScale = -1;
	int scaleShow = 0;        // control ticks left of showing the scale
	int blink = 0;

	// 8mu on the card
	bool wasConnected = false;
	int connectHoldoff = 0;

	// USB mode, fixed at power-up
	bool hostMode = true;

	// Device mode: written on core1, read on core0
	volatile int32_t tempo = 120;      // internal clock, BPM
	volatile int32_t webTempo = -1;    // a tempo from the web editor, for core0
	volatile int webPage = -1;
	volatile bool resetRequest = false;
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// STATUS for the web editor, built on core0 and sent from core1
	static constexpr int kStatusPayload = sysex::kStatusLen - 5;
	volatile uint8_t status[kStatusPayload] = {};

	// Control work, each part every 32 samples (1.5kHz).  First the knobs
	// and the settings they make.
	void ControlKnobs()
	{
		if (!started)
		{
			// The knobs' last bits are noisy: another little stir
			started = true;
			engine.Stir(uint32_t(KnobVal(Main) * 7919 + KnobVal(X) * 104729 + KnobVal(Y)) ^ time_us_32());
		}
		if (resetRequest)
		{
			resetRequest = false;
			engine.DefaultFaders();
		}
		if (webTempo >= 0)
		{
			// The knob must pick the new tempo up before it changes it
			tempo = ClampBpm(webTempo);
			settings[SetTempo] = KnobFromBpm(tempo);
			if (mainOnTempo) knobLatched[2] = false;
			webTempo = -1;
		}
		if (webPage >= 0)
		{
			ui.SetPage(webPage);
			webPage = -1;
		}

		HandleKnobs();
		engine.scale = ScaleFromKnob(settings[SetScale]);
		engine.ratio = RatioFromKnob(settings[SetRatio]);
		// The exponential settings only when their knob has moved
		if (settings[SetSlew] != madeFrom[0]) {madeFrom[0] = settings[SetSlew]; engine.slewAlpha = SlewFromKnob(madeFrom[0]); engine.slewTiesOnly = SlewTiesOnly(madeFrom[0]);}
		if (settings[SetDecay] != madeFrom[1]) {madeFrom[1] = settings[SetDecay]; engine.decayInc = DecayFromKnob(madeFrom[1]);}
		if (settings[SetAttack] != madeFrom[2]) {madeFrom[2] = settings[SetAttack]; engine.attackInc = AttackFromKnob(madeFrom[2]);}
		engine.pluck = settings[SetPluck];
		if (tempo != madeFrom[3]) {madeFrom[3] = tempo; engine.internalPeriod = PeriodFromBpm(tempo);}
		if (engine.scale != lastScale)
		{
			if (lastScale >= 0) scaleShow = 1500; // show it for a second
			lastScale = engine.scale;
		}

		waiting = !knobLatched[0] || !knobLatched[1] || !knobLatched[2];
	}
	int32_t madeFrom[4] = {-1, -1, -1, -1};

	// The LEDs: the 8mu's (when it's on the card) and the Computer's.
	void ControlLeds()
	{
		if (hostMode && mu.Connected() && connectHoldoff == 0)
		{
			int32_t led[kFaders];
			ui.Leds(engine, led);
			for (int i = 0; i < kFaders; i++) mu.SetLed(i, led[i]);
		}

		// The Computer's LEDs.  For a second after the scale changes, its
		// number (1-37) in binary, LED 1 the lowest bit.  Otherwise LEDs 1-4
		// show the 8mu page's button (lit for a first page, blinking slowly
		// for a second, fast for a third, faster still for a fourth),
		// or with no 8mu the two gates; LEDs 5 and 6 the two envelopes.
		// While the switch is held down, LEDs 1-4 flash on every step.
		// LED 6 blinks fast while a knob is waiting to pick up.
		bool conn = mu.Connected() || WebLinked();
		blink = (blink + 1) % 600;
		if (scaleShow > 0)
		{
			scaleShow--;
			for (int i = 0; i < 6; i++) LedOn(i, ((engine.scale + 1) >> i) & 1);
		}
		else if (mainOnTempo)
		{
			// Switch held down: the top four LEDs flash on every step
			bool beat = engine.sinceStep < engine.stepLen / 4;
			for (int i = 0; i < 4; i++) LedOn(i, beat);
			LedBrightness(4, uint16_t((engine.audio[0] * 9831) >> 12));
			if (waiting) LedOn(5, blink % 150 < 75);
			else LedBrightness(5, uint16_t((engine.audio[1] * 9831) >> 12));
		}
		else
		{
			int button = ButtonOf(ui.page), layer = LayerOf(ui.page);
			static constexpr int kBlink[kLayers] = {1, 600, 200, 80};
			for (int i = 0; i < 4; i++)
			{
				bool on;
				if (conn) on = button == i && (layer == 0 || blink % kBlink[layer] < kBlink[layer] / 2);
				else on = i < 2 && engine.gate[i];
				LedOn(i, on);
			}
			LedBrightness(4, uint16_t((engine.audio[0] * 9831) >> 12));
			if (waiting) LedOn(5, blink % 150 < 75);
			else LedBrightness(5, uint16_t((engine.audio[1] * 9831) >> 12));
		}
	}

	// X, Y and Main, with soft takeover when the switch changes which
	// settings they control.  Down is momentary: while it's held Main sets the
	// tempo, and X and Y keep the settings of the position it was pressed
	// from.
	int SettingOf(int bank, int knob) const
	{
		return (knob == 2 && mainOnTempo) ? SetTempo : kKnobSetting[bank][knob];
	}

	void HandleKnobs()
	{
		Switch sw = SwitchVal();
		int bank = sw == Up ? 0 : (sw == Middle ? 1 : knobBank);
		if (bank < 0) bank = 1;
		const Knob knobs[3] = {X, Y, Main};
		int32_t k[3];
		for (int i = 0; i < 3; i++) k[i] = MapKnob(KnobVal(knobs[i]));
		if (knobBank < 0)
		{
			// Power-up: the current position's settings take the knobs as
			// they are
			knobBank = bank;
			mainOnTempo = sw == Down;
			for (int i = 0; i < 3; i++)
			{
				settings[SettingOf(bank, i)] = k[i];
				knobLatched[i] = true;
				lastKnob[i] = k[i];
			}
			if (mainOnTempo) tempo = BpmFromKnob(k[2]);
			return;
		}
		if (bank != knobBank)
		{
			knobBank = bank;
			knobLatched[0] = knobLatched[1] = knobLatched[2] = false;
		}
		if ((sw == Down) != mainOnTempo)
		{
			mainOnTempo = sw == Down;
			knobLatched[2] = false;
		}
		for (int i = 0; i < 3; i++)
		{
			int set = SettingOf(bank, i);
			int32_t &v = settings[set];
			if (!knobLatched[i])
			{
				int32_t d = k[i] - v;
				bool near = d > -48 && d < 48;
				bool crossed = (lastKnob[i] - v < 0) != (d < 0);
				if (near || crossed) knobLatched[i] = true;
			}
			if (knobLatched[i] && v != k[i])
			{
				v = k[i];
				if (set == SetTempo) tempo = BpmFromKnob(v);
			}
			lastKnob[i] = k[i];
		}
	}

	// The 8mu on the card: its buttons and faders
	void EightMuInput()
	{
		if (!mu.Connected())
		{
			wasConnected = false;
			return;
		}
		if (!wasConnected)
		{
			// Wait for the 8mu's reply to the fader position query before
			// trusting fader values
			wasConnected = true;
			connectHoldoff = 1500; // ~1s at control rate
			ui.Reset();
		}
		if (connectHoldoff > 0)
		{
			connectHoldoff--;
			return;
		}

		int32_t f[kFaders];
		bool b[4];
		for (int i = 0; i < kFaders; i++) f[i] = mu.Fader(i) >> 5;   // 0-127
		for (int i = 0; i < 4; i++) b[i] = mu.Button(i);
		uint32_t before = 0;
		for (int i = 0; i < kFaders; i++) before = before * 31 + uint32_t(engine.faders[ui.page][i]);
		ui.Update(engine, f, b);
		uint32_t after = 0;
		for (int i = 0; i < kFaders; i++) after = after * 31 + uint32_t(engine.faders[ui.page][i]);
		if (after != before) engine.Stir(after ^ time_us_32());
	}

	// STATUS for the web editor (only built while one is listening)
	void BuildStatus()
	{
		int n = 0;
		uint8_t shared = 0;
		for (int p = 0; p < kPairs; p++) if (engine.Shared(p)) shared |= uint8_t(1 << p);
		status[n++] = uint8_t((engine.clocked ? 1 : 0) | (mu.Connected() ? 2 : 0)
			| (knobBank == 0 ? 4 : 0) | (waiting ? 8 : 0) | (mainOnTempo ? 16 : 0)
			| (Connected(Audio1) ? 32 : 0) | (Connected(Audio2) ? 64 : 0));
		status[n++] = uint8_t((knobLatched[0] ? 0 : 1) | (knobLatched[1] ? 0 : 2)
			| (knobLatched[2] ? 0 : 4) | (shared << 3));
		status[n++] = uint8_t(engine.scale);
		status[n++] = uint8_t(engine.ratio);
		status[n++] = uint8_t(settings[SetSlew] >> 5);
		status[n++] = uint8_t(settings[SetAttack] >> 5);
		status[n++] = uint8_t(settings[SetDecay] >> 5);
		status[n++] = uint8_t(settings[SetPluck] >> 5);
		int32_t ms = engine.stepLen / 48;
		if (ms > 16383) ms = 16383;
		status[n++] = uint8_t(ms >> 7);
		status[n++] = uint8_t(ms & 127);
		// Position through the step, 0-127, in 32-bit sums
		int32_t len = engine.stepLen > 0 ? engine.stepLen : 1, at = engine.sinceStep < len ? engine.sinceStep : len;
		int32_t pos = len < (1 << 24) ? (at * 128) / len : at / (len >> 7);
		status[n++] = uint8_t(pos > 127 ? 127 : pos);
		status[n++] = uint8_t(engine.stepCount & 127);
		for (int v = 0; v < kVoices; v++)
		{
			const Engine::Voice &vc = engine.voice[v];
			status[n++] = uint8_t(vc.noteFader);
			status[n++] = uint8_t(vc.note & 127);
			status[n++] = uint8_t(vc.octaveFader);
			status[n++] = uint8_t(vc.gateFader);
			status[n++] = uint8_t(vc.ratchetFader);
			status[n++] = uint8_t(vc.hitCount & 127);
			status[n++] = uint8_t(engine.gate[v] ? 1 : 0);
			status[n++] = uint8_t((engine.audio[v] * 127) / 1706);
			int32_t mv = engine.cvMv[v] + 8192;
			if (mv < 0) mv = 0;
			if (mv > 16383) mv = 16383;
			status[n++] = uint8_t(mv >> 7);
			status[n++] = uint8_t(mv & 127);
			for (int k = 0; k < kEnvKinds; k++) status[n++] = uint8_t(vc.dev[k] < 0 ? 8 : vc.dev[k]);
		}
		status[n++] = uint8_t(tempo >> 7);
		status[n++] = uint8_t(tempo & 127);
		status[n++] = uint8_t(shownBusiestUs > 127 ? 127 : shownBusiestUs);
		for (int i = 0; i < 6; i++) status[n++] = uint8_t(shownPart[i] > 127 ? 127 : shownPart[i]);
	}

	//------------------------------------------------------------------------
	// USB, on core1
	//------------------------------------------------------------------------

	// The web editor counts as linked while its pings keep arriving
	bool WebLinked() const
	{
		return !hostMode && pinged && (time_us_32() - lastPingUs) < 3000000;
	}

	// Host mode: read an 8mu plugged straight into the Computer
	static void Core1Host()
	{
		board_init();
		tuh_init(0);
		while (true)
		{
			gCard->mu.Poll();
		}
	}

	// Device mode: talk to the web editor
	static void Core1Device()
	{
		board_init();
		tud_init(0);
		sysex::Parser parser;
		uint32_t lastStatusUs = 0;
		while (true)
		{
			tud_task();
			uint8_t buf[64];
			while (tud_midi_available())
			{
				uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
				if (n == 0) break;
				for (uint32_t i = 0; i < n; i++)
				{
					if (parser.Feed(buf[i]))
					{
						gCard->OnSysEx(parser.cmd, parser.payload, parser.length);
					}
				}
			}

			uint32_t now = time_us_32();
			if (gCard->WebLinked() && now - lastStatusUs >= 33000)
			{
				lastStatusUs = now;
				uint8_t msg[sysex::kStatusLen];
				int len = gCard->EncodeStatus(msg);
				Write(msg, len);
			}
		}
	}

	// Send a whole message, waiting briefly for room if need be.  If the
	// computer isn't reading, the rest is dropped; the editor's parser
	// resynchronises on the next F0.
	static void Write(const uint8_t *msg, int len)
	{
		uint32_t start = time_us_32();
		int sent = 0;
		while (sent < len && tud_mounted())
		{
			sent += int(tud_midi_stream_write(0, msg + sent, uint32_t(len - sent)));
			if (sent < len)
			{
				if (time_us_32() - start > 50000) return;
				tud_task();
			}
		}
	}

public:
	// Handle one message from the web editor.  Public so it can be tested.
	void OnSysEx(uint8_t cmd, const uint8_t *p, int len)
	{
		switch (cmd)
		{
		case sysex::Hello:
			SendState();
			break;
		case sysex::Set:
			if (len >= 3 && p[0] < kPages && p[1] < kFaders)
			{
				engine.faders[p[0]][p[1]] = uint8_t(p[2] & 0x7F);
			}
			break;
		case sysex::SetAll:
			if (len >= 3 + sysex::kNumValues && p[0] == sysex::kVersion)
			{
				webTempo = sysex::Get14(p + 1);
				for (int i = 0; i < sysex::kNumValues; i++)
				{
					engine.faders[i / kFaders][i % kFaders] = uint8_t(p[3 + i] & 0x7F);
				}
			}
			break;
		case sysex::Page:
			if (len >= 1 && p[0] < kPages) webPage = p[0];
			break;
		case sysex::Reset:
			engine.DefaultFaders();
			resetRequest = true; // and again on core0, in case of a race
			SendState();
			break;
		case sysex::Ping:
			lastPingUs = time_us_32();
			pinged = true;
			break;
		case sysex::Tempo:
			if (len >= 2) webTempo = sysex::Get14(p);
			break;
		default:
			break;
		}
	}

	int EncodeState(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::State);
		out[n++] = sysex::kVersion;
		out[n++] = uint8_t(ui.page);
		n += sysex::Put14(out + n, tempo);
		for (int i = 0; i < sysex::kNumValues; i++)
		{
			out[n++] = uint8_t(engine.faders[i / kFaders][i % kFaders] & 0x7F);
		}
		out[n++] = 0xF7;
		return n;
	}

	int EncodeStatus(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::Status);
		for (int i = 0; i < kStatusPayload; i++) out[n++] = status[i] & 0x7F;
		out[n++] = 0xF7;
		return n;
	}

private:
	void SendState()
	{
		uint8_t msg[sysex::kStateLen];
		Write(msg, EncodeState(msg));
	}
};


int main()
{
	set_sys_clock_khz(200000, true);

	static Weights card;
	card.Run();
	return 0;
}
