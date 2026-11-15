#pragma once
#include "filter.hpp"
#include "modal.hpp"
namespace mtm {
/// Signed Q15 audio. dry is the melody, wet is the audible sympathetic halo.
struct Frame {
    int32_t mix = 0, dry = 0, wet = 0;
    int32_t periodQ16 = 0; // Smoothed model pitch, before jawari contact shortening.
};

// One worker owns this 24 kHz instrument. Trigger preserves both string histories
// and all sympathetic states. Pitch/Swell changes perform coefficient division;
// next() uses bounded integer arithmetic without allocation or hardware access.
class Voice {
public:
    Voice();
    void setParameters(Parameters parameters);
    /// Q16 octaves relative to C4; effective frequency is limited to 40-2000 Hz.
    void setPitchQ16(int32_t octaves);
    void trigger(int32_t strength = 32767);
    void damp();
    Frame next(int32_t externalString = 0, int32_t externalHalo = 0);
    uint32_t clippedStates() const { return clampCount; }

private:
    static constexpr unsigned size = 1024, mask = size - 1;
    // Q15 displacement buffers; writes are bounded to +/-16383 for headroom.
    int16_t line[size]{}, transverse[size]{}, noiseLine[256]{};
    unsigned write = 0, noiseWrite = 0;
    ModalBank bank;
    Fir bankDown, bankUp;
    Parameters target{}, params{};
    // Pitch is Q16 octaves; period/targetPeriod/compensation are Q16 samples.
    int32_t pitchQ16 = INT32_MAX, period = 0, targetPeriod = 0;
    unsigned frequencyBin = 0, frequencyFraction = 0;
    int32_t decayLoss = 32;
    // One-pole and allpass states for the two displacement channels.
    int32_t lp = 0, apX = 0, apY = 0, crossLP = 0, crossAPX = 0, crossAPY = 0;
    int32_t pickup = 0, contactLP = 0, lastContactLP = 0, dc = 0, dcRemainder = 0, output = 0;
    // Q15 melody trajectories and their Q24 discarded-loss accumulators.
    int32_t early = 0, slow = 0, rise = 0;
    uint32_t earlyErr = 0, slowErr = 0, riseErr = 0;
    uint32_t rng = 1;
    int age = 100000, length = 1, pickPosition = 1, excitationStep = 32768;
    int32_t pluckStrength = 32767, pickWarp = 0, noiseLP = 0, driveLP = 0, previousDrive = 0;
    int dampHold = 0;
    int32_t damping = 0;
    unsigned clock = 0;
    int32_t compensation = 0;
    uint32_t clampCount = 0;

    void updateDecay();
    int32_t tap(const int16_t *buffer, int32_t delay) const;
    void updateCompensation();
    int32_t excitation();
};
} // namespace mtm
