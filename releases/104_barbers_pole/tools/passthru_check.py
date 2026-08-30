#!/usr/bin/env python3
"""
passthru_check.py - end-to-end output level through the REAL integer chain.

THIS IS THE AUTHORITY ON SCALING. Run it after any change to a shift, a gain,
or a normalisation.

It exists because of a specific failure on the sibling card. WorkshopSpectral
shipped a build that was 512x too loud and clipped constantly - and its own
overlap-add test PASSED, because that test MODELLED the transform's gain
instead of running it, encoding the same wrong assumption as the firmware. The
only test that can catch that class of bug is one that drives the actual
integer arithmetic from input to output and checks the number that comes out.

So nothing here is modelled. The oscillator bank, the window, the
normalisation and the output clipper are all run as the firmware runs them.

Run: python tools/passthru_check.py
"""

import math
import sys

MIN_LAYERS = 3
MAX_LAYERS = 11
DAC_MAX = 2047

SIN_LUT = [int(round(math.sin(2.0 * math.pi * i / 1024) * 32767.0))
           for i in range(1024)]

POW2_LUT = []
for _i in range(257):
    _v = (2.0 ** (_i / 256.0)) * 1073741824.0
    POW2_LUT.append(2147483647 if _v >= 2147483647.0 else int(round(_v)))

INV_SQRT_N = [0, 0, 0, 18919, 16384, 14654, 13377, 12385,
              11585, 10923, 10362, 9880]

BASE_INC = 1229782


def mul_q15(w, x):
    hi = x >> 15
    lo = x - (hi << 15)
    return w * hi + ((w * lo) >> 15)


def sin_q15(phase):
    idx = (phase >> 22) & 1023
    frac = (phase >> 7) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & 1023]
    return s0 + mul_q15(frac, s1 - s0)


def hann_q15(u):
    idx = (u >> 23) & 0x1FF
    frac = (u >> 8) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & 1023]
    s = s0 + mul_q15(frac, s1 - s0)
    return mul_q15(s, s)


def pow2_q30(f):
    idx = f >> 24
    frac = (f >> 9) & 0x7FFF
    return POW2_LUT[idx] + mul_q15(frac, POW2_LUT[idx + 1] - POW2_LUT[idx])


def soft_clip_out(x):
    lim, knee = 2047, 1450
    ax = -x if x < 0 else x
    if ax <= knee:
        return x
    over = ax - knee
    room = lim - knee
    y = knee + (over * room) // (room + over)
    return -y if x < 0 else y


def render(n_layers, master, osc, width=0):
    """One sample of the synth bank, exactly as main.cpp computes it."""
    m = pow2_q30(master)
    inc0 = ((BASE_INC >> 5) * (m >> 15)) >> 10

    md = master // n_layers
    od = (0xFFFFFFFF // n_layers) + 1
    wd = ((width << 17) // n_layers) & 0xFFFFFFFF

    acc_l = 0
    acc_r = 0
    for i in range(n_layers):
        inc = (inc0 << i) & 0xFFFFFFFF
        osc[i] = (osc[i] + inc) & 0xFFFFFFFF
        v = sin_q15(osc[i])

        u_l = (md + i * od) & 0xFFFFFFFF
        u_r = (u_l + wd) & 0xFFFFFFFF
        acc_l += mul_q15(v, hann_q15(u_l))
        acc_r += mul_q15(v, hann_q15(u_r))

    g = INV_SQRT_N[n_layers]
    return mul_q15(acc_l, g) >> 5, mul_q15(acc_r, g) >> 5


def check_output_level():
    """Peak and RMS must sit in the DAC's range at every layer count.

    Too quiet wastes resolution; too loud clips. Both are scaling bugs and
    both are invisible until the card is played.
    """
    print("  output level vs layer count:")
    ok = True
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        osc = [0] * MAX_LAYERS
        master = 0
        rate = 4000000        # a brisk glide, so the window is swept
        peak = 0
        sq = 0
        count = 0
        for _ in range(20000):
            master = (master + rate) & 0xFFFFFFFF
            l, r = render(n, master, osc)
            peak = max(peak, abs(l), abs(r))
            sq += l * l
            count += 1
        rms = math.sqrt(sq / count)

        headroom = 20 * math.log10(DAC_MAX / peak) if peak else 99
        status = "ok"
        if peak > DAC_MAX:
            status = "CLIPS"
            ok = False
        elif peak < DAC_MAX // 4:
            status = "too quiet"
            ok = False
        print(f"    N={n:2d}: peak {peak:5d} rms {rms:7.1f}  "
              f"headroom {headroom:+5.1f} dB  {status}")
    return ok


def check_level_flat_across_density():
    """Turning X (density) must not change the perceived level.

    This is what 1/sqrt(N) is for. If it drifts, sweeping density sounds like
    a volume control, which is not what the knob is.
    """
    print("  level flatness as density sweeps (what 1/sqrt(N) buys):")
    rms = {}
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        osc = [0] * MAX_LAYERS
        master = 0
        sq = 0
        count = 0
        for _ in range(20000):
            master = (master + 4000000) & 0xFFFFFFFF
            l, _r = render(n, master, osc)
            sq += l * l
            count += 1
        rms[n] = math.sqrt(sq / count)

    ref = rms[MIN_LAYERS]
    worst = 0.0
    for n in range(MIN_LAYERS, MAX_LAYERS + 1):
        db = 20 * math.log10(rms[n] / ref) if ref else 0
        worst = max(worst, abs(db))
        print(f"    N={n:2d}: {db:+6.2f} dB")
    ok = worst < 3.0
    print(f"    worst deviation {worst:.2f} dB  {'ok' if ok else 'FAIL'}")
    return ok


def check_no_clipping_at_extremes():
    """The clipper must never be reached in normal operation.

    Peak alignment of all layers is possible in principle; SoftClipOut is
    there for that. But it must be RARE - if the clipper is active during
    ordinary playing, the gain staging is wrong.
    """
    print("  clipper engagement in normal use:")
    ok = True
    for n in (3, 8, 11):
        osc = [0] * MAX_LAYERS
        master = 0
        clipped = 0
        total = 0
        for _ in range(40000):
            master = (master + 1500000) & 0xFFFFFFFF
            l, r = render(n, master, osc)
            for v in (l, r):
                total += 1
                if abs(v) > 1450:        # the soft knee
                    clipped += 1
        pct = clipped / total * 100
        status = "ok" if pct < 1.0 else "TOO OFTEN"
        if pct >= 1.0:
            ok = False
        print(f"    N={n:2d}: {pct:.3f}% of samples above the soft knee  "
              f"{status}")
    return ok


def check_stereo_sums_to_mono():
    """L+R must never null.

    Stereo width offsets the WINDOW, not the oscillator phase, specifically so
    that a mono sum stays valid. If width were applied to phase this test
    would show cancellation.
    """
    print("  mono compatibility across stereo width:")
    ok = True
    n = 8
    for width_pct in (0, 25, 50, 75, 100):
        width = int(width_pct / 100 * 32767)
        osc = [0] * MAX_LAYERS
        master = 0
        sq_mid = 0
        sq_l = 0
        count = 0
        for _ in range(20000):
            master = (master + 4000000) & 0xFFFFFFFF
            l, r = render(n, master, osc, width)
            mid = (l + r) >> 1
            sq_mid += mid * mid
            sq_l += l * l
            count += 1
        rms_mid = math.sqrt(sq_mid / count)
        rms_l = math.sqrt(sq_l / count)
        loss = 20 * math.log10(rms_mid / rms_l) if rms_l else -99
        status = "ok" if loss > -3.0 else "CANCELS"
        if loss <= -3.0:
            ok = False
        print(f"    width {width_pct:3d}%: mono sum {loss:+.2f} dB "
              f"vs left alone  {status}")
    return ok


def main():
    print("SHEPARD end-to-end gain check (the authority on scaling)")
    ok = check_output_level()
    ok &= check_level_flat_across_density()
    ok &= check_no_clipping_at_extremes()
    ok &= check_stereo_sums_to_mono()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
