// looper.h — the four-bar event loop behind DRUMS mode.
//
// This records EVENTS, not audio. That decision buys three things that matter:
//
//   1. OVERDUB IS LOSSLESS. Nothing is re-recorded, so a twentieth pass sounds
//      exactly like the first. An audio looper degrades a little every pass.
//   2. TEMPO IS A PLAYBACK PARAMETER. A position is stored in loop TICKS, not
//      in samples, so turning the tempo knob re-times the pattern instead of
//      pitching it.
//   3. IT FITS. Four bytes per event, 512 events, 2KB total. One second of
//      48kHz audio would be 96KB.
//
// Quantisation is applied at PLAYBACK, never at capture: the raw tick is what
// gets stored. That keeps it non-destructive, so a later version can add swing
// or humanise without having thrown the original timing away.

#pragma once
#include <stdint.h>
#include "nibble.h"
#include "drums.h"

namespace nib {

/// One recorded event. Exactly 4 bytes; the array is the card's largest single
/// allocation and it is still only 2KB.
struct LoopEvent
{
	uint16_t tick;    ///< 0..kLoopTicks-1, RAW (unquantised)
	uint8_t  what;    ///< 0..kNumVoices-1 = a drum VOICE; kKnobEvent|lane = knob
	uint8_t  value;   ///< drum: velocity. filter: knob >> 4.
};

/// Marks a knob-automation event rather than a drum hit. The low bits carry
/// which knob, so both lanes share one code path.
constexpr uint8_t kKnobEvent = 0x80;

/// The automated knobs. Main is the DJ filter; Y is the kit character.
enum KnobLane : uint8_t { kLaneFilter = 0, kLaneTone = 1, kNumLanes = 2 };

/// Marks an automation event as belonging to the CURRENT recording pass.
///
/// The replace window would otherwise eat the sweep it is laying down: each new
/// event falls inside the next one's window, so a full re-record ended up with
/// a single surviving event instead of ninety-six. Tagging lets the sweep clear
/// what came before without clearing itself.
///
/// The tag is cleared from every event when recording is armed, so "this pass"
/// always means the one in progress.
constexpr uint8_t kThisPass = 0x40;

static inline bool IsKnobEvent(uint8_t what) { return (what & kKnobEvent) != 0; }
static inline uint8_t LaneOf(uint8_t what)   { return what & (kNumLanes - 1); }
static inline bool IsThisPass(uint8_t what)  { return (what & kThisPass) != 0; }

/// Compare two event types ignoring the pass tag.
static inline bool SameKind(uint8_t a, uint8_t b)
{
	return static_cast<uint8_t>(a & ~kThisPass) == static_cast<uint8_t>(b & ~kThisPass);
}

/// Four bars of 4/4 at 48 ticks per beat. 48 divides by 2, 3, 4, 6, 8, 12 and
/// 16, so every useful subdivision (and triplets) lands on an exact tick.
constexpr int kTicksPerBeat = 48;
constexpr int kBeatsPerLoop = 16;
constexpr int kLoopTicks    = kTicksPerBeat * kBeatsPerLoop;   // 768

/// Playback quantisation: 1/16 notes. A lo-fi drum looper played with fingers
/// on a resistor network needs it.
constexpr int kQuantTicks = kTicksPerBeat / 4;                 // 12

constexpr int kMaxEvents = 512;

/// Filter automation may never occupy more than this many slots.
///
/// This is a HARD budget, and it exists because automation and hits compete for
/// one array. A continuous knob sweep generates one event every
/// kFilterSampleTicks, which is ~96 per pass — so about five passes of idle
/// twiddling would fill all 512 slots, after which Insert() silently drops
/// everything and NEW DRUM HITS STOP BEING RECORDED. From the player's side
/// that feels exactly like the looper overwriting what they just played.
///
/// Automation also REPLACES rather than accumulates (see RecordKnob), so this
/// ceiling is generous: one pass of dense knob movement fits inside it with
/// room to spare, and hits always keep at least kMaxEvents - kMaxFilterEvents
/// slots to themselves.
constexpr int kMaxKnobEvents = 192;

/// Tempo range. 40-240 BPM across the X knob.
constexpr int32_t kBpmMin = 40;
constexpr int32_t kBpmMax = 240;

/// Longest gap between clock pulses that still counts as a tempo, in control
/// ticks. 2 seconds is 30 BPM — below the knob's own 40 BPM floor, so anything
/// the card can play internally can also be clocked.
constexpr int32_t kClockMaxGap = 2 * kCtrlRate;

/// How long after the last pulse the X knob takes the tempo back.
///
/// MUST be longer than kClockMaxGap, or a slow clock times out before its next
/// pulse arrives and can never establish an interval at all. It was 3s against
/// a 4s measurement limit, which made every clock below 20 BPM unlockable.
constexpr int32_t kClockTimeout = 3 * kCtrlRate;
static_assert(kClockTimeout > kClockMaxGap,
              "the clock must not time out before it can measure itself");

/// How often each automated knob is sampled while recording, in ticks. Caps
/// automation density at ~6 events/beat worst case and usually near zero,
/// since an event is only emitted when the knob has actually moved.
constexpr int kKnobSampleTicks = 8;

/// How near an existing automation event has to be, in ticks, for a new one to
/// REPLACE it rather than sit alongside it.
///
/// This has to be a window, not an exact match, and the arithmetic says why: a
/// second pass over the same sweep samples on a different phase from the first,
/// so of 96 samples per lane per loop, EXACTLY ZERO land on an existing tick.
/// With an exact test nothing was ever replaced — both sweeps survived,
/// interleaved, and playback alternated between two different values on
/// adjacent ticks. That is what a re-recorded sweep sounded like.
///
/// Slightly wider than the sample interval, so a pass always subsumes the one
/// beneath it however the two happen to line up. At 120bpm this is ~125ms, which
/// is also about the resolution a hand can place a knob move at anyway.
constexpr int kKnobReplaceWindow = kKnobSampleTicks + 4;   // 12 ticks

class Looper
{
public:
	/// Advance time. Returns true on each tick boundary crossed; the caller
	/// fires whatever events that tick carries. Control rate.
	bool Advance();

	/// Set tempo from the X knob (0..4095). Recomputes the tick increment ONLY
	/// when the knob has moved: the division is a libgcc call of ~100 cycles on
	/// an M0+, which is fine a few times a second and unacceptable per tick.
	///
	/// Ignored while an external clock is running — see ClockPulse().
	void SetTempo(int32_t xKnob);

	/// An external clock edge arrived on Pulse In 1. Treated as one QUARTER
	/// NOTE, and it takes over from the knob for as long as it keeps arriving.
	///
	/// Rather than snapping the playhead (which would stutter the pattern on
	/// every edge), this measures the interval between edges and sets the tick
	/// rate from it. The loop then free-runs at the clock's tempo and stays
	/// phase-locked because each edge also re-aligns the playhead to the
	/// nearest beat — a correction of a tick or two, inaudible, rather than a
	/// jump.
	void ClockPulse();

	/// True while an external clock is driving the tempo.
	bool Clocked() const { return clockTimeout_ > 0; }

	/// Record a drum hit at the current position, by VOICE index.
	///
	/// Deliberately not by gesture: a pattern is a list of sounds, and how each
	/// was played belongs to the performance. Storing the gesture also meant a
	/// replayed hit had to re-derive its sound from a combination with no shift
	/// attached, and re-arranging the gesture map would silently change what an
	/// old loop played.
	///
	/// Ignored when the loop is full — dropping the 513th event is better than
	/// wrapping over the first.
	void RecordHit(int8_t voice, uint8_t velocity);

	/// Record a filter-knob position, if it has moved enough to be worth it.
	///
	/// The first call after ArmFilter() only seeds the reference — arming record
	/// must never itself write automation, or the knob's resting position gets
	/// stamped into the loop and can silence it.
	/// Record both automated knobs. Called once per control tick while
	/// recording; internally rate-limited to one sample every kKnobSampleTicks.
	///
	/// A lane only writes while its `moving` flag is set — i.e. while the player
	/// actually has hold of that knob. That is what makes automation OVERWRITE
	/// IN PLACE: the ticks you sweep across get your new values, and the ticks
	/// you do not touch keep whatever was recorded before. A knob sitting still
	/// records nothing at all, so it can never overwrite an earlier sweep with a
	/// flat line.
	///
	/// Both lanes are taken together rather than each managing its own timer,
	/// because a shared countdown consumed by whichever lane asked first would
	/// starve the other completely.
	void RecordKnobs(bool filterMoving, int32_t filterKnob,
	                 bool toneMoving,   int32_t toneKnob);

	/// Called when record is armed or released. Drops the knob reference so the
	/// next RecordKnob() re-seeds instead of comparing against a stale value
	/// from a previous pass.
	void ArmKnobs()
	{
		for (int i = 0; i < kNumLanes; i++) lastKnob_[i] = -9999;
		// Everything already in the loop belongs to a PREVIOUS pass, so it is
		// all fair game for the replace window from here on.
		for (int i = 0; i < count_; i++)
			events_[i].what = static_cast<uint8_t>(events_[i].what & ~kThisPass);
	}

	/// Fire every event scheduled for the current tick.
	/// `outCombo` receives drum hits; `outFilter` receives automation.
	/// Returns the number of drum hits written (0..kMaxFirePerTick).
	static constexpr int kMaxFirePerTick = 8;
	/// `outKnob[lane]` receives any automation that fired this tick, with the
	/// matching `haveKnob[lane]` set.
	int Fire(int8_t *outCombo, uint8_t *outVel,
	         int32_t *outKnob, bool *haveKnob);

	void Clear();

	uint16_t Position() const   { return playHead_; }
	uint16_t EventCount() const { return count_; }
	bool     Full() const       { return count_ >= kMaxEvents; }

	/// True while the playhead is on a beat's first tick — the LEDs pulse on it.
	///
	/// A LEVEL, not an edge: it stays true for as long as that tick lasts, which
	/// at 40bpm is dozens of control ticks. Fine for an LED, useless for a
	/// trigger. Use BeatEdge() for anything that needs to fire once.
	bool OnBeat() const { return (playHead_ % kTicksPerBeat) == 0; }

	/// True exactly ONCE per beat, on the control tick that crosses into it.
	///
	/// This is what drives the click on Pulse Out 2. Consumed by reading, so
	/// call it once per Advance() and nowhere else.
	bool BeatEdge()
	{
		if (!beatEdge_) return false;
		beatEdge_ = false;
		return true;
	}

private:
	void Insert(const LoopEvent &ev);
	void Remove(int i);
	void RecordLane(uint8_t lane, int32_t knob);
	bool NearPlayhead(uint16_t tick) const;

	/// The tick at which an event actually sounds, after quantisation. The
	/// event array is sorted by THIS, not by the raw stored tick — see the
	/// comment in Insert().
	static uint16_t FireTick(const LoopEvent &ev);

	LoopEvent events_[kMaxEvents] = {};
	uint16_t  count_       = 0;
	uint16_t  knobCount_ = 0;     ///< how many of count_ are automation
	uint16_t  playHead_ = 0;
	uint16_t  cursor_   = 0;    ///< index of the next event at or after playHead_

	int32_t  phase_   = 0;      ///< Q16 fraction of a tick
	int32_t  tickInc_ = 0;      ///< Q16 ticks per control step
	int32_t  lastX_   = -9999;  ///< last tempo knob reading, for move detection

	// External clock. clockTimeout_ counts down at control rate; while it is
	// non-zero the knob is ignored. About 3 seconds, so a stopped clock hands
	// control back rather than freezing the loop.
	bool     beatEdge_ = false;   ///< set by Advance(), consumed by BeatEdge()
	int32_t  clockTimeout_ = 0;
	int32_t  clockInterval_ = 0;   ///< control ticks between the last two edges
	int32_t  sinceClock_    = 0;

	int32_t  lastKnob_[kNumLanes] = { -9999, -9999 };
	int32_t  knobCountdown_ = 0;
};

} // namespace nib
