// drums.cpp — the kit and the DJ filter.
//
// Two backends. The synth one is ported unchanged in behaviour from NIBBLE's
// DRUMS half; the sampled one plays a baked bank entry or a user upload.
// Which a voice uses is decided per slot by kVoiceSample, resolved through
// ResolveSample() at trigger time — see samples_default.h.

#include "drums.h"
#include "samplestore.h"   // ResolveSample; includes samples_default.h
#include "fastmath.h"
#include <stdint.h>
#include "pico.h"

namespace nko {

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------
//
// Combo order is A B C D AB AC AD BC BD CD. Kick and snare are on A and B so
// the two sounds a pattern is mostly made of need one finger each; the toms sit
// on pairs sharing a finger, so hold-A-and-tap gives a tom run.
//
// This table is the SYNTH character of each slot. Whether a slot actually
// renders from it or from a recording is decided elsewhere, by
// gVoiceSample[] in samplestore.h — an index into the sample bank, or -1 to
// stay synthesised. TriggerVoice resolves that per hit through
// ResolveSample(), so a slot that plays a sample simply never reaches the
// synth path and its entry here goes unused.

/// The kit as a flat list of VOICES, which is what the looper records.
///
/// A recorded event stores "kick", not "C held while D was tapped". That is the
/// right level of abstraction: how a hit was produced is a property of the
/// performance, not of the pattern, and storing the gesture meant a replayed
/// loop had to re-derive a sound from a combination that no longer had a shift
/// attached to it. It also means a future edit to the mapping does not silently
/// change what an existing loop plays.
///
/// Index order is the display order. LEVELS are set by PERCEIVED loudness, not
/// by energy or by peak.
///
/// Two corrections were needed, and both came from voices being reported as too
/// loud despite the numbers looking balanced:
///
///  1. Equal ENERGY is not equal loudness once pitches differ. The ear is about
///     24dB more sensitive at 800Hz than at 55Hz, so the cowbell — a pure tone
///     sitting right in that band — was the loudest thing in the kit while
///     measuring the same as the kick. It is now 1500Hz (a real cowbell's
///     register) at 40/256.
///  2. A long voice needs to be quieter still, because the ear integrates. The
///     crash is the extreme case: 22/256 AND a shorter decay, since level alone
///     could not fix it without making the transient inaudible.
///
/// Rough rule for editing this table: a voice an octave higher wants roughly
/// half the level; a voice twice as long wants roughly two thirds.
///
/// Pitches are written as HzToInc(...) so the table can be READ as frequencies
/// and checked against what a drum should be. See docs/LESSONS.md §2 for what
/// happens when a bare increment is used instead.
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
/// UNITY GAIN below the knee, which is the property to protect. tools/dspsim.py
/// asserts small-signal unity.
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

	// Clear the PCM side. Slots are reused between backends, so a synth
	// trigger landing on a slot that last played a sample would otherwise
	// keep the recording and ignore everything set below.
	pcm_    = nullptr;
	pcmLen_ = 0;

	// And the transport effects, which are armed per HIT — a slot that was
	// last reversed must not reverse the next thing that lands on it.
	reverse_   = false;
	swell_     = 0;
	stopScale_ = -1;

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

void DrumVoice::TriggerPcm(const int8_t *data, uint32_t len,
                           int32_t rateQ16, uint16_t level)
{
	pcm_     = data;
	pcmLen_  = len;
	pcmIdx_  = 0;
	pcmFrac_ = 0;

	// Armed per hit, so clear whatever the previous user of this slot left.
	reverse_   = false;
	stopScale_ = -1;

	// Clamped either side. Below about an octave down a short one-shot turns
	// into a slow lump several seconds long, and above four octaves up it is
	// a click — both are past the point where the control is musical, and the
	// bottom end also matters because the voice is not freed until it ends.
	constexpr int32_t kMinRate = 65536 / 2;
	constexpr int32_t kMaxRate = 65536 * 4;
	if (rateQ16 < kMinRate) rateQ16 = kMinRate;
	if (rateQ16 > kMaxRate) rateQ16 = kMaxRate;
	pcmInc_ = static_cast<uint32_t>(rateQ16);

	level_  = level ? level : kLevelFull;

	// The synth side must be silent, or Active() and Step() would see a live
	// envelope and mix a tone under the recording.
	env_      = 0;
	noiseEnv_ = 0;
}

void DrumVoice::SetReverse()
{
	reverse_ = true;

	if (pcm_)
	{
		// Start at the last whole sample and walk down. One short of the end
		// so the first interpolation still has a neighbour above it.
		pcmIdx_  = (pcmLen_ > 1) ? pcmLen_ - 2 : 0;
		pcmFrac_ = 0;
		return;
	}

	// A synthesised voice has nothing to play backwards, so its ENVELOPE is
	// reversed instead: silence swelling into the hit rather than decaying
	// out of it. The swell runs over roughly the length the decay would have
	// taken, so a reversed crash still feels like a crash.
	//
	// Derived from the decay shift rather than fixed, so a long voice swells
	// slowly and a short one snaps — which is the same relationship the
	// forward envelope has.
	swell_    = 0;
	swellInc_ = (4095 << kDrumEnvFrac) >> (decayShift_ + 1);
	if (swellInc_ < 1) swellInc_ = 1;
}

void DrumVoice::SetTapeStop(int32_t fallSamples)
{
	if (fallSamples < 1) fallSamples = 1;

	// A Q16 scale that walks from 1.0 to 0 over the fall. Linear rather than
	// exponential: an exponential approach never actually reaches zero, and
	// the whole point of a tape stop is that it STOPS.
	stopScale_ = 65536;
	stopDec_   = 65536 / fallSamples;
	if (stopDec_ < 1) stopDec_ = 1;
}

int32_t __not_in_flash_func(DrumVoice::Step)(uint32_t &rng)
{
	// --- sampled voices ---------------------------------------------------
	if (pcm_)
	{
		if (pcmIdx_ >= pcmLen_) { pcm_ = nullptr; return 0; }

		// Linear interpolation between neighbouring samples. Without it a
		// rate other than 1.0 steps between whole samples and the aliasing is
		// audible as a gritty edge on every pitched hit — which matters here
		// because the Y knob pitches the whole kit.
		const int32_t a = pcm_[pcmIdx_];
		const int32_t b = (pcmIdx_ + 1 < pcmLen_) ? pcm_[pcmIdx_ + 1] : 0;
		const int32_t mu = static_cast<int32_t>(pcmFrac_);
		const int32_t s  = a + (((b - a) * mu) >> 16);

		// Tape stop scales the rate down to nothing. Applied here rather than
		// by rewriting pcmInc_ so the ORIGINAL rate survives — which matters
		// because the Y knob already set it, and a stop should not erase the
		// pitch the voice was struck at.
		uint32_t inc = pcmInc_;
		if (stopScale_ >= 0)
		{
			inc = static_cast<uint32_t>(
				(static_cast<int64_t>(inc) * stopScale_) >> 16);
			stopScale_ -= stopDec_;
			if (stopScale_ < 0) stopScale_ = 0;
			// Fully stopped: free the slot rather than sitting on one sample
			// forever, which would hold a voice against the allocator and
			// leak a DC offset into the mix.
			if (stopScale_ == 0) { pcm_ = nullptr; return 0; }
		}

		pcmFrac_ += inc;
		const uint32_t step = pcmFrac_ >> 16;
		pcmFrac_ &= 0xFFFF;

		if (reverse_)
		{
			// Walking down. Ending is running off the START, so guard the
			// unsigned subtraction rather than letting it wrap to 4 billion.
			if (pcmIdx_ < step) { pcm_ = nullptr; return 0; }
			pcmIdx_ -= step;
		}
		else
		{
			pcmIdx_ += step;
		}

		// 8-bit source (-128..127) up to the card's 12-bit range, then the
		// per-voice level, exactly as the synth path applies it.
		return ((s << 4) * static_cast<int32_t>(level_)) >> 8;
	}

	if (env_ <= 0 && noiseEnv_ <= 0) return 0;

	// Tape stop on a SYNTH voice drops its pitch instead of its rate — there
	// is no playback rate to slow, but winding the oscillator down to nothing
	// is the same gesture and sounds like the same thing.
	//
	// The envelope is left alone deliberately: a synth voice that stopped
	// dead would just be a mute, where letting it keep decaying while the
	// pitch falls away is what makes it read as a wind-down.
	if (stopScale_ >= 0)
	{
		stopScale_ -= stopDec_;
		if (stopScale_ <= 0)
		{
			stopScale_ = 0;
			env_ = noiseEnv_ = 0;     // wound fully down: let the slot go
			return 0;
		}
	}

	// --- body: a triangle folded out of an 18-bit phase accumulator ---
	const uint32_t inc = (stopScale_ >= 0)
		? static_cast<uint32_t>((static_cast<int64_t>(pitch_) * stopScale_) >> 16)
		: pitch_;
	phase_ = (phase_ + inc) & kPhaseMask;

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
	// reaches the floor in about 11ms and then sits there, so almost the whole
	// note is at one pitch and the sweep is a click at the front. Decaying the
	// DIFFERENCE glides down over 50-150ms: fast at first, easing in at the
	// bottom, which is the "pew" the ear is listening for.
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
	//
	// REVERSED voices use the swell in place of the decay: silence climbing
	// into the hit rather than falling out of it. Both envelopes are replaced,
	// so a noisy voice reverses as convincingly as a tonal one.
	int32_t amp      = env_;
	int32_t noiseAmp = noiseEnv_;
	if (reverse_)
	{
		swell_ += swellInc_;
		if (swell_ > (4095 << kDrumEnvFrac))
		{
			// Reached full: the hit "lands", and from here the voice is done.
			// Stopping rather than decaying afterwards is what makes it a
			// reverse and not a swell-then-fade.
			env_ = noiseEnv_ = 0;
			return 0;
		}
		amp      = swell_;
		noiseAmp = (noiseMix_ > 0) ? swell_ : 0;
	}

	int32_t body = (osc   * (amp      >> kDrumEnvFrac)) >> 12;
	int32_t nz   = (noise * (noiseAmp >> kDrumEnvFrac)) >> 12;
	int32_t mixed = ((body * (256 - noiseMix_)) + (nz * noiseMix_)) >> 8;

	// Per-voice level. Without it a long voice is simply louder, because every
	// voice starts at the same peak and the ear integrates over time — the
	// 1.9-second crash was delivering thirteen times a kick's energy.
	return (mixed * level_) >> 8;
}

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------

void DrumKit::TriggerVoice(int8_t voice, int32_t yKnob,
                           bool reverse, int32_t tapeStopSamples)
{
	if (voice < 0 || voice >= kNumVoices) return;

	// Y sweeps the whole kit's character in one gesture: pitch from half to
	// double, and decay from three shifts longer to three shorter.
	int32_t y = knob_to_q16(yKnob);
	int32_t pitchScale = 32768 + y;                  // Q16 0.5 .. 1.5
	int32_t decayAdj   = 3 - ((yKnob * 6) >> 12);    // +3 .. -3

	// A SAMPLED slot plays its recording; everything else is synthesised.
	//
	// Y becomes PLAYBACK RATE on a sample, which is the honest equivalent of
	// what it does to a synth voice: the same knob makes the whole kit higher
	// and shorter or lower and longer, because resampling couples pitch and
	// duration exactly the way tape does. NIBBLE's LESSONS.md flagged
	// "decide early what Y does" precisely because pitch and decay stop being
	// independent once samples are involved — this accepts that rather than
	// fighting it, and it is the behaviour anyone who has pitched a sampler
	// already expects.
	const SampleRef s = ResolveSample(voice);
	DrumVoice &slot = voice_[PickSlot()];

	// A synthesised slot can borrow ANOTHER voice's character — two kicks, or
	// a tom on every pad. SynthVoiceFor() returns the slot's own index unless
	// the browser has pointed it somewhere else, so the ordinary case is
	// unchanged.
	const int synth = SynthVoiceFor(voice, gVoiceSample[voice]);

	if (s.data && s.len)
		slot.TriggerPcm(s.data, s.len, pitchScale, kVoices[synth].level);
	else
		slot.Trigger(kVoices[synth], pitchScale, decayAdj);

	// The transport effects go on AFTER the trigger, because both need the
	// voice already set up: reverse has to know where the recording ends, and
	// the tape stop scales whatever rate the Y knob just chose.
	if (reverse)             slot.SetReverse();
	if (tapeStopSamples > 0) slot.SetTapeStop(tapeStopSamples);
}

void __not_in_flash_func(DrumKit::TapeStopAll)(int32_t fallSamples)
{
	for (int i = 0; i < kMaxVoices; i++)
		if (voice_[i].Active()) voice_[i].SetTapeStop(fallSamples);
}

/// Choose a voice slot: a free one if there is one, otherwise the QUIETEST.
///
/// A blind round robin — `next_ = (next_ + 1) % n` — takes the next slot
/// whether or not it is still sounding, which is what made a loop appear to
/// erase itself: a crash rings for 874ms, seven sixteenths at 120bpm, and the
/// six hits that followed it each had an equal chance of being handed its
/// slot. See docs/LESSONS.md §2 for the full derivation.
///
/// Stealing by loudness rather than by age is what makes the failure inaudible
/// when it does happen. A voice near the end of its decay is contributing
/// almost nothing, so cutting it costs nothing; cutting the freshest one —
/// which is what round robin does about as often as not — removes the
/// loudest thing in the mix.
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

} // namespace nko
