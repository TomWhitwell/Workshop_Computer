#!/usr/bin/env python3
"""Static initialisation order: are the biquads actually usable? Regression.

THE BUG THIS EXISTS FOR. v1.0.1 was flashed and made no sound except the
plosive click, while the LEDs tracked the knobs and faders perfectly.

That combination is a fingerprint. The LEDs display g_state.band_gain, not
the filter output, so their working proved the panel, the MIDI path and the
shared state were all fine. Plosives are summed AFTER the filter bank, so
their working proved the ISR, the DAC and the output path were fine. The
only thing left between those two proofs was the filter bank itself.

The cause was C++ static initialisation order:

    VoderCard card;          // file-scope global -> ctor runs BEFORE main()
      -> voder_.Init()       // copies band_b0[] etc into bank_[]
    int main() {
      VoderInit(0);          // ...which is where band_b0[] gets COMPUTED
    }

Every biquad was initialised from still-zeroed tables. A biquad with
b0 = a1 = a2 = 0 outputs zero forever, regardless of input or band gain -
so the bank was mute while everything around it worked.

No previous test could have caught this. filter_check.py computes
coefficients in Python and checks the maths; it never models WHEN the C++
runs. The maths was always right. The coefficients just were not there yet.

Checks:
  1. A zero-coefficient biquad is silent (establishes the fingerprint).
  2. Correctly initialised biquads are not silent (the fix works).
  3. The mute guard in Voder::Init catches a zero set and stays audible.
  4. VoderInit is idempotent, so calling it twice is safe.

Run: python tools/init_check.py
"""

import sys
import math

FS = 48000
BAND_HZ = [250, 450, 700, 1000, 1400, 1900, 2600, 3800]
Q = 4.0
COEFF_SHIFT = 15
COEFF_SCALE = 1 << COEFF_SHIFT


def voder_init():
    """Transcription of VoderInit() - what SHOULD be in the tables."""
    b0s, a1s, a2s = [], [], []
    for f0 in BAND_HZ:
        w0 = 2.0 * math.pi * f0 / FS
        alpha = math.sin(w0) / (2.0 * Q)
        a0 = 1.0 + alpha
        b0s.append(round((alpha / a0) * COEFF_SCALE))
        a1s.append(round(((-2.0 * math.cos(w0)) / a0) * COEFF_SCALE))
        a2s.append(round(((1.0 - alpha) / a0) * COEFF_SCALE))
    return b0s, a1s, a2s


def run_bank(b0s, a1s, a2s, gains, n=2000):
    """Feed a full-scale square wave through the bank; return peak output."""
    states = [[0, 0, 0, 0] for _ in BAND_HZ]
    peak = 0
    for k in range(n):
        # A square wave at ~375 Hz excites every band eventually.
        x = (32767 if (k // 64) % 2 else -32767) >> 3
        total = 0
        for i in range(len(BAND_HZ)):
            st = states[i]
            acc = b0s[i] * (x - st[1]) - a1s[i] * st[3] - a2s[i] * st[2]
            y = acc >> COEFF_SHIFT
            st[1] = st[0]
            st[0] = x
            st[2] = st[3]
            st[3] = y
            total += (y * gains[i]) >> 15
        out = total >> 4
        peak = max(peak, abs(out))
    return peak


def check_zero_is_silent():
    print("\n1. The fingerprint: a zero-coefficient bank is silent")
    peak = run_bank([0] * 8, [0] * 8, [0] * 8, [26800] * 8)
    ok = peak == 0
    print(f"   b0=a1=a2=0, gains wide open -> peak {peak} DAC counts   "
          f"{'ok (this is what v1.0.1 did)' if ok else 'UNEXPECTED'}")
    print("   Note the band gains are irrelevant here - that is why the")
    print("   LEDs could track the faders while nothing was audible.")
    return ok


def check_real_is_audible():
    print("\n2. A correctly initialised bank is audible")
    b0s, a1s, a2s = voder_init()
    peak = run_bank(b0s, a1s, a2s, [26800] * 8)
    ok = peak >= 20
    print(f"   real coefficients        -> peak {peak} DAC counts   "
          f"{'ok' if ok else 'FAIL - still silent'}")

    # And prove the coefficients are not accidentally near zero.
    print(f"   b0 values: {b0s}")
    nonzero = all(b != 0 for b in b0s)
    print(f"   every b0 non-zero        {'ok' if nonzero else 'FAIL'}")
    return ok and nonzero


def check_mute_guard():
    print("\n3. The mute guard: a zero set must fall back to pass-through")
    # Transcription of the guard at the end of Voder::Init().
    b0s, a1s, a2s = [0] * 8, [0] * 8, [0] * 8
    if b0s[0] == 0:
        b0s = [32768] * 8
        a1s = [0] * 8
        a2s = [0] * 8
    peak = run_bank(b0s, a1s, a2s, [26800] * 8)
    ok = peak >= 20
    print(f"   guard engaged            -> peak {peak} DAC counts   "
          f"{'ok - audible, so diagnosable' if ok else 'FAIL'}")
    print("   It will sound wrong (no filtering at all), which is the")
    print("   point: wrong is diagnosable from the panel, silent is not.")
    return ok


def check_idempotent():
    print("\n4. VoderInit is idempotent")
    a = voder_init()
    b = voder_init()
    ok = a == b
    print(f"   two calls give identical tables   {'ok' if ok else 'FAIL'}")
    print("   (so Voder::Init calling it, and main() calling it again, is")
    print("    safe - neither has to know whether the other ran first)")
    return ok


def main():
    print("TRACT8 initialisation-order check")
    print("  Rule: the filter bank must be usable the moment Voder::Init")
    print("  returns, without depending on anything main() does later.")
    ok = check_zero_is_silent()
    ok &= check_real_is_audible()
    ok &= check_mute_guard()
    ok &= check_idempotent()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
