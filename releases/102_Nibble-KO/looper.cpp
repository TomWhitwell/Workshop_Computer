// looper.cpp — event recording and playback.

#include "looper.h"
#include "fastmath.h"
#include "pico.h"

namespace nko {

namespace {

} // namespace

/// When an event actually sounds.
///
/// For a drum hit this is now just ev.tick: quantisation happens ONCE, at
/// RecordHit() time, against whatever grid was live then — see QuantGrid's
/// comment for why. FireTick() no longer re-derives it, which is what makes
/// that permanent: a hit recorded under 12ths keeps sounding on the 12th grid
/// forever, including after later overdubs are recorded under 8ths, because
/// nothing here ever looks at quantGrid_ for an event that already exists.
///
/// Filter/tone automation was ALREADY exempt — a quantised sweep is a
/// staircase — so this brings drum hits in line with how automation always
/// behaved, rather than introducing a new exemption.
///
/// Note the wrap: QuantiseTick() (called from RecordHit) can round a tick in
/// the last half-step of the loop UP to kLoopTicks, which must land on tick 0
/// of the next pass rather than off the end — handled there, once, at the
/// point of rounding.
uint16_t __not_in_flash_func(Looper::FireTick)(const LoopEvent &ev) const
{
	return ev.tick;
}

/// Round `tick` to the CURRENT grid. The only caller is RecordHit() — this is
/// capture-time snapping, not a playback filter — but it is worth its own
/// function because the wrap needs the same care FireTick() used to take.
uint16_t __not_in_flash_func(Looper::QuantiseTick)(uint16_t tick) const
{
	const int q = kTicksPerBeat / kQuantNotesPerBeat[static_cast<int>(quantGrid_)];
	uint16_t snapped = static_cast<uint16_t>(((tick + q / 2) / q) * q);

	// Wrap against the LIVE length, not the full four bars. A hit played in
	// the last half-step of a shortened loop rounds up to loopTicks_, and a
	// tick equal to the wrap point is one the playhead never reaches — so
	// against kLoopTicks it would survive as an event that can never fire.
	// It belongs on tick 0 of the next pass, exactly as it would at full
	// length.
	return static_cast<uint16_t>(snapped % loopTicks_);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void __not_in_flash_func(Looper::SetTempo)(int32_t xKnob)
{
	// An external clock owns the tempo while it is running.
	//
	// Do NOT track the knob position while clocked. That looks like the helpful
	// thing to do, and it is exactly wrong: when the clock stops, the knob
	// compares equal to its remembered value, reads as "not moved", and the
	// tempo stays wherever the clock left it. The card would sit at the clock's
	// tempo until the knob was physically wiggled.
	//
	// Forgetting the position instead means the first call after the clock
	// releases always recomputes, so the tempo snaps back to whatever the knob
	// is actually showing.
	if (Clocked()) { lastX_ = -9999; return; }

	// Only recompute when the knob has actually moved. See the header.
	if (lastX_ >= 0 && (xKnob - lastX_ < kKnobMoveThresh)
	                && (lastX_ - xKnob < kKnobMoveThresh))
		return;
	lastX_ = xKnob;

	int32_t bpm = kBpmMin + ((xKnob * (kBpmMax - kBpmMin)) >> 12);

	// ticks/sec = bpm/60 * kTicksPerBeat; per control step, in Q16.
	tickInc_ = static_cast<int32_t>(
		(static_cast<int64_t>(bpm) * kTicksPerBeat * kQ16One)
		/ (60 * kCtrlRate));
	RecacheTempo();
}

void __not_in_flash_func(Looper::ClockPulse)()
{
	// The first edge only starts the stopwatch; an interval needs two.
	if (sinceClock_ > 0 && sinceClock_ <= kClockMaxGap)
	{
		clockInterval_ = sinceClock_;

		// One edge is a quarter note, so kTicksPerBeat ticks must elapse in
		// clockInterval_ control steps.
		tickInc_ = static_cast<int32_t>(
			(static_cast<int64_t>(kTicksPerBeat) * kQ16One) / clockInterval_);
		RecacheTempo();

		// Re-align to the nearest beat rather than snapping to it. Jumping the
		// playhead on every edge would stutter the pattern; nudging it keeps
		// the loop phase-locked while staying inaudible.
		int32_t offset = playHead_ % kTicksPerBeat;
		if (offset != 0)
		{
			if (offset > kTicksPerBeat / 2) offset -= kTicksPerBeat;
			// Pull back at most one tick per edge — enough to hold sync against
			// drift, small enough never to be heard as a skip.
			if (offset > 0)      playHead_--;
			else if (offset < 0) playHead_++;
			playHead_ = static_cast<uint16_t>((playHead_ + loopTicks_) % loopTicks_);
		}
	}

	sinceClock_   = 0;
	clockTimeout_ = kClockTimeout;   // hand back to the knob after it stops
}

bool __not_in_flash_func(Looper::Advance)()
{
	// Clock housekeeping FIRST and unconditionally: these are wall-clock
	// timers, not musical ones, and putting them after the early-outs below
	// would freeze them exactly when the tempo is zero or between ticks — so a
	// stopped external clock would never time out and hand control back.
	if (sinceClock_ < kCtrlRate * 8) sinceClock_++;   // saturates, never wraps
	if (clockTimeout_ > 0) clockTimeout_--;

	if (tickInc_ <= 0) return false;

	phase_ += tickInc_;
	if (phase_ < kQ16One) return false;

	// A single step never spans more than one tick at any supported tempo
	// (240bpm is 192 ticks/sec against a 3000Hz control rate), but loop rather
	// than subtract-once so a future faster tempo cannot silently drift.
	while (phase_ >= kQ16One)
	{
		phase_ -= kQ16One;

		// loopTicks_, NOT kLoopTicks: this is where a shortened loop actually
		// takes effect. Events keep their full-length ticks (see
		// SetLoopBeats), so shortening just wraps early and the hits past the
		// new end sit silent until the length is put back.
		playHead_ = static_cast<uint16_t>((playHead_ + 1) % loopTicks_);

		// Latch the beat crossing here, where it happens exactly once, rather
		// than letting the caller poll OnBeat() — that is a level and stays true
		// for the whole tick, which at 40bpm is dozens of control steps and
		// would give a click that is on more than it is off.
		if ((playHead_ % kTicksPerBeat) == 0) beatEdge_ = true;

		// Rewinding the cursor at the loop boundary is what makes playback a
		// walk rather than a search.
		if (playHead_ == 0) cursor_ = 0;
	}

	if (knobCountdown_ > 0) knobCountdown_--;
	return true;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void __not_in_flash_func(Looper::Insert)(const LoopEvent &ev)
{
	if (count_ >= kMaxEvents) return;

	// Keep the array sorted by FIRE time.
	//
	// Since quantisation moved to capture time, FireTick() is just ev.tick and
	// the two are the same thing — but the sort still has to happen, and going
	// through FireTick() rather than reading ev.tick keeps every ordering
	// decision in one place if that ever changes again.
	//
	// The reason this is load-bearing has not gone away, only moved: rounding
	// can push a hit near the end of the loop across the BOUNDARY (a hit at
	// tick 763 quantises up to tick 0 of the next pass), so a hit recorded
	// late in the bar can legitimately sort before one recorded early in it.
	// QuantiseTick() does that wrap now, at RecordHit(), rather than
	// FireTick() doing it on every playback. Get the order wrong and the
	// cursor walk in Fire() double-fires or strands events —
	// tools/loopsim.py caught exactly that as a hit sounding twice per pass.
	//
	// An insertion-sort step: O(n) worst case at 512 entries, but it happens at
	// most a few times a second at control rate and is bounded, so it is
	// affordable even inside the interrupt.
	const uint16_t evWhen = FireTick(ev);
	int i = count_;
	while (i > 0 && FireTick(events_[i - 1]) > evWhen)
	{
		events_[i] = events_[i - 1];
		i--;
	}
	events_[i] = ev;
	count_++;
	if (IsKnobEvent(ev.what)) knobCount_++;

	// Keep the cursor pointing at the same event it was pointing at.
	//
	// Two cases, and getting the second wrong is what made recording DOUBLE
	// every hit. Inserting BEFORE the cursor shifts everything up, so the
	// cursor must move with it. Inserting AT the cursor — which is what a hit
	// played at the current playhead does — must ALSO step it past, or the
	// walk in Fire() lands on the event that was just recorded and plays it
	// back immediately, on top of the live hit the player already heard.
	//
	// That doubling was audible as a flam and, worse, consumed two voices per
	// hit instead of one, so a few overdub passes exhausted the polyphony and
	// the loop appeared to silence itself. An event recorded on this pass must
	// not sound again until the NEXT one.
	if (static_cast<uint16_t>(i) <= cursor_) cursor_++;
}

void __not_in_flash_func(Looper::RecordHit)(int8_t voice, uint8_t velocity)
{
	// Bounded by the VOICE count, not the combo count: an event stores which
	// sound to make, never which buttons made it.
	if (voice < 0 || voice >= kNumVoices) return;

	// If the array is full but automation is hogging it, evict the oldest
	// automation event to make room. A drum hit the player just performed
	// always matters more than a knob position from three passes ago — and
	// silently dropping it is what made the looper feel like it was
	// overwriting things.
	if (count_ >= kMaxEvents && knobCount_ > 0)
	{
		for (int i = 0; i < count_; i++)
			if (IsKnobEvent(events_[i].what)) { Remove(i); break; }
	}

	// Quantised HERE, once, against whatever grid is live right now — not
	// left raw for FireTick() to re-round on every playback. That is the
	// whole difference from how NIBBLE's looper worked, and it is
	// deliberate: quantisation used to be a non-destructive PLAYBACK filter
	// (never touching the stored tick, so it could be changed or removed
	// later with the original timing intact). Making the grid live and
	// cyclable broke that assumption in a way that mattered musically —
	// changing the grid mid-performance must not retroactively move hits
	// already recorded under a different one, or overdubbing a new part at
	// 8ths would silently drag the 12th-quantised part underneath it onto a
	// grid it was never played against. Snapping once at capture is what
	// makes each hit remember its own grid permanently.
	LoopEvent ev;
	ev.tick  = QuantiseTick(playHead_);
	ev.what  = static_cast<uint8_t>(voice);
	ev.value = velocity;
	Insert(ev);
}

void __not_in_flash_func(Looper::RecordKnobs)(bool filterMoving, int32_t filterKnob,
                         bool toneMoving,   int32_t toneKnob,
                         const uint8_t *fxPacked,
                         int8_t parShift, int32_t parKnob, bool parMoving)
{
	// One countdown for ALL lanes, checked once and reset once. Letting each
	// lane test and consume it separately would mean whichever asked first ate
	// the whole interval and the others never recorded at all.
	if (knobCountdown_ > 0) return;

	// RESET TO AN ODD INTERVAL so this stops colliding with Advance().
	//
	// knobCountdown_ is decremented inside Advance(), so it only ever reaches
	// zero on a LOOP TICK — which means the automation scan landed on exactly
	// the tick where Advance() was already firing hits and running GlitchTick.
	// The two most expensive things on the card were synchronised by
	// construction. Profiled at 9051 cycles on one tick (226% of budget), with
	// RecordKnobs 2491 of it, while neighbouring ticks sat near idle.
	//
	// SEVEN, and the value was chosen by arithmetic rather than by feel.
	// gcd(7,48) = gcd(7,768) = 1, so it is coprime with both the beat and the
	// loop and successive samples land on a different phase every time —
	// nine would have shared a factor of 3 with both and repeated every
	// sixteen samples. Seven also samples marginally MORE often than eight, so
	// the recorded curve does not lose resolution to buy the spreading.
	knobCountdown_ = kKnobSampleTicks - 1;


	// Only a knob the player is actually holding writes anything — see the
	// header. A still knob recording its position every tick would flatten an
	// existing sweep into a straight line just by being armed.
	if (filterMoving) RecordLane(kLaneFilter, filterKnob);
	if (toneMoving)   RecordLane(kLaneTone,   toneKnob);

	// The FX lanes write whenever an effect is HELD, not only when something
	// moved — holding one steady is exactly what has to be captured, or
	// playback would start the effect and never stop it. RecordLane takes a
	// 0..4095 knob value and stores value>>4, so the packed byte is scaled up
	// here to survive that round trip intact.
	//
	// Only the lane whose shift is held writes. The rest are left untouched,
	// so an effect recorded under a different shift on an earlier pass
	// survives — which is why there are four lanes rather than one.
	if (fxPacked)
		for (int8_t s = 0; s < kNumSingles; s++)
			if (fxPacked[s] != 0)
				RecordLane(FxLaneForShift(s),
				           static_cast<int32_t>(fxPacked[s]) << 4);

	// The parameter curve follows the KNOB LANE rules: it writes only while
	// the knob is genuinely MOVING.
	//
	// This is not a nicety, and an attempt to relax it had to be reverted.
	// "Write for every tick the shift is held" sounds equivalent and is not,
	// because FOUR VOLTAGES LATCHES: levels_.Current() reports the last
	// voltage indefinitely, so a shift that was tapped once looks held
	// forever. The parameter would then record a flat line for the rest of
	// the pass and flatten every curve underneath it.
	//
	// An effect held steady still has to be recorded every tick — playback
	// would otherwise start it and never stop it — but that lane can afford
	// it: a packed 0 means "no effect" and writes nothing, so a stale
	// latched voltage is silent there in a way a knob POSITION never is.
	if (parShift >= 0 && parShift < kNumSingles && parMoving)
		RecordLane(ParLaneForShift(parShift), parKnob);
}

/// Is `tick` within the replace window of the playhead, the short way round?
///
/// The wrap matters: a sweep recorded across the loop boundary must still
/// replace one recorded just before it, or the seam between passes is exactly
/// where two sweeps survive together.
bool __not_in_flash_func(Looper::NearPlayhead)(uint16_t tick) const
{
	// Against the LIVE length: the seam this exists to handle is where the
	// playhead wraps, which a shortened loop moves earlier.
	const int32_t len = static_cast<int32_t>(loopTicks_);
	int32_t d = static_cast<int32_t>(tick) - static_cast<int32_t>(playHead_);
	if (d >  len / 2) d -= len;
	if (d < -len / 2) d += len;
	if (d < 0) d = -d;
	return d <= kKnobReplaceWindow;
}

void __not_in_flash_func(Looper::RecordLane)(uint8_t lane, int32_t knob)
{
	if (lane >= kNumLanes) return;

	// The caller has already established that this knob is being moved, and by
	// a smoothed, thresholded test — so there is no move detection here. What
	// this used to do was compare against its own remembered value, which meant
	// arming record could itself look like a move and stamp the knob's resting
	// position into the loop.
	lastKnob_[lane] = knob;

	const uint8_t what = static_cast<uint8_t>(kKnobEvent | lane);

	// Automation REPLACES itself rather than accumulating. A knob pass on top of
	// an earlier one should read as "I redid that sweep", not as two sweeps
	// fighting for adjacent ticks.
	//
	// The replace test is a WINDOW, not an exact tick match — see
	// kKnobReplaceWindow. An exact match replaced essentially nothing, because
	// a second pass samples on a different phase from the first and the two
	// grids never coincide.
	//
	// Only THIS lane is touched, so re-recording the filter never disturbs a Y
	// sweep lying underneath it.
	//
	// BOUNDED BY THE WINDOW, not a scan of the whole array.
	//
	// The history here is worth keeping, because two earlier versions were each
	// a real improvement and each still too slow. It began as a Remove() per
	// match, which shifts the tail every time: O(n) scan driving O(n) shifts.
	// That became one linear compacting pass, then the same pass with the loop
	// invariants hoisted into registers (the compiler was reloading playHead_
	// and loopTicks_ from memory on EVERY iteration). Measured across those:
	// 8433 -> 10166 -> 7483 cycles against a 4000-cycle budget.
	//
	// Still ~22 cycles an event, because the work was never the problem: the
	// COUNT of it was. The window is +/-12 ticks and a lane samples once every
	// 8, so at most about four events can ever match — out of 171. Better than
	// 97% of the scan was provably wasted, and no amount of tightening the body
	// fixes that.
	//
	// events_ is sorted by tick, so the window is a CONTIGUOUS span and its
	// start can be found by bisection. The wrap is what made this look
	// impossible: near the loop boundary the span runs off the end of the array
	// and resumes at the front. That is two contiguous spans, not a reason to
	// scan everything — so this walks [lo..n) and, only when the window wraps,
	// [0..hi) as well.
	//
	// Events are moved rather than compacted now: with a bounded span there is
	// no long tail of survivors to slide down, and removing 4 of 171 by
	// memmove beats copying 167 of them.
	{
		const int32_t head = static_cast<int32_t>(playHead_);
		const int32_t len  = static_cast<int32_t>(loopTicks_);
		const int32_t lo   = head - kKnobReplaceWindow;
		const int32_t hi   = head + kKnobReplaceWindow;

		// Bisect for the first event at or after `from`, on the raw tick.
		auto LowerBound = [this](int32_t from) -> int
		{
			int a = 0, b = static_cast<int>(count_);
			while (a < b)
			{
				const int m = (a + b) >> 1;
				if (static_cast<int32_t>(events_[m].tick) < from) a = m + 1;
				else                                             b = m;
			}
			return a;
		};

		// DropSpan removes matching events in [first, lastTick] going forward,
		// stopping as soon as the tick passes the window. Returns how many went.
		auto DropSpan = [&](int first, int32_t lastTick)
		{
			int i = first;
			while (i < static_cast<int>(count_)
			    && static_cast<int32_t>(events_[i].tick) <= lastTick)
			{
				if (SameKind(events_[i].what, what) && !IsThisPass(events_[i].what))
					Remove(i);          // rare: ~4 per call, so the shift is cheap
				else
					i++;
			}
		};

		if (lo >= 0 && hi < len)
		{
			// The common case: the window lies wholly inside the array.
			DropSpan(LowerBound(lo), hi);
		}
		else
		{
			// Wrapped. Two spans: the tail from `lo` to the end, and the head
			// from 0 to `hi`, each reduced into range.
			const int32_t loWrapped = (lo < 0) ? (lo + len) : lo;
			const int32_t hiWrapped = (hi >= len) ? (hi - len) : hi;
			DropSpan(LowerBound(loWrapped), len - 1);
			DropSpan(0, hiWrapped);
		}
	}

	if (knobCount_ >= kMaxKnobEvents) return;

	LoopEvent ev;
	ev.tick  = playHead_;
	ev.what  = static_cast<uint8_t>(what | kThisPass);
	ev.value = static_cast<uint8_t>(knob >> 4);   // 0..4095 -> 0..255
	Insert(ev);
}

/// Remove the event at `i`, keeping the array sorted and the cursor honest.
void __not_in_flash_func(Looper::Remove)(int i)
{
	if (i < 0 || i >= count_) return;
	if (IsKnobEvent(events_[i].what) && knobCount_ > 0) knobCount_--;

	for (int j = i; j + 1 < count_; j++) events_[j] = events_[j + 1];
	count_--;

	// Keep "cursor_ is the next event to fire" true. Removing something the
	// cursor has already passed must pull the cursor back with it, or the rest
	// of this pass fires the wrong events.
	if (static_cast<uint16_t>(i) < cursor_ && cursor_ > 0) cursor_--;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

int __not_in_flash_func(Looper::Fire)(int8_t *outCombo, uint8_t *outVel,
                 int32_t *outKnob, bool *haveKnob)
{
	int n = 0;
	for (int i = 0; i < kNumLanes; i++) haveKnob[i] = false;
	if (count_ == 0) return 0;

	// Walk forward over every event due on EXACTLY this tick. FireTick() is the
	// authority on when that is, and the array is sorted by it — see Insert().
	//
	// An exact match, not `<= playHead_`. A "catch up on anything overdue"
	// condition looks safer and is actually wrong here: an event that fires at
	// tick 0 then matches at every tick until the cursor passes it, so it
	// sounds once late on the first pass and again correctly on the next.
	// tools/loopsim.py caught precisely that as a hit firing twice.
	//
	// Exact matching is safe because Advance() cannot skip a tick: the
	// increment is well under one tick even at the top of the tempo range
	// (240bpm gives 4194 of 65536, and the loop inside Advance() would cover
	// larger steps anyway).
	while (cursor_ < count_)
	{
		const LoopEvent &ev = events_[cursor_];
		if (FireTick(ev) != playHead_) break;

		if (IsKnobEvent(ev.what))
		{
			uint8_t lane = LaneOf(ev.what);
			outKnob[lane]  = static_cast<int32_t>(ev.value) << 4;
			haveKnob[lane] = true;
		}
		else if (n < kMaxFirePerTick)
		{
			outCombo[n] = static_cast<int8_t>(ev.what);
			outVel[n]   = ev.value;
			n++;
		}
		cursor_++;
	}
	return n;
}

void Looper::Clear()
{
	count_      = 0;
	knobCount_  = 0;
	cursor_     = 0;
	for (int i = 0; i < kNumLanes; i++) lastKnob_[i] = -9999;
	// playHead_ and phase_ are left alone: clearing the pattern should not also
	// jump the transport, or an erase mid-performance lurches the timing.
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

void Looper::Snapshot()
{
	// A plain copy of the live array. No allocation, no memcpy call — this
	// runs from the control tick inside the audio interrupt, and 512 four-byte
	// copies is a few hundred cycles, well inside one 20.83us slot even at the
	// worst case where the loop is full.
	for (uint16_t i = 0; i < count_; i++) snapshot_[i] = events_[i];
	snapshotCount_     = count_;
	snapshotKnobCount_ = knobCount_;
	haveSnapshot_      = true;
}

// ---------------------------------------------------------------------------
// Pattern slots
// ---------------------------------------------------------------------------

bool Looper::StorePattern(int i)
{
	if (i < 0 || i >= kNumPatterns) return false;

	// Never overwrite with silence. A store is irreversible — Undo covers the
	// live loop, not the slots — so the one case worth refusing outright is
	// the one that destroys a pattern and leaves nothing in its place.
	if (count_ == 0) return false;

	for (uint16_t k = 0; k < count_; k++) pattern_[i][k] = events_[k];
	patternCount_[i]     = count_;
	patternKnobCount_[i] = knobCount_;
	return true;
}

bool Looper::RecallPattern(int i)
{
	if (i < 0 || i >= kNumPatterns) return false;
	if (patternCount_[i] == 0)      return false;   // empty: say so

	for (uint16_t k = 0; k < patternCount_[i]; k++) events_[k] = pattern_[i][k];
	count_     = patternCount_[i];
	knobCount_ = patternKnobCount_[i];

	// THE PLAYHEAD IS NOT TOUCHED. Switching patterns mid-bar should feel
	// like the band changing part, not like hitting stop and start — so the
	// new pattern picks up wherever the bar already is.
	//
	// The cursor, though, is an index into an array that has just been
	// replaced, so it means nothing now. Rebuild it from the playhead: the
	// array is sorted by fire time, so the first event at or after the
	// playhead is where the walk in Fire() should resume. Resetting it to
	// zero instead would re-fire everything between the loop start and here,
	// all on this one tick — the same trap Undo() has, and worth stating
	// twice because it is silent when wrong.
	//
	// Recalling a SPARSER pattern mid-bar also has to wrap rather than sit at
	// count_, or the loop goes quiet until the next pass — see RebuildCursor().
	RebuildCursor();

	for (int k = 0; k < kNumLanes; k++) lastKnob_[k] = -9999;
	return true;
}

void Looper::SetLoopBeats(int beats)
{
	if (beats < kMinBeatsPerLoop) beats = kMinBeatsPerLoop;
	if (beats > kBeatsPerLoop)    beats = kBeatsPerLoop;

	const uint16_t ticks = static_cast<uint16_t>(beats * kTicksPerBeat);
	if (ticks == loopTicks_) return;

	// ORDER MATTERS, because this is called from CORE 1 (the WebUI) while
	// core 0 is still playing — unlike the pattern slots, which are safe only
	// because the pads are inert in WebUi mode. Both writes are to aligned
	// 16-bit values, so neither tears on this chip; what could bite is the
	// WINDOW BETWEEN them.
	//
	// Fold the playhead FIRST, against the new length, then publish the
	// length. That way core 0 only ever observes (old length, in-range head)
	// or (new length, in-range head). Publishing the length first would leave
	// a tick or two where playHead_ >= loopTicks_, and Advance()'s modulo
	// would not rescue it until it had counted all the way round the OLD
	// length — an audible hang of up to four bars.
	if (playHead_ >= ticks)
		playHead_ = static_cast<uint16_t>(playHead_ % ticks);

	loopTicks_ = ticks;

	// The cursor points into an array whose events are still at full length,
	// but the playhead has just moved — so the walk has to be re-seeded, for
	// the same reason Undo and RecallPattern re-seed it.
	RebuildCursor();
}

void Looper::RebuildCursor()
{
	cursor_ = 0;
	while (cursor_ < count_ && FireTick(events_[cursor_]) < playHead_) cursor_++;

	// Ran off the end: every remaining event is behind the playhead, so the
	// next one due is the first of the next pass. See the header comment —
	// leaving cursor_ == count_ here is what silenced the loop until the wrap.
	if (cursor_ >= count_) cursor_ = 0;
}

bool Looper::GetPatternRaw(int i, LoopEvent *out, uint16_t &count,
                           uint16_t &knobCount) const
{
	if (i < 0 || i >= kNumPatterns || out == nullptr) return false;

	count     = patternCount_[i];
	knobCount = patternKnobCount_[i];
	for (uint16_t k = 0; k < count; k++) out[k] = pattern_[i][k];
	return true;
}

bool Looper::SetPatternRaw(int i, const LoopEvent *in, uint16_t count,
                           uint16_t knobCount)
{
	if (i < 0 || i >= kNumPatterns || in == nullptr) return false;
	if (count > kMaxEvents) return false;

	// This is a TRUST BOUNDARY, unlike StorePattern(): that copies events_,
	// which Insert() has already kept sorted and in range, whereas this takes
	// whatever arrived over USB from a JSON file a user could have edited by
	// hand. RecallPattern() walks the array assuming it is sorted by tick (see
	// FireTick()), so an out-of-order or out-of-range pattern would leave the
	// cursor pointing at the wrong place and fire events at the wrong time —
	// silently, and only once that slot was recalled.
	//
	// Clamping the tick and insertion-sorting on the way in costs a few
	// microseconds in USB mode, where nothing is playing, and makes a bad file
	// merely wrong rather than able to corrupt playback.
	uint16_t n = 0;
	for (uint16_t k = 0; k < count; k++)
	{
		LoopEvent ev = in[k];
		if (ev.tick >= kLoopTicks) ev.tick %= kLoopTicks;

		uint16_t j = n;
		while (j > 0 && pattern_[i][j - 1].tick > ev.tick)
		{
			pattern_[i][j] = pattern_[i][j - 1];
			j--;
		}
		pattern_[i][j] = ev;
		n++;
	}

	patternCount_[i]     = n;
	patternKnobCount_[i] = (knobCount > n) ? n : knobCount;
	return true;
}

bool Looper::Undo()
{
	if (!haveSnapshot_) return false;

	for (uint16_t i = 0; i < snapshotCount_; i++) events_[i] = snapshot_[i];
	count_     = snapshotCount_;
	knobCount_ = snapshotKnobCount_;

	// The cursor is an index into an array that has just been replaced, so
	// whatever it pointed at is meaningless now. Rebuild it from the playhead
	// rather than resetting it to zero: zero would replay everything from the
	// start of the loop on the way to the current position, firing a burst of
	// hits that are not due yet.
	//
	// The array is sorted by FireTick, so the first event at or after the
	// playhead is exactly where the walk in Fire() should resume — and if the
	// undone pass added events BEHIND the playhead, the shortened array runs
	// out and the walk must wrap to 0 rather than strand the cursor at count_.
	// That stranding is what silenced the loop until the next pass.
	RebuildCursor();

	// The knob lanes were tracking values from the undone pass. Drop the
	// references so the next RecordKnobs() re-seeds instead of comparing
	// against a reading that no longer belongs to anything.
	for (int i = 0; i < kNumLanes; i++) lastKnob_[i] = -9999;

	// One-way: consumed, so a second Undo is a no-op rather than a redo.
	haveSnapshot_ = false;
	return true;
}

} // namespace nko
