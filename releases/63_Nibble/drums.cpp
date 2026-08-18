// drums.cpp — the kit and the DJ filter.

#include "drums.h"
#include "fastmath.h"
#include <stdint.h>
#include "pico.h"

namespace nib {

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------
//
// Combo order is A B C D AB AC AD BC BD CD. Kick and snare are on A and B so
// the two sounds a pattern is mostly made of need one finger each; the toms sit
// on pairs sharing a finger, so hold-A-and-tap gives a tom run.

/// The kit as a flat list of VOICES, which is what the looper records.
///
/// A recorded event stores "kick", not "C held while D was tapped". That is the
/// right level of abstraction: how a hit was produced is a property of the
/// performance, not of the pattern, and storing the gesture meant a replayed
/// loop had to re-derive a sound from a combination that no longer had a shift
/// attached to it. It also means a future edit to the mapping does not silently
/// change what an existing loop plays.
///
/// Index order is the display order, and matches kVoiceName below.
/// LEVELS are set by PERCEIVED loudness, not by energy or by peak.
///
/// Two corrections were needed, and both came from voices being reported as too
/// loud despite the numbers looking balanced:
///
///  1. Equal ENERGY is not equal loudness once pitches differ. The ear is about
///     24dB more sensitive at 800Hz than at 55Hz, so the cowbell — a pure tone
///     sitting right in that band — was the loudest thing in the kit while
///     measuring the same as the kick. It is now 1500Hz (a real cowbell's
///     register, and where it was asked to be) at 40/256.
///  2. A long voice needs to be quieter still, because the ear integrates. The
///     crash is the extreme case and has been reported loud three times: it is
///     now 22/256 AND a shorter decay, since level alone could not fix it
///     without making the transient inaudible.
///
/// Rough rule for editing this table: a voice an octave higher wants roughly
/// half the level; a voice twice as long wants roughly two thirds.
///
/// Pitches are written as HzToInc(...) so the table can be READ as frequencies
/// and checked against what a drum should be. The first version of this kit
/// carried raw increments inherited from another card whose accumulator was a
/// different width, and every voice came out about an octave and a half sharp —
/// a "kick" at 2.5kHz — which is invisible in a column of bare integers and
/// obvious the moment the unit is in the source.
const DrumSpec kVoices[kNumVoices] = {
	//  from        to        decay noise  mix  fall metal level
	{ HzToInc(62), HzToInc(45),  11,   0,    0,   7,   0,  256 },  // 0  kick
	{ HzToInc(50), HzToInc(38),  12,   0,    0,   8,   0,  200 },  // 1  kick deep
	{ HzToInc(190),HzToInc(150),  9,   7,  140,   6,   0,  190 },  // 2  snare
	{ HzToInc(230),HzToInc(180),  8,  10,  200,   6,   0,  170 },  // 3  snare snappy
	{ HzToInc(6000),HzToInc(6000),8,   8,  256,   0,   0,   70 },  // 4  closed hat
	{ HzToInc(7600),HzToInc(7600),7,   7,  256,   0,   1,   80 },  // 5  hi-hat metallic
	{ HzToInc(7600),HzToInc(7600),10, 10,  256,   0,   1,   45 },  // 6  open hi-hat
	{ HzToInc(5200),HzToInc(5200),13, 13,  256,   0,   1,   22 },  // 7  crash
	{ HzToInc(1500),HzToInc(1500),10,  0,    0,   0,   0,   40 },  // 8  cowbell
	{ HzToInc(420),HzToInc(90),  11,   0,    0,  10,   0,  120 },  // 9  tom 1 "pew"
	{ HzToInc(300),HzToInc(110), 11,   8,   20,  10,   0,  110 },  // 10 syn tom 2
	{ HzToInc(360),HzToInc(120), 10,   6,   40,  10,   0,  120 },  // 11 syn drum 3
};

/// Which voice each (shift, tap) gesture plays. This is the ONLY place the
/// gesture-to-sound mapping lives, so it can be rearranged without touching
/// the voices or invalidating a recorded loop.
///
/// Chosen for the hand: a right thumb on C leaves closed hat, snare and kick
/// under the fingers — a whole beat without moving. D is the tom row for fills;
/// A and B carry the colour and the crash.
const int8_t kGestureVoice[kNumSingles][kNumSingles] = {
	//        tap A  tap B  tap C  tap D
	/* A */ {   -1,     8,     5,     6 },   // cowbell, hi-hat alt, open hat
	/* B */ {    7,    -1,     1,     3 },   // CRASH, kick deep, snare snappy
	/* C */ {    4,     2,    -1,     0 },   // closed hat, snare, kick
	/* D */ {    9,    10,    11,    -1 },   // tom 1, syn tom 2, syn drum 3
};

namespace {

/// Cubic soft clip: y = x - x^3/(3T^2) below the knee T, hard at +/-kLimit
/// above it. Several voices summing at full scale can reach many times the DAC
/// range, so this is load-bearing, not decoration.
///
/// UNITY GAIN below the knee, which is the property to protect. An earlier
/// version normalised the input by 3*kLimit but scaled the output back by only
/// kLimit, giving every signal a gain of 0.33 — the whole kit played a third
/// too quiet, which is exactly the kind of wrongness that sounds plausible.
/// tools/dspsim.py asserts small-signal unity now.
///
/// The knee is 1.5*kLimit because the cubic reaches its maximum of (2/3)T
/// there, and (2/3)(1.5 kLimit) == kLimit exactly.
inline int32_t SoftClip(int32_t x)
{
	constexpr int32_t kLimit = 2047;
	constexpr int32_t kKnee  = (3 * kLimit) / 2;

	if (x >  kKnee) return  kLimit;
	if (x < -kKnee) return -kLimit;

	// Split the cube so the intermediate stays inside 32 bits: (x*x/T)*x/(3T)
	// peaks around x*2/3, far below the limit.
	return x - (((x * x) / kKnee) * x) / (3 * kKnee);
}

} // namespace

// ---------------------------------------------------------------------------
// One voice
// ---------------------------------------------------------------------------

void DrumVoice::Trigger(const DrumSpec &spec, int32_t pitchScaleQ16, int32_t decayAdj)
{
	int32_t p0 = mul_q16(static_cast<int32_t>(spec.pitch0),     pitchScaleQ16);
	int32_t pf = mul_q16(static_cast<int32_t>(spec.pitchFloor), pitchScaleQ16);
	// Floor at ~20Hz and ceiling below Nyquist.
	constexpr int32_t kMinInc = HzToInc(20);
	constexpr int32_t kMaxInc = HzToInc(16000);
	if (p0 < kMinInc) p0 = kMinInc;
	if (pf < kMinInc) pf = kMinInc;
	if (p0 > kMaxInc) p0 = kMaxInc;
	if (pf > kMaxInc) pf = kMaxInc;

	phase_      = 0;
	phase2_     = 0;
	pitch_      = static_cast<uint32_t>(p0);
	pitchFloor_ = static_cast<uint32_t>(pf);
	noiseMix_   = spec.noiseMix;
	sweepShift_ = spec.sweepShift ? spec.sweepShift : 8;
	pitchDiff_  = spec.sweepShift ? ((p0 - pf) << 8) : 0;
	metal_      = spec.metal;
	level_      = spec.level ? spec.level : kLevelFull;

	// decayAdj shifts the whole kit shorter or longer. Clamped so the extremes
	// stay musical rather than becoming a click or a drone.
	int32_t ds = spec.decayShift + decayAdj;
	int32_t ns = spec.noiseShift + decayAdj;
	if (ds < 4)  ds = 4;
	if (ds > 15) ds = 15;
	if (ns < 4)  ns = 4;
	if (ns > 15) ns = 15;
	decayShift_ = static_cast<uint8_t>(ds);
	noiseShift_ = static_cast<uint8_t>(ns);

	env_      = 4095 << kDrumEnvFrac;
	noiseEnv_ = (spec.noiseMix > 0) ? (4095 << kDrumEnvFrac) : 0;
}

int32_t __not_in_flash_func(DrumVoice::Step)(uint32_t &rng)
{
	if (env_ <= 0 && noiseEnv_ <= 0) return 0;

	// --- body: a triangle folded out of a 12-bit phase accumulator ---
	phase_ = (phase_ + pitch_) & kPhaseMask;

	// Triangle, folded out of the accumulator and scaled to +/-2047 regardless
	// of the accumulator width.
	int32_t tri = (phase_ & kPhaseHalf)
	            ? static_cast<int32_t>(kPhaseHalf - (phase_ & (kPhaseHalf - 1)))
	            : static_cast<int32_t>(phase_ & (kPhaseHalf - 1));
	int32_t osc = ((tri - static_cast<int32_t>(kPhaseHalf / 2)) * 2047)
	            / static_cast<int32_t>(kPhaseHalf / 2);

	// Pitch sweep — EXPONENTIAL, decaying the distance to the floor rather than
	// stepping the pitch down linearly.
	//
	// This is what makes a Simmons-style tom sound like one. A linear fall
	// (`pitch--` every N samples, which is what this used to do) reaches the
	// floor in about 11ms and then sits there, so almost the whole note is at
	// one pitch and the sweep is a click at the front. Decaying the DIFFERENCE
	// glides down over 50-150ms: fast at first, easing in at the bottom, which
	// is the "pew" the ear is listening for.
	//
	// The difference is kept in a Q8 accumulator for the same reason the
	// envelopes carry headroom — a plain shift on a small integer stalls, and
	// the pitch would stop short of the floor.
	if (pitchDiff_ > 0)
	{
		pitchDiff_ -= (pitchDiff_ >> sweepShift_) + 1;
		if (pitchDiff_ < 0) pitchDiff_ = 0;
		pitch_ = pitchFloor_ + static_cast<uint32_t>(pitchDiff_ >> 8);
	}

	// --- noise ---
	int32_t noise = 0;
	if (noiseEnv_ > (8 << kDrumEnvFrac))
		noise = (static_cast<int32_t>((xorshift32(rng) >> 24) & 255) - 128) << 4;

	// METALLIC voices ring-modulate the noise against a second, inharmonically
	// related square. Plain filtered noise reads as "shh"; cymbals need the
	// clangorous, slightly pitched quality that comes from beating partials,
	// and a ring mod is the cheapest honest way to get it — two phase
	// accumulators and a sign flip, no extra filters.
	//
	// The 1.47x ratio is deliberately not a simple fraction: an integer ratio
	// would lock the two into a harmonic relationship and sound like a tone.
	if (metal_)
	{
		phase2_ = (phase2_ + (pitch_ * 3) / 2 + HzToInc(37)) & kPhaseMask;
		if (phase2_ & kPhaseHalf) noise = -noise;
		// Square the body too, so the metal voices are all edge and no thud.
		osc = (phase_ & kPhaseHalf) ? 2047 : -2047;
	}

	// --- decays ---
	if (env_ > 0)
	{
		env_ -= (env_ >> decayShift_) + 1;
		if (env_ < 0) env_ = 0;
	}
	if (noiseEnv_ > 0)
	{
		noiseEnv_ -= (noiseEnv_ >> noiseShift_) + 1;
		if (noiseEnv_ < 0) noiseEnv_ = 0;
	}

	// --- mix body and noise by the voice's character ---
	int32_t body = (osc   * (env_      >> kDrumEnvFrac)) >> 12;
	int32_t nz   = (noise * (noiseEnv_ >> kDrumEnvFrac)) >> 12;
	int32_t mixed = ((body * (256 - noiseMix_)) + (nz * noiseMix_)) >> 8;

	// Per-voice level. Without it a long voice is simply louder, because every
	// voice starts at the same peak and the ear integrates over time — the
	// 1.9-second crash was delivering thirteen times a kick's energy.
	return (mixed * level_) >> 8;
}

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------

void DrumKit::TriggerVoice(int8_t voice, int32_t yKnob)
{
	if (voice < 0 || voice >= kNumVoices) return;

	// Y sweeps the whole kit's character in one gesture: pitch from half to
	// double, and decay from three shifts longer to three shorter.
	int32_t y = knob_to_q16(yKnob);
	int32_t pitchScale = 32768 + y;                  // Q16 0.5 .. 1.5
	int32_t decayAdj   = 3 - ((yKnob * 6) >> 12);    // +3 .. -3

	voice_[PickSlot()].Trigger(kVoices[voice], pitchScale, decayAdj);
}

/// Choose a voice slot: a free one if there is one, otherwise the QUIETEST.
///
/// The old allocator was a blind round robin — `next_ = (next_ + 1) % n` — which
/// takes the next slot whether or not it is still sounding. That is what made a
/// loop appear to erase itself: a crash rings for 874ms, seven sixteenths at
/// 120bpm, and the six hits that followed it each had an equal chance of being
/// handed its slot.
///
/// Stealing by loudness rather than by age is what makes the failure inaudible
/// when it does happen. A voice near the end of its decay is contributing almost
/// nothing, so cutting it costs nothing; cutting the freshest one — which is
/// what round robin does about as often as not — removes the loudest thing in
/// the mix.
uint8_t __not_in_flash_func(DrumKit::PickSlot)()
{
	int32_t quietest = INT32_MAX;
	uint8_t pick = 0;

	for (int i = 0; i < kMaxVoices; i++)
	{
		int32_t e = voice_[i].Energy();
		if (e == 0) return static_cast<uint8_t>(i);   // free: take it
		if (e < quietest) { quietest = e; pick = static_cast<uint8_t>(i); }
	}
	return pick;
}

int32_t __not_in_flash_func(DrumKit::Step)()
{
	int32_t sum = 0;
	for (int i = 0; i < kMaxVoices; i++) sum += voice_[i].Step(rng_);
	return SoftClip(sum);
}

// ---------------------------------------------------------------------------
// DJ filter
// ---------------------------------------------------------------------------

namespace {

/// Knob position -> filter coefficient, with a perceptually exponential taper.
/// The shape (linear + quadratic + cubic, weighted) is from 45_bends; a linear
/// sweep spends most of its travel in a range the ear reads as "already open".
inline int32_t CutoffCurve(int32_t ratioQ15)
{
	int32_t quad  = (ratioQ15 * ratioQ15) >> 15;
	int32_t cubic = (quad * ratioQ15) >> 15;
	int32_t mixed = (ratioQ15 * 4000 + quad * 10000 + cubic * 18768) >> 15;
	int32_t g = 300 + mixed;
	if (g < 150)   g = 150;
	if (g > 22000) g = 22000;
	return g;
}

/// Fixed mild resonance, Q15. No knob is free for it, and a fixed slight
/// emphasis is what a DJ filter sounds like anyway.
constexpr int32_t kResonance = 12000;

/// Bypass deadband around centre. Generous on purpose: you have to be able to
/// find "no filtering" by feel, mid-performance, without looking at the panel.
constexpr int32_t kBypassLo = 1800;
constexpr int32_t kBypassHi = 2300;

} // namespace

void DjFilter::SetKnob(int32_t knob)
{
	if (knob < kBypassLo)
	{
		// Low-pass: fully CCW is nearly closed, opening toward centre.
		mode_ = -1;
		g_ = CutoffCurve((knob << 15) / kBypassLo);
	}
	else if (knob > kBypassHi)
	{
		// High-pass: opens as the knob climbs above centre.
		mode_ = 1;
		g_ = CutoffCurve(((knob - kBypassHi) << 15) / (4095 - kBypassHi));
	}
	else
	{
		mode_ = 0;
	}
}

int32_t __not_in_flash_func(DjFilter::Step)(int32_t in)
{
	if (mode_ == 0)
	{
		// Bypassed. Bleed the state toward zero rather than freezing it, so
		// coming back out of bypass does not thump with a stale charge.
		v1_ -= v1_ >> 6;
		v2_ -= v2_ >> 6;
		return in;
	}

	// Chamberlin SVF, in the order 45_bends documents as its correctness fix:
	// compute the high-pass from the PREVIOUS states, integrate, then derive
	// the low-pass from the freshly updated band-pass.
	int32_t hp = in - ((kResonance * v1_) >> 15) - v2_;
	v1_ += (g_ * hp) >> 15;
	int32_t lp = v2_ + ((g_ * v1_) >> 15);
	v2_ = lp;

	// Clamp the states. At high g with resonance an SVF will happily blow up,
	// and an integer one wraps instead of merely getting loud — which is a
	// full-scale square wave, not a filter sound.
	constexpr int32_t kStateMax = 1 << 20;
	if (v1_ >  kStateMax) v1_ =  kStateMax;
	if (v1_ < -kStateMax) v1_ = -kStateMax;
	if (v2_ >  kStateMax) v2_ =  kStateMax;
	if (v2_ < -kStateMax) v2_ = -kStateMax;

	return (mode_ < 0) ? lp : hp;
}

} // namespace nib
