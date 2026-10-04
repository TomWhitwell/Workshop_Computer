/*
Bootleg Radio (538) - play WAV files from a USB stick like pirate radio stations

A lo-fi bootleg tribute to Music Thing Modular's Radio Music (not a
replacement for it), built on the ComputerCard's new USB mass-storage host
support (ComputerCard example usb_msc_host).  The 538 is the AM frequency the
pirate station Radio Veronica used, off the Dutch coast near the Botlek.
Plug a USB stick full of WAV files into the Computer's front USB-C jack and
tune through them: a control voltage picks the station, another sets where in
the file playback begins, and the switch retriggers or selects the bank.

Building: this needs TinyUSB 0.21 or later.  The TinyUSB bundled with older
Pico SDKs (0.18/0.20) reads USB sticks at only ~64kB/s, far too slow for audio.
Pass
  -DPICO_TINYUSB_PATH=/path/to/tinyusb
to cmake if the SDK in use is older.  The repo dev container is pinned to a
new enough TinyUSB for this card.

Hardware: ComputerCard Rev1_1 (the first revision with USB host support), with
nothing plugged into the Computer's own USB socket except the stick (via a
suitable adaptor).

What it does
  - Scans the USB stick for .wav files, in "banks": the root directory is bank 0,
    and each subdirectory that holds WAV files is another bank (up to 32).  Each
    file is a "station".  Banks and stations are natural-sorted (case-insensitive,
    digits compared as numbers), so the order matches what you see in Finder.
    Files must be uncompressed 16-bit PCM, mono or stereo, at any sample rate up
    to 192kHz (resampled to 48kHz by linear interpolation, which is simple rather
    than pristine).
  - Loops the current station over a window [loop start, loop start + length],
    forever.  The card never advances by itself; you move it by hand or voltage.

Controls
  Main knob        Station selection.  Turned to pick a file in the current bank.
                   While the switch is Up, it selects the bank instead.
  CV in 1          Station selection too, added to the Main knob.  With CV1
                   patched, both the knob and the voltage sweep the stick, so
                   you can tune by hand or by voltage.  (CV inputs are bipolar
                   +/-6V; a unipolar source sweeps only half the range.)
  X knob           Tape speed: playback rate from -2 to +2 octaves, exponential
                   around a centre of normal speed.  Slower also plays lower,
                   like slowing a tape.
  CV in 2          Tape speed too, added to the X knob.
  Y knob           Loop start point, as a fraction of the file.
  Audio in 1       Loop start point too, added to the Y knob.  The audio inputs
                   are DC-coupled, so they read a steady voltage like a CV.
  Audio in 2       Loop length, as a fraction of the file (-6V = very short,
                   +6V = whole file).  Unpatched, the loop runs to the end.
  Switch down      Tap: retrigger from the loop start.  Hold ~1s: toggle the
                   "tune" mode (Loop <-> Sync).  (Momentary.)
  Switch up        While held: select the bank with the Main knob / CV in 1.
                   A new bank starts at its first station.
  Pulse in 1       External clock for the two CV outs below.  Unpatched, they
                   run from an internal 120 BPM clock; patch a clock here to
                   drive them from it (they glide to the new tempo).
  Pulse in 2       Retrigger, like a short switch-down tap.

Tuning (Main/CV1, or a bank change) lands differently by mode:
  Loop mode        the new station starts at its loop start point.
  Sync mode        the new station starts at the same proportional position as
                   the old file (the "synchronised jump"), clamped into its
                   window.  This is the default.
The mode is toggled by holding switch down ~1s; the panel blinks 2 times for
Loop or 3 times for Sync.  A deliberate retrigger, or reaching the loop end,
restarts from the live loop start point.

Outputs
  Audio out 1/2    Left/right playback (mono files go to both).
  Pulse out 1      Loop gate (default): high while the loop plays, low during the
                   wrap.  Settings can set this to tick, clock, or off.
  Pulse out 2      Wrap tick (default): a short pulse each time the loop wraps.
                   Settings can set this to gate, clock, or off.
  CV out 1         Random stepped CV, a new value on every clock pulse (bipolar,
                   so 2Hz at the internal 120 BPM).  Patch it into an input to
                   step a parameter.
  CV out 2         Bipolar triangle, one cycle per 40 clock pulses (20 seconds
                   at 120 BPM, slower/faster with an external clock).  Patch it
                   into CV in 1 to sweep the stations.
  LEDs 0/1         Left/right output level.
  LED 2            Off when idle; steady while playing in Sync mode; blinking
                   while playing in Loop mode (so it shows both play state and
                   the tune mode).
  LED 3            Flashes when the station changes.
  LED 4            USB stick mounted.
  LED 5            Buffer underrun (the stick can't keep up).

  While a stick is present but its filesystem is not yet readable (it has
  enumerated but the root directory cannot be listed), all six LEDs blink slowly
  together; they stop once the volume is readable. This is also when the optional
  /settings.txt is read.

Settings
  An optional /settings.txt in the stick root sets the power-on tune mode, start
  bank, internal clock tempo, triangle rate, the two pulse-output modes, and the
  LED mode.  It is read once when the stick mounts; with no file the card behaves
  exactly as described above.  See the Settings section in README.md.

How it works

  Core 0 runs main().  It handles USB (TinyUSB), the filesystem (FatFs), and
         parsing WAV files -- all things that take too long to process in a
         single audio sample.
  Core 1 runs ComputerCard, so ProcessSample() is called 48000 times per second.
         It just consumes audio samples through a buffer in RAM, and watches the
         knob, CVs and switch.

The two cores communicate through a single ring buffer, ringBuf.  Core 0 fills
it with data read from the file and core 1 takes samples out.  There are always
two byte positions:

  ringWritePos   total bytes ever put into the buffer (only ever increases)
  ringReadPos    total bytes ever taken out of the buffer (only ever increases)

The bytes between them are in the buffer, and byte number x lives at
ringBuf[x % ringSize].  Each position is only ever written by one core, which is
what makes this safe without locks.  The one thing that must be done carefully
is the order of memory operations: a core must finish writing or reading data
before it publishes its updated position, hence the __dmb() memory barriers.

Finally, some state (the ring positions, the playback format) needs to be reset
when a file starts or stops.  If core 0 did this directly, it could change things
under core 1's feet in the middle of a sample.  So core 0 never does: it leaves
a request for core 1 (see SetMode()), and core 1 applies it at the start of its
next ProcessSample() call, while core 0 waits.

The reverse-direction requests -- core 1 asking core 0 to change station or
retrigger -- travel the other way through the small volatile "request" block.
Core 1 only ever sets it; core 0 only ever reads and clears it.  Core 1 computes
no divisions in the audio path: core 0 works out the proportional position when
it acts on the request.
*/

#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h" // TinyUSB
#include "ff.h"   // FatFs

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <strings.h>
#include <algorithm>

#if TUSB_VERSION_NUMBER < 2100
#error "TinyUSB older than 0.21 reads USB sticks too slowly; configure with -DPICO_TINYUSB_PATH=<tinyusb 0.21 or later>"
#endif

////////////////////////////////////////////////////////////////////////////////
// State shared between the cores
//
// Everything here is volatile, because it is accessed from both cores.

// The ring buffer. Its size must be a power of two, so that x % ringSize can be
// calculated as x & ringMask. ioChunk is how much we read from the stick at a
// time. USB sticks are much more efficient with big transfers, and with ones
// that are a whole number of 512-byte sectors. It must divide ringSize so that a
// chunk never wraps around the end of the buffer.
static constexpr uint32_t ringSize = 131072;
static constexpr uint32_t ringMask = ringSize - 1;
static constexpr uint32_t ioChunk = 8192;

static uint8_t __attribute__((aligned(4))) ringBuf[ringSize];

static volatile uint32_t ringWritePos = 0; // bytes ever put into ringBuf
static volatile uint32_t ringReadPos = 0;  // bytes ever taken out of ringBuf

enum class Mode
{
	Idle,
	Play
};

// Requests from core 0 to core 1. Core 0 fills in the pending values, then sets
// modeRequest, and waits for core 1 to clear it (see SetMode()).
static volatile bool modeRequest = false;
static volatile Mode pendingMode = Mode::Idle;
static volatile uint32_t pendingChannels = 1;
static volatile uint32_t pendingRateStep = 65536;
static volatile int32_t pendingNumFrames = 0;

// Requests from core 1 (audio) to core 0 (USB/file system). Core 1 fills in the
// values, then sets request, and core 0 clears it once it has acted.
enum class Request : uint8_t
{
	None,      // nothing to do
	Station,   // load requestStation, landing per the play mode
	Bank,      // switch to bank requestStation, starting at station 0
	Retrigger  // restart the current station from the live loop start (loopStartQ12)
};

static volatile Request request = Request::None;
static volatile int32_t requestStation = 0; // station index to load / bank to switch to

// How the card lands when you tune or change bank. Both modes still loop the station;
// the mode only decides where a new station starts.
enum class PlayMode : uint8_t
{
	Loop,   // start the new station at its loop start point
	Sync    // start it at the same proportional position as the old file (sync jump)
};

// Settings loaded from /settings.txt in the stick's root. Core 0 reads the file
// once per mount, after the filesystem has mounted and the file has opened
// successfully; core 1 then applies the values at a sample boundary (via
// settingsDirty). With no file, every value below is the default, so the card
// behaves exactly as if the feature did not exist.
enum class PulseMode : uint8_t
{
	Gate,  // high while the loop plays, low during the wrap/seek gap
	Tick,  // short pulse at each loop wrap
	Clock, // the active clock pulse (internal tempo, or external if patched)
	Off    // always low
};

enum class LedMode : uint8_t
{
	Verbose, // level, playing, station-change, mounted, underrun/fault
	Quiet    // level, playing, mounted only (LEDs 3 and 5 stay dark)
};

static volatile PlayMode cfgMode = PlayMode::Sync; // power-on tune mode
static volatile int32_t cfgBank = 0;               // start bank
static volatile int32_t cfgClockBpm = 120;         // internal clock tempo
static volatile int32_t cfgTriPulses = 40;         // clock pulses per triangle cycle
static volatile PulseMode cfgPulse1 = PulseMode::Gate;
static volatile PulseMode cfgPulse2 = PulseMode::Tick;
static volatile LedMode cfgLed = LedMode::Verbose;

// The internal clock period in Q8 samples, derived from cfgClockBpm. Global so
// core 1 can follow it without a cross-core member write.
static volatile int32_t clockDefaultQ8 = 24000 << 8; // 120 BPM (0.5s)

// Set by core 0 after it (re)reads /settings.txt, so core 1 re-applies them.
static volatile bool settingsDirty = false;

// Set by core 0 while a USB stick is present but its filesystem is not yet
// readable (it has enumerated but the root directory cannot be listed yet).
// Core 1 blinks all six LEDs while this is true, so the card clearly shows it is
// waiting rather than appearing hung. Cleared once the volume is readable (or
// the stick is gone).
static volatile bool waitingForDrive = false;

// Set by core 0 once it has loaded a bank (at mount, and on every bank change),
// after curBank/curStation/numFiles/numBanks are up to date. Core 1 then resets
// its selection baselines to what is actually playing, so the settings-chosen
// start bank (cfgBank) is not overridden by the Main knob at power-on: the knob
// must be moved to act.
static volatile bool selectionResync = false;

// The live loop-start and loop-length points, as fractions of the file (0..4095).
// Core 1 keeps these up to date from the Y knob + Audio in 1 (start) and Audio
// in 2 (length); core 0 reads them whenever a station starts, retriggers or
// reaches the end of its loop, so the window can be moved while a file plays
// and takes effect at the next loop.
static volatile int32_t loopStartQ12 = 0;
static volatile int32_t loopLengthQ12 = 4095; // 4095 = whole file

// The play mode (Loop or Sync), written by core 1 (switch long-press) and read
// by core 0. Defaults to Sync so the card powers up in the tuning-sweep feel.
static volatile PlayMode playMode = PlayMode::Sync;

// Published by core 0, read by core 1
static volatile int32_t numFiles = 0;    // number of WAV files in the current bank
static volatile int32_t curStation = 0;  // index of the station now playing
static volatile int32_t numBanks = 1;    // banks found (root is bank 0)
static volatile int32_t curBank = 0;     // bank now playing
static volatile bool stickMounted = false;

// Why the card is (or is not) playing, so the panel LEDs can say. There is no
// serial console on a USB-host card, so the LEDs are the only feedback when
// something goes wrong.
enum class Scan : uint8_t
{
	NoStick,      // nothing plugged in
	Mounting,     // stick present, filesystem not mounted yet
	MountFailed,  // filesystem would not mount (sector size, filesystem type)
	NoFiles,      // mounted, but no .wav files in the root directory
	Unplayable,   // mounted with files, but none could be parsed/opened
	Ready         // mounted with at least one playable file
};
static volatile Scan scanState = Scan::NoStick;


////////////////////////////////////////////////////////////////////////////////
// Core 1: the audio code

class RadioComputer : public ComputerCard
{
	// The current mode, latched from the pending values by HandleRequests()
	Mode mode = Mode::Idle;

public:
	RadioComputer()
	{
		// Build the tape-speed table once at power-on: 256 steps from -2 to +2
		// octaves, stored as Q12 multipliers (4096 = 1x, 1024 = 0.25x, 16384 =
		// 4x). This is the only floating point, and it runs once, so it costs
		// nothing in the audio path.
		const float lo = 0.25f, hi = 4.0f;
		const float ratio = powf(hi / lo, 1.0f / 255.0f);
		float s = lo;
		for (int i = 0; i < 256; i++)
		{
			speedTable[i] = (int32_t)(s * 4096.0f + 0.5f);
			s *= ratio;
		}
	}

private:
	// Apply the settings.txt values that live in globals: the boot tune mode and
	// the internal clock tempo. Called by core 1 (via settingsDirty) after core 0
	// has read /settings.txt; never from the constructor. It writes only members
	// and the shared clockDefaultQ8, so it is safe at a sample boundary.
	void ApplySettings()
	{
		playMode = cfgMode;

		int32_t bpm = cfgClockBpm; // already clamped by the parser, belt and braces
		if (bpm < 30) bpm = 30;
		if (bpm > 240) bpm = 240;
		clockDefaultQ8 = (int32_t)((48000LL << 8) * 60 / bpm);

		// If no external clock is driving us, retarget the internal clock so the
		// new tempo takes effect (it will slew to it).
		if (!clkHavePrev)
		{
			clockTargetQ8 = clockDefaultQ8;
		}
	}

public:
	// Playback format
	uint32_t channels = 1;
	uint32_t blockAlign = 2;  // bytes per frame (one sample for each channel)
	int32_t rateStep = 65536; // file sample rate / 48kHz, in 16.16 fixed point
	int32_t numFrames = 0;    // length of the file (from the start point)

	// Tape speed. The X knob and CV in 2 pick one of 256 octave steps between
	// 0.25x and 4x (i.e. -2 to +2 octaves), and speedTable[index] is that step
	// as a Q12 multiplier (4096 = 1x). Because the multiply that applies it to
	// rateStep is done only when the index changes -- not per sample -- we can
	// afford a 64-bit product there and keep the audio path to a plain add.
	int32_t speedTable[256];
	int32_t speedIndex = 128;          // 128 = centre = 1x
	int32_t lastSpeedIndex = -1;       // forces a recompute
	int32_t effectiveRateStep = 65536; // rateStep x speed, 16.16
	int32_t speedCountdown = 0;        // samples until the next speed recompute

	// Playback position: frame + frac / 65536 is the position in the file, in
	// frames. For a 44.1kHz file, each output sample advances this by 0.91875
	// frames.
	int32_t frame = 0;
	int32_t frac = 0;

	// Station selection. lastDesired is the station the selector last asked for,
	// so that we only act when the knob or CV actually moves to a new station.
	// It starts at 0 because core 0 always begins playing the first file, so a
	// knob already at the bottom of its travel asks for nothing; a knob pointing
	// elsewhere tunes there on the first sample.
	int32_t lastDesired = 0;

	// Bank selection. While the switch is Up, Main/CV1 sweeps banks instead of
	// stations; lastBankDesired tracks the last bank we asked for, so we only
	// send a Bank request when it changes. wasSelectingBank gives pickup: the
	// bank only changes once the knob moves after entering bank mode.
	int32_t lastBankDesired = -1;
	bool wasSelectingBank = false;

	// Set for one sample after core 0 loads a bank: adopt the knob's current
	// position as the baseline without acting on it, so the settings start bank
	// is not overridden until the knob is actually moved.
	bool adoptSelection = false;

	// False until core 0 has loaded a bank and told us to re-baseline. While
	// false, selector changes are ignored entirely, so the Main knob cannot
	// override the settings start bank during the window before the stick's
	// banks are known.
	bool selectionReady = false;

	int32_t xrunLedTimer = 0;
	int32_t stationFlash = 0; // counts down while LED 3 shows a station change
	uint32_t led2Phase = 0;   // free-running phase for the LED 2 mode blink
	uint32_t waitPhase = 0;   // free-running phase for the "waiting for drive" blink

	// Switch-down long-press: a short tap retriggers, a ~1s hold toggles the play
	// mode. downHeld counts samples spent in the Down position.
	uint32_t downHeld = 0;
	static constexpr uint32_t kModeHoldSamples = 48000; // 1s at 48kHz

	// Mode-toggle feedback: all six LEDs blink a distinct count (2 = Loop,
	// 3 = Sync) so the new mode is unmistakable. Each 4096-sample bit is one
	// on/off half, so 2 blinks = 16384 samples, 3 blinks = 24576.
	int32_t modeBlink = 0;

	// Wrap tick: a short pulse on Pulse out 2 each time a loop reaches its end.
	int32_t wrapTickTimer = 0;
	bool endNotified = false; // set when the wrap tick for this loop has fired

	// Modulation sources on the two otherwise-unused CV outputs, so the card
	// doubles as a slow LFO and a clocked random for patching back into its own
	// (or another module's) inputs. A shared clock drives both.
	//
	//   CV out 2  a bipolar triangle, one cycle per 40 clock pulses
	//   CV out 1  a random stepped CV, re-sampled on every clock pulse
	//
	// The clock runs at 120 BPM internally; Pulse in 1 overrides it with an
	// external clock, and if that stops for a second the internal clock returns.
	// The measured tempo is slewed so the triangle glides between rates.
	//
	// Periods are held in Q8 samples (1/256 sample resolution), so an external
	// period up to ~10s still fits in an int32.
	static constexpr int32_t kClockDefaultQ8 = 24000 << 8; // 120 BPM (0.5s)
	static constexpr int kClkSlewShift = 10;               // ~21ms glide
	static constexpr int32_t kClockTimeoutSamples = 48000; // 1s without an edge
	static constexpr int32_t kClockMinSamples = 2400;      // 20Hz fastest
	static constexpr int32_t kClockMaxSamples = 480000;    // 0.1Hz slowest

	int32_t clockTargetQ8 = kClockDefaultQ8; // where the tempo is heading
	int32_t clockPeriodQ8 = kClockDefaultQ8; // slewed current tempo
	int32_t clockCount = kClockDefaultQ8;    // Q8 samples until the next pulse
	int32_t clkSampleCount = 0;              // samples since the last external edge
	int32_t clkTimeout = 0;                  // counts down while external clock is live
	bool clkHavePrev = false;                // have we seen an edge to measure from
	uint32_t triPhase = 0;                   // Q32 triangle phase (one cycle = 2^32)
	int32_t triInc = 4474;                   // Q32 phase step per sample
	int32_t lastTriPeriod = 0;              // recompute triInc when this changes
	uint32_t rngSeed = 0x1234567u;           // any nonzero seed
	int32_t randHeld = 0;                   // current sample-and-hold value

	// Linear interpolation between a and b. frac12 is 0-4095.
	static int32_t Interp(int32_t a, int32_t b, int32_t frac12)
	{
		return a + (((b - a) * frac12) >> 12);
	}

	void Xrun()
	{
		xrunLedTimer = 4800; // light the LED for 100ms
	}

	// Apply any request from core 0. This happens at the very start of
	// ProcessSample(), so we never change state halfway through a sample.
	void HandleRequests()
	{
		if (modeRequest)
		{
			mode = pendingMode;
			channels = pendingChannels;
			blockAlign = channels * 2;
			rateStep = pendingRateStep;
			numFrames = pendingNumFrames;
			frame = 0;
			frac = 0;
			lastSpeedIndex = -1; // force the speed step to be recomputed
			endNotified = false; // arm the wrap tick for this loop
			ringWritePos = 0; // both positions are ours to reset,
			ringReadPos = 0;  // because core 0 is waiting

			// Everything above must be visible to core 0 before it sees that
			// the request is complete
			__dmb();
			modeRequest = false;
		}
	}

	// Produce one output sample from the file. left and right are 16-bit.
	void Play(int32_t &left, int32_t &right)
	{
		if (frame >= numFrames)
		{
			// Reached the end. Core 0 sees this through ringReadPos and loops
			// the station back to the live start point. Fire the wrap tick once
			// (~5ms) for other modules to follow.
			if (!endNotified)
			{
				endNotified = true;
				wrapTickTimer = 240;
			}
			return;
		}

		// To interpolate, we need this frame and the next one (unless this is
		// the last). Check that core 0 has already put them in the buffer.
		// The difference is signed, because frame can skip ahead of what has
		// been read if we've underrun.
		uint32_t pos = (uint32_t)frame * blockAlign;
		bool haveNext = (frame + 1 < numFrames);
		int32_t need = (int32_t)(haveNext ? 2 * blockAlign : blockAlign);
		if ((int32_t)(ringWritePos - pos) < need)
		{
			// Underrun: core 0 hasn't read the data from the stick in time.
			// We output silence, and wait. At the very start of a file this is
			// normal, because the buffer is still filling, so the LED isn't lit.
			if (frame > 0)
			{
				Xrun();
			}
			return;
		}

		// The samples are int16, interleaved left, right for stereo files
		const int16_t *a = (const int16_t *)(ringBuf + (pos & ringMask));
		const int16_t *b = haveNext ? (const int16_t *)(ringBuf + ((pos + blockAlign) & ringMask)) : a;
		int32_t frac12 = frac >> 4;

		left = Interp(a[0], b[0], frac12);
		right = (channels == 2) ? Interp(a[1], b[1], frac12) : left;

		// Advance by one output sample's worth of the file, at the tape speed
		// currently selected (effectiveRateStep is recomputed by HandleControls
		// whenever the speed control moves).
		int32_t f = frac + effectiveRateStep;
		frame += f >> 16;
		frac = f & 0xFFFF;

		// Now we've finished with the data before this frame, so tell core 0 that
		// it can be overwritten. The barrier makes sure the reads above have
		// really happened first.
		__dmb();
		ringReadPos = (uint32_t)frame * blockAlign;
	}

	// Read the live controls, work out what core 0 should do next, and update the
	// tape-speed step. Also handles the switch (down retriggers/toggles mode, up
	// selects banks) and the automatic loop when the current file reaches its end.
	void HandleControls()
	{
		// Core 0 has just loaded a bank (at mount, or on a bank change): adopt
		// what is actually playing as the selection baseline, so the settings
		// start bank survives power-on. The Main knob must then be *moved* to
		// change station or bank (the usual pickup behaviour).
		if (selectionResync)
		{
			selectionResync = false;
			lastDesired = curStation;
			lastBankDesired = curBank;
			wasSelectingBank = false;
			adoptSelection = true; // adopt the knob position without acting once
			selectionReady = true;
		}

		// --- tape speed: X knob + CV in 2, exponential over +/-2 octaves -----
		//
		// The knob and CV are summed into a single 0..4095 position, mapped to
		// the 256-entry octave table. effectiveRateStep is the native resample
		// step scaled by that multiplier; recomputing it here (rather than per
		// sample) keeps the 64-bit product out of the audio path. It is only
		// recomputed when the index changes, and at most once every 16 samples,
		// so a fast-moving CV cannot drag the multiply into every sample.
		int32_t speed = KnobVal(Knob::X);
		if (Connected(Input::CV2))
		{
			speed += CVIn2() + 2048;
		}
		if (speed < 0) speed = 0;
		if (speed > 4095) speed = 4095;

		if (speedCountdown > 0) speedCountdown--;
		speedIndex = speed >> 4; // 0..4095 -> 0..255
		if (speedIndex != lastSpeedIndex && speedCountdown == 0)
		{
			lastSpeedIndex = speedIndex;
			speedCountdown = 16; // ~3kHz at 48kHz, plenty for a speed control
			effectiveRateStep = (int32_t)(((int64_t)rateStep * speedTable[speedIndex]) >> 12);
			if (effectiveRateStep < 1) effectiveRateStep = 1;
		}

		// --- loop start: Y knob + Audio in 1 --------------------------------
		//
		// Published continuously so core 0 can use the live value at the next
		// retrigger or loop (both the switch and the end-of-file wrap).
		int32_t start = KnobVal(Knob::Y);
		if (Connected(Input::Audio1))
		{
			start += AudioIn1() + 2048;
		}
		if (start < 0) start = 0;
		if (start > 4095) start = 4095;
		loopStartQ12 = start;

		// --- loop length: Audio in 2 ----------------------------------------
		//
		// A fraction of the whole file: -6V is a very short loop, 0V is half the
		// file, +6V is the whole file. Unpatched, the loop runs to the end of the
		// file, exactly as before.
		int32_t length = 4095;
		if (Connected(Input::Audio2))
		{
			length = AudioIn2() + 2048;
			if (length < 1) length = 1;
			if (length > 4095) length = 4095;
		}
		loopLengthQ12 = length;

		// The Main knob is the primary selector, running 0 to full scale. CV in 1
		// adds to it, so patching a CV lets you tune by voltage as easily as by
		// hand. (An unpatched CV adds nothing; the probe tells us it is absent.)
		// While the switch is Up this selects the *bank*; otherwise it selects
		// the *station* within the bank.
		int32_t selector = KnobVal(Knob::Main);
		if (Connected(Input::CV1))
		{
			selector += CVIn1() + 2048;
		}
		if (selector < 0) selector = 0;
		if (selector > 4095) selector = 4095;

		// --- switch: down retriggers / toggles mode; up selects banks --------
		//
		// Down is momentary: a short tap retriggers to the loop start, a ~1s hold
		// toggles the play mode (Loop <-> Sync), edge-tracked so a hold does not
		// also retrigger. Up is the bank selector: while it is held, Main/CV1
		// chooses the bank.
		bool selectingBank = (SwitchVal() == Up);

		if (SwitchVal() == Down)
		{
			if (downHeld < kModeHoldSamples)
			{
				downHeld++;
				if (downHeld == kModeHoldSamples)
				{
					// Held long enough: toggle mode and flash it out on the LEDs.
					playMode = (playMode == PlayMode::Loop) ? PlayMode::Sync : PlayMode::Loop;
					modeBlink = (playMode == PlayMode::Loop) ? 16384 : 24576; // 2 or 3 blinks
				}
			}
		}
		else if (SwitchChanged() && downHeld > 0)
		{
			// Just released. A short tap retriggers; a long hold already toggled.
			if (downHeld < kModeHoldSamples && request == Request::None)
			{
				__dmb();
				request = Request::Retrigger;
				stationFlash = 24000;
			}
			downHeld = 0;
		}

		if (selectingBank)
		{
			// Sweep banks. A new bank always starts at station 0. Entering bank
			// mode picks up from wherever the knob currently points, so holding
			// the switch changes nothing until the knob actually moves to a
			// different bank.
			int32_t nb = numBanks;
			if (nb > 0 && selectionReady)
			{
				int32_t wantBank = (int32_t)(((int32_t)nb * selector) >> 12);
				if (wantBank >= nb) wantBank = nb - 1;

				if (!wasSelectingBank || adoptSelection)
				{
					lastBankDesired = wantBank; // adopt the knob's bank on entry
				}
				else if (wantBank != lastBankDesired && request == Request::None)
				{
					requestStation = wantBank;
					__dmb();
					request = Request::Bank;
					stationFlash = 24000;
					lastBankDesired = wantBank;
				}
			}
		}
		else
		{
			int32_t n = numFiles;
			if (n > 0 && selectionReady)
			{
				// Map the selector (0..4095) onto the station list. n is bounded
				// by the number of files in the bank, so this stays well inside
				// int32_t; an int64_t multiply would be a slow library call in
				// the 48kHz interrupt for no benefit.
				int32_t desired = (int32_t)(((int32_t)n * selector) >> 12);
				if (desired >= n) desired = n - 1;

				if (wasSelectingBank || adoptSelection)
				{
					// Just entered station select, or just adopted the settings
					// start bank: take the knob's position as the baseline without
					// acting, so it stays on station 0 until the knob moves.
					lastDesired = desired;
				}
				else if (desired != lastDesired && request == Request::None)
				{
					// Only a real change in the selector asks for a new station.
					requestStation = desired;
					__dmb();
					request = Request::Station;
					stationFlash = 24000; // half a second at 48kHz
					lastDesired = desired;
				}
			}
			// Leaving bank select (or just playing): keep the bank selector in
			// step with the bank actually playing, so the next hold starts there.
			lastBankDesired = curBank;
		}

		adoptSelection = false;

		wasSelectingBank = selectingBank;

		// Pulse in 2: an external trigger, identical to a short switch-down tap.
		if (PulseIn2RisingEdge() && request == Request::None)
		{
			__dmb();
			request = Request::Retrigger;
			stationFlash = 24000;
		}
	}

public:
	// Called at 48kHz
	virtual void ProcessSample()
	{
		// Apply freshly loaded settings once, at a sample boundary (core 0 sets
		// settingsDirty after reading /settings.txt).
		if (settingsDirty)
		{
			settingsDirty = false;
			ApplySettings();
		}

		HandleRequests();
		HandleControls();

		int32_t left = 0, right = 0;
		if (mode == Mode::Play)
		{
			Play(left, right);
		}

		// Play() produces 16-bit samples; the DAC wants 12-bit. When idle both
		// outputs are silent.
		left >>= 4;
		right >>= 4;
		AudioOut1(left);
		AudioOut2(right);

		// --- shared clock: external on Pulse in 1, else internal 120 BPM -----
		//
		// An external clock edge sets the target tempo (measured from the gap
		// between edges) and fires a tick immediately; while an external clock is
		// live the internal countdown is idle. If no edge arrives for a second,
		// the tempo glides back to 120 BPM and the internal countdown resumes.
		clkSampleCount++;
		bool clockTick = false;

		if (PulseIn1RisingEdge())
		{
			if (clkHavePrev)
			{
				int32_t m = clkSampleCount;
				if (m < kClockMinSamples) m = kClockMinSamples;
				if (m > kClockMaxSamples) m = kClockMaxSamples;
				clockTargetQ8 = m << 8;
			}
			clkHavePrev = true;
			clkSampleCount = 0;
			clkTimeout = kClockTimeoutSamples;
			clockCount = clockPeriodQ8; // resync the internal countdown
			clockTick = true;
		}

		if (clkTimeout > 0)
		{
			if (--clkTimeout == 0)
			{
				clockTargetQ8 = kClockDefaultQ8; // external clock stopped
				clkHavePrev = false;
				clockCount = clockPeriodQ8;      // restart the internal clock cleanly
			}
		}
		else
		{
			clockCount -= 256; // decrement one sample (periods are in Q8 samples)
			if (clockCount <= 0)
			{
				clockCount += clockPeriodQ8;
				clockTick = true;
			}
		}

		// Slew the tempo toward its target, so a changing clock glides.
		clockPeriodQ8 += (clockTargetQ8 - clockPeriodQ8) >> kClkSlewShift;

		// CV out 1: a new random value on every clock pulse.
		if (clockTick)
		{
			rngSeed = 1664525u * rngSeed + 1013904223u;
			randHeld = (int32_t)(rngSeed >> 20) - 2048; // -2048..2047
		}
		CVOut1((int16_t)randHeld);

		// CV out 2: a bipolar triangle, one full cycle per cfgTriPulses clock
		// pulses. The per-sample step is recomputed only when the slewed period's
		// integer part changes (a control-rate divide, not per sample).
		int32_t periodSamples = clockPeriodQ8 >> 8;
		if (periodSamples < 1) periodSamples = 1;
		if (periodSamples != lastTriPeriod)
		{
			lastTriPeriod = periodSamples;
			int32_t pulses = cfgTriPulses;
			if (pulses < 1) pulses = 1;
			uint32_t inc = (uint32_t)(0x100000000ULL / ((uint64_t)pulses * (uint64_t)periodSamples));
			triInc = (inc < 1) ? 1 : (int32_t)inc;
		}
		triPhase += (uint32_t)triInc;
		{
			uint32_t folded = (triPhase & 0x80000000u) ? ~triPhase : triPhase;
			int32_t tri = (int32_t)(folded >> 15); // 0..65535, up then down
			CVOut2((int16_t)((tri >> 4) - 2048));  // bipolar -2048..2047
		}

		// Pulse outs. Each does one of: gate (high while looping), tick (short
		// pulse at each loop wrap), clock (the active clock pulse), or off.
		if (wrapTickTimer > 0)
		{
			wrapTickTimer--;
		}
		const bool loopGate = (mode == Mode::Play) && (frame < numFrames);
		const bool wrapTick = wrapTickTimer > 0;
		const PulseMode pm1 = cfgPulse1;
		const PulseMode pm2 = cfgPulse2;
		bool out1 = false, out2 = false;
		if (pm1 == PulseMode::Gate) out1 = loopGate;
		else if (pm1 == PulseMode::Tick) out1 = wrapTick;
		else if (pm1 == PulseMode::Clock) out1 = clockTick;
		if (pm2 == PulseMode::Gate) out2 = loopGate;
		else if (pm2 == PulseMode::Tick) out2 = wrapTick;
		else if (pm2 == PulseMode::Clock) out2 = clockTick;
		PulseOut1(out1);
		PulseOut2(out2);

		// LEDs. Show levels, and status. There is no serial console on a
		// USB-host card, so these carry the diagnosis when a stick misbehaves:
		//   LED 4           USB stick detected
		//   LED 4 + LED 5   filesystem would not mount
		//   LED 4 + LED 3   mounted, but no .wav files found
		//   LED 4 + 3 + 5   files found, but none could be opened/parsed
		//   LED 4 + LED 2   playing
		LedBrightness(0, (uint16_t)std::min<int32_t>(std::abs(left) * 2, 4095));
		LedBrightness(1, (uint16_t)std::min<int32_t>(std::abs(right) * 2, 4095));

		const Scan s = scanState;

		// While a stick is present but its filesystem is not yet readable, blink
		// all six LEDs (~0.34s on, ~0.34s off) so the card clearly shows it is
		// waiting for the drive. This takes over the panel and returns.
		if (waitingForDrive)
		{
			waitPhase++;
			const bool on = (waitPhase >> 14) & 1;
			for (int i = 0; i < 6; i++)
			{
				LedOn(i, on);
			}
			return;
		}

		// Mode-toggle feedback takes over all six LEDs for a moment: the whole
		// panel blinks 2 times for Loop mode, 3 times for Sync mode.
		if (modeBlink > 0)
		{
			modeBlink--;
			const bool on = (modeBlink >> 12) & 1;
			for (int i = 0; i < 6; i++)
			{
				LedOn(i, on);
			}
			return;
		}

		// LED 2 shows both "playing" and the tune mode: off when idle, steady
		// while playing in Sync mode, and slowly blinking while playing in Loop
		// mode. (The panel-wide mode-toggle blink above is the momentary cue.)
		led2Phase++;
		const bool playing = (mode == Mode::Play) && (frame < numFrames);
		if (!playing)
		{
			LedOff(2);
		}
		else if (playMode == PlayMode::Loop)
		{
			LedOn(2, (led2Phase >> 14) & 1); // ~0.34s on, ~0.34s off (~1.5Hz)
		}
		else
		{
			LedOn(2, true); // Sync: steady
		}

		// LED 3: station-change blink when it happens; a steady "no files" or
		// "unplayable" fault otherwise. Quiet LED mode leaves it dark.
		const bool verbose = (cfgLed == LedMode::Verbose);
		if (!verbose)
		{
			stationFlash = 0;
			LedOff(3);
		}
		else if (stationFlash > 0)
		{
			stationFlash--;
			LedOn(3, (stationFlash >> 12) & 1); // slow blink, so a change is obvious
		}
		else
		{
			LedOn(3, s == Scan::NoFiles || s == Scan::Unplayable);
		}

		LedOn(4, stickMounted);
		if (xrunLedTimer > 0)
		{
			xrunLedTimer--;
		}
		// LED 5: underrun while playing; a steady mount/unplayable fault otherwise.
		// Quiet LED mode leaves it dark.
		if (xrunLedTimer > 0)
		{
			LedOn(5, true);
		}
		else if (verbose)
		{
			LedOn(5, s == Scan::MountFailed || s == Scan::Unplayable);
		}
		else
		{
			LedOff(5);
		}
	}
};


////////////////////////////////////////////////////////////////////////////////
// Core 0: startup and handshake with core 1

static FATFS fatFs; // the mounted filesystem
static FIL wavFile; // the file being played
static RadioComputer *card = nullptr;

// Run on core 1. ComputerCard::Run() doesn't return.
static void Core1Entry()
{
	card->Run();
}

// Ask core 1 to switch to a new mode, and wait until it has. It resets the
// ring buffer, so this is also how we start a new file.
static void SetMode(Mode mode, uint32_t channels = 1, uint32_t sampleRate = 48000, int32_t numFrames = 0)
{
	pendingMode = mode;
	pendingChannels = channels;
	pendingRateStep = (uint32_t)(((uint64_t)sampleRate << 16) / 48000);
	pendingNumFrames = numFrames;
	__dmb();
	modeRequest = true;
	while (modeRequest)
	{
		tight_loop_contents();
	}
}

// WAV files are little-endian
static uint16_t Le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t Le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

////////////////////////////////////////////////////////////////////////////////
// Playback (core 0)

struct WavFormat
{
	uint32_t channels;
	uint32_t sampleRate;
	uint32_t blockAlign;
	FSIZE_t dataOffset; // where in the file the sample data starts
	uint32_t dataBytes; // length of the sample data
};

enum class PlayResult
{
	Finished,    // reached the end of the file
	Interrupted, // core 1 asked for a station change or retrigger
	Error        // couldn't play this file
};

// Parse the header of a WAV file, which is a sequence of chunks, each with a
// four-character name and a length. We need the "fmt " chunk, which describes
// the audio format, and the "data" chunk, which holds the samples. Others (for
// example "LIST" metadata) are skipped. On return the file position is at the
// start of the sample data.
static bool ParseWav(FIL *fil, WavFormat &fmt)
{
	uint8_t buf[40];
	UINT br;

	if (f_read(fil, buf, 12, &br) != FR_OK || br != 12)
	{
		return false;
	}
	if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
	{
		return false;
	}

	bool haveFmt = false;
	uint32_t format = 0, bits = 0;
	while (true)
	{
		if (f_read(fil, buf, 8, &br) != FR_OK || br != 8)
		{
			return false;
		}
		uint32_t chunkSize = Le32(buf + 4);
		FSIZE_t chunkData = f_tell(fil);

		if (memcmp(buf, "fmt ", 4) == 0)
		{
			UINT n = (chunkSize < sizeof(buf)) ? chunkSize : sizeof(buf);
			if (n < 16 || f_read(fil, buf, n, &br) != FR_OK || br != n)
			{
				return false;
			}
			format = Le16(buf);
			fmt.channels = Le16(buf + 2);
			fmt.sampleRate = Le32(buf + 4);
			fmt.blockAlign = Le16(buf + 12);
			bits = Le16(buf + 14);
			if (format == 0xFFFE && n >= 26) // WAVE_FORMAT_EXTENSIBLE: real format is in the subformat GUID
			{
				format = Le16(buf + 24);
			}
			haveFmt = true;
		}
		else if (memcmp(buf, "data", 4) == 0)
		{
			if (!haveFmt)
			{
				return false;
			}
			// The size can be wrong (0xFFFFFFFF in a recording that was never
			// finished, for example), so never trust it beyond the end of the file
			FSIZE_t left = f_size(fil) - chunkData;
			fmt.dataOffset = chunkData;
			fmt.dataBytes = (chunkSize < left) ? chunkSize : (uint32_t)left;
			break;
		}

		// Skip to the next chunk (chunks are padded to an even length)
		if (f_lseek(fil, chunkData + chunkSize + (chunkSize & 1)) != FR_OK)
		{
			return false;
		}
	}

	if (format != 1 || bits != 16 || (fmt.channels != 1 && fmt.channels != 2)
	    || fmt.blockAlign != fmt.channels * 2 || fmt.sampleRate < 1000 || fmt.sampleRate > 192000)
	{
		return false;
	}
	fmt.dataBytes -= fmt.dataBytes % fmt.blockAlign; // whole frames only
	return fmt.dataBytes > 0;
}

// Play one file over the window [startQ12, endQ12), both fractions of the whole
// file (0..4095). startQ12 is where this pass begins and endQ12 is where the
// loop wraps: core 1 stops at endQ12 and core 0 reloops back to the live loop
// start. Core 0 has one job while this runs: keep the ring buffer full. On
// return, normOut is the proportional position (0..4095) when playback stopped,
// for the synchronised jump to the next station.
static PlayResult PlayFile(const char *name, int32_t startQ12, int32_t endQ12, int32_t &normOut)
{
	normOut = 0;
	if (f_open(&wavFile, name, FA_READ) != FR_OK)
	{
		return PlayResult::Error;
	}

	WavFormat fmt = {};
	if (!ParseWav(&wavFile, fmt))
	{
		f_close(&wavFile);
		return PlayResult::Error;
	}

	int32_t numFrames = (int32_t)(fmt.dataBytes / fmt.blockAlign);
	if (numFrames <= 0)
	{
		f_close(&wavFile);
		return PlayResult::Error;
	}

	// Convert the proportional start and end points into whole frames. These are
	// divisions, but core 0 is not in the audio interrupt, so it can afford them;
	// core 1 never divides.
	int32_t startFrame = (int32_t)(((int64_t)startQ12 * numFrames) >> 12);
	if (startFrame >= numFrames) startFrame = numFrames - 1;
	if (startFrame < 0) startFrame = 0;

	int32_t endFrame = (int32_t)(((int64_t)endQ12 * numFrames) >> 12);
	if (endFrame > numFrames) endFrame = numFrames;
	if (endFrame <= startFrame) endFrame = startFrame + 1; // at least one frame

	uint32_t total = (uint32_t)(endFrame - startFrame) * fmt.blockAlign;

	// Start core 1 playing. It sets the ring buffer positions to zero, so byte x
	// of the sample data (from startFrame on) goes at ringBuf[x % ringSize].
	f_lseek(&wavFile, fmt.dataOffset + (FSIZE_t)startFrame * fmt.blockAlign);
	SetMode(Mode::Play, fmt.channels, fmt.sampleRate, (int32_t)(endFrame - startFrame));

	PlayResult result = PlayResult::Error;

	// tuh_msc_mounted() goes false if the stick is pulled out
	while (tuh_msc_mounted(1))
	{
		if (request != Request::None)
		{
			result = PlayResult::Interrupted;
			break;
		}

		// Have we reached the end of the file? Core 1 publishes that through
		// ringReadPos, which reaches `total` once the last frame is played.
		if ((uint32_t)ringReadPos >= total)
		{
			result = PlayResult::Finished;
			break;
		}

		// Is there room for another chunk, and more file to read? The difference
		// is signed, as core 1 can move ringReadPos past ringWritePos briefly
		// if it underruns.
		uint32_t writePos = ringWritePos;
		bool room = (int32_t)(writePos - ringReadPos) <= (int32_t)(ringSize - ioChunk);
		if (writePos >= total || !room)
		{
			// Nothing to do. TinyUSB needs tuh_task() called regularly, or
			// nothing happens.
			tuh_task();
			continue;
		}

		// Read the next chunk from the file. writePos is always a multiple of
		// ioChunk here, so the chunk can't wrap around the end of the buffer.
		UINT n = std::min<uint32_t>(ioChunk, total - writePos);
		UINT br = 0;
		if (f_read(&wavFile, ringBuf + (writePos & ringMask), n, &br) != FR_OK || br != n)
		{
			break;
		}

		// Data must be in the buffer before core 1 is told it is there
		__dmb();
		ringWritePos = writePos + n;
	}

	// Work out where we stopped, as a fraction of the whole file, for the
	// synchronised jump. Core 1's ringReadPos is frames played since startFrame.
	int32_t played = (int32_t)(ringReadPos / fmt.blockAlign);
	int32_t absFrame = startFrame + played;
	if (absFrame > numFrames) absFrame = numFrames;
	if (absFrame < 0) absFrame = 0;
	normOut = (int32_t)(((int64_t)absFrame << 12) / numFrames);

	SetMode(Mode::Idle);
	f_close(&wavFile);
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Settings
//
// Optional /settings.txt in the stick's root, read once when the stick is
// mounted (on core 0). It is a plain text file of "key = value" lines; "#"
// starts a comment, blank lines and unknown keys are ignored, and keys are
// case-insensitive. If the file is absent, or a value is not understood, the
// default is kept, so the card behaves exactly as before without a settings
// file. The integer parser is hand-rolled: pulling in atoi/strtol would drag
// newlib's reentrancy machinery (and several KB of RAM) into the build.

static constexpr int kMaxBanks = 32;
// A bank path is "/" (root) or "/name/" where name can be a long filename, so
// allow the full 255-character name plus the two slashes and terminator.
static constexpr int kBankPathLen = 260;
// Stations are cached as bare filenames (up to a long filename + terminator).
static constexpr int kMaxStations = 64;
static constexpr int kStationNameLen = 256;

static char *Trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
	*end = 0;
	return s;
}

static bool EqualsIgnoreCase(const char *a, const char *b)
{
	return strcasecmp(a, b) == 0;
}

// Parse a decimal integer, clamped to [lo, hi]. Returns false if there are no
// digits, so a missing or malformed value can keep the default.
static bool ParseInt(const char *s, int lo, int hi, int &out)
{
	while (*s == ' ' || *s == '\t') s++;
	bool neg = false;
	if (*s == '-') { neg = true; s++; }
	else if (*s == '+') { s++; }
	if (*s < '0' || *s > '9') return false;

	long v = 0;
	while (*s >= '0' && *s <= '9')
	{
		v = v * 10 + (*s - '0');
		if (v > 1000000) v = 1000000; // clamp early, avoid overflow
		s++;
	}
	if (neg) v = -v;
	if (v < lo) v = lo;
	if (v > hi) v = hi;
	out = (int)v;
	return true;
}

// Apply one "key = value" pair. Unknown keys and values are ignored.
static void ApplySetting(char *key, char *val)
{
	if (EqualsIgnoreCase(key, "mode"))
	{
		if (EqualsIgnoreCase(val, "loop")) cfgMode = PlayMode::Loop;
		else if (EqualsIgnoreCase(val, "sync")) cfgMode = PlayMode::Sync;
	}
	else if (EqualsIgnoreCase(key, "bank"))
	{
		int v;
		if (ParseInt(val, 0, kMaxBanks - 1, v)) cfgBank = v;
	}
	else if (EqualsIgnoreCase(key, "clockBpm"))
	{
		int v;
		if (ParseInt(val, 30, 240, v)) cfgClockBpm = v;
	}
	else if (EqualsIgnoreCase(key, "triPulses"))
	{
		int v;
		if (ParseInt(val, 1, 256, v)) cfgTriPulses = v;
	}
	else if (EqualsIgnoreCase(key, "pulse1") || EqualsIgnoreCase(key, "pulse2"))
	{
		PulseMode m;
		if (EqualsIgnoreCase(val, "gate")) m = PulseMode::Gate;
		else if (EqualsIgnoreCase(val, "tick")) m = PulseMode::Tick;
		else if (EqualsIgnoreCase(val, "clock")) m = PulseMode::Clock;
		else if (EqualsIgnoreCase(val, "off")) m = PulseMode::Off;
		else return; // unrecognised: keep the default
		if (EqualsIgnoreCase(key, "pulse1")) cfgPulse1 = m;
		else cfgPulse2 = m;
	}
	else if (EqualsIgnoreCase(key, "led"))
	{
		if (EqualsIgnoreCase(val, "quiet")) cfgLed = LedMode::Quiet;
		else if (EqualsIgnoreCase(val, "verbose")) cfgLed = LedMode::Verbose;
	}
}

// Read /settings.txt if it is present, and apply its values to the config
// globals. Called on core 0 only after the filesystem has mounted; with no file
// (or a file that will not open) every default stands.
// A FIL is ~4 KB (it embeds a FF_MAX_SS=4096 byte window), which is larger than
// core 0's stack. Keep it in BSS, never on the stack, and mark this noinline so
// the compiler cannot fold the 4 KB frame into main(). (Inlining it there once
// overflowed the stack at power-up and hung the card before anything ran.)
static FIL settingsFile;

static void __attribute__((noinline)) LoadSettings()
{
	if (f_open(&settingsFile, "/settings.txt", FA_READ) != FR_OK)
	{
		return; // no settings file: keep the defaults
	}

	// settings.txt is small; read it whole, then walk it line by line.
	static char buf[1024];
	UINT br = 0;
	if (f_read(&settingsFile, buf, sizeof(buf) - 1, &br) == FR_OK)
	{
		buf[br] = 0;
		char *p = buf;
		while (*p)
		{
			char *nl = strchr(p, '\n');
			if (nl)
			{
				*nl = 0;
			}
			char *s = Trim(p);
			if (*s != 0 && *s != '#' && *s != ';')
			{
				char *eq = strchr(s, '=');
				if (eq)
				{
					*eq = 0;
					ApplySetting(Trim(s), Trim(eq + 1));
				}
			}
			if (!nl)
			{
				break;
			}
			p = nl + 1;
		}
	}
	f_close(&settingsFile);
}

////////////////////////////////////////////////////////////////////////////////
// Natural-order sorting
//
// FatFs returns directory entries in raw on-disk order (the order files were
// created, shuffled by deletions), which is not alphabetical. We sort banks and
// stations ourselves. The comparison is case-insensitive and treats runs of
// digits as numbers, so "station2" comes before "station10".

static int ToLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

// Compare two names in natural order. Returns <0, 0 or >0.
static int NaturalCompare(const char *a, const char *b)
{
	while (*a && *b)
	{
		if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			// Both at a digit: compare the whole number by value. Skip leading
			// zeros so "007" and "7" compare equal.
			while (*a == '0') a++;
			while (*b == '0') b++;
			const char *da = a, *db = b;
			while (*da >= '0' && *da <= '9') da++;
			while (*db >= '0' && *db <= '9') db++;
			const int la = (int)(da - a), lb = (int)(db - b);
			if (la != lb) return la - lb; // more digits = bigger number
			while (a < da)
			{
				if (*a != *b) return *a - *b;
				a++;
				b++;
			}
		}
		else
		{
			const int ca = ToLower((unsigned char)*a);
			const int cb = ToLower((unsigned char)*b);
			if (ca != cb) return ca - cb;
			a++;
			b++;
		}
	}
	// One string ran out: the shorter one (or an equal prefix) sorts first.
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// Insertion-sort `count` fixed-width rows of `rowLen` bytes so that the rows are
// in natural order of the strings they hold. Uses one scratch row on the stack.
static void SortRows(char *rows, int count, int rowLen)
{
	char tmp[kBankPathLen];
	for (int i = 1; i < count; i++)
	{
		char *cur = rows + (size_t)i * rowLen;
		int j = i - 1;
		memcpy(tmp, cur, rowLen);
		while (j >= 0 && NaturalCompare(rows + (size_t)j * rowLen, tmp) > 0)
		{
			memcpy(rows + (size_t)(j + 1) * rowLen, rows + (size_t)j * rowLen, rowLen);
			j--;
		}
		memcpy(rows + (size_t)(j + 1) * rowLen, tmp, rowLen);
	}
}

////////////////////////////////////////////////////////////////////////////////
// File selection
//
// A "bank" is a place to look for stations: bank 0 is the root directory, and
// each subdirectory that holds at least one WAV file is another bank. The bank
// paths are discovered once when the stick is mounted, so sweeping banks is
// instant. Banks and stations are natural-sorted, up to kMaxBanks.

static char bankPaths[kMaxBanks][kBankPathLen];        // "/" or "/folder/"
static char stationNames[kMaxStations][kStationNameLen]; // sorted, current bank

static bool IsWavFile(const FILINFO &info)
{
	const char *name = info.fname;
	size_t len = strlen(name);
	if ((info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || name[0] == '.' || len < 4) // skip macOS "._" files
	{
		return false;
	}
	return strcasecmp(name + len - 4, ".wav") == 0;
}

// Does the directory at path hold at least one WAV file?
static bool DirHasWavs(const char *path)
{
	DIR dir;
	FILINFO info;
	if (f_opendir(&dir, path) != FR_OK)
	{
		return false;
	}
	bool found = false;
	while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
	{
		if (IsWavFile(info))
		{
			found = true;
			break;
		}
	}
	f_closedir(&dir);
	return found;
}

// Discover the banks: the root first (always bank 0), then every subdirectory
// that holds WAV files, natural-sorted. Returns the number found, at least 1.
static int ScanBanks()
{
	int n = 0;
	strcpy(bankPaths[n], "/");
	n++;

	DIR dir;
	FILINFO info;
	if (f_opendir(&dir, "/") == FR_OK)
	{
		while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0 && n < kMaxBanks)
		{
			if ((info.fattrib & AM_DIR) && !(info.fattrib & (AM_HID | AM_SYS)) && info.fname[0] != '.')
			{
				char path[kBankPathLen];
				snprintf(path, sizeof(path), "/%s/", info.fname);
				if (DirHasWavs(path))
				{
					snprintf(bankPaths[n], sizeof(bankPaths[n]), "%s", path);
					n++;
				}
			}
		}
		f_closedir(&dir);
	}

	// Natural-sort the folders (bank 0, the root, stays first).
	if (n > 1)
	{
		SortRows(&bankPaths[1][0], n - 1, kBankPathLen);
	}
	return n;
}

// Collect the WAV files in one bank into the stationNames cache, natural-sorted,
// and return how many there are (capped at kMaxStations). Called on mount and
// whenever the bank changes; the audio path then reads names straight from the
// cache instead of re-scanning the directory.
static int LoadStations(const char *bankPath)
{
	int n = 0;
	DIR dir;
	FILINFO info;
	if (f_opendir(&dir, bankPath) == FR_OK)
	{
		while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0 && n < kMaxStations)
		{
			if (IsWavFile(info))
			{
				snprintf(stationNames[n], kStationNameLen, "%s", info.fname);
				n++;
			}
		}
		f_closedir(&dir);
	}
	if (n > 1)
	{
		SortRows(&stationNames[0][0], n, kStationNameLen);
	}
	return n;
}

// The live loop window [winStart, winEnd), as fractions of the whole file
// (0..4095). The start is the Y knob + Audio in 1; the length is Audio in 2.
// A degenerate window (start at the very end) falls back to the whole file.
static void LoopWindow(int32_t &winStart, int32_t &winEnd)
{
	winStart = loopStartQ12;
	winEnd = loopStartQ12 + loopLengthQ12;
	if (winEnd > 4095) winEnd = 4095;
	if (winEnd <= winStart)
	{
		winStart = 0;
		winEnd = 4095;
	}
}

// Play WAV files from the stick, until it is removed. Stations come from the
// active bank (root = bank 0, or a subfolder); each loops over its window. The
// knob/CVs retune, the switch retriggers, and holding switch-up swaps banks.
// In Loop mode a new station starts at its loop start; in Sync mode it starts at
// the same proportional position as the old file (the synchronised jump).
static void RunStick()
{
	int bankCount = ScanBanks();
	numBanks = bankCount;

	// Start on the bank configured in settings.txt (clamped to what exists).
	int bank = cfgBank;
	if (bank < 0) bank = 0;
	if (bank >= bankCount) bank = bankCount - 1;
	int count = LoadStations(bankPaths[bank]);
	curBank = bank;
	curStation = 0;
	numFiles = count;
	// Tell core 1 to re-baseline selection to what is now playing, so cfgBank is
	// not overridden by the Main knob at power-on. All published values are set
	// above; the barrier makes them visible before the flag.
	__dmb();
	selectionResync = true;

	int index = 0;
	int failures = 0;      // consecutive files that couldn't be played
	int32_t startQ12 = 0;  // where the next pass begins
	bool anyPlayed = false;
	int32_t lastNorm = 0;  // where the last (interrupted) file stopped

	scanState = (count > 0) ? Scan::Ready : Scan::NoFiles;

	while (tuh_msc_mounted(1))
	{
		// Servicing a request here (not only after a file plays) is what keeps
		// the card alive when every file fails: a fresh request clears the
		// failure count and gives the card another go, instead of freezing.
		//
		// Retrigger (switch-down, pulse-in, or end-of-file) replays the same
		// station from the loop start. Station loads a new station, landing per
		// the play mode: Loop = the loop start, Sync = the proportional position
		// of the old file, clamped into the new window. Bank switches folder and
		// starts at station 0.
		if (request != Request::None)
		{
			const Request req = request;
			request = Request::None;
			failures = 0;
			anyPlayed = false;

			int32_t winStart, winEnd;
			LoopWindow(winStart, winEnd);

			if (req == Request::Retrigger)
			{
				startQ12 = winStart;
			}
			else if (req == Request::Bank)
			{
				bank = requestStation;
				if (bank < 0) bank = 0;
				if (bank >= bankCount) bank = bankCount - 1;
				count = LoadStations(bankPaths[bank]);
				curBank = bank;
				curStation = 0;
				index = 0;
				startQ12 = winStart;
				lastNorm = 0;
				// Re-baseline core 1's selection to this bank, so the knob must be
				// moved again to change it (and the station stays 0).
				__dmb();
				selectionResync = true;
				numFiles = count;
			}
			else // Station
			{
				index = requestStation;
				if (index < 0) index = 0;
				if (index >= count) index = count - 1;

				if (playMode == PlayMode::Sync)
				{
					startQ12 = lastNorm;
					if (startQ12 < winStart) startQ12 = winStart;
					if (startQ12 >= winEnd) startQ12 = winEnd - 1;
				}
				else
				{
					startQ12 = winStart;
				}
			}
			continue;
		}

		if (count == 0)
		{
			scanState = Scan::NoFiles;
			tuh_task();
			continue;
		}

		// Once every file has failed a pass, report it and slow to a poll, but
		// keep watching for requests (handled above) and for removal.
		if (failures >= count)
		{
			scanState = anyPlayed ? Scan::Ready : Scan::Unplayable;
			tuh_task();
			continue;
		}

		int32_t winStart, winEnd;
		LoopWindow(winStart, winEnd);

		// Build the full path for this station (the bank path always ends in '/').
		char path[kBankPathLen + kStationNameLen];
		snprintf(path, sizeof(path), "%s%s", bankPaths[bank], stationNames[index]);

		curStation = index;
		PlayResult r = PlayFile(path, startQ12, winEnd, lastNorm);

		if (r == PlayResult::Interrupted)
		{
			// A core 1 request arrived; the top of the loop will act on it.
			continue;
		}

		if (r == PlayResult::Error)
		{
			failures++;
			index = (index + 1) % count;
			startQ12 = 0;
			continue;
		}

		// Reached the end of the loop window: wrap back to the live loop start
		// (pure loop — the card never advances on its own).
		anyPlayed = true;
		scanState = Scan::Ready;
		failures = 0;
		startQ12 = winStart;
	}
}

// Is the root directory of the mounted volume actually listable? The stick can
// enumerate (tuh_msc_mounted) and even mount before its medium is readable, so
// we prove readiness by opening "/" and reading one directory entry. Returns
// true only if that succeeds, which is the gate for reading settings and playing.
// noinline: keep its DIR off main's stack frame.
static bool __attribute__((noinline)) RootIsReadable()
{
	DIR dir;
	if (f_opendir(&dir, "/") != FR_OK)
	{
		return false;
	}
	FILINFO info;
	FRESULT fr = f_readdir(&dir, &info);
	f_closedir(&dir);
	// f_readdir returns FR_OK with an empty name at end-of-dir; both are fine.
	return fr == FR_OK;
}

int main()
{
	// The card is constructed here on core 0, so that it exists before core 1
	// starts running it. (It is a plain local, not a function-level static:
	// main() never returns, and a static would add a thread-safe initialisation
	// guard, which drags a lot of C++ exception code into the binary.)
	RadioComputer player;
	card = &player;

	// The normalisation probe lets ProcessSample tell whether a CV jack is
	// patched, so an unpatched CV1 does not offset the Main station selector.
	player.EnableNormalisationProbe();

	multicore_launch_core1(Core1Entry);

	// Start TinyUSB as a host
	tusb_rhport_init_t hostInit = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUH_RHPORT, &hostInit);

	while (true)
	{
		// tuh_task() runs the USB stack: enumeration of newly plugged devices,
		// and completion of transfers. It must be called often.
		tuh_task();

		if (!tuh_msc_mounted(1))
		{
			// No stick (or none yet). Nothing to wait for.
			waitingForDrive = false;
			continue;
		}

		// A stick is present. Until its filesystem is actually readable we ask
		// core 1 to blink all six LEDs, so the card shows it is waiting rather
		// than appearing hung.
		stickMounted = true;
		scanState = Scan::Mounting;
		waitingForDrive = true;

		// Wait (bounded) for the medium to be ready, the volume to mount, and
		// the root directory to be listable. tuh_msc_ready() is false while a
		// transfer is in flight or the medium is not ready, so we only try to
		// mount once it is true. f_mount returning FR_OK is not enough on its
		// own: the very first real block access is often the one that fails, so
		// prove readiness by listing the root directory before going further.
		FRESULT fr = FR_NOT_READY;
		bool readable = false;
		absolute_time_t mountDeadline = make_timeout_time_ms(5000);
		while (tuh_msc_mounted(1) && !time_reached(mountDeadline))
		{
			if (tuh_msc_ready(1))
			{
				fr = f_mount(&fatFs, "", 1);
				if (fr == FR_OK && RootIsReadable())
				{
					readable = true;
					break;
				}
			}
			tuh_task();
		}

		// From here on the LEDs are the card's own again.
		waitingForDrive = false;

		if (readable)
		{
			// The volume is mounted and readable. Read the optional settings
			// file, then let core 1 apply the values at its next sample. A
			// missing file leaves every default in place. The barrier makes the
			// cfg globals visible before core 1 sees settingsDirty.
			LoadSettings();
			__dmb();
			settingsDirty = true;

			RunStick();
			f_unmount("");
		}
		else
		{
			// Never became readable in time: show it (LED 4 + 5) and wait for
			// the stick to be removed.
			scanState = Scan::MountFailed;
		}

		// Wait for removal before trying again
		while (tuh_msc_mounted(1))
		{
			tuh_task();
		}
		stickMounted = false;
		numFiles = 0;
		scanState = Scan::NoStick;
		request = Request::None;
	}
}
