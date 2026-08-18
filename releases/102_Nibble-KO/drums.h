// drums.h — twelve percussion voices, each independently synthesised or
// sample-based, and the DJ filter.
//
// Ported from NIBBLE's DRUMS half (../WoskshopButtons/drums.h), whose synth
// engine is hardware-tested and taken as-is: a phase accumulator masked to 18
// bits, a triangle folded out of it, the pitch swept downward while the
// envelope runs, and exponential decay via `e -= (e >> shift) + 1`. See that
// file's header comment for the full derivation (from
// 74_Wild_Pebble/WildPebble.cpp:787-901) and docs/LESSONS.md §2 for the bugs
// that arithmetic has already caused once (the `+1`-caps-your-range trap, the
// phase-accumulator-width mismatch) so they are not repeated here.
//
// NIBBLE-KO's difference: every voice can ALSO play a sample instead of the
// synth voice, chosen per-slot by the user via the WebUI — see WorkshopBio's
// webui.cpp/samplestore.h for the upload/flash-management pattern this will
// extend (../WorkshopBio/samplestore.h has the resolve-by-priority logic:
// user upload overrides baked default, per slot).
//
// BOTH BACKENDS NOW EXIST. Which one a voice uses is decided per slot by
// kVoiceSample in samples_default.h: an index into the baked sample bank, or
// -1 to stay synthesised. DrumKit::TriggerVoice resolves that at trigger time
// and calls TriggerPcm or Trigger accordingly, so the choice is data rather
// than a second code path through the kit.
//
// Y IS PLAYBACK RATE on a sampled voice. NIBBLE chose synthesis partly to
// avoid that question (docs/LESSONS.md §4 flags "decide early what Y does",
// because resampling couples pitch to duration where a synth voice keeps
// them independent). The answer here is to accept the coupling: the same
// knob makes the whole kit higher-and-shorter or lower-and-longer, which is
// what tape does and what anyone who has pitched a sampler expects.
//
// Which slot plays what is RUNTIME data, not a compile-time table:
// gVoiceSample[] in samplestore.h starts as a copy of the baked kVoiceSample
// defaults and is rewritten per-slot by the browser over MSG_SET_SOURCE. The
// shape was always right for this, being indices rather than audio.

#pragma once
#include <stdint.h>
#include "nibbleko.h"

namespace nko {

/// Per-voice character, synth backend. One struct covers the whole kit.
/// Phase accumulator width, in bits.
///
/// 18, not 12. The obvious 12-bit accumulator (mask 4095) gives a frequency
/// step of 48000/4096 = 11.7Hz, which is hopeless at the bottom of the kit: a
/// kick wants ~55Hz and the nearest available pitches are 47Hz and 59Hz.
/// NIBBLE's kit inherited its pitch numbers from another card without
/// checking that its accumulator matched, and came out about an octave and a
/// half sharp before this was fixed — see docs/LESSONS.md §2. Six extra
/// fractional bits bring the step to 0.18Hz, which is inaudible.
constexpr int kPhaseBits = 18;
constexpr uint32_t kPhaseMask = (1u << kPhaseBits) - 1;
constexpr uint32_t kPhaseHalf = 1u << (kPhaseBits - 1);

/// Frequency (Hz) -> phase increment. Use this rather than hand-computed
/// numbers, so a voice table can be read and checked as pitches.
constexpr uint32_t HzToInc(int32_t hz)
{
	return static_cast<uint32_t>((static_cast<int64_t>(hz) << kPhaseBits) / 48000);
}

struct DrumSpec
{
	uint32_t pitch0;      ///< starting phase increment (use HzToInc)
	uint32_t pitchFloor;  ///< sweep stops here (== pitch0 means no sweep)
	uint8_t  decayShift;  ///< body decay; larger = longer
	uint8_t  noiseShift;  ///< noise decay
	uint16_t noiseMix;    ///< Q8: 0 = pure tone, 256 = pure noise
	uint8_t  sweepShift;  ///< pitch-fall rate: SMALLER = faster. 0 = no sweep.
	                      ///< The fall is exponential (decaying the distance to
	                      ///< pitchFloor), not linear, because that is what makes
	                      ///< a Simmons tom sound like one — see DrumVoice::Step.
	uint8_t  metal;       ///< 0 = normal, 1 = ring-modulated metallic (cymbals)
	uint16_t level;       ///< Q8 output gain, 256 = full. See below.
};

/// Why voices need their own level, rather than all starting at full scale.
///
/// Peak amplitude is a bad proxy for loudness once decay times differ by two
/// orders of magnitude. Every voice firing at 4095 means a 1.9-second crash
/// delivers roughly THIRTEEN TIMES the total energy of a kick and four hundred
/// times a closed hat — it was not slightly hot, it buried the kit.
///
/// The ear integrates over roughly a tenth of a second, so a long voice has to
/// start proportionally quieter to sit in the same mix. These are set by that
/// reasoning and then trimmed by ear (see NIBBLE's docs/DEVLOG.md).
constexpr uint16_t kLevelFull = 256;

/// How many distinct sounds the kit has.
///
/// Twelve, one per ordered gesture — A-held-then-B and B-held-then-A are
/// different gestures even though they produce an identical voltage. See
/// LevelTracker::Shift() in levels.h for how the ordering is recovered.
constexpr int kNumVoices = 12;

extern const DrumSpec kVoices[kNumVoices];
extern const int8_t   kGestureVoice[kNumSingles][kNumSingles];

/// Which VOICE a (shift, tap) gesture plays, or -1 if it is not a gesture.
///
/// The separation matters: the looper records the voice this returns, never
/// the gesture. How a hit was produced belongs to the performance, not the
/// pattern.
static inline int8_t VoiceForGesture(int8_t shift, int8_t tap)
{
	if (shift < 0 || shift >= kNumSingles) return -1;
	if (tap   < 0 || tap   >= kNumSingles) return -1;
	return kGestureVoice[shift][tap];
}

// NIBBLE put a four-note BASSLINE on the singles here, using CV Out 1 --
// the singles are shifts and make no sound, so the output was going spare.
// NIBBLE-KO deliberately drops it: this card is percussion, and the singles
// have a bigger job now (they are mode selectors under the switch). CV Out 1
// and 2 are unassigned for the moment; see the plan's open questions.

/// Fractional headroom in the drum envelope accumulators.
///
/// `e -= (e >> shift) + 1` reaches zero thanks to the `+1`, but that `+1`
/// DOMINATES once `e >> shift` rounds to zero, which for a 4095 peak happens
/// around shift 12. Every longer setting decayed in the same ~85ms, so shifts
/// 12 and 13 were identical and a "crash" was a blip — see
/// docs/LESSONS.md §2.
///
/// Six bits of headroom puts the usable range at ~6ms to ~1.9s, which is what
/// a cymbal needs and leaves the Y knob's +/-3 meaningful at both ends.
constexpr int kDrumEnvFrac = 6;

class DrumVoice
{
public:
	void Trigger(const DrumSpec &spec, int32_t pitchScaleQ16, int32_t decayAdj);

	/// Start a SAMPLED voice instead of a synthesised one.
	///
	/// `rateQ16` is the playback rate: 65536 is the recorded pitch, double is
	/// an octave up. `level` is the same Q8 gain the synth voices carry, so
	/// the two backends sit in one mix without a second balancing pass.
	void TriggerPcm(const int8_t *data, uint32_t len,
	                int32_t rateQ16, uint16_t level);

	/// Play this voice BACKWARDS. Applied at trigger time, so it affects
	/// hits that start while the effect is held and leaves ones already
	/// sounding alone.
	///
	/// A sampled voice starts at the end and walks down. A synthesised one
	/// has no recording to reverse, so its ENVELOPE runs backwards instead —
	/// quiet to loud rather than loud to quiet, which is what a reversed
	/// sample sounds like. Different mechanism, same musical gesture.
	void SetReverse();

	/// Slow to a halt like tape. `fallTicks` is how many samples the fall
	/// takes; the rate (or pitch) decays toward zero over that span.
	void SetTapeStop(int32_t fallSamples);

	/// One audio-rate sample. Returns roughly -2047..2047; silent when idle.
	int32_t Step(uint32_t &rng);

	bool Active() const
	{
		return env_ > 0 || noiseEnv_ > 0 || (pcm_ && pcmIdx_ < pcmLen_);
	}

	/// How much this voice is still contributing, for the allocator to compare.
	/// Scaled by level_ so a quiet voice is correctly seen as a cheap steal.
	///
	/// A sampled voice has no envelope to read, so it reports how much of the
	/// recording is LEFT. That is the same question the allocator is really
	/// asking — "how much would cutting this cost" — and it keeps one
	/// comparable number across both backends.
	int32_t Energy() const
	{
		if (pcm_)
		{
			if (pcmIdx_ >= pcmLen_) return 0;
			const uint32_t left = pcmLen_ - pcmIdx_;
			return static_cast<int32_t>(left >> 4) * static_cast<int32_t>(level_);
		}
		int32_t e = (env_ > noiseEnv_) ? env_ : noiseEnv_;
		return (e >> 8) * static_cast<int32_t>(level_);
	}

private:
	uint32_t phase_      = 0;
	uint32_t phase2_     = 0;   ///< second accumulator, metallic voices only
	uint32_t pitch_      = 0;
	uint32_t pitchFloor_ = 0;
	int32_t  env_        = 0;
	int32_t  noiseEnv_   = 0;
	uint16_t noiseMix_   = 0;
	uint8_t  decayShift_ = 10;
	uint8_t  noiseShift_ = 10;
	uint8_t  sweepShift_ = 0;
	int32_t  pitchDiff_  = 0;   ///< Q8 distance still to fall
	uint8_t  metal_      = 0;
	uint16_t level_      = kLevelFull;

	// --- PCM backend ------------------------------------------------------
	//
	// Position is split into WHOLE SAMPLES and a Q16 fraction rather than
	// being one Q16 number, and that is not a stylistic choice: a single
	// uint32 Q16 position tops out at 2^32/65536 = 65536 samples, i.e. 1.37
	// seconds at 48kHz. BioMimicry hit exactly this — its longer recordings
	// wrapped to zero before reaching their end, so `idx < len` was never
	// false, the voice never freed itself, and the sound looped forever.
	// Splitting them makes the reachable length 2^32 samples instead.
	const int8_t *pcm_    = nullptr;   ///< null = this voice is synthesised
	uint32_t      pcmLen_ = 0;
	uint32_t      pcmIdx_ = 0;         ///< whole samples played
	uint32_t      pcmFrac_ = 0;        ///< Q16 fraction within the current one
	uint32_t      pcmInc_ = 65536;     ///< Q16 rate; 65536 = recorded pitch

	// --- transport effects ------------------------------------------------
	/// Walking backwards through the recording (or running the envelope
	/// backwards, on a synthesised voice).
	bool     reverse_ = false;
	/// Envelope position for a reversed SYNTH voice: climbs 0 -> full where
	/// the normal envelope falls. Separate from env_ because env_ is also the
	/// decay's own state and reusing it would fight the decay arithmetic.
	int32_t  swell_    = 0;
	int32_t  swellInc_ = 0;
	/// Tape-stop: the rate is multiplied by this Q16 scale, which decays to
	/// zero over the fall. -1 when not stopping.
	int32_t  stopScale_ = -1;
	int32_t  stopDec_   = 0;
};

/// How many voices can decay at once.
///
/// Sixteen, not six. A four-bar looped pattern with several overdubbed passes
/// fires far more hits than a pair of hands ever could, and they overlap: at
/// 120bpm a sixteenth is 125ms and a crash stays audible for ~874ms, i.e.
/// seven sixteenths, so a single crash needs seven slots to itself if a
/// hi-hat pattern is running underneath. See docs/LESSONS.md §2 for the full
/// derivation (six slots was NIBBLE's original figure and it was not enough).
constexpr int kMaxVoices = 16;

class DrumKit
{
public:
	/// Fire a VOICE by index. Both live playing and the looper come through
	/// here, so a replayed hit is bit-identical to the one that was played.
	///
	/// `yKnob` (0..4095) reshapes the whole kit: CCW lower and longer, CW higher
	/// and shorter. Applied at TRIGGER time only, so sweeping the knob does not
	/// warp voices that are already decaying — which would sound like a fault
	/// rather than a control.
	///
	/// `reverse` and `tapeStopSamples` are the D-bank transport effects,
	/// applied as the voice STARTS. Pass 0 for no tape stop.
	///
	/// REVERSE genuinely can only be done here: it has to know where the
	/// recording ends before it can play backwards from it. Tape stop does
	/// not — see TapeStopAll(), which is how holding D+B brakes the kit.
	void TriggerVoice(int8_t voice, int32_t yKnob,
	                  bool reverse = false, int32_t tapeStopSamples = 0);

	/// Brake every voice that is currently sounding.
	///
	/// TriggerVoice() only reaches hits that START while D+B is held, which
	/// is right for a KO-style momentary effect but wrong for a tape stop:
	/// a real one grabs whatever is already spinning. Between recorded hits
	/// there is nothing to attach to, so on playback the effect appeared to
	/// do nothing at all — reported from the bench.
	///
	/// Cheap and safe because SetTapeStop() is a pure state setter: it
	/// installs a Q16 ramp and reads nothing that was decided at trigger
	/// time.
	///
	/// Call on the RISING EDGE of the effect only. Calling it every tick
	/// re-installs the ramp at full scale continuously, so the rate never
	/// actually falls and the kit simply plays on.
	void TapeStopAll(int32_t fallSamples);

	/// Sum of every active voice, soft-clipped. Ten voices at full scale would
	/// reach +/-20470, so clipping is mandatory rather than a nicety.
	int32_t Step();

private:
	uint8_t PickSlot();

	DrumVoice voice_[kMaxVoices];
	uint32_t  rng_ = 0x1234567u;
};

// ---------------------------------------------------------------------------
// DJ filter
// ---------------------------------------------------------------------------

/// A mono Chamberlin state-variable filter with a DJ-isolator law on one knob:
/// low-pass below centre, high-pass above, and a generous bypass deadband in
/// the middle so "off" can be found by feel without looking.
///
/// Ported unchanged from NIBBLE. What that file takes from 45_bends'
/// FilterBlock is its update ORDER, which its own comments flag as a
/// correctness fix over an earlier version:
///
///     hp = in - r*v1 - v2;  v1 += g*hp;  lp = v2 + g*v1;  v2 = lp;
class DjFilter
{
public:
	/// `knob` is 0..4095. Control rate — the coefficients only need to move as
	/// fast as the knob does.
	void SetKnob(int32_t knob);

	/// One audio-rate sample.
	int32_t Step(int32_t in);

private:
	int32_t g_    = 0;        ///< Q15 frequency coefficient
	int32_t v1_   = 0;        ///< band-pass state
	int32_t v2_   = 0;        ///< low-pass state
	int8_t  mode_ = 0;        ///< -1 low-pass, 0 bypass, +1 high-pass
};

} // namespace nko
