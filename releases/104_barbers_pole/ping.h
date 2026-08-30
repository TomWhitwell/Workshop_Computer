// ping.h - the alt-boot HIDDEN BARBER mode: an invisible barber's pole you
// strike. (The file and its types keep the name `ping` because that is what a
// struck, decaying voice IS - the mode's display name is HIDDEN BARBER.)
//
// The pole keeps climbing, silently. A trigger voices a PING at whatever
// pitches the pole is currently at - and that ping then decays WITHOUT
// climbing, because it is a snapshot rather than a window onto the moving
// stack.
//
// So the illusion is still running the whole time, but you only ever hear
// where it happens to be at the moments you strike it. Trigger repeatedly and
// each ping comes from a different point on the pole, building a chord out of
// one silently-rising structure. Because the pole wraps every octave, the
// sequence of pitches you can strike is endless but bounded - the barber's
// pole becomes a scale you play rather than a drone you listen to.
//
// This replaced SPIRAL as the alt-boot. SPIRAL was a pitch-shifting delay,
// which is a fine effect but had nothing to do with the card's own idea and
// occupied a lot of RAM to say so.
//
// A VOICE is a frozen copy of the oscillator bank: the per-layer increments
// and window gains as they were at the trigger, plus an envelope. Nothing in a
// voice moves except its envelope, which is exactly what "does not climb once
// voiced" means.

#ifndef SHEPARD_PING_H_
#define SHEPARD_PING_H_

#include <stdint.h>
#include <string.h>

#include "fixed.h"
#include "shepard.h"

namespace shepard {

// Four voices. Enough that fast triggering accumulates a chord from different
// moments on the pole - which is the point - without the density becoming a
// cloud. Measured ~23% of the per-sample budget raw at 12 layers, comparable
// to what normal boot already runs.
static constexpr int kPingVoices = 4;

// Decay coefficients, Q15, one per knob step. The envelope is multiplied by
// one of these every sample, via MulQ15.
//
// TWO WRONG APPROACHES, both worth recording because each looked right:
//
//   A SHIFT. `env -= env >> n` is the obvious one-pole and it does not work
//   here: the envelope is 15 bits, so `env >> 17` is ZERO for every value it
//   can hold - the decay never begins and the voice rings forever. Anything
//   above ~14 stalls outright.
//
//   A BESPOKE Q20 MULTIPLY. Splitting env at bit 12 made the high term
//   4095 * 1048497 = 4.29e9, twice int32's limit - it wrapped NEGATIVE, the
//   envelope collapsed on its first sample, and every ping was a click
//   regardless of the knob. Moving the split to 15 fixed that term and broke
//   the other one (lo * c reaching 3.4e10). There is no single split that
//   works: hi needs s < 11 and lo needs s > 13.
//
// So: MulQ15, which is already proven safe for |x| up to 20e6 against an
// envelope peaking at 16.8e6, with the envelope carried in Q24 so a
// coefficient just under 1.0 still has bits to bite on. Measured 11 ms to
// 1.7 s:
//
//     [0]  0.011 s   a click
//     [4]  0.040 s   a pluck
//     [8]  0.137 s   a short bell
//    [12]  0.458 s   a bell
//    [16]  1.716 s   a long ring
// The table itself is declared in shepard.h and defined in shepard.cpp.
static constexpr int kPingDecaySteps = 17;

// The envelope is carried in Q24 - Q15 shifted up by 9 - purely so the decay
// multiply has bits to work with. Audio uses env >> 9.
static constexpr int kPingEnvShift = 9;
static constexpr int32_t kPingEnvFull = 32767 << kPingEnvShift;

// Attack, as a one-pole shift. Fast enough to read as a strike, slow enough
// that the oscillators do not start from a step - a hard start on twelve
// oscillators at once is a click, which is the whole reason the normal-boot
// engine slews everything.
static constexpr int kPingAttack = 6;      // ~1.3 ms

class PingVoice {
 public:
  void Init() {
    env_ = 0;
    target_ = 0;
    active_ = false;
    age_ = 0;
    for (int i = 0; i < kMaxLayers; ++i) {
      phase_[i] = 0;
      inc_[i] = 0;
      win_l_[i] = 0;
      win_r_[i] = 0;
      oct_rate_[i] = 65536u;
    }
  }

  // Take a snapshot of the pole and start ringing.
  //
  // The PHASES are reset to zero rather than copied. That is deliberate: every
  // layer starting at phase zero means every layer starts at zero amplitude,
  // so the attack has nothing to click against. Copying the pole's running
  // phases would start each oscillator mid-cycle, and twelve of those together
  // is a broadband transient.
  void __not_in_flash_func(Strike)(int layers, const uint32_t* inc,
                                   const int16_t* win_l, const int16_t* win_r,
                                   const uint32_t* oct_rate) {
    layers_ = layers;
    for (int i = 0; i < layers; ++i) {
      phase_[i] = 0;
      inc_[i] = inc[i];
      win_l_[i] = win_l[i];
      win_r_[i] = win_r[i];
      oct_rate_[i] = oct_rate[i];
    }
    target_ = kPingEnvFull;
    active_ = true;
    age_ = 0;
  }

  bool __not_in_flash_func(Active)() const { return active_; }
  uint32_t __not_in_flash_func(Age)() const { return age_; }
  int32_t __not_in_flash_func(Env)() const { return env_ >> kPingEnvShift; }
  int __not_in_flash_func(Layers)() const { return layers_; }
  uint32_t __not_in_flash_func(OctRate)(int i) const { return oct_rate_[i]; }
  int32_t __not_in_flash_func(WinL)(int i) const { return win_l_[i]; }
  int32_t __not_in_flash_func(WinR)(int i) const { return win_r_[i]; }

  // Advance the envelope. Call once per sample per voice.
  void __not_in_flash_func(Tick)(int32_t decay_q15) {
    if (!active_) return;
    if (target_ > 0) {
      // Attack, then release once it has arrived.
      env_ += (target_ - env_) >> kPingAttack;
      if (env_ > kPingEnvFull - (kPingEnvFull >> 6)) target_ = 0;
    } else {
      // MulQ15, not a bespoke multiply. Two attempts at a wider one both
      // OVERFLOWED int32 - see the note on kPingDecay - and MulQ15 is already
      // proven safe for |x| up to 20e6, where the envelope peaks at 16.8e6.
      env_ = MulQ15(decay_q15, env_);
      // A one-pole never truly reaches zero, so retire the voice when it is
      // inaudible rather than leaving it running forever and stealing a slot.
      // Retire at -58 dB rather than -72. A voice below this is inaudible,
      // and every sample it stays "active" costs a full 12-layer pass - which
      // with four voices and the live path is what pushed the ISR over budget.
      if (env_ < (40 << kPingEnvShift)) {
        env_ = 0;
        active_ = false;
      }
    }
    ++age_;
  }

  // One layer's oscillator contribution, already enveloped.
  int32_t __not_in_flash_func(Osc)(int i) {
    phase_[i] += inc_[i];
    return MulQ15(SinQ15(phase_[i]), env_ >> kPingEnvShift);
  }

 private:
  uint32_t phase_[kMaxLayers];
  uint32_t inc_[kMaxLayers];
  uint32_t oct_rate_[kMaxLayers];
  int16_t win_l_[kMaxLayers];
  int16_t win_r_[kMaxLayers];
  int32_t env_ = 0;
  int32_t target_ = 0;
  uint32_t age_ = 0;
  int layers_ = 0;
  bool active_ = false;
};

class PingBank {
 public:
  void Init() {
    for (int v = 0; v < kPingVoices; ++v) voice_[v].Init();
  }

  // Find a slot: a free one, else the OLDEST. Stealing the oldest rather than
  // the quietest keeps the chord evolving - the newest four strikes are always
  // the ones sounding, which is what a player expects.
  void __not_in_flash_func(Strike)(int layers, const uint32_t* inc,
                                   const int16_t* win_l, const int16_t* win_r,
                                   const uint32_t* oct_rate) {
    int slot = -1;
    uint32_t oldest = 0;
    for (int v = 0; v < kPingVoices; ++v) {
      if (!voice_[v].Active()) { slot = v; break; }
      if (voice_[v].Age() >= oldest) { oldest = voice_[v].Age(); slot = v; }
    }
    if (slot >= 0) voice_[slot].Strike(layers, inc, win_l, win_r, oct_rate);
  }

  PingVoice& __not_in_flash_func(Voice)(int v) { return voice_[v]; }

  void __not_in_flash_func(Tick)(int32_t decay_q15) {
    for (int v = 0; v < kPingVoices; ++v) voice_[v].Tick(decay_q15);
  }

  bool __not_in_flash_func(AnyActive)() const {
    for (int v = 0; v < kPingVoices; ++v) {
      if (voice_[v].Active()) return true;
    }
    return false;
  }

 private:
  PingVoice voice_[kPingVoices];
};

}  // namespace shepard

#endif  // SHEPARD_PING_H_
