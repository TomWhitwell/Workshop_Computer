/// @file voice.cpp
/// @brief Core1 worker implementation of the fixed-point sitar voice.
///
/// One 24 kHz pass per frame: pluck excitation, travelling-wave contact
/// model, loop propagation with jawari loss, bridge radiation and the 12 kHz
/// sympathetic bank behind halfband converters, then melody shaping and
/// click-free mixing. Control smoothing and the heavy bank render are
/// budgeted against the 41.67 us average worker frame; all arithmetic is
/// bounded integer with tracked clamp counters.
#include "voice.hpp"
#include "tables.hpp"
namespace mtm {
Voice::Voice() {
    setPitchQ16(0);
    period = targetPeriod;
    updateCompensation();
    decaySmoothedQ8 = decayTargetQ8;
}

void Voice::updateDecay() {
    // Swell maps 0..32767 onto a 2.0..5.0 s loop time constant (ms).
    const uint32_t tauMs = 2000u + uint32_t(target.swell) * 3000u / 32767u;
    // Preserve the original integer loss exactly at 100%; finer Q8 steps
    // make changes smooth even when the original loss is only a few units.
    int32_t legacy =
        int32_t((uint64_t(targetPeriod) * 32768u * 1000u) / (uint64_t(65536u) * RATE * tauMs));
    // Q8 loss; higher percent stretches decay, so the loss divides by it.
    decayTargetQ8 = legacy * 256 * 100 / target.melodyDecayPercent;
}

void Voice::setParameters(Parameters p) {
    p = bounded(p);
    bool changed = p.swell != target.swell || p.melodyDecayPercent != target.melodyDecayPercent;
    if (p.melodyDecayPercent != 100) {
        decayAdjusted = true;
    }
    target = p;
    if (changed) {
        updateDecay();
    }
}

void Voice::setPitchQ16(int32_t octaves) {
    octaves = clip(octaves, -200000, 220000);
    if (octaves == pitchQ16) {
        return;
    }
    pitchQ16 = octaves;
    // Floor-division into whole octaves plus a non-negative table fraction.
    int octave = octaves / 65536;
    int fraction = octaves % 65536;
    if (fraction < 0) {
        fraction += 65536;
        --octave;
    }
    // Q16 period table spans C4..C5 in 1/1024-octave steps, lerped at 6 bits.
    unsigned i = unsigned(fraction) >> 6, f = unsigned(fraction) & 63;
    int32_t p = tables::period[i] + ((tables::period[i + 1] - tables::period[i]) * int32_t(f) >> 6);
    // Shift the base-band period for whole octaves above/below C4.
    if (octave >= 0) {
        p >>= octave;
    } else {
        p *= 1 << (-octave);
    }
    // 12..600 Q16 samples = 40..2000 Hz at 24 kHz.
    targetPeriod = clip(p, 12 * 65536, 600 * 65536);
    // Control-rate arithmetic only; all sample-loop products fit signed 32 bits.
    uint64_t hzQ16 = (uint64_t(RATE) * 65536u * 65536u) / unsigned(targetPeriod);
    // 64 delay-compensation bins across 40..2000 Hz (30625/16 Hz per bin).
    uint64_t pos = (hzQ16 - (40u * 65536u)) * 64u * 65536u / (1960u * 65536u);
    frequencyBin = std::min(63u, unsigned(pos >> 16));
    // At exactly 2000 Hz the position lands on the bin-64 boundary with a
    // zeroed fraction; pin the top bin to its interpolation endpoint instead.
    frequencyFraction =
        frequencyBin == 63u && pos >= 64u * 65536u ? 65535u : unsigned(pos) & 65535u;
    updateDecay();
}

void Voice::trigger(int32_t strength) {
    // Retriggering raises the phrase-balance cut to at least the previous
    // stroke's level (see the couplingReduction member).
    couplingReduction = std::max(couplingReduction, recentPluck);
    recentPluck = 32767;
    recentPluckErr = 0;
    pluckStrength = clip(strength, 0, 32767);
    age = 0;
    // Pluck geometry: one full current period, rounded whole samples.
    length = clip((targetPeriod + 32768) >> 16, 12, 600);
    // Offset of the pick along the string; bounded to the 256-word noise line.
    pickPosition = clip((length * target.pickPositionPercent) / 100, 1, 255);
    // Q15 ramp step that reaches unity exactly at the end of one period.
    excitationStep = 32768 / length;
    // Pick applies to the next pluck: latch the bipolar warp now.
    pickWarp = target.pick;
    // Keep the existing melody rise when a new pluck enters the same string.
    // Restarting both trajectories at unity used to erase that rise on every
    // rapid strike while the sympathetic bank kept all of its energy.
    const int32_t existingGrowth = slow - rise;
    early = slow = 32767;
    rise = 32767 - existingGrowth;
    earlyErr = slowErr = riseErr = 0;
    driveLP = previousDrive = 0;
}

void Voice::damp() {
    // Hold damping engaged for 250 ms; next() smooths its release.
    dampHold = RATE / 4;
}

int32_t Voice::tap(const int16_t *buffer, int32_t delay) const {
    delay = clip(delay, 8 * 65536, 900 * 65536);
    // floor(write-delay), converted to a positive fraction explicitly.
    int integer = delay >> 16;
    unsigned fraction = unsigned(delay) & 65535u;
    unsigned i = (write - unsigned(integer) - (fraction ? 1u : 0u)) & mask;
    // 256-phase cubic table: (65536 - fraction) >> 8 selects the 4-tap set.
    unsigned phase = fraction ? ((65536u - fraction) >> 8) : 0u;
    const int16_t *c = &tables::cubic[phase * 4];
    // Four-tap dot product; cubic coefficients are Q13, so >> 13 returns the
    // interpolated sample, still in displacement units.
    int32_t sum = int32_t(buffer[(i - 1) & mask]) * c[0] + int32_t(buffer[i]) * c[1] +
                  int32_t(buffer[(i + 1) & mask]) * c[2] + int32_t(buffer[(i + 2) & mask]) * c[3];
    // Keep reads inside the same +/-16383 displacement bound as writes.
    return clip(sum >> 13, -16383, 16383);
}

void Voice::updateCompensation() {
    // delayComp: 32 String rows x 65 frequency bins of Q12 sample offsets,
    // interpolated vertically (string) and horizontally (bin fraction).
    // Entries are trimmed to Q12; *16 at the end lifts them back to Q16.
    unsigned pos = unsigned(params.string) * 2u, mi = std::min(31u, pos >> 11), mf = pos & 2047u;
    const int16_t *a = &tables::delayComp[mi * 65 + frequencyBin];
    const int16_t *b = a + 65;
    int32_t lo = a[0] + ((int32_t(a[1]) - a[0]) * int32_t(frequencyFraction) >> 16);
    int32_t hi = b[0] + ((int32_t(b[1]) - b[0]) * int32_t(frequencyFraction) >> 16);
    // Blend the two string rows at the 11-bit fraction; *16 lifts Q12 -> Q16.
    compensation = (lo + ((hi - lo) * int32_t(mf) >> 11)) * 16;
}

int32_t Voice::excitation() {
    // Done once both the period-combed burst and the 53-sample release profile end.
    if (age >= length && age >= 53) {
        return 0;
    }
    // Top 16 bits of the xorshift state become bipolar noise.
    int32_t n = int32_t(random(rng) >> 16) - 32768;
    // One-pole noise coefficient centred at 0.65, warped by bipolar Pick:
    // softened toward 0.25 for negative pick (soft release), snappier toward
    // 0.8 for positive pick (sharper wire release). All Q15.
    int32_t alpha = 21299 + mul(pickWarp, pickWarp < 0 ? 13107 : 4915);
    noiseLP += mul(n - noiseLP, alpha);
    // Comb read one pick-position back; the difference is the burst.
    int32_t old = noiseLine[(noiseWrite - unsigned(pickPosition)) & 255u];
    noiseLine[noiseWrite] = int16_t(noiseLP);
    noiseWrite = (noiseWrite + 1) & 255u;
    int32_t legacy = 0, mizrab = 0;
    if (age < length) {
        // Parabolic raised-cosine window over one period (Q15 amplitude).
        int32_t t = age * excitationStep;
        int32_t window = 4 * mul(t, 32768 - t);
        legacy = mul(mul(noiseLP - old, window), 3932);
    }
    if (age < 53) {
        // Wire-release profile plus a shaped pulse term ("mizrab").
        mizrab = mul(mul(noiseLP - old, tables::pickWindow[age]), 4456) +
                 mul(tables::pickPulse[age], 786);
    }
    ++age;
    // Q15 blend weights: two thirds legacy burst, one third release profile.
    int32_t drive = mul(legacy, 21299) + mul(mizrab, 11469);
    drive = mul(drive, pluckStrength);
    // Softer release left; bounded differentiated wire release right.
    driveLP += mul(drive - driveLP, pickWarp < 0 ? 32767 + mul(pickWarp, 24576) : 32767);
    int32_t shape =
        pickWarp < 0 ? driveLP : drive + mul(drive - previousDrive, mul(pickWarp, 16384));
    previousDrive = drive;
    // Excitation stays at quarter amplitude for loop headroom.
    return clip(shape, -8191, 8191);
}

Frame Voice::next(int32_t externalString, int32_t externalHalo) {
    // 1. Smooth control targets and pitch before reading delay lines.
    params.string = approach(params.string, target.string, 8);
    params.jawari = approach(params.jawari, target.jawari, 8);
    params.swell = approach(params.swell, target.swell, 8);
    params.sympathy = approach(params.sympathy, target.sympathy, 8);
    params.haloLevel = approach(params.haloLevel, target.haloLevel, 8);
    period = approach(period, targetPeriod, 5);
    decaySmoothedQ8 = decayAdjusted ? approach(decaySmoothedQ8, decayTargetQ8, 8) : decayTargetQ8;
    decayLoss = (decaySmoothedQ8 + 128) >> 8;
    // Control tick every 32 frames: refresh compensation and the bank duration.
    if ((clock++ & 31u) == 0) {
        updateCompensation();
        sympatheticPercentQ8 =
            approach(sympatheticPercentQ8, target.sympatheticDecayPercent * 256, 5);
        bank.setDurationQ8(sympatheticPercentQ8);
    }
    damping = approach(damping, dampHold > 0 ? 32767 : 0, 5);
    if (dampHold > 0) {
        --dampHold;
    }

    // 2. One-sided contact shortens the delay; this is not ordinary detuning.
    int32_t jawari = params.jawari;
    int32_t delay = period - compensation;
    int32_t incoming = tap(line, delay);
    // Jawari-scaled early pluck trajectory gates transverse-to-normal coupling.
    int32_t motion = mul(mul(early, 22938), jawari);
    // Transverse delay sits a little longer than period, minus this branch's
    // own filter lag (a small period-proportional stretch and a fixed part).
    int32_t cross = tap(transverse, period + (period >> 10) * 4 - 94372);
    int32_t normal = clip(incoming + mul(cross, mul(motion, 24576)) * 2, -32767, 32767);
    // One-sided contact: only displacement above the 393 deadband presses.
    int32_t contact = std::max<int32_t>(0, normal - 393);
    // Maximum shortening is min(2.2 samples, .9% of one string period).
    int32_t depth = std::min<int32_t>(144179, (period >> 10) * 9);
    // Shift down around the muls so each product stays inside int32; *8 and
    // the /3 pre-bias restore the intended scale.
    int32_t shortening = mul(depth >> 3, mul(jawari, ratio(tables::ratio035, contact))) * 8;
    incoming = tap(line, delay - shortening);
    // Loop low-pass: String sets the pole between about .82 and .995.
    // Loop low-pass: String sets the pole between about .82 and .995.
    lp += mul(incoming - lp, 26870 + mul(params.string, 5734));
    // Fixed first-order allpass (.3, Q15) adds the matched phase shift.
    const int32_t a = 9830;
    int32_t propagated = clip(-mul(a, lp) + apX + mul(a, apY), -16383, 16383);
    apX = lp;
    apY = propagated;
    // Jawari loss bites only the contact side, above a small 819 bias.
    int32_t over = std::max<int32_t>(0, propagated - 819);
    propagated -= mul(over, mul(jawari, 393));
    // damp() scales up decayLoss (up to +.2 loss per sample); the 30000 cap
    // stops the loop from collapsing toward silence.
    int32_t loss = clip(decayLoss + mul(damping, 6554), 0, 30000);
    // Bounded sum: self pluck plus external string drive stays under the
    // +/-16383 history write headroom.
    int32_t drive = clip(excitation() + clip(externalString, -8191, 8191), -12000, 12000);
    int32_t outgoing = mul(propagated, 32768 - loss) + drive;

    // 3. Propagate transverse displacement and exchange energy between channels.
    // The transverse branch repeats the main one-pole + allpass pair with
    // its own fixed coefficients.
    crossLP += mul(cross - crossLP, 32276);
    int32_t q = -mul(5898, crossLP) + crossAPX + mul(5898, crossAPY);
    crossAPX = crossLP;
    crossAPY = clip(q, -16383, 16383);
    int32_t other = mul(clip(q, -16383, 16383), 32768 - loss) + mul(drive, 11469);
    // Contact-dependent rotation between the two displacement channels
    // (Givens form: c = 1 - k^2 with k rising as contact deepens, so the
    // pair exchanges energy while keeping its combined scale).
    int32_t k = mul(mul(motion, 1475), ratio(tables::ratio020, contact));
    int32_t c = 32768 - ((k * k) >> 16);
    int32_t otherOut = mul(c, other) - mul(k, outgoing);
    outgoing = mul(c, outgoing) + mul(k, other);
    // Clamp counting backs clippedStates(): writes must stay inside the
    // +/-16383 history headroom.
    if (abs32(outgoing) > 16383 || abs32(otherOut) > 16383) {
        ++clampCount;
    }
    line[write] = int16_t(clip(outgoing, -16383, 16383));
    transverse[write] = int16_t(clip(otherOut, -16383, 16383));
    write = (write + 1) & mask;

    // 4. Bridge radiation excites the persistent sympathetic bank.
    // Radiation is velocity-like: mostly the one-pole velocity estimate plus
    // a little raw displacement, in Q15.
    int32_t velocity = incoming - lp;
    int32_t bridge = mul(incoming, 3932) + 7 * velocity;
    // Jawari-gated transverse leakage into the bridge signal.
    bridge += mul(motion, mul(cross, 3932) + clip(40 * (cross - crossLP)));
    pickup += mul(clip(bridge) - pickup, 24576);
    // Contact pressure through a strong compressive curve, then smoothed.
    int32_t force = mul(contact, ratio(tables::ratio003, contact));
    contactLP += mul(force - contactLP, 6554);
    // Sympathetic exchange rides the smoothed contact-force change.
    int32_t exchange = mul(motion, clip(18 * (contactLP - lastContactLP)));
    lastContactLP = contactLP;
    // Closely spaced strokes share a finite bridge-coupling budget. Only new
    // string energy is reduced; the resonators ring freely and external halo
    // excitation keeps its original path. An isolated pluck is unchanged.
    const int32_t coupling = 32768 - mul(couplingReduction, 24576);
    int32_t coupledString = pickup + (5 * drive) / 2 + exchange;
    if (couplingReduction) {
        // Bound the product without changing the original external-input sum
        // when no phrase reduction is active.
        coupledString = mul(clip(coupledString), coupling);
    }
    int32_t bankDrive = bankDown.next(clip(coupledString + clip(externalHalo, -8191, 8191)));
    decayWithRemainder(recentPluck, recentPluckErr, 2796);
    decayWithRemainder(couplingReduction, couplingErr, 1165);
    int32_t bankSample = 0;
    // Bank renders every other frame at its 12 kHz rate; zero holds the gap.
    if (clock & 1u) {
        bankSample = bank.next(bankDrive, damping);
    }
    int32_t wet = clip(2 * bankUp.next(bankSample));

    // 5. Shape the melody, then mix and limit without clicks.
    // Swell changes the string decay and its secondary rise, not halo tuning.
    int32_t growth = mul(slow - rise, params.swell);
    // Bloom envelope: a Swell-scaled base plus about 2.4x the secondary
    // growth; the melody applies it clipped to 0..1.5 (Q15).
    int32_t bloom = 32768 - mul(params.swell, 21299) + 2 * growth + mul(growth, 13107);
    wet = mul(wet, params.sympathy);
    // Preserve the candidate radiation blend while applying the USB halo trim.
    wet = mul(wet, params.haloLevel);
    radiationLP += mul(pickup - radiationLP, 6554);
    int32_t radiation = mul(pickup, 8192) + mul(radiationLP, 24576);
    int32_t melody = mul(radiation, clip(bloom, 0, 49152));
    decayWithRemainder(early, earlyErr, 1553);
    decayWithRemainder(slow, slowErr, 582);
    decayWithRemainder(rise, riseErr, 2796);
    // Final mix: full melody plus 7/5 of the (sympathy/halo-scaled) halo.
    int32_t mixed = melody + (7 * wet) / 5;
    // DC follower steps one 512th of the error per frame (~7 Hz corner),
    // a gain-3 soft limiter, then a bounded anti-click output slew.
    dc += carryStep(mixed - dc, dcRemainder, 9);
    int32_t y = limit(3 * (mixed - dc));
    output += clip(y - output, -2048, 2048);
    return {clip(output), clip(melody), clip(wet), period};
}
} // namespace mtm
