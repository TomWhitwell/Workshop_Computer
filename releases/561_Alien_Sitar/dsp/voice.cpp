#include "voice.hpp"
#include "tables.hpp"
namespace mtm {
Voice::Voice() {
    setPitchQ16(0);
    period = targetPeriod;
    updateCompensation();
}

void Voice::updateDecay() {
    const uint32_t tauMs = 2000u + uint32_t(target.swell) * 3000u / 32767u;
    decayLoss =
        int32_t((uint64_t(targetPeriod) * 32768u * 1000u) / (uint64_t(65536u) * RATE * tauMs));
}

void Voice::setParameters(Parameters p) {
    p = bounded(p);
    bool changed = p.swell != target.swell;
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
    int octave = octaves / 65536;
    int fraction = octaves % 65536;
    if (fraction < 0) {
        fraction += 65536;
        --octave;
    }
    unsigned i = unsigned(fraction) >> 6, f = unsigned(fraction) & 63;
    int32_t p = tables::period[i] + ((tables::period[i + 1] - tables::period[i]) * int32_t(f) >> 6);
    if (octave >= 0) {
        p >>= octave;
    } else {
        p *= 1 << (-octave);
    }
    targetPeriod = clip(p, 12 * 65536, 600 * 65536);
    // Control-rate arithmetic only; all sample-loop products fit signed 32 bits.
    uint64_t hzQ16 = (uint64_t(RATE) * 65536u * 65536u) / unsigned(targetPeriod);
    uint64_t pos = (hzQ16 - (40u * 65536u)) * 64u * 65536u / (1960u * 65536u);
    frequencyBin = std::min(63u, unsigned(pos >> 16));
    frequencyFraction =
        frequencyBin == 63u && pos >= 64u * 65536u ? 65535u : unsigned(pos) & 65535u;
    updateDecay();
}

void Voice::trigger(int32_t strength) {
    pluckStrength = clip(strength, 0, 32767);
    age = 0;
    length = clip((targetPeriod + 32768) >> 16, 12, 600);
    pickPosition = clip((length * 17) / 100, 1, 255);
    excitationStep = 32768 / length;
    pickWarp = target.pick;
    early = slow = rise = 32767;
    earlyErr = slowErr = riseErr = 0;
    driveLP = previousDrive = 0;
}

void Voice::damp() {
    dampHold = RATE / 4;
}

int32_t Voice::tap(const int16_t *buffer, int32_t delay) const {
    delay = clip(delay, 8 * 65536, 900 * 65536);
    // floor(write-delay), converted to a positive fraction explicitly.
    int integer = delay >> 16;
    unsigned fraction = unsigned(delay) & 65535u;
    unsigned i = (write - unsigned(integer) - (fraction ? 1u : 0u)) & mask;
    unsigned phase = fraction ? ((65536u - fraction) >> 8) : 0u;
    const int16_t *c = &tables::cubic[phase * 4];
    int32_t sum = int32_t(buffer[(i - 1) & mask]) * c[0] + int32_t(buffer[i]) * c[1] +
                  int32_t(buffer[(i + 1) & mask]) * c[2] + int32_t(buffer[(i + 2) & mask]) * c[3];
    return clip(sum >> 13, -16383, 16383);
}

void Voice::updateCompensation() {
    unsigned pos = unsigned(params.string) * 2u, mi = std::min(31u, pos >> 11), mf = pos & 2047u;
    const int16_t *a = &tables::delayComp[mi * 65 + frequencyBin];
    const int16_t *b = a + 65;
    int32_t lo = a[0] + ((int32_t(a[1]) - a[0]) * int32_t(frequencyFraction) >> 16);
    int32_t hi = b[0] + ((int32_t(b[1]) - b[0]) * int32_t(frequencyFraction) >> 16);
    compensation = (lo + ((hi - lo) * int32_t(mf) >> 11)) * 16;
}

int32_t Voice::excitation() {
    if (age >= length && age >= 53) {
        return 0;
    }
    int32_t n = int32_t(random(rng) >> 16) - 32768;
    int32_t alpha = 21299 + mul(pickWarp, pickWarp < 0 ? 13107 : 4915);
    noiseLP += mul(n - noiseLP, alpha);
    int32_t old = noiseLine[(noiseWrite - unsigned(pickPosition)) & 255u];
    noiseLine[noiseWrite] = int16_t(noiseLP);
    noiseWrite = (noiseWrite + 1) & 255u;
    int32_t legacy = 0, mizrab = 0;
    if (age < length) {
        int32_t t = age * excitationStep;
        int32_t window = 4 * mul(t, 32768 - t);
        legacy = mul(mul(noiseLP - old, window), 3932);
    }
    if (age < 53) {
        mizrab = mul(mul(noiseLP - old, tables::pickWindow[age]), 4456) +
                 mul(tables::pickPulse[age], 786);
    }
    ++age;
    int32_t drive = mul(legacy, 21299) + mul(mizrab, 11469);
    drive = mul(drive, pluckStrength);
    // Softer release left; bounded differentiated wire release right.
    driveLP += mul(drive - driveLP, pickWarp < 0 ? 32767 + mul(pickWarp, 24576) : 32767);
    int32_t shape =
        pickWarp < 0 ? driveLP : drive + mul(drive - previousDrive, mul(pickWarp, 16384));
    previousDrive = drive;
    return clip(shape, -8191, 8191);
}

Frame Voice::next(int32_t externalString, int32_t externalHalo) {
    // 1. Smooth control targets and pitch before reading delay lines.
    params.string = approach(params.string, target.string, 8);
    params.jawari = approach(params.jawari, target.jawari, 8);
    params.swell = approach(params.swell, target.swell, 8);
    params.sympathy = approach(params.sympathy, target.sympathy, 8);
    period = approach(period, targetPeriod, 5);
    if ((clock++ & 31u) == 0) {
        updateCompensation();
    }
    damping = approach(damping, dampHold > 0 ? 32767 : 0, 5);
    if (dampHold > 0) {
        --dampHold;
    }

    // 2. One-sided contact shortens the delay; this is not ordinary detuning.
    int32_t jawari = params.jawari;
    int32_t delay = period - compensation;
    int32_t incoming = tap(line, delay);
    int32_t motion = mul(mul(early, 22938), jawari);
    int32_t cross = tap(transverse, period + (period >> 10) * 4 - 94372);
    int32_t normal = clip(incoming + mul(cross, mul(motion, 24576)) * 2, -32767, 32767);
    int32_t contact = std::max<int32_t>(0, normal - 393);
    // Maximum shortening is min(2.2 samples, .9% of one string period).
    int32_t depth = std::min<int32_t>(144179, (period >> 10) * 9);
    int32_t shortening = mul(depth >> 3, mul(jawari, ratio(tables::ratio035, contact))) * 8;
    incoming = tap(line, delay - shortening);
    lp += mul(incoming - lp, 26870 + mul(params.string, 5734));
    const int32_t a = 9830;
    int32_t propagated = clip(-mul(a, lp) + apX + mul(a, apY), -16383, 16383);
    apX = lp;
    apY = propagated;
    int32_t over = std::max<int32_t>(0, propagated - 819);
    propagated -= mul(over, mul(jawari, 393));
    int32_t loss = clip(decayLoss + mul(damping, 6554), 0, 30000);
    int32_t drive = clip(excitation() + clip(externalString, -8191, 8191), -12000, 12000);
    int32_t outgoing = mul(propagated, 32768 - loss) + drive;

    // 3. Propagate transverse displacement and exchange energy between channels.
    crossLP += mul(cross - crossLP, 32276);
    int32_t q = -mul(5898, crossLP) + crossAPX + mul(5898, crossAPY);
    crossAPX = crossLP;
    crossAPY = clip(q, -16383, 16383);
    int32_t other = mul(clip(q, -16383, 16383), 32768 - loss) + mul(drive, 11469);
    int32_t k = mul(mul(motion, 1475), ratio(tables::ratio020, contact));
    int32_t c = 32768 - ((k * k) >> 16);
    int32_t otherOut = mul(c, other) - mul(k, outgoing);
    outgoing = mul(c, outgoing) + mul(k, other);
    if (abs32(outgoing) > 16383 || abs32(otherOut) > 16383) {
        ++clampCount;
    }
    line[write] = int16_t(clip(outgoing, -16383, 16383));
    transverse[write] = int16_t(clip(otherOut, -16383, 16383));
    write = (write + 1) & mask;

    // 4. Bridge radiation excites the persistent sympathetic bank.
    int32_t velocity = incoming - lp;
    int32_t bridge = mul(incoming, 3932) + 7 * velocity;
    bridge += mul(motion, mul(cross, 3932) + clip(40 * (cross - crossLP)));
    pickup += mul(clip(bridge) - pickup, 24576);
    int32_t force = mul(contact, ratio(tables::ratio003, contact));
    contactLP += mul(force - contactLP, 6554);
    int32_t exchange = mul(motion, clip(18 * (contactLP - lastContactLP)));
    lastContactLP = contactLP;
    int32_t bankDrive =
        bankDown.next(clip(pickup + (5 * drive) / 2 + exchange + clip(externalHalo, -8191, 8191)));
    int32_t bankSample = 0;
    if (clock & 1u) {
        bankSample = bank.next(bankDrive, damping);
    }
    int32_t wet = clip(2 * bankUp.next(bankSample));

    // 5. Shape the melody, then mix and limit without clicks.
    // Swell changes the string decay and its secondary rise, not halo tuning.
    int32_t growth = mul(slow - rise, params.swell);
    int32_t bloom = 32768 - mul(params.swell, 21299) + 2 * growth + mul(growth, 13107);
    wet = mul(wet, params.sympathy);
    int32_t melody = mul(pickup, clip(bloom, 0, 49152));
    decayWithRemainder(early, earlyErr, 1553);
    decayWithRemainder(slow, slowErr, 582);
    decayWithRemainder(rise, riseErr, 2796);
    int32_t mixed = melody + (7 * wet) / 5;
    dc += carryStep(mixed - dc, dcRemainder, 9);
    int32_t y = limit(3 * (mixed - dc));
    output += clip(y - output, -2048, 2048);
    return {clip(output), clip(melody), clip(wet), period};
}
} // namespace mtm
