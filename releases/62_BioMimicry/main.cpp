// BioMimicry — organic rhythms for the Music Thing Workshop Computer.
//
// Five physics engines (Horses, Geese, Frogs, Rain, Meteors) drive four agents.
// The engines run at 1.5kHz; voices and outputs run at the full 48kHz.
//
// Hardware realities that shape the mapping (the Computer has 2 pulse outs, 2 CV
// outs and ONE three-position switch):
//   * Discrete routing puts agents 1-2 on the pulse outs and fires agents 3-4 as
//     5V blips on the CV outs, so all four get a physical trigger.
//   * The single switch does double duty: a Down tap cycles the mode, while the
//     Up/Middle position selects the routing.
//   * Holding Down at power-on selects the PCM voice bank instead of the synth.

#include "ComputerCard.h"
#include "biomimicry.h"
#include "engines.h"
#include "voices.h"
#include "samples_default.h"   // kHaveSamples
#include "samplestore.h"       // user flash region
#include "webui.h"             // USB-MIDI sample upload
#include "profile.h"           // -DBIO_PROFILE=ON: cycle-count ProcessSample
#include "crosscore.h"         // the only state shared between the cores
#include "pico/multicore.h"
#include "hardware/watchdog.h" // leaving USB mode reboots
#include "hardware/vreg.h"     // overvolt for the 192MHz clock
#include "pico/stdlib.h"       // set_sys_clock_khz

using namespace bio;

// Shared with core 1, which does nothing but service USB. Single writer per
// field (core 0 sets them once at the end of boot, core 1 only reads), so no
// lock is needed — the same discipline WorkshopZX uses for its CrossCore.
static volatile bool  gUsbReady = false;
static WebUI * volatile gWebUI = nullptr;
// Core 1 drives the progress LEDs during an upload, when core 0's audio
// interrupt is switched off and nothing else is left running.
class BioMimicryCard;
static BioMimicryCard * volatile gCard = nullptr;

// Gate width for the pulse outs and the CV-out trigger blips: 5ms at 48kHz is
// comfortably long enough for any drum module or envelope to register.
static constexpr int kGateSamples = 240;

// How long to sample the switch at startup before deciding the boot mode. The
// switch only becomes readable once the audio worker is running, so this window
// also covers the release of the hold.
static constexpr int kBootWindowSamples = 48000 / 2;   // ~0.5s

// After booting, announce the mode on the LEDs for a moment: Rhythm lights the
// left column, Tuned the right. Without this the only way to tell them apart is
// by ear, and the difference is subtle until you hear a pitched sample held.
static constexpr int kSplashSamples = 48000;           // ~1s

// How long the momentary switch must be held to hand the card over to USB.
// Two seconds is long enough that no tap reaches it by accident, and the LEDs
// fill as it counts so the gesture is visible rather than guessed at.
// Measured in control ticks, because that is where the switch is sampled.
static constexpr int32_t kHoldTicks = 2 * kCtrlRate;   // ~2s at 1.5kHz

class BioMimicryCard : public ComputerCard
{
public:
	/// Progress bar during an upload, called from core 1 because core 0's audio
	/// interrupt is switched off by then. RAM-resident: this runs while flash is
	/// being erased, and LedOn() only pokes a PWM register, so it is safe there.
	void __not_in_flash_func(showUploadProgress)()
	{
		// The LEDs show WebUI::stage as a binary count, not a progress bar.
		// A hang in the upload path also stops USB, so the card cannot report it
		// over the wire -- whatever number is frozen here is the last step that
		// completed, which is the only diagnostic available from outside.
		uint8_t st = WebUI::stage;
		for (int i = 0; i < 6; i++)
			LedOn(static_cast<uint32_t>(i), (st >> i) & 1);
	}

	BioMimicryCard()
	{
		engines_[0] = &horses_;
		engines_[1] = &geese_;
		engines_[2] = &frogs_;
		engines_[3] = &rain_;
		engines_[4] = &meteors_;
		engines_[5] = &cicadas_;
		// USB runs on core 1. tud_task() is TinyUSB's whole device stack and is
		// unbounded by design — measured at up to 36000 cycles, i.e. 14x the
		// entire 20.8us audio budget. Calling that from inside the audio
		// interrupt was always wrong; it only looked survivable because it is
		// intermittent, and every occurrence drops samples.
		multicore_launch_core1(core1Entry);
	}

	/// Core 1: idle until the switch is held, then nothing but USB. It must not
	/// touch anything core 0 owns.
	///
	/// v1.1.0 will put the physics here — the idle wait below is the space that
	/// makes that possible, and the reason USB had to become modal first.
	static void __not_in_flash_func(core1Entry)()
	{
		// Wait for core 0 to finish booting and publish the globals.
		while (!gUsbReady) tight_loop_contents();

		// USB does not exist until the switch is held. Idle here until it is.
		//
		// This is the point of the modal design: with TinyUSB uninitialised the
		// card does not enumerate, USBCTRL_IRQ (whose handler lives in flash) is
		// never armed, and nothing on this core competes with the audio path
		// while the card is being played.
		//
		// The loop also exercises the pacing the physics will use, so the
		// mechanism is proven before anything depends on it. maxBacklog is how
		// many control ticks core 1 owed at once: 1 is healthy, a steady 2+ means
		// it cannot keep up and the physics would be running in slow motion.
		{
			uint32_t lastSample = gXC.sampleCount;
			while (!WebUI::usbMode)
			{
				uint32_t elapsed = gXC.sampleCount - lastSample;
				if (elapsed < static_cast<uint32_t>(kCtrlDiv)) { tight_loop_contents(); continue; }
				uint32_t owed = elapsed >> 5;          // kCtrlDiv is 32: a shift
				if (owed > gXC.maxBacklog) gXC.maxBacklog = owed;
				if (owed > 4) owed = 4;                // slow motion, not a burst
				// Anchored, NOT `= gXC.sampleCount`: keeps the tick grid on the
				// 1.5kHz phase instead of letting it drift after a stall.
				lastSample += owed * kCtrlDiv;

				// The physics, for BOTH boot modes. Alt boot used to run its
				// whole tick inline on core 0 because it needed a different
				// renderer; now it is the same ecosystem with different output
				// routing, so it takes the same path.
				for (uint32_t n = 0; n < owed; n++) gCard->physicsTick();
			}
		}

		// Held: bring USB up, on this core, and stay here.
		gWebUI->Init();
		for (;;)
		{
			gWebUI->Task();

			// Once an upload starts, core 0's audio interrupt is off and this is
			// the only core still running — so the progress LEDs have to be
			// driven from here. LedOn() is RAM-resident and only writes a PWM
			// register, so it stays safe even mid-erase.
			if (WebUI::uploadMode && gCard) gCard->showUploadProgress();
		}
	}

	virtual void __not_in_flash_func(ProcessSample)()
	{
		// An upload is starting: park HERE, in RAM, and never return to flash.
		//
		// Masking DMA_IRQ_0 from core 1 is not sufficient on its own, which is
		// what the first two attempts got wrong. ComputerCard::AudioCallback,
		// BufferFull and AudioWorker's outer loop all live in flash, and the CV
		// output runs a second flash-resident ISR (PWM_IRQ_WRAP -> OnCVPWMWrap).
		// Any of them executing when flash_range_erase drops XIP is a hard fault.
		//
		// So core 0 stops inside this function, which IS RAM-resident, mutes the
		// outputs, tells core 1 it has arrived, and spins until the reboot. It
		// never returns, so the flash-resident caller never runs again.
		//
		// This is safe now in a way it was not before: USB lives on core 1, so
		// spinning core 0 no longer stalls the upload it is waiting for.
		if (WebUI::uploadMode)
		{
			AudioOut1(0);
			AudioOut2(0);
			PulseOut1(false);
			PulseOut2(false);
			WebUI::core0Parked = true;
			for (;;) tight_loop_contents();
		}

		// Times the WHOLE callback, which is the number that decides whether
		// audio glitches — not the per-sample average.
		BIO_PROFILE_SCOPE(Total);

		// ---- Boot window ------------------------------------------------
		// Decide the voice backend before any normal switch handling, so the
		// power-on hold is never mistaken for a mode-cycle tap.
		if (bootPhase_ < kBootWindowSamples)
		{
			// The switch is NOT readable straight away, and reads as Down until
			// it settles. ComputerCard derives it from knobs[3], which comes off
			// a ~60Hz smoothing filter starting at zero — and zero decodes as
			// Down. So for the first few milliseconds of every boot the card
			// reports Down wherever the switch actually is.
			//
			// The old code latched on "Down seen at any point in the window",
			// which therefore latched on EVERY boot: both modes came up as
			// Drone. Take a single reading once settled instead, which is what
			// WorkshopZX's BootSelector does (see its main.cpp) and what is
			// proven on this hardware.
			if (++bootPhase_ == kBootWindowSamples)
			{
				// Holding the switch at power-on picks DRONE, a different
				// instrument built from the same six engines. PCM sample
				// playback is a build-time choice now (bake samples/ or don't),
				// not something worth spending a whole boot mode on.
				boot_ = (SwitchVal() == Switch::Down) ? BootMode::Drone
				                                     : BootMode::Rhythm;
				// NOT webui_.Init() here any more — TinyUSB is not started
				// until the switch is held. See core1Entry.
				gXC.bootMode = static_cast<uint8_t>(boot_);
				gXC.mode     = mode_;
				gXC.routing  = static_cast<uint8_t>(routing_);
				gXC.population = static_cast<uint8_t>(population_);
				gCard  = this;
				gWebUI = &webui_;
				gUsbReady = true;      // releases core 1
				// Alt boot plays samples at their recorded pitch: no per-agent
				// body size, no per-hit wobble. Rhythm is an ecosystem and wants
				// the humanisation; alt mode is an instrument and does not.
				voices_.init(AnySamples(), boot_ == BootMode::Drone);
				engines_[mode_]->reset(0xB10Du);
				// Swallow the release of the boot hold, so booting into Drone
				// does not also read as a tap and cycle straight off Horses.
				// downTicks_ is additionally gated on splash_ in controlTick.
				holdFired_ = (SwitchVal() == Switch::Down);
				// Announce what booted, because otherwise the only way to know
				// is by ear — and a Drone patch can be mistaken for broken
				// samples. See the splash pattern in ProcessSample.
				splash_ = kSplashSamples;
			}
			return;
		}

		// USB is serviced on core 1; nothing to do here. See core1Entry.
		//
		// Uploads are NOT handled here any more. This handler stops being called
		// at all once an upload starts (DMA_IRQ_0 is disabled), so the progress
		// LEDs are driven from core 1 — see core1Entry. A progress bar rendered
		// here would simply freeze on the first frame.

		// ---- Boot splash -------------------------------------------------
		// LEDs are 0 1 / 2 3 / 4 5. Rhythm lights the left column, Drone the
		// right, so which instrument you are in is visible on power-up.
		if (splash_ > 0)
		{
			if (--splash_ == 0)
			{
				for (int i = 0; i < 6; i++) LedOff(i);
				// Start measuring only now: the boot-time voices_.init() memset
				// is a one-off spike and would otherwise latch the overrun
				// indicator permanently.
				BIO_PROFILE_ARM();
			}
			else if (splash_ == kSplashSamples - 1)
			{
				bool drone = (boot_ == BootMode::Drone);
				for (int i = 0; i < 6; i++)
					LedOn(i, ((i & 1) == 1) == drone);
			}
		}

		// ---- Control tick (1.5kHz) --------------------------------------
		// NOTE: this runs INLINE, inside the DMA interrupt. Dividing by
		// kCtrlDiv lowers the average load but does NOT relax the deadline —
		// on the sample where it fires, the whole engine must still finish
		// within this one 20.83us slot. Build with -DBIO_PROFILE=ON to see
		// what that actually costs.
		// The engine and the LED/UI housekeeping used to run on the SAME sample,
		// stacking their costs into one peak. They are now offset by half the
		// divider, so each gets its own sample. Same rates, lower worst case.
		if (++ctrlDiv_ >= kCtrlDiv) ctrlDiv_ = 0;
		if (ctrlDiv_ == 0)
		{
			BIO_PROFILE_SCOPE(Ctrl);
			controlTick();
		}
		else if (ctrlDiv_ == kCtrlDiv / 2)
		{
			BIO_PROFILE_SCOPE(Outputs);
			uiTick();
		}
		else
		{
			// ONE queued note-on per sample, on the samples that have nothing
			// else to do. note() is ~300 instructions and a full Geese tick fires
			// four at once, so draining them together put ~1200 instructions on a
			// single sample — the whole cost the split was meant to remove, just
			// moved rather than spread. The ring already decouples when a note is
			// consumed from when it was produced, so there is no reason to take
			// them all in one slot. 30 free samples per control period against a
			// worst case of 4 notes is ample.
			BIO_PROFILE_SCOPE(Notes);
			drainOneTrigger();
		}

		// Core 1's clock. Everything it does is paced off this rather than
		// free-running, so the 1.5kHz control rate stays anchored to audio time.
		// Published now, unused until the physics move across.
		//
		// This is the ONLY thing published at 48kHz. switchMirror started here
		// too and cost real cycles for nothing: core 1 reads it at the control
		// rate, so publishing it 32x more often than it can be consumed is pure
		// waste in the hottest loop on the card. It lives in controlTick now.
		gXC.sampleCount++;

		// ---- Audio (48kHz) ----------------------------------------------
		int16_t l, r;
		{
			BIO_PROFILE_SCOPE(Voices);
			voices_.render(population_, l, r);
		}
		AudioOut1(l);
		AudioOut2(r);

		{
			BIO_PROFILE_SCOPE(Outputs);
			// ---- Listening (48kHz) --------------------------------------
			listen();

			// ---- Gate/blip timers (48kHz) -------------------------------
			serviceOutputs();
		}

#ifdef BIO_PROFILE
		profileReadout();
#endif
	}

private:
#ifdef BIO_PROFILE
	// -------------------------------------------------------------------
	// The first hardware run flashed all six LEDs in every mode, i.e. something
	// overruns everywhere. "Everywhere" points at shared cost rather than any
	// one engine, so the readout now CYCLES through the buckets and names the
	// culprit instead of only saying that one exists.
	//
	// Each bucket gets ~1.5s. Its identity is shown on the LEFT column
	// (LEDs 0/2/4) as a 3-bit number, and its peak as a fraction of budget on
	// the RIGHT column (LEDs 1/3/5), one LED per third. A bucket that alone
	// exceeds budget flashes its right column.
	//
	//   left 000 = Total    001 = Engine   010 = Voices
	//        011 = Outputs  100 = Usb
	void profileReadout()
	{
		if (++profDiv_ < 700) return;       // ~68Hz update
		profDiv_ = 0;

		if (++profPhase_ >= 100)            // ~1.5s per bucket
		{
			profPhase_ = 0;
			profBucket_ = (profBucket_ + 1) % kNumProf;
		}
		profFlash_ = !profFlash_;

		const ProfStat &s = gProf[profBucket_];
		uint32_t frac = static_cast<uint32_t>(
			static_cast<uint64_t>(s.peak) * 65536u / kCycleBudget);

		// Left column: which bucket, in binary.
		LedOn(0, (profBucket_ & 1) != 0);
		LedOn(2, (profBucket_ & 2) != 0);
		LedOn(4, (profBucket_ & 4) != 0);

		// Right column: that bucket's peak, a third of the budget per LED.
		if (frac >= 65536)
		{
			// Over budget on its own — flash so it is unmistakable.
			LedOn(1, profFlash_); LedOn(3, profFlash_); LedOn(5, profFlash_);
		}
		else
		{
			int lit = static_cast<int>((frac * 3) >> 16);
			LedOn(1, lit > 0);
			LedOn(3, lit > 1);
			LedOn(5, lit > 2);
		}

		// The peaks used to be cleared by a Down tap, which now cycles the mode —
		// the two conflicted, so a tap both changed the ecosystem and threw away
		// the numbers for it. Entering USB mode resets them instead
		// (see enterUsbMode), which is the moment you stop playing and go to read
		// them anyway.
	}

	int  profDiv_ = 0;
	int  profPhase_ = 0;
	int  profBucket_ = 0;
	bool profFlash_ = false;

	// PulseOut2 is protected, so the gate hook goes through a static trampoline
	// holding the one card instance. Only compiled in profile builds, and it
	// takes over Pulse Out 2 — do not release a build with this on.
	static BioMimicryCard *profCard_;
	static void ProfGate(bool on) { if (profCard_) profCard_->PulseOut2(on); }
public:
	void EnableProfileGate() { profCard_ = this; bio::gProfGate = &ProfGate; }
private:
#endif

	// -------------------------------------------------------------------
	void __not_in_flash_func(controlTick)()
	{
		// --- Switch: tap cycles the mode, HOLD enters USB mode. ---
		//
		// The mode change fires on RELEASE, not on press. It used to fire on the
		// first tick Down was seen, which cannot coexist with a hold: beginning
		// the hold would also cycle the ecosystem out from under you. Releasing
		// before the hold completes is a tap; holding past it is consumed.
		Switch sw = SwitchVal();
		// Republished for core 1, which must never call SwitchVal() itself:
		// ComputerCard returns a member that is not declared volatile, so core 1
		// would cache it forever and the switch would appear to stop working.
		gXC.switchMirror = static_cast<uint8_t>(sw);

		if (sw == Switch::Down)
		{
			// Do not start counting until the boot splash has finished AND the
			// switch has been released at least once. Without the second
			// condition, holding at power-on to pick Drone and simply not letting
			// go fast enough would drop you straight into USB mode ~3.5s later —
			// booting an instrument should never hand the card to the uploader.
			if (splash_ == 0 && !holdFired_ && downTicks_ < kHoldTicks) downTicks_++;

			if (downTicks_ == kHoldTicks && !holdFired_)
			{
				holdFired_ = true;
				if (WebUI::usbMode)
				{
					// Already in USB mode: hold again to leave. Entering by
					// accident mid-set and being told to power-cycle is not an
					// acceptable answer, and a reboot is the honest way out —
					// TinyUSB cannot be cleanly unwound, and the card reboots
					// after a successful upload anyway, so the path is proven.
					// Any uploaded samples are already committed to flash.
					leaveUsbMode();
				}
				else
				{
					// Hand the card over to USB, and consume the tap.
					enterUsbMode();
				}
			}
		}
		else
		{
			if (downTicks_ > 0 && !holdFired_)
			{
				// A short tap, now released: next ecosystem. Core 1 owns mode_
				// and the engine instances, so this is a REQUEST rather than the
				// change itself — same monotonic-counter discipline as the edges.
				gXC.modeCycleReq = gXC.modeCycleReq + 1;
				clearOutputs();

				// Clear the peaks with the mode change, so a reading always
				// describes the ecosystem you actually played rather than the
				// worst of every mode you passed through on the way to it.
				// The old Down-tap reset did this, and dropping it in the switch
				// rework left no way to measure one mode without a power cycle.
				BIO_PROFILE_RESET();
			}
			downTicks_ = 0;
			holdFired_ = false;
			routing_ = (sw == Switch::Up) ? Routing::Discrete : Routing::Summed;
			gXC.routing = static_cast<uint8_t>(routing_);
		}

		// In USB mode the ecosystem is stopped: no physics, no new notes, no
		// gates. The switch above is still read (that is the whole reason this
		// function keeps running) and the voices are left to decay on their own.
		if (WebUI::usbMode) return;

		// The physics used to run here, inline, inside the DMA interrupt. They
		// are on core 1 now — see physicsTick(). Core 0 keeps only what needs the
		// 48kHz deadline or the hardware core 0 owns:
		//   - the switch, above (ComputerCard's switchVal is not volatile)
		//   - draining the trigger ring into voices_.note(), below
		//   - applying the gate/CV targets core 1 published
		// Everything else crosses through gXC and gTrig.
		//
		// Note-ons are drained one per sample elsewhere, so this tick only
		// applies the gate and CV decisions core 1 made.
		applyOutputTargets();
	}

private:
	// -------------------------------------------------------------------
	/// Turn queued note-ons into voices. Runs on core 0 at the control rate.
	///
	/// note() writes ~20 fields of Voice including a 128-entry ks[] buffer, and
	/// render() reads and mutates the same struct every sample — so this cannot
	/// move to core 1 whatever the timing says. The ring is what crosses instead.
	void __not_in_flash_func(drainOneTrigger)()
	{
		if (gTrig.tail == gTrig.head) return;

		TrigWord w = gTrig.slot[gTrig.tail & (kTrigRingSize - 1)];
		gTrig.tail = gTrig.tail + 1;
		voices_.note(TrigAgent(w), static_cast<Mode>(TrigMode(w)), kQ16One,
		             TrigVariation(w), static_cast<uint8_t>(TrigMember(w)));
	}

	// -------------------------------------------------------------------
	/// Copy the gate and CV decisions core 1 made onto core 0's own timers.
	void __not_in_flash_func(applyOutputTargets)()
	{
		population_ = gXC.population;
		routing_    = static_cast<Routing>(gXC.routing);

		uint32_t seq = gXC.pulseSeq;
		if (seq != lastPulseSeq_)
		{
			lastPulseSeq_ = seq;
			uint8_t pa = gXC.pulseArm, ca = gXC.cvTrigArm;
			if (pa & 1) pulseTimer_[0] = kGateSamples;
			if (pa & 2) pulseTimer_[1] = kGateSamples;
			if (ca & 1) cvTimer_[0]    = kGateSamples;
			if (ca & 2) cvTimer_[1]    = kGateSamples;
		}
		cvLevel_[0] = gXC.cvTarget[0];
		cvLevel_[1] = gXC.cvTarget[1];

		uint32_t act = gXC.activitySeq;
		if (act != lastActivitySeq_) { lastActivitySeq_ = act; activity_ = kQ16One; }
	}

	// -------------------------------------------------------------------
	/// One physics tick. Runs on CORE 1, paced off core 0's sample counter.
	///
	/// Core 1 must never call any ComputerCard accessor except the volatile ones
	/// (KnobVal, CVIn1/2, Connected). SwitchVal() in particular returns a member
	/// that is NOT declared volatile, so core 1 would cache it forever and the
	/// switch would appear to stop working — read gXC.switchMirror instead.
public:
	void __not_in_flash_func(physicsTick)()
	{
		// Mode changes are requested by core 0 (it owns the switch) and performed
		// here, because this core owns mode_ and the engine instances.
		uint32_t mc = gXC.modeCycleReq;
		if (mc != lastModeReq_)
		{
			lastModeReq_ = mc;
			mode_ = static_cast<uint8_t>((mode_ + 1) % kNumModes);
			engines_[mode_]->reset(seed_ ^ (mode_ * 2654435761u));
			gXC.mode = mode_;
		}

		// --- Knobs, with CV modulating Main and X. ---
		int32_t physics = knob_to_q16(KnobVal(Knob::Main));
		if (Connected(Input::CV1)) physics += CVIn1() << 4;
		physics = clampQ16(physics);

		int32_t popRaw = knob_to_q16(KnobVal(Knob::X));
		if (Connected(Input::CV2)) popRaw += CVIn2() << 4;
		popRaw = clampQ16(popRaw);

		// Edges arrive as monotonic counters that only core 0 writes, compared
		// against core-1-private last-seen values. A bool set at 48kHz and
		// cleared at 1.5kHz would be a lost-update race across cores — and it
		// already collapsed two edges inside one tick into one.
		uint32_t sSeq = gXC.spookSeq, cSeq = gXC.clockSeq, tSeq = gXC.startleSeq;
		bool spookEdge   = (sSeq != lastSpookSeq_)   || (tSeq != lastStartleSeq_);
		bool clockEdge   = (cSeq != lastClockSeq_);
		lastSpookSeq_ = sSeq; lastClockSeq_ = cSeq; lastStartleSeq_ = tSeq;

		// Clock tracking for Pulse In 2. The period between edges lets the
		// engines entrain to an external tempo rather than merely be nudged by
		// it. It ages out after ~3s of silence so a stopped clock releases the
		// ecosystem back to its own timing instead of freezing it.
		if (clockAge_ < 3 * kCtrlRate) clockAge_++;
		else clockPeriod_ = 0;

		if (clockEdge)
		{
			if (clockAge_ >= 2 && clockAge_ < 3 * kCtrlRate) clockPeriod_ = clockAge_;
			clockAge_ = 0;
		}

		Ctrl c;
		c.physics     = physics;
		c.chaos       = knob_to_q16(KnobVal(Knob::Y));
		c.population  = 1 + (popRaw * kNumAgents - 1) / kQ16One;
		if (c.population < 1) c.population = 1;
		if (c.population > kNumAgents) c.population = kNumAgents;
		// A sharp transient on Audio In 2 counts as a spook, so the ecosystem
		// reacts to the rest of the patch and not only to a patched gate.
		c.spook       = spookEdge;
		c.clock       = clockEdge;
		c.clockPeriod = clockPeriod_;
		c.loudness    = gXC.loudness;
		gXC.population = static_cast<uint8_t>(c.population);

		// --- Physics. ---
		EngineOut out;
		out.triggers = 0;
		out.global = 0;
		for (int i = 0; i < kNumAgents; i++) { out.state[i] = 0; out.member[i] = 0; }
		engines_[mode_]->tick(c, out);

		// Agents beyond the population never sound or fire.
		uint8_t mask = out.triggers & static_cast<uint8_t>((1 << c.population) - 1);

		// --- Voices. ---
		//
		// Note-ons cross to core 0 as packed words rather than as calls. note()
		// writes ~20 fields of Voice including a 128-entry ks[] buffer while
		// render() mutates the same struct every sample, so calling it from here
		// would tear the struct — a half-applied note-on gives a new pcm pointer
		// with an old length, which is an out-of-bounds read, not a glitch.
		for (int i = 0; i < kNumAgents; i++)
		{
			if (!(mask & (1 << i))) continue;

			uint32_t h = gTrig.head;
			uint32_t n = h - gTrig.tail;
			if (n >= kTrigRingSize)
			{
				// Ring full means core 0 is not draining, i.e. still overrunning.
				// Drop the NEWEST: the ones already queued are committed to
				// sound, and truncating a hit mid-attack is worse than losing
				// the latest. A non-zero count here says the split is not working.
				gTrig.dropped++;
				continue;
			}
			gTrig.slot[h & (kTrigRingSize - 1)] =
				PackTrig(i, mode_, out.member[i], out.state[i]);
			// Payload stored before head moves, so core 0 can never see a slot
			// that is not finished. The barrier makes that ordering explicit
			// rather than relying on the M0+ not reordering it.
			__dmb();
			gTrig.head = h + 1;
		}

		// Smoothed trigger density for CV 1. Computed BEFORE the routing below,
		// which reads it — it used to be updated forty lines later, so CV 1
		// carried the PREVIOUS tick's value and read zero on the first one. Each firing agent pushes it up;
		// it leaks away continuously, so a busy patch sits high and a sparse one
		// hovers near zero. Shift 7 at the 1.5kHz control rate is a time constant
		// of about 85ms — fast enough to follow a cascade, slow enough that it
		// reads as a control voltage rather than a stream of blips.
		int32_t d = density_;
		d += (popcount4(mask) * (kQ16One / 4) - d) >> 7;
		if (d < 0) d = 0;
		if (d > kQ16One) d = kQ16One;
		density_ = d;
		gXC.density = d;

		// The same envelope per agent, for alt boot's Discrete routing: each
		// pulse out gets a CV describing how busy THAT voice is.
		for (int a = 0; a < kNumAgents; a++)
		{
			int32_t da = densityAgent_[a];
			da += (((mask >> a) & 1) * kQ16One - da) >> 7;
			if (da < 0) da = 0;
			if (da > kQ16One) da = kQ16One;
			densityAgent_[a] = da;
			gXC.densityAgent[a] = da;
		}

		// --- Trigger outputs. ---
		//
		// Decided here, applied by core 0: this core does not own the gate
		// timers, which are serviced at 48kHz. pulseSeq is what tells core 0
		// there is something new to arm.
		uint8_t pulseArm = 0, cvArm = 0;
		// CV 2 carries a pitch in alt boot Summed, and a pitch CV must STEP
		// between values rather than glide, so the usual slew is bypassed.
		bool stepCv1 = false;
		// Seeded from what was last published, not from core 0's cvLevel_ — this
		// core does not own that. Discrete routing leaves them alone, so they
		// keep whatever the last Summed pass set.
		int32_t cv0 = gXC.cvTarget[0], cv1 = gXC.cvTarget[1];

		Routing routing = static_cast<Routing>(gXC.routing);

		if (gXC.bootMode == static_cast<uint8_t>(BootMode::Drone))
		{
			// ALT BOOT: the same ecosystems, but as an INSTRUMENT rather than a
			// menagerie. Samples play at their recorded pitch (see VoiceBank's
			// `tuned`), so uploading four notes lets Horses gallop them and Rain
			// drip them — and the control outputs describe the SEQUENCE rather
			// than the ecology.
			//
			// Both CV outs are continuous in both switch positions. A mode meant
			// for pitched material should not turn half its control outputs into
			// 5V blips.
			if (routing == Routing::Discrete)
			{
				// Two independent voices, each with its own trigger and its own
				// density: agent 1 on Pulse 1 with its density on CV 1, agent 2
				// on Pulse 2 with its density on CV 2.
				if (mask & 0b0001) pulseArm |= 1;
				if (mask & 0b0010) pulseArm |= 2;
				cv0 = gXC.densityAgent[0];
				cv1 = gXC.densityAgent[1];
			}
			else
			{
				// Everything on Pulse 1, and Pulse 2 is that DIVIDED BY FOUR — a
				// bar line against the beat, rather than the cluster accent
				// Rhythm uses. Free downbeat for anything that wants one.
				if (mask)
				{
					pulseArm |= 1;
					if (++pulseDiv_ >= 4) { pulseDiv_ = 0; pulseArm |= 2; }
				}
				cv0 = gXC.density;

				// CV 2 = WHICH SAMPLE just fired, as 1V/oct: one semitone per
				// round-robin slot, so slot 0 is 0V and slot 7 is 583mV. An
				// external oscillator tracks the sequence the ecosystem is
				// playing.
				int slot = voices_.lastSlot();
				cv1 = (slot >= 0)
					? static_cast<int32_t>((slot * 1000 * kQ16One) / (12 * 5000))
					: 0;
				stepCv1 = true;                 // no slew: a pitch CV must step
			}
		}
		else if (routing == Routing::Discrete)
		{
			if (mask & 0b0001) pulseArm |= 1;
			if (mask & 0b0010) pulseArm |= 2;
			if (mask & 0b0100) cvArm    |= 1;
			if (mask & 0b1000) cvArm    |= 2;
		}
		else
		{
			// Summed: everything OR'd onto Pulse 1; Pulse 2 marks a "cluster"
			// (two or more agents firing at once) as an accent.
			if (mask) pulseArm |= 1;
			if (popcount4(mask) >= 2) pulseArm |= 2;

			// CV outs carry continuous state instead of triggers.
			cv0 = out.state[0];
			cv1 = out.global;
		}

		gXC.cvTarget[0] = cv0;
		gXC.cvTarget[1] = cv1;
		gXC.cvStep = stepCv1 ? 2 : 0;   // bit1: CV 2 is a stepped pitch
		if (pulseArm || cvArm)
		{
			gXC.pulseArm   = pulseArm;
			gXC.cvTrigArm  = cvArm;
			__dmb();                 // arms visible before the sequence bumps
			gXC.pulseSeq   = gXC.pulseSeq + 1;
		}

		// Triggers feed the activity glow. Published as a counter rather than a
		// level: core 0's uiTick owns the decay, so nothing shares a mutable
		// brightness between the cores.
		if (mask) gXC.activitySeq = gXC.activitySeq + 1;

		seed_ = seed_ * 1664525u + 1013904223u;
	}

	// -------------------------------------------------------------------
	// LED housekeeping, run on a different sample from the engine so their
	// costs never land in the same 20.8us slot.
	void __not_in_flash_func(uiTick)()
	{
		activity_ = fast_exp_decay(activity_, 3);

		// Leave the LEDs alone while the boot splash is still showing.
		if (splash_ != 0) return;

		// Holding the switch fills the LEDs left to right, so the two-second
		// hold is something you watch arrive rather than count in your head.
		// Releasing before the fill completes is a tap and cycles the mode.
		//
		// This is checked BEFORE the usbMode branch below, so the same fill also
		// shows the hold that LEAVES usb mode — the way out looks like the way in.
		if (downTicks_ > 0)
		{
			int lit = static_cast<int>((downTicks_ * 6) / kHoldTicks);
			for (int i = 0; i < 6; i++) LedOn(i, i < lit);
			return;
		}

		// In USB mode all six stay lit: the card is not performing. Hold again
		// to leave. An upload overrides this with its own stage count, driven
		// from core 1.
		if (WebUI::usbMode)
		{
			for (int i = 0; i < 6; i++) LedOn(i, true);
			return;
		}

		// Six modes on six LEDs leaves none spare for an activity indicator, so
		// the two jobs share: the mode's own LED sits at a dim "you are here"
		// glow and flares to full on every trigger. One light, both meanings.
		for (int i = 0; i < kNumModes; i++)
		{
			if (i == mode_)
			{
				constexpr int32_t kIdleGlow = kQ16One / 5;
				int32_t level = kIdleGlow
				              + mul_q16(activity_, kQ16One - kIdleGlow);
				LedBrightness(i, static_cast<uint16_t>(level >> 4));
			}
			else
			{
				LedOff(i);
			}
		}
	}

	// -------------------------------------------------------------------
	// 48kHz: the ecosystem listens to the rest of the patch.
	//
	// Audio In 1 is LOUDNESS — a fast envelope follower. A loud room is a
	// disturbed one: it spooks the geese, silences the cicadas and startles the
	// herd. Audio In 2 is DISTURBANCE — the same signal differentiated, so it
	// responds to transients rather than level, which is what actually alarms an
	// animal. Both only act when something is patched in.
	void __not_in_flash_func(listen)()
	{
		if (Connected(Input::Audio1))
		{
			int32_t a = AudioIn1();
			if (a < 0) a = -a;
			// Fast attack, slow release: an envelope that tracks how alive the
			// room is rather than the waveform itself.
			// << 5, not << 4. Audio in is signed 12-bit, so |a| tops out at 2048
			// and a << 4 could only ever reach half of Q16 - a full-scale signal
			// read as "half loud" and nothing could reach the top of the range.
			if (a > loudness_) loudness_ = slew(loudness_, a << 5, 3);
			else               loudness_ = slew(loudness_, a << 5, 9);
			if (loudness_ > kQ16One) loudness_ = kQ16One;
		}
		else loudness_ = 0;

		// PUBLISH IT. The engines read gXC.loudness, and until this line nothing
		// ever wrote it - so Audio In 1 did nothing at all in every mode, however
		// hot the signal. listen() stayed on core 0 when the physics moved to
		// core 1 (Stage 3), and the value was simply never carried across; the
		// field was declared in crosscore.h with core 0 named as its writer, and
		// the writer was never written.
		//
		// Audio In 2 survived the same split because it publishes an edge counter
		// inline below, which is why Disturb worked and Loudness did not.
		gXC.loudness = loudness_;

		if (Connected(Input::Audio2))
		{
			int32_t a = AudioIn2();
			int32_t d = a - lastAudio2_;
			lastAudio2_ = a;
			if (d < 0) d = -d;
			// A sharp transient arms a startle that the control tick consumes.
			if ((d << 4) > kQ16One / 3) { startle_ = true; gXC.startleSeq++; }
		}
		else lastAudio2_ = 0;
	}

	// -------------------------------------------------------------------
	// 48kHz: run down the gate widths and hold the CV outs.
	void __not_in_flash_func(serviceOutputs)()
	{
		// Pulse ins are edge-detected at audio rate so a short trigger is never
		// missed between control ticks.
		//
		// The seq counters are the cross-core form of the same thing, published
		// alongside the flags until the physics actually move to core 1. A
		// counter cannot lose an edge the way a bool can, so two pulses inside
		// one control tick stop collapsing into one.
		if (PulseIn1RisingEdge()) { spookPending_ = true; gXC.spookSeq++; }
		if (PulseIn2RisingEdge()) { clockPending_ = true; gXC.clockSeq++; }

		PulseOut1(pulseTimer_[0] > 0);
		PulseOut2(pulseTimer_[1] > 0);
		if (pulseTimer_[0] > 0) pulseTimer_[0]--;
		if (pulseTimer_[1] > 0) pulseTimer_[1]--;

		for (int i = 0; i < 2; i++)
		{
			// Only Rhythm's Discrete routing uses the CV outs as triggers
			// (agents 3 and 4, which have no pulse out of their own). Drone
			// always carries continuous density instead, in both positions.
			bool cvAsTrigger = (boot_ == BootMode::Rhythm)
			                && (routing_ == Routing::Discrete);
			// CVOutMillivolts calls MillivoltsToDAC, which is FLASH-RESIDENT in
			// the vendored ComputerCard. Calling it twice per sample at 48kHz
			// meant two XIP reads in the hot loop while core 1 contends for the
			// same bus. It is only called now when the value actually changes —
			// the CV outs move at control rate, so the vast majority of samples
			// were re-sending a number the DAC already had.
			int32_t mv;
			if (cvAsTrigger)
			{
				// CV out as a trigger: a calibrated 5V blip.
				mv = cvTimer_[i] > 0 ? 5000 : 0;
				if (cvTimer_[i] > 0) cvTimer_[i]--;
			}
			else
			{
				// CV out as continuous state, 0-5V. Slewed a little so stepped
				// control-rate updates don't click.
				// Stepped outputs bypass the slew entirely: gliding between
				// semitones turns a sequence into portamento.
				if (gXC.cvStep & (1u << i)) cvSmooth_[i] = cvLevel_[i];
				else cvSmooth_[i] = slew(cvSmooth_[i], cvLevel_[i], 4);
				mv = (cvSmooth_[i] * 5000) >> 16;
			}
			if (mv != cvLastMv_[i])
			{
				cvLastMv_[i] = mv;
				CVOutMillivolts(i, mv);
			}
		}
	}

	void clearOutputs()
	{
		pulseTimer_[0] = pulseTimer_[1] = 0;
		cvTimer_[0] = cvTimer_[1] = 0;
		cvLevel_[0] = cvLevel_[1] = 0;
	}

	/// Hand the card over to USB, for sample management. One-way: leaving costs
	/// a power cycle, which is the honest trade for not running TinyUSB at all
	/// while the card is being played.
	///
	/// The ecosystem stops here, but the VOICES are deliberately left alone so
	/// whatever is sounding decays naturally instead of being cut off. Rhythm
	/// voices fall away on their own envelopes and Drone grains run off the end
	/// of their samples, so the card goes quiet by itself within a second or two.
	void __not_in_flash_func(enterUsbMode)()
	{
		clearOutputs();

		// Deliberately NOT resetting the profiler here. Entering USB mode is
		// exactly the moment you stop playing in order to go and read the
		// numbers, so wiping them on entry would guarantee a reading of zero.
		// They stay sticky until MSG_PROF_GET replies, which is what the web UI
		// tells the user: play the mode, then hold, connect and read.
		WebUI::usbMode = true; // core 1 picks this up and calls tusb_init()
	}

	/// Leave USB mode, by rebooting.
	///
	/// Reached by holding the switch again, so entering by accident costs two
	/// seconds rather than a trip to the power switch. A reboot rather than an
	/// unwind: TinyUSB has no reliable teardown, the sample pointers the engine
	/// cached may be stale after an upload, and a restart resolves everything
	/// cleanly at boot — which is what an upload already does.
	void __not_in_flash_func(leaveUsbMode)()
	{
		AudioOut1(0);
		AudioOut2(0);
		PulseOut1(false);
		PulseOut2(false);
		watchdog_reboot(0, 0, 0);
	}

	static int32_t clampQ16(int32_t v)
	{
		if (v < 0) return 0;
		if (v > kQ16One) return kQ16One;
		return v;
	}

	static int popcount4(uint8_t m)
	{
		return ((m >> 0) & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1);
	}

	// -------------------------------------------------------------------
	HorsesEngine  horses_;
	GeeseEngine   geese_;
	FrogsEngine   frogs_;
	RainEngine    rain_;
	MeteorsEngine meteors_;
	CicadasEngine cicadas_;
	Engine       *engines_[kNumModes];

	VoiceBank voices_;
	WebUI     webui_;

	uint8_t  mode_       = 0;
	Routing  routing_    = Routing::Discrete;
	int      population_ = 1;

	int      ctrlDiv_    = 0;
	// Switch hold tracking. downTicks_ counts control ticks the switch has been
	// held Down; holdFired_ marks that the hold already did its job, so the
	// release is not also read as a tap.
	int32_t  downTicks_  = 0;
	bool     holdFired_  = false;

	// Core-1-private last-seen values for the monotonic counters core 0 writes.
	// Never shared: only this core reads or writes them, which is what makes the
	// counter scheme a strictly single-writer arrangement.
	uint32_t lastSpookSeq_ = 0, lastClockSeq_ = 0, lastStartleSeq_ = 0;
	uint32_t lastModeReq_  = 0;
	// Core-0-private, for the counters core 1 writes.
	uint32_t lastPulseSeq_ = 0, lastActivitySeq_ = 0;
	bool     spookPending_ = false;
	bool     clockPending_ = false;
	int32_t  clockPeriod_  = 0;   // control ticks between Pulse In 2 edges
	int32_t  clockAge_     = 0;   // ticks since the last edge
	int32_t  loudness_     = 0;   // Q16 envelope of Audio In 1
	// Last value actually written to each CV out, so the flash-resident
	// MillivoltsToDAC is only called when it changes. Seeded to an impossible
	// value so the first real write always goes through.
	int32_t  cvLastMv_[2]  = { -1, -1 };
	int32_t  lastAudio2_   = 0;
	bool     startle_      = false;
	BootMode boot_         = BootMode::Rhythm;
	int32_t  activity_   = 0;
	// Smoothed trigger density, core-1 private; published as gXC.density.
	int32_t  density_    = 0;
	int32_t  densityAgent_[kNumAgents] = {};
	// Alt boot Summed: Pulse 2 is Pulse 1 divided by four, a bar line.
	int      pulseDiv_   = 0;
	uint32_t seed_       = 0xC0FFEEu;

	int      pulseTimer_[2] = { 0, 0 };
	int      cvTimer_[2]    = { 0, 0 };
	int32_t  cvLevel_[2]    = { 0, 0 };
	int32_t  cvSmooth_[2]   = { 0, 0 };

	int      bootPhase_    = 0;
	int      splash_       = 0;   // boot-mode announcement countdown
};

#ifdef BIO_PROFILE
BioMimicryCard *BioMimicryCard::profCard_ = nullptr;
#endif

int main()
{
	// 192MHz, up from the stock 125. This is the honest fix for the last of the
	// timing: the budget is clock/48000, so it goes 2604 -> 4000 cycles per
	// sample, and full Geese at 2652 stops being 102% of budget and becomes 66%.
	//
	// Done here, FIRST, because BioMimicryCard's constructor launches core 1 and
	// the SDK requires the clock to be settled before that.
	//
	// The overvolt is what makes 192 safe rather than hopeful, and it is proven
	// on this exact hardware: ../WorkshopZX runs 200MHz at 1.15V. 192 is a
	// deliberate step below that — it clears the budget with room to spare, so
	// there is no reason to spend the extra margin.
	//
	// NOTE: flash access does NOT scale with the core clock. XIP reads cost more
	// core cycles at 192MHz than at 125, so the flash-bound paths (PCM playback)
	// will not drop by the full 1.54x. The budget still grows by it, so this is
	// a win either way — but expect the measured improvement to be less than the
	// clock ratio, and do not read that as a fault.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);                       // let the regulator settle before the PLL
	set_sys_clock_khz(192000, true);

	BIO_PROFILE_INIT();
	static BioMimicryCard card;
	card.EnableNormalisationProbe();
#ifdef BIO_PROFILE
	card.EnableProfileGate();      // Pulse Out 2 mirrors the callback duration
#endif
	card.Run();
}
