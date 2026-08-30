// BARBER'S POLE - a Shepard-Risset infinite-glissando card for the Music
// Thing Modular Workshop System Computer.
//
// The card is BARBER'S POLE; the alt-boot mode is HIDDEN BARBER. The code
// keeps the names `shepard` and `ping` throughout, deliberately: "Shepard
// tone" is the technical name of the illusion the DSP implements, and a
// "ping" is what a struck voice actually sounds like. Renaming either would
// make the code describe the product instead of the mechanism.
//
// A Shepard tone is octave-spaced components sliding together through a fixed
// spectral envelope. Each fades in at the bottom, rises, and fades out at the
// top; because the stack is octave-spaced, one full slide returns it to
// exactly where it started, so the glide can loop forever without a seam.
//
// Structure:
//   shepard.cpp   tables, window, scale quantisation  (tools/shepard_check.py,
//                                                      tools/quant_check.py)
//   comb.h        rising comb filter for live audio    (tools/comb_check.py)
//   ping.h        alt-boot: strike the invisible pole  (tools/ping_check.py)
//   main.cpp      panel, CV, LEDs, and the audio ISR
//
// CORE SPLIT - deliberately NOT WorkshopSpectral's arrangement.
//
// That card puts its DSP on core 1 because an FFT frame costs ~0.5 ms, 24x the
// per-sample budget: it CANNOT run inline, and the ring buffer is the price of
// admission. An additive oscillator bank has no frame structure at all - every
// sample costs the same, there is nothing to batch and no deadline to miss.
//
// So everything runs in the audio ISR, and the expensive control-rate work
// (one pow2 lookup, the window weights, quantisation) runs inline every 32nd
// sample. That removes the cross-core protocol entirely, along with the ring
// races and stale-slot bugs that dominate the sibling card's debugging notes.
// It also degrades honestly: if this were ever too slow the ISR would overrun
// visibly, where a block renderer turns the same fault into an intermittent
// underrun that is miserable to reproduce.
//
// Core 1 stays free. CV Out 2 reports the real measured load - if that ever
// reads high, the control block is what moves across.

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "fixed.h"
#include "shepard.h"
#include "octave.h"
#include "ping.h"

using namespace shepard;

// --- timing constants, all at 48 kHz ---------------------------------------

// Boot window. Two things must settle before the card can start: the ADC
// (~150 ms) and the SWITCH, which ComputerCard derives through a ~60 Hz filter
// starting at zero - and zero decodes as Down, so every boot reports Down for
// the first few milliseconds regardless of the real position.
//
// The filter maths says ~14 ms. NIBBLE-KO uses half a second and that is
// PROVEN on this hardware, which is worth more than a model that ignores
// supply ramp and ADC reference settling. Same figure here.
static constexpr int32_t kBootMute = 24000;     // 0.5 s

// How long the boot mode pattern stays up after the switch has been read.
// Runs past the end of the mute so it is readable rather than a flicker.
static constexpr int32_t kBootSplash = 36000;   // 0.75 s

// Switch debounce. ReadPanel runs at 48 kHz, so contact bounce on the
// momentary Down position would toggle freeze several times per press - and an
// EVEN number of toggles leaves it off, which reads as "freeze works only
// sometimes". 20 ms outrides bounce while staying far below a deliberate tap.
static constexpr int32_t kSwitchSettle = 960;

// How long the switch must sit still before a page change commits. Up and
// Middle are NOT adjacent - reaching Down from Up passes through Middle - so
// without this a slow flick registers the transit as a real page change and
// hands page 1 the knob positions meant for page 2. Audible as a lurch.
static constexpr int32_t kPageSettle = 576;     // 12 ms

// Width of the octave-wrap trigger on Pulse Out 1, in samples. 5 ms is well
// above any gate-input threshold and far below the gap between wraps even at
// the top glide speed (125 ms).
static constexpr int32_t kWrapPulseLen = 240;

// Control-rate divider. The pow2 lookup, window weights and quantisation run
// once per 32 samples (1.5 kHz). Worst-case pitch drift within a block is
// 1.6 cents at the fastest glide - inaudible, and it is a smooth staleness
// rather than a discontinuity. Power of two so the counter is a mask.
static constexpr uint32_t kControlMask = 31;

// Main knob deadzone, in ADC counts either side of centre. Gives a definite
// "stopped" position that can be found by feel.
static constexpr int32_t kDeadzone = 50;

// How far a knob must move before it takes control after a page change.
// 1200 of 32767 is ~3.7% of travel, about 4 degrees - past ADC jitter, well
// inside a deliberate nudge.
static constexpr int32_t kPickupBand = 1200;

// HOLD thresholds for Audio In 2, as a Schmitt trigger.
//
// Audio In 2 is a bipolar AUDIO input reading +-2048 over roughly +-6 V, with
// no comparator in front of it. A single threshold would chatter on any slow
// edge, and chatter on a hold reads as the pole stuttering rather than
// stopping - so two thresholds, with a gap far wider than any plausible noise.
//
//     engage   400 of 2048  ~1.2 V
//     release  200 of 2048  ~0.6 V
//
// The hysteresis is half the engage level, not a handful of LSB. Any real gate
// (5 V, or a 0/8 V envelope) crosses both cleanly; audio-level noise crosses
// neither. An unpatched jack reads exactly 0, because ComputerCard zeroes
// disconnected inputs - so "not held" is the default, which is the safe way
// round.
static constexpr int32_t kHoldEngage = 400;
static constexpr int32_t kHoldRelease = 200;

// Glide rate curve coefficients - see the derivation in UpdateControl().
//
//     rate_q32 = |defl| * kRateLinear + defl^7 * kRateHigh
//
// A SEVENTH power, not a cube, and the reason is perceptual rather than
// numerical.
//
// The Shepard illusion only works while the listener cannot track the octave
// cycle. Above about 1 octave/second the cycle repeats once a second or
// faster and the ear stops hearing "endlessly rising" and starts hearing "a
// climbing line, repeated" - because that is what it is. Every Shepard
// implementation has this limit; Risset's originals run at roughly 0.1-0.3
// oct/s.
//
// An earlier cubic curve put 1 oct/s at 70% of travel, so HALF the knob was
// past the point where the effect survives. Heard on hardware as "at faster
// than a period of about 1 second I hear repeated climbing lines" - which was
// the illusion breaking down exactly as it must, not a fault.
//
// The 7th power keeps the knee much later: 85% of travel now stays inside the
// illusion, with the top 15% running out to a deliberate siren.
//
//     52%   0.02 oct/s    46 s per octave
//     70%   0.22           4.6 s
//     85%   0.97           1.0 s     <- the perceptual edge
//     92%   2.6            0.38 s    siren
//    100%   7.9            0.13 s    siren
static constexpr int32_t kRateLinear = 3;
static constexpr int32_t kRateHigh = 2600;

// Base frequency of layer 0 when the master phase is at zero: A-1, 13.75 Hz.
// At 12 layers the top sits near 28 kHz, but its window gain is ~0 there so
// the alias is ~90 dB down. The increment is clamped anyway.
//
// As a Q32 phase increment at 48 kHz: 13.75 / 48000 * 2^32.
static constexpr uint32_t kBaseInc = 1229782u;

// The octave stack's delay buffer, 128 KB - the card's one large allocation,
// and 93% of its RAM. A plain global, deliberately: see the table note in
// shepard.h about statics in headers landing in flash.
int16_t g_delay_buf[kOctLen];

class ShepardCard : public ComputerCard {
 public:
  ShepardCard() {
    octave_.Init(g_delay_buf);
    ping_.Init();

    boot_mute_ = kBootMute;
    page_ = 0;
    last_switch_ = Switch::Middle;

    for (int i = 0; i < kMaxLayers; ++i) {
      osc_q32_[i] = 0;
      win_l_[i] = 0;
      win_r_[i] = 0;
    }

    for (int p = 0; p < 2; ++p) {
      for (int k = 0; k < 3; ++k) {
        knob_arrival_[p][k] = 0;
        knob_live_[p][k] = true;   // page 1 is live from boot - nothing to pick up
      }
    }

    // Page 1 defaults: stopped, mid density, fully internal voice.
    stored_[0][0] = 16384;    // Main: speed, centred = stationary
    stored_[0][1] = 16384;    // X: density
    stored_[0][2] = 0;        // Y: source - internal synth
    // Page 2 defaults: smooth glissando, mono, full level.
    stored_[1][0] = 0;        // Main: quantise = smooth
    stored_[1][1] = 0;        // X: stereo width = mono
    stored_[1][2] = 32767;    // Y: output level
  }

  virtual void __not_in_flash_func(ProcessSample)() override {

    // --- boot mute --------------------------------------------------------
    if (boot_mute_ > 0) {
      --boot_mute_;

      // Alt-boot: hold the switch DOWN through power-on for SPIRAL mode.
      //
      // ONE reading, taken once the window has FULLY elapsed - never "Down
      // seen at any point during the window". The switch reads Down until it
      // settles, so a card that latches on any sighting latches on every boot:
      // WorkshopZX and WorkshopBio both shipped exactly that. Down is safe
      // here, despite being the unsettled value, precisely because the reading
      // happens once, after the filter has resolved from any start position.
      if (boot_mute_ == 0) {
        ping_mode_ = (SwitchVal() == Switch::Down);
        boot_splash_ = kBootSplash;
        // Start on whichever page the switch is already showing, so the card
        // does not immediately page on the first move. A Down alt-boot springs
        // back to Middle, so page 1 is the right landing place for it.
        last_switch_ = SwitchVal();
        page_ = (last_switch_ == Switch::Up) ? 1 : 0;
        pending_page_ = page_;
      }

      // Boot splash. Half a second of silence with dark LEDs reads as a dead
      // card, so show which mode latched - NIBBLE-KO's convention: EVEN LEDs
      // (0/2/4) normal boot, ODD (1/3/5) alt-boot. Nothing is shown before the
      // switch is read; lighting a guess would be worse than lighting nothing.
      if (boot_splash_ > 0) {
        --boot_splash_;
        for (uint32_t i = 0; i < 6; ++i) {
          LedOn(i, ((i & 1u) == 1u) == ping_mode_);
        }
      }

      AudioOut1(0);
      AudioOut2(0);
      CVOut1(0);
      CVOut2(0);
      return;
    }

    // --- boot splash, continuing past the mute ----------------------------
    if (boot_splash_ > 0) {
      --boot_splash_;
      for (uint32_t i = 0; i < 6; ++i) {
        LedOn(i, ((i & 1u) == 1u) == ping_mode_);
      }
      if (boot_splash_ == 0) {
        // Hand the LEDs back cleanly - LedOn and LedBrightness drive the same
        // hardware, so leave nothing latched before the normal path takes over.
        for (uint32_t i = 0; i < 6; ++i) LedOff(i);
        led_phase_ = 240;
      }
    }

    ReadPanel();

    // --- control rate -----------------------------------------------------
    // Everything expensive lives here: one pow2 lookup, the window weights,
    // the scale search. 1.5 kHz rather than 48 kHz.
    // The control block is SPLIT across two samples of each period, and the
    // reason is peak cost rather than average cost.
    //
    // Everything used to land on sample 0: the rate curve, the scale search,
    // the pow2 lookup, three divides AND a twelve-iteration per-layer loop.
    // With four PING voices and the live path that one sample went over
    // budget while the other 31 sat at 84% - which is exactly the reported
    // fault, distortion only during fast triggering.
    //
    // Splitting the per-layer loop onto its own sample halves the peak. The
    // control RATE is unchanged at 48 kHz / 32, so nothing about the sound
    // changes; only the work is spread.
    const uint32_t phase = sample_count_ & kControlMask;
    if (phase == 0) {
      UpdateControl();
    } else if (phase == (kControlMask + 1u) / 2u) {
      UpdateLayers();
      // Arm the deferred strike only now, so it copies the layer data this
      // block just wrote rather than the previous period's. It then fires on
      // the NEXT sample, which is neither of the two busy ones.
      if (strike_armed_) { strike_armed_ = false; strike_pending_ = true; }
    }
    ++sample_count_;

    int32_t l = 0;
    int32_t r = 0;

    if (ping_mode_) {
      RenderPing(&l, &r);
    } else {
      RenderShepard(&l, &r);
    }

    // Output level, slewed. The knob is live the instant the page changes, so
    // the target can step by the full range in one sample; a one-pole turns
    // that into a fade rather than a click.
    level_smooth_ += (param_level_ - level_smooth_) >> 8;
    l = MulQ15(l, level_smooth_);
    r = MulQ15(r, level_smooth_);

    AudioOut1((int16_t)SoftClipOut(l));
    AudioOut2((int16_t)SoftClipOut(r));

    // CV 1: the master phase as a 0-1 V ramp, ONE OCTAVE on a V/oct input.
    //
    // Calibrated per card via CVOutMillivolts rather than scaled by hand, so
    // an oscillator patched to it tracks the pole's climb in tune.
    //
    // CV 2: the WINDOW level at that same phase - which is what layer 0 is
    // doing. Patch both to a VCA and its pitch input and you get one Shepard
    // layer made external: it rises in pitch while swelling and fading, then
    // the next cycle begins.
    {
      const uint32_t ph = master_out_q32_;
      CVOut1Millivolts((int32_t)(ph >> 22) * 1000 / 1024);
      // HannQ15 is Q15; scale to 0-5 V so it drives a VCA over its full range.
      CVOut2Millivolts(MulQ15(HannQ15(ph), 5000));
    }

    UpdateLeds();
  }

 private:
  // --- the oscillator bank ------------------------------------------------
  void __not_in_flash_func(RenderShepard)(int32_t* out_l, int32_t* out_r) {
    // The two sources accumulate SEPARATELY because they need DIFFERENT
    // normalisers: the oscillator bank is incoherent (1/sqrt(N)), the octave
    // stack is correlated copies of one input (1/N^0.70). Mixing first and
    // normalising once - which is what this did originally - applies the
    // wrong law to whichever source dominates, and left the live path quiet
    // and layer-count dependent. See kInvPowN in shepard.cpp.
    int32_t syn_l = 0;
    int32_t syn_r = 0;
    int32_t liv_l = 0;
    int32_t liv_r = 0;

    // The live-audio path OCTAVE-STACKS the input: N read heads on one delay
    // line at 2^i the write rate, so every partial of the source is transposed
    // into N octave-spaced copies and glides through the same window as the
    // oscillators. See octave.h.
    if (source_mix_ > 0) {
      // << 5 measured: rms 308-358 across noise, drone and chord against the
      // synth voice's 443, with ~4 dB of headroom and no clipping on any of
      // them. Unlike the comb this path's level barely depends on source type,
      // because it transposes rather than resonates.
      octave_.Write((int32_t)AudioIn1() << 5);
    }

    for (int i = 0; i < active_layers_; ++i) {
      // --- internal voice ---
      int32_t synth = 0;
      if (source_mix_ < 32767) {
        osc_q32_[i] += inc_[i];
        synth = SinQ15(osc_q32_[i]);
      }

      // --- live audio, octave-shifted ---
      // This head's shift ratio matches this layer's oscillator interval, so
      // the transposed copy of the input lands exactly where the sine does.
      int32_t filtered = 0;
      if (source_mix_ > 0) {
        filtered = octave_.Read(i, oct_rate_[i]);
      }

      // Window each source into its own accumulator. The crossfade happens
      // after normalisation, below, because the two need different laws.
      syn_l += MulQ15(synth, win_l_[i]);
      syn_r += MulQ15(synth, win_r_[i]);
      liv_l += MulQ15(filtered, win_l_[i]);
      liv_r += MulQ15(filtered, win_r_[i]);
    }

    // Normalise each source by ITS OWN law, then crossfade at equal power.
    //
    // 1/sqrt(N) for the bank, because those layers are incoherent so their
    // powers add. 1/N^0.70 for the stack, because those layers are correlated
    // copies of one input - measured, see kInvPowN.
    //
    // The crossfade is cos/sin rather than linear. Two unrelated signals
    // faded linearly lose 3 dB at the midpoint, which is heard as the synth
    // dominating everywhere but the extremes of the knob. XfadeQ15 reads both
    // legs out of sin_lut, so this costs no new table.
    const int32_t syn = MulQ15(syn_l, inv_sqrt_n_);
    const int32_t liv = MulQ15(liv_l, inv_pow_n_);
    const int32_t syn2 = MulQ15(syn_r, inv_sqrt_n_);
    const int32_t liv2 = MulQ15(liv_r, inv_pow_n_);

    int32_t gs, gl;
    XfadeQ15(source_mix_, &gs, &gl);

    // The final >> 5 is the output scaling, and it is MEASURED, not assumed -
    // tools/passthru_check.py runs the whole integer chain and reports the
    // peak. At >> 4 the card peaks at 1754 against a 2047 rail and sits in
    // soft clip 2-10% of the time; at >> 6 it is needlessly quiet. >> 5 gives
    // about 7 dB of headroom with rms ~443.
    //
    // If any gain in this path changes, re-run that test. The sibling card
    // shipped a 512x scaling error to hardware that every MODELLED test
    // passed - only running the real arithmetic catches it.
    *out_l = (MulQ15(syn, gs) + MulQ15(liv, gl)) >> 5;
    *out_r = (MulQ15(syn2, gs) + MulQ15(liv2, gl)) >> 5;
  }

  // --- the alt-boot delay --------------------------------------------------
  // --- PING: strike the invisible pole ---------------------------------
  //
  // The pole itself is silent here. UpdateControl still advances it and still
  // computes inc_[] and the window, so the structure is running the whole
  // time - a strike simply takes a copy of it.
  void __not_in_flash_func(RenderPing)(int32_t* out_l, int32_t* out_r) {
    // The deferred strike, on the sample after the control block that armed
    // it - see the note in UpdateControl.
    if (strike_pending_) {
      strike_pending_ = false;
      ping_.Strike(layers_, inc_, win_l_, win_r_, oct_rate_);
    }

    ping_.Tick(ping_decay_);

    // The live input is written to the octave stack continuously, so a strike
    // has recent material to transpose rather than starting from silence.
    octave_.Write((int32_t)AudioIn1() << 5);

    int32_t acc_l = 0;
    int32_t acc_r = 0;

    // Read the octave stack ONCE PER LAYER, not once per voice per layer.
    //
    // This was both a performance bug and a correctness one. Each call to
    // Read(i, ...) advances drift_q16_[i], and all four voices share that
    // array - so with four voices sounding the read heads advanced FOUR TIMES
    // per sample and the transposition was garbage. It also cost 48 reads per
    // sample instead of 12, which on its own pushed the ISR over budget at
    // 4080 cycles raw against 4000.
    //
    // The heads track the CURRENT pole rather than each voice's frozen rates.
    // That is a deliberate simplification: what makes a ping a ping is its
    // ENVELOPE and its window gains, both of which stay per-voice. The wet
    // signal is then shaped by each voice's envelope as it is summed, so a
    // strike still gates and still decays - the transposition simply follows
    // the pole rather than being frozen with it.
    int32_t wet[kMaxLayers];
    if (source_mix_ > 0) {
      for (int i = 0; i < active_layers_; ++i) {
        wet[i] = octave_.Read(i, oct_rate_[i]);
      }
    }

    // Equal-power crossfade legs, as in RenderShepard - a linear fade between
    // two unrelated sources dips 3 dB at the midpoint. Hoisted out of the
    // voice loop because the mix does not change within a sample.
    //
    // Unlike normal boot this path does NOT normalise the two sources
    // separately: each voice's envelope shapes both, so they share an
    // accumulator. The layer counts here are per-voice and often differ, so
    // there is no single N to normalise the live side by - and PING is struck
    // rather than held, so a standing level difference matters far less.
    int32_t gs, gl;
    XfadeQ15(source_mix_, &gs, &gl);

    for (int v = 0; v < kPingVoices; ++v) {
      PingVoice& voice = ping_.Voice(v);
      if (!voice.Active()) continue;

      const int n = voice.Layers();
      const int32_t env = voice.Env();
      for (int i = 0; i < n; ++i) {
        int32_t sig = 0;
        if (source_mix_ < 32767) sig = voice.Osc(i);

        if (source_mix_ > 0) {
          const int32_t w = MulQ15(wet[i], env);
          sig = MulQ15(sig, gs) + MulQ15(w, gl);
        }

        acc_l += MulQ15(sig, voice.WinL(i));
        acc_r += MulQ15(sig, voice.WinR(i));
      }
    }

    // 1/sqrt(N) for the layers, and NO fixed halving for polyphony.
    //
    // An earlier version halved the output to guarantee four simultaneous
    // voices could never clip. But four voices at full envelope is rare - a
    // player strikes one at a time and mostly hears ONE decaying voice - so
    // that halving made the card 6 dB quiet essentially all of the time in
    // order to protect a case that the soft clipper already handles
    // gracefully.
    //
    // Measured: a single voice peaks around 1453 at N=3 and 1746 at N=12,
    // which is at or just past the 1450 knee - effectively transparent. All
    // four at once reaches 3493 raw, which SoftClipOut turns into 1911
    // asymptotically rather than pinning at the rail.
    const int32_t g = inv_sqrt_n_;
    *out_l = MulQ15(acc_l, g) >> 5;
    *out_r = MulQ15(acc_r, g) >> 5;
  }

  // --- control rate --------------------------------------------------------
  void __not_in_flash_func(UpdateControl)() {
    // Layer count from X, 3..12. DISCRETE, with hysteresis.
    //
    // Two attempts were made to fade a new layer in, and both were chasing
    // something that does not exist:
    //
    //   1. Crossfading the N-layer and (N+1)-layer LAYOUTS. Keeps the window
    //      sum flat at every fractional position (0.0000% ripple, verified),
    //      but the two layouts ROTATE DIFFERENTLY at an octave wrap, so
    //      mid-blend the stack cannot rotate cleanly. Measured 123 sample step
    //      mid-fade against 5 at either end.
    //
    //   2. Appending a slot and slewing its gain. The appended layer sits at
    //      u = (master + n)/n, which wraps to master/n - the SAME position as
    //      layer 0. So it was not a quiet new layer at the edge of the window;
    //      it was a DUPLICATE of layer 0 at up to 0.74 gain, faded in on top.
    //      Heard as "very audible when it introduces a new layer, especially
    //      descending - it's not fading in at all".
    //
    // The reason both fail is structural: N appears in the window DIVISOR, so
    // changing it RESPACES every layer at once. Going 3 -> 4 moves all three
    // existing layers as well as adding one. There is no "entering layer" to
    // fade, and a worst-case per-layer gain change of 0.63 is unavoidable.
    //
    // So the step is accepted and made RARE and PREDICTABLE instead. Hysteresis
    // means ADC jitter at a boundary cannot retrigger it - without that the
    // count flickers between N and N+1 every control block, which is far worse
    // than one clean step when the knob is actually moved.
    int32_t density = stored_[0][1];
    if (Connected(Input::CV2)) density += (int32_t)CVIn2() << 4;
    density = ClampQ15(density);

    {
      const int32_t kStep = 32768 / (kMaxLayers - kMinLayers + 1);  // ~3277
      const int32_t kHyst = kStep / 6;      // ~16% of a step, well past jitter
      int32_t want = kMinLayers + (density / kStep);
      if (want < kMinLayers) want = kMinLayers;
      if (want > kMaxLayers) want = kMaxLayers;

      // Only move when the knob is clear of the boundary it last crossed.
      if (want != layers_) {
        const int32_t boundary = (want > layers_ ? want : layers_ + 1)
                                 - kMinLayers;
        const int32_t edge = boundary * kStep;
        if ((want > layers_ && density > edge + kHyst) ||
            (want < layers_ && density < edge - kHyst)) {
          layers_ = want;
        }
      }
    }

    source_mix_ = ClampQ15(stored_[0][2]);

    // Page 2 X is read ONCE, here, and then used by exactly one parameter
    // depending on the mode - decay in PING, stereo width otherwise.
    // tools/ping_check.py::check_no_knob_collision counts readers of each
    // stored_[page][knob] and fails if any knob drives two things, which is
    // how the decay/quantise collision was found.
    page2_x_ = stored_[1][1];

    // Quantisation mode from page-2 Main, 0..3. Same reasoning.
    scale_ = (stored_[1][0] * kScaleCount) >> 15;
    if (scale_ >= kScaleCount) scale_ = kScaleCount - 1;
    if (scale_ < 0) scale_ = 0;

    // --- glide rate, from Main + CV 1 ---
    // The deadzone applies to the KNOB only; CV is summed after it, so CV can
    // drive the glide with the knob parked at centre. Without that a centred
    // knob would swallow small CV entirely.
    int32_t defl = stored_[0][0] - 16384;
    if (defl > -kDeadzone * 8 && defl < kDeadzone * 8) defl = 0;
    const int32_t cv = Connected(Input::CV1) ? (int32_t)CVIn1() : 0;
    defl += cv << 3;

    // Rate curve: a LINEAR term plus a CUBIC one.
    //
    // A pure cube was the first attempt and it was wrong twice over. It was far
    // too slow - two MulQ15s leave a full-scale cube at 4096 rather than 2^31,
    // so the `* 12` multiplier delivered 0.55 oct/s at full deflection against
    // a comment claiming 2. And the shape wasted the knob: at 70% travel it
    // gave 29 SECONDS per octave, which is not distinguishable from stopped.
    //
    // The cubic term alone is what makes the extremes reachable; the linear
    // term is what stops the first two thirds of the travel being dead. The
    // measured spread:
    //
    //     55% travel   0.12 oct/s    8.6 s per octave   a slow drift
    //     70%          0.88          1.1 s              clearly moving
    //     85%          3.1           0.32 s             a fast sweep
    //    100%          8.0           0.13 s             a siren
    //
    // Note the top speed costs precision: pitch drifts 6.4 cents within a
    // 32-sample control block at 8 oct/s, against 0.4 at the old top speed.
    // That is smooth staleness rather than a discontinuity - the pitch is
    // momentarily behind, then catches up - and at a rate this fast the ear
    // is tracking the sweep, not the tuning. If it ever reads as gritty,
    // lower kControlMask to 15 rather than capping the speed.
    const int32_t mag = defl >= 0 ? defl : -defl;
    const int32_t sq = MulQ15(defl, defl);
    const int32_t q4 = MulQ15(sq, sq);
    const int32_t q6 = MulQ15(q4, sq);
    const int32_t q7 = MulQ15(q6, mag);
    int32_t rate = mag * kRateLinear + q7 * kRateHigh;
    if (defl < 0) rate = -rate;
    if (reverse_) rate = -rate;          // Pulse In 2, momentary
    rate_q32_ = rate;

    shift_up_ = (defl >= 0);

    if (ping_mode_) {
      // Page 2 X sets the decay, from a short click to a long ring.
      //
      // NOT page 2 Main, which is QUANTISE and is shared with normal boot.
      // Putting decay there too made one knob drive both: turning it up for a
      // longer ring also walked the pole from smooth into chromatic, major,
      // minor and finally pentatonic - so with a slow pole, consecutive
      // triggers landed on the SAME scale degree and the pitch stopped
      // changing. Reported as "it stops changing note when I change the
      // length from a click", which is exactly that collision.
      //
      // Quantise is worth keeping on Main here: landing the strikes on a
      // scale is one of the most playable things about this mode. Stereo
      // width loses its knob in PING instead, which matters far less when
      // striking discrete notes.
      int32_t idx = (page2_x_ * kPingDecaySteps) >> 15;
      if (idx >= kPingDecaySteps) idx = kPingDecaySteps - 1;
      if (idx < 0) idx = 0;
      ping_decay_ = kPingDecay[idx];
    }

    // --- master phase ---
    // A free-running continuous phase always advances smoothly; the quantised
    // target is derived from it, and the phase actually USED slews toward that
    // target. Snapping directly would make all N oscillators jump together,
    // which is audible as a thump even though a pitch step is not itself a
    // click.
    //
    // MULTIPLIED BY THE CONTROL DIVIDER. rate_q32_ is a PER-SAMPLE increment,
    // but this runs once per control block, so advancing by it directly made
    // the glide 32x too slow - and worse, it made the pitch move in 32-sample
    // STEPS large enough to hear. On hardware that was "the pitch climbs, then
    // plateaus, then clicks and resets": the plateau is the master barely
    // moving, and the click is the accumulated error arriving all at once.
    //
    // The window and the layer frequencies both derive from master, so they
    // stayed consistent with each other - which is why the illusion still
    // half-worked and the fault read as a wrap artefact rather than as a
    // broken rate.
    master_free_q32_ += (uint32_t)rate_q32_ * (kControlMask + 1u);

    // Pulse In 1 advances one degree in stepped modes. Counted, not flagged -
    // a bool set by the ISR and cleared elsewhere loses any trigger arriving
    // between the read and the clear.
    //
    // NOT in PING, where the same jack is the strike trigger and the pole must
    // be left alone. Stepping there would move the pitch on every strike, so
    // a repeated trigger would walk up the scale instead of sampling wherever
    // the pole has drifted to - which is the whole idea of the mode.
    //
    // This was latent rather than audible until HOLD moved to Audio In 2: the
    // gate used to freeze the pole for its own duration, which hid the jump.
    const uint32_t tc = trigger_count_;
    const bool triggered = (tc != trigger_seen_);
    if (triggered) {
      trigger_seen_ = tc;
      if (!ping_mode_ && scale_ != kScaleSmooth) {
        master_free_q32_ = StepScale(master_free_q32_, scale_,
                                     shift_up_ ? 1 : -1);
      }
    }

    if (scale_ == kScaleSmooth) {
      master_out_q32_ = master_free_q32_;
    } else {
      // STEPPED MODES ADVANCE IN DEGREE SPACE, NOT PITCH SPACE.
      //
      // The free phase runs at a constant rate through the SCALE - one degree
      // per unit time - and DegreeToPitch maps that to where the degree
      // actually sits. Without it the pole moves at constant PITCH rate, so a
      // whole-tone gap takes twice as long to cross as a semitone and the
      // notes either side of E-F and B-C in major pass twice as quickly as
      // the rest. Heard as steps that are not even in time.
      //
      // The window and the layer increments consume the resulting PITCH and
      // never see how it was reached, so the constant-sum invariant is
      // untouched - verified 0.0000% ripple either way.
      const uint32_t pitch = DegreeToPitch(master_free_q32_, scale_);
      const uint32_t target = SnapToScale(pitch, scale_);
      // The int32_t cast is LOAD-BEARING: a signed difference takes the
      // shorter way round the octave automatically. Unsigned, a one-semitone
      // step would sometimes slew the long way - an eleven-semitone sweep in
      // the wrong direction.
      const int32_t err = (int32_t)(target - master_out_q32_);
      master_out_q32_ += (uint32_t)(err >> 6);      // ~1.4 ms approach
    }

    if (freeze_) {
      // Freeze holds the phase ADVANCE: the stack stops moving through the
      // envelope but the oscillators keep sounding.
      //
      // The FREE-running phase is pinned to the held value as well. Without
      // that it carries on advancing underneath, and releasing the gate jumps
      // to wherever it had reached - audible as a lurch, and the opposite of
      // what a freeze should do. Resuming continues from where it paused.
      master_out_q32_ = frozen_q32_;
      master_free_q32_ = frozen_q32_;
    } else {
      frozen_q32_ = master_out_q32_;
    }

    // --- per-layer increments and window gains ---
    // ONE pow2 lookup serves every layer: because the layers are exactly an
    // octave apart, inc[i] is inc[0] shifted left by i. This is the economy
    // that makes a 12-layer bank affordable at all.
    ctl_m_ = Pow2Q30(master_out_q32_);
    const int32_t m = ctl_m_;
    // inc0 = kBaseInc * (m / 2^30), in int32 only.
    //
    // m is Q30 in [2^30, 2^31), so shifting it to Q15 first keeps the product
    // inside int32: 1229782 * 65535 would overflow, but 1229782 * 65535 >> 15
    // computed as (kBaseInc >> 5) * (m >> 15) >> 10 does not. The 5 bits given
    // up cost 0.003 cents of tuning, far below the LUT's own 0.0016.
    ctl_inc0_ = ((kBaseInc >> 5) * (uint32_t)(m >> 15)) >> 10;

    // Window-position divisors, computed once per control block rather than
    // per layer.
    //
    // The whole stack spans the window exactly once: u_i = (master + i)/n, so
    // layer 0 fades in at the bottom and the top layer fades out at the top.
    // As master sweeps one octave the set shifts by exactly one layer slot.
    const uint32_t n = (uint32_t)layers_;
    oct_div_ = 0xFFFFFFFFu / n + 1u;              // 2^32 / n, one layer slot
    // Stereo width. In PING mode page 2 X is the DECAY knob, so width is
    // pinned to a fixed spread there rather than tracking it - otherwise
    // lengthening the ring would also widen the image, the same class of
    // collision that put decay and quantise on one knob.
    const uint32_t width = ping_mode_ ? 12000u : (uint32_t)page2_x_;
    width_div_ = (width << 17) / n;
    master_div_ = master_out_q32_ / n;


    if (ping_mode_ && triggered) strike_armed_ = true;
  }

  // The per-layer half of the control block - see the note in
  // ProcessSample about why this runs on its own sample.
  void __not_in_flash_func(UpdateLayers)() {
    // ROTATE THE OSCILLATOR PHASES WITH THE STACK.
    //
    // THIS IS THE FIX FOR THE BIG CLICK AT THE LOOP POINT. Heard on hardware
    // as "it sounds like the new overtone coming in at full volume".
    //
    // Crossing an octave boundary RELABELS the stack: after an upward wrap,
    // layer i holds the window gain that layer i-1 held (verified to 6e-5),
    // and its frequency relabels too, since inc[i] = inc0<<i and inc0 has
    // just halved.
    //
    // The oscillator PHASES do not relabel on their own. Without this, every
    // layer keeps its old phase while inheriting a different neighbour's gain
    // AND frequency - so the waveform steps, at the loop, every time.
    //
    // Measured: largest single-sample step 128 without the rotation and 67
    // with it, against a global maximum of 68. So the loop goes from being
    // the single largest discontinuity in the whole signal to being
    // indistinguishable from ordinary signal slew.
    //
    // The window itself was never the problem - its sum is constant AND its
    // layout is correct - which is why shepard_check.py stayed green. Both
    // conditions can hold while the phases are wrong.
    //
    // Costs 11 word moves per octave, at control rate.
    if (prev_master_valid_) {
      if (prev_master_q32_ > 0xC0000000u && master_out_q32_ < 0x40000000u) {
        wrap_pulse_ = kWrapPulseLen;      // Pulse Out 1: once per octave
        // ascending: the pattern shifts up in index
        for (int i = kMaxLayers - 1; i > 0; --i) osc_q32_[i] = osc_q32_[i - 1];
        // The octave stack's read positions must rotate WITH it - see
        // octave.h. A head that keeps its position while inheriting a
        // different shift ratio jumps in the buffer - a click per octave.
        octave_.RotateUp();
      } else if (prev_master_q32_ < 0x40000000u &&
                 master_out_q32_ > 0xC0000000u) {
        wrap_pulse_ = kWrapPulseLen;
        // descending
        for (int i = 0; i < kMaxLayers - 1; ++i) osc_q32_[i] = osc_q32_[i + 1];
        octave_.RotateDown();
      }
    }
    prev_master_q32_ = master_out_q32_;
    prev_master_valid_ = true;

    // While fading in, the (N+1)th layer must be computed too - it enters at
    // gain 0 in the N layout and rises as the blend moves toward N+1.
    active_layers_ = layers_;

    // 1/sqrt(N) is slewed rather than stepped, so the LEVEL does not jump at
    // the same instant the layout does. The layout step is unavoidable; the
    // level step is not, and two simultaneous discontinuities read as much
    // worse than one.
    {
      const int32_t target = kInvSqrtN[layers_];
      inv_sqrt_n_ += (target - inv_sqrt_n_) >> 5;   // ~20 ms
      // The live path's normaliser is slewed on the same clock, for the same
      // reason. Different LAW (1/N^0.70, see kInvPowN), identical treatment.
      const int32_t target_p = kInvPowN[layers_];
      inv_pow_n_ += (target_p - inv_pow_n_) >> 5;
    }

    for (int i = 0; i < active_layers_; ++i) {
      // A layer past Nyquist is SILENCED, not clamped.
      //
      // Clamping to 0x7FFFFFFF pins the oscillator to exactly Nyquist, which
      // is an audible artefact rather than the nothing it should be - a
      // component above half the sample rate cannot be reproduced, so the
      // honest answer is silence. Letting it wrap instead is worse still: the
      // increment aliases, and as the pole RISES the alias descends, giving a
      // tone moving against the illusion.
      //
      // With kMaxLayers = 11 this only happens in the top eighth of the
      // master sweep, where the window has already faded that layer to 0.001
      // - so it is inaudible either way and this is correctness rather than a
      // fix for something heard. `over` is checked before the shift so the
      // test itself cannot overflow.
      const bool over = (i >= 31) || (ctl_inc0_ > (0x7FFFFFFFu >> i));
      inc_[i] = over ? 0u : (ctl_inc0_ << i);

      // Shift ratio for this layer: 2^(master + i - N/2), CENTRED so the
      // rates straddle unity rather than running from 1x upward. Heads reading
      // slower than the write pointer drift slowly, which halves the worst
      // recycle rate - see octave.h.
      {
        const int32_t centre = i - (layers_ >> 1);
        uint32_t r = (uint32_t)(ctl_m_ >> 14);           // 2^frac in Q16
        if (centre >= 0) r <<= centre; else r >>= (-centre);
        if (r > kOctMaxRate) r = kOctMaxRate;
        if (r < 1024u) r = 1024u;
        oct_rate_[i] = r;
      }

      // Window position: u_i = (master_frac + i) / layers.
      //
      // Stereo width offsets the WINDOW, not the oscillator phase. Offsetting
      // phase would comb-filter on a mono sum; offsetting the window gives
      // amplitude decorrelation instead, which is mono-safe - summing L+R just
      // gives a different valid window sum, never a null.
      // u_i = (master_frac + i) / layers, in Q32.
      //
      // The numerator is master + i*2^32, which needs 36 bits - but the
      // division by `layers` brings it back under 32. Rather than carry a
      // 64-bit intermediate, split it: i*2^32/n is a whole-number-of-layers
      // offset computed once, and master/n fits in 32 bits directly.
      //
      //     (master + i*2^32) / n  ==  master/n + i*(2^32/n)   (to within 1 LSB)
      //
      // 2^32/n is exact for n = 4, 8 and approximate otherwise; the residual
      // is under 1 part in 2^32 and is absorbed by the window's smoothness.
      // Verified against the exact form in tools/shepard_check.py.
      const uint32_t u_l = master_div_ + (uint32_t)i * oct_div_;
      const uint32_t u_r = u_l + width_div_;

      win_l_[i] = (int16_t)HannQ15(u_l);
      win_r_[i] = (int16_t)HannQ15(u_r);
    }

    // PING: a trigger ARMS a strike; the strike itself happens on the next
    // sample, not in this one.
    //
    // A voice copies inc_[], win_l_/win_r_[] and oct_rate_[] - all computed
    // just above - so the snapshot must not be taken before them. But taking
    // it HERE puts the whole control block, the ~60-word copy, four voices and
    // the live path on the SAME sample, and that one sample is over budget
    // even though the steady state fits at 84%.
    //
    // Measured on hardware: ~5 V steady with four voices, pinning during fast
    // triggering. Deferring by one sample costs nothing musically - 20 us -
    // and takes the copy off the worst sample entirely.
  }


  // --- panel ---------------------------------------------------------------
  void __not_in_flash_func(ReadPanel)() {
    // Switch geometry: MIDDLE and UP are the two stable rest positions; DOWN
    // is spring-loaded momentary and returns to Middle when released. So Down
    // cannot hold a latched state - it is a button, and freeze is TOGGLED.
    const Switch sw = SwitchVal();
    if (switch_settle_ > 0) --switch_settle_;

    // Long press selects SEALED freeze. The decision has to happen on RELEASE
    // since at press time the length is unknown; the exception is the 2 s mark
    // itself, which engages while still held so there is feedback before
    // letting go.
    if (sw != last_switch_ && switch_settle_ == 0) {
      const Switch prev = last_switch_;
      last_switch_ = sw;
      switch_settle_ = kSwitchSettle;

      if (sw == Switch::Down) {
        // Reaching Down cancels any pending page change. Without this a flick
        // from Up that paused at Middle would arm a change and then commit it
        // while Down was held, dropping the page.
        page_settle_ = 0;
      } else if (prev == Switch::Down) {
        // A press toggles freeze. There is no long-press variant: SEALED was
        // inherited from WorkshopSpectral, where it held each bin's phase
        // ADVANCE in a frozen spectrum and was genuinely distinct. Here it
        // only muted the live input, which Y already does - a hidden
        // two-second gesture duplicating a knob.
        freeze_latch_ = !freeze_latch_;
      } else {
        // A page change between the two stable positions, DEFERRED - see
        // kPageSettle. Coming back from a Down press must not re-page.
        pending_page_ = (sw == Switch::Up) ? 1 : 0;
        page_settle_ = kPageSettle;
      }
    }

    if (page_settle_ > 0 && --page_settle_ == 0) {
      if (pending_page_ != page_) {
        page_ = pending_page_;
        {
          // Capture where each knob is SITTING as the page arrives, from the
          // live ADC rather than from stored_ (which holds the value the page
          // was left at, and on the first visit the constructor default).
          knob_arrival_[page_][0] = KnobVal(Knob::Main) << 3;
          knob_arrival_[page_][1] = KnobVal(Knob::X) << 3;
          knob_arrival_[page_][2] = KnobVal(Knob::Y) << 3;
          knob_live_[page_][0] = false;
          knob_live_[page_][1] = false;
          knob_live_[page_][2] = false;
        }
      }
    }

    // Pulse In 2 REVERSES the glide while held. Released, the direction goes
    // back to whatever Main says - a momentary flip rather than a latch, which
    // is what makes it playable against a clock.
    reverse_ = PulseIn2();

    // Pulse In 1 is an EVENT jack: a strike in PING, a scale step in normal
    // boot. It no longer freezes anything.
    //
    // It used to do both, and the two meanings fought each other. The rising
    // edge advanced a degree and the gate then pinned the pole for as long as
    // it stayed high, so "step the scale while the glide runs underneath" -
    // which is the interesting combination, and which the README claimed -
    // could not actually happen: the glide stopped the moment you stepped it.
    //
    // HOLD now lives on Audio In 2, which was previously unused. A hold is a
    // STATE, not an event, so a level input is the honest home for it, and it
    // leaves Pulse In 1 free to mean one thing in each mode.
    const bool pulse1 = PulseIn1();
    if (pulse1 && !last_pulse1_) ++trigger_count_;
    last_pulse1_ = pulse1;

    // HOLD from Audio In 2, with a Schmitt trigger.
    //
    // This is an AUDIO input, not a pulse input: it reads bipolar +-2048 with
    // no comparator in front of it, so a single threshold would chatter on any
    // slow edge - and chatter here means the pole stutters instead of holding.
    // Two thresholds with a wide gap between them cost one compare and remove
    // the problem entirely.
    //
    // ~1.2 V to engage, ~0.6 V to release, against a 6 V full scale. Well
    // above any plausible audio-level noise, well below a 5 V gate, and the
    // hysteresis is half the engage level rather than a few LSB.
    //
    // An UNPATCHED jack reads exactly 0 - ComputerCard zeroes disconnected
    // inputs - so the default is "not held", which is the safe way round.
    const int32_t hold_in = AudioIn2();
    if (hold_ext_) {
      if (hold_in < kHoldRelease) hold_ext_ = false;
    } else {
      if (hold_in > kHoldEngage) hold_ext_ = true;
    }

    // HOLD applies in BOTH modes, and that is deliberate.
    //
    // In normal boot it stops the glide. In PING it holds the invisible pole
    // still, so every strike gives the SAME chord until it is released - which
    // is a real performance control, and the switch-latched freeze already
    // worked that way. What was wrong before was never that PING could be
    // frozen; it was that the STRIKE TRIGGER froze it as a side effect.
    gate_freeze_ = hold_ext_;

    freeze_ = freeze_latch_ || gate_freeze_;

    // One knob per sample, round-robin. Each still updates at ~16 kHz, far
    // faster than a hand.
    switch (panel_phase_) {
      case 0: UpdateKnob(0, KnobVal(Knob::Main)); break;
      case 1: UpdateKnob(1, KnobVal(Knob::X)); break;
      case 2: UpdateKnob(2, KnobVal(Knob::Y)); break;
      default: break;
    }
    if (++panel_phase_ > 2) panel_phase_ = 0;

    // Output level. The general pickup above already holds stored_[1][2] at
    // its previous value until Y is moved, so no separate latch is needed -
    // an earlier one duplicated this and got the arrival capture wrong,
    // silencing the card on the first page change.
    param_level_ = stored_[1][2];
  }

  // PICKUP on every knob, on both pages.
  //
  // Without it, changing page snaps three parameters to wherever the knobs
  // happen to be sitting - so flicking Up to check the scale would also jump
  // the width and the level, and flicking back would jump speed, density and
  // source. Every page change was three unintended edits.
  //
  // The sibling card SPECTRAL tried pickup and removed it, and its note is
  // worth taking seriously: after a page change all three knobs felt DEAD,
  // with no indication that anything was waiting. That is a real failure and
  // the reason pickup usually feels bad.
  //
  // The fix is feedback, not abandoning pickup. This card has six LEDs, and
  // while a knob is uncaptured its LED pulses - so "dead" becomes "waiting,
  // and here is which one". See UpdateLeds().
  //
  // A knob captures when it is moved past kPickupBand from where it sat on
  // arrival. 1200 of 32767 is about 3.7%, roughly 4 degrees of travel: far
  // enough that ADC jitter cannot trip it, close enough that a deliberate
  // nudge does.
  void __not_in_flash_func(UpdateKnob)(int idx, int32_t raw) {
    const int32_t val = raw << 3;      // KnobVal is 0..4095; params are Q15

    if (!knob_live_[page_][idx]) {
      const int32_t d = val - knob_arrival_[page_][idx];
      if (d > kPickupBand || d < -kPickupBand) {
        knob_live_[page_][idx] = true;
      } else {
        return;                        // still waiting - hold the old value
      }
    }
    stored_[page_][idx] = val;
  }

  void __not_in_flash_func(UpdateLeds)() {
    // The boot splash owns the LEDs while it runs - without this the normal
    // refresh repaints them within ~5 ms and the mode pattern is never seen.
    if (boot_splash_ > 0) return;

    if (++led_phase_ < 240) return;    // ~200 Hz is plenty for LEDs
    led_phase_ = 0;

    led_pulse_ += 40;
    const int32_t tri_pk = (led_pulse_ & 2047) < 1024
                               ? (led_pulse_ & 1023)
                               : 1023 - (led_pulse_ & 1023);

    // Top row: normally the master phase as a moving dot, so the glide is
    // visible even when it is too slow to hear.
    //
    // But while any knob on this page is UNCAPTURED, that LED pulses instead.
    // This is what makes pickup usable rather than baffling: a knob that does
    // nothing is only frustrating if you cannot tell it is waiting. LED 0 is
    // Main, 1 is X, 2 is Y - the same left-to-right order as the knobs.
    const uint32_t p = master_out_q32_ >> 30;      // 0..3
    for (uint32_t i = 0; i < 3; ++i) {
      if (!knob_live_[page_][i]) {
        LedBrightness(i, (uint16_t)(300 + tri_pk * 3));
      } else {
        LedBrightness(i, (uint16_t)((p == i) ? 3000 : 200));
      }
    }

    // Bottom row: freeze state and page.
    const int32_t tri = tri_pk;
    LedBrightness(3, freeze_ ? 4095 : 0);

    // In SPIRAL mode both page LEDs carry a slow counter-phase glow, so the
    // alt-boot stays obvious after the splash has gone.
    if (ping_mode_) {
      const uint16_t glow = (uint16_t)(400 + tri);
      LedBrightness(4, page_ == 0 ? 2500 : glow);
      LedBrightness(5, page_ == 1 ? 2500 : glow);
    } else {
      LedBrightness(4, page_ == 0 ? 2500 : 0);
      LedBrightness(5, page_ == 1 ? 2500 : 0);
    }
  }

  OctaveStack octave_;
  PingBank ping_;

  int32_t boot_mute_;
  int32_t boot_splash_ = 0;
  bool ping_mode_ = false;
  uint32_t sample_count_ = 0;

  int panel_phase_ = 0;
  int page_;
  Switch last_switch_;
  int32_t switch_settle_ = 0;
  int32_t page_settle_ = 0;
  int pending_page_ = 0;
  bool last_pulse1_ = false;

  int32_t stored_[2][3];        // [page][knob]
  int32_t knob_arrival_[2][3];  // where each knob sat when the page arrived
  bool knob_live_[2][3];        // has it been moved enough to take control?

  // Oscillator bank state.
  uint32_t osc_q32_[kMaxLayers];
  uint32_t inc_[kMaxLayers];
  uint32_t oct_rate_[kMaxLayers];
  int16_t win_l_[kMaxLayers];
  int16_t win_r_[kMaxLayers];

  uint32_t prev_master_q32_ = 0;   // for octave-wrap detection
  bool prev_master_valid_ = false;
  int32_t wrap_pulse_ = 0;
  uint32_t master_div_ = 0;      // master_out_q32_ / layers
  uint32_t oct_div_ = 0;         // 2^32 / layers
  uint32_t width_div_ = 0;       // stereo width offset / layers

  uint32_t master_free_q32_ = 0;
  uint32_t master_out_q32_ = 0;
  uint32_t frozen_q32_ = 0;
  int32_t rate_q32_ = 0;
  int layers_ = 6;
  int active_layers_ = 6;   // layers_ + 1 while a new layer is fading in
  int32_t inv_sqrt_n_ = 13377;
  int32_t inv_pow_n_ = 14169;   // kInvPowN[6], matching inv_sqrt_n_'s N=6
  int scale_ = 0;
  int32_t source_mix_ = 0;
  bool shift_up_ = true;   // glide direction, for Pulse In 1 stepping

  // SPIRAL state.
  int32_t ping_decay_ = 32746;   // ~0.25 s
  bool strike_armed_ = false;
  bool strike_pending_ = false;
  int32_t page2_x_ = 0;      // page 2 X, read once - see UpdateControl
  int32_t ctl_m_ = 0;        // pow2(master), shared across the split block
  uint32_t ctl_inc0_ = 0;    // layer 0 increment, likewise

  bool freeze_ = false;
  bool gate_freeze_ = false;
  bool hold_ext_ = false;      // Schmitt state for the Audio In 2 hold
  bool reverse_ = false;
  bool freeze_latch_ = false;
  uint32_t trigger_count_ = 0;
  uint32_t trigger_seen_ = 0;

  int32_t param_level_ = 32767;
  int32_t level_smooth_ = 32767;

  int led_phase_ = 0;
  int32_t led_pulse_ = 0;
};

// Global, not on the stack: the SPIRAL buffer alone is 128 KB and the stack is
// only 4 KB.
ShepardCard card;

int main() {
  // 192 MHz. Proven on this hardware by grains/51 and glitter/53, and by the
  // sibling card.
  set_sys_clock_khz(192000, true);

  ShepardInit();

  card.EnableNormalisationProbe();
  card.Run();
}
