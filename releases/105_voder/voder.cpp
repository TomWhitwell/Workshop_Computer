// TRACT8 - the Voder engine. See voder.h for the overview.
//
// Verified by: tools/filter_check.py, tools/excite_check.py

#include "voder.h"

#include <math.h>

#ifdef TRACT8_HOST_BUILD
  // The host test harness compiles this file straight onto a PC, where the
  // Pico attribute does not exist. Same shim as WorkshopSpectral's fft.h.
  #define __not_in_flash_func(f) f
#else
  #include "pico.h"
#endif

namespace tract8 {

// Coefficient template for the eight bands, filled by VoderInit(). Plain
// globals in the .cpp, not header statics: these are WRITTEN at init, and a
// header static would land in flash where writes are silently discarded.
// That failure mode is invisible until the filters produce nothing.
int32_t band_b0[kBands];
int32_t band_a1[kBands];
int32_t band_a2[kBands];

// Phase increment per milli-Hz, Q32. The sawtooth phase is a uint32_t that
// wraps naturally at 2^32, so the increment for frequency f is
//
//   inc = f * 2^32 / fs
//
// Precomputed here for f in milli-Hz to keep a divide off the hot path:
//
//   inc = f_mhz * 2^32 / (fs * 1000)
//
// 2^32 / 48000000 is 89.478, too small to hold usefully as an integer, so
// the multiply is done at runtime in 64 bits against a Q16 constant.
// 2^32 * 65536 / 48000000 = 5864062.5, rounded.
static constexpr uint64_t kIncPerMilliHz_Q16 = 5864063;

void VoderInit(uint32_t seed) {
  (void)seed;

  // Standard RBJ audio-EQ-cookbook constant-skirt bandpass, evaluated in
  // double and stored as Q15. This is the one place floats are allowed: it
  // runs once at boot, before Run() starts the audio interrupt, so the
  // ~360 ns per soft-float operation costs nothing that matters.
  //
  //   w0    = 2*pi*f0/fs
  //   alpha = sin(w0) / (2*Q)
  //   b0 =  alpha,  b1 = 0,  b2 = -alpha
  //   a0 =  1 + alpha,  a1 = -2*cos(w0),  a2 = 1 - alpha
  //
  // Everything is divided through by a0 and the signs of a1/a2 are flipped
  // so the difference equation is a straight sum of products:
  //
  //   y = b0*(x - x2) - a1*y1 - a2*y2
  //
  const double q = 4.0;  // kFilterQ_Q15 / 32768.0, spelled out for clarity

  for (int i = 0; i < kBands; i++) {
    const double w0 = 2.0 * M_PI * (double)kBandHz[i] / (double)kSampleRate;
    const double alpha = sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    const double b0 = alpha / a0;
    const double a1 = (-2.0 * cos(w0)) / a0;
    const double a2 = (1.0 - alpha) / a0;

    // Q15, i.e. a scale of 32768. The largest coefficient across the bank
    // is a1 for the 250 Hz band at -1.99079, so the stored values reach
    // +/-65234 - well inside int32, and the int64 accumulator in Process()
    // absorbs the products.
    //
    // Q13 was tried first and is NOT enough: tools/filter_check.py measured
    // the 250 Hz band landing at 239 Hz, a 74-cent error, with 0.6 dB of
    // level lost. The cause is that a1 approaches -2.0 as the centre
    // frequency falls, so the coefficient needs resolution exactly where
    // Q13 has least to give. Q15 brings that error to 1.4 cents. Do not
    // reduce this shift without re-running the test.
    band_b0[i] = (int32_t)lrint(b0 * 32768.0);
    band_a1[i] = (int32_t)lrint(a1 * 32768.0);
    band_a2[i] = (int32_t)lrint(a2 * 32768.0);
  }
}

void Voder::Init(uint32_t seed) {
  // Build the coefficients here rather than trusting that VoderInit() has
  // already run.
  //
  // THIS IS NOT BELT AND BRACES. The v1.0.1 card was silent because
  // VoderCard is a file-scope global, so its constructor - and this
  // function with it - ran BEFORE main() ever reached VoderInit(). Every
  // biquad was initialised from the still-zeroed coefficient tables, and a
  // biquad with b0 = a1 = a2 = 0 outputs zero forever no matter what you
  // feed it or what gain you apply. Plosives kept working because they are
  // summed after the bank; the LEDs kept moving because they display
  // band_gain, not the filter output. Static initialisation order is not
  // something to be careful about, it is something to design out.
  //
  // VoderInit() is idempotent, so calling it again from main() is harmless.
  VoderInit(seed);

  for (int i = 0; i < kBands; i++) {
    bank_[i].b0 = band_b0[i];
    bank_[i].a1 = band_a1[i];
    bank_[i].a2 = band_a2[i];
    bank_[i].x1 = bank_[i].x2 = 0;
    bank_[i].y1 = bank_[i].y2 = 0;
  }
  for (int i = 0; i < kBands; i++) gain_[i] = 0;
  phase_ = 0;
  phase_inc_ = 0;
  rng_ = seed ? seed : 0xDEADBEEFu;  // xorshift must never be zero
  plosive_ = 0;
  plosive_hp1_ = 0;
  plosive_hp2_ = 0;
  plosive_lp1_ = 0;
  plosive_lp2_ = 0;
  plosive_len_ = kPlosiveSamples;
  plosive_hold_ = false;
  gate_env_ = 0;
  noise_env_ = 0;
  energy_ = 0;
  voiced_ = false;

  // Last line of defence. If b0 is zero here the bank is mute and nothing
  // downstream can tell - so refuse to run silently: fall back to a
  // pass-through (b0 = 1.0 in Q15, no poles) which is wrong-sounding but
  // audible, and therefore diagnosable. Silence is the one failure mode
  // that gives the player no information at all.
  if (bank_[0].b0 == 0) {
    for (int i = 0; i < kBands; i++) {
      bank_[i].b0 = 32768;
      bank_[i].a1 = 0;
      bank_[i].a2 = 0;
    }
  }
}

uint32_t __not_in_flash_func(Voder::Random)() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void __not_in_flash_func(Voder::TriggerPlosive)(int32_t decay_q15) {
  // Map the decay control between a consonant-length click and a full
  // second. Squared, so the short end - where the useful consonant
  // lengths live - gets most of the control's travel.
  const int32_t sq = (int32_t)(((int64_t)decay_q15 * decay_q15) >> 15);
  plosive_len_ = kPlosiveSamples +
                 (int32_t)(((int64_t)(kPlosiveMaxSamples - kPlosiveSamples) *
                            sq) >> 15);
  plosive_ = plosive_len_;
}

void __not_in_flash_func(Voder::SetPlosiveSustain)(bool on) {
  plosive_hold_ = on;
  if (on && plosive_ <= 0) {
    // Opening the gate with no burst running starts one, so the button
    // works as a trigger even from silence.
    plosive_len_ = kPlosiveMaxSamples;
    plosive_ = plosive_len_;
  }
}

// polyBLEP correction around a sawtooth discontinuity.
//
// A naive sawtooth at F0 = 110 Hz has harmonics every 110 Hz all the way to
// Nyquist and beyond; everything above 24 kHz folds back as inharmonic
// rubbish, and the 2600 and 3800 Hz bands sit right where it lands. The
// audible result is a metallic edge that no amount of filtering removes,
// because the aliases are already inside the passband.
//
// polyBLEP fixes it by replacing the instantaneous jump with a two-sample
// polynomial approximation to a bandlimited step. It costs two multiplies
// only on the samples adjacent to the wrap, and needs no table.
//
//   t  = position within one sample of the discontinuity, Q15
//   returns the correction to ADD to the naive value, Q15
static inline int32_t __not_in_flash_func(PolyBlep)(int32_t t_q15) {
  // t in [0,1): approaching the discontinuity   -> t*t - 2t + 1 ... etc.
  // Standard form:  t<dt:  t = t/dt; return t+t - t*t - 1
  //                 t>1-dt: t = (t-1)/dt; return t*t + t+t + 1
  // 32-bit: t_q15 is 0..32767, so t*t peaks at 1.07e9 against int32's
  // 2.1e9. The 64-bit form here cost an __aeabi_lmul on every wrap.
  const int32_t t2 = (t_q15 * t_q15) >> 15;
  return (t_q15 << 1) - t2 - 32768;
}

int32_t __not_in_flash_func(Voder::Process)(const Params& p) {
  // --- excitation -------------------------------------------------------

  // Buzz: bandlimited sawtooth. The phase accumulator is a plain uint32_t
  // wrapping at 2^32, so the raw sawtooth is just the top bits of it.
  int32_t f0 = p.f0_milli_hz;
  if (f0 < kF0MinMilliHz) f0 = kF0MinMilliHz;
  if (f0 > kF0MaxMilliHz) f0 = kF0MaxMilliHz;

  phase_inc_ = (uint32_t)(((uint64_t)f0 * kIncPerMilliHz_Q16) >> 16);
  const uint32_t prev_phase = phase_;
  phase_ += phase_inc_;

  // Naive saw in Q15, -32768..32767.
  int32_t buzz = (int32_t)(phase_ >> 17) - 16384;
  buzz <<= 1;

  // Apply the polyBLEP correction if we just wrapped, or are about to. dt
  // is the phase increment expressed as a fraction of a full cycle.
  if (phase_ < prev_phase) {
    // Wrapped this sample. Distance past the discontinuity, as a fraction
    // of one sample step.
    const uint32_t dt = phase_inc_ ? phase_inc_ : 1;
    const int32_t t = (int32_t)(((uint64_t)phase_ << 15) / dt);
    if (t < 32768) buzz -= PolyBlep(t);
  }

  // Hiss: xorshift32, taken from the top 16 bits because the low bits of a
  // shift-register PRNG are the least well distributed.
  const int32_t noise = ((int32_t)(Random() >> 16)) - 32768;

  // Gate envelopes. A one-pole toward the target would take ~10 ms to
  // settle, which slurs consonant attacks; a linear ramp over kGateRamp
  // samples is both faster and click-free.
  const int32_t v_target = p.voiced_level;
  const int32_t n_target = p.noise_level;
  const int32_t step = 32768 / kGateRamp;
  if (gate_env_ < v_target) {
    gate_env_ += step; if (gate_env_ > v_target) gate_env_ = v_target;
  } else if (gate_env_ > v_target) {
    gate_env_ -= step; if (gate_env_ < v_target) gate_env_ = v_target;
  }
  if (noise_env_ < n_target) {
    noise_env_ += step; if (noise_env_ > n_target) noise_env_ = n_target;
  } else if (noise_env_ > n_target) {
    noise_env_ -= step; if (noise_env_ < n_target) noise_env_ = n_target;
  }

  // Source mix, then ONE envelope over the pair.
  //
  // Breath is a RATIO, not a pair of independent levels. The crossfade
  // decides how much of the excitation is noise, and the gate envelope
  // then scales the result - so "50% breath" means half-noisy sound at
  // whatever level the card is playing, rather than half the level.
  //
  // The old arrangement gated buzz and hiss separately and crossfaded
  // afterwards. With both gates open it behaved identically, but a button
  // gating only one source silenced half the crossfade, so breath at 50%
  // gave HALF THE VOLUME instead of a half-breathy voice. That is not
  // what a breath control means, and it is why the control did not feel
  // like "percentage breathiness".
  //
  // Taking the louder of the two gates as the envelope keeps a single
  // source gate working as a gate: opening only the voiced button still
  // opens the voice, and the breath knob still decides its character.
  const int32_t mix = 32767 - p.source_mix;
  // Each term shifted BEFORE the add, so neither the products nor their
  // sum needs 64 bits. buzz*mix peaks at 2.1e9 - just inside int32 - but
  // the two summed would reach 4.3e9 and overflow, which is why the
  // original used int64 for the whole expression. Shifting first costs
  // one bit of resolution on a crossfade nobody can hear a bit of, and
  // saves two __aeabi_lmul calls per sample.
  const int32_t blended = ((buzz * mix) >> 15) +
                          ((noise * p.source_mix) >> 15);

  const int32_t env = gate_env_ > noise_env_ ? gate_env_ : noise_env_;
  // 32-bit: blended is now bounded by +/-32767 after the shifts above,
  // and env is Q15, so the product cannot exceed 1.07e9.
  int32_t excite = (blended * env) >> 15;

  // Audio In 1 is SUMMED into the excitation, not substituted for it.
  //
  // Replacing was tried first and is the worse instrument. It turns the
  // card into a formant filter and nothing else while a cable is in - the
  // internal voice disappears, so you cannot talk over the drum loop you
  // are filtering, which is the obvious thing to want. Summing keeps both:
  // the external signal and the buzz go through the same eight bands, so
  // whatever vowel the hands are making is imposed on both at once.
  //
  // Summed at unity with the internal sources, not attenuated. Halving it
  // was tried and put a full-scale input 12 dB below the buzz, which is
  // too quiet for external audio to be the main event - and being the main
  // event is the point of patching something in. Level is set by whatever
  // is plugged in, which is where a modular player expects it.
  //
  // The worst case - all eight bands wide open, coherent input - would
  // clip, but a real vowel uses about a fifth of that summed gain and
  // leaves plenty of headroom. main.cpp clamps either way, so the failure
  // mode is audible clipping rather than integer wrap; chain_check.py
  // guards the wrap.
  excite += p.ext_input;

  // Headroom before the filter bank. Each band can contribute up to its
  // full gain, and eight bands summing at once would overflow; scaling the
  // input down by 3 bits here is cheaper than scaling eight outputs.
  // >>4, one bit more than the >>3 this used to be. That bit is what
  // makes the 32-bit accumulator below provably safe rather than merely
  // safe-in-practice; the output shift gives it straight back, so the
  // level is unchanged. See the note on the accumulator.
  excite >>= 4;

  // --- filter bank ------------------------------------------------------

  int32_t sum = 0;
  int32_t energy = 0;

  for (int i = 0; i < kBands; i++) {
    Biquad& f = bank_[i];

    // y = b0*(x - x2) - a1*y1 - a2*y2, all coefficients Q15.
    //
    // The accumulation genuinely needs 64 bits. A single product reaches
    // 65234 * ~20000 = 1.3e9, which still fits int32 - but three of them
    // sum to 4.0e9, which does not. Checked in tools/filter_check.py.
    // PICO_INT64_OPS_IN_RAM keeps the __aeabi_lmul helper this generates
    // out of flash - see CMakeLists.txt.
    // 32-BIT, NOT 64. This is the card's single biggest cost.
    //
    // Three int64 products per band times eight bands is 24 calls to
    // __aeabi_lmul every sample - the M0+ has no 64-bit multiply, so each
    // one is a function call. CV Out 2 measured the ISR at essentially
    // 100% of its 20.83 us budget with the 64-bit form; the model in
    // budget_check.py predicted 44% and was wrong in the same direction
    // and for the same reason WorkshopSpectral's was.
    //
    // The width was never needed. Driving each band at its own centre
    // frequency - the worst case for a resonator - the accumulator peaks
    // around 2.7e8 against int32's 2.1e9, which is 8x headroom. Even the
    // absolute algebraic bound, every term at maximum with the same sign
    // at once, is 1.2e9 with the >>4 input shift below: 1.8x margin on a
    // case no signal can actually produce.
    const int32_t acc = f.b0 * (excite - f.x2) - f.a1 * f.y1 - f.a2 * f.y2;

    // TRUNCATE TOWARD ZERO, not toward negative infinity.
    //
    // An arithmetic >> rounds DOWN, so every sample loses up to one count
    // in the SAME direction. In a feedback path that bias recirculates:
    // fed pure silence with any non-zero state, the 700 Hz band settles at
    // -118 counts and stays there forever rather than decaying to zero.
    // Summed across the bank that is a permanent negative DC offset which
    // moves with the band gains - measured on the bench as a waveform
    // sitting well below centre, with the offset tracking the faders.
    //
    // Rounding by adding half before the shift is WORSE (+586 across the
    // bank), because it biases positive instead. The fix is symmetry: with
    // truncation toward zero the error has no preferred direction, every
    // band settles at exactly 0, and the residual DC in a real vowel drops
    // about fivefold and stops tracking brightness.
    const int32_t y = acc < 0 ? -((-acc) >> 15) : (acc >> 15);

    f.x2 = f.x1; f.x1 = excite;
    f.y2 = f.y1; f.y1 = y;

    // Scale by this band's gain and accumulate.
    //
    // THE GAIN IS SMOOTHED HERE, PER SAMPLE, and that rate is the whole
    // point. The control path recomputes the target at 125 Hz, and
    // applying a target directly - or slewing it at the control rate,
    // which was the first attempt - puts a step in the multiplier every
    // 8 ms. A step in a multiplier is a corner in the output, corners are
    // broadband, and 125 Hz of them is an audible buzz. Slewing at the
    // control rate only replaced one step with four smaller ones at the
    // same rate, which is why it did not help.
    //
    // At 48 kHz a one-pole with a >>6 coefficient settles in about 1.3 ms
    // and no single sample moves the gain by more than a fraction of a
    // count, so there is no edge left to hear.
    //
    // The += form also converges exactly. The control-rate version used
    // (target - cur) >> 2, and an arithmetic shift of a small POSITIVE
    // delta is zero, so a rising gain stalled a few counts short forever
    // while ADC dither jittered the target around it - a buzz that
    // persisted until something moved a control far enough to matter,
    // which is precisely what was reported.
    gain_[i] += (p.band_gain[i] - gain_[i]) >> 6;
    if (gain_[i] != p.band_gain[i] &&
        ((p.band_gain[i] - gain_[i]) >> 6) == 0) {
      // Within the shift's dead zone: close the last few counts outright
      // rather than stalling. One count per sample is 48000 counts a
      // second, so this is instant and still cannot be heard as a step.
      gain_[i] += (p.band_gain[i] > gain_[i]) ? 1 : -1;
    }
    // Also 32-bit: |y| stays under 2^14 in normal operation and gain_ is
    // Q15, so the product fits with room to spare.
    sum += (y * gain_[i]) >> 15;

    // Energy tracking for CV Out 1 and the LEDs. Absolute value is enough;
    // a true RMS would need a square root per band per sample.
    energy += (y >= 0) ? y : -y;
  }

  energy_ = energy >> 3;
  voiced_ = (gate_env_ > noise_env_);

  // --- plosive burst ----------------------------------------------------

  // Summed AFTER the bank, not through it. The Voder's stop keys bypassed
  // the filter chain too: a plosive release is a broadband click, and
  // running it through the formant bank turns it into a filtered thud that
  // reads as a vowel onset rather than a consonant.
  if (plosive_ > 0) {
    // Linear decay over the burst. The ear reads the envelope shape of a
    // stop release as its identity, and linear is close enough.
    //
    // While sustaining, the envelope is held at full instead of decaying,
    // so the same burst becomes a steady noise source. That is what makes
    // the trigger button usable as a gate rather than only as a hit.
    const int32_t env = plosive_hold_
                        ? 32767
                        : (plosive_ << 15) / plosive_len_;
    // The burst is BANDPASSED, not raw noise and not merely lowpassed.
    //
    // Raw noise sounded like white noise. A plain lowpass fixed that and
    // introduced the opposite fault - it kept everything below its corner,
    // including the sub-100 Hz region, and the burst became a thump. A
    // real bilabial release has little energy below 200 Hz, because the
    // lips are closed and nothing radiates. See kPlosiveLoQ15.
    //
    // Two poles up, two poles down, by subtraction: the high pair is a
    // lowpass that removes the hiss, and subtracting a slower tracking of
    // its own output removes the thump. Four multiply-adds, no resonance,
    // nothing to go unstable.
    //
    // The filter runs continuously rather than only during a burst, so it
    // is already settled when one starts - a filter starting from zero
    // state produces a click of its own at the very moment the burst is
    // meant to be shaping one.
    const int32_t raw = ((int32_t)(Random() >> 16)) - 32768;
    plosive_hp1_ += (int32_t)(((int64_t)(raw - plosive_hp1_) *
                               kPlosiveHiQ15) >> 15);
    plosive_hp2_ += (int32_t)(((int64_t)(plosive_hp1_ - plosive_hp2_) *
                               kPlosiveHiQ15) >> 15);
    plosive_lp1_ += (int32_t)(((int64_t)(plosive_hp2_ - plosive_lp1_) *
                               kPlosiveLoQ15) >> 15);
    plosive_lp2_ += (int32_t)(((int64_t)(plosive_lp1_ - plosive_lp2_) *
                               kPlosiveLoQ15) >> 15);
    const int32_t burst = plosive_hp2_ - plosive_lp2_;

    // Scaled by the click level so the burst can be balanced against the
    // voice.
    const int32_t lvl = (int32_t)(((int64_t)env * p.click_level) >> 15);

    // >>17 here. The bandpass costs about 9 dB of level where the earlier
    // lowpass cost 16, so the shift moves with it - the two numbers are
    // the same decision expressed in two places, and changing the filter
    // without changing the shift silently changes the balance.
    //
    // The shift sets the fader's RANGE and kDefaultClickLevel sets where
    // it sits without an 8mu. Those are separate decisions and were
    // briefly conflated: >>17 gave a suitably quiet default but capped
    // the fader at -3.3 dB, so the loud-click option disappeared. Keeping
    // the range here and lowering the default instead gives both - a
    // default 18 dB under the voice, and a fader that still reaches
    // slightly above it for a deliberate hit.
    sum += (int32_t)(((int64_t)burst * lvl) >> 17);

    if (!plosive_hold_) plosive_--;
  }

  // --- output -----------------------------------------------------------

  // Back down to the 12-bit DAC range. The >>3 before the bank plus this
  // shift is the whole gain staging, and tools/chain_check.py measures it
  // in DAC counts - which is the only unit that says whether anyone can
  // hear the card.
  //
  // >>4 was the original and left a vowel peaking around 130 counts of a
  // possible 2047: audible, but thin, and it got thinner when the vowel
  // table was rescaled to 16000 to give the 8mu's faders room to boost.
  // >>2 puts a vowel near 530 counts with the worst case - all eight bands
  // open, coherent input - at 1568, still inside the DAC. Do not raise it
  // further without re-running chain_check.py: >>1 overflows the DAC on
  // the coherent case, and that clips rather than clamping gracefully.
  return sum >> 1;
}

}  // namespace tract8
