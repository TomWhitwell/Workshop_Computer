// ZX — a ZX Spectrum 128K for the Workshop Computer.
//
// BOOT SLICE (v0.2.0): a vertical slice that boots the real 128K ROM to its menu.
//   * Core 1 free-runs the Z80 + 128K memory/paging + ULA, paced to 3.5MHz.
//   * Core 0's 48kHz ProcessSample() latches the beeper to Pulse Out 1 and the
//     border to CV Out 1 / the LED bar, and advances a time reference the CPU
//     core uses to pace itself.
// AY sound, snapshots, the input-mapping engine and the Web UI bolt on next; this
// slice exists to prove CPU + memory + paging + timing + the two-core split.
//
// See spectrum.h for the machine model and CrossCore (the only shared state).

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/vreg.h"   // vreg_set_voltage for the overclock

#include "spectrum.h"
#include "machine.h"
#include "mapping.h"
#include "reverb.h"
#include "webui.h"
#include "snapshot_default.h" // pulls in snapshot_data.h if present, else empty
                              // (snapshot_z80[], snapshot_z80_len, ZX_HAVE_SNAPSHOT)

using namespace zx;

// The emulated machine + CPU runner. Single instances, reached from core 1 via
// ComputerCard::ThisPtr() (the repo's second_core idiom).
static Spectrum gSpectrum;
static Machine  gMachine;
static Mapper   gMapper;
static WebUI    gWebUI;
static Reverb   gReverb;

class ZXCard : public ComputerCard
{
public:
	ZXCard()
	{
		gMapper.LoadDefaults();   // light table setup — safe in the constructor
		gReverb.Reset();          // clear the reverb delay lines
		// Only launch the second core here. Do NOT do emulator setup
		// (Attach/Reset — which wires ROM pointers and clears 128KB of RAM) in
		// this constructor: it runs during ComputerCard's own construction,
		// before Run()/hardware init, and that heavy work wedged the chip.
		// Instead, core 1 constructs the emulator itself (proven by the STAGE 2+
		// diagnostics, which all set up the machine on core 1 and work).
		multicore_launch_core1(core1);
	}

	// Core-1 entry point. Does NOT use ThisPtr() (null before Run()); touches
	// only the file-scope gSpectrum/gMachine globals.
	static void core1()
	{
		gMachine.Attach(&gSpectrum);   // set up the emulator ON core 1
		gMachine.Reset();
		// Boot the baked-in snapshot if one is present; otherwise leave the
		// machine on the 128K menu (a game can be uploaded live over the Web UI).
		if (ZX_HAVE_SNAPSHOT)
			LoadSnapshot(gMachine, snapshot_z80, snapshot_z80_len);
		gWebUI.Init(&gSpectrum, &gMapper);   // USB-MIDI/SysEx Web UI on this core

		// Warm-up: run a few frames of emulation with the audio outputs still
		// muted (audioReady=false), so any power-on / first-instruction beeper
		// click passes silently. Then unmute.
		for (int i = 0; i < 4; i++)                 // ~4 frames (~80ms)
			gMachine.Run(kTStatesPerFrame);
		gSpectrum.xc.audioReady = true;

		EmulationCore();
	}

	// -------- Core 1: free-running Z80 + ULA, paced to real Spectrum time -----
	static void EmulationCore()
	{
		// Run in chunks and pace to wall-clock so 70908 T-states take ~20ms
		// (one 50Hz frame). We derive elapsed emulated time from how many 48kHz
		// audio samples core 0 has counted, which keeps the Spectrum locked to
		// audio time (and hence to the outputs) rather than to core1's own speed.
		uint32_t lastSample = 0;   // T-states/sample now comes from xc.tsPerSample
		                           // (per-mode: 48K vs 128K run at slightly
		                           // different clocks, set by the load path).

		while (true)
		{
			// Service USB every iteration. Cheap when idle; this keeps the Web UI
			// responsive while running, and gets full attention while paused.
			gWebUI.Task();

			// Snapshot uploads are STAGED, not applied immediately: an uploaded
			// image is loaded fresh (registers/flags/PC) only when we return to
			// run mode. Remapping-only sessions never disturb the running machine.
			static bool wasPaused = false;
			bool nowPaused = gSpectrum.xc.paused;
			if (wasPaused && !nowPaused && gWebUI.SnapshotReady())
			{
				// Leaving pause with a freshly uploaded snapshot. The browser has
				// already decoded it: RAM banks were written directly as pages
				// arrived, so we just apply the register/paging state. (No Reset()
				// — that would wipe the RAM the browser just filled.)
				gSpectrum.xc.audioReady = false;   // mute over the load transient
				gWebUI.ApplyDecoded(&gMachine);
				for (int i = 0; i < 4; i++) gMachine.Run(kTStatesPerFrame); // warm up
				gSpectrum.xc.audioReady = true;
				gWebUI.ClearSnapshot();
				lastSample = gSpectrum.xc.sampleCount;
				wasPaused = false;
				continue;
			}
			wasPaused = nowPaused;

			uint32_t now = gSpectrum.xc.sampleCount;
			uint32_t elapsed = now - lastSample; // samples since we last ran
			lastSample = now;

			// Reset+reload on request (currently unbound; kept for future use,
			// e.g. a Web-UI reset button).
			if (gSpectrum.xc.resetRequest)
			{
				gMachine.Reset();
				if (ZX_HAVE_SNAPSHOT)
					LoadSnapshot(gMachine, snapshot_z80, snapshot_z80_len);
				gSpectrum.xc.resetRequest = false;
				lastSample = gSpectrum.xc.sampleCount;
				continue;
			}
			// Paused (switch Up): don't run the CPU; spin servicing USB hard so
			// uploads/remaps are fast. This is the intended time to use the Web UI.
			if (gSpectrum.xc.paused)
			{
				for (int k = 0; k < 50; k++) gWebUI.Task();
				lastSample = gSpectrum.xc.sampleCount; // don't accumulate backlog
				continue;
			}

			if (elapsed == 0)
			{
				tight_loop_contents(); // wait for core 0 to advance time
				continue;
			}
			// PERF METER: if elapsed regularly hits the cap, core 1 can't keep up
			// with real-time (running the Spectrum in slow motion). Record the
			// worst-case backlog so core 0 can show it on the LEDs.
			if (elapsed >= gSpectrum.xc.maxElapsed) gSpectrum.xc.maxElapsed = elapsed;
			if (elapsed > 96) elapsed = 96;

			// Scale the batch by the Knob Main speed setting (100 = real time).
			// Speed changes emulated pitch on 1-bit material, which is the point;
			// a centre deadzone (set on core 0) holds exactly 100% when wanted.
			uint32_t spd = gSpectrum.xc.speedPct;
			gMachine.Run((elapsed * gSpectrum.xc.tsPerSample * spd) / 100);

			// Publish a heartbeat + paging state for core 0 to show on LEDs.
			// (LED hardware is driven only from core 0 to avoid contention.)
			gSpectrum.xc.emuAlive++;
		}
	}

	// -------- Core 0: 48kHz I/O ----------------------------------------------
	// Forced into RAM (like OneBit): core 1 saturates the flash XIP bus running
	// the Z80, which would otherwise starve a flash-resident audio callback.
	virtual void __not_in_flash_func(ProcessSample)()
	{
		// Switch (while running):
		//   Up     -> PAUSE the emulator (freeze)
		//   Middle -> RUN (normal rest position)
		//   Down   -> a mappable KEYPRESS source (held while down)
		Switch sw = SwitchVal();
		gSpectrum.xc.paused = (sw == Switch::Up);

		// --- Knob Main -> emulation speed, with a centre deadzone at 100% ------
		// Knob 0..4095. A flat band around centre (1900..2200) = exactly 100%
		// (so 1-bit pitch is stable when you want it). Below centre slows down to
		// ~25%; above speeds up to ~300%. Speed shifts emulated pitch — intended.
		int32_t km = KnobVal(Knob::Main);
		uint16_t spd;
		if (km >= 1900 && km <= 2200)      spd = 100;               // deadzone
		else if (km < 1900) spd = uint16_t(25 + (km * 75) / 1900);  // 25..100
		else                spd = uint16_t(100 + ((km - 2200) * 200) / (4095 - 2200)); // 100..300
		gSpectrum.xc.speedPct = spd;

		// Knob Y -> a Z80-readable peripheral value (0-255) at port 0x5F.
		gSpectrum.xc.knobY = uint8_t(KnobVal(Knob::Y) >> 4);

		// --- Tape EAR: Audio In 1 -> ULA EAR bit (for LOAD "") ----------------
		// The ROM's tape loader polls bit 6 of port 0xFE and times the pulses
		// itself, so we just present Audio In 1 as a clean 1-bit signal. A
		// comparator with hysteresis around 0 rejects noise; only active when a
		// jack is patched into Audio In 1 (so it doesn't interfere otherwise).
		// This is the "brave" LOAD "" path — success depends on the tape signal
		// level/quality and our 48kHz sampling resolving the ROM's pulse widths.
		{
			static bool ear = false;
			if (Connected(Input::Audio1))
			{
				int32_t a = AudioIn1();               // bipolar ~-2048..2047
				if (a >  200) ear = true;             // hysteresis band ±200
				else if (a < -200) ear = false;
				gSpectrum.xc.earIn = ear;
			}
			else gSpectrum.xc.earIn = false;
		}

		// --- Evaluate the mapping sources from the jacks + switch --------------
		// A jack only triggers if something is actually PLUGGED IN (normalisation
		// probe). This stops floating/unpatched CV/Audio inputs pressing keys.
		// For patched analog inputs, the source is active while the value is above
		// the per-mapping threshold (comparator). Pulses are digital gates.
		bool srcActive[SRC_COUNT];
		srcActive[SRC_PULSE1] = Connected(Input::Pulse1) && PulseIn1();
		srcActive[SRC_PULSE2] = Connected(Input::Pulse2) && PulseIn2();
		srcActive[SRC_CV1]    = Connected(Input::CV1)    && CVIn1()    >= gMapper.GetMapping(SRC_CV1).threshold;
		srcActive[SRC_CV2]    = Connected(Input::CV2)    && CVIn2()    >= gMapper.GetMapping(SRC_CV2).threshold;
		srcActive[SRC_AUDIO1] = Connected(Input::Audio1) && AudioIn1() >= gMapper.GetMapping(SRC_AUDIO1).threshold;
		srcActive[SRC_AUDIO2] = Connected(Input::Audio2) && AudioIn2() >= gMapper.GetMapping(SRC_AUDIO2).threshold;
		srcActive[SRC_SWITCH] = (sw == Switch::Down);

		// 0-255 value of each source for TGT_PORT (bipolar -2048..2047 -> 0..255,
		// centre ~128). Pulses/switch are 0/255. Used when mapped to a Z80 port.
		auto to255 = [](int32_t v){ v = (v + 2048) >> 4; return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
		uint8_t srcValue[SRC_COUNT];
		srcValue[SRC_PULSE1] = PulseIn1() ? 255 : 0;
		srcValue[SRC_PULSE2] = PulseIn2() ? 255 : 0;
		srcValue[SRC_CV1]    = to255(CVIn1());
		srcValue[SRC_CV2]    = to255(CVIn2());
		srcValue[SRC_AUDIO1] = to255(AudioIn1());
		srcValue[SRC_AUDIO2] = to255(AudioIn2());
		srcValue[SRC_SWITCH] = (sw == Switch::Down) ? 255 : 0;
		gMapper.Apply(srcActive, srcValue, gSpectrum);

		// Mute the audio outputs until the emulator has warmed up (kills the
		// power-on / first-instruction beeper click). Border/CV/LEDs stay live.
		bool ready = gSpectrum.xc.audioReady;

		// Latch emulator outputs (written by core 1) to the hardware.
		PulseOut1(ready && gSpectrum.xc.beeper);  // beeper (dry)
		PulseOut2(ready && gSpectrum.xc.mic);     // MIC/tape
		AudioOut2(ready ? gSpectrum.xc.aySample : 0);  // AY (dry)

		// --- Reverb (Knob X wet/dry) -> Audio Out 1 ---------------------------
		// Feed the beeper (as a ±level) + AY into the reverb; Audio Out 1 carries
		// a wet/dry blend so you get ambience without touching the dry outs.
		int16_t dry = ready ? int16_t((gSpectrum.xc.beeper ? 600 : -600) + gSpectrum.xc.aySample) : 0;
		if (dry > 2047) dry = 2047; else if (dry < -2048) dry = -2048;
		int16_t wet = gReverb.Process(dry);
		int32_t wetAmt = KnobVal(Knob::X);        // 0..4095
		int32_t mixed = (dry * (4095 - wetAmt) + wet * wetAmt) >> 12;
		AudioOut1(ready ? int16_t(mixed) : 0);

		// Border colour still drives CV Out 1 (a voltage), but no longer the LEDs.
		uint8_t b = gSpectrum.xc.border;
		CVOut1(int16_t((b * 2047) / 7));

		// CV Out 2 = the value of a UI-selectable memory location (0-255) scaled
		// to 0-5V. Sampled by core 1 at xc.probeAddr (default 16384 = screen mem,
		// so it flickers with the display). CVOut range -2048..2047 ≈ -6..+6V, so
		// 0-5V is 0..~1706. probeVal 0..255 -> 0..1706.
		CVOut2(int16_t((gSpectrum.xc.probeVal * 1706) / 255));

		// --- LED layout ------------------------------------------------------
		// Left column = machine mode (one lit):
		//   LED 0: 128K   LED 2: 48K   LED 4: AY file
		// Right column = status:
		//   LED 1: CPU heartbeat (blinks while the Z80 runs)
		//   LED 3: "correct speed" (100%) — OR tape activity if Audio In 1 patched
		//   LED 5: recent beeper activity (also the tape LOAD "" screech)
		uint8_t mode = gSpectrum.xc.mode;
		LedOn(0, mode == MODE_128K);
		LedOn(2, mode == MODE_48K);
		LedOn(4, mode == MODE_AY);

		static uint32_t lastAlive = 0, blink = 0;
		uint32_t alive = gSpectrum.xc.emuAlive;
		if (alive != lastAlive) { blink++; lastAlive = alive; }
		LedOn(1, (blink >> 11) & 1);

		static bool lastBeep = false;
		static uint32_t beepSeen = 0;
		if (gSpectrum.xc.beeper != lastBeep) { beepSeen = 24000; lastBeep = gSpectrum.xc.beeper; }
		if (beepSeen) beepSeen--;
		LedOn(5, beepSeen != 0);

		// LED 3 doubles as a TAPE ACTIVITY light while a jack is patched into
		// Audio In 1: it blinks whenever the EAR bit toggles (a tape signal is
		// being seen). Otherwise it shows "correct speed" (100%) as before.
		static bool lastEar = false;
		static uint32_t earSeen = 0;
		if (gSpectrum.xc.earIn != lastEar) { earSeen = 12000; lastEar = gSpectrum.xc.earIn; }
		if (earSeen) earSeen--;
		if (Connected(Input::Audio1))
			LedOn(3, (earSeen && (earSeen >> 10) & 1));  // blink = tape pulses seen
		else
			LedOn(3, gSpectrum.xc.speedPct == 100);      // else: correct-speed

		// Advance the shared time reference the emulation core paces against.
		gSpectrum.xc.sampleCount++;
	}
};

// OneBit alternate boot mode (onebit.cpp). Held momentary switch at power-on
// boots OneBit instead of the Spectrum.
void run_onebit();

// ---------------------------------------------------------------------------
// BootSelector: a tiny ComputerCard that just samples the switch at power-on so
// we can pick a boot mode. The switch is only readable once the ADC/audio worker
// is running, so we can't read it before Run(). This selector runs Run() briefly,
// records the switch position, then Abort()s (which returns from Run). main()
// then dispatches to the chosen card. Hold the momentary switch DOWN at power-on
// to boot OneBit; otherwise the ZX Spectrum boots.
// ---------------------------------------------------------------------------
class BootSelector : public ComputerCard
{
public:
	volatile bool onebit = false;
	virtual void ProcessSample()
	{
		// Let the switch reading settle (~150ms), then latch and abort.
		if (++count_ < 7200) return;              // ~150ms @48kHz
		onebit = (SwitchVal() == Switch::Down);   // held Down -> OneBit
		Abort();
	}
private:
	uint32_t count_ = 0;
};

int main()
{
	// Read the switch at power-on with a minimal card (the switch needs the ADC
	// running, so we can't read it before Run()).
	set_sys_clock_khz(144000, true);
	BootSelector sel;
	sel.Run();                    // returns when it Abort()s after latching

	if (sel.onebit)
	{
		run_onebit();             // never returns
	}

	// --- ZX Spectrum path -------------------------------------------------
	// Overclock for emulation headroom. 200MHz needs a small core-voltage bump;
	// 200MHz is not a multiple of 48MHz (mild audio-input noise tradeoff), but the
	// emulator needs the raw MHz more than pristine ADC noise here.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	set_sys_clock_khz(200000, true);

	ZXCard zx;
	zx.EnableNormalisationProbe();
	zx.Run();
}
