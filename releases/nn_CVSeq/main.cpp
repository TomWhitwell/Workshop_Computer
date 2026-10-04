// CVSeq
//
// An eight-step CV sequencer for the Music Thing Workshop Computer, after the
// Performer modulator in Native Instruments' Massive: each step plays a shape,
// a morph between two shapes from a library of 32, and the steps run on one
// after another as a continuous control voltage.  Edited from a Music Thing
// 8mu, over USB MIDI host, or from the web editor in web/index.html.
//
// The 8mu's eight faders edit the eight steps; its four buttons choose what
// they edit.  Each button has two pages: pressing it again flips to its second
// page, and pressing a different button always starts on that button's first.
//
//             First page                          Second page
//   Button A  SHAPE 1  first shape (of 32)        START  level at the step's start
//   Button B  SHAPE 2  second shape (of 32)       END    level at the step's end
//   Button C  MORPH    shape 1 to shape 2         QUANT  digital stepping of the shape
//   Button D  LEVEL    step level                 CHANCE chance the step plays
//
// START and END make a ramp across the step that multiplies the shape (and
// LEVEL).  QUANT samples the step into fewer, held stairs as it rises, from
// smooth (fully down) to a single held value (top).  A step that loses its
// CHANCE roll holds the last output for its length, and gives no trigger.
//
// After a page change the faders 'pick up': a fader only takes over its step
// once it has been moved to (or across) the value already stored.  The 8mu's
// LEDs show the stored values on the current page, the playing step full on.
//
// Panel
//   Main knob   Rate, 8s to 10ms per step, plus CV In 2 at 1V/oct
//   Switch up   X = depth (-100% to +100%), Y = offset (-5V to +5V)
//   Switch mid  X = smoothing (off to ~2s), Y = morph offset (all steps)
//   Switch down Tap to step the direction: step forward, step ping-pong,
//               step backward, true ping-pong, true reverse
//
// The four X/Y settings each keep their value; after the switch moves a knob
// does nothing until it reaches its new setting's value, so nothing jumps.
// LED 5 blinks fast while one is waiting.
//
// Directions
//   Step forward     steps 1-8, each shape forwards
//   Step ping-pong   steps 1-8 then 7-2, each shape forwards
//   Step backward    steps 8-1, each shape forwards
//   True ping-pong   the whole sequence forwards, then the whole sequence in
//                    reverse (steps 8-1, each shape backwards)
//   True reverse     the whole sequence in reverse
//
// Inputs
//   CV In 1     Morph offset, added to every step's MORPH (+5V = all the way)
//   CV In 2     Rate, 1V/oct
//   Pulse In 1  Clock: while clocks arrive, each step lasts one clock
//   Pulse In 2  Restart from the first step
//
// Outputs
//   CV Out 1    The sequence: offset + depth x (0 to 5V), smoothed
//   CV Out 2    The same, quantised to semitones (1V/oct)
//   Audio Out 1 The same as CV Out 1, uncalibrated
//   Audio Out 2 CV Out 1 inverted, uncalibrated
//   Pulse Out 1 Trigger at the start of every step that plays
//   Pulse Out 2 Trigger at the start of the sequence
//
// 8mu motion
//   Pitch (tilt front/back)  morph offset, added to the knob and CV In 1
//
// USB, chosen once at power-up
//   Port supplying power (an 8mu, or nothing yet): USB host, reading the 8mu.
//   Computer plugged in: USB MIDI device called "CVSeq", for the web editor
//   (protocol in sysex.h).  An 8mu plugged into the computer is passed on by
//   the editor.  Either way the card runs on its own.

// First, so TinyUSB is configured for host and device modes before
// EightMU.h supplies its host-only defaults
#include "tusb_config.h"
#include "ComputerCard.h"
#include "EightMU.h"
#include "sysex.h"
#include "shapes.h"

class CVSeq;
static CVSeq *gCard = nullptr;


class CVSeq : public ComputerCard
{
public:
	static constexpr int kSteps = 8;
	// Page = button + 4 * layer
	enum Page {PageShape1, PageShape2, PageMorph, PageLevel,
		PageStart, PageEnd, PageQuant, PageChance, kPages};
	enum Direction {StepForward, StepPingPong, StepBackward, TruePingPong,
		TrueReverse, kDirections};

	CVSeq()
	{
		shapegen::Build();
		for (int i = 0; i <= 256; i++)
		{
			exp2Tab[i] = uint32_t(1073741824.0f * exp2f(float(i) / 256.0f));
		}

		SetDefaults();
		Restart();

		// Give the USB power circuitry time to settle, then pick the USB
		// mode once: host if the port is supplying power (an 8mu, or nothing
		// yet), device if a computer is (the web editor).  Boards older than
		// Rev 1.1 can't tell, and are always a device.
		sleep_us(150000);
		gCard = this;
		hostMode = USBPowerState() == DFP;
		multicore_launch_core1(hostMode ? Core1Host : Core1Device);
	}

	// Default sequence, so the card does something with no 8mu.
	// Values are in 8mu fader units (0-127), stored shifted up to 0-4064.
	void SetDefaults()
	{
		static const uint8_t defShape1[kSteps] = {3, 4, 7, 26, 16, 8, 14, 30};
		static const uint8_t defShape2[kSteps] = {4, 3, 6, 27, 17, 9, 15, 31};
		static const uint8_t defLevel[kSteps] = {127, 100, 127, 80, 127, 100, 127, 80};
		for (int i = 0; i < kSteps; i++)
		{
			params[PageShape1][i] = ShapeFader(defShape1[i]) << 5;
			params[PageShape2][i] = ShapeFader(defShape2[i]) << 5;
			params[PageMorph][i] = 0;
			params[PageLevel][i] = defLevel[i] << 5;
			params[PageStart][i] = 127 << 5;
			params[PageEnd][i] = 127 << 5;
			params[PageQuant][i] = 0;
			params[PageChance][i] = 127 << 5;
		}
	}

	// Fader value (0-127) in the middle of shape s's range
	static int ShapeFader(int s) {return s * 4 + 2;}

	virtual void ProcessSample()
	{
		// Restart on a rising edge at Pulse In 2 (or from the web editor)
		bool restart = PulseIn2RisingEdge();
		if (restartRequest)
		{
			restartRequest = false;
			restart = true;
		}

		// A tap down on the switch steps the direction
		if (SwitchChanged() && SwitchVal() == Down)
		{
			direction = (direction + 1) % kDirections;
			dirShow = 1500; // show it on the LEDs for ~1s
		}

		// Clock on Pulse In 1
		bool clockEdge = PulseIn1RisingEdge();
		if (samplesSinceClock < 0x7FFFFFFF) samplesSinceClock++;
		if (clockEdge)
		{
			if (haveClock)
			{
				clockPeriod = samplesSinceClock;
				if (clockPeriod < 48) clockPeriod = 48;
				if (clockPeriod > 8 * 48000) clockPeriod = 8 * 48000;
			}
			haveClock = true;
			samplesSinceClock = 0;
		}
		clocked = haveClock && samplesSinceClock < 2 * 48000;

		if (++controlCount >= 32)
		{
			controlCount = 0;
			Control();
		}

		// Step phase: free running wraps into the next step; clocked holds
		// at the end of the step until the clock arrives
		if (restart)
		{
			Restart();
		}
		else if (clocked)
		{
			uint32_t old = phase;
			phase += phaseInc;
			if (phase < old) phase = 0xFFFFFFFF;
			if (clockEdge)
			{
				// The first clock after a restart starts the step rather
				// than ending it
				if (swallowClock) {swallowClock = false; phase = 0;}
				else {phase = 0; Advance();}
			}
		}
		else
		{
			uint32_t old = phase;
			phase += phaseInc;
			if (phase < old) Advance();
		}

		// The step's value, 0-4095
		int32_t val;
		if (holding)
		{
			val = heldVal;
		}
		else
		{
			uint32_t u = reversed ? ~phase : phase;
			if (quantN > 0)
			{
				uint32_t k = uint32_t((uint64_t(u) * uint32_t(quantN)) >> 32);
				u = k * quantStep;
			}
			int32_t v1 = ShapeAt(shape1, u);
			int32_t v2 = ShapeAt(shape2, u);
			int32_t v = v1 + (((v2 - v1) * morph) >> 12);
			int32_t t = int32_t(u >> 20); // 0-4095 through the step
			int32_t env = startLevel + (((endLevel - startLevel) * t) >> 12);
			val = (((v * env) >> 12) * level) >> 12;
			lastVal = val;
		}

		// Depth and offset, then smoothing
		int32_t mv = offsetMv + ((((depth * val) >> 12) * 5000) >> 12);
		if (smoothAlpha >= (1 << 24))
		{
			smoothed = int64_t(mv) << 16;
		}
		else
		{
			smoothed += (((int64_t(mv) << 16) - smoothed) * smoothAlpha) >> 24;
		}
		int32_t out = int32_t(smoothed >> 16);
		if (out > 6000) out = 6000;
		if (out < -6000) out = -6000;
		outMv = out;

		CVOut1Millivolts(out);
		int32_t semis = (out * 12 + (out >= 0 ? 500 : -500)) / 1000;
		CVOut2Millivolts((semis * 1000) / 12);
		AudioOut1(int16_t((out * 349) >> 10));
		AudioOut2(int16_t(-((out * 349) >> 10)));

		if (stepTrig) stepTrig--;
		if (seqTrig) seqTrig--;
		PulseOut1(stepTrig > 0);
		PulseOut2(seqTrig > 0);
	}

private:
	EightMU mu;

	// Step parameters, stored as raw fader values 0-4095.  Written by core1
	// in device mode, as the web editor sends them.
	volatile int32_t params[kPages][kSteps];

	// 8mu paging and fader pickup
	volatile int page = PageShape1;
	int pageBlink = 0;
	bool latched[kSteps] = {};
	int32_t lastFader[kSteps] = {};
	bool lastFaderValid = false;
	bool prevButton[EightMU::numButtons] = {};
	bool wasConnected = false;
	int connectHoldoff = 0;

	// Sequencer state
	int cur = 0;
	int travel = 1;           // +1 or -1, for the ping-pongs
	bool reversed = false;    // this step plays its shape backwards
	volatile int direction = StepForward; // also set by the web editor
	int dirShow = 0;
	uint32_t phase = 0, phaseInc = 89478;
	bool holding = false;     // lost its chance roll
	int32_t heldVal = 0, lastVal = 0;
	int clockCount = 0;
	bool haveClock = false, clocked = false, swallowClock = false;
	int32_t samplesSinceClock = 0x7FFFFFFF;
	int32_t clockPeriod = 24000;
	int controlCount = 0;
	int stepTrig = 0, seqTrig = 0;
	uint32_t rng = 0x2545F491;

	// The current step, refreshed at control rate so edits are heard live
	const int16_t *shape1 = gShapes[0], *shape2 = gShapes[0];
	int32_t morph = 0, level = 4096, startLevel = 4096, endLevel = 4096; // Q12
	int quantN = 0;           // 0 = smooth, else stairs per step
	uint32_t quantStep = 0;

	// Output
	int32_t depth = 4096;     // Q12, signed
	int32_t offsetMv = 0;
	int32_t smoothAlpha = 1 << 24;
	int64_t smoothed = 0;     // millivolts, Q16
	volatile int32_t outMv = 0;
	int32_t morphOffset = 0;  // Q12, signed
	int32_t rateOct = 0;      // Q12 octaves, 0 = one step a second
	int32_t tiltMorph = 0;

	// X/Y knob settings, with soft takeover when the switch moves.
	// Up: depth, offset.  Middle: smoothing, morph offset.
	enum Setting {SetDepth, SetOffset, SetSmooth, SetMorph, kSettings};
	int32_t settings[kSettings] = {4095, 2048, 0, 2048};
	int knobBank = -1;        // 0 = up pair, 1 = middle pair
	bool knobLatched[2] = {};
	int32_t lastKnob[2] = {};

	uint32_t exp2Tab[257]; // 2^(i/256) in Q30

	// USB mode, fixed at power-up
	bool hostMode = true;

	// Device mode: written on core1, read on core0
	volatile bool restartRequest = false;
	volatile int32_t webPitch = 0, webRoll = 0;
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// Snapshot for the web editor: written on core0, read on core1
	volatile uint8_t stCur = 0, stProgress = 0, stFlags = 0, stFlags2 = 0;
	volatile uint8_t stDepth = 0, stOffset = 0, stSmooth = 0, stMorph = 0;
	volatile int32_t stOut = 0, stRate = 0;

	// base * 2^(oct/4096)
	uint32_t ExpScale(uint32_t base, int32_t oct) const
	{
		int32_t whole = oct >> 12;
		int32_t frac = oct & 4095;
		int i = frac >> 4, r = frac & 15;
		uint32_t m = exp2Tab[i] + (((exp2Tab[i + 1] - exp2Tab[i]) * uint32_t(r)) >> 4);
		uint64_t v = (uint64_t(base) * m) >> 30;
		if (whole >= 0)
		{
			if (whole > 31) return 0xFFFFFFFF;
			v <<= whole;
		}
		else
		{
			if (whole < -31) return 0;
			v >>= -whole;
		}
		return v > 0xFFFFFFFFull ? 0xFFFFFFFF : uint32_t(v);
	}

	static int32_t ShapeAt(const int16_t *tab, uint32_t u)
	{
		uint32_t i = u >> 24;
		int32_t f = int32_t((u >> 8) & 0xFFFF);
		return tab[i] + (((tab[i + 1] - tab[i]) * f) >> 16);
	}

	// Stored fader value (0-4064) to 0-4096, so a fader at the top is 100%
	static int32_t Q12(int32_t v)
	{
		return v >= 4064 ? 4096 : (v * 4096) / 4064;
	}

	uint32_t NextRandom()
	{
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return rng;
	}

	int FirstStep() const
	{
		return (direction == StepBackward || direction == TrueReverse) ? kSteps - 1 : 0;
	}

	// Load the current step's settings
	void LoadStep()
	{
		shape1 = gShapes[(params[PageShape1][cur] >> 5) >> 2];
		shape2 = gShapes[(params[PageShape2][cur] >> 5) >> 2];
		int32_t m = Q12(params[PageMorph][cur]) + morphOffset;
		morph = m < 0 ? 0 : (m > 4096 ? 4096 : m);
		level = Q12(params[PageLevel][cur]);
		startLevel = Q12(params[PageStart][cur]);
		endLevel = Q12(params[PageEnd][cur]);
		// QUANT: down = smooth, then 32 stairs per step down to 1 at the top
		int q = params[PageQuant][cur] >> 5;
		int n = q == 0 ? 0 : 32 - ((q - 1) * 31) / 126;
		if (n != quantN)
		{
			quantN = n;
			quantStep = n > 0 ? 0xFFFFFFFFu / uint32_t(n) : 0;
		}
	}

	// Roll the current step's chance; a step that loses holds the output
	void BeginStep()
	{
		int32_t c = params[PageChance][cur];
		bool plays = c >= 127 << 5 || int32_t(NextRandom() % 4064u) < c;
		holding = !plays;
		heldVal = lastVal;
		LoadStep();
		if (plays) stepTrig = 480;
	}

	void Advance()
	{
		switch (direction)
		{
		case StepForward:
			cur = (cur + 1) % kSteps;
			reversed = false;
			break;
		case StepBackward:
			cur = (cur + kSteps - 1) % kSteps;
			reversed = false;
			break;
		case TrueReverse:
			cur = (cur + kSteps - 1) % kSteps;
			reversed = true;
			break;
		case StepPingPong:
		{
			// Turn at the ends without playing the end step twice
			int n = cur + travel;
			if (n < 0 || n >= kSteps) {travel = -travel; n = cur + travel;}
			cur = n;
			reversed = false;
			break;
		}
		default: // TruePingPong
		{
			// Turning round plays the end step again, backwards: the
			// sequence mirrored, so the CV turns round without a jump
			int n = cur + travel;
			if (n < 0 || n >= kSteps) {travel = -travel; n = cur;}
			cur = n;
			reversed = travel < 0;
			break;
		}
		}
		BeginStep();
		if (cur == FirstStep() && !(direction == TruePingPong && reversed)) seqTrig = 480;
	}

	void Restart()
	{
		cur = FirstStep();
		travel = 1;
		reversed = direction == TrueReverse;
		phase = 0;
		swallowClock = true;
		BeginStep();
		stepTrig = holding ? 0 : 480;
		seqTrig = 480;
	}

	// Runs every 32 samples (1.5kHz)
	void Control()
	{
		HandleKnobs();

		// Rate: Main knob, 8s (-3 octaves from one step a second) to 10ms
		// (+6.64 octaves), plus CV In 2 at 1V/oct
		rateOct = -12288 + (KnobVal(Main) * 39485) / 4095 + CVIn2() * 12;
		if (rateOct < -16384) rateOct = -16384;
		if (rateOct > 28672) rateOct = 28672;
		phaseInc = clocked ? 0xFFFFFFFFu / uint32_t(clockPeriod)
			: ExpScale(89478, rateOct); // 2^32 / 48000: one step a second

		depth = (settings[SetDepth] - 2048) * 2;
		if (depth > 4096) depth = 4096;
		if (depth < -4096) depth = -4096;
		offsetMv = ((settings[SetOffset] - 2048) * 5000) / 2048;

		// Smoothing: off at zero, then a time constant of 1ms to ~2s
		int32_t s = settings[SetSmooth];
		if (s < 32) smoothAlpha = 1 << 24;
		else
		{
			float tau = 0.001f * exp2f(float(s) * (11.0f / 4095.0f));
			smoothAlpha = int32_t(16777216.0f * (1.0f - expf(-1.0f / (tau * 48000.0f))));
			if (smoothAlpha < 1) smoothAlpha = 1;
		}

		if (hostMode) HandleEightMU();
		else tiltMorph = webPitch * 2;

		// Morph offset: knob, CV In 1 (+5V = all the way) and tilt
		morphOffset = (settings[SetMorph] - 2048) * 2 + (CVIn1() * 12) / 5 + tiltMorph;

		LoadStep(); // so edits to the playing step are heard straight away

		// Snapshot for the web editor
		bool waiting = !knobLatched[0] || !knobLatched[1];
		stCur = uint8_t(cur);
		stProgress = uint8_t(phase >> 25);
		stFlags = uint8_t((direction & 7) | (clocked ? 8 : 0) | (mu.Connected() ? 16 : 0)
			| (knobBank == 0 ? 32 : 0) | (waiting ? 64 : 0));
		stFlags2 = uint8_t((reversed ? 1 : 0) | (holding ? 2 : 0));
		stOut = outMv + 8192;
		stRate = (rateOct >> 4) + 8192;
		stDepth = uint8_t((settings[SetDepth] >> 5) & 127);
		stOffset = uint8_t((settings[SetOffset] >> 5) & 127);
		stSmooth = uint8_t((settings[SetSmooth] >> 5) & 127);
		stMorph = uint8_t((settings[SetMorph] >> 5) & 127);
		if (dirShow > 0) dirShow--;

		// Computer LEDs: page (or step, with no 8mu), direction after a
		// tap, CV Out 1 level, and connection
		bool conn = mu.Connected() || WebLinked();
		pageBlink = (pageBlink + 1) % 900;
		for (int i = 0; i < 4; i++)
		{
			if (dirShow > 0)
			{
				// Direction 1-5 in binary-ish: 1-4 LEDs, five = all blink
				bool on = direction < 4 ? i <= direction : pageBlink % 300 < 150;
				LedOn(i, on);
			}
			else if (conn)
			{
				// First page lit; second page blinks slowly
				bool second = page >= 4;
				LedOn(i, (page & 3) == i && (!second || pageBlink < 450));
			}
			else LedBrightness(i, (cur & 3) == i ? (cur < 4 ? 4095 : 1024) : 0);
		}
		int32_t lv = outMv < 0 ? 0 : (outMv * 4095) / 5000;
		LedBrightness(4, uint16_t(lv > 4095 ? 4095 : lv));
		static int blink = 0;
		blink = (blink + 1) % 300;
		LedBrightness(5, waiting ? (blink < 150 ? 4095 : 0) : (conn ? 4095 : 0));
	}

	// X and Y knobs, with soft takeover when the switch changes which pair
	// of settings they control.  Down is momentary and keeps the pair of the
	// position it was pressed from.
	void HandleKnobs()
	{
		Switch sw = SwitchVal();
		int bank = sw == Up ? 0 : (sw == Middle ? 1 : knobBank);
		if (bank < 0) bank = 1;
		int32_t k[2] = {KnobVal(X), KnobVal(Y)};
		if (knobBank < 0)
		{
			// Power-up: the current pair takes the knobs as they are
			knobBank = bank;
			for (int i = 0; i < 2; i++)
			{
				settings[bank * 2 + i] = k[i];
				knobLatched[i] = true;
				lastKnob[i] = k[i];
			}
			return;
		}
		if (bank != knobBank)
		{
			knobBank = bank;
			knobLatched[0] = knobLatched[1] = false;
		}
		for (int i = 0; i < 2; i++)
		{
			int32_t &v = settings[bank * 2 + i];
			if (!knobLatched[i])
			{
				int32_t d = k[i] - v;
				bool near = d > -48 && d < 48;
				bool crossed = (lastKnob[i] - v < 0) != (d < 0);
				if (near || crossed) knobLatched[i] = true;
			}
			if (knobLatched[i]) v = k[i];
			lastKnob[i] = k[i];
		}
	}

	void HandleEightMU()
	{
		bool conn = mu.Connected();
		if (!conn)
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
			lastFaderValid = false;
			for (int i = 0; i < kSteps; i++) latched[i] = false;
		}
		if (connectHoldoff > 0)
		{
			connectHoldoff--;
			return;
		}

		// Page buttons
		for (int b = 0; b < EightMU::numButtons; b++)
		{
			bool down = mu.Button(b);
			if (down && !prevButton[b])
			{
				// Same button again flips between its two pages; another
				// button starts on its first page
				page = (page & 3) == b ? (page ^ 4) : b;
				for (int i = 0; i < kSteps; i++) latched[i] = false;
			}
			prevButton[b] = down;
		}

		// Faders, with pickup
		for (int i = 0; i < kSteps; i++)
		{
			int32_t f = mu.Fader(i);
			volatile int32_t &p = params[page][i];
			if (!latched[i])
			{
				int32_t d = f - p;
				bool near = d > -96 && d < 96;
				bool crossed = lastFaderValid && ((lastFader[i] - p < 0) != (d < 0));
				if (near || crossed) latched[i] = true;
			}
			if (latched[i]) p = f;
			lastFader[i] = f;

			// 8mu LEDs: stored value, playing step full on
			mu.SetLed(i, i == cur ? 4095 : (p * 9) >> 4);
		}
		lastFaderValid = true;

		// Motion: front/back tilt is a morph offset
		tiltMorph = mu.Pitch() * 2;
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
			if (len >= 3 && p[0] < kPages && p[1] < kSteps)
			{
				params[p[0]][p[1]] = int32_t(p[2] & 0x7F) << 5;
			}
			break;
		case sysex::SetAll:
			if (len >= 1 + sysex::kNumValues && p[0] == sysex::kVersion)
			{
				for (int i = 0; i < sysex::kNumValues; i++)
				{
					params[i / kSteps][i % kSteps] = int32_t(p[1 + i] & 0x7F) << 5;
				}
			}
			break;
		case sysex::Page:
			if (len >= 1 && p[0] < kPages) page = p[0];
			break;
		case sysex::Reset:
			SetDefaults();
			restartRequest = true;
			SendState();
			break;
		case sysex::Motion:
			if (len >= 4)
			{
				webPitch = sysex::Get14(p) - 2048;
				webRoll = sysex::Get14(p + 2) - 2048;
			}
			break;
		case sysex::Ping:
			lastPingUs = time_us_32();
			pinged = true;
			break;
		case sysex::Restart:
			restartRequest = true;
			break;
		case sysex::Direction:
			if (len >= 1 && p[0] < kDirections) direction = p[0];
			break;
		default:
			break;
		}
	}

	int EncodeState(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::State);
		out[n++] = sysex::kVersion;
		out[n++] = uint8_t(page);
		for (int i = 0; i < sysex::kNumValues; i++)
		{
			out[n++] = uint8_t((params[i / kSteps][i % kSteps] >> 5) & 0x7F);
		}
		out[n++] = 0xF7;
		return n;
	}

	int EncodeStatus(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::Status);
		out[n++] = stCur;
		out[n++] = stProgress;
		out[n++] = stFlags;
		out[n++] = stFlags2;
		n += sysex::Put14(out + n, stOut);
		n += sysex::Put14(out + n, stRate);
		out[n++] = stDepth;
		out[n++] = stOffset;
		out[n++] = stSmooth;
		out[n++] = stMorph;
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

	static CVSeq card;
	card.Run();
}
