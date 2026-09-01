// TRACT8 - the Voder engine.
//
// An eight-band reimplementation of the 1939 Bell Labs Voder: two raw
// excitation sources (a buzz and a hiss) fed through a bank of fixed
// bandpass filters whose gains are the performance.
//
//   1. Excitation. A bandlimited sawtooth at F0 for voiced sounds, white
//      noise for unvoiced ones, mixed continuously. Either may be replaced
//      wholesale by an external signal on Audio In 1.
//   2. Filter bank. Eight parallel 2-pole bandpass sections at fixed
//      centres, each scaled by its own Q15 gain.
//   3. Plosives. A short noise burst summed after the bank, for the stop
//      consonants the original had dedicated keys for.
//
// Everything on the hot path is fixed point. Floats appear exactly once,
// in VoderInit(), where the biquad coefficients are derived - see the note
// there for why that is safe.
//
// Verified by: tools/filter_check.py (band geometry, Q15 quantisation,
// stability), tools/excite_check.py (aliasing, noise flatness).

#ifndef TRACT8_VODER_H_
#define TRACT8_VODER_H_

#include <stdint.h>

namespace tract8 {

static constexpr int kSampleRate = 48000;
static constexpr int kBands = 8;

// Band centres, Hz. Chosen in the brief and kept: they are close to a
// third-octave spacing across the speech-intelligible range, and eight of
// them map one-to-one onto the 8mu's faders, which is the whole reason this
// card is playable. The Voder had ten; we trade two for the physical
// correspondence.
static constexpr int kBandHz[kBands] = {250, 450, 700, 1000, 1400, 1900, 2600, 3800};

// Filter Q. 4.0 gives roughly a quarter-octave -3 dB width at each centre,
// which is narrow enough that the bands are perceptually distinct but wide
// enough that eight of them cover the spectrum without gaps between the
// upper bands. Higher Q rings audibly on plosives; lower Q smears the
// vowels together. Do not change without re-running tools/filter_check.py.
static constexpr int32_t kFilterQ_Q15 = 131072;  // 4.0 in Q15

// F0 range for the buzz oscillator.
static constexpr int32_t kF0MinMilliHz = 50000;    // 50 Hz
static constexpr int32_t kF0MaxMilliHz = 500000;   // 500 Hz
static constexpr int32_t kF0DefaultMilliHz = 110000;  // 110 Hz, a low male pitch

// Plosive burst length, samples. 8 ms is the acoustic duration of the
// release transient in an unaspirated stop - long enough to read as a
// consonant, short enough not to read as a fricative. This is the SHORTEST
// the decay control goes; see kPlosiveMaxSamples.
static constexpr int32_t kPlosiveSamples = 384;  // 8 ms at 48 kHz

// Longest plosive decay, samples. At the top of the decay control the
// burst lasts a full second, which stops being a consonant and becomes a
// sustained noise source - useful as a voice in its own right rather than
// as punctuation.
static constexpr int32_t kPlosiveMaxSamples = 48000;  // 1 s at 48 kHz

// Plosive BANDPASS coefficients, Q15. Two poles either side, giving a band
// of roughly 600-2800 Hz.
//
// This started as a plain lowpass, which was wrong in an instructive way.
// Removing the top of a white-noise burst does make it darker, but it
// leaves everything below the corner INCLUDING the sub-100 Hz region -
// and that is where a thump lives. The result was reported as "a bomb
// going off", correctly: a burst with most of its energy under 200 Hz is
// a kick drum, not a consonant.
//
// A real bilabial release has little energy below 200 Hz, because the lips
// are closed and nothing is radiating. Its peak is a broad one around
// 500-1500 Hz. So the burst needs a BAND, not a ceiling: the high poles
// remove the hiss that made it sound like white noise, and the low poles
// remove the thump that made it sound like an explosion.
//
// Implemented as a two-pole lowpass minus a two-pole tracking of its own
// low end, which is a bandpass for four multiply-adds and no resonance to
// go unstable. Measured: -10.4 dB below 200 Hz and -12.6 dB above 4 kHz,
// both relative to the 500-1500 Hz peak.
static constexpr int32_t kPlosiveLoQ15 = 2600;   // ~600 Hz, removes thump
static constexpr int32_t kPlosiveHiQ15 = 12000;  // ~2800 Hz, removes hiss

// Glottal gate ramp, samples.// Glottal gate ramp, samples. Gating the buzz with a hard edge clicks
// audibly; 2 ms of linear ramp removes it without slurring the attack.
static constexpr int32_t kGateRamp = 96;  // 2 ms at 48 kHz

// One 2-pole bandpass section, Direct Form I, Q15 coefficients and state.
struct Biquad {
  int32_t b0;      // Q15. b1 is always 0 and b2 always -b0 for a bandpass,
                   // so neither is stored.
  int32_t a1, a2;  // Q15. |a1| reaches 1.99 at the lowest band, so these
                   // are NOT limited to +/-1.0 despite the Q15 scaling.
  int32_t x1, x2;  // input history
  int32_t y1, y2;  // output history
};

// Parameters the card hands to the engine each sample. All Q15 unless
// noted. Plain struct, passed by const reference - the compiler keeps it
// in registers.
struct Params {
  int32_t band_gain[kBands];  // 0..32767 per band
  int32_t f0_milli_hz;        // absolute pitch, milli-Hz
  int32_t source_mix;         // 0 = all buzz, 32767 = all noise
  int32_t voiced_level;       // buzz gate, 0..32767
  int32_t noise_level;        // hiss gate, 0..32767
  // Audio In 1, SUMMED with the internal buzz and noise rather than
  // replacing them - so an external signal is filtered by the same vowel
  // the card is speaking, with the internal voice still underneath it.
  int32_t ext_input;

  // Plosive level, Q15. Scales the burst so it can be balanced against
  // the voice - it was too loud fixed at full.
  int32_t click_level;

  // Plosive decay length, Q15, mapped between kPlosiveSamples and
  // kPlosiveMaxSamples. At the top the burst also stops decaying and
  // holds steady, so it reads as a sustained sound rather than a hit.
  int32_t click_decay;
};

// Build the biquad coefficient set. Call once, before Run(). Uses double
// arithmetic and cos() - acceptable here and nowhere else, because it runs
// exactly once at boot with the audio interrupt not yet started. The
// results are stored as Q15 integers and the hot path never sees a float.
void VoderInit(uint32_t seed);

class Voder {
 public:
  void Init(uint32_t seed);

  // Produce one output sample. Returns a 12-bit-ranged value suitable for
  // AudioOut (roughly -2048..2047, though the caller must still clamp).
  int32_t Process(const Params& p);

  // Fire a plosive burst. Idempotent within a burst - calling again
  // restarts it, which is what a repeated key press should do. The length
  // comes from Params::click_decay at the moment of the trigger.
  void TriggerPlosive(int32_t decay_q15);

  // Hold the burst open indefinitely (decay at maximum), or release it.
  void SetPlosiveSustain(bool on);

  // Sum of band output magnitudes from the last sample, Q15-ish. Drives
  // CV Out 1 and the LED display. Not normalised: the caller scales it.
  int32_t Energy() const { return energy_; }

  // True while the buzz is the dominant source. Drives LED 5.
  bool Voiced() const { return voiced_; }

 private:
  uint32_t Random();

  Biquad   bank_[kBands];

  // Band gains, smoothed PER SAMPLE toward the values the control path
  // asks for. See the note in Process().
  int32_t  gain_[kBands];
  uint32_t phase_;          // Q32 sawtooth phase accumulator
  uint32_t phase_inc_;      // Q32 increment
  uint32_t rng_;
  int32_t  plosive_;        // samples remaining in the burst
  int32_t  plosive_hp1_, plosive_hp2_;   // bandpass state: high poles
  int32_t  plosive_lp1_, plosive_lp2_;   // bandpass state: low poles
  int32_t  plosive_len_;    // length this burst was started with
  bool     plosive_hold_;   // true while sustaining at full decay
  int32_t  gate_env_;       // Q15, smoothed voiced gate
  int32_t  noise_env_;      // Q15, smoothed noise gate
  int32_t  energy_;
  bool     voiced_;
};

}  // namespace tract8

#endif  // TRACT8_VODER_H_
