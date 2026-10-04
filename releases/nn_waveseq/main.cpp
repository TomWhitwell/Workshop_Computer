// Wave Sequencer
//
// Wavestation-style wave sequencing for the Music Thing Workshop Computer,
// edited from a Music Thing 8mu over USB MIDI host.
//
// An eight-step sequence, where every step plays a wave from a bank of 64
// single-cycle waves for a set time, at a set pitch and level, crossfading
// into the next step.  The 8mu's eight faders edit the eight steps; its four
// buttons choose which property of the steps the faders are editing:
//
//   Button A  WAVE   wave position, scanning through the 64-wave bank
//   Button B  TIME   step duration, 20ms to 4s; fully down skips the step
//   Button C  PITCH  -12 to +12 semitones
//   Button D  LEVEL  step loudness
//
// After a page change the faders 'pick up': a fader only takes over its step
// once it has been moved to (or across) the value already stored, so changing
// page never makes the sound jump.  The 8mu's LEDs show the stored values on
// the current page, with the playing step lit fully.
//
// Panel
//   Main knob   Pitch (C1 to C7), plus CV In 1 at 1V/oct
//   X knob      Sequence speed, 1/8x to 8x, plus CV In 2 at 1V/oct
//   Y knob      Crossfade, from a hard cut to fading over the whole step
//   Switch up   Ping-pong
//   Switch mid  Forward loop
//   Switch down Restart from the first step
//
// Inputs
//   Audio In 1  Linear FM
//   Audio In 2  Wave scan, offsetting every step's wave position
//   CV In 1     Pitch, 1V/oct
//   CV In 2     Speed, 1V/oct
//   Pulse In 1  Restart from the first step
//   Pulse In 2  Clock: while clocks arrive, each step lasts its TIME fader's
//               number of clocks (1-8) instead of a time
//
// Outputs
//   Audio Out 1 Wave sequence
//   Audio Out 2 Wave sequence, slightly detuned (8mu roll widens it)
//   CV Out 1    Pitch of the current step, 1V/oct, 0V = no offset
//   CV Out 2    Level of the current step, crossfaded, 0-5V
//   Pulse Out 1 Trigger on every step
//   Pulse Out 2 Trigger at the start of the sequence
//
// 8mu motion
//   Pitch (tilt front/back)  scans every step's wave position, +/-16 waves
//   Roll (tilt left/right)   detunes Audio Out 2 by up to +/-50 cents
//
// USB, chosen once at power-up
//   Port supplying power (an 8mu, or nothing yet): USB host, reading the 8mu.
//   Computer plugged in: USB MIDI device called "Wave Sequencer", for the web
//   editor in web/index.html (protocol in sysex.h).  An 8mu plugged into the
//   computer is passed on by the editor.  Either way the card runs on its
//   own; the editor only adds a picture of the sequence and presets.

// First, so TinyUSB is configured for host and device modes before
// EightMU.h supplies its host-only defaults
#include "tusb_config.h"
#include "ComputerCard.h"
#include "EightMU.h"
#include "sysex.h"
#include "wavetables.h"

class WaveSeq;
static WaveSeq *gCard = nullptr;


class WaveSeq : public ComputerCard
{
public:
	static constexpr int kSteps = 8;
	enum Page {PageWave, PageTime, PagePitch, PageLevel, kPages};

	WaveSeq()
	{
		wavegen::Build();
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

	// Default sequence, so the card plays something with no 8mu.
	// Values are in 8mu fader units (0-127), stored shifted up to 0-4064.
	void SetDefaults()
	{
		static const uint8_t defWave[kSteps] = {6, 22, 34, 50, 67, 81, 95, 116};
		for (int i = 0; i < kSteps; i++)
		{
			params[PageWave][i] = defWave[i] << 5;
			params[PageTime][i] = 64 << 5;
			params[PagePitch][i] = 64 << 5;
			params[PageLevel][i] = 127 << 5;
		}
	}

	virtual void ProcessSample()
	{
		// Restart: rising edge on Pulse In 1, or switch pushed down
		bool restart = PulseIn1RisingEdge() || (SwitchChanged() && SwitchVal() == Down);
		if (restartRequest)
		{
			restartRequest = false;
			restart = true;
		}
		pingPong = SwitchVal() == Up;

		// Clock on Pulse In 2
		bool clockEdge = PulseIn2RisingEdge();
		if (samplesSinceClock < 0x7FFFFFFF) samplesSinceClock++;
		if (clockEdge)
		{
			if (haveClock)
			{
				clockPeriod = samplesSinceClock;
				if (clockPeriod < 48) clockPeriod = 48;
				if (clockPeriod > 4 * 48000) clockPeriod = 4 * 48000;
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

		if (restart)
		{
			Restart();
		}
		else if (clocked)
		{
			elapsed += 256;
			// The first clock after a restart starts the step rather than
			// counting towards its end
			if (clockEdge && swallowClock)
			{
				swallowClock = false;
				elapsed = 0;
			}
			else if (clockEdge && ++clockCount >= ClocksForStep(cur))
			{
				Advance();
			}
		}
		else
		{
			elapsed += speed;
			if (elapsed >= stepLen) Advance();
		}

		// Crossfade into the next step over the last part of this one
		int32_t mix = 0;
		int32_t xStart = stepLen - xfLen;
		if (elapsed > xStart)
		{
			mix = (elapsed >= stepLen) ? 4096
				: ((((elapsed - xStart) >> xfShift) << 12) / ((xfLen >> xfShift) | 1));
			if (mix > 4096) mix = 4096;
		}
		lastMix = mix;
		int32_t gA = (levelA * (4096 - mix)) >> 12;
		int32_t gB = (levelB * mix) >> 12;

		// Wave scan from Audio In 2 (+/-32 waves)
		int32_t scan = AudioIn2() * 4;
		int32_t wA = ClampWave(waveA + scan);
		int32_t wB = ClampWave(waveB + scan);

		// Linear FM from Audio In 1
		int32_t fm = AudioIn1();
		if (fm > -8 && fm < 8) fm = 0;

		phA += incA + (int32_t(incA >> 11) * fm);
		phB += incB + (int32_t(incB >> 11) * fm);
		ph2A += inc2A + (int32_t(inc2A >> 11) * fm);
		ph2B += inc2B + (int32_t(inc2B >> 11) * fm);

		int32_t out1 = Osc(phA, wA, mipA) * gA;
		int32_t out2 = Osc(ph2A, wA, mipA) * gA;
		if (gB)
		{
			out1 += Osc(phB, wB, mipB) * gB;
			out2 += Osc(ph2B, wB, mipB) * gB;
		}
		AudioOut1(int16_t(out1 >> 16));
		AudioOut2(int16_t(out2 >> 16));

		// Level envelope, 0 to 5V
		CVOut2(int16_t(((levelA * (4096 - mix) + levelB * mix) >> 12) * 1706 >> 12));

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
	volatile int page = PageWave;
	bool latched[kSteps] = {};
	int32_t lastFader[kSteps] = {};
	bool lastFaderValid = false;
	bool prevButton[EightMU::numButtons] = {};
	bool wasConnected = false;
	int connectHoldoff = 0;

	// Sequencer state
	int cur = 0, nxt = 0;
	int dir = 1, nxtDir = 1;
	bool pingPong = false;
	int32_t elapsed = 0;      // time into step, samples in Q8
	int32_t stepLen = 256;    // step length, samples in Q8
	int32_t xfLen = 256;      // crossfade length, samples in Q8
	int xfShift = 0;          // keeps the crossfade division within 32 bits
	int32_t speed = 256;      // elapsed increment per sample
	int clockCount = 0;
	bool haveClock = false, clocked = false, swallowClock = false;
	int32_t samplesSinceClock = 0x7FFFFFFF;
	int32_t clockPeriod = 24000;
	int controlCount = 0;
	int stepTrig = 0, seqTrig = 0;

	// Global controls, updated at control rate
	int32_t baseNote = 60 << 8; // Q8 semitones
	int32_t detune = 15;        // Q8 semitones, for Audio Out 2
	int32_t xfAmount = 0;       // Q12
	int32_t tiltScan = 0;       // Q8 waves

	// Voice slots: A is the current step, B the next one being faded into
	uint32_t phA = 0, phB = 0, ph2A = 0, ph2B = 0;
	uint32_t incA = 0, incB = 0, inc2A = 0, inc2B = 0;
	int32_t waveA = 0, waveB = 0; // Q8 wave position
	int32_t levelA = 0, levelB = 0; // Q12
	int mipA = 0, mipB = 0;

	uint32_t exp2Tab[257]; // 2^(i/256) in Q30

	// USB mode, fixed at power-up
	bool hostMode = true;

	// Device mode: written on core1, read on core0
	volatile bool restartRequest = false;
	volatile int32_t webPitch = 0, webRoll = 0;
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// Snapshot for the web editor: written on core0, read on core1
	volatile uint8_t stCur = 0, stNext = 0, stMix = 0, stProgress = 0, stFlags = 0;
	volatile uint8_t stXfade = 0;
	volatile int32_t stNote = 0, stSpeed = 0;
	int32_t lastMix = 0;

	static constexpr int32_t kSkipBelow = 128;
	static constexpr uint32_t kIncNote0 = 731558; // MIDI note 0, 8.18Hz
	static constexpr int32_t kMaxWave = (kNumWaves - 1) << 8;

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

	static int32_t ClampWave(int32_t w)
	{
		return w < 0 ? 0 : (w > kMaxWave ? kMaxWave : w);
	}

	// One sample of the wavetable oscillator, morphing between adjacent
	// waves.  Returns roughly +/-32767.
	static int32_t Osc(uint32_t phase, int32_t wavePos, int mip)
	{
		int wi = wavePos >> 8;
		int32_t wf = wavePos & 255;
		const int16_t *a = gWaves[mip][wi];
		const int16_t *b = gWaves[mip][wi < kNumWaves - 1 ? wi + 1 : wi];
		uint32_t i = phase >> 24;
		int32_t f = (phase >> 9) & 0x7FFF;
		int32_t sa = a[i] + (((a[i + 1] - a[i]) * f) >> 15);
		int32_t sb = b[i] + (((b[i + 1] - b[i]) * f) >> 15);
		return sa + (((sb - sa) * wf) >> 8);
	}

	static int MipFor(uint32_t inc)
	{
		return inc < kMipMaxInc[0] ? 0 : (inc < kMipMaxInc[1] ? 1 : 2);
	}

	bool Active(int s) const {return params[PageTime][s] >= kSkipBelow;}

	int ClocksForStep(int s) const
	{
		int c = 1 + ((params[PageTime][s] - kSkipBelow) * 8) / (4096 - kSkipBelow);
		return c < 1 ? 1 : (c > 8 ? 8 : c);
	}

	int FirstActive() const
	{
		for (int i = 0; i < kSteps; i++) if (Active(i)) return i;
		return 0;
	}

	// The step after s, travelling in direction d
	void NextStep(int s, int d, int &ns, int &nd) const
	{
		ns = s; nd = d;
		if (!pingPong)
		{
			nd = 1;
			for (int k = 1; k <= kSteps; k++)
			{
				int i = (s + k) % kSteps;
				if (Active(i)) {ns = i; return;}
			}
			return;
		}
		for (int pass = 0; pass < 2; pass++)
		{
			for (int i = s + d; i >= 0 && i < kSteps; i += d)
			{
				if (Active(i)) {ns = i; nd = d; return;}
			}
			d = -d; // reached an end: turn round
		}
	}

	int32_t StepSemitones(int s) const
	{
		return ((params[PagePitch][s] * 25) >> 12) - 12;
	}

	void SetSlot(bool b, int s)
	{
		int32_t note = baseNote + (StepSemitones(s) << 8);
		if (note < 0) note = 0;
		if (note > (127 << 8)) note = 127 << 8;
		uint32_t inc = ExpScale(kIncNote0, (note * 4) / 3);
		uint32_t inc2 = ExpScale(kIncNote0, ((note + detune) * 4) / 3);
		int32_t w = ClampWave(params[PageWave][s] * kMaxWave / 4064 + tiltScan);
		int32_t lv = params[PageLevel][s];
		lv = (lv * lv) >> 12;
		if (b) {incB = inc; inc2B = inc2; waveB = w; levelB = lv; mipB = MipFor(inc);}
		else {incA = inc; inc2A = inc2; waveA = w; levelA = lv; mipA = MipFor(inc);}
	}

	void Advance()
	{
		int prev = cur;
		cur = nxt;
		dir = nxtDir;

		// Slot B becomes the current slot.  Phases are aligned, so that a
		// crossfade between steps at the same pitch never phase-cancels.
		phA = phB; ph2A = ph2B;
		incA = incB; inc2A = inc2B;
		waveA = waveB; levelA = levelB; mipA = mipB;
		phB = phA; ph2B = ph2A;

		if (clocked) elapsed = 0;
		else
		{
			elapsed -= stepLen;
			if (elapsed < 0 || elapsed > stepLen) elapsed = 0;
		}
		clockCount = 0;
		swallowClock = false;

		NextStep(cur, dir, nxt, nxtDir);
		SetSlot(true, nxt);
		UpdateTiming();

		if (cur != prev || nxt != cur) stepTrig = 480;
		if (cur == FirstActive() && cur != prev) seqTrig = 480;
	}

	void Restart()
	{
		cur = FirstActive();
		dir = 1;
		elapsed = 0;
		clockCount = 0;
		phA = phB = ph2A = ph2B = 0;
		swallowClock = true;
		NextStep(cur, dir, nxt, nxtDir);
		SetSlot(false, cur);
		SetSlot(true, nxt);
		UpdateTiming();
		stepTrig = 480;
		seqTrig = 480;
	}

	// Step and crossfade lengths for the current step
	void UpdateTiming()
	{
		if (clocked)
		{
			stepLen = clockPeriod * ClocksForStep(cur) * 256;
		}
		else
		{
			// 20ms to ~4s, exponential across the fader
			int32_t x = params[PageTime][cur] - kSkipBelow;
			if (x < 0) x = 0;
			stepLen = int32_t(ExpScale(960 * 256, (x * 31293) / 3968));
		}
		int32_t xf = int32_t((int64_t(stepLen) * xfAmount) >> 12);
		if (xf < 48 * 256) xf = 48 * 256; // at least 1ms, to avoid clicks
		if (xf > stepLen) xf = stepLen;
		xfLen = xf;
		xfShift = 0;
		while ((xf >> xfShift) >= (1 << 19)) xfShift++;
	}

	// Runs every 32 samples (1.5kHz)
	void Control()
	{
		// Panel
		baseNote = (24 << 8) + (KnobVal(Main) * 72 * 256) / 4095 + CVIn1() * 9;
		int32_t spd = (KnobVal(X) - 2048) * 6 + CVIn2() * 12;
		if (spd < -24576) spd = -24576;
		if (spd > 24576) spd = 24576;
		speed = int32_t(ExpScale(256, spd));
		xfAmount = KnobVal(Y);

		if (hostMode)
		{
			HandleEightMU();
		}
		else
		{
			tiltScan = webPitch * 2;
			detune = 15 + webRoll / 16;
		}

		// Keep slots and timing following edits, the pitch knob, CV and tilt.
		// Which step comes next is only chosen again while slot B is silent,
		// so a crossfade never switches to a different step part-way; but
		// the step being faded into still follows everything else.
		if (elapsed <= stepLen - xfLen)
		{
			NextStep(cur, dir, nxt, nxtDir);
		}
		SetSlot(false, cur);
		SetSlot(true, nxt);
		UpdateTiming();

		CVOut1MIDINote(uint8_t(60 + StepSemitones(cur)));

		// Snapshot for the web editor
		stCur = uint8_t(cur);
		stNext = uint8_t(nxt);
		stMix = uint8_t(lastMix >> 5 > 127 ? 127 : lastMix >> 5);
		{
			int32_t pr = stepLen > 0 ? int32_t((int64_t(elapsed) * 127) / stepLen) : 0;
			stProgress = uint8_t(pr < 0 ? 0 : (pr > 127 ? 127 : pr));
		}
		stFlags = uint8_t((pingPong ? 1 : 0) | (clocked ? 2 : 0) | (mu.Connected() ? 4 : 0));
		stNote = baseNote >> 5;
		stSpeed = (spd >> 4) + 2048;
		stXfade = uint8_t(xfAmount >> 5);

		// Computer LEDs: page (or step, with no 8mu), step trigger, 8mu
		bool conn = mu.Connected() || WebLinked();
		for (int i = 0; i < 4; i++)
		{
			if (conn) LedOn(i, i == page);
			else LedBrightness(i, (cur & 3) == i ? (cur < 4 ? 4095 : 1024) : 0);
		}
		LedOn(4, stepTrig > 0);
		LedOn(5, conn);
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
			if (down && !prevButton[b] && b != page)
			{
				page = b;
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

		// Motion
		tiltScan = mu.Pitch() * 2;
		detune = 15 + mu.Roll() / 16;
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
		out[n++] = stNext;
		out[n++] = stMix;
		out[n++] = stProgress;
		out[n++] = stFlags;
		n += sysex::Put14(out + n, stNote);
		n += sysex::Put14(out + n, stSpeed);
		out[n++] = stXfade;
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

	static WaveSeq card;
	card.Run();
}
