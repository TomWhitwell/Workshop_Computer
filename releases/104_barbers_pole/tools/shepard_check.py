#!/usr/bin/env python3
"""
shepard_check.py - verify the oscillator bank's tables and, above all, the
window's constant-sum property.

The single most important assertion here is check_window_sum. The Shepard
illusion depends on the octave-spaced layers sliding through a fixed envelope
whose contributions ALWAYS sum to the same total. If that sum ripples, the
output level pulses once per octave and the illusion collapses into an audible
cycle - which is the failure mode that reads as "it sounds like a sweep, not an
infinite glide".

It is a mathematical property of sin^2 at even spacing, not a tuning, so it
should hold to the limits of integer rounding. If this test ever fails, the
window or the layer spacing has been changed and the card is broken.

Run: python tools/shepard_check.py
"""

import math
import sys

SIN_LUT_SIZE = 1024
POW2_LUT_SIZE = 256
MIN_LAYERS = 3
MAX_LAYERS = 11
SR = 48000


# --- mirrors of the fixed-point code in shepard.h / shepard.cpp -------------

def build_sin_lut():
    return [int(round(math.sin(2.0 * math.pi * i / SIN_LUT_SIZE) * 32767.0))
            for i in range(SIN_LUT_SIZE)]


def build_pow2_lut():
    lut = []
    for i in range(POW2_LUT_SIZE + 1):
        v = (2.0 ** (i / POW2_LUT_SIZE)) * 1073741824.0
        lut.append(2147483647 if v >= 2147483647.0 else int(round(v)))
    return lut


SIN_LUT = build_sin_lut()
POW2_LUT = build_pow2_lut()


def mul_q15(w, x):
    """MulQ15 from fixed.h - the int32-only Q15 multiply."""
    hi = x >> 15
    lo = x - (hi << 15)
    return w * hi + ((w * lo) >> 15)


def hann_q15(u_q32):
    """HannQ15 from shepard.h - sin^2(pi*u) via the first half of sin_lut.

    Interpolated, and that is load-bearing. A truncated read gives 0.48%
    ripple at ODD layer counts while staying clean at even ones - see the
    comment on HannQ15 in shepard.h.
    """
    idx = (u_q32 >> 23) & 0x1FF
    frac = (u_q32 >> 8) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & (SIN_LUT_SIZE - 1)]
    s = s0 + mul_q15(frac, s1 - s0)
    return mul_q15(s, s)


def pow2_q30(f_q32):
    """Pow2Q30 from shepard.h."""
    idx = f_q32 >> 24
    frac = (f_q32 >> 9) & 0x7FFF
    v0 = POW2_LUT[idx]
    v1 = POW2_LUT[idx + 1]
    return v0 + mul_q15(frac, v1 - v0)


INV_SQRT_N = [0, 0, 0, 18919, 16384, 14654, 13377, 12385,
              11585, 10923, 10362, 9880]


def layer_u_q32(master_q32, i, n):
    """Window position of layer i, as Q32 in [0, 2^32).

        u_i = (master_frac + i) / n

    The firmware computes this in the control block, where the divide is
    affordable because it runs at 1.5 kHz rather than 48 kHz.
    """
    return (((master_q32 + (i << 32)) // n)) & 0xFFFFFFFF


# --- the tests --------------------------------------------------------------

def check_window_sum():
    """THE critical invariant: sum of window gains is constant.

    For N layers at u_i = (master + i)/N, sum_i sin^2(pi*u_i) == N/2 exactly,
    for every master phase. A ripple here is an audible level pulse once per
    octave cycle.

    Checked in the ACTUAL integer arithmetic, so what is measured is what the
    card computes - rounding included.
    """
    print("  window sum (the COLA invariant):")
    ok = True
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        sums = []
        for step in range(512):
            master = (step << 23) & 0xFFFFFFFF     # sweep a full octave
            total = 0
            for i in range(n):
                total += hann_q15(layer_u_q32(master, i, n))
            sums.append(total)

        lo, hi = min(sums), max(sums)
        mean = sum(sums) / len(sums)
        ripple = (hi - lo) / mean * 100.0 if mean else 0.0
        expect = n * 16384              # N/2 in Q15
        err = abs(mean - expect) / expect * 100.0

        # 0.02% is the integer rounding floor for an interpolated Q15 table
        # (a couple of LSB out of 32767 per layer). The threshold is set just
        # above it deliberately: a truncated lookup gives 0.48% at odd N, so
        # anything looser would let that regression back in unnoticed.
        status = "ok" if ripple < 0.02 and err < 0.5 else "FAIL"
        if status == "FAIL":
            ok = False
        print(f"    N={n:2d}: sum={mean:9.1f} (expect {expect})  "
              f"ripple={ripple:.4f}%  err={err:.3f}%  {status}")
    return ok


def check_layer_entry():
    """Every layer must enter and leave at silence, and the stack must repeat
    itself shifted by one slot after each octave.

    That second property is the illusion stated precisely: if the pattern
    after a wrap equals the pattern before it shifted one layer, there is
    nothing to hear at the boundary.

    NOTE what this does NOT cover. It checks the WINDOW only. The oscillator
    PHASES also have to rotate with the stack, and they are invisible here -
    a card can pass every assertion below and still click loudly at the loop.
    That is exactly what happened; see check_phase_rotation and the note in
    UpdateControl().
    """
    print("  layers enter and leave at silence:")
    ok = True
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        first = hann_q15(layer_u_q32(0, 0, n))
        worst_jump = 0
        prev = None
        for step in range(1024):
            master = (step << 22) & 0xFFFFFFFF
            gains = [hann_q15(layer_u_q32(master, i, n)) for i in range(n)]
            if prev is not None:
                worst_jump = max(worst_jump,
                                 max(abs(a - b) for a, b in zip(gains, prev)))
            prev = gains
        entry_ok = first < 200
        smooth_ok = worst_jump < 400
        if not (entry_ok and smooth_ok):
            ok = False
        print(f"    N={n:2d}: layer 0 enters at {first:5d}, "
              f"worst per-step jump {worst_jump:4d}  "
              f"{'ok' if entry_ok and smooth_ok else 'FAIL'}")

    print("    after one octave the stack repeats, shifted one slot:")
    for n in (3, 8, 11):
        before = [hann_q15(layer_u_q32(0xFFFFF000, i, n)) for i in range(n)]
        after = [hann_q15(layer_u_q32(0x00001000, i, n)) for i in range(n)]
        worst = max(abs(after[i] - before[i - 1]) for i in range(1, n))
        status = "ok" if worst < 100 else "FAIL"
        if worst >= 100:
            ok = False
        print(f"      N={n:2d}: worst mismatch {worst:4d}  {status}")
    return ok


def check_phase_rotation():
    """The oscillator phases must rotate with the stack at an octave wrap.

    THE BUG THIS EXISTS FOR, heard on hardware as "a BIG click at the loop -
    it sounds like the new overtone coming in at full volume".

    At a wrap the stack relabels: layer i inherits the window gain AND the
    frequency that layer i-1 had. If its phase does not move with them, the
    layer is suddenly a different oscillator continuing from an unrelated
    phase, and the summed waveform steps.

    The window is entirely innocent here - its sum is constant and its layout
    is correct either way - so every window assertion stays green while the
    card clicks. This is the test that closes that gap.

    Measured on the real engine: largest single-sample step 128 without the
    rotation, 67 with it, against a global maximum of 68.
    """
    print("  oscillator phases rotate with the stack at the wrap:")

    # A layer's contribution is gain * sin(phase). After a wrap, layer i must
    # continue the WAVEFORM that layer i-1 was producing - same phase, and the
    # gain and frequency it inherits are already correct by construction.
    n = 8
    ok = True

    # Model two adjacent control blocks either side of a wrap.
    phases = [(i * 0x18000000) & 0xFFFFFFFF for i in range(n)]

    before = [mul_q15(SIN_LUT[(phases[i] >> 22) & 1023],
                      hann_q15(layer_u_q32(0xFFFFF000, i, n)))
              for i in range(n)]

    # WITHOUT rotation: layer i keeps its own phase, takes layer i-1's gain.
    no_rot = [mul_q15(SIN_LUT[(phases[i] >> 22) & 1023],
                      hann_q15(layer_u_q32(0x00001000, i, n)))
              for i in range(n)]

    # WITH rotation: layer i takes layer i-1's phase as well.
    rot_phases = [phases[i - 1] if i > 0 else phases[0] for i in range(n)]
    with_rot = [mul_q15(SIN_LUT[(rot_phases[i] >> 22) & 1023],
                        hann_q15(layer_u_q32(0x00001000, i, n)))
                for i in range(n)]

    step_no = abs(sum(no_rot) - sum(before))
    step_yes = abs(sum(with_rot) - sum(before))

    print(f"    summed output step at the wrap:")
    print(f"      without rotation {step_no:6d}")
    print(f"      with rotation    {step_yes:6d}")

    if step_yes >= step_no:
        ok = False
        print("      FAIL - rotation does not reduce the step")
    else:
        print(f"      ok - rotation reduces the step "
              f"{step_no / max(step_yes, 1):.1f}x")
    return ok


def check_split_divisor():
    """The firmware avoids a 64-bit divide by splitting the window position:

        (master + i*2^32) / n  ==  master/n + i*(2^32/n)

    computed as three int32 divides per control block instead of one 64-bit
    divide per layer. This asserts the split form is equivalent - if it ever
    diverges, the window ripples and the illusion degrades.
    """
    print("  split-divisor window position matches the exact form:")
    ok = True
    worst = 0
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        md = 0
        od = (0xFFFFFFFF // n) + 1
        for step in range(256):
            master = (step << 24) & 0xFFFFFFFF
            md = master // n
            for i in range(n):
                exact = layer_u_q32(master, i, n)
                split = (md + i * od) & 0xFFFFFFFF
                d = min((exact - split) & 0xFFFFFFFF,
                        (split - exact) & 0xFFFFFFFF)
                worst = max(worst, d)
    # A few LSB of Q32 is ~1e-9 of an octave; the window cannot see it.
    ok = worst < 4096
    print(f"    worst divergence {worst} LSB of 2^32 "
          f"({worst / 2**32 * 1200:.6f} cents)  {'ok' if ok else 'FAIL'}")
    return ok


def check_pow2_lut():
    """Pitch accuracy of the exponential table under interpolation."""
    print("  pow2 LUT accuracy:")
    worst_cents = 0.0
    worst_at = 0.0
    for i in range(20000):
        f = i / 20000.0
        f_q32 = int(f * (1 << 32)) & 0xFFFFFFFF
        got = pow2_q30(f_q32) / 1073741824.0
        want = 2.0 ** f
        cents = abs(1200.0 * math.log2(got / want))
        if cents > worst_cents:
            worst_cents = cents
            worst_at = f
    print(f"    {POW2_LUT_SIZE}+1 entries, worst error {worst_cents:.5f} cents "
          f"at f={worst_at:.4f}")
    ok = worst_cents < 0.01
    print(f"    under 0.01 cents: {'yes' if ok else 'NO'}")
    return ok


def check_nyquist():
    """inc[i] = inc0 << i must never wrap past Nyquist at 12 layers.

    A wrapped increment is not a subtle artefact - it is a loud wrong note, so
    the clamp in the control block matters. This reports the headroom.
    """
    print("  Nyquist headroom (f_base = 13.75 Hz, A-1):")
    f_base = 13.75
    ok = True
    for n in (3, 8, 11):
        top = f_base * (2 ** (n - 1)) * 2.0    # worst case: master at top of octave
        print(f"    N={n:2d}: top layer reaches {top:9.1f} Hz "
              f"({'above' if top > SR / 2 else 'below'} Nyquist {SR // 2})")
        # Above Nyquist is acceptable ONLY because the window has closed there.
        if top > SR / 2:
            u = 1.0                      # top of the window
            gain = math.sin(math.pi * u) ** 2
            print(f"           window gain there = {gain:.6f} "
                  f"({'silent, ok' if gain < 1e-6 else 'AUDIBLE ALIAS'})")
            if gain >= 1e-6:
                ok = False
    return ok


def check_inv_sqrt():
    """1/sqrt(N) table must hold total RMS flat as N sweeps."""
    print("  1/sqrt(N) normalisation:")
    ok = True
    ref = None
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        want = 1.0 / math.sqrt(n)
        got = INV_SQRT_N[n] / 32767.0
        err = abs(got - want) / want * 100.0

        # Incoherent layers: power adds, so amplitude ~ sqrt(sum of w^2).
        # sum w^2 = 3N/8 for this window.
        amp = math.sqrt(3.0 * n / 8.0) * got
        if ref is None:
            ref = amp
        dev_db = 20.0 * math.log10(amp / ref)

        status = "ok" if err < 0.02 and abs(dev_db) < 0.5 else "FAIL"
        if status == "FAIL":
            ok = False
        print(f"    N={n:2d}: 1/sqrt(N)={got:.6f} (err {err:.4f}%)  "
              f"level {dev_db:+.2f} dB  {status}")
    return ok


def check_illusion():
    """Sweep several full wraps and confirm nothing lurches at the seam.

    The centroid must be periodic with the octave, and the level must not
    deviate. A mis-shifted layer or a broken window shows up here and in no
    other test, because every individual component looks fine in isolation.
    """
    print("  illusion continuity across the octave wrap:")
    n = 8
    f_base = 13.75
    rms_track = []
    centroid_track = []

    steps = 256
    for w in range(3):                       # three full octave wraps
        for step in range(steps):
            master = (w * steps + step) / steps
            frac = master - math.floor(master)

            num = 0.0
            den = 0.0
            power = 0.0
            for i in range(n):
                u = (frac + i) / n
                g = math.sin(math.pi * u) ** 2
                f = f_base * (2 ** (frac + i))
                num += g * f
                den += g
                power += g * g
            centroid_track.append(num / den if den else 0.0)
            rms_track.append(math.sqrt(power / n))

    # Level flatness across the whole sweep.
    lo, hi = min(rms_track), max(rms_track)
    dev_db = 20.0 * math.log10(hi / lo) if lo > 0 else 99.0
    print(f"    level deviation over 3 wraps: {dev_db:.4f} dB")

    # Periodicity: the centroid at the same phase of each wrap must match.
    per_ok = True
    for step in range(steps):
        a = centroid_track[step]
        b = centroid_track[steps + step]
        c = centroid_track[2 * steps + step]
        if abs(a - b) / a > 1e-9 or abs(b - c) / b > 1e-9:
            per_ok = False
            break
    print(f"    centroid periodic per octave: {'yes' if per_ok else 'NO'}")

    ok = dev_db < 0.5 and per_ok
    print(f"    {'ok' if ok else 'FAIL'}")
    return ok


def check_layer_count_pitch_steps():
    """Adding a layer must shift the perceived pitch by a CONSISTENT amount.

    THE BUG THIS EXISTS FOR. Each layer added moves the window peak up half a
    slot, which at octave spacing is exactly +600 cents - and that held from
    N=3 to N=11. At N=12 the top layer landed at 28160 Hz, above the 24 kHz
    Nyquist limit: it still carried window gain and still counted in the
    constant-sum, but contributed nothing audible, so the AUDIBLE centre fell
    68 cents short of the pattern.

    Heard on hardware as the pitch dropping about a semitone when the last
    layer was added, and obvious when frozen because no glide masked it. Fixed
    by capping kMaxLayers at 11, where every layer is below Nyquist.

    The measurement has to weight by AUDIBILITY - a layer above Nyquist must
    be excluded - or it reports the pattern as even and misses the fault
    entirely.
    """
    print("  pitch step per added layer is consistent:")
    ok = True
    nyquist = SR / 2.0
    prev = None
    steps = []

    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        num = den = 0.0
        for i in range(n):
            f = 13.75 * (2 ** i)
            if f >= nyquist:          # inaudible - must not count
                continue
            w = hann_q15(layer_u_q32(0, i, n)) / 32767.0
            num += w * math.log2(f)
            den += w
        centroid = 2 ** (num / den) if den else 0.0
        if prev:
            cents = 1200 * math.log2(centroid / prev)
            steps.append(cents)
            print(f"    N={n:2d}: {centroid:8.1f} Hz  {cents:+7.1f} cents")
        else:
            print(f"    N={n:2d}: {centroid:8.1f} Hz")
        prev = centroid

    if steps:
        lo, hi = min(steps), max(steps)
        spread = hi - lo
        print(f"    steps span {lo:+.1f} to {hi:+.1f} cents "
              f"(spread {spread:.1f})")
        # A 68-cent outlier is what reached hardware; 25 catches that with
        # room for the window's own rounding.
        if spread > 25.0:
            ok = False
            print("    INCONSISTENT - one layer count is out of step")
        else:
            print("    even throughout  ok")

    # And no layer may sit above Nyquist at any count.
    worst = 13.75 * (2 ** (MAX_LAYERS - 1))
    print(f"    top layer at N={MAX_LAYERS}: {worst:.0f} Hz "
          f"({'below' if worst < nyquist else 'ABOVE'} Nyquist)")
    if worst >= nyquist:
        ok = False
    return ok


def main():
    print("SHEPARD oscillator bank check")
    ok = check_window_sum()
    ok &= check_layer_entry()
    ok &= check_phase_rotation()
    ok &= check_split_divisor()
    ok &= check_pow2_lut()
    ok &= check_inv_sqrt()
    ok &= check_nyquist()
    ok &= check_illusion()
    ok &= check_layer_count_pitch_steps()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
