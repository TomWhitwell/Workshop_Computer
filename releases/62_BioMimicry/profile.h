// profile.h — cycle-accurate timing for ProcessSample.
//
// WHY THIS EXISTS
//
// ProcessSample() runs inside the DMA interrupt handler (ComputerCard::
// AudioCallback), and controlTick() is called INLINE from it. So on the one
// sample in kCtrlDiv where the physics run, the whole engine still has to finish
// inside that single 20.83us slot. Dividing the physics by 32 lowers the AVERAGE
// load; it does not relax the deadline.
//
// This matters because the card's own docs previously quoted "~55 cycles/sample
// amortised", which is throughput — the wrong quantity. What decides whether
// audio glitches is the WORST SINGLE SAMPLE, and that had never been measured.
//
// HOW
//
// The Cortex-M0+ SysTick counter counts CPU cycles directly, so this gives exact
// numbers with no external equipment and no scope. It is a 24-bit DOWN counter,
// which is ample: the whole budget is 4000 cycles at 192MHz.
//
// Everything here compiles to nothing unless BIO_PROFILE is defined, so the
// released firmware is byte-identical to a build without it.

#pragma once
#include <stdint.h>

#ifdef BIO_PROFILE
#include "hardware/structs/systick.h"
#endif

namespace bio {

/// Cycles available per sample. Overrun this and the next DMA interrupt arrives
/// while we are still in the handler.
///
/// Derived from the clock rather than hard-coded, so the overclock in main()
/// cannot silently leave the profiler reporting percentages of the wrong number.
constexpr uint32_t kClockHz     = 192000000u;            // see main(): 192MHz
constexpr uint32_t kCycleBudget = kClockHz / 48000u;     // 4000

#ifdef BIO_PROFILE

/// What we time. Total is the whole callback; the rest are its phases.
// Named for what they now measure. "Engine" no longer means the physics: after
// v1.1.0 those run on core 1, and this is core 0's remaining control work.
enum class Prof : uint8_t { Total = 0, Ctrl, Voices, Outputs, Notes, Count };
constexpr int kNumProf = static_cast<int>(Prof::Count);

struct ProfStat
{
	uint32_t last;      // most recent measurement, cycles
	uint32_t peak;      // worst ever seen
	uint32_t overruns;  // samples where Total exceeded the budget
};

/// One global block — this is a single-threaded interrupt context, so no
/// synchronisation is needed. `inline` gives it a definition here without
/// needing a .cpp (C++17).
inline ProfStat gProf[kNumProf] = {};

/// Set true once the card has finished booting. Until then measurements are
/// discarded: voices_.init() clears four 128-sample Karplus-Strong buffers and
/// 32 grain slots on a SINGLE sample at the end of the boot window, which
/// certainly overruns — and since `overruns` is cumulative, that one spike would
/// otherwise latch the overrun indicator on forever and look like a steady-state
/// fault in every mode.
inline bool gProfArmed = false;

/// Set `gProfGate` to a function that drives a pulse out, and the Total scope
/// will raise it for the duration of the callback — a scope then shows the duty
/// cycle directly, confirming the SysTick numbers with independent hardware.
inline void (*gProfGate)(bool) = nullptr;

/// Start SysTick free-running at the core clock. Call once before Run().
inline void ProfileInit()
{
	systick_hw->csr = 0;              // disable while reconfiguring
	systick_hw->rvr = 0x00FFFFFF;     // full 24-bit reload
	systick_hw->cvr = 0;              // clear (writing any value clears)
	// ENABLE | CLKSOURCE=core clock. No TICKINT: we never want an interrupt,
	// only a counter to read.
	systick_hw->csr = 0x5;
	for (int i = 0; i < kNumProf; i++) gProf[i] = { 0, 0, 0 };
}

/// RAII scope: reads the counter at entry and exit and records the delta.
class ProfileScope
{
public:
	explicit ProfileScope(Prof which)
		: which_(which), start_(systick_hw->cvr)
	{
		if (which_ == Prof::Total && gProfGate) gProfGate(true);
	}

	~ProfileScope()
	{
		if (!gProfArmed)
		{
			if (which_ == Prof::Total && gProfGate) gProfGate(false);
			return;
		}
		uint32_t now = systick_hw->cvr;
		// SysTick counts DOWN, so elapsed = start - now, and a single wrap
		// past zero is corrected by the 24-bit mask. A scope longer than
		// 2^24 cycles (134ms) would alias, which cannot happen inside a
		// 20.83us callback.
		uint32_t elapsed = (start_ - now) & 0x00FFFFFF;

		ProfStat &s = gProf[static_cast<int>(which_)];
		s.last = elapsed;
		if (elapsed > s.peak) s.peak = elapsed;
		if (which_ == Prof::Total)
		{
			if (elapsed > kCycleBudget) s.overruns++;
			if (gProfGate) gProfGate(false);
		}
	}

private:
	Prof     which_;
	uint32_t start_;
};

/// Worst-case Total as a fraction of the budget, Q16. >65536 means it overran.
inline uint32_t ProfilePeakFrac()
{
	uint64_t p = static_cast<uint64_t>(gProf[0].peak) * 65536u / kCycleBudget;
	return static_cast<uint32_t>(p > 0xFFFFFFFFu ? 0xFFFFFFFFu : p);
}

inline bool ProfileOverran() { return gProf[0].overruns > 0; }

/// Forget the peaks — lets you measure one mode at a time without a reboot.
inline void ProfileReset()
{
	for (int i = 0; i < kNumProf; i++) { gProf[i].peak = 0; gProf[i].overruns = 0; }
}

/// Begin recording. Called once the boot-time allocation spike is behind us.
inline void ProfileArm() { ProfileReset(); gProfArmed = true; }

#define BIO_PROFILE_INIT()      ::bio::ProfileInit()
#define BIO_PROFILE_ARM()       ::bio::ProfileArm()
#define BIO_PROFILE_SCOPE(w)    ::bio::ProfileScope _bioProf##__LINE__(::bio::Prof::w)
#define BIO_PROFILE_RESET()     ::bio::ProfileReset()

#else   // ---------------------------------------------------------------

// Not profiling: every macro vanishes, so the normal build is unaffected.
#define BIO_PROFILE_INIT()      ((void)0)
#define BIO_PROFILE_ARM()       ((void)0)
#define BIO_PROFILE_SCOPE(w)    ((void)0)
#define BIO_PROFILE_RESET()     ((void)0)

#endif

} // namespace bio
