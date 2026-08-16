#!/usr/bin/env python3
"""dspsim.py - numerical check of the DJ filter and the drum voices.

An integer state-variable filter that goes unstable does not merely sound bad:
the state wraps rather than saturating, which is a full-scale square wave
straight into whatever you have patched. That is worth proving cannot happen
BEFORE flashing it, and it is not something the ear can check safely.

Mirrors drums.cpp. If that changes, change this.

Run:  python tools/dspsim.py
"""

import math
import sys

# --- constants, mirroring drums.cpp ---------------------------------------
RESONANCE  = 12000
BYPASS_LO  = 1800
BYPASS_HI  = 2300
STATE_MAX  = 1 << 20
LIMIT      = 2047

FAILURES = []


def check(name, ok, detail=""):
    if ok:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s  %s" % (name, detail))
        FAILURES.append(name)


def cutoff_curve(ratio_q15):
    quad = (ratio_q15 * ratio_q15) >> 15
    cubic = (quad * ratio_q15) >> 15
    mixed = (ratio_q15 * 4000 + quad * 10000 + cubic * 18768) >> 15
    g = 300 + mixed
    return max(150, min(22000, g))


class DjFilter:
    def __init__(self):
        self.g = 0
        self.v1 = 0
        self.v2 = 0
        self.mode = 0

    def set_knob(self, knob):
        if knob < BYPASS_LO:
            self.mode = -1
            self.g = cutoff_curve((knob << 15) // BYPASS_LO)
        elif knob > BYPASS_HI:
            self.mode = 1
            self.g = cutoff_curve(((knob - BYPASS_HI) << 15) // (4095 - BYPASS_HI))
        else:
            self.mode = 0

    def step(self, x):
        if self.mode == 0:
            self.v1 -= self.v1 >> 6
            self.v2 -= self.v2 >> 6
            return x
        hp = x - ((RESONANCE * self.v1) >> 15) - self.v2
        self.v1 += (self.g * hp) >> 15
        lp = self.v2 + ((self.g * self.v1) >> 15)
        self.v2 = lp
        self.v1 = max(-STATE_MAX, min(STATE_MAX, self.v1))
        self.v2 = max(-STATE_MAX, min(STATE_MAX, self.v2))
        return lp if self.mode < 0 else hp


def rms_response(knob, freq, n=8000, fs=48000):
    """RMS of the filter output for a full-scale sine at `freq`."""
    f = DjFilter()
    f.set_knob(knob)
    acc = 0.0
    for i in range(n):
        x = int(2000 * math.sin(2 * math.pi * freq * i / fs))
        y = f.step(x)
        if i > n // 2:                      # let it settle
            acc += float(y) * y
    return math.sqrt(acc / (n // 2))


def test_stability_full_sweep():
    """No knob position, on any input, may drive the state to its clamp.

    Hitting the clamp is not automatically fatal, but a filter that is pinned
    there is self-oscillating at full scale, which is not a filter any more.
    Drive worst-case input: full-scale square wave, which is the richest thing
    the drum bus can produce after soft clipping.
    """
    worst_knob, worst_state = None, 0
    pinned = []
    for knob in range(0, 4096, 64):
        f = DjFilter()
        f.set_knob(knob)
        peak = 0
        for i in range(4000):
            x = LIMIT if (i // 40) % 2 else -LIMIT      # 600Hz square
            f.step(x)
            peak = max(peak, abs(f.v1), abs(f.v2))
        if peak > worst_state:
            worst_state, worst_knob = peak, knob
        if peak >= STATE_MAX:
            pinned.append(knob)

    check("SVF: state never pinned at clamp (square wave, all knobs)",
          not pinned,
          "pinned at knobs %r" % (pinned[:8],))
    print("          worst state %d (%.1f%% of clamp) at knob %d"
          % (worst_state, 100.0 * worst_state / STATE_MAX, worst_knob))


def test_stability_impulse():
    """An impulse must decay, not ring forever or grow."""
    bad = []
    for knob in range(0, 4096, 128):
        if BYPASS_LO <= knob <= BYPASS_HI:
            continue
        f = DjFilter()
        f.set_knob(knob)
        f.step(LIMIT)
        early = max(abs(f.step(0)) for _ in range(200))
        late = max(abs(f.step(0)) for _ in range(3000, 4000))
        if late > early:
            bad.append((knob, early, late))
    check("SVF: impulse response decays at every knob position", not bad,
          "growing at %r" % (bad[:4],))


def test_lowpass_actually_lowpasses():
    """CCW must pass bass and reject treble."""
    lo = rms_response(200, 100)
    hi = rms_response(200, 8000)
    check("SVF: knob 200 is a low-pass (bass > treble)", lo > hi * 4,
          "bass %.0f treble %.0f" % (lo, hi))


def test_highpass_actually_highpasses():
    """CW must pass treble and reject bass."""
    lo = rms_response(3900, 100)
    hi = rms_response(3900, 8000)
    check("SVF: knob 3900 is a high-pass (treble > bass)", hi > lo * 4,
          "bass %.0f treble %.0f" % (lo, hi))


def test_bypass_is_unity():
    """Centre must be exactly the input, not merely close to it."""
    f = DjFilter()
    f.set_knob((BYPASS_LO + BYPASS_HI) // 2)
    xs = [int(1500 * math.sin(i / 7.0)) for i in range(500)]
    ok = all(f.step(x) == x for x in xs)
    check("SVF: centre deadband is bit-exact bypass", ok)


def test_monotonic_opening():
    """Opening the low-pass must let progressively more TOTAL energy through.

    Measured as the sum across a spread of frequencies rather than at a single
    tone, because no single tone tests the right thing:

      - at 1kHz the response legitimately PEAKS then falls, because 1kHz sits
        on the resonant shoulder as the corner sweeps past it. A resonant
        filter is supposed to do that; asserting otherwise would mean deleting
        the resonance that makes this sound like a DJ filter.
      - at 50Hz the response is flat, because 50Hz is already inside the
        passband at every knob position, so the sweep never crosses it.

    Both of those were written as tests here first, and both failed against a
    filter that was in fact correct. Broadband energy is the honest measure of
    "the filter is opening".

    A sparse frequency grid is not enough either: with Q~2.7 the resonant peak
    sweeps
    BETWEEN the sample points, so the total wobbles by tens of percent purely
    from where the peak happens to land. That is a measurement artifact, and it
    also failed against a correct filter. Use a dense log grid so the peak is
    always captured by some bin.
    """
    freqs = [int(60 * (1.25 ** i)) for i in range(22)]     # 60Hz .. ~10kHz
    prev, bad = 0.0, []
    for knob in range(100, BYPASS_LO, 200):
        total = sum(rms_response(knob, f, n=3000) for f in freqs)
        if total < prev * 0.97:
            bad.append(knob)
        prev = total
    check("SVF: low-pass opens monotonically (broadband)", not bad,
          "dips at %r" % (bad,))


def test_passband_is_unity():
    """A wide-open low-pass must pass bass at roughly unity gain.

    If it does not, the DJ filter doubles as an unexpected volume control and
    the drum bus level changes whenever the knob moves.
    """
    f = DjFilter()
    f.set_knob(BYPASS_LO - 1)
    acc, n = 0.0, 8000
    for i in range(n):
        y = f.step(int(2000 * math.sin(2 * math.pi * 50 * i / 48000)))
        if i > n // 2:
            acc += float(y) * y
    out = math.sqrt(acc / (n // 2))
    ref = 2000 / math.sqrt(2)
    check("SVF: open low-pass is ~unity in the passband",
          0.9 < out / ref < 1.15, "gain %.3f" % (out / ref))


def test_knob_travel_is_used():
    """The coefficient must still be moving at the top of each sweep.

    g saturating early wastes real estate on a knob that has none to spare -
    the last chunk of travel would do nothing at all.
    """
    lo_end = cutoff_curve(((BYPASS_LO - 1) << 15) // BYPASS_LO)
    lo_mid = cutoff_curve(((BYPASS_LO * 3 // 4) << 15) // BYPASS_LO)
    check("SVF: low-pass still opening at the top of its travel",
          lo_end > lo_mid, "mid %d end %d" % (lo_mid, lo_end))

    hi_end = cutoff_curve(((4095 - BYPASS_HI) << 15) // (4095 - BYPASS_HI))
    hi_mid = cutoff_curve((((4095 - BYPASS_HI) * 3 // 4) << 15) // (4095 - BYPASS_HI))
    check("SVF: high-pass still opening at the top of its travel",
          hi_end > hi_mid, "mid %d end %d" % (hi_mid, hi_end))


# --- soft clip -------------------------------------------------------------

KNEE = (3 * LIMIT) // 2


def itrunc_div(a, b):
    """C's integer division truncates toward zero; Python's // floors.

    Spelled out because getting this wrong makes the model agree with the C++
    for positive signals and silently diverge for negative ones - which on a
    symmetric waveform is exactly half the samples.
    """
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def soft_clip(x):
    if x > KNEE:
        return LIMIT
    if x < -KNEE:
        return -LIMIT
    return x - itrunc_div(itrunc_div(x * x, KNEE) * x, 3 * KNEE)


def test_softclip_bounded():
    """The clip must never exceed the DAC range, for any input the summed kit
    can produce - including well past its knee."""
    worst = max(abs(soft_clip(x)) for x in range(-80000, 80000, 7))
    check("SoftClip: output always within DAC range", worst <= LIMIT,
          "worst %d" % worst)


def test_softclip_monotonic():
    """A non-monotonic clipper folds the waveform, which sounds like a
    ring-modulator rather than saturation."""
    bad = []
    prev = soft_clip(-80000)
    for x in range(-80000, 80000, 13):
        y = soft_clip(x)
        if y < prev - 1:
            bad.append(x)
        prev = y
    check("SoftClip: monotonic (no fold-back)", not bad, "folds at %r" % (bad[:4],))


def test_softclip_linear_near_zero():
    """Quiet material must pass through essentially untouched, or a single
    soft hit sounds squashed."""
    err = max(abs(soft_clip(x) - x) for x in range(-200, 200))
    check("SoftClip: near-unity for small signals", err <= 3, "err %d" % err)


def main():
    print("NIBBLE DSP checks")
    print()
    print("DJ filter:")
    test_stability_full_sweep()
    test_stability_impulse()
    test_lowpass_actually_lowpasses()
    test_highpass_actually_highpasses()
    test_bypass_is_unity()
    test_monotonic_opening()
    test_passband_is_unity()
    test_knob_travel_is_used()
    print()
    print("Soft clip:")
    test_softclip_bounded()
    test_softclip_monotonic()
    test_softclip_linear_near_zero()
    print()
    if FAILURES:
        print("%d FAILED: %s" % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
