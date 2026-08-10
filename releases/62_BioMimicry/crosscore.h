// crosscore.h — the ONLY state shared between the two cores.
//
// v1.1.0 moves the physics onto core 1 so the engine and the voices stop landing
// in the same 20.83us sample. Everything that has to cross between them lives
// here, following the same discipline WorkshopZX uses for its CrossCore (see
// ../WorkshopZX/spectrum.h): every field volatile, ONE WRITER PER FIELD, no
// locks, no SDK FIFO, no barriers on plain scalars.
//
// That works because every field below is a single naturally-aligned word or
// byte, so each core sees either the old value or the new one and never a
// mixture. It is a real constraint, not a style preference:
//
//   THE RULE. Word-tearing is acceptable for smoothed continuous values — a
//   one-tick mismatch between densityAgent[0] and densityAgent[2] is a control
//   voltage a fraction of a millisecond stale, and inaudible. It is NEVER
//   acceptable for pointers, lengths or counts, where a torn read is an
//   out-of-bounds access. Anything with that shape crosses through TrigRing
//   below, packed into one word, never as a struct.
//
// This is why note-on data crosses as a packed word rather than by calling
// VoiceBank::note() from core 1: note() writes ~20 fields of Voice, including a
// 128-entry ks[] buffer, while render() reads and mutates the same struct every
// sample. No amount of volatile makes that safe.

#pragma once
#include <stdint.h>
#include "biomimicry.h"

namespace bio {

// ---------------------------------------------------------------------------
// Trigger ring: core 1 (physics) -> core 0 (voices)
// ---------------------------------------------------------------------------
//
// A note-on carries agent, mode, member and a variation value. accent is always
// kQ16One today, and variation only ever feeds `variation >> 3` in note(), so
// the whole payload packs into ONE 32-bit word — a single aligned store, which
// cannot tear.
//
//   bits  0..1   agent      0..3
//   bits  2..4   mode       0..5
//   bits  5..6   member     0..3
//   bits  7..22  variation  Q16, truncated to 16 bits
//   bits 23..31  spare      (if accent ever varies, take 8 bits here as Q8)
typedef uint32_t TrigWord;

static inline TrigWord PackTrig(int agent, int mode, int member, int32_t variation)
{
	uint32_t v = static_cast<uint32_t>(variation);
	if (variation < 0)      v = 0;
	else if (v > 0xFFFFu)   v = 0xFFFFu;
	return static_cast<uint32_t>(agent & 3)
	     | (static_cast<uint32_t>(mode & 7) << 2)
	     | (static_cast<uint32_t>(member & 3) << 5)
	     | (v << 7);
}

static inline int      TrigAgent(TrigWord w)     { return  w        & 3; }
static inline int      TrigMode(TrigWord w)      { return (w >> 2)  & 7; }
static inline int      TrigMember(TrigWord w)    { return (w >> 5)  & 3; }
static inline int32_t  TrigVariation(TrigWord w) { return static_cast<int32_t>((w >> 7) & 0xFFFF); }

// Power of two so head/tail wrap with a mask — the M0+ has no divider.
constexpr int kTrigRingBits = 4;
constexpr int kTrigRingSize = 1 << kTrigRingBits;

/// Single-producer (core 1), single-consumer (core 0) ring.
///
/// Safe without a lock because `head` is written only by core 1 and `tail` only
/// by core 0, both single words. The payload is stored BEFORE head is bumped and
/// read AFTER tail is compared against head, so the consumer can never see a
/// slot the producer has not finished filling.
///
/// Sized 16 against a worst case of 4 (one note per agent per control tick),
/// drained once per control tick — four ticks of slack, 64 bytes.
struct TrigRing
{
	volatile TrigWord slot[kTrigRingSize];
	volatile uint32_t head;      // WRITER: core 1
	volatile uint32_t tail;      // WRITER: core 0
	volatile uint32_t dropped;   // WRITER: core 1 — see below
};

// ---------------------------------------------------------------------------

struct CrossCore
{
	// --- written by CORE 0, read by core 1 ---------------------------------

	/// ++ every ProcessSample. This is core 1's only clock: it derives how many
	/// physics ticks it owes from the elapsed count, so the 1.5kHz control rate
	/// stays anchored to audio time however fast core 1 actually runs.
	volatile uint32_t sampleCount;

	/// ComputerCard::SwitchVal() returns a member that is NOT declared volatile,
	/// so core 1 could cache it forever. Core 0 republishes it here instead.
	/// CORE 1 MUST NEVER CALL SwitchVal() — it will appear to work and then stop.
	volatile uint8_t  switchMirror;

	/// Edge counters, not flags. The old bools were set at 48kHz by core 0 and
	/// cleared at 1.5kHz by the consumer; with the consumer on the other core
	/// that is a lost-update race, and two edges inside one tick already
	/// collapsed into one. Core 1 compares these against its own private
	/// last-seen values and never writes them.
	volatile uint32_t spookSeq;
	volatile uint32_t clockSeq;
	volatile uint32_t startleSeq;

	/// Q16 envelope of Audio In 1, from listen().
	volatile int32_t  loudness;

	/// ++ when a short tap is released. Core 1 owns mode_ (it selects the engine
	/// and calls reset()), so core 0 requests a change rather than making one.
	volatile uint32_t modeCycleReq;

	/// Core 0 has finished its boot window and published the globals.
	volatile bool     bootReady;
	volatile uint8_t  bootMode;     // BootMode, latched once at boot

	// --- written by CORE 1, read by core 0 ---------------------------------

	/// Single bytes: cannot tear, so core 0 can read them at 48kHz safely.
	volatile uint8_t  mode;
	volatile uint8_t  routing;
	volatile uint8_t  population;

	/// Continuous CV, Q16 (Summed routing and Drone).
	volatile int32_t  cvTarget[2];

	/// Bitmask of CV outs carrying a STEPPED value that must not be slewed:
	/// bit0 = CV 1, bit1 = CV 2. Alt boot's pitch CV sets bit1, because
	/// gliding between semitones would turn a sequence into a portamento
	/// smear rather than notes.
	volatile uint8_t  cvStep;

	/// Gate arming. Core 0 notices pulseSeq change and starts its timers.
	volatile uint8_t  pulseArm;     // bit0/bit1 -> Pulse Out 1/2
	volatile uint8_t  cvTrigArm;    // bit0/bit1 -> CV Out 1/2 blips
	volatile uint32_t pulseSeq;

	/// ++ on any trigger. uiTick() on core 0 owns the decay of its own activity
	/// level; nothing shares a mutable brightness.
	volatile uint32_t activitySeq;

	/// Smoothed trigger density, Q16 — how BUSY the ecosystem is, independent of
	/// what it is doing. Goes to CV 1 in alt boot. Decays far more slowly than
	/// the LED activity glow, which is tuned to flash per hit; this has to read
	/// as a continuous control voltage over seconds.
	volatile int32_t  density;

	/// Per-agent smoothed density, Q16. Alt boot's Discrete routing puts agents 1
	/// and 2 on the pulse outs and THEIR OWN densities on the CV outs, so each
	/// half of the patch is a trigger and a matching control voltage.
	volatile int32_t  densityAgent[kNumAgents];

	/// Perf meters, both read out over SysEx with the profile buckets.
	/// maxBacklog is the worst number of ticks core 1 ever owed at once: 1 is
	/// healthy, a steady 2+ means the engine is taking longer than one control
	/// period and the physics are running in slow motion.
	volatile uint32_t maxBacklog;

	// --- USB mode ----------------------------------------------------------

	/// Core 1 has stopped the physics and handed itself to TinyUSB.
	volatile bool     usbActive;
};

inline CrossCore gXC = {};
inline TrigRing  gTrig = {};

} // namespace bio
