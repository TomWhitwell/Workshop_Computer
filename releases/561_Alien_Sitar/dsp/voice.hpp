/// @file voice.hpp
/// @brief The 24 kHz fixed-point sitar instrument: string loop + modal bank.
///
/// Voice is the whole Core1-owned instrument. next() computes one 24 kHz
/// frame from two 1024-sample travelling displacement histories (normal and
/// transverse), a pluck exciter with a jawari contact model, bridge radiation
/// and the 12 kHz ModalBank behind a halfband Fir pair, returning the mixed
/// output plus dry/wet labels. All state is fixed-size; hot-path arithmetic
/// is bounded 32-bit integer with no allocation, no blocking and no hardware
/// access, sized for the 41.67 us average worker frame. Setters only retarget
/// control state and are safe between frames.
#pragma once
#include "filter.hpp"
#include "modal.hpp"
namespace mtm {
/// Signed Q15 audio. dry is the melody, wet is the audible sympathetic halo.
struct Frame {
    /// Full instrument (mix), melody only (dry) and halo only (wet), Q15.
    int32_t mix = 0, dry = 0, wet = 0;
    int32_t periodQ16 = 0; // Smoothed model pitch, before jawari contact shortening.
};

/// One plucked sitar instrument: melody string, sympathetic halo and mixer.
// One worker owns this 24 kHz instrument. Trigger preserves both string histories
// and all sympathetic states. Pitch/Swell changes perform coefficient division;
// next() uses bounded integer arithmetic without allocation or hardware access.
class Voice {
public:
    /// Compute boot defaults: initial period, compensation and decay trail.
    Voice();
    /// Stage new control values; per-frame smoothing happens inside next().
    ///
    /// @param parameters Q15 controls (bipolar pick; integer decay percents).
    /// @note Inputs are bounded here; no coefficient work when unchanged.
    void setParameters(Parameters parameters);
    /// Pass the twelve sympathetic-string MIDI note numbers to the bank.
    void setTarafNotes(const uint8_t (&notes)[12]) { bank.setNotes(notes); }
    /// Q16 octaves relative to C4; effective frequency is limited to 40-2000 Hz.
    ///
    /// @param octaves Pitch offset in 1/65536-octave units around C4.
    /// @note Also refreshes pitch-dependent delay compensation and decay.
    void setPitchQ16(int32_t octaves);
    /// Strike a fresh pluck that keeps every ringing state and history.
    ///
    /// @param strength Q15 pluck amplitude (0..32767; default full).
    /// @note Latches Pick and preserves the existing melody rise.
    void trigger(int32_t strength = 32767);
    /// Momentary damping: raises loop loss over a short smoothed hold.
    void damp();
    /// Advance the whole instrument by one 24 kHz frame.
    ///
    /// @param externalString Q15 melody-string drive (bounded to +/-8191).
    /// @param externalHalo Q15 sympathetic-bank drive (bounded to +/-8191).
    /// @return Q15 {mix, dry, wet} plus the smoothed Q16 model period.
    /// @note Heavy 12 kHz bank work runs on alternate frames only.
    Frame next(int32_t externalString = 0, int32_t externalHalo = 0);
    /// Counter of loop writes clamped into the displacement bounds (diagnostics).
    uint32_t clippedStates() const { return clampCount; }
    /// Modal-bank clamped-state counter (same counter as clippedModalStates()).
    uint32_t clippedSympatheticStates() const { return bank.clippedStates(); }
    /// Modal-bank clamped-state counter (alias kept for the older name).
    uint32_t clippedModalStates() const { return bank.clippedStates(); }

private:
    /// Delay-line length and ring-address mask for the displacement histories.
    static constexpr unsigned size = 1024, mask = size - 1;
    // Q15 displacement buffers; writes are bounded to +/-16383 for headroom.
    int16_t line[size]{}, transverse[size]{}, noiseLine[256]{};
    /// Ring write cursors: the two histories and the 256-word pluck-noise comb.
    unsigned write = 0, noiseWrite = 0;
    /// Sympathetic bank plus its pre-decimation and up-conversion halfband FIRs.
    ModalBank bank;
    Fir bankDown, bankUp;
    /// Requested control values and their per-frame-smoothed copies, Q15.
    Parameters target{}, params{};
    // Pitch is Q16 octaves; period/targetPeriod/compensation are Q16 samples.
    int32_t pitchQ16 = INT32_MAX, period = 0, targetPeriod = 0;
    /// Compensation frequency bin 0..63 (30.625 Hz) and the fraction inside it.
    unsigned frequencyBin = 0, frequencyFraction = 0;
    /// Per-sample loop loss (Q15) and the Q8 decay target/smoothing trail.
    int32_t decayLoss = 32, decayTargetQ8 = 32 * 256, decaySmoothedQ8 = 32 * 256;
    /// Bank decay percent (Q8) converged on the 32-frame control tick.
    int32_t sympatheticPercentQ8 = 100 * 256;
    /// Set once a non-100% melody decay is requested (enables Q8 smoothing).
    bool decayAdjusted = false;
    // One-pole and allpass states for the two displacement channels.
    int32_t lp = 0, apX = 0, apY = 0, crossLP = 0, crossAPX = 0, crossAPY = 0;
    /// Bridge-velocity low-pass state for the smooth radiation blend.
    int32_t radiationLP = 0;
    /// Per-sample smoothers: bridge pickup, contact force, then the output
    /// DC-block/slew chain.
    int32_t pickup = 0, contactLP = 0, lastContactLP = 0, dc = 0, dcRemainder = 0, output = 0;
    // Q15 melody trajectories and their Q24 discarded-loss accumulators.
    int32_t early = 0, slow = 0, rise = 0;
    /// Sub-LSB remainders for the trajectory decays above.
    uint32_t earlyErr = 0, slowErr = 0, riseErr = 0;
    /// xorshift32 pluck-noise state (seeded nonzero at construction).
    uint32_t rng = 1;
    /// Pluck phase: samples since trigger, burst length in samples, pick
    /// offset into the noise line and the per-sample ramp step.
    int age = 100000, length = 1, pickPosition = 1, excitationStep = 32768;
    /// Pluck amplitude and latched Pick warp plus the pluck-filter states.
    int32_t pluckStrength = 32767, pickWarp = 0, noiseLP = 0, driveLP = 0, previousDrive = 0;
    /// damp() hold countdown and the smoothed damping value it drives.
    int dampHold = 0;
    int32_t damping = 0;
    /// Phrase balance: decaying recent pluck level and the coupling cut it
    /// causes when strikes come close together.
    int32_t recentPluck = 0, couplingReduction = 0;
    /// Q24 sub-LSB remainders for the two decaying measures above.
    uint32_t recentPluckErr = 0, couplingErr = 0;
    /// Frame counter; low bits schedule the control tick and the bank rate.
    unsigned clock = 0;
    /// Frequency/String-dependent loop-delay compensation, Q16 samples.
    int32_t compensation = 0;
    /// Count of loop writes clamped into the displacement bounds.
    uint32_t clampCount = 0;

    /// Recompute the Q8 decay target from Swell and melodyDecayPercent.
    void updateDecay();
    /// Cubic-interpolated history read at a Q16-sample delay.
    ///
    /// @param buffer Displacement history to read (line or transverse).
    /// @param delay Read distance in Q16 samples (clamped to 8..900).
    /// @return Interpolated displacement bounded to +/-16383 (write headroom).
    int32_t tap(const int16_t *buffer, int32_t delay) const;
    /// Refresh the frequency/String-dependent loop-delay compensation.
    void updateCompensation();
    /// Render the current pluck sample (0 when no pluck is active).
    int32_t excitation();
};
} // namespace mtm
