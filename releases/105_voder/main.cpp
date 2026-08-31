// TRACT8 - a Voder for the Music Thing Modular Workshop System Computer.
//
// An eight-band reimplementation of the 1939 Bell Labs Voder, played from a
// Music Thing 8mu over USB MIDI Host or from the card's own panel.
//
// Structure:
//   voder.cpp     excitation and the 8-band filter bank
//                                       (verified: tools/filter_check.py,
//                                                  tools/excite_check.py)
//   vowels.h      vowel gain vectors    (verified: tools/vowel_check.py)
//   midi8mu.cpp   8mu CC/note dispatch  (verified: tools/midi_check.py)
//   usb_core1.cpp Core 1 USB host pump
//   main.cpp      panel, CV, LEDs, core scheduling
//
// Jacks:
//   Audio In 1   exciter  external audio, summed with buzz and noise
//   Audio In 2   volume   CV, adds to the fader
//   CV In 1      pitch    1V/oct
//   CV In 2      formant  bipolar, sweeps the vowel cube diagonally
//   Pulse In 1   click    trigger
//   Pulse In 2   glottal  gate
//
// All four CV inputs ADD to whatever the panel or the 8mu has set, rather
// than replacing it. That is what makes the card useful fed from random
// voltages: a slow random into CV 2 wanders the vowel around wherever the
// controls are parked, and gates into the pulses chatter it, without ever
// needing the patch to supply a sensible absolute value.
//
// The Voder's controls map onto this card as:
//   wrist bar (buzz/hiss)  -> notes C2/C3, Knob 3, Pulse In 2, Switch down
//   foot pedal (pitch)     -> accelerometer CC 42/43, CV In 1, Knob 1 page 2
//   ten filter keys        -> 8mu faders CC 34-41, Knob 1/2 on the panel
//   three stop keys        -> note C4, Pulse In 1

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"

#include "shared.h"
#include "voder.h"
#include "vowels.h"
#include "midi8mu.h"  // for kButtonBreath

using namespace tract8;

extern "C" void core1_entry(void);

// Boot mute, samples. 0.5 s of silence while the DAC settles and Core 1
// brings the USB controller up. Without it the card clicks on power-up.
static constexpr int32_t kBootMute = 24000;

// Boot splash, samples. Runs past the mute so there is visible feedback
// that the card is alive before it makes any sound.
static constexpr int32_t kBootSplash = 36000;

// Panel read rate divider. Reading one knob per sample round-robin costs a
// third of an ADC read per sample instead of three, and knobs do not move
// at 48 kHz.
// Four, to match ComputerCard's 4-state input mux exactly.
//
// It was 3, and a 3-phase read against a 4-state mux beats at
// lcm(3,4) = 12 samples = 4 kHz, which is audible as a whine. Any value
// that is not a multiple of 4 reintroduces it. See ReadPanel().
static constexpr int32_t kPanelDiv = 4;

// BABBLE alt-boot: hold the switch DOWN at power-on.
//
// Each knob drives several parameters at once, so three knobs and a gate
// give chattering speech with no controller attached. The normal mode maps
// one knob to one parameter, which is right for playing deliberately and
// wrong for getting a texture going quickly.
//
// The boot detection is the NIBBLE-KO method, verbatim, because that one is
// proven on this hardware and the obvious alternative is a known trap:
//
//   - ONE reading, taken after the full 0.5 s boot window has elapsed.
//   - NEVER "Down seen at any point". The switch reads Down until it
//     settles, so latching on any sighting latches on every boot.
//     WorkshopZX and WorkshopBio both shipped that bug.
//
// Down is safe as the trigger here precisely because the reading happens
// once, after the window, by which point the filter has settled.
// BABBLE chatter rate, in samples per syllable. 2400 is 20 Hz, a trill;
// 24000 is 2 Hz, unhurried speech. Knob X moves between them.
//
// The first version ran from the click length (8 ms, 125 Hz) to 250 ms,
// which was wrong at both ends: 125 Hz is a buzz rather than chatter, and
// at the slow end the click was a full second long against a 250 ms gap,
// so the bursts overlapped into a continuous wash instead of separating
// into syllables.
static constexpr int32_t kBabbleMinPeriod = 2400;   // 20 Hz
static constexpr int32_t kBabbleMaxPeriod = 24000;  // 2 Hz

// AUTO-CHATTER: hold the momentary switch DOWN for two seconds in BABBLE
// to engage, tap it to leave. It generates its own gates - varied lengths,
// grouped into phrases with breaths between them - so the card talks
// unprompted, as if something were feeding Pulse In 2.
//
// The switch sounds throughout that two-second hold, because it is a gate
// in its own right (see the gate block in ProcessSample). So the arming
// gesture is audible as a held note that then hands over to the chatter,
// rather than two seconds of silence ending in a surprise.
//
// The shape is what makes it read as speech rather than as a gate
// sequencer. Real utterances are bursts of a few syllables with pauses
// between, not an even stream, so this generates PHRASES: a run of 2-7
// syllables of differing lengths, then a gap long enough to read as taking
// a breath. An even stream of identical gates sounds like a machine no
// matter how the syllables themselves are shaped.
static constexpr int32_t kLongPressSamples = 96000;   // 2 s at 48 kHz

// Syllables per phrase, and the pause after one. The pause is drawn from a
// range rather than fixed, so phrases do not fall into a rhythm.
static constexpr int32_t kPhraseMinSyllables = 2;
static constexpr int32_t kPhraseMaxSyllables = 7;
static constexpr int32_t kBreathMinSamples = 9600;    // 0.2 s
static constexpr int32_t kBreathMaxSamples = 38400;   // 0.8 s

// How often a syllable also gets a plosive, out of 256. Consonants on
// every syllable read as percussion - the whole reason the automatic
// click was removed - but a few give the phrase articulation.
static constexpr int32_t kPlosiveChance = 70;         // ~27%

// Default click level with no 8mu attached, Q15. About -14 dB.
//
// The click is a broadband burst summed AFTER the filter bank, so it is
// never attenuated by the vowel the way everything else is - at full scale
// it dominates the card completely, which is how it shipped and how it was
// reported. This sits it under the voice as punctuation. Fader 6 reaches
// full for when a loud one is wanted.
static constexpr int32_t kDefaultClickLevel = 3000;

// Default click decay with no 8mu attached, Q15. Maps to about 12 ms
// through the squared curve in voder.cpp.
//
// It was 6000, which is 41 ms - long enough to read as a drum hit rather
// than a consonant, and part of why the burst was reported as a bomb. A
// real plosive release is 5 to 15 ms. The fader still reaches a full
// second at the top for sustained textures.
static constexpr int32_t kDefaultClickDecay = 2000;

// Vowel morph recompute interval, samples. 384 samples is 125 Hz - far
// faster than a hand can turn a knob, and it takes the morph from roughly
// a quarter of the ISR budget down to under one percent. See ReadPanel().
static constexpr int32_t kMorphDiv = 384;

static inline int32_t Clamp15(int32_t x) {
  if (x > 32767) return 32767;
  if (x < 0) return 0;
  return x;
}

// Limiter into the 12-bit DAC range: exactly linear below the knee.
//
// The gain staging left 11.8 dB of headroom unused, because the shift had
// to be sized for the WORST case - all eight bands wide open with a
// coherent input - rather than for the vowels anyone actually plays. A
// real vowel uses about three bands, so the card ran ~10 dB quieter than
// it needed to and the noise floor sat correspondingly closer to it.
//
// v1.20.0 took that headroom back with a cubic soft clipper and THAT WAS
// A MISTAKE. A cubic bends from the very first sample - there is no
// linear region at all - so every vowel was distorted all the time: 28 dB
// of THD at ordinary playing level, with the third harmonic of a 700 Hz
// formant landing near 2 kHz and the fifth near 3.5 kHz. On the bench
// that was heard as a whine at roughly 3750 Hz, loudest with the volume
// fader full (which drives the level into the curve) and varying with the
// brightness fader (which decides WHICH band is loudest, and so where its
// harmonics land). A test build with this stage bypassed had no whine.
//
// This is a hard-knee limiter instead. Below kKnee it is the identity -
// not approximately linear, EXACTLY linear, so ordinary playing is
// bit-for-bit undistorted. Only genuine peaks above the knee bend, with a
// quadratic landing onto the rail. A vowel at the shipped gain measures
// -92 dB THD where the cubic measured -28 dB.
static inline int32_t SoftClip(int32_t x) {
  constexpr int32_t kLim = 2047;
  constexpr int32_t kKnee = 1500;
  constexpr int32_t kSpan = kLim - kKnee;

  const int32_t mag = x < 0 ? -x : x;
  if (mag <= kKnee) return x;  // the common case, untouched

  const int32_t over = mag - kKnee;
  int32_t y;
  if (over >= 2 * kSpan) {
    y = kLim;
  } else {
    // Quadratic landing: slope 1 at the knee, 0 at the rail.
    y = kKnee + over - (over * over) / (4 * kSpan);
    if (y > kLim) y = kLim;
  }
  return x < 0 ? -y : y;
}

class VoderCard : public ComputerCard {
 public:
  VoderCard() {
    voder_.Init(time_us_32() | 1u);

    boot_mute_ = kBootMute;
    boot_splash_ = kBootSplash;
    panel_phase_ = 0;
    morph_phase_ = 0;
    // Seeded on the first pass through ReadPanel rather than at zero,
    // so the card does not ramp its vowel up from nothing at boot.
    knob_main_ = knob_x_ = knob_y_ = 0;
    knob_main_acc_ = knob_x_acc_ = knob_y_acc_ = 0;
    vol_cv_acc_ = formant_cv_acc_ = 0;
    knobs_seeded_ = false;

    // Panel defaults chosen so an untouched card speaks immediately:
    // a mid-open back vowel, moderate pitch, flat brightness, all buzz.
    panel_openness_ = 20000;
    panel_front_ = 0;
    panel_breath_ = 0;
    panel_pitch_ = 8000;
    panel_bright_ = 16384;
    panel_round_ = 0;        // spread; the unrounded vowels

    last_plosive_ = 0;
    last_pulse1_ = false;
    vowel_cv_ = vowel_smooth_ = 0;
    led_phase_ = 0;
    energy_smooth_ = 0;
    formant_cv_ = 0;
    babble_decay_ = 16384;
    babble_count_ = 0;
    babble_open_ = false;
    auto_count_ = 0;
    auto_left_ = 0;
    down_held_ = 0;
    babble_animate_ = 12000;
    babble_consonant_ = 9000;
    anim_open_ = anim_front_ = anim_pitch_ = 0;
    auto_rng_ = time_us_32() | 1u;
    auto_chatter_ = false;
    auto_open_ = false;
    auto_plosive_ = false;
    gate_seen_ = false;
    babble_ = false;
    diag_use_ext_ = false;
    diag_gated_ = false;

    // Start from the panel defaults so the card makes a recognisable
    // sound the instant it boots, rather than silence until something is
    // touched.
    g_state.openness = panel_openness_;
    g_state.front = panel_front_;
    g_state.breath = panel_breath_;
    g_state.pitch = panel_pitch_;
    g_state.bright = panel_bright_;
    g_state.openness_from_midi = 0;
    g_state.front_from_midi = 0;
    g_state.breath_from_midi = 0;
    g_state.pitch_from_midi = 0;
    g_state.bright_from_midi = 0;
    g_state.click_level = 32767;
    g_state.click_decay = 32767;
    g_state.click_level_from_midi = 0;
    g_state.click_decay_from_midi = 0;
    g_state.click_gate = 0;
    g_state.round = 0;
    g_state.round_from_midi = 0;
    g_state.volume = 32767;
    // Full, so BABBLE is not silent before fader 7 is first moved.
    g_state.volume_fader = 32767;
    g_state.volume_from_midi = 0;
    g_state.muted = 0;
    g_state.breath_button_a = 0;
    g_state.breath_button_d = 0;
    g_state.midi_mute = 0;
    g_state.midi_gate = 0;
    g_state.freeze = 0;
    g_state.midi_connected = 0;
    g_state.plosive_count = 0;

    int32_t init_gains[kNumBands];
    BlendVowel(panel_openness_, panel_front_, 0, init_gains);
    for (int i = 0; i < kNumBands; i++) {
      g_state.band_gain[i] = Clamp15(init_gains[i]);
    }
  }

  // AUTO-CHATTER phrase generator. Returns the state of the gate it is
  // pretending to receive on Pulse In 2.
  //
  // Three levels of structure, because speech has three:
  //   - a SYLLABLE is a gate high for a while, of a length drawn per
  //     syllable so no two are quite alike;
  //   - a PHRASE is 2-7 syllables run together;
  //   - a BREATH is the gap between phrases.
  //
  // Without the phrase level this is a gate sequencer with a random clock,
  // which sounds mechanical however the syllables are shaped. The pauses
  // are what make it read as something with lungs.
  bool __not_in_flash_func(AutoChatterGate)() {
    if (--auto_count_ > 0) return auto_open_;

    if (auto_open_) {
      // A syllable just ended. Either continue the phrase with a short
      // gap, or take a breath.
      auto_open_ = false;
      if (--auto_left_ > 0) {
        // Within a phrase: a short gap, proportional to the syllable
        // length so the rate knob still governs the tempo.
        auto_count_ = kBabbleMinPeriod + (int32_t)(AutoRandom() % 4800u);
      } else {
        auto_count_ = kBreathMinSamples +
                      (int32_t)(AutoRandom() %
                                (uint32_t)(kBreathMaxSamples -
                                           kBreathMinSamples));
      }
      return false;
    }

    // A gap just ended. Start a syllable; if the phrase is spent, start a
    // new one and decide its length.
    if (auto_left_ <= 0) {
      auto_left_ = kPhraseMinSyllables +
                   (int32_t)(AutoRandom() %
                             (uint32_t)(kPhraseMaxSyllables -
                                        kPhraseMinSyllables + 1));
    }

    auto_open_ = true;

    // A new syllable: draw the animation offsets it will hold. Doing this
    // per syllable rather than continuously is what makes it sound like
    // speech - a spoken syllable holds its vowel and the next one differs,
    // where a continuous modulation is a vibrato or a siren.
    if (babble_animate_ > 0) {
      const int32_t amt = babble_animate_;
      anim_open_ = (int32_t)(((int64_t)((int32_t)(AutoRandom() % 24000u) -
                                        12000) * amt) >> 15);
      anim_front_ = (int32_t)(((int64_t)((int32_t)(AutoRandom() % 24000u) -
                                         12000) * amt) >> 15);
      anim_pitch_ = (int32_t)(((int64_t)((int32_t)(AutoRandom() % 8000u) -
                                         4000) * amt) >> 15);
    } else {
      anim_open_ = anim_front_ = anim_pitch_ = 0;
    }

    // Syllable length: the babble rate sets the centre and this varies
    // around it by up to 2x, so a phrase has long and short syllables in
    // it the way a spoken one does.
    // Squared, so natural speech sits in the MIDDLE of the rate knob.
    //
    // Linear put 3-7 syllables per second - the range human speech
    // actually occupies - across the top third of the travel, with the
    // whole lower half spent on rates slower than anyone talks. Squaring
    // moves that band to roughly 35-75% of the knob, which is where a
    // player expects the useful part of a control to be.
    const int32_t sq =
        (int32_t)(((int64_t)babble_decay_ * babble_decay_) >> 15);
    const int32_t base =
        kBabbleMinPeriod +
        (int32_t)(((int64_t)(kBabbleMaxPeriod - kBabbleMinPeriod) * sq) >> 15);
    auto_count_ = base + (int32_t)(AutoRandom() % (uint32_t)(base + 1));

    // Some syllables get a consonant. Counted, not flagged, so the ISR
    // path is identical to a MIDI or jack trigger.
    // The consonant knob sets how many syllables get one. At zero the
    // voice is legato; at full nearly every syllable is articulated.
    // Capped at 3/4, deliberately. At the top of its travel the knob
    // should give a heavily articulated voice, not a consonant on every
    // syllable - that is the percussion failure mode the automatic click
    // was removed to avoid, and a control that can reach it will be found
    // there by accident.
    uint32_t chance = (uint32_t)(babble_consonant_ >> 7);
    if (chance > 192) chance = 192;
    if ((AutoRandom() & 0xFF) < chance) {
      auto_plosive_ = true;
    }
    return true;
  }

  // xorshift32, seeded from the timer at construction. Separate from the
  // engine's PRNG so a phrase does not perturb the noise source.
  uint32_t __not_in_flash_func(AutoRandom)() {
    auto_rng_ ^= auto_rng_ << 13;
    auto_rng_ ^= auto_rng_ >> 17;
    auto_rng_ ^= auto_rng_ << 5;
    return auto_rng_;
  }

  // True when the 8mu is present AND has actually sent this control.
  //
  // Both halves matter. The flag alone latches forever, so unplugging the
  // controller used to leave the panel dead for the rest of the session -
  // the knobs did nothing and there was no way to get them back without a
  // power cycle. Requiring midi_connected as well hands every control back
  // to the panel the moment the cable comes out, which is the only
  // behaviour that makes sense: the card must always be playable from its
  // own front panel.
  //
  // The flag is still needed alongside it, so that an 8mu sitting plugged
  // in but untouched does not seize a knob the player has a hand on.
  bool __not_in_flash_func(MidiOwns)(uint8_t flag) const {
    return g_state.midi_connected && flag;
  }

  virtual void __not_in_flash_func(ProcessSample)() override {
    // --- boot mute --------------------------------------------------------
    if (boot_mute_ > 0) {
      boot_mute_--;

      // ONE reading, after the full window has elapsed - tested after the
      // decrement so the last sample is included. See kBabbleLfoRate for
      // why this is not "Down seen at any point".
      if (boot_mute_ == 0) {
        // BABBLE IS THE DEFAULT; holding Down at power-on gives the
        // one-knob-one-parameter mode instead.
        //
        // Swapped round after playing: the macro mode is what the card is
        // for, and the deliberate mode is the specialist one. A card
        // should do its most characteristic thing when you simply turn it
        // on, not require a gesture to reach it.
        //
        // The single-reading discipline protects both senses equally: the
        // switch reads Down until it settles (~46 ms), and this reading
        // happens once after the full 0.5 s window, so an unsettled read
        // cannot leak into either mode. Latching on "Down seen at any
        // point" would have been fatal in EITHER direction.
        babble_ = (SwitchVal() != Switch::Down);
      }
      AudioOut1(0);
      AudioOut2(0);
      CVOut1(0);
      CVOut2(0);
      PulseOut1(false);
      PulseOut2(false);
      UpdateLeds();
      if (boot_splash_ > 0) boot_splash_--;
      return;
    }
    if (boot_splash_ > 0) boot_splash_--;

    // --- panel, at a reduced rate ----------------------------------------
    ReadPanel();

    // --- assemble the parameter set --------------------------------------
    Params p;

    // Band gains come from g_state, which both the 8mu (Core 1) and the
    // panel (below, in ReadPanel) write to. Last writer wins, deliberately:
    // there is no mode switch, so a fader move and a knob turn are equally
    // valid ways to move a formant and the player can use either at any
    // moment. The 8mu only transmits on change, so a parked fader never
    // fights a moving knob.
    for (int i = 0; i < kBands; i++) {
      p.band_gain[i] = g_state.band_gain[i];
    }

    // Pitch. Fader 4 if the 8mu has sent it, otherwise Knob 1 on page 2.
    // Then 1V/oct from CV In 1 on top, so the card still tracks a keyboard
    // whichever control set the base note.
    const int32_t pitch_src =
        MidiOwns(g_state.pitch_from_midi) ? g_state.pitch : panel_pitch_;
    int32_t f0 = kF0MinMilliHz +
                 (int32_t)(((int64_t)pitch_src *
                            (kF0MaxMilliHz - kF0MinMilliHz)) >> 15);

    if (Connected(Input::CV1)) {
      f0 = ApplyOctaves(f0, (int32_t)CVIn1());
    }
    p.f0_milli_hz = f0;

    // Breath: fader 3 if the 8mu has sent it, otherwise Knob 3, plus
    // whatever buttons A and D are adding while held.
    //
    // The buttons ADD rather than set, so they are a gesture on top of
    // wherever the fader is parked - press one over a voiced sound and it
    // turns breathy without losing the pitch. Both buttons together give
    // twice as much, which is the obvious reading of holding both.
    int32_t breath = MidiOwns(g_state.breath_from_midi) ? g_state.breath : panel_breath_;

    if (g_state.breath_button_a) breath += kButtonBreath;
    if (g_state.breath_button_d) breath += kButtonBreath;
    p.source_mix = Clamp15(breath);

    // Gates. The 8mu's buttons and a patched gate are OR'd - either opens a
    // source. With no controller and nothing patched, both sources are open
    // so the card drones, which is what you want when exploring the vowels.
    //
    // The gate is taken as OPEN unless Pulse In 2 has actually been seen
    // going high at some point. Testing Connected(Pulse2) alone was the
    // first hardware run's silence bug, or half of it: if the
    // normalisation probe reports Pulse2 connected when nothing is
    // patched, PulseIn2() reads low forever, both excitation levels sit at
    // zero and the card is mute. Plosives still sounded because they are
    // summed after the filter bank - which is the symptom that found this.
    //
    // Latching on "have we ever seen this jack go high" means a
    // misdetected jack leaves the card droning (recoverable, obvious)
    // rather than silent (looks broken). A real gate patched in takes
    // control the first time it rises.
    // Button 2 on the 8mu is OR'd in here, so a held button and a gate
    // patched into Pulse In 2 are the same thing to everything downstream.
    //
    // gate_seen_ still latches only on the JACK. A button press must not
    // make the card think a cable has appeared: if it did, releasing the
    // button would leave the gate closed and the card silent with nothing
    // patched, which is the failure mode the latch exists to prevent.
    if (PulseIn2()) gate_seen_ = true;
    const bool jack_gate = gate_seen_ ? PulseIn2() : true;
    // The switch is read here rather than at its own block below, because
    // it is now a GATE and the gate has to be assembled before the voice
    // is. Held down it opens the voice for as long as it is held, exactly
    // like Pulse In 2 - it is the panel's copy of that jack.
    const bool down = (SwitchVal() == Switch::Down);
    const bool ext_gate = jack_gate || g_state.midi_gate || down;
    // The 8mu no longer gates the sources separately - button 1 is a
    // mute now, and breath already covers the buzz/noise balance better
    // than two buttons could.
    // Button 2 overrides the syllable gate, so holding it sustains rather
    // than chatters. It is the only control that does; everything else
    // goes through the same envelope.
    //
    // The switch sustains for the same reason. A finger held on a button
    // is one continuous intent, not a stream of events, so it opens the
    // voice for the whole hold - INCLUDING the two seconds it takes to
    // arm auto-chatter, which would otherwise be the one gesture on the
    // card that goes quiet while you perform it.
    const bool open = (g_state.midi_gate || down) ? true
                    : babble_                     ? babble_open_
                                                  : ext_gate;
    p.voiced_level = open ? 32767 : 0;
    p.noise_level = open ? 32767 : 0;

    // Click level and decay.
    //
    // The panel has no knobs free for these, so the default is what the
    // switch-down key and Pulse In 1 get. It was full scale, and reported
    // as far too loud - which it was: the click is a broadband noise burst
    // summed AFTER the filter bank, so unlike everything else on the card
    // it is never attenuated by a vowel. At full it simply dominates.
    //
    // kDefaultClickLevel is about -14 dB, which sits it under the voice as
    // punctuation rather than over it as percussion. Fader 6 still reaches
    // full for when a loud click is wanted.
    p.click_level =
        MidiOwns(g_state.click_level_from_midi) ? g_state.click_level
        : babble_ ? (kDefaultClickLevel +
                     (int32_t)(((int64_t)babble_consonant_ * 12000) >> 15))
                  : kDefaultClickLevel;

    // Decay defaults short, not long. A full-length default made every
    // panel trigger a one-second noise wash; a consonant-length click is
    // what a plosive input should give you before anyone touches a fader.
    p.click_decay =
        MidiOwns(g_state.click_decay_from_midi) ? g_state.click_decay
        : babble_                               ? babble_decay_
                                                : kDefaultClickDecay;

    // Audio In 1 is an EXTERNAL EXCITER, summed with the internal buzz
    // and noise inside the engine.
    //
    // It was briefly the breath CV instead, and the two genuinely cannot
    // share a jack: a slow breath voltage means nothing to a bank of
    // bandpass filters, which have no DC gain, while an audio signal
    // summed into the excitation is not a balance control. They are
    // different functions, not two readings of one.
    //
    // The jack goes to the exciter because breath is the most reachable
    // control on the card already - fader 3, Knob 3, and buttons A and D
    // all reach it - while external audio had no route at all. Formant
    // keeps CV In 2 despite also having faders, knobs and tilt, because
    // sweeping vowels from a random voltage is the point of the card.
    // <<4 puts a full-scale input level with the internal sources, so a
    // patched signal can carry the sound rather than sitting under it.
    p.ext_input = Connected(Input::Audio1) ? ((int32_t)AudioIn1() << 4) : 0;

    // --- plosive triggers -------------------------------------------------
    // Counted, not flagged: fire once per increment Core 1 has made since
    // we last looked, so a burst that lands between two samples is never
    // lost and never doubled.
    const uint32_t pc = g_state.plosive_count;
    if (pc != last_plosive_) {
      last_plosive_ = pc;
      voder_.TriggerPlosive(p.click_decay);
    }

    // Button 1 holds the click open while held. At a long decay setting
    // that turns the burst into a sustained noise source; at a short one
    // it is still a hit, because the burst has already decayed by the
    // time the finger lifts.
    voder_.SetPlosiveSustain(g_state.click_gate != 0);

    // Pulse In 1, rising edge.
    const bool pulse1 = PulseIn1();
    if (pulse1 && !last_pulse1_) voder_.TriggerPlosive(p.click_decay);
    last_pulse1_ = pulse1;

    // BABBLE: a HELD gate on either pulse input chatters by itself, so a
    // single sustained gate is enough to get the card talking.
    //
    // The chatter gates the VOICE. It used to fire a plosive on every
    // syllable, which put a click on the front of each one and made the
    // whole mode read as percussion rather than as speech. Consonants are
    // still available from Pulse In 1, which triggers clicks in every
    // mode, so the player chooses when to have them.
    //
    // AUTO-CHATTER supplies its own gate when engaged, so the same code
    // path runs whether the gate came from a jack or from here.
    // Button 2 is NOT in the chatter gate.
    //
    // It opens the voice continuously instead, which is what a finger on
    // a button means. A gate from a sequencer is a stream of events and
    // should chatter; a held button is one sustained intent and should
    // not. Treating them identically was a too-literal reading of "make
    // it work as a gate in at Pulse In 2", and the result was a button
    // that stuttered while held.
    const bool chatter_gate = auto_chatter_ ? AutoChatterGate()
                                            : (pulse1 || PulseIn2());

    if (babble_ && chatter_gate) {
      const int32_t period =
          kBabbleMinPeriod +
          (int32_t)(((int64_t)(kBabbleMaxPeriod - kBabbleMinPeriod) *
                     babble_decay_) >> 15);
      if (--babble_count_ <= 0) babble_count_ = period;
      // Voiced for THREE QUARTERS of the period, not one third.
      //
      // A third was reported as staccato and separated - "that's not like
      // speech" - and it is not. Connected speech is mostly voiced:
      // vowels and sonorants run into one another, and the only silence
      // is the stop closures, which are 40-80 ms inside a syllable of
      // 200-400 ms. That is around 75-85% voiced.
      //
      // The gap must not vanish entirely, though. Above about 90% the
      // syllables stop being separable and the whole thing becomes a
      // drone, which loses the articulation the mode exists for.
      babble_open_ = babble_count_ > (period >> 2);
    } else {
      babble_count_ = 0;
      babble_open_ = false;
    }

    // Switch down: a GATE, not a key. It was a momentary plosive - one
    // click on the press, silence for the rest of the hold - which made
    // the panel's copy of Pulse In 1 rather than of Pulse In 2, and left
    // the two-second auto-chatter arming gesture completely silent while
    // you performed it. It now sounds for as long as it is held, through
    // the gate assembled above, and that includes the countdown.
    //
    // Consonants have not been lost: Pulse In 1 still fires clicks in
    // every mode, the 8mu's plosive note still does, and BABBLE draws its
    // own. The switch simply is not one of those sources any more.
    //
    // In BABBLE a two-second hold still toggles AUTO-CHATTER. Leaving is
    // a tap, so the gesture is asymmetric on purpose: engaging something
    // that then plays by itself should take deliberate effort, while
    // stopping it should not - the same reasoning as a panic button.
    if (down && SwitchChanged()) {
      // A tap while auto-chatter is running stops it. Handled on the
      // press rather than the release so it stops the instant it is
      // touched.
      if (auto_chatter_) {
        auto_chatter_ = false;
        auto_open_ = false;
        auto_count_ = 0;
        auto_left_ = 0;
      }
    }

    if (babble_ && down && !auto_chatter_) {
      if (++down_held_ >= kLongPressSamples) {
        auto_chatter_ = true;
        down_held_ = 0;
        // Start on a breath, so engaging it does not bark immediately.
        auto_open_ = false;
        auto_left_ = 0;
        auto_count_ = kBreathMinSamples;
      }
    } else {
      down_held_ = 0;
    }

    // A syllable that drew a consonant fires it here, through the same
    // counted path as every other trigger.
    if (auto_plosive_) {
      auto_plosive_ = false;
      voder_.TriggerPlosive(p.click_decay);
    }

    // --- audio ------------------------------------------------------------
    int32_t out = voder_.Process(p);

    // Volume, from the 8mu front/back tilt. This is the one control that
    // wants to be a gesture rather than a setting: pitch and vowel get set
    // and left, but swelling and ducking a phrase is what having the
    // controller in your hands is FOR. Unity until the accelerometer has
    // actually been used, so a card with no 8mu is never quiet.
    // Volume: the 8mu's fader and tilt, plus Audio In 2 as a CV.
    //
    // The CV is bipolar and ADDS, so it swells and ducks around whatever
    // the fader is set to - an envelope into this jack articulates a
    // phrase without needing the patch to control absolute level. With no
    // 8mu the base is full, so a negative CV ducks from full and a
    // positive one is simply already at the ceiling.
    // In BABBLE the tilt does NOT touch the volume - the fader alone
    // sets it.
    //
    // The assumption in the main mode is that you want sound: a card that
    // can be silenced by holding the controller at the wrong angle is
    // working against its player. The tilt still drives volume in the
    // deliberate mode, where the whole point is fine manual control and
    // the player has chosen that.
    int32_t vol = MidiOwns(g_state.volume_from_midi)
                      ? (babble_ ? g_state.volume_fader : g_state.volume)
                      : 32767;
    if (Connected(Input::Audio2)) {
      // SMOOTHED, because this multiplies the output every sample.
      //
      // Audio In 2 is an AUDIO-rate input: ComputerCard gives it a 12 kHz
      // notch for the mux interference and nothing else, because an audio
      // input must pass audio. Its residual broadband ADC noise is a
      // couple of LSB - inaudible as a signal, but this is not a signal
      // path. It is a MULTIPLIER on everything the card outputs, so <<4
      // turns those two LSB into full-depth amplitude modulation of the
      // whole voice: a broadband hiss that appears the moment anything is
      // patched here, rides on the sound so it is loudest when the card is
      // loud, and vanishes into a strong signal. Measured at -78.8 dB of
      // HF against the fundamental, where an unpatched card measures
      // nothing at all.
      //
      // Volume is a CONTROL, not audio, so it can be filtered. The state
      // is kept 4 bits above Q15 for the same reason the knob filter is:
      // (delta >> n) of a small delta is zero and the filter would stall
      // exactly where the noise lives. >>6 is 120 Hz, which takes the
      // modulation down by 38 dB and still settles a real CV move in
      // 1.3 ms - faster than any envelope you would patch into it.
      vol_cv_acc_ += (((int32_t)AudioIn2() << 8) - vol_cv_acc_) >> 6;
      vol += vol_cv_acc_ >> 4;
    } else {
      vol_cv_acc_ = 0;
    }
    vol = Clamp15(vol);
    if (vol != 32767) {
      out = (int32_t)(((int64_t)out * vol) >> 15);
    }

    // Upside down is a hard mute. Turning the controller over is an
    // unmistakable, deliberate gesture that nobody makes by accident,
    // which is exactly what a panic stop should be - no aim required.
    //
    // Applied after the volume scale and before the clamp, so it silences
    // the audio outputs without disturbing the CV outs: the energy and
    // DSP-load readings stay live while muted, which is what you want if
    // you are muting to look at something.
    // Upside down, or button 1 held.
    if (g_state.muted || g_state.midi_mute) out = 0;

    // Diagnostic state for the LED display. Kept because the first
    // hardware run was silent and there was no way to see WHY from the
    // panel - every candidate cause looked identical from outside.
    diag_use_ext_ = (p.ext_input != 0);
    diag_gated_ = (p.voiced_level == 0 && p.noise_level == 0);

    // Level, then limiting. See SoftClip above.
    //
    // x3.5, set from a SCOPE READING rather than from the model. The model
    // assumes a vowel peaks near 528 counts before this stage, which is AH
    // with the vowel table at its peak - optimistic by about a factor of
    // two. On the bench at x2.5 real playing measured +/-2 V typical with
    // momentary peaks near +/-3 V, so the true figures are about 273 and
    // 409 counts and the card was using a third of its range.
    //
    // The PEAK is what has to stay under the knee, not the average. At
    // x3.5 a momentary peak lands at 1432 against the 1500 knee - clean,
    // at about 4.2 V of a possible 6 V, with a typical vowel at 2.8 V.
    // x4 was tried first and puts that same peak at 1636, over the knee
    // and into the landing curve, which is exactly the mistake v1.20.0
    // made: sizing the stage for the average and letting the peaks bend.
    out = (out * 7) >> 1;
    out = SoftClip(out);

    AudioOut1((int16_t)out);
    AudioOut2((int16_t)out);

    // --- CV / pulse out ---------------------------------------------------

    // CV Out 1: formant energy, smoothed. One-pole with a ~5 ms time
    // constant so it reads as an envelope rather than as audio.
    const int32_t e = voder_.Energy();
    energy_smooth_ += (e - energy_smooth_) >> 6;
    int32_t cv1 = energy_smooth_ >> 3;
    if (cv1 > 2047) cv1 = 2047;
    CVOut1((int16_t)cv1);

    // CV Out 2: VOWEL POSITION - the openness axis of the cube, 0 to 5 V.
    //
    // This was the DSP load meter, which did its job: it read ~100% of
    // budget against a model predicting 44.2%, which is what found the
    // int64 filter bank (see CLAUDE.md, v1.23.0). After the rewrite it
    // read 60% typical and 70% peak, and a number that is not going to
    // change again is not worth a jack on a card this short of outputs.
    //
    // The vowel is the useful thing to put here. In BABBLE and
    // auto-chatter it wanders on its own, so this is a modulation source
    // that is musically RELATED to what is being heard rather than an
    // unrelated LFO - patch it at a filter and the patch moves with the
    // voice. In the deliberate mode it simply follows the hand.
    //
    // Smoothed at the same ~5 ms as the energy out, so it reads as a
    // control voltage rather than as stepped control-rate updates.
    vowel_smooth_ += (vowel_cv_ - vowel_smooth_) >> 6;
    int32_t cv2 = vowel_smooth_ >> 4;
    if (cv2 > 2047) cv2 = 2047;
    if (cv2 < 0) cv2 = 0;
    CVOut2((int16_t)cv2);

    PulseOut1(voder_.Voiced());
    PulseOut2(g_state.freeze != 0);

    UpdateLeds();

    // The DSP load meter that used to live here is gone with CV Out 2.
    // It did its job - it read ~100% of budget against a model predicting
    // 44.2%, which is what found the int64 filter bank - and once the
    // number stopped changing it was two time_us_32() reads and a divide
    // per sample to compute something nothing looked at. The measurement
    // is in CLAUDE.md; put it back if the ISR ever grows again.
  }

 private:
  // Scale a frequency by a signed number of octaves given in CV units,
  // where 2048 units is one octave (the Computer's roughly 1 V/oct scaling
  // on a +/-6 V input).
  //
  // A true 2^x needs exp(); this uses the standard piecewise-linear-in-the
  // -mantissa trick: integer octaves are a shift, and the fractional part
  // is approximated by a straight line between 1.0 and 2.0. That is within
  // about 6% at the worst point (a semitone and a half), which is audible
  // as pitch but perfectly usable for a speech formant source where F0 is
  // an expressive control rather than a tuned note. If this card ever
  // needs to track a keyboard properly, replace this with CVOutMIDINote's
  // calibrated path.
  static int32_t __not_in_flash_func(ApplyOctaves)(int32_t f, int32_t cv) {
    const int32_t oct = cv >> 11;          // whole octaves, floor
    const int32_t frac = cv - (oct << 11);  // 0..2047 within the octave

    int32_t r = f;
    if (oct > 0) {
      r <<= (oct > 4 ? 4 : oct);
    } else if (oct < 0) {
      r >>= (-oct > 4 ? 4 : -oct);
    }
    // r * (1 + frac/2048)
    r += (int32_t)(((int64_t)r * frac) >> 11);
    return r;
  }

  void __not_in_flash_func(ReadPanel)() {
    // One knob per sample, round-robin, FILTERED IN HIGHER PRECISION.
    //
    // A raw RP2040 ADC reading jitters by a few LSB continuously, and
    // KnobVal() << 3 turns every one of those into 8 counts of Q15. Fed
    // into the vowel blend that moves the band gains a few counts every
    // control block, forever: the gain smoother settles in about 9 ms and
    // a freshly jittered target arrives every 8 ms, so the gains never
    // settle at all. The result is a continuous modulation at the control
    // rate - a buzz that does not stop when the hand leaves the knob, and
    // whose loudness depends on where the knob sits, because ADC noise is
    // not uniform across the range.
    //
    // The filter state is kept at Q19 - the 12-bit reading shifted up by
    // 7 - and only shifted down to Q15 on the way out. That extra
    // precision is the point, and it took two attempts to see why:
    // filtering directly in Q15 does almost nothing, because (delta >> n)
    // of a small delta is zero and the filter stalls exactly where the
    // jitter lives. Same trap as the gain slew, one level up.
    //
    // >>5 leaves about 10 counts of residual against 24 unfiltered, and
    // settles a real knob move in 11 ms.
    // ALL THREE KNOBS, EVERY FOURTH SAMPLE - not one knob per sample.
    //
    // This looks like more work and is not. ComputerCard drives an
    // external 4-way mux, one state per interrupt, and updates
    // knobs[mux_state] from the single shared ADC channel. So each knob
    // only receives a genuinely NEW value every 4 samples, whatever we do
    // here.
    //
    // Reading them round-robin on a 3-phase counter against that 4-state
    // mux is the bug. The two cycles beat: the AGE of the value we happen
    // to read walks 0,1,2,0,1,2 with a period of lcm(3,4) = 12 samples.
    // 48000/12 is 4 kHz - a high-pitched whine, measured on the bench at
    // roughly 280 us per cycle, and INDEPENDENT of anything patched in
    // because it is generated by the sampling pattern itself.
    //
    // It is intermittent for the same reason: the knob's own ~60 Hz filter
    // still carries a little ripple, and the beat only turns that ripple
    // into a tone while it is non-zero. A dead-still knob on a quiet ADC
    // makes no whine; anything that keeps the value moving brings it back.
    //
    // Sampling on a multiple of 4 puts every read at the same point in the
    // mux cycle, so the age is constant and there is no beat left to hear.
    // Three one-poles every fourth sample is fewer operations than one
    // one-pole every sample, so this is also cheaper than what it replaces.
    //
    // The >>5 shift is unchanged from the version that fixed the ADC
    // jitter. At the slightly lower 12 kHz per-knob rate it still leaves
    // 10 counts of residual against 24 unfiltered and settles a real move
    // in 15 ms. Loosening it to >>3 to "compensate" for the rate change
    // was tried and puts the residual back up to 17, which is trading one
    // bug for the other.
    if (panel_phase_ == 0) {
      knob_main_acc_ +=
          (((int32_t)KnobVal(Knob::Main) << 7) - knob_main_acc_) >> 5;
      knob_main_ = knob_main_acc_ >> 4;
      knob_x_acc_ +=
          (((int32_t)KnobVal(Knob::X) << 7) - knob_x_acc_) >> 5;
      knob_x_ = knob_x_acc_ >> 4;
      knob_y_acc_ +=
          (((int32_t)KnobVal(Knob::Y) << 7) - knob_y_acc_) >> 5;
      knob_y_ = knob_y_acc_ >> 4;
    }

    // First time through, take the readings whole rather than filtering
    // up from zero.
    if (!knobs_seeded_) {
      knob_main_acc_ = (int32_t)KnobVal(Knob::Main) << 7;
      knob_x_acc_ = (int32_t)KnobVal(Knob::X) << 7;
      knob_y_acc_ = (int32_t)KnobVal(Knob::Y) << 7;
      knob_main_ = knob_main_acc_ >> 4;
      knob_x_ = knob_x_acc_ >> 4;
      knob_y_ = knob_y_acc_ >> 4;
      knobs_seeded_ = true;
    }

    if (++panel_phase_ >= kPanelDiv) panel_phase_ = 0;

    const Switch sw = SwitchVal();

    // Every knob does something on every page, and between the two pages
    // the panel reaches EVERY parameter the 8mu can:
    //
    //   Middle/Down   Knob1 OPENNESS   Knob2 FRONT      Knob3 BREATH
    //   Up            Knob1 PITCH      Knob2 BRIGHT     Knob3 ROUNDING
    //
    // Knob 3 used to be breath on both pages, which wasted a slot and left
    // ROUNDING - the third vowel axis - reachable only from an 8mu tilt.
    // Without a controller a whole dimension of the vowel cube was simply
    // missing, including the rounded vowels that dimension exists for.
    //
    // The card must be fully playable from its own front panel, both
    // because not everyone has an 8mu and because the controller can be
    // unplugged mid-session - see MidiOwns().
    if (babble_) {
      // BABBLE has two pages of macros, because three knobs is not
      // enough for a mode meant to be played with nothing else attached.
      //
      // Page 1 (switch middle) is WHAT IS SAID: which vowel, how fast, how
      // breathy. Page 2 (switch up) is WHO IS SAYING IT: the size and
      // register of the voice, and how animated it is. Splitting them that
      // way means each page is a coherent idea rather than an arbitrary
      // six controls, and you can set a voice on page 2 and then play it
      // on page 1.
      if (sw == Switch::Up) {
        // MAIN: VOICE SIZE. Sweeps from a small bright voice to a large
        // dark one, which is three things moving together - pitch down,
        // brightness down, and the vowel toward the back of the mouth.
        // That combination is what actually distinguishes a big voice
        // from a small one; pitch alone just sounds transposed.
        const int32_t size = knob_main_;
        panel_pitch_ = 24000 - (int32_t)(((int64_t)size * 20000) >> 15);
        panel_bright_ = 26000 - (int32_t)(((int64_t)size * 20000) >> 15);
        panel_front_ = 26000 - (int32_t)(((int64_t)size * 22000) >> 15);
        panel_openness_ = 6000 + (int32_t)(((int64_t)size * 20000) >> 15);

        // X: ANIMATION. How much the voice moves while it talks. At zero
        // it is a monotone; turned up, each phrase wanders through the
        // vowel cube and the pitch drifts with it. This is the control
        // that decides whether it sounds like reading aloud or like
        // muttering.
        babble_animate_ = knob_x_;

        // Y: CONSONANTS. The chance a syllable gets a plosive, and how
        // loud it is when it does. Both together, so one knob goes from a
        // smooth legato voice to a heavily articulated one.
        babble_consonant_ = knob_y_;
      } else {
        // MAIN sweeps the vowel diagonally AND opens the mouth. One knob
        // walks through recognisably different vowels rather than along
        // one edge of the cube - the same diagonal the formant CV uses,
        // and for the same reason: it crosses the middle where the
        // vowels live.
        panel_openness_ = knob_main_;
        panel_front_ = 32767 - knob_main_;

        // X is the CHATTER RATE. It drives the syllable length and the
        // brightness together, so turning it up gives faster, brighter
        // speech - the two things that make speech sound hurried.
        // Inverted: clockwise is shorter, which reads as faster.
        babble_decay_ = 32767 - knob_x_;
        panel_bright_ = 16384 + (knob_x_ >> 2);

        // Y is the VOICE CHARACTER: breath and rounding together. Turning
        // it up moves from a clear rounded vowel to a breathy spread one,
        // which is the axis between a hum and a whisper.
        panel_breath_ = knob_y_;
        panel_round_ = 32767 - knob_y_;

        // Pitch follows the vowel, so the diagonal sweep is also a melodic
        // one. Kept to the lower half of the range, where speech lives.
        panel_pitch_ = 4000 + (knob_main_ >> 2);
      }

      // ANIMATION is applied on both pages, since it is a property of the
      // voice rather than of a page. Each syllable draws an offset, held
      // for its duration, so the movement is per-syllable rather than a
      // continuous wobble - speech moves in steps between syllables, not
      // as a vibrato.
      if (babble_animate_ > 0) {
        panel_openness_ = Clamp15(panel_openness_ + anim_open_);
        panel_front_ = Clamp15(panel_front_ + anim_front_);
        panel_pitch_ = Clamp15(panel_pitch_ + anim_pitch_);
      }

    } else if (sw == Switch::Up) {
      panel_pitch_ = knob_main_;
      panel_bright_ = knob_x_;
      panel_round_ = knob_y_;
    } else {
      panel_openness_ = knob_main_;
      panel_front_ = knob_x_;
      panel_breath_ = knob_y_;
    }

    // Frozen formants ignore the vowel controls entirely.
    if (g_state.freeze) return;

    // Rebuilding the vowel is the second most expensive thing in the ISR
    // after the filter bank. Running it every sample would spend a
    // meaningful slice of the budget recomputing a value that only changes
    // when a hand moves. Once every kMorphDiv samples is 125 Hz, far above
    // the rate anything can be moved by hand; the band gains are held in
    // g_state between updates so the bank still sees a value every sample.
    if (++morph_phase_ < kMorphDiv) return;
    morph_phase_ = 0;

    int32_t openness =
        MidiOwns(g_state.openness_from_midi) ? g_state.openness : panel_openness_;
    int32_t front =
        MidiOwns(g_state.front_from_midi) ? g_state.front : panel_front_;


    // The formant CV moves openness up as front moves down. Sweeping both
    // together along one axis would mostly travel between two corners of
    // the cube; opposing them crosses the middle, which is where the
    // distinct vowels are.
    if (formant_cv_ != 0) {
      // WRAPPED AT THE ENDS, not clamped - or rather, centred so the clamp
      // stops being reachable by an ordinary bipolar LFO.
      //
      // Clamp15 floors at 0, so with fader 1 low the negative half of a
      // bipolar CV was swallowed whole: the vowel stopped moving for half
      // the LFO cycle and the output jumped between "vowel present" and
      // "vowel pinned". On a scope that reads as a half-wave rectified
      // waveform whose offset moves with fader position, which is exactly
      // what the bench showed.
      //
      // Folding the CV into the space the fader leaves means a full-swing
      // LFO always sweeps a full vowel range, wherever the faders sit -
      // the fader chooses the centre and the CV always gets its whole
      // travel around it.
      const int32_t head_o = (openness > 16384) ? (32767 - openness) : openness;
      const int32_t head_f = (front > 16384) ? (32767 - front) : front;
      const int32_t head = (head_o < head_f ? head_o : head_f) + 1;
      const int32_t cv = (int32_t)(((int64_t)formant_cv_ * head) >> 15);
      openness = Clamp15(openness + cv);
      front = Clamp15(front - cv);
    }

    // CV In 2 is a bipolar FORMANT CV. It sweeps the vowel cube along its
    // most useful diagonal - openness and front together, in opposite
    // senses - so one random voltage walks through recognisably different
    // vowels rather than along one axis of a cube. That is the input to
    // patch an S&H or a slow random into.
    //
    // Added to the existing position rather than replacing it, so the
    // faders and knobs still set where the sweep is centred.
    // Smoothed for the same reason as the volume CV above, though this one
    // is milder: ComputerCard already gives the CV inputs a ~247 Hz
    // one-pole. That still passes plenty of broadband ADC noise, and <<3
    // multiplies it before it moves all eight band gains at once. This
    // runs at the control rate (125 Hz), so >>2 here is gentle - enough to
    // take the edge off without making a formant sweep feel laggy.
    if (Connected(Input::CV2)) {
      formant_cv_acc_ += (((int32_t)CVIn2() << 7) - formant_cv_acc_) >> 2;
      formant_cv_ = formant_cv_acc_ >> 4;
    } else {
      formant_cv_ = 0;
      formant_cv_acc_ = 0;
    }

    // Rounding: the third vowel axis, from the 8mu left/right tilt. The
    // panel has no knob free for it, so with no controller attached the
    // card stays on the spread face of the cube - which is where the
    // ordinary unrounded vowels live, so nothing is lost.
    const int32_t round_amt =
        MidiOwns(g_state.round_from_midi) ? g_state.round : panel_round_;

    // Three axes, eight corner vowels, trilinear between them. vowels.h.
    int32_t gains[kNumBands];
    // Published for CV Out 2. This is the openness ACTUALLY being sounded -
    // after the 8mu takeover, the formant CV and the babble animation - so
    // the jack reports the vowel the card is making rather than where any
    // one control happens to sit.
    vowel_cv_ = openness;

    BlendVowel(openness, front, round_amt, gains);

    // Brightness tilts the result toward the high or low bands.
    // Multiplicative, so it cannot drive a band negative or pin one at
    // full scale - the additive version could do both, depending on the
    // vowel, and a band pinned at zero is a hole no control can reopen.
    const int32_t bright =
        MidiOwns(g_state.bright_from_midi) ? g_state.bright : panel_bright_;
    const int32_t tilt = bright - 16384;

    for (int i = 0; i < kNumBands; i++) {
      const int32_t pos = (i * 2) - 7;  // -7 .. +7
      // >>3, not >>4. At >>4 the brightness knob moved the spectrum only
      // 2.5 dB across its whole travel, which is not enough to feel like a
      // control; >>3 gives 5.2 dB with still no band pinned at either
      // extreme for any vowel. >>2 reaches 13.8 dB but starts overwhelming
      // the vowel itself, which is the opposite of a tone control.
      int32_t factor = 32768 + (int32_t)(((int64_t)tilt * pos) >> 3);
      if (factor < 0) factor = 0;
      int32_t g = (int32_t)(((int64_t)gains[i] * factor) >> 15);
      // Written straight through. The smoothing happens PER SAMPLE in
      // the engine, where it belongs - see the note in Voder::Process().
      //
      // Slewing here was tried and does not work: this runs at 125 Hz, so
      // a slew at this rate replaces one step with several smaller ones
      // at the same rate, and the buzz is unchanged. It also stalled,
      // because an arithmetic shift of a small positive delta is zero.
      g_state.band_gain[i] = Clamp15(g);
    }
  }

  void __not_in_flash_func(UpdateLeds)() {
    if (boot_splash_ > 0) {
      if (boot_mute_ > 0) {
        // Still inside the mute: the switch has not been read yet, so the
        // mode is not known. Chase rather than guess - showing the wrong
        // mode would be worse than showing none.
        const int32_t step = (kBootSplash - boot_splash_) >> 12;
        for (int i = 0; i < 6; i++) LedOn(i, (step % 6) == i);
      } else {
        // Mode is known. EVEN LEDs for the default mode, odd for the
        // alt-boot one - the NIBBLE-KO convention and its idiom. Since
        // BABBLE is now the default, even means BABBLE: the test is
        // against !babble_ so that a plain power-on lights the pattern
        // that reads as "normal", whichever mode that happens to be.
        // The splash runs on past the end of the mute so this is readable
        // before the card speaks.
        for (int i = 0; i < 6; i++) LedOn(i, ((i & 1) == 1) == !babble_);
      }
      return;
    }

    // Rate-limit: the LED matrix does not need refreshing at 48 kHz, and
    // doing so costs more than the display is worth.
    if (++led_phase_ < 240) return;
    led_phase_ = 0;

    // LEDs 0-3: band energy in four pairs, low to high.
    for (int i = 0; i < 4; i++) {
      const int32_t g = (g_state.band_gain[i * 2] +
                         g_state.band_gain[i * 2 + 1]) >> 1;
      LedBrightness(i, (uint16_t)(g >> 3));
    }

    if (SwitchVal() == Switch::Down) {
      // In BABBLE, holding Down engages auto-chatter after two seconds,
      // so the LEDs become a PROGRESS BAR for that hold. Without it the
      // player is counting two seconds blind and cannot tell whether the
      // gesture registered - and a two-second hold with no feedback feels
      // broken long before it completes.
      if (babble_ && !auto_chatter_) {
        const int32_t lit = (down_held_ * 7) / kLongPressSamples;
        for (int i = 0; i < 6; i++) LedOn(i, i < lit);
        return;
      }

      // Otherwise the diagnostic display: when this card is silent every
      // possible cause looks the same from the front panel, and working
      // that out once cost a whole hardware round trip.
      //
      //   LED 0  excitation is gated shut (no buzz, no noise)
      //   LED 1  external input has taken over from the internal sources
      //   LED 2  formant freeze is latched
      //   LED 3  all eight band gains are near zero
      int32_t gsum = 0;
      for (int i = 0; i < kBands; i++) gsum += g_state.band_gain[i];
      LedOn(0, diag_gated_);
      LedOn(1, diag_use_ext_);
      LedOn(2, g_state.freeze != 0);
      LedOn(3, gsum < 4096);
      LedOn(4, g_state.midi_connected != 0);
      LedBrightness(5, 4095);
      return;
    }

    // FROZEN: every LED at half brightness.
    //
    // Freeze blocks openness, front, rounding and the panel morph all at
    // once, so a frozen card looks exactly like a card that has stopped
    // working - and it was taken for one, because nothing on the panel
    // said otherwise. A state that makes several controls stop responding
    // has to announce itself.
    //
    // Half brightness across all six is deliberately unlike every other
    // display on this card: the band meters vary, the diagnostics are
    // on/off per LED, the boot splash chases. A flat even glow is the one
    // pattern that cannot be mistaken for any of them.
    if (g_state.freeze) {
      for (int i = 0; i < 6; i++) LedBrightness(i, 2048);
      return;
    }

    // MUTED by button 1: the same idea, dimmer still, since silence is
    // even easier to mistake for a fault than a stuck vowel.
    if (g_state.midi_mute) {
      for (int i = 0; i < 6; i++) LedBrightness(i, 400);
      return;
    }

    // AUTO-CHATTER running: LED 5 pulses with each syllable, so the panel
    // shows that the card is talking by itself rather than merely being
    // left switched on. Cheap, and it is the only outward sign that the
    // mode is engaged once the switch is released.
    if (auto_chatter_) {
      LedBrightness(5, auto_open_ ? 4095 : 300);
      for (int i = 0; i < 4; i++) {
        const int32_t g = (g_state.band_gain[i * 2] +
                           g_state.band_gain[i * 2 + 1]) >> 1;
        LedBrightness(i, (uint16_t)(g >> 3));
      }
      LedOn(4, g_state.midi_connected != 0);
      return;
    }

    // LED 4: 8mu connected.
    LedOn(4, g_state.midi_connected != 0);

    // LED 5: voiced/unvoiced, or full brightness while frozen.
    if (g_state.muted) {
      // Dark while muted, so the panel says why it is silent. Silence with
      // no explanation is the one failure mode this card keeps finding.
      LedBrightness(5, 0);
    } else if (g_state.freeze) {
      LedBrightness(5, 4095);
    } else {
      LedBrightness(5, voder_.Voiced() ? 3000 : 400);
    }
  }

  Voder voder_;

  int32_t boot_mute_, boot_splash_;
  int32_t panel_phase_, morph_phase_;
  int32_t formant_cv_;
  int32_t babble_decay_, babble_count_;
  bool babble_open_;
  int32_t auto_count_, auto_left_, down_held_;
  int32_t babble_animate_, babble_consonant_;
  int32_t anim_open_, anim_front_, anim_pitch_;
  uint32_t auto_rng_;
  bool auto_chatter_, auto_open_, auto_plosive_;
  bool gate_seen_;
  bool babble_;
  bool diag_use_ext_, diag_gated_;
  int32_t knob_main_, knob_x_, knob_y_;
  int32_t knob_main_acc_, knob_x_acc_, knob_y_acc_;
  int32_t vol_cv_acc_, formant_cv_acc_;
  bool knobs_seeded_;
  int32_t panel_openness_, panel_front_, panel_breath_;
  int32_t panel_pitch_, panel_bright_, panel_round_;
  uint32_t last_plosive_;
  bool last_pulse1_;
  int32_t vowel_cv_, vowel_smooth_;
  int32_t led_phase_;
  int32_t energy_smooth_;
};

// Global, not on the stack: the card holds the filter bank and its state,
// and the stack is only 4 KB.
//
// Being a global means this constructor runs BEFORE main(). That is what
// made v1.0.1 silent: the constructor called Voder::Init(), which copied
// coefficients that VoderInit() had not computed yet. Voder::Init() now
// builds them itself, so the ordering cannot bite again - but do not add
// anything here that depends on work done in main().
VoderCard card;

int main() {
  // 192 MHz. A proven clock on this hardware - grains/51, glitter/53 and
  // WorkshopSpectral all run here. Eight biquads at 48 kHz is not cheap on
  // a core with no FPU and no 64-bit multiply, and the headroom matters.
  set_sys_clock_khz(192000, true);

  // Recompute the coefficients at the real system clock. Voder::Init()
  // already built them from the card's constructor (see the note there),
  // and VoderInit() is idempotent - this is not what makes the card work,
  // it just keeps main() honest about what has been initialised.
  VoderInit(0);

  // Core 1 owns the USB host stack and nothing else. See usb_core1.cpp for
  // why the audio ISR stays on Core 0.
  multicore_launch_core1(core1_entry);

  card.EnableNormalisationProbe();
  card.Run();
}
