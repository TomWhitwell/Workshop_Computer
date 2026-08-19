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
// Quantisation of DRUM HITS is applied at CAPTURE — see QuantGrid's comment
// for why this reverses the card's original design (a live playback filter
// cannot let two different grids, e.g. a straight part and a triplet fill,
// coexist in one loop; snapping once at RecordHit() time can). Filter/tone
// AUTOMATION is the one thing still true to the original design: never
// snapped at all, at capture or playback, because a quantised knob sweep is
// a staircase rather than a curve.

#pragma once
#include <stdint.h>
#include "nibbleko.h"
#include "drums.h"

namespace nko {

/// One recorded event. Exactly 4 bytes; the array is the card's largest single
/// allocation and it is still only 2KB.
///
/// A PATTERN IS SOUND-AGNOSTIC, and that is a property to protect rather than
/// an accident. `what` holds a VOICE INDEX — "slot 4" — never a sample, a
/// pointer, or anything describing what slot 4 currently sounds like. The
/// chain from event to audio is:
///
///     event -> voice index -> kVoiceSample -> bank index -> ResolveSample
///
/// and only the last two links know anything about sound. So re-pointing a
/// voice at a different sample, or uploading a new one over USB, changes what
/// every existing pattern PLAYS without touching a byte of the patterns
/// themselves. The same four bars can be a Cheetah kit or a user upload.
///
/// This falls out of NIBBLE's decision to record the VOICE rather than the
/// GESTURE (see RecordHit), which it made so that re-arranging the gesture map
/// could not silently change an old loop. The same indirection is what makes
/// patterns portable across sample sets — and it is why patterns will
/// transfer over the web app as bare event lists, with no audio attached.
///
/// Storing a resolved pointer here would be faster by one indirection and
/// would break all of that, so: don't.
struct LoopEvent
{
	uint16_t tick;    ///< 0..kLoopTicks-1, RAW (unquantised)
	uint8_t  what;    ///< 0..kNumVoices-1 = a drum VOICE; kKnobEvent|lane = knob
	uint8_t  value;   ///< drum: velocity. filter: knob >> 4.
};

/// Marks a knob-automation event rather than a drum hit. The low bits carry
/// which knob, so both lanes share one code path.
constexpr uint8_t kKnobEvent = 0x80;

/// The automation lanes.
///
/// The first two are knobs. The rest are performance EFFECTS, recorded as
/// PARAMETERS rather than as knob movement — "crush at depth 900", not "the
/// Main knob is at 900". That distinction matters because in FX1 the Main
/// knob IS the depth, so recording it as a knob would write the same movement
/// into the filter lane and replay a filter sweep that never happened.
///
/// FOUR FX lanes, one per SHIFT BUTTON. With a single lane a second recorded
/// effect overwrote the first wherever the two overlapped in the bar — you
/// could not have a crush on beat 1 and a gate on beat 3 without the later
/// pass eating the earlier one. One lane per shift means each family of
/// effects keeps its own timeline and they layer instead of competing.
///
/// The lane is chosen by the SHIFT, not by the effect, so which lane a
/// gesture writes to is fixed by how it is played — hold A and you are
/// always writing lane A, whichever of A's three effects you tap.
///
/// kNumLanes must stay a POWER OF TWO: LaneOf() masks with kNumLanes-1. Six
/// lanes therefore need eight slots; the last two are spare.
/// Each shift owns TWO lanes: which effect is running, and that lane's
/// PARAMETER curve. They are separate because they are performed separately —
/// hold a shift alone and turn Main to draw the curve, then tap effects in
/// and out over the top of it. Recording them together would mean you could
/// not change one without re-performing the other.
enum KnobLane : uint8_t {
	kLaneFilter = 0,   ///< Main knob -> DJ filter
	kLaneTone   = 1,   ///< Y knob -> kit character

	kLaneFxA    = 2,   ///< which effect is held under shift A (filters)
	kLaneFxB    = 3,   ///< shift B (destruction)
	kLaneFxC    = 4,   ///< shift C (TIME — see below)
	kLaneFxD    = 5,   ///< shift D (dynamics)

	kLaneParA   = 6,   ///< shift A's parameter curve
	kLaneParB   = 7,
	kLaneParC   = 8,
	kLaneParD   = 9,

	kNumLanes   = 16   ///< a power of two, for LaneOf()'s mask
};

/// The first lane of each family, so a shift index maps to a lane by addition.
constexpr uint8_t kLaneFxFirst  = kLaneFxA;
constexpr uint8_t kLaneParFirst = kLaneParA;

/// Which effect-lane a shift button writes to.
static inline uint8_t FxLaneForShift(int8_t shift)
{
	return static_cast<uint8_t>(kLaneFxFirst + shift);
}

/// Which parameter-lane a shift button writes to.
///
/// Keyed on the SHIFT alone — the tap is deliberately ignored, so the three
/// effects under one shift SHARE a depth curve. That is what makes a shift a
/// coherent layer rather than three unrelated things, and it is why there are
/// four parameter lanes rather than twelve.
///
/// The cost, which is real and documented in README.md: recording a depth
/// twiddle under B+C overwrites a curve recorded earlier under B+A, wherever
/// they overlap. Playing over a recorded curve non-destructively is done with
/// the switch at MIDDLE, where the hand drives the depth live and nothing is
/// written. Twelve lanes would remove the caveat at the cost of kNumLanes
/// growing past 16 and a lot more event-buffer pressure.
static inline uint8_t ParLaneForShift(int8_t shift)
{
	return static_cast<uint8_t>(kLaneParFirst + shift);
}

/// Shift C carries the TIME family (stutter, half/double time, flam), and
/// those cannot meaningfully stack — half-time and double-time at once is a
/// contradiction, not a texture. Its lane is therefore EXCLUSIVE: only one
/// timing effect sounds at a time, while the other three lanes chain freely.
constexpr uint8_t kLaneTiming = kLaneFxC;

/// An FX lane event carries only WHICH effect, since the depth now lives in
/// its own parameter lane. Zero means NO EFFECT, which is why Fx::None must
/// stay index 0.
///
/// Depth used to be packed into the same byte, four bits each, which cost it
/// most of its resolution — a sweep replayed as a sixteen-step staircase.
/// Splitting the two lanes gives the parameter a full byte and, more
/// importantly, lets them be performed independently: draw a curve once, then
/// pop effects in and out over it without re-recording the curve.
static inline uint8_t PackFx(uint8_t fx)   { return fx; }
static inline uint8_t FxOf(uint8_t v)      { return v; }

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

/// The shortest loop the WebUI may set, in beats. Four beats is one bar of
/// 4/4; below that the "loop" stops being a phrase and becomes a stutter,
/// and the pattern slots (which always hold the FULL sixteen) would be
/// almost entirely unreachable.
constexpr int kMinBeatsPerLoop = 4;

/// Quantisation grid, as a divisor of kTicksPerBeat. A lo-fi drum looper
/// played with fingers on a resistor network needs it.
///
/// RUNTIME now, not a constant — cycled live by the QUANTISE gesture
/// (switch+AD). Three settings, all EXACT divisors of kTicksPerBeat=48 by
/// construction (see the comment above): 16th is the default a beginner
/// wants, 12th is the triplet feel, 8th is loose enough to keep a
/// deliberately sloppy take human.
///
/// APPLIED AT CAPTURE, NOT PLAYBACK — a deliberate reversal of this file's
/// original quantisation design (still true for filter/tone automation,
/// which is never snapped at all). RecordHit() rounds to whatever grid is
/// live at the moment of the hit and stores THAT tick; playback never
/// re-rounds it. Two things follow, both wanted:
///
///   - cycling the grid mid-performance never moves anything already
///     recorded, so a straight part recorded under 16ths and a triplet part
///     recorded under 12ths coexist in the same loop, each on the grid it
///     was played against. A live playback filter cannot do this — one
///     divisor applies to every event at once, so switching to 12ths for a
///     triplet fill would retroactively drag the straight part's hits onto
///     the triplet grid too.
///   - it costs the non-destructive property the file header still claims
///     for automation: a hit's ORIGINAL unquantised timing is gone the
///     instant it is recorded, not recoverable by a future "loosen the
///     grid" control the way it would be under playback quantisation.
///     Accepted knowingly — mixed grids in one loop is the more useful
///     property for this card.
enum class QuantGrid : uint8_t { k16th = 0, k12th = 1, k8th = 2, kNumGrids = 3 };

/// NOTES PER BEAT, not a divisor of kTicksPerBeat directly — a 1/16 note is
/// FOUR per beat (kTicksPerBeat/4), a 1/8 note TWO (kTicksPerBeat/2), and a
/// 1/12 "note" here means the triplet-eighth feel, THREE per beat
/// (kTicksPerBeat/3). QuantiseTick() does that division.
///
/// An earlier version of this table held {16, 12, 8} and used it directly as
/// the divisor of kTicksPerBeat=48, which gives 16/12/8 grid points PER BEAT
/// instead of per bar — a grid four times finer than the names claimed. At
/// that resolution even the loosest setting (1/8, meant to be "loose enough
/// to keep a sloppy take human") only ever moved a hit by up to 3 ticks out
/// of 768, under 40ms at a typical tempo: inaudible, which is exactly the
/// bug report that found this ("I don't think I'm hearing them").
constexpr int kQuantNotesPerBeat[static_cast<int>(QuantGrid::kNumGrids)] = { 4, 3, 2 };

constexpr int kMaxEvents = 512;

/// How many patterns can be stored.
///
/// THREE, not four, and for the same reason there are three mute groups: the
/// gesture is hold-D-and-tap, so the shift button is not itself a slot. The
/// count follows the panel rather than being a number someone picked.
///
/// Indexed 0..2 by SlotForShiftedTap, which ranks the tap among the
/// non-shift singles in panel order — so slots 0/1/2 are pads A/B/C.
constexpr int kNumPatterns = 3;

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
/// room to spare, and hits always keep at least kMaxEvents - kMaxKnobEvents
/// slots to themselves.
///
/// Raised from 192 when the FX lanes went from one to four. Six lanes can
/// legitimately hold more than two ever could: a lane held for a whole
/// four-bar loop is 96 events on its own, so 192 was under two full lanes.
/// 320 leaves 192 slots for hits, which is far more than a pair of hands
/// plus a few overdubs will ever place, while still refusing to let
/// automation eat the array — which is the bug this constant exists for.
constexpr int kMaxKnobEvents = 320;

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
	/// All lanes are taken together rather than each managing its own timer,
	/// because a shared countdown consumed by whichever lane asked first would
	/// starve the others completely.
	///
	/// The FX lanes differ from the two knob lanes in one way that matters:
	/// they write whenever an effect is ACTIVE, not only when something is
	/// "moving". An effect being held steady is information — it has to be
	/// recorded for every tick it is held, or playback would turn it on and
	/// then never off. A packed value of 0 means no effect, which records
	/// nothing and so leaves any earlier pass alone, exactly as a still knob
	/// does.
	///
	/// `fxPacked` is indexed by SHIFT (0..3 for A..D), so only the lane whose
	/// shift is currently held is written. The others keep whatever earlier
	/// passes put there — which is the whole point of having four.
	///
	/// `parShift` names the lane whose PARAMETER curve is being drawn, or -1
	/// for none. That is a separate gesture from holding an effect: shift
	/// alone plus a turn of Main writes the curve, and any effect on that lane
	/// reads its depth from it. Unlike the effect lanes this only writes while
	/// the knob is actually MOVING, exactly like the filter and tone lanes —
	/// a still knob must not flatten a curve recorded on an earlier pass.
	///
	/// That rule cannot be relaxed to "while the shift is held", and the
	/// reason is the card's founding quirk: FOUR VOLTAGES LATCHES, so
	/// levels_.Current() reports a shift as held long after the finger has
	/// gone. Every knob lane therefore records CHANGE, never position.
	void RecordKnobs(bool filterMoving, int32_t filterKnob,
	                 bool toneMoving,   int32_t toneKnob,
	                 const uint8_t *fxPacked,
	                 int8_t parShift, int32_t parKnob, bool parMoving);

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

	/// Copy the whole pattern aside, so Undo() can put it back.
	///
	/// UNDO IS ABOUT RECORDING, AND ONLY RECORDING. It covers what a pass of
	/// the switch in the Up position put into the live loop — drum hits,
	/// mute toggles, effects, knob curves — and nothing else. Specifically it
	/// does NOT cover the pattern slots: storing one is a separate,
	/// deliberate act on a separate piece of state, and a gesture that
	/// sometimes meant "undo my playing" and sometimes "un-store that slot"
	/// would be two features wearing one name.
	///
	/// StorePattern therefore does not snapshot, and must not start. What
	/// protects a slot instead is that storing refuses an empty loop, so the
	/// destructive case cannot happen by accident — see StorePattern.
	///
	/// Called before anything destructive to the LIVE LOOP: arming record,
	/// and (once it exists) quantising. Depth is ONE — this is "undo the last
	/// thing", not a history stack. A second buffer is already 2KB, the
	/// card's largest single allocation doubled; a stack is not affordable
	/// and was never asked for.
	///
	/// Snapshotting on ARM rather than on the first recorded event is
	/// deliberate: it means an overdub pass that you decide against can be
	/// undone even if you played nothing at all during it, and it gives one
	/// obvious moment to reason about rather than a lazily-taken one.
	void Snapshot();

	/// Put the snapshot back, if there is one. Returns false if there was
	/// nothing to undo.
	///
	/// One-way: the snapshot is consumed, so a second Undo does nothing rather
	/// than toggling back and forth. Redo was not asked for and a toggle that
	/// looks like undo-twice is worse than a no-op.
	bool Undo();

	/// True when there is a snapshot to go back to.
	bool CanUndo() const { return haveSnapshot_; }

	// --- pattern slots -----------------------------------------------------
	//
	// Four stored patterns, recalled instantly and overwritten deliberately.
	// 8KB of RAM, which is affordable at ~15% total and buys the one thing a
	// four-bar looper most obviously lacks: somewhere to put a second idea.
	//
	// Recall REPLACES the live loop without saving it. That is the whole
	// reason storing is a separate, deliberate gesture (a long hold): if
	// switching away saved automatically there would be no way to try
	// something and abandon it, and every overdub would be committed whether
	// or not you meant it. Undo still covers one step back.

	/// Copy the live loop into slot `i`.
	///
	/// Refuses to store an EMPTY loop. Overwriting a good pattern with silence
	/// is never what the gesture meant, and it is unrecoverable — Undo covers
	/// the live loop, not the slots. Returns false so the caller can say so
	/// rather than flashing a confirmation for something that did not happen.
	bool StorePattern(int i);

	/// Load slot `i` over the live loop. The PLAYHEAD IS NOT MOVED — the new
	/// pattern picks up from wherever in the bar you already are, so
	/// switching mid-bar is seamless rather than a restart.
	///
	/// Does nothing if the slot is empty; returns false so the caller can say
	/// so rather than silently clearing the loop.
	bool RecallPattern(int i);

	/// True if slot `i` holds anything.
	bool PatternStored(int i) const
	{
		return i >= 0 && i < kNumPatterns && patternCount_[i] > 0;
	}

	/// Copy slot `i` out for transfer to the browser. `out` must have room for
	/// kMaxEvents. Returns false for a bad slot; an EMPTY slot is a success
	/// with count 0, so the browser can tell "nothing stored" apart from
	/// "no such slot".
	bool GetPatternRaw(int i, LoopEvent *out, uint16_t &count,
	                   uint16_t &knobCount) const;

	/// Replace slot `i` with a pattern received from the browser.
	///
	/// Touches ONLY the slot. events_/cursor_/playHead_ are left alone, so a
	/// pattern arriving over USB behaves exactly like one stored by the
	/// gesture: what is currently playing keeps playing, and the new content
	/// takes effect at the next RecallPattern(). Rejects count > kMaxEvents.
	bool SetPatternRaw(int i, const LoopEvent *in, uint16_t count,
	                   uint16_t knobCount);

	// --- quantise grid ------------------------------------------------------

	/// Step to the next grid (16th -> 12th -> 8th -> 16th...).
	///
	/// AFFECTS ONLY FUTURE HITS. Quantisation snaps once, at RecordHit() time
	/// (see QuantiseTick()), so this never touches anything already in the
	/// loop — cycling from 12ths to 8ths mid-performance does not drag an
	/// already-recorded 12th-quantised part onto the new grid. Only the next
	/// overdub pass records against the new setting.
	///
	/// No re-sort needed: since existing events' stored ticks never change,
	/// the array's sorted-by-FireTick() invariant is undisturbed by a grid
	/// change — unlike an earlier version of this feature, which re-quantised
	/// at PLAYBACK and had to re-sort the whole array every time the grid
	/// changed, because a coarser grid could push a hit's fire tick across
	/// the loop boundary and out of order with its neighbours.
	QuantGrid CycleQuantGrid()
	{
		quantGrid_ = static_cast<QuantGrid>(
			(static_cast<int>(quantGrid_) + 1)
			% static_cast<int>(QuantGrid::kNumGrids));
		return quantGrid_;
	}

	QuantGrid CurrentQuantGrid() const { return quantGrid_; }

	// --- loop length --------------------------------------------------------

	/// How many beats the loop plays before wrapping, 4..kBeatsPerLoop.
	///
	/// A PLAYBACK setting, and that distinction is the whole design. Events
	/// are always recorded and stored against the full sixteen beats; this
	/// only moves where the playhead wraps. So shortening the loop is
	/// non-destructive — the hits past the new end are still there, silent,
	/// and lengthening brings them back exactly as they were.
	///
	/// The click on Pulse Out 2 is unstressed and the card never marks beat
	/// one, so a fourteen-beat loop is 7/4 + 7/4, or 5/4 + 2/4 + 3/4 + 4/4,
	/// or anything else the listener hears in it. That ambiguity is the
	/// feature: odd lengths are usable without the card imposing a metre.
	///
	/// CAVEAT, and it is worth saying out loud rather than hiding: since
	/// nothing marks beat one, changing the length mid-performance moves the
	/// wrap to an arbitrary point in what you recorded. The truncated part
	/// (or the silent gap a longer setting exposes) can land anywhere in the
	/// phrase. That is inherent to a loop with no downbeat, not a bug.
	void SetLoopBeats(int beats);

	int  LoopBeats() const { return loopTicks_ / kTicksPerBeat; }

	/// The live loop length in TICKS. Everything that wraps the playhead uses
	/// this; everything that stores an event uses kLoopTicks.
	uint16_t LoopTicks() const { return loopTicks_; }

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

	/// How many AUDIO SAMPLES one beat currently lasts.
	///
	/// For anything that must be a musical LENGTH rather than a moment — the
	/// glitch gates on CV Out 1/2, whose width is how long a random effect is
	/// applied for. Derived from tickInc_ so it follows an external clock as
	/// readily as the tempo knob.
	///
	///     tickInc_ is Q16 loop-ticks per control step, so
	///     control steps per beat = (kTicksPerBeat << 16) / tickInc_
	///     and one control step is kCtrlDiv samples.
	///
	/// The Q16 scale is written as a shift rather than fastmath.h's kQ16One,
	/// so this header does not gain an include for one constant.
	///
	/// Falls back to a 120bpm beat before any tempo has been set, so a caller
	/// never divides by zero or gets an absurd length on the first tick.
	/// CACHED, and that is a performance fix rather than a style choice.
	///
	/// The expression below is a 64-BIT DIVISION, which on a Cortex-M0+ is a
	/// call into __aeabi_ldivmod — there is no divide instruction, so it costs
	/// hundreds of cycles. GlitchTick() calls this every control tick, and the
	/// profiler found the control tick at 354% of its budget with this and the
	/// modulos below as the bulk of it.
	///
	/// tickInc_ only changes when the tempo does (SetTempo/ClockPulse), so the
	/// division is done there instead and this becomes a load. Any new writer
	/// of tickInc_ MUST call RecacheTempo() — hence the single private helper
	/// rather than assignments scattered about.
	int32_t SamplesPerBeat() const { return samplesPerBeat_; }

private:
	void Insert(const LoopEvent &ev);
	void Remove(int i);
	void RecordLane(uint8_t lane, int32_t knob);
	bool NearPlayhead(uint16_t tick) const;

	/// Snap a raw tick to the CURRENT grid. Called once, from RecordHit() —
	/// see QuantGrid's comment for why this is capture-time, not playback.
	uint16_t QuantiseTick(uint16_t tick) const;

	/// Re-point cursor_ at the first event due at or after the playhead, after
	/// events_ has been REPLACED wholesale (Undo, RecallPattern).
	///
	/// The walk is the obvious part. The wrap is not, and leaving it out is a
	/// bug that was shipped: if the new array is SHORTER than the playhead's
	/// position — undoing a pass that added events behind the playhead, or
	/// recalling a sparser pattern mid-bar — the walk runs off the end and
	/// leaves cursor_ == count_. Fire() only ever moves the cursor forward and
	/// Advance() only resets it when playHead_ hits 0, so nothing sounds again
	/// until the loop wraps. On the bench that is "undo silences the loop
	/// until the start of the next pass".
	///
	/// Landing on 0 instead is right rather than merely non-silent: the array
	/// is a RING sorted by fire time, so "no event left this pass" and "the
	/// next event is the first one, next pass" are the same statement.
	void RebuildCursor();

	/// The tick at which an event actually sounds. For a drum hit this is
	/// just ev.tick — quantisation already happened once, at RecordHit()
	/// time (see QuantiseTick()) — so despite the name this no longer
	/// re-derives anything for hits. Kept as its own function because the
	/// array is sorted by THIS, not by the raw stored tick, and every call
	/// site already goes through it rather than reading ev.tick directly —
	/// see the comment in Insert().
	uint16_t FireTick(const LoopEvent &ev) const;

	LoopEvent events_[kMaxEvents] = {};
	uint16_t  count_       = 0;
	uint16_t  knobCount_ = 0;     ///< how many of count_ are automation
	uint16_t  playHead_ = 0;
	uint16_t  cursor_   = 0;    ///< index of the next event at or after playHead_

	/// The live playback quantise grid. See CycleQuantGrid()/QuantGrid.
	QuantGrid quantGrid_ = QuantGrid::k16th;

	/// Where the playhead wraps. Defaults to the full four bars, so a card
	/// that is never told otherwise behaves exactly as it always has.
	/// See SetLoopBeats() for why this is playback-only.
	uint16_t  loopTicks_ = kLoopTicks;

	// --- undo, depth 1 ----------------------------------------------------
	// 2KB, the same size as events_ itself. Worth it for one level; a stack
	// would not be. See Snapshot().
	LoopEvent snapshot_[kMaxEvents] = {};
	uint16_t  snapshotCount_    = 0;
	uint16_t  snapshotKnobCount_ = 0;
	bool      haveSnapshot_     = false;

	// --- pattern slots ----------------------------------------------------
	// 8KB. Flat arrays rather than anything clever: a pattern is exactly what
	// events_ is, so storing one is a copy and recalling it is a copy back.
	LoopEvent pattern_[kNumPatterns][kMaxEvents] = {};
	uint16_t  patternCount_[kNumPatterns] = {};
	uint16_t  patternKnobCount_[kNumPatterns] = {};

	/// Recompute samplesPerBeat_ from tickInc_. The ONLY place the 64-bit
	/// division happens; called from every writer of tickInc_.
	void RecacheTempo()
	{
		samplesPerBeat_ = (tickInc_ <= 0)
			? (kSampleRate / 2)                       // 120bpm fallback
			: static_cast<int32_t>(
				((static_cast<int64_t>(kTicksPerBeat) << 16) * kCtrlDiv) / tickInc_);
	}

	int32_t  phase_   = 0;      ///< Q16 fraction of a tick
	int32_t  tickInc_ = 0;      ///< Q16 ticks per control step
	int32_t  samplesPerBeat_ = kSampleRate / 2;   ///< cached; see SamplesPerBeat()
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

} // namespace nko
