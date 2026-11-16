/// @file modal.hpp
/// @brief 12 kHz sympathetic-resonance modal bank behind the sitar voice.
///
/// Twelve editable sympathetic strings of five partial slots each (harmonics
/// 1, 3, 5, 7, 2 - sixty modes), built from damped fast/slow resonator pairs
/// with wide 24-bit state, driven by the shared bridge signal. The Core1
/// voice worker renders the bank at its own 12 kHz rate behind a halfband
/// filter pair. All work is bounded integer arithmetic: no allocation, no
/// floating point, no hardware access; states saturate rather than overflow.
#pragma once
#include "fixed.hpp"
namespace mtm {
/// Bank of sixty sympathetic modes arranged as twelve strings x five partials.
// 12 kHz, twelve strings with partials 1, 3, 5, 7, 2 (sixty slots total).
// Each has two damped resonators at the same frequency.
// Subtracting their fast/slow responses makes a causal metallic bloom. Wider
// state coordinates keep weak tails alive without four fraction-carry loops.
struct ModalBank {
    /// Bank sample rate in Hz and total mode count (12 strings x 5 partials).
    static constexpr int rate = 12000, count = 60;
    /// |state| saturation bound; keeps the split 32-bit products representable.
    static constexpr int32_t stateLimit = (1 << 24) - 1;
    /// One partial: Q17 radius coefficients, Q24 losses, an injection gain and
    /// the paired resonator states (slow branch s1/s2, fast branch f1/f2).
    struct Mode {
        int32_t aSlow, lossSlow, aFast, lossFast, gain;
        int32_t s1 = 0, s2 = 0, f1 = 0, f2 = 0;
    };
    /// Monotonic count of mode states clamped since construction (diagnostics).
    uint32_t clippedStates() const { return clampCount; }
    /// Fixed-size array type of the whole mode bank.
    using Modes = Mode[count];
    /// Read-only view of all modes for inspection and testing.
    const Modes &modesView() const { return modes; }

private:
    /// One state set per partial slot, count total.
    Modes modes;
    /// Monotonic clamped-state counter, reported by clippedStates().
    uint32_t clampCount = 0;
    /// Sympathetic decay percent (Q8, clamped 25..400) and its Q12 reciprocal.
    int32_t durationQ8 = 100 * 256, inverseQ12 = 4096;
    /// Round-robin cursor and remaining count for the spread coefficient update.
    unsigned updateIndex = 0, updatesRemaining = 0;
    /// Live MIDI note per string, and the pending retune target for each.
    uint8_t notes[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    uint8_t pending[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    // Retuning fades the existing bank to zero, replaces one mode per frame,
    // then fades in. The last requested full snapshot wins.
    /// Current retune fade level: 0 mutes the bank output, 32768 is full.
    int32_t tuningFade = 32768;
    /// Retune state machine stages tracked by the fade above.
    enum class TuningPhase { Stable, FadeOut, Replace, FadeIn } phase = TuningPhase::Stable;
    /// Next slot to rebuild while replacing modes after a note change.
    unsigned replaceIndex = 0;
    /// Rebuild mode i from the current tuning/loss tables and zero its states.
    ///
    /// @param i Mode slot index, 0..count-1.
    void replaceMode(unsigned i);

public:
    /// Build all sixty modes for the boot tuning and zero their states.
    ModalBank();
    /// Wide Q17 radius feedback term split to keep products inside int32.
    ///
    /// @param x Resonator state, |x| < 2^24.
    /// @param a Q17 radius coefficient, |a| <= 262144.
    /// @return Feedback contribution in the same (wide) state scale.
    // Split wide multiplies as NOTES.md suggests. Products remain signed 32-bit:
    // |x| < 2^24, |a| <= 262144, and loss <= 65535. No 64-bit hot-path helpers.
    static int32_t feedback(int32_t x, int32_t a);
    /// Wide Q24 loss term split to keep products inside int32.
    ///
    /// @param x Resonator state, |x| < 2^24.
    /// @param l Q24 loss coefficient (1 - r^2).
    /// @return Decay term in the same (wide) state scale.
    static int32_t loss(int32_t x, int32_t l);
    /// Advance one damped second-order resonator by one bank sample.
    ///
    /// @param y1 Previous output state in/out.
    /// @param y2 Output before y1 in/out.
    /// @param a Q17 radius coefficient (2r*cos(w)).
    /// @param l Q24 loss (1 - r^2).
    /// @param injection Drive sample added into the resonator.
    /// @return New output in the same (wide) state scale, saturated to
    ///         +/-stateLimit (clamps count toward the diagnostics counter).
    /// @note Two calls per mode per frame: one slow branch, one fast.
    int32_t advance(int32_t &y1, int32_t &y2, int32_t a, int32_t l, int32_t injection);
    /// Retarget the shared sympathetic decay duration.
    ///
    /// @param percentQ8 Duration percent in Q8 (clamped 25..400%).
    /// @note Schedules a coefficient update pass spread across following
    ///       frames; histories keep ringing meanwhile.
    void setDurationQ8(int32_t percentQ8);
    /// Queue new per-string MIDI notes for a fade-out/replace/fade-in retune.
    ///
    /// @param value Twelve MIDI note numbers; each clamped to 36..84.
    void setNotes(const uint8_t (&value)[12]);
    /// Render one 12 kHz bank sample and service any pending updates.
    ///
    /// @param bridge Q15 shared bridge drive (clipped internally).
    /// @param dampQ15 Q15 extra damping request (clamped to 0..32767; scaled
    ///        down to a small per-mode loss increase).
    /// @return Q15-scale summed halo contribution for this bank frame.
    int32_t next(int32_t bridge, int32_t dampQ15);
};
} // namespace mtm
