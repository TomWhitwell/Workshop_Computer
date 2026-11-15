#pragma once
#include "../dsp/voice.hpp"
#include "bloom.hpp"
#include "envelopes.hpp"

namespace computer {
// Convert the model's smoothed Q16 string period to nominal 1 V/oct, 0 V=C4.
// This is a model-state conversion, not a measured audio-frequency estimator.
int32_t pitchMillivoltsFromPeriod(int32_t periodQ16);

// Shared device/desktop auxiliary outputs. Core0 alone owns this state.
class AuxiliaryOutputs {
public:
    void advance(const mtm::Frame &frame, bool validFrame = true);
    void notifyDamp();
    int32_t pitchMillivolts() const { return pitchMv; }
    int32_t haloMillivolts() const;
    int32_t dryLevelQ15() const { return levels.dry; }
    int32_t haloLevelQ15() const { return levels.halo; }
    bool bloomPulseHigh() const { return bloom.pulseHigh(); }

private:
    Envelopes levels;
    BloomDetector bloom;
    int32_t pitchMv = 0;
    uint32_t frames = 0;
};
} // namespace computer
