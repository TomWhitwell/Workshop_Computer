// Wave Sequencer
//
// Wavestation-style wave sequencing for the Music Thing Workshop Computer,
// edited from a Music Thing 8mu over USB MIDI host.
//
// An eight-step sequence, where every step plays a wave from a bank of 64
// single-cycle waves for a set time, at a set pitch and level, crossfading
// into the next step.  The 8mu's eight faders edit the eight steps; its four
// buttons choose which property of the steps the faders are editing.  Each
// button has two pages: pressing it again flips to its second page, and
// pressing a different button always starts on that button's first page.
//
//             First page                         Second page
//   Button A  WAVE  position in the 64-wave bank FM    per-step FM amount
//   Button B  TIME  20ms to 4s; down = skip      SCAN  per-step wave scan amount
//   Button C  PITCH -12 to +12 semitones         GLIDE pitch slides into the step
//   Button D  LEVEL step loudness                GATE  gate length on Pulse Out 1
//
// The per-step FM and SCAN amounts (default 100%) multiply the panel's FM and
// wave scan amounts, and crossfade between steps along with everything else.
//
// GLIDE: 0 jumps to the step's pitch; otherwise the pitch slides from where
// the previous step was, starting as the step begins to be heard (the start
// of the crossfade into it), over up to the whole length of the step.
// GATE: 0 gives no pulse for the step; otherwise Pulse Out 1 is high for
// that fraction of the step (at least 5ms), and at the top it ties into the
// next step.  Default is half the step.
//
// After a page change the faders 'pick up': a fader only takes over its step
// once it has been moved to (or across) the value already stored, so changing
// page never makes the sound jump.  The 8mu's LEDs show the stored values on
// the current page, with the playing step lit fully.
//
// Panel
//   Main knob   Pitch (C1 to C7), plus CV In 1 at 1V/oct
//   Switch up   X = sequence speed (1/8x to 8x), Y = crossfade (hard cut to
//               fading over the whole step)
//   Switch mid  X = FM amount (Audio In 1), Y = wave scan amount (Audio In 2)
//   Switch down Tap to step the direction: forward, ping-pong, random
//
// The four X/Y settings each keep their value.  After the switch moves, a
// knob does nothing until it reaches (or passes) its new setting's value,
// so nothing jumps; LED 5 blinks fast while one is waiting.
//
// Inputs
//   Audio In 1  Linear FM, depth set by X with the switch in the middle
//   Audio In 2  Wave scan, offsetting every step's wave position, depth set
//               by Y with the switch in the middle
//   CV In 1     Pitch, 1V/oct
//   CV In 2     Speed, 1V/oct
//   Pulse In 1  Clock: while clocks arrive, each step lasts its TIME fader's
//               number of clocks (1-8) instead of a time.  Clocks up to 20s
//               apart; followed from the second pulse, and the speed knob
//               takes over again after four of the clock's periods (at
//               least 2s) without one
//   Pulse In 2  Restart from the first step
//
// Outputs
//   Audio Out 1 Wave sequence
//   Audio Out 2 Wave sequence, slightly detuned (8mu roll widens it)
//   CV Out 1    Pitch of the current step, 1V/oct, 0V = no offset
//   CV Out 2    Level of the current step, crossfaded, 0-5V
//   Pulse Out 1 Gate for each step, length from the GATE page
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
	// Page = button + 4 * layer
	enum Page {PageWave, PageTime, PagePitch, PageLevel,
		PageFM, PageScan, PageGlide, PageGate, kPages};

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
			params[PageFM][i] = 127 << 5;
			params[PageScan][i] = 127 << 5;
			params[PageGlide][i] = 0;
			params[PageGate][i] = 64 << 5;
		}
	}

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
			// Two clocks up to 20s apart give the period; until then (and
			// after the clock stops) the speed knob keeps time
			if (haveClock && samplesSinceClock <= kMaxClockPeriod)
			{
				clockPeriod = samplesSinceClock < 48 ? 48 : samplesSinceClock;
				periodKnown = true;
			}
			haveClock = true;
			samplesSinceClock = 0;
		}
		// The clock counts as stopped after four of its own periods without
		// one (never less than 2s), so slow clocks are followed rather than
		// dropped mid-step
		int32_t timeout = clockPeriod > 24000 ? clockPeriod * 4 : 2 * 48000;
		if (periodKnown && samplesSinceClock >= timeout) periodKnown = false;
		clocked = periodKnown;

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
			// Waiting for a late clock holds at the end of the step
			elapsed += 256;
			if (elapsed > stepLen) elapsed = stepLen;
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

		// Per-step FM and scan amounts, crossfaded like the levels
		int32_t stepFM = (fmA * (4096 - mix) + fmB * mix) >> 12;
		int32_t stepScan = (scanA * (4096 - mix) + scanB * mix) >> 12;

		// Wave scan from Audio In 2 (up to +/-32 waves)
		int32_t scan = (((AudioIn2() * scanAmount) >> 12) * stepScan) >> 10;
		int32_t wA = ClampWave(waveA + scan);
		int32_t wB = ClampWave(waveB + scan);

		// Linear FM from Audio In 1
		int32_t fm = AudioIn1();
		if (fm > -8 && fm < 8) fm = 0;
		fm = (((fm * fmAmount) >> 12) * stepFM) >> 12;

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
	int pageBlink = 0;
	bool latched[kSteps] = {};
	int32_t lastFader[kSteps] = {};
	bool lastFaderValid = false;
	bool prevButton[EightMU::numButtons] = {};
	bool wasConnected = false;
	int connectHoldoff = 0;

	// Sequencer state
	int cur = 0, nxt = 0;
	int dir = 1, nxtDir = 1;
	enum Direction {DirForward, DirPingPong, DirRandom, kDirections};
	volatile int direction = DirForward; // also set by the web editor
	int lastDirection = DirForward;
	int dirShow = 0;
	uint32_t rng = 0x12345678;
	int32_t elapsed = 0;      // time into step, samples in Q8
	int32_t stepLen = 256;    // step length, samples in Q8
	int32_t xfLen = 256;      // crossfade length, samples in Q8
	int xfShift = 0;          // keeps the crossfade division within 32 bits
	int32_t speed = 256;      // elapsed increment per sample
	int clockCount = 0;
	bool haveClock = false, clocked = false, swallowClock = false;
	int32_t samplesSinceClock = 0x7FFFFFFF;
	int32_t clockPeriod = 24000;
	bool periodKnown = false;
	// Up to 20s a clock: with up to 8 clocks a step, the step length in
	// 1/256 samples still fits 32 bits
	static constexpr int32_t kMaxClockPeriod = 20 * 48000;
	int controlCount = 0;
	int stepTrig = 0, seqTrig = 0;

	// Global controls, updated at control rate
	int32_t baseNote = 60 << 8; // Q8 semitones
	int32_t detune = 15;        // Q8 semitones, for Audio Out 2
	int32_t xfAmount = 0;       // Q12
	int32_t fmAmount = 0;       // Q12, squared law
	int32_t scanAmount = 0;     // Q12

	// X/Y knob settings, with soft takeover when the switch moves.
	// Up: speed, crossfade.  Middle: FM amount, wave scan amount.
	enum Setting {SetSpeed, SetFade, SetFM, SetScan, kSettings};
	int32_t settings[kSettings] = {2048, 1024, 0, 0};
	int knobBank = -1;          // 0 = up pair, 1 = middle pair
	bool knobLatched[2] = {};
	int32_t lastKnob[2] = {};
	int32_t tiltScan = 0;       // Q8 waves

	// Voice slots: A is the current step, B the next one being faded into
	uint32_t phA = 0, phB = 0, ph2A = 0, ph2B = 0;
	uint32_t incA = 0, incB = 0, inc2A = 0, inc2B = 0;
	int32_t waveA = 0, waveB = 0; // Q8 wave position
	int32_t levelA = 0, levelB = 0; // Q12
	int32_t fmA = 4096, fmB = 4096;     // per-step FM amount, Q12
	int32_t scanA = 4096, scanB = 4096; // per-step scan amount, Q12
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
	volatile uint8_t stXfade = 0, stFM = 0, stScan = 0;
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

	uint32_t NextRandom()
	{
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return rng >> 8;
	}

	// The step after s, travelling in direction d
	void NextStep(int s, int d, int &ns, int &nd)
	{
		ns = s; nd = d;
		if (direction == DirRandom)
		{
			// Any other active step, chosen at random
			int choices[kSteps], n = 0;
			for (int i = 0; i < kSteps; i++) if (Active(i) && i != s) choices[n++] = i;
			nd = 1;
			if (n > 0) ns = choices[NextRandom() % uint32_t(n)];
			return;
		}
		if (direction == DirForward)
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

	// Glide into a step: its pitch starts 'from' (Q8 semitones) away from the
	// step's own pitch and slides to it over len samples
	struct Glide
	{
		int32_t from = 0, len = 0, pos = 0;
		bool started = false;
		int32_t Offset() const
		{
			if (!started || pos >= len || len <= 0) return 0;
			return int32_t((int64_t(from) * (len - pos)) / len);
		}
	};
	Glide glideA, glideB;

	// Pitch of a slot relative to the base note, Q8 semitones, with glide
	int32_t RelPitch(int s, const Glide &g) const
	{
		return (StepSemitones(s) << 8) + g.Offset();
	}

	// Length of step s in samples, at the current speed or clock
	int32_t StepSamples(int s) const
	{
		if (clocked) return clockPeriod * ClocksForStep(s);
		int32_t x = params[PageTime][s] - kSkipBelow;
		if (x < 0) x = 0;
		int64_t q8 = ExpScale(960 * 256, (x * 31293) / 3968);
		return int32_t(q8 / (speed > 0 ? speed : 1));
	}

	// Start slot B gliding, from wherever slot A's pitch is now
	void StartGlideB()
	{
		if (glideB.started) return;
		glideB.started = true;
		glideB.pos = 0;
		glideB.from = RelPitch(cur, glideA) - (StepSemitones(nxt) << 8);
		glideB.len = int32_t((int64_t(StepSamples(nxt)) * Q12(params[PageGlide][nxt])) >> 12);
	}

	// Start the gate on Pulse Out 1 for the step just begun
	void StartGate()
	{
		int32_t g = params[PageGate][cur];
		if (g < 32) {stepTrig = 0; return;}
		// At the top, stay high until the next step starts: a tie
		if (g >= 127 << 5) {stepTrig = 0x7FFFFFFF; return;}
		int32_t len = int32_t((int64_t(StepSamples(cur)) * Q12(g)) >> 12);
		stepTrig = len < 240 ? 240 : len;
	}

	void SetSlot(bool b, int s)
	{
		int32_t note = baseNote + RelPitch(s, b ? glideB : glideA);
		if (note < 0) note = 0;
		if (note > (127 << 8)) note = 127 << 8;
		uint32_t inc = ExpScale(kIncNote0, (note * 4) / 3);
		uint32_t inc2 = ExpScale(kIncNote0, ((note + detune) * 4) / 3);
		int32_t w = ClampWave(params[PageWave][s] * kMaxWave / 4064 + tiltScan);
		int32_t lv = params[PageLevel][s];
		lv = (lv * lv) >> 12;
		int32_t fa = Q12(params[PageFM][s]), sa = Q12(params[PageScan][s]);
		if (b) {incB = inc; inc2B = inc2; waveB = w; levelB = lv; mipB = MipFor(inc); fmB = fa; scanB = sa;}
		else {incA = inc; inc2A = inc2; waveA = w; levelA = lv; mipA = MipFor(inc); fmA = fa; scanA = sa;}
	}

	// Stored fader value (0-4064) to 0-4096, so a fader at the top is 100%
	static int32_t Q12(int32_t v)
	{
		return v >= 4064 ? 4096 : (v * 4096) / 4064;
	}

	void Advance()
	{
		// Start the next step's glide now if the crossfade was too short for
		// Control to see it begin, while cur is still the outgoing step
		StartGlideB();
		int prev = cur;
		cur = nxt;
		dir = nxtDir;

		// Slot B becomes the current slot.  Phases are aligned, so that a
		// crossfade between steps at the same pitch never phase-cancels.
		phA = phB; ph2A = ph2B;
		incA = incB; inc2A = inc2B;
		waveA = waveB; levelA = levelB; mipA = mipB;
		fmA = fmB; scanA = scanB;
		phB = phA; ph2B = ph2A;
		glideA = glideB;
		glideB = Glide();

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

		StartGate();
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
		glideA = Glide();
		glideB = Glide();
		NextStep(cur, dir, nxt, nxtDir);
		SetSlot(false, cur);
		SetSlot(true, nxt);
		UpdateTiming();
		StartGate();
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
		baseNote = (24 << 8) + (MapKnob(KnobVal(Main), false) * 72 * 256) / 4095 + CVIn1() * 9;
		HandleKnobs();
		// Speed: 1/8x at 0, 1x at 2048, 8x at 4095 (+/-3 octaves), plus CV
		int32_t sd = settings[SetSpeed] - 2048;
		int32_t spd = (sd * 12288) / (sd > 0 ? 2047 : 2048) + CVIn2() * 12;
		if (spd < -24576) spd = -24576;
		if (spd > 24576) spd = 24576;
		speed = int32_t(ExpScale(256, spd));
		xfAmount = settings[SetFade];
		fmAmount = (settings[SetFM] * settings[SetFM]) >> 12;
		scanAmount = settings[SetScan];

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
		// In random mode the next step is rolled once per step (in Advance),
		// and only rolled again here if it has since been skipped.
		if (elapsed <= stepLen - xfLen)
		{
			if (direction != DirRandom || direction != lastDirection || !Active(nxt)
				|| (nxt == cur && ActiveCount() > 1))
			{
				NextStep(cur, dir, nxt, nxtDir);
			}
			lastDirection = direction;
		}
		SetSlot(false, cur);
		SetSlot(true, nxt);
		UpdateTiming();

		// Glides advance at control rate; slot B's starts with the crossfade
		if (glideA.started) glideA.pos += 32;
		if (glideB.started) glideB.pos += 32;
		else if (lastMix > 0) StartGlideB();

		// Current step's pitch, gliding, as 1V/oct (1000/12 mV per semitone)
		CVOut1Millivolts((RelPitch(cur, glideA) * 1000) / (12 * 256));

		// Snapshot for the web editor
		stCur = uint8_t(cur);
		stNext = uint8_t(nxt);
		stMix = uint8_t(lastMix >> 5 > 127 ? 127 : lastMix >> 5);
		{
			int32_t pr = stepLen > 0 ? int32_t((int64_t(elapsed) * 127) / stepLen) : 0;
			stProgress = uint8_t(pr < 0 ? 0 : (pr > 127 ? 127 : pr));
		}
		bool waiting = !knobLatched[0] || !knobLatched[1];
		stFlags = uint8_t((direction & 3) | (clocked ? 4 : 0) | (mu.Connected() ? 8 : 0)
			| (knobBank == 0 ? 16 : 0) | (waiting ? 32 : 0));
		stFM = uint8_t(settings[SetFM] >> 5);
		stScan = uint8_t(settings[SetScan] >> 5);
		stNote = baseNote >> 5;
		stSpeed = (spd >> 4) + 2048;
		stXfade = uint8_t(xfAmount >> 5);
		if (dirShow > 0) dirShow--;

		// Computer LEDs: page (or step, with no 8mu), step trigger, 8mu
		bool conn = mu.Connected() || WebLinked();
		for (int i = 0; i < 4; i++)
		{
			if (dirShow > 0) LedOn(i, i <= direction && i < 3); // 1, 2 or 3 LEDs
			else if (conn)
			{
				// First page lit; second page blinks slowly
				bool second = page >= 4;
				LedOn(i, (page & 3) == i && (!second || pageBlink < 450));
			}
			else LedBrightness(i, (cur & 3) == i ? (cur < 4 ? 4095 : 1024) : 0);
		}
		LedOn(4, stepTrig > 0);
		// LED 5: connection, blinking fast while a knob waits to pick up
		pageBlink = (pageBlink + 1) % 900;
		static int blink = 0;
		blink = (blink + 1) % 300;
		LedBrightness(5, waiting ? (blink < 150 ? 4095 : 0) : (conn ? 4095 : 0));
	}

	int ActiveCount() const
	{
		int n = 0;
		for (int i = 0; i < kSteps; i++) if (Active(i)) n++;
		return n;
	}

	// A knob reading (0-4095) with dead zones, as the knobs don't always
	// reach the very ends of their range: the outer kKnobEndZone at each end
	// reads as fully 0 or 4095, and the travel between is stretched to fill
	// the range.  Two-sided controls also get kKnobCentreZone either side of
	// the middle, which reads as exactly 2048 (speed 1x).
	static constexpr int32_t kKnobEndZone = 128;
	static constexpr int32_t kKnobCentreZone = 64;
	static int32_t MapKnob(int32_t raw, bool twoSided)
	{
		int32_t v = ((raw - kKnobEndZone) * 4095) / (4095 - 2 * kKnobEndZone);
		if (v < 0) v = 0;
		if (v > 4095) v = 4095;
		if (!twoSided) return v;
		int32_t d = v - 2048;
		if (d > -kKnobCentreZone && d < kKnobCentreZone) return 2048;
		if (d > 0) return 2048 + ((d - kKnobCentreZone) * 2047) / (2047 - kKnobCentreZone);
		return 2048 + ((d + kKnobCentreZone) * 2048) / (2048 - kKnobCentreZone);
	}

	// X and Y knobs, with soft takeover when the switch changes which pair
	// of settings they control.  Down is momentary and keeps the pair of the
	// position it was pressed from.
	void HandleKnobs()
	{
		Switch sw = SwitchVal();
		int bank = sw == Up ? 0 : (sw == Middle ? 1 : knobBank);
		if (bank < 0) bank = 1;
		// Speed is two-sided (1x in the middle), with a centre zone
		int32_t k[2] = {MapKnob(KnobVal(X), bank * 2 == SetSpeed),
			MapKnob(KnobVal(Y), false)};
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
		out[n++] = stNext;
		out[n++] = stMix;
		out[n++] = stProgress;
		out[n++] = stFlags;
		n += sysex::Put14(out + n, stNote);
		n += sysex::Put14(out + n, stSpeed);
		out[n++] = stXfade;
		out[n++] = stFM;
		out[n++] = stScan;
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
