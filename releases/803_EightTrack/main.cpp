// EightTrack
//
// A stereo multi-head tape delay for the Music Thing Workshop Computer,
// loosely after Zlosynth's Kaseta: one loop of tape, recorded at the input,
// with eight playback heads along it.  Each head has its own time, volume,
// pan, stereo width and feedback, and the feedback runs through a low-pass
// and high-pass filter and a tape model (saturation, hiss, wow, flutter and
// dropouts) on its way back onto the tape.  Edited from a Music Thing 8mu,
// over USB MIDI host, or from the web editor in web/index.html.
//
// The 8mu's eight faders are the eight heads.  Its four buttons, acted on
// when released:
//
//   Short press  the page, below.  Pressing a button again moves on to its
//                next page (C and D have two), and round to its first;
//                another button always starts on its first.
//   Long press D (half a second or more) freeze on or off.
//   Hold A + move a fader   load preset 1-8 (the fader's number) from flash.
//   Hold D + move a fader   save the settings as preset 1-8.
//
//             First page                    Second page
//   Button A  TIME      each head's delay       REVERSE  each head backwards
//   Button B  VOLUME    each head's level
//   Button C  PAN       each head's pan     WIDTH  each head's stereo width
//   Button D  FEEDBACK  each head's feedback  FX   filter and tape (below)
//
// REVERSE: a fader in the top half plays its head backwards.  A reversed head
// plays the tape in slices as long as the head's time, each one backwards,
// arriving on average a head's time later, with a 4ms fade at each join.
//
// FX page, one setting a fader
//   1 Low-pass   in the feedback path, 150Hz to 16kHz, off at the top
//   2 High-pass  in the feedback path, off at the bottom, then 20Hz to 2kHz
//   3 Saturation tape drive on the way onto the tape
//   4 Hiss       tape noise recorded with the feedback
//   5 Wow        slow, drifting pitch wobble
//   6 Flutter    fast pitch wobble
//   7 Dropouts   random moments where the tape loses level
//   8 Mix        dry only (down), both (centre), wet only (up)
//
// After a page change the faders 'pick up': a fader only takes over once it
// has been moved to (or across) the value already stored.  The 8mu's LEDs
// show the stored values on the current page.
//
// Time
//   Free (nothing in Pulse In 1, no tap tempo): a fader sets its head
//   anywhere from 0 to 0.5s, times the Main knob's multiplier, x1/4 (fully
//   down) to x4 (fully up), x1 at the centre.  Times glide, so moving a
//   fader or Main bends the pitch of what's on the tape, as a tape does.
//
//   Synced (a jack in Pulse In 1, or tap tempo): each head is a whole number
//   of sixteenths, 1/16 (fader down) to 16/16 (up), of a beat x4: so a
//   sixteenth note to a bar of four, with Pulse In 1 as quarter notes.  Main
//   picks a ratio, in nine equal zones: /4, /3, /2, x2/3, x1 (centre), x3/2,
//   x2, x3, x4.  A time longer than the tape holds is halved until it fits.
//
// Panel
//   Main        time multiplier (free) or clock ratio (synced)
//   X           volume and feedback multiplier, x0 to x2, x1 at the centre
//   Y           pan and width multiplier, x0 (all heads mono, centred) to x2
//   Switch up   freeze: the input is cut and the tape loops the longest
//               head that's playing, so what's on it repeats unchanged
//   Switch mid  run
//   Switch down momentary: tap tempo.  Two taps (up to 3s apart) set the
//               beat and sync the heads to it; hold it down for 1.5s to go
//               back to free time.  With a jack in Pulse In 1, the clock
//               wins and taps are ignored.
//
// Inputs
//   Audio In 1, 2  left and right in; with only Audio In 1 patched, it
//               feeds both sides
//   CV In 1     added to X: +5V adds x1 to volume and feedback
//   CV In 2     added to Y: +5V adds x1 to pan and width
//   Pulse In 1  clock, one pulse a beat.  Plugging a jack in syncs the
//               heads: to the clock from its second pulse, and until then
//               to the last beat it had (120bpm at first)
//   Pulse In 2  freeze, while high
//
// Outputs
//   Audio Out 1, 2  left and right out
//   CV Out 1    the tape's wow and flutter, +-5V at full depth
//   CV Out 2    envelope of the delayed signal, 0 to 5V
//   Pulse Out 1 a trigger every beat (synced), or every free-time length
//               (fader at the top) when free
//   Pulse Out 2 high while frozen
//
// 8mu motion
//   Pitch (tilt front/back)  bends every head's time up to an octave either
//                            way, past about 15 degrees from flat, like a
//                            hand on the reel
//
// The tape is a buffer in RAM: stereo, 12 bits, at 24kHz (so the echoes
// are a little darker than the dry signal, as on tape), about 2.2 seconds
// long.  Presets live in the last 4kB sector of the program card's flash,
// which every card has, so it needs no more than a 2MB card.
//
// USB, chosen once at power-up
//   Port supplying power (an 8mu, or nothing yet): USB host, reading the 8mu.
//   Computer plugged in: USB MIDI device called "EightTrack", for the web
//   editor (protocol in sysex.h).  An 8mu plugged into the computer is passed
//   on by the editor.  Either way the card runs on its own.

// First, so TinyUSB is configured for host and device modes before
// EightMU.h supplies its host-only defaults
#include "tusb_config.h"
#include "ComputerCard.h"
#include "EightMU.h"
#include "sysex.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/systick.h"
#include <string.h>
#include <math.h>

class EightTrack;
static EightTrack *gCard = nullptr;

// The tape: stereo frames of two 12-bit samples packed into three bytes, at
// 24kHz.  Sized to fill what RAM the program leaves.
static constexpr int kTapeFrames = 53000;
static uint8_t gTape[kTapeFrames * 3];

// tanh(x) for x = 0 to 4 in 512 steps, Q15
static int16_t gTanh[513];
// One cycle of a sine, 256 steps and a guard point, Q15
static int16_t gSine[257];
// One-pole coefficients (Q16) for the low-pass and high-pass faders, made
// at power-up, as working them out takes longer than a sample
static int32_t gLpCoef[128], gHpCoef[128];

// Presets, kept in the last sector of flash.  The same layout is kept in
// RAM, and written back whenever it changes.
struct PresetStore
{
	uint32_t magic;
	uint8_t version;
	uint8_t lastSlot;          // sysex::kNoSlot for none
	uint8_t pad[2];
	uint8_t valid[sysex::kSlots];
	uint8_t values[sysex::kSlots][sysex::kNumValues];
};
static constexpr uint32_t kPresetMagic = 0x4B543845; // "E8TK"
static constexpr uint8_t kPresetVersion = 2;
// Version 1, before the REVERSE page: read and carried over at power-up
struct PresetStoreV1
{
	uint32_t magic;
	uint8_t version, lastSlot, pad[2];
	uint8_t valid[sysex::kSlots];
	uint8_t values[sysex::kSlots][6 * 8];
};
static constexpr uint32_t kPresetOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
static constexpr int kPresetBytes = (sizeof(PresetStore) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
static_assert(sizeof(PresetStore) <= FLASH_SECTOR_SIZE, "presets fit a sector");


class EightTrack : public ComputerCard
{
public:
	static constexpr int kHeads = 8;
	// The numbers are the protocol's (and the web editor's presets')
	enum Page {PageTime, PageVolume, PagePan, PageWidth, PageFeedback, PageFX, PageReverse, kPages};
	enum Fx {FxLowPass, FxHighPass, FxSaturation, FxHiss, FxWow, FxFlutter, FxDropout, FxMix};
	static_assert(sysex::kNumValues == kPages * kHeads, "protocol carries every page");

	EightTrack()
	{
		for (int i = 0; i <= 512; i++)
		{
			gTanh[i] = int16_t(32767.0f * tanhf(float(i) / 128.0f));
		}
		for (int i = 0; i <= 256; i++)
		{
			gSine[i] = int16_t(32767.0f * sinf(float(i) * (6.2831853f / 256.0f)));
		}
		for (int i = 0; i <= 256; i++)
		{
			exp2Tab[i] = uint32_t(1073741824.0f * exp2f(float(i) / 256.0f));
		}
		// Low-pass 150Hz to 16kHz, high-pass 20Hz to 2kHz
		for (int v = 0; v < 128; v++)
		{
			gLpCoef[v] = CoefFor(150.0f * exp2f(float(v) * (6.74f / 127.0f)));
			gHpCoef[v] = CoefFor(20.0f * exp2f(float(v) * (6.64f / 127.0f)));
		}

		SetDefaults();
		LoadPresetStore();
		if (store.lastSlot < sysex::kSlots && store.valid[store.lastSlot])
		{
			ApplySlot(store.lastSlot);
		}

		// Start every head where its settings put it, so nothing glides at
		// power-up
		freeLen = ExpScale(uint32_t(kFreeBase) << 8, 0);
		for (int h = 0; h < kHeads; h++)
		{
			headSmooth[h] = HeadTarget(h);
			headD[h] = uint32_t(headSmooth[h]) << 8;
		}

		EnableNormalisationProbe();

		// SysTick counting CPU cycles, for the load meter
		systick_hw->rvr = 0x00FFFFFF;
		systick_hw->cvr = 0;
		systick_hw->csr = 0x5;

		// Give the USB power circuitry time to settle, then pick the USB
		// mode once: host if the port is supplying power (an 8mu, or nothing
		// yet), device if a computer is (the web editor).  Boards older than
		// Rev 1.1 can't tell, and are always a device.
		sleep_us(150000);
		gCard = this;
		hostMode = USBPowerState() == DFP;
		multicore_launch_core1(hostMode ? Core1Host : Core1Device);
	}

	// Default settings, so the card does something with no 8mu: four heads
	// on the eighth notes of a bar, panned across the stereo field, the
	// last feeding back.  Values are in 8mu fader units (0-127), stored
	// shifted up to 0-4064.
	void SetDefaults()
	{
		static const uint8_t def[kPages][kHeads] = {
			{12, 28, 44, 60, 76, 92, 108, 124},   // time
			{110, 92, 78, 66, 0, 0, 0, 0},        // volume
			{36, 92, 24, 104, 64, 64, 64, 64},    // pan
			{127, 127, 127, 127, 127, 127, 127, 127}, // width
			{0, 0, 0, 60, 0, 0, 0, 0},            // feedback
			{100, 12, 24, 6, 14, 10, 0, 64},      // fx
			{0, 0, 0, 0, 0, 0, 0, 0},             // reverse
		};
		for (int p = 0; p < kPages; p++)
			for (int h = 0; h < kHeads; h++)
				params[p][h] = def[p][h] << 5;
	}

	virtual void ProcessSample()
	{
		uint32_t t0 = systick_hw->cvr;
		int32_t inL = int32_t(AudioIn1()) << 4;
		// Mono in: with only Audio In 1 patched, it feeds both sides
		int32_t inR = (Connected(Audio1) && Disconnected(Audio2)) ? inL : int32_t(AudioIn2()) << 4;
		bool odd = (sampleCount++ & 1) != 0;

		// Clock on Pulse In 1, followed only while a jack is in it
		if (samplesSinceClock < 0x7FFFFFFF) samplesSinceClock++;
		bool beat = false;
		if (PulseIn1RisingEdge() && Connected(Pulse1))
		{
			if (samplesSinceClock >= kMinBeat && samplesSinceClock <= kMaxBeat)
			{
				// Close to the last period: average out jitter.  Otherwise
				// take the new tempo straight away.
				int32_t d = samplesSinceClock - clockPeriod;
				if (d < 0) d = -d;
				if (d * 32 < clockPeriod) clockPeriod = (clockPeriod * 3 + samplesSinceClock) >> 2;
				else clockPeriod = samplesSinceClock;
			}
			samplesSinceClock = 0;
			beat = true;
			beatCount = 0;
		}
		// Beats with no clock: tap tempo, or the free-time length
		if (!clockJack)
		{
			if (++beatCount >= beatLength)
			{
				beatCount = 0;
				beat = true;
			}
		}
		if (beat) beatTrig = 480;

		// Control, every 32 samples, shared out across them so that no one
		// sample has too much to do: the transport on the first, one head on
		// every fourth from the third, the panel in the middle and the 8mu
		// at three quarters
		if (++controlCount >= 32) controlCount = 0;
		if (controlCount == 0) Control();
		else if ((controlCount & 3) == 2) ControlHead(controlCount >> 2);
		else if (controlCount == 16) ControlUI();
		else if (controlCount == 24)
		{
			if (hostMode) HandleEightMU();
			else tiltOct = TiltToOct(webPitch);
		}

		// The tape runs at 24kHz, half the sample rate, and its work is
		// shared over each pair of samples so that neither has too much: the
		// first reads heads 1-4, the second heads 5-8 and records a frame.
		// Every head's delay still moves on every sample.
		for (int h = 0; h < kHeads; h++) headD[h] += uint32_t(headInc[h]);
		uint32_t rev = revMask;
		if (rev)
		{
			// Reversed heads: half a frame a sample through their slice
			for (int h = 0; h < kHeads; h++)
			{
				if (!(rev & (1u << h))) continue;
				revPhase[h] += 0x8000;
				if (revPhase[h] >= revLen[h]) revPhase[h] = 0;
			}
		}

		// Input, low-passed at 48kHz and averaged down to one frame a pair
		aaL[0] += ((inL - aaL[0]) * kAACoef + 2048) >> 12;
		aaL[1] += ((aaL[0] - aaL[1]) * kAACoef + 2048) >> 12;
		aaR[0] += ((inR - aaR[0]) * kAACoef + 2048) >> 12;
		aaR[1] += ((aaR[0] - aaR[1]) * kAACoef + 2048) >> 12;

		// Heads.  Each reads the tape at its delay, between frames, then
		// gets its width, pan, volume and feedback.
		if (!odd)
		{
			prevL = aaL[1];
			prevR = aaR[1];
			accWL = accWR = accFL = accFR = 0;
		}
		uint32_t wq = uint32_t(writePos) << 16;
		uint32_t active = activeMask;
		for (int h = odd ? kHeads / 2 : 0, e = h + kHeads / 2; h < e; h++)
		{
			if (!(active & (1u << h))) continue;
			uint32_t d = headD[h];
			int32_t env = 4096;
			if (rev & (1u << h))
			{
				// Backwards: through each slice the delay grows from
				// nothing to twice the head's time, so the tape is read
				// backwards at its own speed, a slice a head's time long
				// played back after (on average) the head's time.  A short
				// fade at each end of the slice hides the join.
				uint32_t p = revPhase[h], q = revLen[h] - p;
				d = (p << 1) + (uint32_t(kMinDelay) << 16);
				int32_t x = int32_t((p < q ? p : q) >> 8);   // Q8 frames
				env = x >= revFade[h] ? 4096 : (x * revFadeK[h]) >> 16;
			}
			uint32_t rp = wq >= d ? wq - d : wq + kTapeQ - d;
			uint32_t i0 = rp >> 16;
			uint32_t i1 = i0 + 1;
			if (i1 >= uint32_t(kTapeFrames)) i1 = 0;
			int32_t fr = int32_t((rp >> 2) & 0x3FFF);
			int32_t l0, r0, l1, r1;
			ReadFrame(i0, l0, r0);
			ReadFrame(i1, l1, r1);
			int32_t l = l0 + (((l1 - l0) * fr) >> 14);
			int32_t r = r0 + (((r1 - r0) * fr) >> 14);
			if (env != 4096)
			{
				l = (l * env) >> 12;
				r = (r * env) >> 12;
			}
			int32_t mid = (l + r) >> 1;
			int32_t side = (((l - r) >> 1) * headWidth[h]) >> 12;
			l = mid + side;
			r = mid - side;
			accWL += (l * gainWL[h]) >> 12;
			accWR += (r * gainWR[h]) >> 12;
			accFL += (l * gainFL[h] + 2048) >> 12;
			accFR += (r * gainFR[h] + 2048) >> 12;
		}

		if (odd)
		{
			// Record head
			if (frozen)
			{
				// The tape loops: each frame is the one a loop's length before
				int src = writePos - freezeLength;
				if (src < 0) src += kTapeFrames;
				gTape[writePos * 3] = gTape[src * 3];
				gTape[writePos * 3 + 1] = gTape[src * 3 + 1];
				gTape[writePos * 3 + 2] = gTape[src * 3 + 2];
			}
			else
			{
				// Feedback through the filters
				int32_t fbL = Clip(accFL, 131072);
				int32_t fbR = Clip(accFR, 131072);
				if (lpOn)
				{
					fbL = OnePole(lpL[1], OnePole(lpL[0], fbL, lpCoef), lpCoef);
					fbR = OnePole(lpR[1], OnePole(lpR[0], fbR, lpCoef), lpCoef);
				}
				if (hpOn)
				{
					fbL -= OnePole(hpL[0], fbL, hpCoef);
					fbL -= OnePole(hpL[1], fbL, hpCoef);
					fbR -= OnePole(hpR[0], fbR, hpCoef);
					fbR -= OnePole(hpR[1], fbR, hpCoef);
				}

				// Hiss, a little brighter than white
				int32_t hissL = 0, hissR = 0;
				if (hissLevel)
				{
					int32_t nL = int32_t(NextRandom()) >> 16, nR = int32_t(NextRandom()) >> 16;
					hissL = ((nL - hissPrevL) * hissLevel) >> 17;
					hissR = ((nR - hissPrevR) * hissLevel) >> 17;
					hissPrevL = nL;
					hissPrevR = nR;
				}

				// Onto the tape: the input's frame (the average of the pair,
				// rounded to nearest, as rounding down would circulate as a
				// small DC offset), dropouts, then saturation
				int32_t frL = (aaL[1] + prevL + 1) >> 1, frR = (aaR[1] + prevR + 1) >> 1;
				int32_t recL = (((frL + fbL) * dropGain) >> 12) + hissL;
				int32_t recR = (((frR + fbR) * dropGain) >> 12) + hissR;
				recL = (Tanh(Clip((recL * drive) >> 8, 0x7FFFFF)) * invDrive + 32768) >> 16;
				recR = (Tanh(Clip((recR * drive) >> 8, 0x7FFFFF)) * invDrive + 32768) >> 16;
				WriteFrame(writePos, recL, recR);
			}
			if (++writePos >= kTapeFrames) writePos = 0;

			// The new wet frame; out at 48kHz, halfway from the last
			int32_t w0 = Clip(accWL, 262143), w1 = Clip(accWR, 262143);
			wetL = (wetPrevL + w0) >> 1;
			wetR = (wetPrevR + w1) >> 1;
			wetPrevL = w0;
			wetPrevR = w1;
		}
		else
		{
			wetL = wetPrevL;
			wetR = wetPrevR;
		}

		// Out: dry and wet
		int32_t outL = SoftClip((inL * dryGain + wetL * wetGain) >> 12);
		int32_t outR = SoftClip((inR * dryGain + wetR * wetGain) >> 12);
		AudioOut1(int16_t(Clip((outL + 8) >> 4, 2047)));
		AudioOut2(int16_t(Clip((outR + 8) >> 4, 2047)));

		// Levels for the LEDs, the envelope and the web editor
		int32_t a = inL < 0 ? -inL : inL, b = inR < 0 ? -inR : inR;
		if (a > peakInL) peakInL = a;
		if (b > peakInR) peakInR = b;
		a = outL < 0 ? -outL : outL;
		b = outR < 0 ? -outR : outR;
		if (a > peakOutL) peakOutL = a;
		if (b > peakOutR) peakOutR = b;
		a = wetL < 0 ? -wetL : wetL;
		b = wetR < 0 ? -wetR : wetR;
		if (a > peakWet) peakWet = a;
		if (b > peakWet) peakWet = b;

		if (beatTrig) beatTrig--;
		PulseOut1(beatTrig > 0);
		PulseOut2(frozen);

		// How long this sample took, in CPU cycles (SysTick counts down)
		uint32_t took = (t0 - systick_hw->cvr) & 0xFFFFFF;
		if (took > loadPeak) loadPeak = took;
	}

private:
	EightMU mu;

	static constexpr uint32_t kTapeQ = uint32_t(kTapeFrames) << 16;
	// Delay limits, in frames: enough behind the record head that both
	// frames read have been written, and short of wrapping round onto it
	static constexpr int32_t kMinDelay = 3;
	static constexpr int32_t kMaxDelay = kTapeFrames - 8;
	// Free time: a fader at the top is 0.5s at x1
	static constexpr int32_t kFreeBase = 12000;
	// Clock periods followed, in samples: 50ms to 4s a beat
	static constexpr int32_t kMinBeat = 2400, kMaxBeat = 4 * 48000;
	// 2 x one-pole at ~10kHz on the input, before the tape's 24kHz, Q12
	static constexpr int32_t kAACoef = 2990;

	// Settings, stored as raw fader values 0-4064.  Written by core1 in
	// device mode, as the web editor sends them.
	volatile int32_t params[kPages][kHeads];

	// 8mu paging and fader pickup
	volatile int page = PageTime;
	int pageBlink = 0;
	bool latched[kHeads] = {};
	int32_t lastFader[kHeads] = {};
	bool lastFaderValid = false;
	bool prevButton[EightMU::numButtons] = {};
	int pressTicks[EightMU::numButtons] = {};
	bool pressUsed[EightMU::numButtons] = {};
	int32_t pressFaders[EightMU::numButtons][kHeads] = {};
	static constexpr int kLongPress = 750;   // UI ticks: 0.5s
	// How far a fader must move while a button is held to count, about a
	// sixth of its travel, so a held button and a nudged fader don't load
	// or save anything
	static constexpr int32_t kHoldMove = 640;
	int ledFlash = 0;          // UI ticks left of an 8mu LED flash
	bool ledFlashSave = false; // save flash, else load
	int ledFlashValue = 0;
	bool wasConnected = false;
	int connectHoldoff = 0;
	bool muFreeze = false;

	// Tape
	int writePos = 0;
	uint32_t sampleCount = 0;
	int32_t prevL = 0, prevR = 0;
	int32_t aaL[2] = {}, aaR[2] = {};
	int32_t accWL = 0, accWR = 0, accFL = 0, accFR = 0; // heads, over a pair
	int32_t wetL = 0, wetR = 0, wetPrevL = 0, wetPrevR = 0;
	bool frozen = false;
	int freezeLength = 24000;

	// Heads: delays in frames, Q16, stepped every sample towards the
	// control rate's targets; smoothed delays Q8
	uint32_t headD[kHeads] = {};
	int32_t headInc[kHeads] = {};
	int32_t headSmooth[kHeads] = {};
	int32_t headWidth[kHeads] = {};          // Q12, 0 to 8192
	int32_t gainWL[kHeads] = {}, gainWR[kHeads] = {}; // Q12, wet
	int32_t gainFL[kHeads] = {}, gainFR[kHeads] = {}; // Q12, feedback
	volatile uint32_t activeMask = 0;
	volatile uint32_t audibleMask = 0;
	// Reversed heads: where each is through its slice and the slice's
	// length (Q16 frames), and its fade (Q8 frames, and 4096 / that, Q16)
	volatile uint32_t revMask = 0;
	uint32_t revPhase[kHeads] = {}, revLen[kHeads] = {};
	int32_t revFade[kHeads] = {}, revFadeK[kHeads] = {};
	// Glide: a one-pole of about 80ms, at most 1.5 frames a frame
	static constexpr int32_t kGlide = 544;            // Q16 a UI tick
	static constexpr int32_t kSlewMax = 24 * 256;     // Q8 frames a tick

	// Filters in the feedback path, Q16 coefficients
	int64_t lpL[2] = {}, lpR[2] = {}, hpL[2] = {}, hpR[2] = {};
	int32_t lpCoef = 65536, hpCoef = 0;
	bool lpOn = false, hpOn = false;
	int lpFor = -1, hpFor = -1;

	// Tape model
	int32_t drive = 256, invDrive = 65536;   // Q8, Q16
	int satFor = -1;
	int32_t hissLevel = 0, hissPrevL = 0, hissPrevR = 0;
	int32_t dropGain = 4096, dropTarget = 4096;
	int dropTicks = 0;
	uint32_t wowPhase = 0, flutPhase = 0, flut2Phase = 0;
	int32_t wowDrift = 0;
	int32_t modQ8 = 0;            // wow and flutter now, Q8 frames
	int32_t modOut = 0;           // the same, Q12 of full depth
	int32_t dryGain = 4096, wetGain = 4096;

	// Time
	bool clockJack = false;
	int32_t samplesSinceClock = 0x7FFFFFFF;
	int32_t clockPeriod = 24000;  // 120bpm until a clock says otherwise
	int32_t tapPeriod = 24000;
	bool tapSynced = false;
	bool synced = false;
	int32_t beatCount = 0, beatLength = 24000;
	int beatTrig = 0;
	uint32_t ticks = 0, lastTapTick = 0;
	bool tapHeld = false;
	int tapHeldTicks = 0;
	int tapShow = 0;
	struct Ratio {int8_t num, den;};
	static constexpr int kNumRatios = 9;
	static constexpr Ratio kRatios[kNumRatios] = {
		{1, 4}, {1, 3}, {1, 2}, {2, 3}, {1, 1}, {3, 2}, {2, 1}, {3, 1}, {4, 1}};
	int ratioIndex = 4;
	int32_t multOct = 0;          // free time multiplier, Q12 octaves
	int32_t tiltOct = 0;          // 8mu tilt, Q12 octaves
	uint32_t freeLen = 0;         // free time with a fader at the top, Q8 frames
	int32_t tiltMul = 4096;       // 2^tiltOct, Q12
	int32_t macroX = 4096, macroY = 4096; // Q12, 0 to 8192
	int controlCount = 0;
	uint32_t rng = 0x2545F491;
	uint32_t exp2Tab[257];        // 2^(i/256) in Q30

	// CPU load: the longest sample, in cycles, since the editor last read
	// it, and the time a sample has (200MHz / 48kHz)
	uint32_t loadPeak = 0;
	static constexpr uint32_t kSampleCycles = 200000000 / 48000;
	volatile uint8_t stLoad = 0;
	uint32_t loadHold = 0;
	int loadTicks = 0;

	// Levels, collected each control period
	int32_t peakInL = 0, peakInR = 0, peakOutL = 0, peakOutR = 0, peakWet = 0;
	int32_t envWet = 0;
	int32_t ledIn = 0;
	int32_t lvInL = 0, lvInR = 0, lvOutL = 0, lvOutR = 0;

	// Presets
	PresetStore store;
	volatile bool flashDirty = false;
	int presetFlash = 0;

	// USB mode, fixed at power-up
	bool hostMode = true;

	// Device mode: written on core1, read on core0
	volatile bool webFreeze = false;
	volatile bool webTap = false;
	volatile int32_t webPitch = 0, webRoll = 0;
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// Snapshot for the web editor: written on core0, read on core1
	volatile uint8_t stFlags = 0, stRatio = 0, stMacroX = 0, stMacroY = 0;
	volatile uint8_t stInL = 0, stInR = 0, stOutL = 0, stOutR = 0;
	volatile int32_t stBeatMs = 0, stMult = 0;
	volatile int32_t stTimes[kHeads] = {};

	//------------------------------------------------------------------------
	// DSP helpers
	//------------------------------------------------------------------------

	static inline void ReadFrame(uint32_t i, int32_t &l, int32_t &r)
	{
		const uint8_t *p = gTape + i * 3;
		uint32_t b0 = p[0], b1 = p[1], b2 = p[2];
		// 12-bit two's complement, sign extended and scaled to Q15
		l = int32_t((b0 | (b1 << 8)) << 20) >> 16;
		r = int32_t(((b1 >> 4) | (b2 << 4)) << 20) >> 16;
	}

	static inline void WriteFrame(int i, int32_t l, int32_t r)
	{
		uint32_t a = uint32_t(Clip((l + 8) >> 4, 2047)) & 0xFFF;
		uint32_t b = uint32_t(Clip((r + 8) >> 4, 2047)) & 0xFFF;
		uint8_t *p = gTape + i * 3;
		p[0] = uint8_t(a);
		p[1] = uint8_t((a >> 8) | (b << 4));
		p[2] = uint8_t(b >> 4);
	}

	static inline int32_t Clip(int32_t v, int32_t m)
	{
		return v > m ? m : (v < -m - 1 ? -m - 1 : v);
	}

	// One-pole low-pass, state Q16 of the signal
	static inline int32_t OnePole(int64_t &y, int32_t x, int32_t a)
	{
		y += (((int64_t(x) << 16) - y) * a) >> 16;
		return int32_t((y + 32768) >> 16);
	}

	// tanh, Q15 in (32768 = 1.0) and out
	static inline int32_t Tanh(int32_t v)
	{
		uint32_t a = uint32_t(v < 0 ? -v : v);
		uint32_t i = a >> 8;
		int32_t r;
		if (i >= 512) r = gTanh[512];
		else
		{
			int32_t f = int32_t(a & 255);
			r = gTanh[i] + (((gTanh[i + 1] - gTanh[i]) * f) >> 8);
		}
		return v < 0 ? -r : r;
	}

	// Clean to half of full scale, then rounding off into full scale
	static inline int32_t SoftClip(int32_t v)
	{
		int32_t a = v < 0 ? -v : v;
		if (a <= 16384) return v;
		int32_t r = 16384 + (Tanh(Clip((a - 16384) * 2, 0x7FFFFF)) >> 1);
		return v < 0 ? -r : r;
	}

	static inline int32_t Sine(uint32_t phase)
	{
		uint32_t i = phase >> 24;
		int32_t f = int32_t((phase >> 8) & 0xFFFF);
		return gSine[i] + (((gSine[i + 1] - gSine[i]) * f) >> 16);
	}

	uint32_t NextRandom()
	{
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return rng;
	}

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

	// Stored fader value (0-4064) to 0-4096, so a fader at the top is 100%
	static int32_t Q12(int32_t v)
	{
		return v >= 4064 ? 4096 : (v * 4096) / 4064;
	}

	// A two-sided fader value as -4096 to 4096, with 63-65 of 127 at the
	// centre reading as exactly 0 (PAN)
	static int32_t BipolarQ12(int32_t raw)
	{
		int32_t v = raw >> 5;
		if (v > 65) return ((v - 65) * 4096) / 62;
		if (v < 63) return -((63 - v) * 4096) / 63;
		return 0;
	}

	// One-pole coefficient (Q16) for a cutoff in Hz at the tape's 24kHz
	static int32_t CoefFor(float hz)
	{
		float a = 1.0f - expf(-6.2831853f * hz / 24000.0f);
		return int32_t(a * 65536.0f);
	}

	//------------------------------------------------------------------------
	// Pages
	//------------------------------------------------------------------------

	// The pages each button steps through, in order
	static constexpr int kMaxLayers = 2;
	static constexpr int8_t kButtonPages[4][kMaxLayers] = {
		{PageTime, PageReverse},
		{PageVolume, -1},
		{PagePan, PageWidth},
		{PageFeedback, PageFX},
	};
	// The button (0-3) a page is on, and its place in that button's list
	static int ButtonOf(int pg, int *layer = nullptr)
	{
		for (int b = 0; b < 4; b++)
			for (int l = 0; l < kMaxLayers; l++)
				if (kButtonPages[b][l] == pg)
				{
					if (layer) *layer = l;
					return b;
				}
		if (layer) *layer = 0;
		return 0;
	}
	// A short press of button b: its next page if one of its pages is
	// showing, otherwise its first
	static int NextPage(int pg, int b)
	{
		int layer;
		if (ButtonOf(pg, &layer) != b) return kButtonPages[b][0];
		int next = layer + 1;
		if (next >= kMaxLayers || kButtonPages[b][next] < 0) next = 0;
		return kButtonPages[b][next];
	}

	//------------------------------------------------------------------------
	// Control, every 32 samples (1.5kHz)
	//------------------------------------------------------------------------

	// Where head h's settings put it, in frames Q8, before gliding and wow
	int32_t HeadTarget(int h) const
	{
		int32_t f = params[PageTime][h];
		int32_t t;
		if (synced)
		{
			// Sixteenths of a beat x4: n/4 beats, n = 1-16.  A beat is
			// beatPeriod samples, half as many frames.
			int n = 1 + ((f >> 5) * 16 >> 7);
			int32_t beatPeriod = clockJack ? clockPeriod : tapPeriod;
			int32_t q8 = n * beatPeriod * 32;    // n * beat / 8 frames, Q8
			q8 = (q8 * kRatios[ratioIndex].num) / kRatios[ratioIndex].den;
			if (tiltMul != 4096) q8 = int32_t((int64_t(q8) * tiltMul) >> 12);
			while (q8 > (kMaxDelay << 8)) q8 >>= 1;
			t = q8;
		}
		else
		{
			t = int32_t((int64_t(freeLen) * Q12(f)) >> 12);
		}
		if (t < (kMinDelay << 8)) t = kMinDelay << 8;
		if (t > (kMaxDelay << 8)) t = kMaxDelay << 8;
		return t;
	}

	// The tape transport and the tape model
	void Control()
	{
		ticks++;
		Switch sw = SwitchVal();

		// Main: time multiplier (free), or clock ratio (synced)
		int32_t k = MapKnob(KnobVal(Main), true);
		multOct = (k - 2048) * 4;     // +-2 octaves: x1/4 to x4
		ratioIndex = (MapKnob(KnobVal(Main), false) * kNumRatios) >> 12;
		if (ratioIndex >= kNumRatios) ratioIndex = kNumRatios - 1;

		// X and Y with CV: multipliers, x1 at the centre, +5V adds x1
		macroX = MapKnob(KnobVal(X), true) * 2 + (CVIn1() * 12) / 5;
		macroY = MapKnob(KnobVal(Y), true) * 2 + (CVIn2() * 12) / 5;
		macroX = macroX < 0 ? 0 : (macroX > 8192 ? 8192 : macroX);
		macroY = macroY < 0 ? 0 : (macroY > 8192 ? 8192 : macroY);

		// Tap tempo, on the switch held down
		bool down = sw == Down;
		bool tap = webTap;
		if (tap) webTap = false;
		if (down && !tapHeld) {tap = true; tapHeldTicks = 0;}
		if (down)
		{
			if (++tapHeldTicks == 2250 && !clockJack) // 1.5s: back to free
			{
				tapSynced = false;
				tapShow = 1500;
			}
		}
		tapHeld = down;
		if (tap && !clockJack)
		{
			uint32_t gap = ticks - lastTapTick;
			if (lastTapTick && gap >= 50 && gap <= 4500)     // 33ms to 3s
			{
				int32_t p = int32_t(gap) * 32;
				// Steady taps average; a new tempo is taken at once
				int32_t d = p - tapPeriod;
				if (d < 0) d = -d;
				tapPeriod = (tapSynced && d * 5 < tapPeriod) ? (tapPeriod + p) >> 1 : p;
				if (tapPeriod < kMinBeat) tapPeriod = kMinBeat;
				tapSynced = true;
				beatCount = 0;
				beatTrig = 480;
			}
			lastTapTick = ticks;
		}

		// Synced with a jack in Pulse In 1 (whether or not a clock has come
		// yet), or with tap tempo
		clockJack = Connected(Pulse1);
		synced = clockJack || tapSynced;
		freeLen = ExpScale(uint32_t(kFreeBase) << 8, multOct + tiltOct);
		tiltMul = int32_t(ExpScale(4096, tiltOct));
		if (synced) beatLength = clockJack ? clockPeriod : tapPeriod;
		else beatLength = int32_t(freeLen >> 7);   // Q8 frames to samples
		if (beatLength < kMinBeat) beatLength = kMinBeat;

		// Freeze: switch up, Pulse In 2 high, the web editor or the 8mu
		bool fz = sw == Up || (Connected(Pulse2) && PulseIn2()) || webFreeze || muFreeze;
		if (fz && !frozen)
		{
			// Loop the longest head that's playing (the longest of all if
			// none is)
			int32_t longest = 0, any = 0;
			for (int h = 0; h < kHeads; h++)
			{
				int32_t d = int32_t(headD[h] >> 16);
				if (d > any) any = d;
				if ((audibleMask & (1 << h)) && d > longest) longest = d;
			}
			if (!longest) longest = any;
			if (longest < 240) longest = 240;
			if (longest > kMaxDelay) longest = kMaxDelay;
			freezeLength = longest;
		}
		frozen = fz;

		// Tape model
		const volatile int32_t *fx = params[PageFX];
		int v = fx[FxLowPass] >> 5;
		if (v != lpFor)
		{
			lpFor = v;
			lpOn = v < 127;
			lpCoef = gLpCoef[v];
		}
		v = fx[FxHighPass] >> 5;
		if (v != hpFor)
		{
			hpFor = v;
			hpOn = v > 0;
			hpCoef = gHpCoef[v];
		}
		v = fx[FxSaturation] >> 5;
		if (v != satFor)
		{
			// Drive x1 to x4, with the level brought back down after, so
			// quiet signals pass at the same level and loud ones squash
			satFor = v;
			drive = 256 + v * 6;
			invDrive = (65536 * 256) / drive;
		}
		// Hiss up to about -26dB
		int32_t hs = fx[FxHiss] >> 5;
		hissLevel = (hs * hs) >> 2;

		// Dropouts: up to about three a second at the top, each a dip of
		// 20-150ms
		int32_t dp = fx[FxDropout] >> 5;
		if (dropTicks > 0)
		{
			if (--dropTicks == 0) dropTarget = 4096;
		}
		else if (dp > 0 && (NextRandom() & 0xFFFF) < uint32_t(dp * dp) >> 7)
		{
			dropTicks = 30 + int(NextRandom() % 195u);
			int32_t depth = 1200 + int32_t(NextRandom() % 2400u);
			dropTarget = 4096 - (depth * dp) / 127;
		}
		dropGain += (dropTarget - dropGain) >> (dropTarget < dropGain ? 3 : 5);

		// Wow (0.55Hz, drifting) and flutter (6.8Hz and 11.3Hz): up to
		// 10ms and 0.4ms of delay
		int32_t wv = fx[FxWow] >> 5, fv = fx[FxFlutter] >> 5;
		wowDrift += ((int32_t(NextRandom()) >> 16) - wowDrift) >> 8;
		wowPhase += uint32_t(1574836 + wowDrift * 300);  // 0.55Hz, +-20% or so
		flutPhase += 19470000u;
		flut2Phase += 32350000u;
		int32_t wowAmt = (wv * wv * 240) >> 6;           // Q8 frames, up to 60k
		int32_t flutAmt = (fv * fv * 10) >> 6;           // Q8 frames, up to 2.5k
		int32_t wowS = Sine(wowPhase), flutS = (Sine(flutPhase) * 3 + Sine(flut2Phase)) >> 2;
		modQ8 = ((wowS * wowAmt) >> 15) + ((flutS * flutAmt) >> 15);
		// For CV Out 1: wow, with a quarter as much flutter, Q12 at full depth
		modOut = ((wowS * wv * wv) / (127 * 127) >> 3) + ((flutS * fv * fv) / (127 * 127) >> 5);
		if (modOut > 4096) modOut = 4096;
		if (modOut < -4096) modOut = -4096;

		// Mix: dry full below the centre, wet full above
		int32_t m = Q12(fx[FxMix]);
		dryGain = m <= 2048 ? 4096 : (4096 - m) * 2;
		wetGain = m >= 2048 ? 4096 : m * 2;
	}

	// One head, each in its own sample of the control period
	void ControlHead(int h)
	{
		// Glide towards the target, a one-pole with a slew limit
		int32_t target = HeadTarget(h);
		int32_t delta = target - headSmooth[h];
		int32_t step = int32_t((int64_t(delta) * kGlide) >> 16);
		if (step == 0) step = delta;
		if (step > kSlewMax) step = kSlewMax;
		if (step < -kSlewMax) step = -kSlewMax;
		headSmooth[h] += step;

		// Wow and flutter, less on short heads, so they don't scrape
		// against the record head
		int32_t s = headSmooth[h];
		int32_t mod = s >= 4800 * 256 ? modQ8 : (modQ8 * (s >> 8)) / 4800;
		int64_t d = (int64_t(s) + mod) << 8;
		if (d < int64_t(kMinDelay) << 16) d = int64_t(kMinDelay) << 16;
		if (d > int64_t(kMaxDelay) << 16) d = int64_t(kMaxDelay) << 16;
		headInc[h] = int32_t((d - int64_t(headD[h])) >> 5);

		// Volume (a square law), feedback, pan and width, with the
		// X and Y multipliers
		int32_t vq = Q12(params[PageVolume][h]);
		int32_t vol = (((vq * vq) >> 12) * macroX) >> 12;
		int32_t fb = (Q12(params[PageFeedback][h]) * macroX) >> 12;
		if (vol > 8192) vol = 8192;
		if (fb > 8192) fb = 8192;
		if (frozen) fb = 0;
		int32_t pan = (BipolarQ12(params[PagePan][h]) * macroY) >> 12;
		if (pan > 4096) pan = 4096;
		if (pan < -4096) pan = -4096;
		int32_t gl = pan > 0 ? 4096 - pan : 4096;
		int32_t gr = pan < 0 ? 4096 + pan : 4096;
		int32_t w = (Q12(params[PageWidth][h]) * macroY) >> 12;
		headWidth[h] = w > 8192 ? 8192 : w;
		gainWL[h] = (vol * gl) >> 12;
		gainWR[h] = (vol * gr) >> 12;
		gainFL[h] = (fb * gl) >> 12;
		gainFR[h] = (fb * gr) >> 12;
		uint32_t bit = 1u << h;
		activeMask = (vol || fb) ? (activeMask | bit) : (activeMask & ~bit);
		audibleMask = vol ? (audibleMask | bit) : (audibleMask & ~bit);
		stTimes[h] = s / 6144;          // Q8 frames at 24kHz to ms

		// Reverse: a slice the head's time long (at most half the tape),
		// with a fade of 4ms, or a quarter of a short slice
		if (params[PageReverse][h] >= (64 << 5))
		{
			int32_t t = s >> 8;
			if (t > (kMaxDelay - kMinDelay) / 2) t = (kMaxDelay - kMinDelay) / 2;
			if (t < 4) t = 4;
			int32_t fade = t / 4 < 96 ? t / 4 : 96;
			revFade[h] = fade << 8;
			revFadeK[h] = 1048576 / fade;
			revLen[h] = uint32_t(t) << 16;
			if (!(revMask & bit)) revPhase[h] = 0;
			revMask |= bit;
		}
		else revMask &= ~bit;
	}

	// Panel LEDs, levels and the web editor's snapshot
	void ControlUI()
	{
		// Levels: peaks over the last control period, with a fall
		int32_t pw = peakWet;
		envWet = pw > envWet ? pw : envWet - (envWet >> 8) - 1;
		if (envWet < 0) envWet = 0;
		lvInL = Fall(lvInL, peakInL);
		lvInR = Fall(lvInR, peakInR);
		lvOutL = Fall(lvOutL, peakOutL);
		lvOutR = Fall(lvOutR, peakOutR);
		peakInL = peakInR = peakOutL = peakOutR = peakWet = 0;

		int32_t e = envWet > 32767 ? 32767 : envWet;
		CVOut2Millivolts((e * 5000) >> 15);
		CVOut1Millivolts((modOut * 5000) >> 12);

		// Snapshot for the web editor
		stFlags = uint8_t((synced ? 1 : 0) | (clockJack ? 2 : 0) | (tapSynced && !clockJack ? 4 : 0)
			| (frozen ? 8 : 0) | (mu.Connected() ? 16 : 0) | (tapHeld ? 32 : 0));
		stRatio = uint8_t(ratioIndex);
		stBeatMs = (synced ? (clockJack ? clockPeriod : tapPeriod) : beatLength) / 48;
		stMult = ((multOct + tiltOct) >> 4) + 8192;
		stMacroX = uint8_t(macroX >= 8128 ? 127 : macroX >> 6);
		stMacroY = uint8_t(macroY >= 8128 ? 127 : macroY >> 6);
		stInL = Level7(lvInL);
		stInR = Level7(lvInR);
		stOutL = Level7(lvOutL);
		stOutR = Level7(lvOutR);
		// Peak load over about a third of a second, as a percentage of a
		// sample's time
		if (loadPeak > loadHold) loadHold = loadPeak;
		loadPeak = 0;
		if (++loadTicks >= 500)
		{
			uint32_t pc = (loadHold * 100) / kSampleCycles;
			stLoad = uint8_t(pc > 127 ? 127 : pc);
			loadHold = 0;
			loadTicks = 0;
		}

		// Computer LEDs: the page (or the beat and levels, with no 8mu or
		// editor), the input, and the connection
		bool conn = mu.Connected() || WebLinked();
		pageBlink = (pageBlink + 1) % 900;
		if (tapShow > 0) tapShow--;
		if (presetFlash > 0) presetFlash--;
		for (int i = 0; i < 4; i++)
		{
			if (tapShow > 0)
			{
				// Back to free time: all four blink
				LedOn(i, (tapShow / 150) & 1);
			}
			else if (conn)
			{
				// First page lit; second page blinks
				int layer;
				int b = ButtonOf(page, &layer);
				bool lit = layer == 0 || pageBlink < 450;
				LedOn(i, b == i && lit);
			}
			else if (i == 0) LedOn(0, beatTrig > 0);
			else if (i == 1) LedOn(1, synced);
			else LedBrightness(i, uint16_t(Level12(i == 2 ? lvOutL : lvOutR)));
		}
		LedBrightness(4, frozen ? 4095 : uint16_t(Level12(lvInL > lvInR ? lvInL : lvInR)));
		LedBrightness(5, presetFlash > 0 ? ((presetFlash / 100) & 1 ? 4095 : 0) : (conn ? 4095 : 0));
	}

	static int32_t Fall(int32_t lv, int32_t peak)
	{
		if (peak > lv) return peak;
		return lv - (lv >> 6) - 1 < 0 ? 0 : lv - (lv >> 6) - 1;
	}
	static uint8_t Level7(int32_t v) {return uint8_t(v >= 32767 ? 127 : v >> 8);}
	static int32_t Level12(int32_t v) {return v >= 32767 ? 4095 : v >> 3;}

	// A knob reading (0-4095) with dead zones, as the knobs don't always
	// reach the very ends of their range: the outer kKnobEndZone at each end
	// reads as fully 0 or 4095, and the travel between is stretched to fill
	// the range.  Two-sided controls also get kKnobCentreZone either side of
	// the middle, which reads as exactly 2048 (x1).
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

	// 8mu front/back tilt (-2032 to 2032 for -90 to +90 degrees) to a time
	// bend (Q12 octaves), ignoring about 15 degrees either side of flat so
	// that holding the 8mu doesn't bend anything; beyond that the tilt is
	// stretched so tipping it right up reaches an octave
	static constexpr int32_t kTiltDeadZone = 512;
	static int32_t TiltToOct(int32_t t)
	{
		if (t > -kTiltDeadZone && t < kTiltDeadZone) return 0;
		int32_t d = t > 0 ? t - kTiltDeadZone : t + kTiltDeadZone;
		int32_t m = (d * 4096) / (2032 - kTiltDeadZone);
		return m > 4096 ? 4096 : (m < -4096 ? -4096 : m);
	}

	void HandleEightMU()
	{
		bool conn = mu.Connected();
		if (!conn)
		{
			wasConnected = false;
			tiltOct = 0;
			return;
		}
		if (!wasConnected)
		{
			// Wait for the 8mu's reply to the fader position query before
			// trusting fader values
			wasConnected = true;
			connectHoldoff = 1500; // ~1s at control rate
			lastFaderValid = false;
			for (int i = 0; i < kHeads; i++) latched[i] = false;
		}
		if (connectHoldoff > 0)
		{
			connectHoldoff--;
			return;
		}

		// Buttons, acted on when released: a short press is the page, a
		// long press of D freeze, and moving a fader while A or D is held
		// loads or saves a preset.  While a button is held the faders edit
		// nothing.
		bool anyHeld = false;
		for (int b = 0; b < EightMU::numButtons; b++)
		{
			bool down = mu.Button(b);
			if (down && !prevButton[b])
			{
				pressTicks[b] = 0;
				pressUsed[b] = false;
				for (int i = 0; i < kHeads; i++) pressFaders[b][i] = mu.Fader(i);
			}
			if (down)
			{
				anyHeld = true;
				if (pressTicks[b] < 0x7FFFFFFF) pressTicks[b]++;
				for (int i = 0; i < kHeads; i++)
				{
					int32_t d = mu.Fader(i) - pressFaders[b][i];
					if (d > kHoldMove || d < -kHoldMove)
					{
						pressFaders[b][i] = mu.Fader(i);
						pressUsed[b] = true;
						if (b == 0) LoadSlot(i);
						else if (b == 3) SaveSlot(i);
						if (b == 0 || b == 3)
						{
							ledFlash = 1350;
							ledFlashSave = b == 3;
							ledFlashValue = i;
						}
					}
				}
			}
			else if (prevButton[b])
			{
				if (!pressUsed[b])
				{
					if (b == 3 && pressTicks[b] >= kLongPress) muFreeze = !muFreeze;
					else page = NextPage(page, b);
				}
				for (int i = 0; i < kHeads; i++) latched[i] = false;
			}
			prevButton[b] = down;
		}
		if (ledFlash > 0) ledFlash--;

		// Faders, with pickup
		for (int i = 0; i < kHeads; i++)
		{
			int32_t f = mu.Fader(i);
			volatile int32_t &p = params[page][i];
			if (anyHeld) latched[i] = false;
			else if (!latched[i])
			{
				int32_t d = f - p;
				bool near = d > -96 && d < 96;
				bool crossed = lastFaderValid && ((lastFader[i] - p < 0) != (d < 0));
				if (near || crossed) latched[i] = true;
			}
			if (latched[i]) p = f;
			lastFader[i] = f;

			// 8mu LEDs: a load flashes the preset's fader three times, a
			// save blinks it fast; otherwise the stored values, and while
			// frozen the heads that are playing pulse
			int32_t led;
			if (ledFlash > 0 && !ledFlashSave)
				led = (i == ledFlashValue && ((1350 - ledFlash) / 225) % 2 == 0) ? 4095 : 0;
			else if (ledFlash > 0)
				led = (i == ledFlashValue && ((1350 - ledFlash) / 75) % 2 == 0) ? 4095 : 0;
			else if (page == PageReverse) led = p >= (64 << 5) ? 4095 : 0;
			else led = (p * 9) >> 4;
			mu.SetLed(i, led);
		}
		lastFaderValid = true;

		tiltOct = TiltToOct(mu.Pitch());
	}

	//------------------------------------------------------------------------
	// Presets
	//------------------------------------------------------------------------

	void LoadPresetStore()
	{
		memcpy(&store, reinterpret_cast<const void *>(XIP_BASE + kPresetOffset), sizeof(store));
		if (store.magic == kPresetMagic && store.version == 1)
		{
			// Carry version 1's presets over, with every head forwards
			static PresetStoreV1 old;
			memcpy(&old, reinterpret_cast<const void *>(XIP_BASE + kPresetOffset), sizeof(old));
			memset(&store, 0, sizeof(store));
			store.magic = kPresetMagic;
			store.version = kPresetVersion;
			store.lastSlot = old.lastSlot;
			for (int s = 0; s < sysex::kSlots; s++)
			{
				store.valid[s] = old.valid[s];
				memcpy(store.values[s], old.values[s], sizeof(old.values[s]));
			}
		}
		else if (store.magic != kPresetMagic || store.version != kPresetVersion)
		{
			memset(&store, 0, sizeof(store));
			store.magic = kPresetMagic;
			store.version = kPresetVersion;
			store.lastSlot = sysex::kNoSlot;
		}
		for (int s = 0; s < sysex::kSlots; s++) if (store.valid[s] != 1) store.valid[s] = 0;
	}

	void ApplySlot(int s)
	{
		for (int i = 0; i < sysex::kNumValues; i++)
		{
			params[i / kHeads][i % kHeads] = int32_t(store.values[s][i] & 0x7F) << 5;
		}
	}

	// From the 8mu or the web editor; the flash is written on core1
	void LoadSlot(int s)
	{
		if (s < 0 || s >= sysex::kSlots || !store.valid[s]) return;
		ApplySlot(s);
		if (store.lastSlot != s)
		{
			store.lastSlot = uint8_t(s);
			flashDirty = true;
		}
		presetFlash = 600;
	}

	void SaveSlot(int s)
	{
		if (s < 0 || s >= sysex::kSlots) return;
		for (int i = 0; i < sysex::kNumValues; i++)
		{
			store.values[s][i] = uint8_t((params[i / kHeads][i % kHeads] >> 5) & 0x7F);
		}
		store.valid[s] = 1;
		store.lastSlot = uint8_t(s);
		flashDirty = true;
		presetFlash = 600;
	}

	// On core1, which runs from RAM like everything else (copy_to_ram), so
	// the audio on core0 carries on while the flash is busy
	void ServiceFlash()
	{
		if (!flashDirty) return;
		flashDirty = false;
		static uint8_t buf[kPresetBytes] __attribute__((aligned(4)));
		memset(buf, 0xFF, sizeof(buf));
		memcpy(buf, &store, sizeof(store));
		uint32_t ints = save_and_disable_interrupts();
		flash_range_erase(kPresetOffset, FLASH_SECTOR_SIZE);
		flash_range_program(kPresetOffset, buf, kPresetBytes);
		restore_interrupts(ints);
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
			gCard->ServiceFlash();
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
			gCard->ServiceFlash();
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
			if (len >= 3 && p[0] < kPages && p[1] < kHeads)
			{
				params[p[0]][p[1]] = int32_t(p[2] & 0x7F) << 5;
			}
			break;
		case sysex::SetAll:
			if (len >= 1 + sysex::kNumValues && p[0] == sysex::kVersion)
			{
				for (int i = 0; i < sysex::kNumValues; i++)
				{
					params[i / kHeads][i % kHeads] = int32_t(p[1 + i] & 0x7F) << 5;
				}
			}
			break;
		case sysex::Page:
			if (len >= 1 && p[0] < kPages) page = p[0];
			break;
		case sysex::Reset:
			SetDefaults();
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
		case sysex::Freeze:
			if (len >= 1) webFreeze = p[0] != 0;
			break;
		case sysex::Tap:
			webTap = true;
			break;
		case sysex::Save:
			if (len >= 1 && p[0] < sysex::kSlots)
			{
				SaveSlot(p[0]);
				SendSlots();
			}
			break;
		case sysex::Load:
			if (len >= 1 && p[0] < sysex::kSlots)
			{
				LoadSlot(p[0]);
				SendState();
			}
			break;
		case sysex::GetSlots:
			SendSlots();
			break;
		case sysex::Store:
			if (len >= 1 + sysex::kNumValues && p[0] < sysex::kSlots)
			{
				for (int i = 0; i < sysex::kNumValues; i++) store.values[p[0]][i] = p[1 + i] & 0x7F;
				store.valid[p[0]] = 1;
				flashDirty = true;
				SendSlots();
			}
			break;
		case sysex::Erase:
			if (len >= 1 && p[0] < sysex::kSlots)
			{
				store.valid[p[0]] = 0;
				if (store.lastSlot == p[0]) store.lastSlot = sysex::kNoSlot;
				flashDirty = true;
				SendSlots();
			}
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
		out[n++] = store.lastSlot < sysex::kSlots ? store.lastSlot : sysex::kNoSlot;
		n += sysex::Put14(out + n, (kMaxDelay * 1000) / 24000);
		for (int i = 0; i < sysex::kNumValues; i++)
		{
			out[n++] = uint8_t((params[i / kHeads][i % kHeads] >> 5) & 0x7F);
		}
		out[n++] = 0xF7;
		return n;
	}

	int EncodeStatus(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::Status);
		out[n++] = stFlags;
		out[n++] = stRatio;
		n += sysex::Put14(out + n, stBeatMs);
		n += sysex::Put14(out + n, stMult);
		out[n++] = stMacroX;
		out[n++] = stMacroY;
		for (int h = 0; h < kHeads; h++) n += sysex::Put14(out + n, stTimes[h]);
		out[n++] = stInL;
		out[n++] = stInR;
		out[n++] = stOutL;
		out[n++] = stOutR;
		out[n++] = uint8_t(page);
		out[n++] = store.lastSlot < sysex::kSlots ? store.lastSlot : sysex::kNoSlot;
		out[n++] = stLoad;
		out[n++] = 0xF7;
		return n;
	}

	int EncodeSlots(uint8_t *out) const
	{
		int n = sysex::Header(out, sysex::Slots);
		for (int s = 0; s < sysex::kSlots; s++) out[n++] = store.valid[s] ? 1 : 0;
		for (int s = 0; s < sysex::kSlots; s++)
			for (int i = 0; i < sysex::kNumValues; i++)
				out[n++] = store.values[s][i] & 0x7F;
		out[n++] = 0xF7;
		return n;
	}

private:
	void SendState()
	{
		uint8_t msg[sysex::kStateLen];
		Write(msg, EncodeState(msg));
	}

	void SendSlots()
	{
		static uint8_t msg[sysex::kSlotsLen];
		Write(msg, EncodeSlots(msg));
	}
};


int main()
{
	set_sys_clock_khz(200000, true);

	static EightTrack card;
	card.Run();
}
