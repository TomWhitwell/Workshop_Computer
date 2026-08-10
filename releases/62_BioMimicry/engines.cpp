// engines.cpp — the five physics models.
//
// All five run at kCtrlRate (1500Hz). Integer-only; see fastmath.h for the
// Q15/Q16/phase conventions. Every engine fills EngineOut::state[] with a
// continuous 0..65536 view of its internals, which the Summed routing mode
// puts out as CV.

#include "engines.h"
#include "pico.h"   // __not_in_flash_func

namespace bio {

// hz_to_inc()'s constant is precomputed for this control rate. If kCtrlDiv
// changes, kHzToIncQ16 in fastmath.h must be recomputed as
// round(2^32 / (256 * kCtrlRate) * 65536).
static_assert(kCtrlRate == 1500,
              "kHzToIncQ16 in fastmath.h is precomputed for kCtrlRate == 1500");

// ===========================================================================
// Mode 1 — Horses: phase-drifting clocks + gaits
// ===========================================================================

// Leg-length offsets. Deliberately close to 1.0 and mutually irrational-ish so
// the four clocks take a long time to realign — that slow drift is the sound.
// Real gaits are PHASE-LOCKED: a trotting horse's diagonal pair stays a pair,
// and that lock is what makes it a trot. So ONE master clock per stride drives
// all four hooves, each landing at a fixed offset within the stride. Leg
// asymmetry becomes small per-hoof jitter on top — not four free-running clocks,
// which pulled every gait apart within seconds.
//
// Agent order is the standard equine convention:
//   0 = LH (left hind)    1 = LF (left fore)
//   2 = RH (right hind)   3 = RF (right fore)
//
// Each entry is the fraction of the stride at which that hoof LANDS, Q16
// (65536 = one full stride). A hoof fires when (stride - offset) wraps, so the
// numbers below read directly as footfall times — 0 is the start of the stride.
static const int32_t kGaitOffset[4][kNumAgents] = {
	//                LH       LF       RH       RF
	// WALK — 4-beat lateral, evenly spaced. The two legs of ONE SIDE follow each
	// other: LH, LF, RH, RF. (The original table fired both forelegs back to
	// back, which no horse does.)
	//   LH 0 · LF 0.25 · RH 0.50 · RF 0.75
	{                  0,   16384,   32768,   49152 },

	// TROT — 2-beat diagonal, LH+RF then LF+RH half a stride apart. The two feet
	// of a pair are offset by a small FLAM (see kFlam below) rather than landing
	// on the same sample: a real diagonal pair is not perfectly simultaneous,
	// and two identical clops at the same instant sum into one louder clop
	// instead of two events — which made a trot read as half-density.
	{                  0,   32768,   32768,       0 },

	// CANTER — 3-beat with suspension, on the right lead: leading hind, then the
	// diagonal pair, then the leading fore, then all four airborne. The beats
	// CLUSTER into the first ~45% and leave a real float; even spacing is what
	// made an earlier version sound like a waltz instead of a canter.
	//   LH 0 · RH+LF 0.22 · RF 0.44 · float 0.44-1.0
	{                  0,   14418,   14418,   28836 },

	// GALLOP — 4-beat rotary with a long suspension. The two HINDS drive first,
	// then the forelegs catch the fall, then all four are airborne. Hinds before
	// fores is exactly what separates a gallop from a fast walk.
	//   LH 0 · RH 0.10 · LF 0.21 · RF 0.31 · float 0.31-1.0
	{                  0,   13763,    6554,   20315 }
};

// FLAM between the feet of a "simultaneous" pair, in control ticks (1.5kHz, so
// one tick is 0.67ms). Only trot and canter have coincident landings, and the
// second foot of each pair is nudged late by these amounts.
//
// Two identical clops fired on the same sample do not sound like two hooves —
// they sum into one louder clop, so a trot audibly halved in density compared
// with a walk. Real diagonal pairs land 10-30ms apart, and restoring that gap
// puts the missing events back. Given as a FIXED time rather than a fraction of
// the stride, because the flam of a real animal does not stretch with tempo.
static const int32_t kFlamTicks[4][kNumAgents] = {
	//  LH   LF   RH   RF
	{     0,   0,   0,   0 },   // Walk   — already four distinct landings
	{     0,  27,   0,  18 },   // Trot   — LF 18ms after RH, RF 12ms after LH
	{     0,  24,   0,   0 },   // Canter — LF lands just after RH in the pair
	{     0,   0,   0,   0 }    // Gallop — already four distinct landings
};

// Stride rate range per gait, Q8 Hz. A real horse doesn't just do the same
// tempo faster — each gait occupies its own band, and a gallop stride is only
// ~2.5x a walk stride even though it covers far more ground.
static const int32_t kGaitHzMin[4] = {  115,  205,  300,  380 }; // 0.45..1.5 Hz
static const int32_t kGaitHzMax[4] = {  205,  330,  420,  640 }; // 0.8 ..2.5 Hz

// Per-horse speed offsets. Real animals travelling together never quite match
// pace, so the herd continuously slides in and out of step — that slow phasing
// between whole gaits is the sound the mode is after, and it is what the old
// per-LEG drift was trying (and failing) to do.
static const int32_t kHorseSpeed[kNumAgents] = {
	65536,        // 1.000x
	63700,        // 0.972x
	67600,        // 1.031x
	62000         // 0.946x
};

void HorsesEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	lastGait_ = 0xFF;
	herdSync_ = 0;
	for (int h = 0; h < kNumAgents; h++) hoofEnv_[h] = 0;
	for (int h = 0; h < kNumAgents; h++)
	{
		// Stagger the herd's starting positions so they don't begin in unison.
		stride_[h] = static_cast<uint32_t>(h) * 0x30000000u;
		speed_[h] = kHorseSpeed[h];
		for (int i = 0; i < kNumAgents; i++)
		{
			lastStep_[h][i] = 0xFF;
			jitter_[h][i] = 0;
			pending_[h][i] = 0;
		}
	}
}

void __not_in_flash_func(HorsesEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	// Pulse In = the herd startles into step: every horse resets to the top of
	// its stride at once, so they land together and then drift apart again.
	if (c.spook)
		for (int h = 0; h < kNumAgents; h++)
		{
			stride_[h] = 0;
			for (int i = 0; i < kNumAgents; i++) lastStep_[h][i] = 0xFF;
		}

	// Knob Main picks the gait (quadrant) and the stride rate within it.
	int gait = c.physics >> 14;               // 0..3
	if (gait > 3) gait = 3;

	// Changing gait re-rolls the step tracking so the new gait starts clean.
	if (gait != lastGait_)
	{
		lastGait_ = static_cast<uint8_t>(gait);
		for (int h = 0; h < kNumAgents; h++)
			for (int i = 0; i < kNumAgents; i++) lastStep_[h][i] = 0xFF;
	}

	// Each gait has its own stride-rate band; the knob sweeps within it.
	int32_t within = (c.physics & 0x3FFF) << 2;              // 0..65535 in-band
	int32_t hz_q8  = kGaitHzMin[gait] +
		mul_q16(within, kGaitHzMax[gait] - kGaitHzMin[gait]);

	// Pulse In 2 entrains the herd: every horse falls in step with the clock.
	if (c.clock && c.clockPeriod > 0)
	{
		int32_t clockHz_q8 = (256 * kCtrlRate) / c.clockPeriod;
		if (clockHz_q8 > 32) hz_q8 = clockHz_q8;
		for (int h = 0; h < kNumAgents; h++)
		{
			stride_[h] = 0;
			for (int i = 0; i < kNumAgents; i++) lastStep_[h][i] = 0xFF;
		}
	}

	uint32_t baseInc = hz_to_inc(hz_q8);

	// One horse per output channel. Each keeps ALL FOUR hooves — a horse with
	// three legs is not a smaller herd — and runs its own stride clock, so the
	// animals phase against each other while every individual gait stays intact.
	for (int h = 0; h < kNumAgents; h++)
	{
		if (h >= c.population) { out.state[h] = 0; continue; }

		uint32_t inc = static_cast<uint32_t>(
			(static_cast<int64_t>(baseInc) * speed_[h]) >> 16);
		stride_[h] += inc;

		for (int i = 0; i < kNumAgents; i++)
		{
			// Where this hoof sits within its horse's stride. Subtracting the
			// offset means a hoof with offset 0.25 wraps a quarter of a stride
			// AFTER the stride start — so the table reads as landing times.
			uint32_t hoofPhase = stride_[h] -
				(static_cast<uint32_t>(kGaitOffset[gait][i]) << 16);

			// Chaos = per-hoof timing jitter, a real animal's unevenness.
			// Applied as a phase offset rather than a rate change, so legs keep
			// their gait relationship instead of drifting out of it.
			if (c.chaos > 0)
			{
				jitter_[h][i] = slew(jitter_[h][i],
					mul_q16(rand_bipolar(rng_), c.chaos), 5);
				hoofPhase += static_cast<uint32_t>(jitter_[h][i] << 10);
			}

			// A hoof lands when its phase wraps past the top of the stride.
			// Tracked in 16ths so the fine canter/gallop offsets resolve.
			uint8_t step = static_cast<uint8_t>(hoofPhase >> 28);   // 0..15
			if (step == 0 && lastStep_[h][i] != 0)
			{
				int32_t flam = kFlamTicks[gait][i];
				if (flam == 0)
				{
					out.triggers |= (1 << h);
					// Tell the voice WHICH hoof this was, so it plays that
					// hoof's own sound — hinds lower and heavier than fores.
					out.member[h] = static_cast<uint8_t>(i);
				}
				else
				{
					// Second foot of a pair: hold it back a few ticks so the
					// two land as two audible events rather than one loud one.
					pending_[h][i] = static_cast<int16_t>(flam);
				}
			}
			lastStep_[h][i] = step;

			// Release any hoof whose flam delay has run out.
			if (pending_[h][i] > 0 && --pending_[h][i] == 0)
			{
				out.triggers |= (1 << h);
				out.member[h] = static_cast<uint8_t>(i);
			}
		}

		// State CV: this horse's FOOTFALL ACTIVITY, up on each hoof landing and
		// decaying between.
		//
		// It used to be the raw stride phase, a sawtooth — every other engine
		// puts a level here, so a patch reading CV 1 as modulation depth got an
		// LFO from this one mode. The stride ramp is genuinely useful, but it is
		// not the same KIND of signal, and consistency across the six modes is
		// worth more than one mode's extra trick.
		if (out.triggers & (1 << h)) hoofEnv_[h] = kQ16One;
		else hoofEnv_[h] = fast_exp_decay(hoofEnv_[h], 5);
		out.state[h] = hoofEnv_[h];
	}

	// Global: HERD SYNC — how closely the animals are in step, 65536 = moving as
	// one, 0 = evenly scattered around the stride.
	//
	// This used to be the lead horse's raw stride phase, which is a sawtooth: a
	// ramp on a CV out that carries a LEVEL in every other mode, and the reason
	// CV 2 was seen ramping. The stride LFO is still available per-animal on
	// state[], where it is deliberate and useful.
	//
	// Same order-parameter idea Frogs uses, on the same fast_sin LUT: sum the
	// unit vectors of each stride phase and take the length of the mean. Four
	// agents at the control rate, so the cost is negligible.
	{
		int32_t sumSin = 0, sumCos = 0;
		int n = 0;
		for (int h = 0; h < c.population && h < kNumAgents; h++)
		{
			sumSin += fast_sin(stride_[h]);
			sumCos += fast_sin(stride_[h] + 0x40000000u);   // +90 degrees
			n++;
		}
		int32_t sync = kQ16One;
		if (n > 1)
		{
			int32_t as = (sumSin > 0) ? sumSin : -sumSin;
			int32_t ac = (sumCos > 0) ? sumCos : -sumCos;
			int32_t hi = (as > ac) ? as : ac;
			int32_t lo = (as > ac) ? ac : as;
			// Octagonal |v| ~ max + 3/8*min, avoiding a sqrt. fast_sin is Q15
			// (+/-32767), so the mean is Q15 and one shift takes it to Q16.
			int32_t mag = hi + ((lo * 3) >> 3);
			sync = (mag / n) << 1;
			if (sync > kQ16One) sync = kQ16One;
		}
		// Smoothed: the raw figure jitters as individual hooves land, and this is
		// a CV describing the herd rather than a per-step readout.
		herdSync_ = slew(herdSync_, sync, 4);
		out.global = herdSync_;
	}
}

// ===========================================================================
// Mode 2 — Geese: stochastic contagion
// ===========================================================================

// Refractory period after a honk: no re-fire for ~170ms. Without this the mutual
// excitation would latch into a continuous tone instead of a cascade. It also
// sets the hard ceiling on how fast one bird can honk — and since three birds
// share each output channel, that ceiling matters three times over.
static constexpr uint16_t kGooseRefractory = kCtrlRate / 6;  // ~250 ticks

void GeeseEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	for (int i = 0; i < kSwarmSize; i++) { excite_[i] = 0; refractory_[i] = 0; }
}

void __not_in_flash_func(GeeseEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	// The flock is kSwarmSize birds folded onto kNumAgents outputs: with the
	// population knob at 4, all 12 are awake and each output channel carries 3
	// of them. Four birds never sounded like a flock; twelve do.
	int flock = c.population * kSwarmPerAgent;

	// Pulse In = spook the flock: a huge excitation spike into every bird at
	// once, guaranteeing a cascade.
	if (c.spook)
		for (int i = 0; i < flock; i++) excite_[i] = kQ16One;

	// Pulse In 2 = the clock nudges the whole flock, so honks lean toward the
	// beat without being locked to it.
	if (c.clock)
		for (int i = 0; i < flock; i++)
		{
			excite_[i] += kQ16One / 3;
			if (excite_[i] > kQ16One) excite_[i] = kQ16One;
		}

	// Baseline spontaneous spark chance per tick, scaled up as the flock shrinks.
	// A cascade needs someone to honk first; with a full flock a spark somewhere
	// is quick, but with three birds the wait for ignition would stretch to tens
	// of seconds and the mode would read as broken. Chaos raises it further.
	int32_t spark = (2 + (c.chaos >> 12)) * (kNumAgents + 1 - c.population);

	// Geese are the opposite of shy: a loud room agitates them, raising the
	// chance any bird sets the flock off. The card answers back to the patch.
	//
	// 48 against a baseline of 2-18 at full population: a loud patch multiplies
	// the spontaneous rate several times over, and since every spark can ignite
	// a cascade the audible effect is larger than the number suggests. Like the
	// Cicadas figure this had never actually been heard - nothing published the
	// envelope across the cores until now - so it is a first tuning, not a
	// retune.
	if (c.loudness > 0) spark += mul_q16(c.loudness, 48);

	// Contagion strength from Knob Main. Mildly shaped (x^1.5-ish via a blend of
	// linear and squared) so the knob keeps resolution down low without going
	// dead across the middle of its travel.
	int32_t contagion = (c.physics + mul_q16(c.physics, c.physics)) >> 1;

	// How much one honk raises every other bird's excitement. Small, because in
	// a flock of twelve each honk lands on eleven neighbours — the cascade comes
	// from many birds accumulating, not from any single honk being decisive.
	constexpr int32_t nudge = kQ16One / 16;

	uint32_t fired = 0;
	for (int i = 0; i < flock; i++)
	{
		if (refractory_[i] > 0)
		{
			refractory_[i]--;
		}
		else
		{
			// Probability = baseline spark + whatever excitation the flock has
			// pushed into this bird. The >>6 is what keeps the contagion knob
			// GRADUAL: at a smaller shift, any working contagion drove every
			// bird straight to its refractory ceiling and the knob became a
			// switch between silence and a solid 16Hz buzz.
			int32_t p = spark + (mul_q16(excite_[i], contagion) >> 6);
			if (rand_q16(rng_) < p)
			{
				fired |= (1u << i);
				refractory_[i] = kGooseRefractory;
			}
		}
	}

	// A bird that honked excites all the *others* — never itself, or it would
	// just retrigger the moment its refractory period ended.
	if (fired)
	{
		for (int i = 0; i < flock; i++)
		{
			if (!(fired & (1u << i))) continue;
			for (int j = 0; j < flock; j++)
			{
				if (j == i) continue;
				// A partial nudge, not a slam to full scale: excitement has to
				// ACCUMULATE across several honks to tip the flock over. That
				// build-up is what makes the cascade audible as a cascade.
				excite_[j] += nudge;
				if (excite_[j] > kQ16One) excite_[j] = kQ16One;
			}
		}
	}

	// Fold the flock down onto the output channels: a channel fires if any of
	// its birds did, and carries the mean excitation of its group as state CV.
	out.triggers = 0;
	for (int a = 0; a < kNumAgents; a++)
	{
		if (a >= c.population) { out.state[a] = 0; continue; }
		int32_t groupExcite = 0;
		for (int k = 0; k < kSwarmPerAgent; k++)
		{
			int i = a * kSwarmPerAgent + k;
			if (fired & (1u << i)) out.triggers |= (1 << a);
			groupExcite += excite_[i];
		}
		// Left as a real divide: it runs 4 times per tick, not 12, and the
		// exact reciprocal for this range needs a 64-bit intermediate, which
		// would cost more than the divide it replaces.
		out.state[a] = groupExcite / kSwarmPerAgent;
	}

	// Excitation decays over roughly a second — slow enough that honks from
	// several birds ACCUMULATE into a cascade, fast enough that the flock
	// settles back to quiet between outbursts. (At the old faster decay the
	// excitation drained before it could ever build, so the contagion knob had
	// almost no effect across its range.)
	int32_t sum = 0;
	for (int i = 0; i < flock; i++)
	{
		excite_[i] = fast_exp_decay(excite_[i], 9);
		sum += excite_[i];
	}
	out.global = (flock > 0) ? (sum / flock) : 0;   // mean flock agitation
}

// ===========================================================================
// Mode 3 — Frogs: Kuramoto coupled oscillators
// ===========================================================================
//
//   dTheta_i/dt = omega_i + (K/N) * sum_j sin(Theta_j - Theta_i)
//
// Phases live in uint32_t so the wrap is free and sin() is a table lookup.

void FrogsEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	clockPhase_ = 0;
	for (int i = 0; i < kNumAgents; i++) croak_[i] = 0;
	for (int i = 0; i < kSwarmSize; i++)
	{
		phase_[i] = xorshift32(rng_);
		natural_[i] = 0;
	}
}

void __not_in_flash_func(FrogsEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	// Pulse In = splash: scramble every phase. Sync shatters, then rebuilds at
	// whatever rate K allows — the most satisfying gesture on the card.
	if (c.spook)
		for (int i = 0; i < kSwarmSize; i++) phase_[i] = xorshift32(rng_);

	// Base chorus rate. The knob here is coupling, so tempo comes from a fixed
	// base plus the chaos spread.
	constexpr int32_t kBaseHz_q8 = 400;   // ~1.56 Hz
	int32_t baseHz_q8 = kBaseHz_q8;

	// Pulse In 2 entrains the chorus to an external clock. This is the most
	// musically useful thing on the card: the frogs pull toward YOUR tempo with
	// exactly the coupling strength Knob Main is set to, so you can dial
	// anywhere from locked-to-the-clock to completely indifferent to it.
	if (c.clockPeriod > 0)
	{
		int32_t clockHz_q8 = (256 * kCtrlRate) / c.clockPeriod;
		if (clockHz_q8 > 32 && clockHz_q8 < 8192) baseHz_q8 = clockHz_q8;
	}

	int32_t baseInc = static_cast<int32_t>(hz_to_inc(baseHz_q8));

	int32_t K;
	// Knob Main INVERSELY controls coupling: CCW = strong K (locked chorus),
	// CW = weak K (every frog for itself).
	//
	// GEOMETRIC, not cubed. Simulating the order parameter against K shows the
	// pond is fully locked above K~0.25 and fully scattered below K~0.03: the
	// entire audible transition lives in less than one octave of K, and
	// everything outside it sounds the same.
	//
	// The cube that used to be here was reasoned backwards. It claimed to push
	// the transition into the middle of the sweep; it actually spent HALF the
	// travel above K=0.125 - all of it locked - and crammed the interesting part
	// between 0.5 and 0.75. That is why the knob felt uniform: most of it was.
	//
	// A geometric sweep from 0.40 down to 0.004 puts equal knob travel into equal
	// RATIOS of K, so the knee lands mid-sweep and stays playable:
	//
	//   knob   0.0    0.2    0.4    0.6    0.8    1.0
	//   K      0.400  0.159  0.063  0.025  0.010  0.004
	//   order  1.00   0.99   0.54   0.26   0.18   0.15
	//
	// Done as a 9-entry table with linear interpolation between points: this is
	// the 1.5kHz control tick, and an exp() here would be both a float and a libm
	// call. The table is Q16 K values on a geometric grid.
	static const int32_t kCouple[9] = {
		26214, 14741,  8290,  4662,  2621,  1474,   829,   466,   262
	};
	{
		int32_t x = c.physics;
		if (x < 0) x = 0;
		if (x > kQ16One) x = kQ16One;
		int32_t idx = (x * 8) >> 16;            // which segment, 0..8
		if (idx > 7) idx = 7;
		int32_t frac = (x * 8) - (idx << 16);   // Q16 position within it
		K = kCouple[idx] + mul_q16(kCouple[idx + 1] - kCouple[idx], frac);
	}

	int n = c.population * kSwarmPerAgent;
	if (n < 1) n = 1;

	// Chaos spreads the natural frequencies apart, making sync harder to reach.
	// Twelve fixed detunes, deliberately uneven so the pond has no symmetry.
	static const int32_t kDetune[kSwarmSize] = {
		     0,  3000, -2200,  5000, -4100,  1800,
		  6200, -3400,   900, -5600,  4400, -1200
	};
	for (int i = 0; i < n; i++)
		natural_[i] = baseInc + ((baseInc >> 4) * mul_q16(kDetune[i], c.chaos) >> 12);

	// The external clock joins the pond as a phantom frog that never listens to
	// anyone. Matching its RATE alone would leave the chorus running at the right
	// tempo but landing anywhere in the bar; coupling to its PHASE is what makes
	// the frogs actually arrive on the beat. Its pull is the same K as everyone
	// else's, so Knob Main dials the whole range from locked-to-clock to
	// completely indifferent.
	bool haveClock = (c.clockPeriod > 0);
	if (haveClock)
	{
		clockPhase_ += static_cast<uint32_t>(baseInc);
		if (c.clock) clockPhase_ = 0;   // a pulse re-anchors the downbeat
	}

	// MEAN FIELD. The textbook Kuramoto sum is O(n^2) — 132 sine lookups for a
	// pond of twelve — but the angle-difference identity
	//     sin(Oj - Oi) = sin(Oj)cos(Oi) - cos(Oj)sin(Oi)
	// lets the sum over j factor into two running totals computed ONCE:
	//     sum_j sin(Oj - Oi) = cos(Oi)*sum(sin Oj) - sin(Oi)*sum(cos Oj)
	// That makes the whole thing O(n) and numerically identical (verified to
	// 0.006% against the direct sum), which is what lets the pond hold twelve.
	int32_t sumSin = 0, sumCos = 0;
	for (int i = 0; i < n; i++)
	{
		sumSin += fast_sin(phase_[i]);
		sumCos += fast_sin(phase_[i] + 0x40000000u);   // cos = sin(x + 90deg)
	}
	int partners = n;
	if (haveClock)
	{
		sumSin += fast_sin(clockPhase_);
		sumCos += fast_sin(clockPhase_ + 0x40000000u);
		partners++;
	}

	uint32_t fired = 0;
	for (int i = 0; i < n; i++)
	{
		// Coupling term: pull toward the pond's mean phase.
		// sin(theta_j - theta_i) is positive when j is ahead, so a lagging frog
		// speeds up and a leading one slows down — that is the whole model.
		int32_t si = fast_sin(phase_[i]);
		int32_t ci = fast_sin(phase_[i] + 0x40000000u);
		// The sums include this frog; subtract its own terms so it does not
		// couple to itself.
		int32_t coupling =
			(((sumSin - si) * ci) - ((sumCos - ci) * si)) >> 15;
		if (partners > 1) coupling /= (partners - 1);

		// Modulate this frog's RATE by up to +/-100% at full K. That much
		// authority is what actually drags the chorus into lock; the old
		// version could only bend the rate ~2% and never synchronised.
		int32_t adjust = static_cast<int32_t>(
			(static_cast<int64_t>(natural_[i]) * mul_q15(coupling, K >> 1)) >> 16);

		uint32_t before = phase_[i];
		phase_[i] += static_cast<uint32_t>(natural_[i] + adjust);

		// Wrap = one full croak cycle completed.
		if (phase_[i] < before) fired |= (1u << i);
	}

	// Fold the pond onto the outputs: a channel croaks if any of its frogs did,
	// and carries that group's lead frog's phase as state CV.
	for (int a = 0; a < kNumAgents; a++)
	{
		if (a >= c.population) { out.state[a] = 0; croak_[a] = 0; continue; }
		bool any = false;
		for (int k = 0; k < kSwarmPerAgent; k++)
			if (fired & (1u << (a * kSwarmPerAgent + k)))
				{ out.triggers |= (1 << a); any = true; }

		// CROAK envelope, not the lead frog's phase. The phase was a sawtooth
		// that told you nothing about the pond; this rises when the group calls
		// and falls between, so the CV describes how vocal that group is - the
		// same KIND of quantity every other engine puts in state[].
		if (any) croak_[a] = kQ16One;
		else     croak_[a] = fast_exp_decay(croak_[a], 5);
		out.state[a] = croak_[a];
	}

	// Global = the Kuramoto order parameter, which the mean-field sums give us
	// for free: the length of the mean phase vector, 65536 = perfectly locked,
	// 0 = evenly scattered. Uses the octagonal |v| ~ max + 3/8*min approximation
	// rather than a sqrt. This is the most informative CV on the card.
	int32_t as = (sumSin > 0) ? sumSin : -sumSin;
	int32_t ac = (sumCos > 0) ? sumCos : -sumCos;
	int32_t hi = (as > ac) ? as : ac;
	int32_t lo = (as > ac) ? ac : as;
	int32_t mag = hi + ((lo * 3) >> 3);
	int32_t coh = (partners > 0) ? (mag * 2 / partners) : 0;
	out.global = (coh > kQ16One) ? kQ16One : coh;
}

// ===========================================================================
// Mode 4 — Rain: leaky integrate-and-fire
// ===========================================================================

// Staggered thresholds. If all four matched, identical noise statistics would
// make them fire in lockstep; the spread is what makes it a drum circle.
static const int32_t kRainThreshold[kNumAgents] = {
	kQ16One, (kQ16One * 115) / 100, (kQ16One * 88) / 100, (kQ16One * 103) / 100
};
// ...and staggered leak offsets, added to the Knob Y leak rate (higher shift =
// slower drain, so a bucket that leaks less fills sooner).
static const int8_t kRainLeakBias[kNumAgents] = { 0, -1, 1, 0 };

// When a bucket overflows it SPLASHES into its neighbours. This is what makes
// Rain more than a Poisson process with extra steps: one drip loads the buckets
// either side, so overflows pull each other along into the rushing-and-dragging
// clusters you hear off a real gutter, then fall apart again.
//
// The splash runs DOWNSTREAM only (i -> i+1), like water finding its way down a
// leaf. A symmetric splash locked all four buckets into firing together within
// seconds, which flattened the mode into a single voice; a one-way cascade
// keeps them staggered while still passing energy along.
static constexpr int32_t kRainSplash = kQ16One / 12;

// Smallest downpour a barely-open knob still admits, Q16. Deliberately tiny: at
// knob zero the mode is silent (no rain is the right answer for no knob), and
// this only stops the first few percent of travel from being a dead band.
static constexpr int32_t kRainFloor = 9000;

// Inflow gain, Q16, ramped across the sweep rather than fixed. A fixed 0.8 made
// the first audible drips arrive faster than one a second, which is not what the
// bottom of a rain knob should do -- you want to hear individual drips land and
// count them. Ramping 0.4 -> 0.8 keeps the onset sparse (~1 drip every 2.5s when
// they first appear) without capping the torrent at the top, which simply
// lowering the gain would have done.
static constexpr int32_t kRainGainMin = (kQ16One * 40) / 100;
static constexpr int32_t kRainGainSpan = (kQ16One * 40) / 100;

void RainEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	for (int i = 0; i < kNumAgents; i++) level_[i] = 0;
}

void __not_in_flash_func(RainEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	// Pulse In = wind gust: dump a big random slug into every bucket, tipping
	// several over the edge at once.
	if (c.spook)
		for (int i = 0; i < kNumAgents; i++)
			level_[i] += (kQ16One >> 1) + (rand_q16(rng_) >> 1);

	// Downpour: how hard it's raining into the buckets.
	//
	// This used to be quadratic-ish, which left the first 40% of the knob
	// completely silent: below a certain inflow the buckets leak as fast as they
	// fill, so nothing ever reaches threshold, and then the mode snapped straight
	// to ~3 drips/sec the moment it crossed. Half the travel did nothing and the
	// onset was a step rather than a fade.
	//
	// Square-rooted instead, so the bottom of the knob moves fastest, with a
	// small floor so a barely-open knob still admits some water.
	int32_t x = c.physics;
	int32_t downpour = kRainFloor + mul_q16(kQ16One - kRainFloor, fast_sqrt_q16(x));
	if (downpour > kQ16One) downpour = kQ16One;

	// Gain rises with the knob so the bottom stays sparse and the top stays full.
	int32_t rainGain = kRainGainMin + mul_q16(kRainGainSpan, x);

	// Pulse In 2 entrains the rain: each clock pulse tops every bucket up a
	// little, so buckets nearest their threshold tip on the beat while the rest
	// keep running free. The rhythm leans on the clock without being quantised
	// to it — the "dragging" stays.
	if (c.clock)
		for (int i = 0; i < kNumAgents; i++)
			level_[i] += kQ16One / 6;

	// Knob Y sets the LEAK rate rather than noise variance. That is the control
	// that actually shapes this model: a slow leak lets buckets accumulate into
	// heavy irregular drips, a fast leak keeps only the strongest bursts alive.
	// (Chaos 0 = shift 10, slow drain; chaos 1 = shift 7, fast drain.)
	int32_t leakBase = 10 - ((c.chaos * 3) >> 16);   // /kQ16One

	int32_t splash[kNumAgents] = { 0, 0, 0, 0 };
	int32_t sum = 0;

	for (int i = 0; i < kNumAgents; i++)
	{
		if (i >= c.population) { out.state[i] = 0; continue; }

		// Noisy inflow, heavy-tailed: cubing the random makes most drops tiny and
		// a few of them large. That tail is what lets a weak downpour still tip a
		// bucket occasionally instead of settling at an equilibrium below the
		// threshold and firing literally never — a uniform drop cannot do it,
		// which is what made the bottom of the knob silent. It is also closer to
		// real dripping, where water gathers for a while and then lets go.
		//
		// rainGain and the >>4 set the ceiling: full downpour lands around 18
		// drips/sec per bucket, a dense torrent that is still a rhythm rather than
		// a wash. Written as Q16 multiplies, not `* 4 / 5` -- the M0+ has no
		// divider and the literal division compiled to an __aeabi_idiv call in
		// the inner loop, four times per control tick.
		int32_t r = rand_q16(rng_);
		int32_t drop = mul_q16(mul_q16(mul_q16(mul_q16(r, r), r), downpour),
		                       rainGain) >> 4;
		if (drop < 0) drop = 0;

		level_[i] += drop;

		int32_t shift = leakBase + kRainLeakBias[i];
		if (shift < 5)  shift = 5;
		if (shift > 12) shift = 12;
		level_[i] = fast_exp_decay(level_[i], static_cast<uint8_t>(shift));

		if (level_[i] >= kRainThreshold[i])
		{
			out.triggers |= (1 << i);
			level_[i] = 0;

			// Splash downstream into the next bucket, collected and applied after
			// the loop so every bucket sees the same instant rather than the
			// low-numbered ones getting a head start. The last bucket spills
			// back to the first, so the cascade wraps around the leaf.
			int next = (i + 1 < c.population) ? (i + 1) : 0;
			if (next != i) splash[next] += kRainSplash;
		}

		// State CV = the bucket filling and emptying: a natural ramp LFO.
		out.state[i] = (level_[i] > kQ16One) ? kQ16One : level_[i];
		sum += out.state[i];
	}

	for (int i = 0; i < c.population; i++) level_[i] += splash[i];

	out.global = sum >> 2;
}

// ===========================================================================
// Mode 5 — Meteors: inhomogeneous Poisson process
// ===========================================================================

// Firing-probability gain, Q16, ramped 0.2 -> 1.0 across Knob Main. The CCW end
// should be a nearly empty sky you wait on, not a patter.
static constexpr int32_t kMeteorGainMin  = kQ16One / 5;
static constexpr int32_t kMeteorGainSpan = kQ16One - kQ16One / 5;

void MeteorsEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	density_ = 0;
	target_ = 0;
	untilNewTarget_ = 1;
}

void __not_in_flash_func(MeteorsEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	// The hidden debris field: a slow random walk, re-aimed every 1-4 seconds
	// and slewed toward smoothly. This is what makes activity arrive in waves
	// rather than at a constant rate.
	if (--untilNewTarget_ == 0)
	{
		target_ = rand_q16(rng_);
		untilNewTarget_ = static_cast<uint16_t>(
			kCtrlRate + (xorshift32(rng_) % (3u * kCtrlRate)));
	}
	density_ = slew(density_, target_, 9);

	// Pulse In = bolide: slam the density to maximum for an immediate burst.
	// It then slews back down to the underlying walk on its own.
	if (c.spook) density_ = kQ16One;

	// Pulse In 2 = a smaller swell on each clock pulse, so the debris field
	// breathes with an external tempo instead of wandering entirely free.
	if (c.clock)
	{
		density_ += kQ16One / 4;
		if (density_ > kQ16One) density_ = kQ16One;
	}

	// Knob Main sets both the floor and how much the walk is allowed to swing.
	// Low: rare isolated hits. Mid: long silences swelling into dense waves.
	// High: locked-high barrage.
	int32_t depth, floorLevel;
	if (c.physics < kQ16One / 2)
	{
		// Lower half: swing grows from nothing, floor stays at zero.
		depth = c.physics * 2;
		floorLevel = 0;
	}
	else
	{
		// Upper half: swing shrinks as the floor rises to a constant barrage.
		int32_t t = (c.physics - kQ16One / 2) * 2;
		depth = kQ16One - t;
		floorLevel = t;
	}

	int32_t effective = floorLevel + mul_q16(density_, depth);

	// Convert the 0..1 density into a per-tick firing probability. Squared for
	// a natural swell, then shifted so even a locked-high field tops out around
	// 12 strikes/sec per voice — a heavy barrage that still reads as discrete
	// meteors rather than a continuous hiss.
	// The extra >>1 accounts for the swarm: kSwarmPerAgent members now roll
	// against this probability for every output channel, so without it the
	// perceived rate would triple against the tuning done at 4 agents.
	// Gain ramps across the knob rather than being constant, so the CCW end can
	// be genuinely sparse -- isolated strikes you wait for -- without capping the
	// barrage at the CW end. A fixed gain gave ~0.5 strikes/sec at zero, which is
	// a steady patter rather than the empty sky the bottom of the knob implies.
	// mul_q16 (64-bit intermediate) is required here, not an optimisation target:
	// these are full-scale Q16 values, and 65536*65536 is exactly 2^31, which
	// overflows a signed 32-bit multiply. Unlike droneRender -- where the inputs
	// are 12-bit audio and 32 bits is provably enough -- the library call stays.
	// It costs six __aeabi_lmul per control tick, not per sample.
	int32_t meteorGain = kMeteorGainMin + mul_q16(kMeteorGainSpan, c.physics);
	int32_t p = mul_q16(mul_q16(effective, effective), meteorGain) >> 8;
	if (c.chaos > 0) p += mul_q16(mul_q16(rand_q16(rng_), c.chaos), meteorGain) >> 11;

	// Like Geese, Meteors runs a swarm folded onto the four outputs. Individual
	// meteors are independent, so a channel strikes if any of its members does —
	// which is what gives a dense field its overlapping, uncountable texture.
	for (int a = 0; a < kNumAgents; a++)
	{
		if (a >= c.population) { out.state[a] = 0; continue; }
		for (int k = 0; k < kSwarmPerAgent; k++)
			if (rand_q16(rng_) < p) { out.triggers |= (1 << a); break; }
		out.state[a] = effective;
	}
	out.global = effective;
}

// ===========================================================================
// Mode 6 — Cicadas: density-dependent chorus
// ===========================================================================
//
// The other modes couple on phase (Frogs) or on excitation events (Geese).
// Cicadas couple on AMPLITUDE: each insect calls faster when the field around
// it is already loud, and the field is just the sum of everyone calling. That
// is a positive feedback loop, so on its own it would saturate — fatigue is
// what turns it into a wave. Insects that have been calling hard get tired,
// drop out, the field quietens, they recover, and it swells again.

void CicadasEngine::reset(uint32_t seed)
{
	rng_ = seed | 1u;
	field_ = 0;
	startle_ = 0;              // or the field opens silent after a mode change
	for (int p = 0; p < kPatches; p++) patchField_[p] = 0;
	for (int i = 0; i < kSwarmSize; i++)
	{
		phase_[i] = xorshift32(rng_);
		fatigue_[i] = 0;
		// Every insect gets its own constitution: how quickly it tires and how
		// long it needs to recover. Fixed per insect rather than re-rolled, so
		// the field has a stable cast of individuals — some that keep going and
		// some that drop out early — instead of twelve identical machines.
		recover_[i] = static_cast<uint8_t>(11 + (xorshift32(rng_) % 3));  // 11..13
		stamina_[i] = static_cast<int32_t>(kQ16One / 2
		            + (xorshift32(rng_) % kQ16One));                      // 0.5..1.5x
		// Spread the natural rates over roughly 0.65x..1.55x. Wide, because a
		// narrow spread still entrains: the shared field steers everyone the
		// same way, and only a real difference in rate keeps them apart.
		tempo_[i] = static_cast<int32_t>((kQ16One * 42) / 64
		          + (xorshift32(rng_) % ((kQ16One * 58) / 64)));
	}
}

void __not_in_flash_func(CicadasEngine::tick)(const Ctrl &c, EngineOut &out)
{
	out.triggers = 0;

	int swarm = c.population * kSwarmPerAgent;

	// Pulse In = a footstep in the grass: the whole field falls silent at once,
	// then creeps back in as the insects regain their nerve. The inverse of the
	// other modes' spooks, and the thing every cicada field actually does.
	//
	// This used to slam fatigue_ to maximum, which was not nearly quiet enough:
	// a fully fatigued insect still calls at a QUARTER rate, because fatigue is
	// meant to thin the field during a swell rather than switch it off. Twelve
	// insects at a quarter rate is still a chorus, so the footstep read as a dip.
	//
	// A startle is its own state, and it silences outright.
	if (c.spook) startle_ = kQ16One;

	// Nerve returns. Slower than the fatigue recovery so the hush is clearly a
	// separate gesture from a swell trough, and slow enough to hear the field
	// creep back in rather than snap on.
	startle_ = fast_exp_decay(startle_, 12);

	// Pulse In 2 nudges the field louder on each beat, so the swells lean
	// toward an external tempo.
	if (c.clock)
		for (int p = 0; p < kPatches; p++)
		{
			patchField_[p] += kQ16One / 4;
			if (patchField_[p] > kQ16One) patchField_[p] = kQ16One;
		}

	// Cicadas are the shyest thing on the card: a loud room shuts them up.
	// Audio In 1's envelope goes straight into fatigue, so the field thins as
	// the rest of the patch gets busy and fills back in when it quietens.
	// A loud room shuts the cicadas up. This scales the call rate DIRECTLY rather
	// than feeding fatigue_, which is where two earlier attempts went wrong.
	//
	// Fatigue is the wrong lever for it. It is driven far harder by calling
	// (0.25 per call) and by ambient field drive (~0.0017/tick) than any loudness
	// term can be without dominating, AND it caps out: `tired` bottoms at 0.25,
	// so even a saturated fatigue only quarters the rate. Every divisor is
	// therefore either inaudible or pinned - /40 pinned it within a quarter
	// second, /4000 sat an order of magnitude below the ambient drive and was
	// inaudible. There was no good value between, because the mechanism is a
	// cliff rather than a control.
	//
	// Scaled here instead, alongside the startle, where it is proportional and
	// can take the field all the way down. Half authority: a full-scale input
	// halves the call rate, which recedes clearly without silencing a mode whose
	// whole character is the drone.
	int32_t hush = kQ16One;
	if (c.loudness > 0) hush = kQ16One - (c.loudness >> 1);

	// Knob Main = COUPLING DEPTH, and it scales both halves of the feedback loop
	// at once: how much the field speeds insects up, and how much being in a
	// loud field tires them out. Scaling both is what makes the knob mean
	// something — with fatigue fixed, the field surged just as hard at zero
	// coupling and the control did nothing.
	//   0.0 = independent insects, a steady even drone
	//   1.0 = the field drives itself into surges that collapse into silence
	int32_t coupling = c.physics;

	// Base call rate, sped up by however loud the field already is. This is the
	// positive half of the loop.
	//
	// The base rate RISES with coupling to compensate for the fatigue that the
	// same knob introduces. Without this the mode simply got quieter as it was
	// turned up: peak activity fell from 7 calls/sec to 2, so a surging field
	// sounded weaker than a steady one. Coupling should redistribute energy
	// into waves, not remove it — the peaks stay loud, the troughs go quiet.
	constexpr int32_t kBaseHz_q8 = 600;   // ~2.3 Hz
	int32_t base = kBaseHz_q8 + mul_q16(kBaseHz_q8 * 5 / 4, coupling);
	// Mean across patches, used only where the whole field matters.
	int32_t globalField = 0;
	for (int p = 0; p < kPatches; p++) globalField += patchField_[p];
	globalField /= kPatches;

	int32_t called[kPatches] = { 0, 0, 0, 0 };
	uint32_t fired = 0;
	int patch = 0, patchCount = -1;
	for (int i = 0; i < swarm; i++)
	{
		// An insect mostly hears its own patch, and only faintly the rest of
		// the field. That weak coupling is what lets patches swell out of step
		// instead of the whole population charging and discharging as one.
		//
		// Walked rather than divided: the M0+ has no divider, and `i / kPerPatch`
		// here cost a libgcc call on every one of the twelve iterations.
		if (++patchCount >= kPerPatch) { patchCount = 0; patch++; }
		int32_t drive = (patchField_[patch] * 3 + globalField) >> 2;

		int32_t rateScale = kQ16One + mul_q16(mul_q16(drive, coupling), kQ16One) * 6;
		int32_t hz_q8 = mul_q16(base << 4, rateScale) >> 4;

		// Fatigue slows an insect right down rather than silencing it outright,
		// so the field thins out gradually instead of switching off.
		int32_t tired = kQ16One - (fatigue_[i] - (fatigue_[i] >> 2));  // *3/4
		// A startle multiplies on top, and unlike fatigue it goes all the way to
		// zero - a footstep stops the field, it does not merely tire it.
		if (startle_ > 0) tired = mul_q16(tired, kQ16One - startle_);
		// Audio In 1's loudness does the same, gently and continuously.
		if (hush < kQ16One) tired = mul_q16(tired, hush);
		// Its own natural rate, then scaled by how tired it is. The per-insect
		// tempo is what stops the twelve locking into unison.
		int32_t myHz = mul_q16(mul_q16(hz_q8, tempo_[i]), tired);

		// Chaos spreads the natural rates so the swarm never sounds like one
		// insect multiplied.
		if (c.chaos > 0)
		{
			int32_t spread = mul_q16(rand_bipolar(rng_) << 1, c.chaos);
			myHz += (myHz >> 3) * spread >> 15;
		}
		if (myHz < 16) myHz = 16;

		uint32_t inc = hz_to_inc(myHz);

		uint32_t before = phase_[i];
		phase_[i] += inc;

		if (phase_[i] < before)          // wrapped: this insect called
		{
			called[patch]++;
			fired |= (1u << i);
			// Calling is work — and some insects tire faster than others.
			fatigue_[i] += mul_q16(mul_q16(kQ16One / 2, coupling), stamina_[i]);
			if (fatigue_[i] > kQ16One) fatigue_[i] = kQ16One;
		}

		// The negative half of the loop: simply BEING in a loud patch is
		// tiring, whether or not this insect called. Without this the swarm
		// spread itself evenly and never surged — collective exhaustion is what
		// turns feedback into a wave rather than a runaway.
		fatigue_[i] += mul_q16(mul_q16(drive, coupling), kQ16One / 150);
		if (fatigue_[i] > kQ16One) fatigue_[i] = kQ16One;

		// Recovery is slow — slower than the build-up, which is what sets the
		// period of the swell. Per-insect, so they do not all come back at once.
		fatigue_[i] = fast_exp_decay(fatigue_[i], recover_[i]);
	}

	// Each patch's field is the perceived loudness of ITS insects: it rises
	// quickly as they join and falls away slowly, so a patch hears its own
	// recent past rather than only the current instant.
	int32_t sumField = 0;
	int activePatches = 0;
	for (int p = 0; p < kPatches; p++)
	{
		if (p * kPerPatch >= swarm) { patchField_[p] = 0; continue; }
		int32_t instant = called[p] * kQ16One * 8 / kPerPatch;
		if (instant > kQ16One) instant = kQ16One;
		patchField_[p] = (instant > patchField_[p])
			? slew(patchField_[p], instant, 4)
			: slew(patchField_[p], instant, 8);
		sumField += patchField_[p];
		activePatches++;
	}
	field_ = activePatches ? (sumField / activePatches) : 0;

	// Fold the swarm onto the outputs: a channel fires if any of its insects
	// called, and carries its group's mean fatigue as state CV.
	for (int a = 0; a < kNumAgents; a++)
	{
		if (a >= c.population) { out.state[a] = 0; continue; }
		int32_t groupFatigue = 0;
		for (int k = 0; k < kSwarmPerAgent; k++)
		{
			int i = a * kSwarmPerAgent + k;
			if (fired & (1u << i)) out.triggers |= (1 << a);
			groupFatigue += fatigue_[i];
		}
		out.state[a] = groupFatigue / kSwarmPerAgent;
	}

	out.global = field_;
}

} // namespace bio
