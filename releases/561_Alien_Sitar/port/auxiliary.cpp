/**
 * @file auxiliary.cpp
 * @brief Pitch-period inversion and the composition of core0 auxiliary state.
 *
 * pitchMillivoltsFromPeriod walks the shared Q16 period table (1/1024-octave
 * entries over C4-C5) plus octave folding for the full 40-2000 Hz span.
 * AuxiliaryOutputs composes the level followers, bloom detection and the
 * 750 Hz decimated pitch refresh that core0 reads for its CV outputs.
 *
 *
 * @date 2026-11-16
 */
#include "auxiliary.hpp"
#include "../dsp/tables.hpp"
#include "config.hpp"

namespace computer {
int32_t pitchMillivoltsFromPeriod(int32_t periodQ16) {
    if (periodQ16 <= 0) {
        return 0;
    }
    // Same 40-2000 Hz bounds as Voice. Normalise into the C4-C5 period table.
    // 24000/12 samples -> 2000 Hz, 24000/600 -> 40 Hz.
    int32_t period = mtm::clip(periodQ16, 12 * 65536, 600 * 65536);
    int octave = 0;
    // Fold periods outside the table into it by power-of-two period scaling.
    while (period > mtm::tables::period[0]) {
        period >>= 1;
        --octave;
    }
    while (period < mtm::tables::period[1024]) {
        period *= 2;
        ++octave;
    }
    unsigned lower = 0, upper = 1024;
    while (upper - lower > 1) { // Binary search over the decreasing period table.
        const unsigned middle = (lower + upper) / 2;
        if (mtm::tables::period[middle] >= period) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    const int32_t span = mtm::tables::period[lower] - mtm::tables::period[upper];
    // Interpolate in Q16-octave units: 64 sub-steps of a 1/1024-octave entry.
    const int32_t fraction = (mtm::tables::period[lower] - period) * 64 / span;
    const int32_t octavesQ16 = octave * 65536 + static_cast<int32_t>(lower) * 64 + fraction;
    const int32_t scaled = octavesQ16 * 1000;
    // Round to nearest millivolt symmetrically; about +/-0.6 cents quantisation.
    return scaled >= 0 ? (scaled + 32768) / 65536 : -((-scaled + 32768) / 65536);
}

void AuxiliaryOutputs::advance(const mtm::Frame &frame, bool validFrame) {
    levels.next(frame.dry, frame.wet);
    bloom.advance(frame.wet, validFrame);
    // Inversion is control-rate work, outside the 24 kHz DSP worker. Ignore
    // startup/fallback frames and hold the last pitch when no valid state arrives.
    // Every 32nd 24 kHz frame is the 750 Hz control-rate pitch refresh.
    if (validFrame && frame.periodQ16 > 0 && (frames & 31u) == 0) {
        pitchMv = pitchMillivoltsFromPeriod(frame.periodQ16);
    }
    ++frames;
}

void AuxiliaryOutputs::notifyDamp() {
    bloom.suppress();
}

int32_t AuxiliaryOutputs::haloMillivolts() const {
    return levels.haloMv();
}
} // namespace computer
