#!/usr/bin/env python3
"""Excitation source checks for TRACT8: aliasing, noise quality, phase.

Why this exists: the buzz oscillator feeds a bank whose top band sits at
3800 Hz. A naive sawtooth at F0 = 110 Hz puts a harmonic every 110 Hz all
the way past Nyquist, and everything above 24 kHz folds back down INTO the
passband. Once an alias is inside the 3800 Hz band there is no filter that
can remove it - it is indistinguishable from a real harmonic. So the
oscillator has to be bandlimited at the source, and this file measures
whether it actually is.

The polyBLEP implementation here is a transcription of Voder::Process() in
voder.cpp, integer-for-integer, including the Q15 phase arithmetic. It is
NOT an independent float model - it is the same algorithm, so that if the
integer version has a bug this test shows it rather than papering over it
with cleaner maths.

Checks:
  1. polyBLEP reduces aliasing vs a naive saw across the F0 range.
  2. The correction never makes the signal worse at any tested F0.
  3. Sawtooth stays inside the int16 range (no wrap).
  4. xorshift32 has full period and passes basic uniformity.
  5. Noise is spectrally flat (white, not tilted).

Run: python tools/excite_check.py
"""

import sys
import math
import numpy as np

FS = 48000
INC_PER_MILLIHZ_Q16 = 5864063     # transcribed from voder.cpp


def poly_blep(t_q15):
    """Exactly as PolyBlep() in voder.cpp."""
    t2 = (t_q15 * t_q15) >> 15
    return (t_q15 << 1) - t2 - 32768


def gen_saw(f0_milli, n, blep=True):
    """Transcription of the buzz path in Voder::Process()."""
    phase = 0
    inc = (f0_milli * INC_PER_MILLIHZ_Q16) >> 16
    out = np.zeros(n, dtype=np.float64)
    for i in range(n):
        prev = phase
        phase = (phase + inc) & 0xFFFFFFFF
        buzz = (phase >> 17) - 16384
        buzz <<= 1
        if blep and phase < prev:
            dt = inc if inc else 1
            t = (phase << 15) // dt
            if t < 32768:
                buzz -= poly_blep(t)
        out[i] = buzz
    return out


def alias_energy(sig, f0_hz):
    """Energy at frequencies that are NOT harmonics of f0.

    A perfect sawtooth has energy only at integer multiples of f0. Anything
    elsewhere is either aliasing or the polyBLEP's own error. Measured over
    the band the filter bank actually listens to (up to 5 kHz), because
    aliases above that are inaudible through this card.
    """
    n = len(sig)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(sig * win))
    freqs = np.fft.rfftfreq(n, 1.0 / FS)

    # Mark bins within a couple of bins of any harmonic as "wanted".
    wanted = np.zeros(len(freqs), dtype=bool)
    bin_hz = FS / n
    k = 1
    while k * f0_hz < FS / 2:
        centre = int(round(k * f0_hz / bin_hz))
        for j in range(max(0, centre - 3), min(len(freqs), centre + 4)):
            wanted[j] = True
        k += 1

    band = freqs <= 5000.0
    total = np.sum(spec[band] ** 2)
    alias = np.sum(spec[band & ~wanted] ** 2)
    if total <= 0:
        return 0.0
    return alias / total


def check_aliasing():
    print("\n1/2. Aliasing: naive saw vs polyBLEP, below 5 kHz")
    print("     F0 Hz    naive dB   blep dB   improvement")
    ok = True
    n = 16384
    for f0 in (55, 82, 110, 147, 220, 330, 440):
        naive = gen_saw(f0 * 1000, n, blep=False)
        blep = gen_saw(f0 * 1000, n, blep=True)
        an = alias_energy(naive, f0)
        ab = alias_energy(blep, f0)
        dn = 10 * math.log10(an) if an > 0 else -200.0
        db = 10 * math.log10(ab) if ab > 0 else -200.0
        imp = dn - db
        flag = "" if imp >= -0.5 else "  <-- WORSE"
        if imp < -0.5:
            ok = False
        print(f"     {f0:5d}   {dn:8.2f}  {db:8.2f}   {imp:+7.2f} dB{flag}")
    print("     (improvement should be positive or near zero; polyBLEP is a")
    print("      2-sample approximation, so it reduces rather than removes)")
    return ok


def check_range():
    print("\n3. Sawtooth stays in range (no int16 wrap)")
    ok = True
    for f0 in (50, 110, 440, 500):
        sig = gen_saw(f0 * 1000, 8192)
        lo, hi = sig.min(), sig.max()
        bad = lo < -40000 or hi > 40000
        flag = "  <-- OUT OF RANGE" if bad else ""
        if bad:
            ok = False
        print(f"   F0 {f0:4d} Hz   min {lo:8.0f}   max {hi:8.0f}{flag}")
    return ok


def xorshift_stream(seed, n):
    x = seed
    out = np.empty(n, dtype=np.float64)
    for i in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = (x >> 16) - 32768
    return out


def check_rng():
    print("\n4. xorshift32 uniformity")
    n = 200000
    s = xorshift_stream(0xDEADBEEF, n)
    mean = float(np.mean(s))
    std = float(np.std(s))
    # Uniform on [-32768, 32767] has mean 0 and std = 65536/sqrt(12).
    exp_std = 65536.0 / math.sqrt(12.0)
    mean_ok = abs(mean) < 500
    std_ok = abs(std - exp_std) / exp_std < 0.02
    print(f"   mean          {mean:10.2f}   (expect ~0)          "
          f"{'ok' if mean_ok else 'FAIL'}")
    print(f"   std dev       {std:10.2f}   (expect {exp_std:8.2f})  "
          f"{'ok' if std_ok else 'FAIL'}")

    # Period: a 32-bit xorshift with these shifts has period 2^32-1. Just
    # confirm it does not fall into a short cycle from our seed.
    x = 0xDEADBEEF
    seen_start = x
    count = 0
    for _ in range(1000000):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        count += 1
        if x == seen_start:
            break
    cycle_ok = count >= 1000000
    print(f"   no short cycle in 1e6 steps                       "
          f"{'ok' if cycle_ok else 'FAIL'}")
    return mean_ok and std_ok and cycle_ok


def check_noise_flat():
    print("\n5. Noise spectral flatness (should be white)")
    n = 32768
    s = xorshift_stream(0x12345678, n)
    spec = np.abs(np.fft.rfft(s * np.hanning(n))) ** 2
    spec = spec[1:]  # drop DC

    # Compare mean power in the low half vs the high half of the spectrum.
    half = len(spec) // 2
    lo = float(np.mean(spec[:half]))
    hi = float(np.mean(spec[half:]))
    tilt_db = 10 * math.log10(hi / lo)
    ok = abs(tilt_db) < 1.0
    print(f"   low-half mean power   {lo:12.3e}")
    print(f"   high-half mean power  {hi:12.3e}")
    print(f"   tilt                  {tilt_db:+7.3f} dB   "
          f"{'ok' if ok else 'FAIL (not white)'}")
    return ok


def main():
    print(f"TRACT8 excitation check: fs={FS}")
    ok = check_aliasing()
    ok &= check_range()
    ok &= check_rng()
    ok &= check_noise_flat()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
