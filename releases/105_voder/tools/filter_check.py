#!/usr/bin/env python3
"""Band geometry, Q15 quantisation, and stability for TRACT8's filter bank.

This is the test that matters most. An IIR filter with quantised
coefficients can do three bad things, and only one of them is obvious:

  1. Sit at the wrong centre frequency (obvious - it sounds wrong).
  2. Have the wrong bandwidth (subtle - the vowels smear together).
  3. Go UNSTABLE (catastrophic - a pole outside the unit circle means the
     output grows without bound and the card screams until reset).

The third is the reason this file exists. voder.cpp stores its biquad
coefficients as Q15 integers, and rounding a1/a2 can push a pole outside
the unit circle for a high-Q filter. At Q=4 and 250 Hz the poles sit at
a radius of about 0.995, which is close enough to the edge to be worth
checking rather than assuming.

This test already earned its keep: the first version of voder.cpp used
Q13, and check 2 below caught the 250 Hz band landing at 239 Hz - a
74-cent error, plus 0.6 dB of level lost - because a1 approaches -2.0 at
low centre frequencies and Q13 has too few bits there. Q15 fixed it.

The coefficients here are computed from the same formulas as VoderInit()
and then quantised the same way, and the resulting response is evaluated
ANALYTICALLY rather than by running the filter. That is deliberate: a test
that ran the C++ filter to check the C++ filter would agree with itself
no matter how wrong both were.

Checks:
  1. Every pole is inside the unit circle (stability).
  2. Each band peaks within 2% of its nominal centre frequency.
  3. Quantisation costs less than 0.5 dB at each band centre.
  4. Q is within 15% of nominal at every band.
  5. Adjacent bands overlap - no dead zones in the spectrum.
  6. Full-scale input with all bands open does not overflow int32.

Run: python tools/filter_check.py
"""

import sys
import math
import numpy as np

# --- transcribed from voder.h / voder.cpp ---------------------------------
FS = 48000
BAND_HZ = [250, 450, 700, 1000, 1400, 1900, 2600, 3800]
Q = 4.0
COEFF_SHIFT = 15          # Q15, per the note in VoderInit()
COEFF_SCALE = 1 << COEFF_SHIFT


def ideal_coeffs(f0):
    """RBJ constant-skirt bandpass. Exactly as VoderInit()."""
    w0 = 2.0 * math.pi * f0 / FS
    alpha = math.sin(w0) / (2.0 * Q)
    a0 = 1.0 + alpha
    return (alpha / a0, (-2.0 * math.cos(w0)) / a0, (1.0 - alpha) / a0)


def quantised_coeffs(f0):
    """Same, rounded to Q15 integers then back to float - what the card runs."""
    b0, a1, a2 = ideal_coeffs(f0)
    qb0 = round(b0 * COEFF_SCALE)
    qa1 = round(a1 * COEFF_SCALE)
    qa2 = round(a2 * COEFF_SCALE)
    return (qb0 / COEFF_SCALE, qa1 / COEFF_SCALE, qa2 / COEFF_SCALE,
            qb0, qa1, qa2)


def response(b0, a1, a2, freqs):
    """|H(z)| at the given frequencies, for y = b0*(x - x2) - a1*y1 - a2*y2.

    Transfer function is  H(z) = b0*(1 - z^-2) / (1 + a1*z^-1 + a2*z^-2).
    """
    w = 2.0 * np.pi * np.asarray(freqs, dtype=float) / FS
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    num = b0 * (1.0 - z2)
    den = 1.0 + a1 * z1 + a2 * z2
    return np.abs(num / den)


def check_stability():
    print("\n1. Pole radii (must be < 1.0 for stability)")
    print("   band     a1        a2      pole radius   margin")
    ok = True
    for f0 in BAND_HZ:
        _, a1, a2, _, qa1, qa2 = quantised_coeffs(f0)
        # Poles of 1 + a1 z^-1 + a2 z^-2, i.e. roots of z^2 + a1 z + a2.
        roots = np.roots([1.0, a1, a2])
        r = float(np.max(np.abs(roots)))
        margin = 1.0 - r
        flag = "" if r < 1.0 else "  <-- UNSTABLE"
        if r >= 1.0:
            ok = False
        print(f"   {f0:5d}  {a1:8.5f}  {a2:8.5f}     {r:.6f}   "
              f"{margin:.6f}{flag}")
    return ok


def check_centres():
    print("\n2. Measured peak vs nominal centre")
    print("   band    peak Hz    error")
    ok = True
    for f0 in BAND_HZ:
        b0, a1, a2, _, _, _ = quantised_coeffs(f0)
        # Search a generous window around the nominal centre.
        lo, hi = f0 * 0.7, f0 * 1.4
        freqs = np.linspace(lo, hi, 4001)
        mag = response(b0, a1, a2, freqs)
        peak = freqs[int(np.argmax(mag))]
        err = abs(peak - f0) / f0
        flag = "" if err < 0.02 else "  <-- OFF"
        if err >= 0.02:
            ok = False
        print(f"   {f0:5d}  {peak:8.1f}   {err*100:5.2f}%{flag}")
    return ok


def check_quantisation():
    print("\n3. Quantisation error at band centre")
    print("   band    ideal dB   quant dB    delta")
    ok = True
    for f0 in BAND_HZ:
        ib0, ia1, ia2 = ideal_coeffs(f0)
        qb0, qa1, qa2, _, _, _ = quantised_coeffs(f0)
        mi = float(response(ib0, ia1, ia2, [f0])[0])
        mq = float(response(qb0, qa1, qa2, [f0])[0])
        di = 20 * math.log10(mi)
        dq = 20 * math.log10(mq)
        delta = abs(dq - di)
        flag = "" if delta < 0.5 else "  <-- COARSE"
        if delta >= 0.5:
            ok = False
        print(f"   {f0:5d}   {di:7.3f}   {dq:7.3f}   {delta:6.3f}{flag}")
    return ok


def measure_q(b0, a1, a2, f0):
    """Measured Q from the -3 dB points either side of the peak."""
    freqs = np.linspace(f0 * 0.4, min(f0 * 2.2, FS / 2 - 1), 20001)
    mag = response(b0, a1, a2, freqs)
    peak_i = int(np.argmax(mag))
    peak = mag[peak_i]
    target = peak / math.sqrt(2.0)

    lo = None
    for i in range(peak_i, 0, -1):
        if mag[i] <= target:
            lo = freqs[i]
            break
    hi = None
    for i in range(peak_i, len(freqs)):
        if mag[i] <= target:
            hi = freqs[i]
            break
    if lo is None or hi is None:
        return None
    return freqs[peak_i] / (hi - lo)


def check_q():
    print("\n4. Measured Q vs nominal (%.1f)" % Q)
    print("   band   measured Q   error")
    ok = True
    for f0 in BAND_HZ:
        b0, a1, a2, _, _, _ = quantised_coeffs(f0)
        q = measure_q(b0, a1, a2, f0)
        if q is None:
            print(f"   {f0:5d}   (no -3dB points found)  <-- FAIL")
            ok = False
            continue
        err = abs(q - Q) / Q
        flag = "" if err < 0.15 else "  <-- OFF"
        if err >= 0.15:
            ok = False
        print(f"   {f0:5d}     {q:7.3f}   {err*100:5.2f}%{flag}")
    return ok


def check_coverage():
    """Adjacent bands must overlap, or there are frequencies no fader reaches."""
    print("\n5. Spectral coverage - crossover level between adjacent bands")
    print("   pair            cross Hz   level dB below peak")
    ok = True
    coeffs = [quantised_coeffs(f)[:3] for f in BAND_HZ]
    for i in range(len(BAND_HZ) - 1):
        f_lo, f_hi = BAND_HZ[i], BAND_HZ[i + 1]
        freqs = np.linspace(f_lo, f_hi, 4001)
        m_lo = response(*coeffs[i], freqs)
        m_hi = response(*coeffs[i + 1], freqs)
        # Where the two responses cross is the weakest point between them.
        both = np.minimum(m_lo, m_hi)
        j = int(np.argmax(both))
        peak_lo = float(np.max(response(*coeffs[i], [f_lo])))
        level_db = 20 * math.log10(both[j] / peak_lo)
        # -12 dB is a generous floor; below that the gap is audible as a
        # notch no fader can fill.
        flag = "" if level_db > -12.0 else "  <-- GAP"
        if level_db <= -12.0:
            ok = False
        print(f"   {f_lo:5d}-{f_hi:<5d}   {freqs[j]:8.1f}   {level_db:7.2f}"
              f"{flag}")
    return ok


def check_headroom():
    """Full-scale excitation, all bands open, must not overflow int32.

    voder.cpp shifts the excitation right by 3 before the bank and the sum
    right by 4 after it. This checks those shifts are actually enough.
    """
    print("\n6. Headroom - worst-case peak gain with all 8 bands open")
    coeffs = [quantised_coeffs(f)[:3] for f in BAND_HZ]
    # The worst case is a sine at whichever frequency maximises the summed
    # response, since band outputs are summed coherently.
    freqs = np.linspace(20, FS / 2 - 1, 20001)
    total = np.zeros_like(freqs)
    for c in coeffs:
        total += response(*c, freqs)
    worst_i = int(np.argmax(total))
    worst_gain = float(total[worst_i])

    excite_max = 32767 >> 3          # after the pre-bank shift
    peak = excite_max * worst_gain   # all gains at 32767/32768 ~ 1.0
    out = peak / (1 << 4)            # after the post-bank shift

    print(f"   worst frequency        {freqs[worst_i]:8.1f} Hz")
    print(f"   summed voltage gain    {worst_gain:8.3f}")
    print(f"   peak before out shift  {peak:8.0f}")
    print(f"   peak at AudioOut       {out:8.0f}   (DAC range +/-2048)")
    print(f"   int32 headroom         {'OK' if peak < 2**31 else 'OVERFLOW'}")

    # Clipping at the DAC is acceptable and expected with every band wide
    # open - that is a deliberate performance state, not a fault, and
    # main.cpp clamps. What must NOT happen is integer overflow, which
    # wraps and sounds like destruction rather than clipping.
    ok = peak < 2**31
    if out > 2048:
        print(f"   note: clips at the DAC by {20*math.log10(out/2048):.1f} dB "
              f"with all bands fully open - clamped in main.cpp, and a "
              f"performance state a player chooses, not a fault.")
    return ok


INT32_MAX = 2**31 - 1
EXCITE_SHIFT = 4      # voder.cpp: excite >>= EXCITE_SHIFT


def check_accumulator_width():
    """The biquad accumulator must not overflow int32."""
    print()
    print("The 32-bit accumulator cannot wrap")
    ok = True

    # WHY THIS MATTERS. The bank used int64 accumulators, and on an M0+
    # with no 64-bit multiply that is three __aeabi_lmul CALLS per band,
    # 24 per sample. CV Out 2 measured the ISR at essentially 100% of its
    # 20.83 us budget; budget_check.py had predicted 44%.
    #
    # The width was never needed - but "never needed" has to be proved,
    # not asserted, because an int32 that wraps does not clip, it INVERTS,
    # and a resonator fed its own inverted output is an explosion rather
    # than a distortion. This drives every band at its own centre
    # frequency, which is the worst case for a resonator, plus squares,
    # noise and DC.
    import random
    import math

    max_excite = (32767 * 2 + 32768) >> EXCITE_SHIFT
    worst = 0
    worst_case = ""

    random.seed(9)
    for f0 in BAND_HZ:
        _, _, _, b0, a1, a2 = quantised_coeffs(f0)
        for shape in ("sine", "square", "noise", "dc"):
            x1 = x2 = y1 = y2 = 0
            for n in range(8000):
                if shape == "sine":
                    x = int(max_excite * math.sin(2 * math.pi * f0 * n / FS))
                elif shape == "square":
                    x = max_excite if (n * f0 // FS) % 2 else -max_excite
                elif shape == "noise":
                    x = random.randint(-max_excite, max_excite)
                else:
                    x = max_excite
                acc = b0 * (x - x2) - a1 * y1 - a2 * y2
                if abs(acc) > worst:
                    worst = abs(acc)
                    worst_case = f"{shape} at {f0} Hz"
                y = -((-acc) >> COEFF_SHIFT) if acc < 0 else acc >> COEFF_SHIFT
                x2, x1 = x1, x
                y2, y1 = y1, y

    margin = INT32_MAX / float(worst)
    good = margin >= 4.0
    if not good:
        ok = False
    print(f"   worst accumulator {worst:,} vs int32 {INT32_MAX:,}")
    print(f"   margin {margin:.1f}x   ({worst_case})   "
          f"{'ok' if good else '<-- TOO CLOSE'}")

    # The algebraic bound too - every term at maximum, same sign at once.
    # No signal can produce it, but if even THAT fits there is nothing to
    # argue about.
    bound = 0
    for f0 in BAND_HZ:
        _, _, _, b0, a1, a2 = quantised_coeffs(f0)
        ymax = max_excite * 2
        bound = max(bound, abs(b0) * 2 * max_excite
                    + abs(a1) * ymax + abs(a2) * ymax)
    good = bound < INT32_MAX
    if not good:
        ok = False
    print(f"   algebraic worst case {bound:,}   "
          f"{'ok - fits with ' + f'{INT32_MAX / bound:.2f}x' if good else '<-- OVERFLOWS'}")
    print("   (an int32 that wraps INVERTS rather than clipping, and a")
    print("    resonator fed its own inverted output explodes - so this")
    print("    is proved, not assumed)")
    return ok


def main():
    print(f"TRACT8 filter bank check: {len(BAND_HZ)} bands, Q={Q}, "
          f"fs={FS}, coefficients Q{COEFF_SHIFT}")

    ok = check_stability()
    ok &= check_centres()
    ok &= check_quantisation()
    ok &= check_q()
    ok &= check_coverage()
    ok &= check_headroom()
    ok &= check_accumulator_width()

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
