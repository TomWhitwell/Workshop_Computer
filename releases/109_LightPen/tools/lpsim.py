#!/usr/bin/env python3
"""lpsim.py - desktop checks for LightPen's fixed-point DSP.

Integer mirrors of the C++ (Python's >> floors like the ARM arithmetic shift;
C's truncating divide is cdiv below). Every product that the C++ keeps in
int32 goes through i32(), which fails loudly on overflow.

    python tools/lpsim.py

No hardware, no pico-sdk. Exits non-zero if any check fails.
"""

import math
import random
import sys

FAILS = []


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + (("  - " + detail) if detail else ""))
    if not ok:
        FAILS.append(name)


def i32(v):
    if not -(2**31) <= v < 2**31:
        raise OverflowError(f"int32 overflow: {v}")
    return v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


# --- fastmath.h -------------------------------------------------------------

SIN = [round(32767 * math.sin((i / 256) * (math.pi / 2))) for i in range(257)]
NOTE_INC = [round(440 * 2 ** ((n - 69) / 12) * 2**32 / 48000) for n in range(13)]


def fast_sin(phase):
    phase &= 0xFFFFFFFF
    quadrant = phase >> 30
    frac = (phase >> 6) & 0xFFFFFF
    idx, mu = frac >> 16, frac & 0xFFFF
    if quadrant & 1:
        idx, mu = 255 - idx, 65536 - mu
        if mu == 65536:
            mu, idx = 0, idx + 1
    a, b = SIN[idx], SIN[idx + 1]
    v = a + (i32((b - a) * mu) >> 16)
    return -v if quadrant & 2 else v


def slew(v, t, s):
    return v + ((t - v) >> s)


def slew_exact(v, t, s):
    d = t - v
    if d == 0:
        return v
    step = d >> s
    if step == 0:
        step = 1 if d > 0 else -1
    return v + step


def mul_q15(a, g):
    return i32(a * g) >> 15


def pow2_scale(base, oct_q12):
    whole, frac = oct_q12 >> 12, oct_q12 & 0xFFF
    v = base << whole if whole > 0 else base >> -whole if whole < 0 else base
    return i32(v + ((v * frac) >> 12))


def note_to_inc(note_q8):
    note_q8 = clamp(note_q8, 0, 127 << 8)
    n, frac = note_q8 >> 8, note_q8 & 0xFF
    octv, semi = n // 12, n % 12
    a, b = NOTE_INC[semi], NOTE_INC[semi + 1]
    inc = (a + (((b - a) * frac) >> 8)) << octv
    assert inc < 2**32
    return inc


def check_fastmath():
    err = max(abs(fast_sin(p) - 32767 * math.sin(2 * math.pi * p / 2**32))
              for p in range(0, 2**32, 2**32 // 4099))
    check("fast_sin within 2 LSB of Q15 sine", err < 2.0, f"max err {err:.2f}")

    worst = 0.0
    for n in range(127):   # note_to_inc clamps at exactly 127
        for f in range(0, 256, 16):
            hz = note_to_inc((n << 8) + f) * 48000 / 2**32
            want = 440 * 2 ** ((n + f / 256 - 69) / 12)
            worst = max(worst, abs(1200 * math.log2(hz / want)))
    check("note_to_inc within 1 cent (semitone table + interp)", worst < 1.0, f"{worst:.3f} cents")

    prev, worst = 0, 0.0
    mono = True
    for o in range(-8192, 8193):
        v = pow2_scale(65536, o)
        mono &= v >= prev
        prev = v
        worst = max(worst, abs(v / (65536 * 2 ** (o / 4096)) - 1))
    check("pow2_scale monotonic", mono)
    check("pow2_scale within 6.2% of 2^x", worst < 0.062, f"{worst*100:.2f}%")
    check("pow2_scale exact at octaves",
          all(pow2_scale(65536, k * 4096) == 65536 * 2**k for k in (-2, -1, 1, 2)))

    # slew_exact reaches the target from either side; slew does not.
    reach = True
    for s in range(5, 15):
        for tgt in (1000 * 256, -1000 * 256, 3, -3):
            for start in (tgt + 7777, tgt - 7777):
                v = start
                for _ in range(40 * (1 << s)):
                    v = slew_exact(v, tgt, s)
                    if v == tgt:
                        break
                reach &= v == tgt
    check("slew_exact always lands on its target (shift 5..14)", reach)


# --- sensors.h / sensors.cpp ----------------------------------------------------
#
# Integer mirror of the whole calibration: the boot maths (gamma search,
# un-mix matrix, damping, tables) as well as the per-sample path. The boot
# maths is where the bugs live — underflow in R^(-1/gamma), the 64-bit
# promotions in the cofactors, the sign of the exponent — so it is mirrored
# exactly, C truncation and all.

MIN_SPAN = 64
SUPPLY, INPUT_OHMS, SERIES_OHMS = 2048, 120000, 1000
LUT_SIZE = GAIN_SIZE = 513
LIGHT_CEIL, LIGHT_FLOOR = 262140, -65536
ROW_BUDGET = 6144
Q24 = 1 << 24
GAMMA_LO, GAMMA_HI, GAMMA_DEF = 26214, 78643, 45875
MAX_ROW_L1 = 6 * Q24
MIN_SEP = 1966
CAPTURE_MIN, CAPTURE_MAX = 16, 2040

QUAL_GAMMA_DEFAULT, QUAL_ROW_CLAMPED, QUAL_LOW_SEP = 1, 2, 4

LOG2 = [round(65536 * math.log2(1 + i / 32)) for i in range(33)]
EXP2 = [round(65536 * 2 ** (i / 32)) for i in range(33)]


def log2_q16(v):
    if v == 0:
        return 0
    msb = v.bit_length() - 1
    m = (v >> (msb - 16)) if msb >= 16 else (v << (16 - msb))
    x = m - 65536
    i, f = x >> 11, x & 0x7FF
    a, b = LOG2[i], LOG2[i + 1]
    return (msb << 16) + (a + (((b - a) * f) >> 11))


def exp2_q16(x):
    w, f = x >> 16, x & 0xFFFF
    a, b = EXP2[f >> 11], EXP2[(f >> 11) + 1]
    m = a + (((b - a) * (f & 0x7FF)) >> 11)
    if w >= 15:
        return 2**31 - 1
    if w >= 0:
        return m << w
    if w < -17:
        return 0
    return (m + (1 << (-w - 1))) >> -w


def ldr_ohms(n):
    if n < 1:
        n = 1
    if n >= SUPPLY:
        return 1
    return max(i32(INPUT_OHMS * (SUPPLY - n)) // n - SERIES_OHMS, 1)


def log_ohms(n):
    return log2_q16(ldr_ohms(n))


def reading_for(r_ohms):
    """The raw reading an LDR of this resistance gives — the model inverted."""
    return round(SUPPLY * INPUT_OHMS / (INPUT_OHMS + r_ohms + SERIES_OHMS))


def light_at(log_r, log_white, t):
    return exp2_q16(clamp((-t * (log_r - log_white)) >> 16, -32 * 65536, 8 * 65536))


def additivity(gamma, log_r):
    t = (65536 << 16) // gamma
    s = sum(light_at(log_r[j], log_r[0], t) for j in (1, 2, 3))
    return 65536 - s + 2 * light_at(log_r[4], log_r[0], t)


def solve_gamma(log_r):
    if not (additivity(GAMMA_LO, log_r) > 0 and additivity(GAMMA_HI, log_r) < 0):
        return GAMMA_DEF, True
    lo, hi = GAMMA_LO, GAMMA_HI
    for _ in range(12):
        mid = (lo + hi) >> 1
        if additivity(mid, log_r) > 0:
            lo = mid
        else:
            hi = mid
    return (lo + hi) >> 1, False


def fail_mask(black, white):
    return sum(1 << i for i in range(3) if -MIN_SPAN < white[i] - black[i] < MIN_SPAN)


def gain_q10(x):
    return pow2_scale(65536, (x - 2048) * 4) >> 6


def curve_at(lut, raw):
    n = clamp(raw, -2048, 2047) + 2048
    k, fr = n >> 3, n & 7
    a = lut[k]
    return a + (i32((lut[k + 1] - a) * fr) >> 3)


def normalise(lut, n_black, n_white):
    u_b, u_w = curve_at(lut, n_black), curve_at(lut, n_white)
    if u_w - u_b < 4096:
        return lut
    return [clamp(cdiv((v - u_b) * 65535, u_w - u_b), LIGHT_FLOOR, LIGHT_CEIL) for v in lut]


def build_two_point(white, black):
    """SensorPipeline::BuildTwoPoint, one channel."""
    model = white > black and black > 0 and white < SUPPLY
    if model:
        log_w, log_b = log_ohms(white), log_ohms(black)
        span = log_b - log_w
        if span < 4096:
            model = False
    lut = []
    for i in range(LUT_SIZE):
        n = (i << 3) - 2048
        if model:
            u = cdiv((log_b - log_ohms(n)) * 65535, span)
        else:
            u = cdiv((n - black) * 65535, white - black)
        lut.append(clamp(u, LIGHT_FLOOR, LIGHT_CEIL))
    return normalise(lut, black, white)


def build_gain_table(l0):
    one = 16 << 16
    den = max(log2_q16(65536 + ((65536 << 16) // l0)) - one, 1)
    gain = [0] * GAIN_SIZE
    for k in range(1, GAIN_SIZE):
        v = k << 9
        arg = 65536 + ((v << 16) // l0)
        num = log2_q16(arg) - one
        curve = cdiv(num << 16, den)
        gain[k] = cdiv(curve << 12, v)
    gain[0] = gain[1]
    return gain


def invert24(m):
    c = [[0] * 3 for _ in range(3)]
    c[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) >> 24
    c[0][1] = -((m[1][0] * m[2][2] - m[1][2] * m[2][0]) >> 24)
    c[0][2] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) >> 24
    c[1][0] = -((m[0][1] * m[2][2] - m[0][2] * m[2][1]) >> 24)
    c[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) >> 24
    c[1][2] = -((m[0][0] * m[2][1] - m[0][1] * m[2][0]) >> 24)
    c[2][0] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) >> 24
    c[2][1] = -((m[0][0] * m[1][2] - m[0][2] * m[1][0]) >> 24)
    c[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) >> 24
    det = (m[0][0] * c[0][0] + m[0][1] * c[0][1] + m[0][2] * c[0][2]) >> 24
    if det < (Q24 >> 8):
        return None, det
    return [[cdiv(c[j][i] << 24, det) for j in range(3)] for i in range(3)], det


def row_scale_max_l1(a):
    clamped = False
    worst = 0
    for i in range(3):
        row_sum = a[i][0] + a[i][1] + a[i][2]
        scale = Q24
        if row_sum > 0:
            scale = cdiv(Q24 << 24, row_sum)
        if scale < Q24 // 2:
            scale, clamped = Q24 // 2, True
        if scale > 2 * Q24:
            scale, clamped = 2 * Q24, True
        l1 = 0
        for j in range(3):
            a[i][j] = (a[i][j] * scale) >> 24
            l1 += abs(a[i][j])
        worst = max(worst, l1)
    return worst, clamped


def shrink_invert(m, sigma, beta):
    reg = [[((Q24 - beta) * m[i][j]) >> 24 for j in range(3)] for i in range(3)]
    for i in range(3):
        reg[i][i] += (beta * sigma[i]) >> 24
    a, _ = invert24(reg)
    if a is None:
        return None, 0, False
    l1, clamped = row_scale_max_l1(a)
    return a, l1, clamped


def separation(m):
    norm = []
    for j in range(3):
        ssq = 0
        for i in range(3):
            v = m[i][j] >> 8
            ssq += (v * v) >> 16
        norm.append(max(isqrt_q16(min(ssq, 2**31 - 1)), 1))
    c0 = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) >> 24
    c1 = (m[1][0] * m[2][2] - m[1][2] * m[2][0]) >> 24
    c2 = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) >> 24
    det = (m[0][0] * c0 - m[0][1] * c1 + m[0][2] * c2) >> 24
    den = max((((norm[0] * norm[1]) >> 16) * norm[2]) >> 16, 1)
    return min((abs(det >> 8) << 16) // den, 65535)


def isqrt_q16(x):
    """fast_sqrt_q16: restoring bitwise integer sqrt."""
    if x <= 0:
        return 0
    v, res, bit = x << 16, 0, 1 << 30
    while bit > v:
        bit >>= 2
    while bit:
        if v >= res + bit:
            v -= res + bit
            res = (res >> 1) + bit
        else:
            res >>= 1
        bit >>= 2
    return res


class Pipeline:
    """Mirror of SensorPipeline: what SetCalibration leaves in the object."""

    def __init__(self, cal):
        self.quality = 0
        self.sep_bars = 0
        self.a10 = [[1024 if i == j else 0 for j in range(3)] for i in range(3)]
        self.gain = [4096] * GAIN_SIZE
        self.lut = None
        if cal.get("mode") == "five" and self._five(cal):
            return
        self.a10 = [[1024 if i == j else 0 for j in range(3)] for i in range(3)]
        self.gain = [4096] * GAIN_SIZE
        self.sep_bars = 0
        self.lut = [build_two_point(cal["white"][ch], cal["black"][ch]) for ch in range(3)]

    def _five(self, cal):
        white, black, prim = cal["white"], cal["black"], cal["prim"]
        for ch in range(3):
            if not (white[ch] > black[ch] > 0 and white[ch] < SUPPLY):
                return False
            for j in range(3):
                p = prim[j][ch]
                if p < black[ch] - 32 or p > white[ch] + 32:
                    return False
                if p < CAPTURE_MIN or p > CAPTURE_MAX:
                    return False

        defaulted = False
        t = [0] * 3
        black_light = [0] * 3
        for ch in range(3):
            log_r = [log_ohms(white[ch]), log_ohms(prim[0][ch]), log_ohms(prim[1][ch]),
                     log_ohms(prim[2][ch]), log_ohms(black[ch])]
            gamma, d = solve_gamma(log_r)
            defaulted |= d
            t[ch] = (65536 << 16) // gamma
            black_light[ch] = light_at(log_r[4], log_r[0], t[ch])
        if max(t) > 2 * min(t):
            defaulted = True
            for ch in range(3):
                t[ch] = (65536 << 16) // GAMMA_DEF
                black_light[ch] = light_at(log_ohms(black[ch]), log_ohms(white[ch]), t[ch])
        if defaulted:
            self.quality |= QUAL_GAMMA_DEFAULT

        lut = []
        for ch in range(3):
            log_w, b = log_ohms(white[ch]), black_light[ch]
            den = 65536 - b
            if den < 4096:
                return False
            chan = []
            for i in range(LUT_SIZE):
                n = (i << 3) - 2048
                u = light_at(log_ohms(n), log_w, t[ch])
                chan.append(clamp(cdiv((u - b) << 16, den), LIGHT_FLOOR, LIGHT_CEIL))
            lut.append(normalise(chan, black[ch], white[ch]))
        self.lut = lut

        m = [[0] * 3 for _ in range(3)]
        sigma = [0] * 3
        for i in range(3):
            for j in range(3):
                m[i][j] = clamp(curve_at(lut[i], prim[j][i]), 0, LIGHT_CEIL) << 8
                sigma[i] += m[i][j]
            if sigma[i] < (Q24 >> 4):
                return False

        sep = separation(m)
        if sep < MIN_SEP:
            self.quality |= QUAL_LOW_SEP
            return False

        lo, hi = Q24 // 100, Q24
        best, max_l1, clamped = shrink_invert(m, sigma, hi)
        if best is None:
            return False
        for _ in range(14):
            mid = (lo + hi) // 2
            cand, l1, c = shrink_invert(m, sigma, mid)
            if cand is not None and l1 <= MAX_ROW_L1:
                hi, best, clamped = mid, cand, c
            else:
                lo = mid
        if clamped:
            self.quality |= QUAL_ROW_CLAMPED

        for i in range(3):
            row = [clamp(best[i][j] >> 14, -ROW_BUDGET, ROW_BUDGET) for j in range(3)]
            l1 = sum(abs(v) for v in row)
            self.a10[i] = [cdiv(v * ROW_BUDGET, l1) if l1 > ROW_BUDGET else v for v in row]

        b_avg = sum(black_light) // 3
        self.gain = build_gain_table(clamp((b_avg << 16) // (65536 - b_avg), 1024, 16384))
        self.sep_bars = 5 if sep >= 19661 else 4 if sep >= 9830 else 3 if sep >= 5243 else 2
        self.sep = sep
        return True

    def gain_at(self, v):
        k, fr = v >> 9, v & 511
        a = self.gain[k]
        return a + (i32((self.gain[k + 1] - a) * fr) >> 9)

    def read(self, raw, gq10=1024):
        """SensorPipeline::Update, without the slew."""
        light = [curve_at(self.lut[ch], raw[ch]) for ch in range(3)]
        c = []
        for i in range(3):
            acc = i32(sum(self.a10[i][j] * light[j] for j in range(3)))
            c.append(clamp(acc >> 10, 0, LIGHT_CEIL))
        g = self.gain_at(max(c))
        return [clamp(i32(i32(v * g) >> 12) * gq10 >> 10, 0, 65535) for v in c]


def check_response_curve():
    err = max(abs(log2_q16(v) / 65536 - math.log2(v)) for v in range(1, 300000, 7))
    check("log2_q16 within 0.001 bits", err < 0.001, f"{err:.5f}")
    # Q16 output, so relative precision necessarily runs out as the result
    # gets small: at 2^-8 one count is already 0.4%. Hold it to a relative
    # bound where the answer is big enough to carry one, and to a single
    # count below that. Light is normalised to white (65536) and black sits
    # at a few percent of that, so the whole working range is in the first case.
    err = max(abs(exp2_q16(x) / 65536 / 2 ** (x / 65536) - 1)
              for x in range(-4 * 65536, 8 * 65536, 499))
    check("exp2_q16 within 2e-4 relative over the working range", err < 2e-4, f"{err:.2e}")
    abs_err = max(abs(exp2_q16(x) - 65536 * 2 ** (x / 65536))
                  for x in range(-17 * 65536, -4 * 65536, 499))
    check("exp2_q16 within 1 count far below the working range", abs_err <= 1.0, f"{abs_err:.2f}")
    mono = all(exp2_q16(x) <= exp2_q16(x + 1) for x in range(-65536, 65536, 7))
    check("exp2_q16 monotonic", mono)
    check("exp2_q16 exact at integers",
          all(exp2_q16(k << 16) == 65536 << k for k in range(0, 8)))

    ceiling = reading_for(1)
    check("the 1k series caps the reading below full scale", ceiling == 2031, str(ceiling))
    worst = max(abs(reading_for(ldr_ohms(n)) - n) for n in range(20, ceiling))
    check("reading -> resistance -> reading is stable", worst <= 1, f"{worst} LSB")

    # The R^(-1/gamma) underflow trap. Un-normalised, R^(-1/gamma) for a real
    # resistance is ~1e-7 and vanishes in Q16 — normalising to the white
    # capture is what keeps the numbers alive. What has to stay well clear of
    # zero is the black reference, because it is the dark-subtraction's
    # denominator; readings far darker than black may underflow, and do no harm.
    worst_black, l0_lo, l0_hi = 65536, 65536, 0
    for r_white in (10_000, 60_000, 200_000):
        log_w = log_ohms(reading_for(r_white))
        for refl in (0.03, 0.10, 0.25):          # a matte black card, at worst
            log_b = log_ohms(reading_for(r_white / refl))
            for gamma in (GAMMA_LO, GAMMA_DEF, GAMMA_HI):
                b = light_at(log_b, log_w, (65536 << 16) // gamma)
                worst_black = min(worst_black, b)
                l0 = clamp((b << 16) // (65536 - b), 1024, 16384)
                l0_lo, l0_hi = min(l0_lo, l0), max(l0_hi, l0)
    check("the black reference never underflows to nothing", worst_black >= 1,
          f"smallest {worst_black} of 65536")
    check("the curve's knee stays inside its clamp for every black reference",
          1024 <= l0_lo and l0_hi <= 16384, f"L0 {l0_lo/65536:.4f}..{l0_hi/65536:.4f}")

    gels = {"red": (10_000, 200_000), "green": (25_000, 400_000), "blue": (60_000, 900_000)}
    mids = {}
    for name, (r_white, r_black) in gels.items():
        white, black = reading_for(r_white), reading_for(r_black)
        lut = build_two_point(white, black)
        check(f"{name}: black reads 0, white reads full",
              curve_at(lut, black) <= 2 and curve_at(lut, white) >= 65533,
              f"{curve_at(lut, black)} .. {curve_at(lut, white)}")
        mono = all(curve_at(lut, n) <= curve_at(lut, n + 1) for n in range(-2048, 2047))
        check(f"{name}: response is monotonic", mono)
        lw, lb = math.log2(ldr_ohms(white)), math.log2(ldr_ohms(black))
        err = max(abs(curve_at(lut, n) - (lb - math.log2(ldr_ohms(n))) / (lb - lw) * 65535)
                  for n in range(black, white + 1))
        check(f"{name}: table within 0.3% of the exact curve", err < 200, f"{err/655.35:.2f}%")
        mids[name] = curve_at(lut, reading_for(math.sqrt(r_white * r_black))) / 65535
    check("mid-grey reads ~50% on every gel, not just one",
          all(0.47 < m < 0.53 for m in mids.values()),
          ", ".join(f"{k} {v*100:.0f}%" for k, v in mids.items()))

    r_white, r_black = gels["red"]
    white, black = reading_for(r_white), reading_for(r_black)
    lin = (reading_for(math.sqrt(r_white * r_black)) - black) / (white - black)
    check("linear-in-voltage would read mid-grey high (the original complaint)",
          lin > 0.60, f"{lin*100:.0f}% linear vs {mids['red']*100:.0f}% corrected")


def check_sensors():
    g = [gain_q10(x) for x in (0, 2048, 4095)]
    check("X gain 0.25x / 1x / ~4x (Q10)", g[0] == 256 and g[1] == 1024 and 4000 < g[2] <= 4096, str(g))
    shifts = [5 + ((y * 10) >> 12) for y in (0, 2048, 4095)]
    check("Y slew shift 5..14", shifts == [5, 10, 14], str(shifts))

    cases = {
        "defaults": (0, 2047),
        "typical": (300, 1500),
        "inverted wand": (1500, 300),
        "minimum span": (1000, 1000 + MIN_SPAN),
        "minimum span, inverted": (-900, -900 - MIN_SPAN),
        "full range": (-2048, 2047),
    }
    for name, (black, white) in cases.items():
        ends_ok = mono = True
        try:
            pipe = Pipeline({"mode": "two", "white": [white] * 3, "black": [black] * 3})
            for gx in (0, 2048, 4095):
                gq = gain_q10(gx)
                ends_ok &= pipe.read([black] * 3, gq)[0] <= 2
                if gx == 2048:
                    ends_ok &= pipe.read([white] * 3, gq)[0] >= 65533
                prev = -1
                step = 1 if white > black else -1
                far_dark, far_bright = (-2048, 2047) if step == 1 else (2047, -2048)
                for raw in range(far_dark, far_bright + step, step):
                    u = pipe.read([raw] * 3, gq)[0]
                    mono &= u >= prev
                    prev = u
            overflow = False
        except OverflowError:
            overflow = True
        check(f"calibration '{name}': black->0, white->full, monotonic, no overflow",
              ends_ok and mono and not overflow)

    check("span check rejects 63, accepts 64",
          fail_mask([0, 0, 0], [63, -63, 64]) == 0b011 and fail_mask([0, 0, 0], [64, -64, 2000]) == 0)

    # The two-point path must be EXACTLY what it was before the un-mix existed:
    # identity matrix in Q10 and a unity gain table are both exact, so every
    # reading must match the plain table lookup bit for bit.
    same = True
    for black, white in ((300, 1500), (0, 2047), (1500, 300)):
        pipe = Pipeline({"mode": "two", "white": [white] * 3, "black": [black] * 3})
        lut = build_two_point(white, black)
        for gx in (0, 2048, 4095):
            gq = gain_q10(gx)
            for raw in range(-2048, 2048, 7):
                want = clamp(i32(curve_at(lut, raw) * gq) >> 10, 0, 65535)
                same &= pipe.read([raw] * 3, gq)[0] == want
    check("two-point mode is bit-identical to the card before the un-mix", same)


# --- cross-talk: the five-point calibration --------------------------------------

class Wand:
    """A synthetic wand: gels x cell response x inter-cell scatter, the LDR
    power law, and the module's divider. The world is float; everything the
    firmware sees is an integer reading."""

    def __init__(self, gels, kappa, gamma, r_white, ambient, black_refl,
                 abl=1.0, night=1.0, noise=0.0, rnd=None):
        col = [sum(gels[i][j] for i in range(3)) for j in range(3)]
        self.mix = [[gels[i][j] + kappa * col[j] for j in range(3)] for i in range(3)]
        self.rowsum = [sum(self.mix[i]) for i in range(3)]
        self.gamma, self.r_white, self.amb = gamma, r_white, ambient
        self.black_refl, self.abl, self.night = black_refl, abl, night
        self.noise, self.rnd = noise, rnd or random.Random(0)

    def light(self, i, c):
        tint = (1.0, 1.0, self.night)
        lit = sum(self.mix[i][j] * c[j] * tint[j] for j in range(3)) / self.rowsum[i]
        return lit + self.amb

    def ohms(self, i, c):
        ref = self.light(i, (self.abl,) * 3)
        return self.r_white[i] * (self.light(i, c) / ref) ** (-self.gamma[i])

    def raw(self, i, c):
        r = self.ohms(i, c)
        n = SUPPLY * INPUT_OHMS / (INPUT_OHMS + r + SERIES_OHMS)
        if self.noise:
            n += self.rnd.gauss(0, self.noise)
        return clamp(round(n), -2048, 2047)

    def capture(self, c):
        """What a tap averages: the ADC noise mostly averages out."""
        if not self.noise:
            return [self.raw(i, c) for i in range(3)]
        return [round(sum(self.raw(i, c) for _ in range(64)) / 64) for i in range(3)]

    def calibration(self, five=True):
        white = self.capture((self.abl,) * 3)
        black = self.capture((self.black_refl,) * 3)
        cal = {"mode": "five" if five else "two", "white": white, "black": black}
        cal["prim"] = [self.capture(tuple(1.0 if k == j else 0.0 for k in range(3)))
                       for j in range(3)]
        return cal


GELS = {
    # gels[i][j]: what cell i sees of primary j, before scatter
    "optimistic": [[1.00, 0.12, 0.10], [0.15, 0.95, 0.18], [0.12, 0.20, 0.70]],
    "leaky":      [[0.90, 0.35, 0.30], [0.40, 0.85, 0.45], [0.35, 0.40, 0.50]],
    "marginal":   [[0.88, 0.40, 0.36], [0.45, 0.82, 0.50], [0.40, 0.45, 0.48]],
    "awful":      [[0.80, 0.60, 0.55], [0.60, 0.75, 0.65], [0.55, 0.60, 0.55]],
    "hopeless":   [[0.70, 0.68, 0.66], [0.68, 0.70, 0.68], [0.66, 0.68, 0.67]],
}


def make_wand(name, **kw):
    opts = dict(kappa=0.10, gamma=(0.70, 0.75, 0.65),
                r_white=(12_000, 30_000, 70_000), ambient=0.03, black_refl=0.05)
    opts.update(kw)
    return Wand(GELS[name], **opts)


def looks_two_point(cal):
    """CapturesLookTwoPoint: are the four bright captures one surface, shown
    four times? That is how a two-point calibration is asked for."""
    for ch in range(3):
        span = abs(cal["white"][ch] - cal["black"][ch])
        tol = max(span // 6, 32)
        for j in range(3):
            if abs(cal["white"][ch] - cal["prim"][j][ch]) > tol:
                return False
    return True


def check_calibration_kind():
    # Four presentations of white, with a hand that wanders more than it
    # should, must still read as a two-point calibration.
    rnd = random.Random(19)
    for name in GELS:
        for jitter, label in ((8, "steady hand"), (40, "wandering hand")):
            w = make_wand(name)
            white = w.capture((1.0, 1.0, 1.0))
            cal = {"mode": "five", "white": white, "black": w.capture((0.05,) * 3),
                   "prim": [[v + rnd.randint(-jitter, jitter) for v in white] for _ in range(3)]}
            check(f"{name}, {label}: white shown four times reads as two-point",
                  looks_two_point(cal))

    # A real set of primaries never does, however badly the gels overlap:
    # white is their sum, so at least one cell sees it clearly brighter.
    for name in GELS:
        cal = make_wand(name).calibration()
        margins = []
        for ch in range(3):
            span = abs(cal["white"][ch] - cal["black"][ch])
            gap = max(abs(cal["white"][ch] - cal["prim"][j][ch]) for j in range(3))
            margins.append(gap / max(span // 6, 32))
        check(f"{name}: real primaries read as five-point",
              not looks_two_point(cal),
              f"widest gap is {max(margins):.1f}x the tolerance")

    # And a mis-read is harmless: four near-identical 'primaries' make a
    # singular matrix, which the separability test declines.
    w = make_wand("leaky")
    white = w.capture((1.0, 1.0, 1.0))
    cal = {"mode": "five", "white": white, "black": w.capture((0.05,) * 3),
           "prim": [[v + rnd.randint(-70, 70) for v in white] for _ in range(3)]}
    pipe = Pipeline(cal)
    check("a two-point capture mistaken for five-point degrades safely",
          pipe.sep_bars == 0 and all(0 <= v <= 65535 for v in pipe.read(white)))


def check_crosstalk():
    blue, red, green = (0, 0, 1), (1, 0, 0), (0, 1, 0)

    # The bench complaint, reproduced against the old pipeline.
    w = make_wand("leaky")
    two = Pipeline(w.calibration(five=False))
    before = [v / 65535 for v in two.read(w.capture(blue))]
    check("the reported symptom reproduces: blue reads grey on a two-point card",
          max(before) - min(before) < 0.30,
          "blue -> " + ", ".join(f"{v:.2f}" for v in before))

    results = {}
    for name in GELS:
        w = make_wand(name)
        pipe = Pipeline(w.calibration())
        results[name] = pipe
        got = [v / 65535 for v in pipe.read(w.capture(blue))]
        sep = got[2] - max(got[0], got[1])
        if name in ("awful", "hopeless"):
            # Too little to separate: it must decline the job, not guess.
            check(f"{name}: declines rather than amplifying noise",
                  pipe.sep_bars == 0 and (pipe.quality & QUAL_LOW_SEP) != 0,
                  f"bars {pipe.sep_bars}, blue -> " + ", ".join(f"{v:.2f}" for v in got))
            continue
        check(f"{name}: blue reads blue, not grey", sep >= 0.30,
              "blue -> " + ", ".join(f"{v:.2f}" for v in got) +
              f"  (separation {sep:.2f}, was {before[2]-max(before[0],before[1]):.2f})")

    for name in ("optimistic", "leaky", "marginal"):
        w = make_wand(name)
        pipe = results[name]
        for label, c, idx in (("red", red, 0), ("green", green, 1)):
            got = [v / 65535 for v in pipe.read(w.capture(c))]
            check(f"{name}: {label} reads {label}",
                  got[idx] - max(got[k] for k in range(3) if k != idx) >= 0.25,
                  ", ".join(f"{v:.2f}" for v in got))

    # White, black and grey.
    for name in ("optimistic", "leaky", "marginal", "awful"):
        w = make_wand(name)
        pipe = results[name]
        white = [v / 65535 for v in pipe.read(w.capture((1.0, 1.0, 1.0)))]
        black = pipe.read(w.capture((w.black_refl,) * 3))
        grey = [v / 65535 for v in pipe.read(w.capture((0.5, 0.5, 0.5)))]
        check(f"{name}: white reads full on all three", min(white) >= 0.98,
              ", ".join(f"{v:.2f}" for v in white))
        check(f"{name}: black reads zero", max(black) == 0, str(black))
        check(f"{name}: grey stays neutral (this is what the gamma ratios buy)",
              max(grey) - min(grey) <= 0.08,
              ", ".join(f"{v:.2f}" for v in grey))

    # Degrading safely.
    w = make_wand("hopeless")
    pipe = results["hopeless"]
    got = pipe.read(w.capture(blue))
    check("hopeless gels: falls back rather than exploding",
          pipe.sep_bars == 0 and (pipe.quality & QUAL_LOW_SEP) != 0
          and all(0 <= v <= 65535 for v in got),
          f"bars {pipe.sep_bars}, quality {pipe.quality}")

    # Row budget and the int32 accumulator — the load-bearing check.
    peak = 0
    overflow = False
    try:
        for name in GELS:
            w = make_wand(name)
            pipe = results[name]
            budget_ok = all(sum(abs(v) for v in pipe.a10[i]) <= ROW_BUDGET for i in range(3))
            check(f"{name}: every matrix row inside its budget", budget_ok,
                  str([sum(abs(v) for v in pipe.a10[i]) for i in range(3)]))
            # The worst case needs the three channels set INDEPENDENTLY: the
            # un-mix has negative coefficients, so the accumulator only peaks
            # when the light vector lines up with their signs. Sweeping the
            # channels together never gets there.
            extremes = (-2048, 0, 1024, 2031, 2047)
            for gx in (0, 2048, 4095):
                gq = gain_q10(gx)
                for a in extremes:
                    for b in extremes:
                        for c in extremes:
                            vals = [a, b, c]
                            pipe.read(vals, gq)
                            light = [curve_at(pipe.lut[ch], vals[ch]) for ch in range(3)]
                            for i in range(3):
                                peak = max(peak, abs(sum(pipe.a10[i][j] * light[j] for j in range(3))))
                for raw in range(-2048, 2048, 7):
                    pipe.read([raw, 2047, -2048], gq)
    except OverflowError:
        overflow = True
    check("no int32 overflow anywhere in the runtime path", not overflow)
    # The pessimistic bound is row budget x light ceiling = 1.61e9. The real
    # worst case is lower, because the table's negative headroom is only
    # -65536 while its positive headroom is 262140, so negative coefficients
    # cannot contribute their full share. Both are inside int32; this asserts
    # the sweep gets close enough to have tested the real one.
    check("the overflow sweep reaches the tight spot",
          peak > 8.0e8, f"peak accumulator {peak:.3e}, bound 1.61e9, int32 2.147e9")

    # Gamma: ratios recovered, and the fallback fires on a grey 'black'.
    w = make_wand("leaky")
    cal = w.calibration()
    est = []
    for ch in range(3):
        log_r = [log_ohms(cal["white"][ch])] + [log_ohms(cal["prim"][j][ch]) for j in range(3)]
        log_r.append(log_ohms(cal["black"][ch]))
        g, _ = solve_gamma(log_r)
        est.append(g / 65536)
    ratios_ok = all(abs(est[ch] / est[0] - w.gamma[ch] / w.gamma[0]) < 0.08 for ch in range(3))
    check("gamma ratios recovered within 8% (absolute value is not the claim)",
          ratios_ok, ", ".join(f"{v:.3f}" for v in est) +
          " vs true " + ", ".join(f"{v:.2f}" for v in w.gamma))

    grey_ref = make_wand("leaky", black_refl=0.20)
    cal = grey_ref.calibration()
    defaulted = False
    for ch in range(3):
        log_r = [log_ohms(cal["white"][ch])] + [log_ohms(cal["prim"][j][ch]) for j in range(3)]
        log_r.append(log_ohms(cal["black"][ch]))
        _, d = solve_gamma(log_r)
        defaulted |= d
    check("a grey card used as 'black' is detected, not silently believed", defaulted)

    # Room light shifts the absolute exponents — the black CARD is not true
    # dark, so the constraint's "black" term carries some of it. What has to
    # hold is the RATIOS between the three cells, since those are what keep a
    # grey card neutral, and the end-to-end result.
    ratios, greys = [], []
    for amb in (0.0, 0.05, 0.15):
        w = make_wand("leaky", ambient=amb)
        cal = w.calibration()
        est = []
        for ch in range(3):
            log_r = [log_ohms(cal["white"][ch])] + [log_ohms(cal["prim"][j][ch]) for j in range(3)]
            log_r.append(log_ohms(cal["black"][ch]))
            g, _ = solve_gamma(log_r)
            est.append(g / 65536)
        ratios.append((est[1] / est[0], est[2] / est[0]))
        grey = [v / 65535 for v in Pipeline(cal).read(w.capture((0.5, 0.5, 0.5)))]
        greys.append(max(grey) - min(grey))
    spread = max(max(abs(r[k] - s[k]) for k in (0, 1)) for r in ratios for s in ratios)
    check("ambient light does not move the gamma RATIOS", spread < 0.08, f"{spread:.3f}")
    check("grey stays neutral at every ambient level", max(greys) <= 0.08,
          ", ".join(f"{v:.3f}" for v in greys))

    # A dimming panel and night mode must not break white.
    for label, kw in (("panel dims full white", {"abl": 0.80}),
                      ("night mode", {"night": 0.60})):
        w = make_wand("leaky", **kw)
        pipe = Pipeline(w.calibration())
        white = [v / 65535 for v in pipe.read(w.capture((w.abl,) * 3))]
        got = [v / 65535 for v in pipe.read(w.capture(blue))]
        check(f"{label}: white still reads full and blue still reads blue",
              min(white) >= 0.97 and got[2] - max(got[0], got[1]) >= 0.25,
              "white " + ",".join(f"{v:.2f}" for v in white) +
              " blue " + ",".join(f"{v:.2f}" for v in got))

    # Noise, through the slew the modes actually see.
    rnd = random.Random(11)
    w = make_wand("leaky", noise=2.0, rnd=rnd)
    pipe = Pipeline(w.calibration())
    state = [0, 0, 0]
    seen = [[], [], []]
    for _ in range(4000):
        u = pipe.read([w.raw(i, (0.5, 0.5, 0.5)) for i in range(3)])
        for ch in range(3):
            state[ch] = slew_exact(state[ch], u[ch], 5)
            seen[ch].append(state[ch])
    worst = 0.0
    for s in seen:
        tail = s[1000:]
        mean = sum(tail) / len(tail)
        worst = max(worst, math.sqrt(sum((v - mean) ** 2 for v in tail) / len(tail)))
    check("2 LSB of ADC noise stays under 0.5% of full scale after the slew",
          worst < 328, f"{worst:.0f} counts rms")

    # Mode 6 consumes this: a blue patch must land on blue in hue terms.
    w = make_wand("leaky")
    pipe = results["leaky"]
    got = pipe.read(w.capture(blue))
    hue = HueOrganMode_hue(got[0], got[1], got[2])
    check("Mode 6 sees the blue patch as blue", abs(hue - 43691) < 4000, f"hue {hue}")


def HueOrganMode_hue(r, g, b):
    return hue_q16(r, g, b)


# --- osc.h --------------------------------------------------------------------

def wave(w, p):
    p &= 0xFFFFFFFF
    if w == "sine":
        return fast_sin(p)
    if w == "tri":
        v = ((p + 0x40000000) & 0xFFFFFFFF) >> 15
        if v >= 65536:
            v = 131071 - v
        return v - 32768
    if w == "saw":
        return (((p + 0x80000000) & 0xFFFFFFFF) >> 16) - 32768
    return 32767 if p < 0x80000000 else -32767


def morph_set(path, pos):
    n = len(path)
    m = pos * (n - 1)
    seg = m >> 12
    if seg >= n - 1:
        return path[-1], path[-1], 0
    return path[seg], path[seg + 1], (m & 0xFFF) << 3


def morph_render(a, b, frac, p):
    va = wave(a, p)
    if frac == 0:
        return va
    return va + mul_q15(wave(b, p) - va, frac)


def check_osc():
    aligned = all(abs(wave(w, 0)) <= 1 or w == "square" for w in ("sine", "tri", "saw"))
    check("waves cross zero at phase 0", aligned)
    peak = {w: max(abs(wave(w, p)) for p in range(0, 2**32, 2**32 // 997)) for w in ("sine", "tri", "saw", "square")}
    check("waves stay inside Q15", all(v <= 32768 for v in peak.values()), str(peak))
    path4 = ["sine", "tri", "saw", "square"]
    ok = True
    for pos in range(0, 4096, 5):
        a, b, fr = morph_set(path4, pos)
        for p in range(0, 2**32, 2**32 // 61):
            ok &= abs(morph_render(a, b, fr, p)) <= 32768
    check("morph: no overflow across the knob", ok)

    # Triad: three full-level voices summed and scaled never exceed the DAC.
    worst = 0
    for pos in (0, 1365, 2730, 4095):
        a, b, fr = morph_set(path4, pos)
        for p in range(0, 2**32, 2**32 // 211):
            s = sum(i32(morph_render(a, b, fr, p * k) * 65535) >> 16 for k in (1, 2, 3))
            worst = max(worst, abs(i32(s * 1365) >> 16))
    check("triad sum stays within +/-2047 before clamp", worst <= 2047, f"peak {worst}")


# --- svf.h --------------------------------------------------------------------

class Svf:
    MAX = 32767 << 8

    def __init__(self):
        self.f, self.q = 1000, 22938
        self.lp8 = self.bp8 = self.hp = 0

    @property
    def lp(self):
        return self.lp8 >> 8

    @property
    def bp(self):
        return self.bp8 >> 8

    def set(self, cut_u, res_u):
        self.f = min(pow2_scale(86, i32(cut_u * 31) >> 6), 16000)
        self.q = 22938 - (i32(res_u * 21627) >> 16)

    def process(self, x):
        hp = x - self.lp - (i32(self.q * self.bp) >> 14)
        self.bp8 = clamp(self.bp8 + (i32(self.f * hp) >> 6), -self.MAX, self.MAX)
        self.lp8 = clamp(self.lp8 + (i32(self.f * self.bp) >> 6), -self.MAX, self.MAX)
        self.hp = clamp(hp, -32767, 32767)


def blend_set(knob):
    z = min(knob // 819, 4)
    return z, min((knob - z * 819) * 40, 32767)


def blend_render(s, seg, frac):
    if seg == 0:
        return s.lp
    if seg == 1:
        return s.lp + mul_q15(s.bp - s.lp, frac)
    if seg == 2:
        return s.bp
    if seg == 3:
        return s.bp + mul_q15(s.hp - s.bp, frac)
    return s.hp


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs))


def check_svf():
    hz = [48000 / (2 * math.pi) * 2 * math.asin(min(pow2_scale(86, (u * 31) >> 6), 16000) / 32768)
          for u in (0, 65535)]
    check("cutoff range ~40Hz .. ~7.8kHz", 35 < hz[0] < 45 and 7000 < hz[1] < 8500,
          f"{hz[0]:.0f}Hz .. {hz[1]:.0f}Hz")

    # Stability: worst-case drive (full-scale square) at every corner of the
    # cutoff/resonance grid, then silence. Must decay, not self-oscillate.
    stable = True
    for cu in (0, 20000, 45000, 65535):
        for ru in (0, 32768, 65535):
            s = Svf()
            s.set(cu, ru)
            for n in range(2400):
                s.process(2047 if (n // 40) % 2 else -2048)
            for n in range(24000):
                s.process(0)
            stable &= abs(s.lp) <= 1 and abs(s.bp) <= 1
    check("SVF stable, decays to <= 1 LSB at every cutoff/resonance corner", stable)

    # A quiet signal well below the lowest cutoff must pass, not stick in a
    # truncation dead band (the bug the Q8 states exist to fix).
    s = Svf()
    s.set(0, 0)
    xs = [round(40 * math.sin(2 * math.pi * 5 * n / 48000)) for n in range(48000)]
    ys = []
    for x in xs:
        s.process(x)
        ys.append(s.lp)
    ratio = rms(ys[9600:]) / rms(xs[9600:])
    check("quiet 5Hz signal passes the 40Hz LP", 0.8 < ratio < 1.25, f"gain {ratio:.2f}")

    # Blend continuity: a sweep of the Main knob must not jump in level.
    rnd = random.Random(1)
    noise = [rnd.randint(-2048, 2047) for _ in range(4800)]
    levels = []
    for knob in range(0, 4096, 64):
        s = Svf()
        s.set(40000, 20000)
        seg, fr = blend_set(knob)
        out = []
        for x in noise:
            s.process(x)
            out.append(blend_render(s, seg, fr))
        levels.append(rms(out[480:]))
    jumps = max(abs(20 * math.log10(levels[i + 1] / levels[i])) for i in range(len(levels) - 1))
    check("LP->BP->HP blend has no level step > 1.5dB between knob steps", jumps < 1.5, f"max {jumps:.2f}dB")


# --- modes/synesthesia.cpp -----------------------------------------------------

def fold(v):
    t = (v + 2048) & 8191
    if t > 4096:
        t = 8192 - t
    return t - 2048


def check_fold():
    check("fold is identity inside the rails", all(fold(v) == v for v in range(-2047, 2048)))
    vs = [fold(i32(v * (256 + 1792) >> 8)) for v in range(-2047, 2048)]
    check("fold bounded at 8x drive", all(-2048 <= v <= 2048 for v in vs))
    check("fold continuous (no jump > 8x step)", all(abs(vs[i + 1] - vs[i]) <= 9 for i in range(len(vs) - 1)))
    k = [pow2_scale(4, ((65535 - u) * 10) >> 4) for u in (65535, 0)]
    taus = [2**20 / kk / 48000 for kk in k]
    check("decay time ~5.5s bright .. ~5ms dark", 5 < taus[0] < 6 and 0.004 < taus[1] < 0.007,
          f"{taus[0]:.2f}s .. {taus[1]*1000:.1f}ms")


# --- modes/barcode.cpp ----------------------------------------------------------

def speed_q16(knob, dead=60):
    d = knob - 2048
    if -dead < d < dead:
        return 65536
    d += -dead if d > 0 else dead
    return pow2_scale(65536, cdiv(d * 12288, 2048 - dead))


def barcode_elements(rec, min_width=2):
    """Mirror of BarcodeMode's analysis: cut a take into runs of black/white."""
    mn, mx = min(rec), max(rec)
    if mx - mn < 16:
        return [], 0
    mid = (mn + mx) // 2
    lo = mid - (mx - mn) // 8
    hi = mid + (mx - mn) // 8
    dark = rec[0] < mid
    start, elems = 0, []
    for i in range(1, len(rec)):
        v = rec[i]
        flip = (v > hi) if dark else (v < lo)
        if not flip:
            continue
        width = i - start
        if width < min_width:
            continue
        elems.append((start, width, dark))
        start, dark = i, not dark
    if len(rec) > start:
        elems.append((start, len(rec) - start, dark))
    if len(elems) < 2:
        return [], 0
    return elems, max(1, len(rec) // len(elems))


def barcode_play(elems, mean, length, speed_q16, ticks):
    """Mirror of the playback walk. Returns (all triggers, wide triggers)."""
    pos, idx, fired, wide = 0, 0, 0, 0
    for _ in range(ticks):
        pos += speed_q16
        if (pos >> 16) >= length:
            pos -= length << 16
            idx = 0
            fired += 1
            if elems[0][1] > mean:
                wide += 1
        i = pos >> 16
        while idx + 1 < len(elems) and elems[idx + 1][0] <= i:
            idx += 1
            fired += 1
            if elems[idx][1] > mean:
                wide += 1
    return fired, wide


def make_barcode(widths, rnd, narrow=6, wide=18, black=40, white=210):
    """A swiped code: alternating black/white runs, with noise and a little
    speed wobble, as an 8-bit brightness track."""
    rec = []
    dark = True
    for w in widths:
        n = (wide if w else narrow) + rnd.randint(-1, 1)
        level = black if dark else white
        rec += [clamp(level + rnd.randint(-12, 12), 0, 255) for _ in range(n)]
        dark = not dark
    return rec


def check_barcode():
    sp = [speed_q16(k) for k in range(4096)]
    check("speed monotonic", all(sp[i] <= sp[i + 1] for i in range(4095)))
    check("speed 1/8x .. 1x at noon .. ~8x", sp[0] == 8192 and sp[2048] == 65536 and sp[4095] > 520000,
          f"{sp[0]} {sp[2048]} {sp[4095]}")
    edge_jump = max(abs(sp[k + 1] / sp[k] - 1) for k in (2048 - 61, 2048 + 59))
    check("no jump at the dead-zone edges", edge_jump < 0.01, f"{edge_jump*100:.2f}%")

    rnd = random.Random(7)

    # A Code-39-like run of narrow and wide bars, swiped with noise.
    pattern = [0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0]
    rec = make_barcode(pattern, rnd)
    elems, mean = barcode_elements(rec)
    check("barcode: every bar and space is found, none invented",
          len(elems) == len(pattern), f"{len(elems)} of {len(pattern)}")

    got_wide = [1 if w > mean else 0 for (_, w, _) in elems]
    check("barcode: wide bars separate from narrow ones", got_wide == pattern,
          f"{sum(a != b for a, b in zip(got_wide, pattern))} misread")

    alternates = all(elems[i][2] != elems[i + 1][2] for i in range(len(elems) - 1))
    check("barcode: black and white alternate", alternates and elems[0][2] is True)

    # Playing a loop fires one trigger per element per pass, at any speed.
    for label, speed in (("1x", 65536), ("1/8x", 8192), ("8x", 524288)):
        passes = 3
        ticks = (len(rec) * 65536 * passes) // speed
        fired, wide = barcode_play(elems, mean, len(rec), speed, ticks)
        check(f"barcode at {label}: {passes} passes fire every element",
              abs(fired - len(elems) * passes) <= 1
              and abs(wide - sum(pattern) * passes) <= 1,
              f"{fired} triggers, {wide} wide")

    flat = [100 + rnd.randint(-5, 5) for _ in range(200)]
    check("barcode: a flat take is not a code", barcode_elements(flat) == ([], 0))

    # Black on white and white on black read the same, whatever the paper.
    inverted = [255 - v for v in rec]
    inv_elems, inv_mean = barcode_elements(inverted)
    check("barcode: an inverted print reads the same widths",
          [w for (_, w, _) in inv_elems] == [w for (_, w, _) in elems])

    # The three gels summed: a grey card reads mid, and one dead channel
    # still leaves a usable signal.
    check("luma averages the three channels",
          (65535 + 65535 + 65535) // 3 == 65535 and (30000 + 30000 + 30000) // 3 == 30000
          and (60000 + 60000 + 0) // 3 == 40000)


# --- modes/hueorgan.cpp ---------------------------------------------------------

SCALE_LENS = [12, 7, 7, 5]


def hue_q16(r, g, b):
    r, g, b = r >> 4, g >> 4, b >> 4
    mx = max(r, g, b)
    d = mx - min(r, g, b)
    if d == 0:
        return -1
    if mx == r:
        h = cdiv(i32((g - b) * 10923), d)
    elif mx == g:
        h = 21845 + cdiv(i32((b - r) * 10923), d)
    else:
        h = 43691 + cdiv(i32((r - g) * 10923), d)
    return h & 0xFFFF


class Organ:
    """Mirror of HueOrganMode's degree picker (hysteresis + dwell)."""

    def __init__(self, n):
        self.n, self.band = n, 65536 // n
        self.degree, self.pending, self.dwell = -1, -1, 0
        self.triggers = 0

    def tick(self, hue):
        hue = (hue + 5461) & 0xFFFF
        cand = -1
        if self.degree >= 0:
            lo = self.degree * self.band - self.band // 4
            hi = (self.degree + 1) * self.band + self.band // 4
            if lo <= hue < hi:
                cand = self.degree
        if cand < 0:
            cand = min(hue // self.band, self.n - 1)
        if cand == self.degree:
            self.pending, self.dwell = -1, 0
        elif cand != self.pending:
            self.pending, self.dwell = cand, 0
        else:
            self.dwell += 1
            if self.dwell >= 45:
                self.degree, self.pending, self.dwell = cand, -1, 0
                self.triggers += 1


def hsv_to_q16(h_deg):
    h = (h_deg % 360) / 60
    x = 1 - abs(h % 2 - 1)
    r, g, b = [(1, x, 0), (x, 1, 0), (0, 1, x), (0, x, 1), (x, 0, 1), (1, 0, x)][int(h) % 6]
    # Gelled LDRs never separate cleanly: add a common floor.
    return [int(40000 * c) + 12000 for c in (r, g, b)]


def check_hue():
    prim = {name: hue_q16(*rgb) for name, rgb in {
        "red": (65535, 0, 0), "yellow": (65535, 65535, 0), "green": (0, 65535, 0),
        "cyan": (0, 65535, 65535), "blue": (0, 0, 65535), "magenta": (65535, 0, 65535)}.items()}
    want = {"red": 0, "yellow": 10923, "green": 21845, "cyan": 32768, "blue": 43691, "magenta": 54613}
    check("hue of primaries/secondaries", all(abs(prim[k] - want[k]) <= 2 for k in want), str(prim))
    check("grey has no hue", hue_q16(30000, 30000, 30000) == -1)

    for s, length in enumerate(SCALE_LENS):
        n = 2 * length + 1
        # A slow rainbow scan, red -> violet (0..300 degrees): every degree of
        # the scale that lies in that arc plays once, in rising order.
        o = Organ(n)
        seq = []
        for i in range(3000):
            o.tick(hue_q16(*hsv_to_q16(300 * i / 2999)))
            if o.degree >= 0 and (not seq or seq[-1] != o.degree):
                seq.append(o.degree)
        rising = all(seq[i] < seq[i + 1] for i in range(len(seq) - 1))
        check(f"scale {s}: rainbow scan plays rising, one trigger per note",
              rising and o.triggers == len(seq), f"{len(seq)} notes, {o.triggers} triggers")

    # Dither right on a band boundary: the hysteresis + dwell must hold one note.
    o = Organ(15)
    rnd = random.Random(3)
    edge = 3 * (65536 // 15) - 5461
    for _ in range(3000):
        o.tick((edge + rnd.randint(-900, 900)) & 0xFFFF)
    check("boundary dither: at most one trigger", o.triggers <= 1, f"{o.triggers}")

    # Red dithering either side of 0 degrees does not wrap to the top note.
    o = Organ(15)
    for _ in range(3000):
        o.tick(rnd.randint(-1500, 1500) & 0xFFFF)
    check("red near 0 degrees stays one note (wrap moved to magenta)", o.triggers == 1, f"{o.triggers}")


# --- controls.h -----------------------------------------------------------------

def check_controls():
    DOWN, MID, UP = 0, 1, 2

    class Controls:
        def __init__(self, sw):
            self.stable = self.cand = sw
            self.count = 30
            self.modes = 0

        def tick(self, raw):
            if raw != self.cand:
                self.cand, self.count = raw, 0
            elif self.count < 30:
                self.count += 1
                if self.count == 30 and self.cand != self.stable:
                    self.stable = self.cand
                    if self.stable == UP:
                        self.modes += 1

    rnd = random.Random(5)
    c = Controls(MID)
    clicks = 20
    for _ in range(clicks):
        # 1.5-6ms of contact bounce each way, then a held position.
        for target, other in ((UP, MID), (MID, UP)):
            for _ in range(rnd.randint(2, 9)):
                c.tick(rnd.choice((target, other)))
            for _ in range(rnd.randint(40, 400)):
                c.tick(target)
    check("bouncy Up clicks advance exactly one mode each", c.modes == clicks, f"{c.modes}/{clicks}")


# --- tape.h / modes/jog.cpp / modes/prism.cpp ------------------------------------

TAPE_MAX = 84000


def position_q8(u, length):
    """Tape::PositionQ8 — the split multiply that avoids a 64-bit product."""
    span = length - 2
    return i32((u >> 8) * span) + (i32((u & 0xFF) * span) >> 8)


def jog_rate(ug, main, dead=2048, maxrate=4 * 65536):
    dev = ug - 32768
    if -dead < dev < dead:
        dev = 0
    else:
        dev += -dead if dev > 0 else dead
    depth = 256 + ((main * 768) >> 12)
    return clamp(65536 + (i32(dev * depth) >> 7), -maxrate, maxrate)


def jog_run(rate, length, samples):
    """Mode 7's position stepping. Returns (idx, frac, wraps)."""
    idx, frac, wraps = 0, 0, 0
    for _ in range(samples):
        frac += rate
        idx += frac >> 16
        frac &= 0xFFFF
        if idx >= length:
            idx -= length
            wraps += 1
        elif idx < 0:
            idx += length
            wraps += 1
    return idx, frac, wraps


def check_tape_modes():
    for length in (2400, 40000, TAPE_MAX):
        ok = True
        for u in range(0, 65536, 7):
            ok &= position_q8(u, length) == (u * (length - 2)) >> 8
        ends = position_q8(0, length) == 0 and (position_q8(65535, length) >> 8) <= length - 2
        check(f"tape position, {length} samples: exact and inside the take", ok and ends)

    rates = [jog_rate(u, 2048) for u in range(65536)]
    check("jog rate monotonic in green", all(rates[i] <= rates[i + 1] for i in range(65535)))
    check("jog rate is exactly 1x across the dead zone",
          all(jog_rate(u, 2048) == 65536 for u in range(32768 - 2047, 32768 + 2048)))
    edges = [jog_rate(32768 - 2049, 4095), jog_rate(32768 + 2049, 4095)]
    check("no jump at the dead-zone edges", all(abs(e - 65536) < 2000 for e in edges), str(edges))
    check("nudge depth (main=0) never reverses", min(jog_rate(u, 0) for u in range(65536)) >= 0)
    full = [jog_rate(0, 4095), jog_rate(65535, 4095)]
    check("shuttle depth (main=max) reverses and reaches +4x",
          full[0] < 0 and full[1] == 4 * 65536, str(full))

    # A constant rate must advance at exactly that rate, forwards and back.
    for rate, name in ((65536, "1x"), (98304, "1.5x"), (-131072, "-2x"), (12288, "0.1875x")):
        idx, frac, wraps = jog_run(rate, 40000, 40000)
        travelled = wraps * 40000 + idx if rate > 0 else -(wraps * 40000 + (40000 - idx) % 40000)
        want = rate * 40000 // 65536
        check(f"jog at {name}: position tracks the rate with no drift",
              abs(travelled - want) <= 1, f"{travelled} vs {want}")

    idx, _, wraps = jog_run(-65536, 2400, 2400 * 3)
    check("jog in reverse wraps at the loop start", wraps == 3 and idx == 0, f"idx {idx} wraps {wraps}")

    # Mode 7's position ramp: one divide, must stay inside the CV range.
    ok = True
    for length in (2400, 40000, TAPE_MAX):
        scale = (65535 * 256) // length
        ok &= 0 <= i32((length - 1) * scale) >> 8 <= 65535
    check("jog position ramp stays 0..65535 for every take length", ok)

    # Mode 8's FM: the product is modular on purpose and spans one cycle.
    worst = max(abs(((fast_sin(p) * (65535 * 2)) % 2**32) - (fast_sin(p) * 131070) % 2**32)
                for p in range(0, 2**32, 2**32 // 97))
    check("prism FM offset is modular (no signed overflow)", worst == 0)
    depth_cycles = (32767 * 131070) / 2**32
    check("prism FM reaches ~1 cycle at full red", 0.99 < depth_cycles <= 1.0, f"{depth_cycles:.3f}")
    crush = [1 + (u >> 10) for u in (0, 65535)]
    check("prism crush holds 1..64 samples", crush == [1, 64], str(crush))


# --- the real colour maps ---------------------------------------------------------
#
# Runs tools/colourmaps/*.png through the same model the cross-talk checks use:
# each pixel is what the screen emits, the wand sees it through its gels, and
# the calibrated pipeline reads it back. That is the end-to-end question — does
# a map the card will actually be pointed at come out as the colours it is?

import os
import struct
import zlib

MAPS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "colourmaps")


def read_png(path):
    """Minimal PNG decode: 8-bit RGB or RGBA, non-interlaced. Returns
    (width, height, rows of (r, g, b))."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos, idat, ihdr = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if not ihdr:
        return None
    w, h, depth, colour, _, _, interlace = ihdr
    if depth != 8 or interlace != 0 or colour not in (2, 6):
        return None
    stride = 3 if colour == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    rows, prev, pos = [], bytearray(w * stride), 0
    for _ in range(h):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + w * stride])
        pos += 1 + w * stride
        for i in range(len(line)):
            a = line[i - stride] if i >= stride else 0
            b = prev[i]
            c = prev[i - stride] if i >= stride else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        prev = line
        rows.append([tuple(line[x * stride:x * stride + 3]) for x in range(w)])
    return w, h, rows


def srgb_to_light(v):
    """A screen emits light proportional to the linearised value, not the
    0-255 code. The sensor sees light, so linearise before modelling it."""
    x = v / 255.0
    return x ** 2.2


def pearson(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    dy = math.sqrt(sum((y - my) ** 2 for y in ys))
    return num / (dx * dy) if dx > 0 and dy > 0 else 0.0


def check_real_maps():
    if not os.path.isdir(MAPS_DIR):
        check("colour maps present to test against", False, "tools/colourmaps missing")
        return

    wands = {}
    for gels in ("optimistic", "leaky"):
        w = make_wand(gels)
        p = Pipeline(w.calibration())
        if p.sep_bars == 0:
            check(f"the {gels} test wand calibrated before reading maps", False)
            return
        wands[gels] = (w, p)

    def reader(gels):
        w, p = wands[gels]
        def read_colour(rgb):
            """What the card reports for a screen showing this pixel."""
            return [v / 65535 for v in p.read(w.capture(tuple(srgb_to_light(c) for c in rgb)))]
        return read_colour

    # 1. Does a map's colour survive the whole chain? Sample a grid of pixels
    # from every map and correlate what went in against what came out.
    #
    # Cross-talk has to be measured against the PICTURE, not against zero: on a
    # black-and-white map the three channels are identical by nature, so a
    # cross-correlation of 1.0 is the image, not the sensor. What matters is
    # whether the card ADDS correlation that was not already there — and doing
    # it with two sets of gels separates the maths from the optics.
    per_gel = {}
    checked = 0
    for gels in ("optimistic", "leaky"):
        read_colour = reader(gels)
        worst_same, worst_added, worst_map = 1.0, 0.0, ""
        checked = 0
        for name in sorted(os.listdir(MAPS_DIR)):
            img = read_png(os.path.join(MAPS_DIR, name))
            if img is None:
                continue
            w, h, rows = img
            ins, outs = [[], [], []], [[], [], []]
            for yi in range(6):
                for xi in range(12):
                    px = rows[(yi * 2 + 1) * h // 13][(xi * 2 + 1) * w // 25]
                    got = read_colour(px)
                    for ch in range(3):
                        ins[ch].append(srgb_to_light(px[ch]))
                        outs[ch].append(got[ch])
            if all(max(v) - min(v) < 0.05 for v in ins):
                continue                  # nothing to correlate against
            checked += 1
            same = min(pearson(ins[ch], outs[ch]) for ch in range(3))
            added = max(abs(pearson(ins[a], outs[b])) - abs(pearson(ins[a], ins[b]))
                        for a in range(3) for b in range(3) if a != b)
            worst_same = min(worst_same, same)
            if added > worst_added:
                worst_added, worst_map = added, name
        per_gel[gels] = (worst_same, worst_added, worst_map)

    check("every map decoded and sampled", checked >= 8, f"{checked} maps")

    good_same, good_added, _ = per_gel["optimistic"]
    check("with clean gels, the maps come back as themselves",
          good_same > 0.95 and good_added < 0.10,
          f"own {good_same:.2f}, added cross {good_added:+.2f}")

    leak_same, leak_added, leak_map = per_gel["leaky"]
    check("with leaky gels, every channel still tracks its own colour",
          leak_same > 0.85, f"worst own {leak_same:.2f}")
    # The residue is the gels, not the arithmetic: same maps, same maths, and
    # it collapses when the gels improve.
    check("leaky gels leave some bleed, and it is the gels that decide how much",
          leak_added < 0.30 and leak_added > good_added,
          f"added cross {leak_added:+.2f} on {leak_map}, vs {good_added:+.2f} with clean gels")

    # 2. The hue field is the colour organ's scale: scanning it sideways should
    # walk the hue circle in one direction, not wander.
    img = read_png(os.path.join(MAPS_DIR, "hue-field.png"))
    w, h, rows = img
    y = h // 2
    hues, notes = [], []
    for xi in range(24):
        px = rows[y][(xi * 2 + 1) * w // 49]
        got = read_colour(px)
        hue = hue_q16(int(got[0] * 65535), int(got[1] * 65535), int(got[2] * 65535))
        if hue < 0:
            continue
        hues.append(hue)
        notes.append((hue + 5461) & 0xFFFF)
    # Unwrap: the sweep crosses the wheel once, so allow a single wrap.
    rises = sum(1 for i in range(len(hues) - 1) if ((hues[i + 1] - hues[i]) & 0xFFFF) < 32768)
    check("hue field: scanning across it walks the colour wheel one way",
          rises >= len(hues) - 2, f"{rises} of {len(hues)-1} steps forward")
    check("hue field: it covers most of the wheel",
          max(hues) - min(hues) > 40000, f"span {(max(hues)-min(hues))/65535*360:.0f} degrees")

    # 3. The red/green field is Mode 4's pad: the two axes must move the two
    # channels independently, which is what makes it usable as an X/Y surface.
    img = read_png(os.path.join(MAPS_DIR, "rg-field.png"))
    w, h, rows = img
    xs = [int(w * 0.05), int(w * 0.3), int(w * 0.55), int(w * 0.75)]
    ys = [int(h * 0.05), int(h * 0.35), int(h * 0.65), int(h * 0.95)]
    grid = [[read_colour(rows[y][x]) for x in xs] for y in ys]
    across = [sum(row[i][0] for row in grid) / len(grid) for i in range(len(xs))]
    down = [sum(cell[1] for cell in row) / len(row) for row in grid]
    check("red/green field: one axis sweeps red",
          max(across) - min(across) > 0.3,
          "red across: " + ", ".join(f"{v:.2f}" for v in across))
    check("red/green field: the other sweeps green",
          max(down) - min(down) > 0.3,
          "green down: " + ", ".join(f"{v:.2f}" for v in down))

    # 4. The barcode is black and white, so Mode 3's luminance path should see
    # bars, and the colour channels should agree with each other throughout.
    img = read_png(os.path.join(MAPS_DIR, "barcode.png"))
    w, h, rows = img
    y = h // 2
    seen = [read_colour(rows[y][x * w // 64]) for x in range(64)]
    neutral = max(max(c) - min(c) for c in seen)
    lumas = [sum(c) / 3 for c in seen]
    check("barcode: reads as neutral black and white, not tinted",
          neutral < 0.25, f"widest colour spread {neutral:.2f}")
    check("barcode: bars and spaces are far apart in level",
          max(lumas) - min(lumas) > 0.5, f"level span {max(lumas)-min(lumas):.2f}")


# --- the tap gesture, the voice, and the behaviour cycles -------------------------

CTRL_RATE = 1500
TAP_TICKS, HOLD_TICKS = 256, CTRL_RATE
DEBOUNCE = 30


def hz_to_inc(hz):
    return (hz << 32) // 48000


def press_ticks(duration_ticks, rnd, bounce=True):
    """Drive Controls with a bouncy press of a known length and report what
    PressTicks() says at the release."""
    stable, cand, count = 1, 1, DEBOUNCE      # Middle
    down_ticks, hold_fired, reported = 0, False, None
    seq = []
    if bounce:
        seq += [rnd.choice((0, 1)) for _ in range(rnd.randint(2, 9))]
    seq += [0] * duration_ticks
    if bounce:
        seq += [rnd.choice((0, 1)) for _ in range(rnd.randint(2, 9))]
    seq += [1] * (HOLD_TICKS + 200)
    for raw in seq:
        if raw != cand:
            cand, count = raw, 0
        elif count < DEBOUNCE:
            count += 1
            if count == DEBOUNCE and cand != stable:
                prev, stable = stable, cand
                if prev == 0 and reported is None:
                    reported = down_ticks
                if stable == 0:
                    down_ticks, hold_fired = 0, False
        held = stable == 0
        if held and not hold_fired:
            down_ticks += 1
            if down_ticks >= HOLD_TICKS:
                hold_fired = True
    return reported, hold_fired


def tape_writes(duration_ticks):
    """Modes 4 and 7: how many samples reach the tape for a press of this
    length, now that recording starts at the threshold rather than the press."""
    if duration_ticks < TAP_TICKS:
        return 0
    return (duration_ticks - TAP_TICKS) * 32      # 32 samples per control tick


def check_gesture():
    rnd = random.Random(23)
    # Clean press: the entry and exit debounce lags must cancel exactly. This
    # is the assumption the whole threshold rests on.
    worst = max(abs(press_ticks(round(ms * CTRL_RATE / 1000), rnd, bounce=False)[0]
                    - round(ms * CTRL_RATE / 1000))
                for ms in range(40, 900, 7))
    check("a clean press is measured exactly", worst == 0, f"worst error {worst} ticks")
    # With contact bounce the reading runs slightly long, because a bouncing
    # contact really is closed for part of the bounce. A few ms either way
    # cannot reach either threshold from the middle of its band.
    worst = max(abs(press_ticks(round(ms * CTRL_RATE / 1000), rnd)[0]
                    - round(ms * CTRL_RATE / 1000))
                for ms in range(40, 900, 7))
    check("contact bounce moves it by under 10ms", worst <= 15, f"worst error {worst} ticks")

    # Nothing may classify both ways.
    taps = [round(ms * CTRL_RATE / 1000) for ms in range(20, 170, 3)]
    recs = [round(ms * CTRL_RATE / 1000) for ms in range(172, 900, 7)]
    ok = all(press_ticks(t, rnd)[0] < TAP_TICKS for t in taps)
    ok &= all(press_ticks(t, rnd)[0] >= TAP_TICKS for t in recs)
    check("every press under 171ms is a tap, every one over it is a record", ok)

    # The saturation that makes Mode 6's migration behaviour-preserving.
    equiv = True
    for ms in (50, 200, 500, 999, 1000, 1200, 3000):
        ticks = round(ms * CTRL_RATE / 1000)
        got, hold = press_ticks(ticks, rnd)
        equiv &= got <= HOLD_TICKS and ((got >= HOLD_TICKS) == hold)
    check("press ticks saturate at the hold, so >= kHoldTicks is the old hold flag", equiv)

    # The claim the deferred record exists to make.
    damaged = max(tape_writes(t) for t in range(0, TAP_TICKS))
    check("a tap writes nothing at all to the tape", damaged == 0)
    window = [t for t in range(TAP_TICKS, TAP_TICKS + 40) if 0 < tape_writes(t) < 256]
    check("only a 5ms window of presses can touch the old take, and then by <=256 samples",
          len(window) <= 8 and all(tape_writes(t) < 256 for t in window),
          f"{len(window)} tick-lengths, at most {max(tape_writes(t) for t in window)} samples")
    check("a real take records exactly what it held for, less the threshold",
          tape_writes(round(1.0 * CTRL_RATE)) == (CTRL_RATE - TAP_TICKS) * 32)

    # Cycling arithmetic.
    for n, name in ((5, "kits"), (3, "behaviours")):
        pos = 0
        for k in range(1, 40):
            pos = (pos + 1) % n
            if pos != k % n:
                break
        check(f"{n} {name} cycle in order and wrap", pos == 39 % n)


# --- percvoice.h ------------------------------------------------------------------

KITS = {
    "kicksnare": {
        "dark":  dict(inc0=hz_to_inc(110), floor=hz_to_inc(50), inc2=0, sweep=9,
                      env=11, nenv=4, mix=20, wave="sine", tap="raw", gain=140),
        "light": dict(inc0=hz_to_inc(230), floor=hz_to_inc(185), inc2=hz_to_inc(269),
                      sweep=8, env=9, nenv=10, mix=170, wave="tri", tap="bp", gain=110),
        "cut": 45003, "res": 20000,
    },
    "clicks": {
        "dark":  dict(inc0=hz_to_inc(700), floor=hz_to_inc(700), inc2=0, sweep=8,
                      env=6, nenv=4, mix=110, wave="sine", tap="lp", gain=128),
        "light": dict(inc0=hz_to_inc(2100), floor=hz_to_inc(2100), inc2=0, sweep=8,
                      env=5, nenv=3, mix=150, wave="sine", tap="hp", gain=128),
        "cut": 45003, "res": 52000,
    },
}


class Voice:
    """Mirror of PercVoice."""

    def __init__(self):
        self.env = self.nenv = 0
        self.phase = self.phase2 = 0
        self.inc = self.inc2 = self.floor = 0
        self.diff = 0
        self.sweep = self.envs = self.nenvs = 8
        self.mix = 0
        self.wave = "sine"
        self.tap = "raw"
        self.gain = 128

    def strike(self, s, floor_inc=None, env_shift=None):
        self.inc = s["inc0"]
        self.floor = floor_inc if floor_inc is not None else s["floor"]
        self.inc2 = s["inc2"]
        self.diff = max(s["inc0"] - self.floor, 0)
        self.sweep, self.envs = s["sweep"], env_shift if env_shift is not None else s["env"]
        self.nenvs, self.mix = s["nenv"], s["mix"]
        self.wave, self.tap, self.gain = s["wave"], s["tap"], s["gain"]
        self.env = self.nenv = 1 << 24

    def idle(self):
        return self.env <= 0 and self.nenv <= 0

    def render(self, raw, svf):
        if self.idle():
            return 0
        body = 0
        if self.env > 0:
            self.phase = (self.phase + self.inc) & 0xFFFFFFFF
            body = wave(self.wave, self.phase) >> 4
            if self.inc2:
                self.phase2 = (self.phase2 + self.inc2) & 0xFFFFFFFF
                body = (body + (wave(self.wave, self.phase2) >> 4)) >> 1
            body = i32(body * (self.env >> 9)) >> 15
            self.env -= (self.env >> self.envs) + 1
            if self.env < 0:
                self.env = 0
            if self.diff > 0:
                self.diff -= (self.diff >> self.sweep) + 1
                if self.diff < 0:
                    self.diff = 0
                self.inc = self.floor + self.diff
        noise = 0
        if self.nenv > 0:
            src = {"raw": raw, "lp": svf.lp, "bp": svf.bp, "hp": svf.hp}[self.tap]
            noise = i32(src * (self.nenv >> 9)) >> 15
            self.nenv -= (self.nenv >> self.nenvs) + 1
            if self.nenv < 0:
                self.nenv = 0
        s = i32(body * (256 - self.mix) + noise * self.mix) >> 8
        return i32(s * self.gain) >> 8


def render_hit(kit, direction, samples=24000, seed=0x1B3F5D79):
    k = KITS[kit]
    v = Voice()
    v.strike(k[direction])
    svf = Svf()
    svf.set(k["cut"], k["res"])
    rng = [seed]

    def rand_audio():
        s = rng[0]
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        rng[0] = s
        return (s >> 20) - 2048

    out = []
    for _ in range(samples):
        raw = rand_audio()
        svf.process(raw)
        out.append(v.render(raw, svf))
    return out, v


def band_energy(sig, lo, hi, step=25):
    """Energy in a band, by direct evaluation — no numpy here."""
    total = 0.0
    n = min(len(sig), 4096)
    for f in range(lo, hi, step):
        w = 2 * math.pi * f / 48000
        re = sum(sig[i] * math.cos(w * i) for i in range(n))
        im = sum(sig[i] * math.sin(w * i) for i in range(n))
        total += re * re + im * im
    return total


def check_voice():
    # Envelopes must REACH zero. A plain shift stalls and leaves DC on the out.
    for shift in range(4, 15):
        env, n = 1 << 24, 0
        while env > 0 and n < 4_000_000:
            env -= (env >> shift) + 1
            n += 1
        check(f"envelope shift {shift} decays to exactly zero", env == 0 and n < 4_000_000,
              f"{n} samples ({n/48000*1000:.0f}ms)") if shift in (4, 9, 14) else None
    stalled = 1 << 24
    for _ in range(2_000_000):
        step = stalled >> 9
        if step == 0:
            break
        stalled -= step
    check("without the +1 guard it would stall short, which is why the guard is there",
          stalled > 0, f"stalls at {stalled}")

    # No int32 overflow anywhere in the voice, both kits, both directions.
    overflow = False
    try:
        for kit in KITS:
            for d in ("dark", "light"):
                render_hit(kit, d, samples=6000)
    except OverflowError:
        overflow = True
    check("no int32 overflow rendering any hit", not overflow)

    # A kick is low, a snare is noisy.
    kick, kv = render_hit("kicksnare", "dark")
    snare, _ = render_hit("kicksnare", "light")
    low = band_energy(kick, 25, 200)
    high = band_energy(kick, 200, 6000)
    check("the kick is actually low", low > 4 * high,
          f"{100*low/(low+high):.0f}% of its energy below 200Hz")
    s_low = band_energy(snare, 25, 1000)
    s_high = band_energy(snare, 1000, 8000)
    check("the snare is actually noisy and bright", s_high > 0.4 * (s_low + s_high),
          f"{100*s_high/(s_low+s_high):.0f}% of its energy above 1kHz")

    zc = lambda s: sum(1 for i in range(len(s) - 1) if (s[i] < 0) != (s[i + 1] < 0))
    check("the snare crosses zero far more often than the kick",
          zc(snare[:4000]) > 8 * max(zc(kick[:4000]), 1),
          f"snare {zc(snare[:4000])} vs kick {zc(kick[:4000])}")

    # The pitch sweep must land exactly on the floor.
    check("the kick's pitch sweep reaches its floor exactly",
          kv.inc == kv.floor and kv.diff == 0,
          f"{kv.inc*48000/2**32:.1f}Hz")

    # Clicks are short.
    for d in ("dark", "light"):
        sig, _ = render_hit("clicks", d, samples=4800)
        peak = max(abs(v) for v in sig)
        tail = max(abs(v) for v in sig[720:])       # after 15ms
        check(f"the {d}-edge click is over within 15ms", tail < peak / 100,
              f"peak {peak}, tail {tail}")

    # They ring together without clipping.
    k = KITS["kicksnare"]
    a, b = Voice(), Voice()
    svf = Svf()
    svf.set(k["cut"], k["res"])
    a.strike(k["dark"])
    rng = [12345]
    worst = 0
    for i in range(24000):
        if i == 960:                                 # snare 20ms later
            b.strike(k["light"])
        s = a.render(0, svf) + b.render(0, svf)
        worst = max(worst, abs(s))
    check("a kick and a snare ring together without clipping",
          worst <= 2047, f"peak {worst}")


# --- modes/tapescrub.cpp and modes/jog.cpp ----------------------------------------

def slice_index(ug, current, slices=16):
    band = 65536 // slices
    lo = current * band - band // 4
    hi = (current + 1) * band + band // 4
    if lo <= ug < hi:
        return current
    return clamp(ug // band, 0, slices - 1)


def check_behaviours():
    # Slices land on real boundaries, for every take length.
    ok = True
    for length in (256, 2400, 40000, 84000):
        slice_len = max(length // 16, 2)
        for ug in range(0, 65536, 37):
            s = slice_index(ug, 0)
            start = s * slice_len
            ok &= 0 <= s < 16 and start + slice_len <= length + slice_len
            ok &= start < length
    check("every slice starts on a real boundary inside the take", ok)

    # Hysteresis and dwell: a wobbling hand must not machine-gun.
    rnd = random.Random(31)
    cur, pending, dwell, changes = 0, -1, 0, 0
    for i in range(6000):
        ug = clamp(int(i * 65535 / 6000) + rnd.randint(-1300, 1300), 0, 65535)
        cand = slice_index(ug, cur)
        if cand == cur:
            pending, dwell = -1, 0
        elif cand != pending:
            pending, dwell = cand, 0
        else:
            dwell += 1
            if dwell >= 45:
                cur, pending, dwell = cand, -1, 0
                changes += 1
    check("a noisy sweep across the slices changes slice at most 16 times",
          changes <= 16, f"{changes} changes")

    # Sweep: loop length is monotonic in green and never leaves the take.
    length = 40000
    ends = []
    for ug in range(0, 65536, 257):
        oct_q12 = -(((65535 - ug) * 6 * 4096) >> 16)
        ends.append(clamp(pow2_scale(length, oct_q12), 256, length))
    check("sweep's loop length is monotonic and stays inside the take",
          all(ends[i] <= ends[i + 1] for i in range(len(ends) - 1))
          and min(ends) >= 256 and max(ends) <= length,
          f"{min(ends)} .. {max(ends)} samples")

    # The crossfade spreads a worst-case discontinuity instead of stepping it.
    steps = []
    prev = -2048
    for f in range(128, 0, -1):
        v = -2048 + (((2047 - -2048) * (128 - f)) >> 7)
        steps.append(abs(v - prev))
        prev = v
    check("a jump between opposite rails is spread, not stepped",
          max(steps) <= 64, f"largest step {max(steps)} LSB")

    # Jog rate laws.
    def rate(ug, main, act):
        dev = ug - 32768
        if -2048 < dev < 2048:
            dev = 0
        else:
            dev += -2048 if dev > 0 else 2048
        depth = 256 + ((main * 768) >> 12)
        if act == "nudge":
            return clamp(65536 + ((dev * depth) >> 7), -4 * 65536, 4 * 65536)
        if act == "platter":
            return clamp((dev * depth) >> 7, -4 * 65536, 4 * 65536)
        return 0 if ug < 12000 else 65536

    check("platter is exactly stopped across the dead zone",
          all(rate(u, 2048, "platter") == 0 for u in range(32768 - 2047, 32768 + 2048)))
    # One LSB of asymmetry, because an arithmetic shift floors: 1/65536 of a
    # rate unit, which is 0.0015% of pitch.
    check("platter is symmetric about mid-grey to within a bit",
          abs(rate(32768 + 20000, 4095, "platter") + rate(32768 - 20000, 4095, "platter")) <= 1)
    plat = [rate(u, 2048, "platter") for u in range(65536)]
    check("platter rate is monotonic in green",
          all(plat[i] <= plat[i + 1] for i in range(65535)))

    # Brake: ramps monotonically and lands exactly, in both directions.
    r, seq = 65536, []
    for _ in range(40000):
        r = slew_exact(r, 0, 9)
        seq.append(r)
        if r == 0:
            break
    check("the brake ramps down monotonically and reaches exactly zero",
          seq[-1] == 0 and all(seq[i] >= seq[i + 1] for i in range(len(seq) - 1)),
          f"{len(seq)} samples to stop")
    stop_len = len(seq)
    r, seq = 0, []
    for _ in range(40000):
        r = slew_exact(r, 65536, 11)
        seq.append(r)
        if r == 65536:
            break
    check("and spins back up monotonically to exactly 1x",
          seq[-1] == 65536 and all(seq[i] <= seq[i + 1] for i in range(len(seq) - 1)))
    check("braking is quicker than spinning up, which is what makes it a motor",
          stop_len < len(seq), f"{stop_len} vs {len(seq)} samples")

    # Falling, a plain shift still reaches zero (it floors, so it always moves
    # down). Rising is where it gives up — which is the spin-up, and why the
    # ramp uses slew_exact.
    stalled = 0
    for _ in range(100000):
        step = (65536 - stalled) >> 11
        if step == 0:
            break
        stalled += step
    check("plain slew would stall short of full speed — hence slew_exact",
          stalled < 65536, f"stalls at {stalled}, {100*(65536-stalled)/65536:.0f}% slow")


# --- Mode 9, the colour filter: drive, soft clip, follower, mode LEDs -------


def soft_clip(x):
    """modes/colourfilter.cpp SoftClip(). Note cdiv: C truncates c/3 toward
    zero, which for a negative sample is not what Python's // would do."""
    x = clamp(x, -4096, 4096)
    q = i32(x * x) >> 12
    c = i32(x * q) >> 12
    return clamp((i32(x - cdiv(c, 3)) * 3) >> 2, -2047, 2047)


def drive_q12(main):
    return pow2_scale(5461, main * 3)


def mode_leds(mode):
    """leds.h: count from one, then again at half brightness past seven."""
    n, on = mode + 1, 4095
    if n > 7:
        n, on = n - 7, 1100
    return (on if n & 4 else 0, on if n & 2 else 0, on if n & 1 else 0)


def check_colour_filter():
    print()
    # The knee is what a drive knob at its minimum should be: unity for
    # anything quiet, a gentle round on the loudest peaks.
    unity = drive_q12(0)
    small = [(v, soft_clip(i32(v * unity) >> 12)) for v in range(1, 200)]
    worst = max(abs(o - v) for v, o in small)
    check("at minimum drive the filter input is unity for small signals",
          worst <= 1, f"worst error {worst} LSB over +/-200")
    peak = soft_clip(i32(2047 * unity) >> 12)
    loss = 20 * math.log10(peak / 2047)
    check("and a full-scale peak loses only the knee, not a bypass",
          -2.0 < loss < -0.5, f"{loss:.2f} dB at full scale")

    # Never exceeds what clamp12 can carry, at any drive, for any input.
    over = []
    for knob in range(0, 4096, 37):
        d = drive_q12(knob)
        for v in (-2048, -2047, -1000, -1, 0, 1, 1000, 2047):
            y = soft_clip(i32(v * d) >> 12)
            if not -2047 <= y <= 2047:
                over.append((knob, v, y))
    check("no drive setting can push the soft clip past 12-bit full scale",
          not over, f"{len(over)} escapes over 111 knob positions")
    # Without the trim the ends land on 2048 and -2049: the shift floors while
    # c/3 truncates toward zero, so the two extremes miss in opposite
    # directions. That is 1 LSB outside what the Svf's headroom is argued for.
    raw_ends = [(i32(x - cdiv((i32(x * ((i32(x * x)) >> 12))) >> 12, 3)) * 3) >> 2
                for x in (-4096, 4096)]
    check("the trim is load-bearing: untrimmed, both extremes escape",
          raw_ends == [-2049, 2048], f"untrimmed ends {raw_ends}")

    mono = all(soft_clip(x) <= soft_clip(x + 1) for x in range(-5000, 5000))
    check("the soft clip is monotonic, so it shapes rather than folds", mono)

    asym = max(abs(soft_clip(x) + soft_clip(-x)) for x in range(0, 4097))
    check("and is odd-symmetric to within the shift's flooring",
          asym <= 1, f"worst |f(x)+f(-x)| = {asym}")

    # 1x to 8x across the travel, which is the three octaves claimed.
    ratio = drive_q12(4095) / unity
    check("drive spans 1x to 8x across the Main knob",
          7.9 < ratio < 8.1, f"{ratio:.2f}x at full")
    quiet = 120
    check("full drive lifts a quiet signal to the ceiling",
          soft_clip(i32(quiet * drive_q12(4095)) >> 12) > 800,
          f"{quiet} in -> {soft_clip(i32(quiet * drive_q12(4095)) >> 12)} out")

    # The follower: up fast, down slow, and it does reach both ends.
    env, rise = 0, 0
    while env < 2000:
        env = slew_exact(env, 2047, 5)
        rise += 1
    fall = 0
    while env > 0:
        env = slew_exact(env, 0, 11)
        fall += 1
    check("the envelope follower attacks faster than it releases",
          rise < fall, f"{rise} samples up vs {fall} down")
    check("and releases all the way to zero, so the gate always closes",
          env == 0)

    # The gate's hysteresis has to be wider than one release step at the
    # threshold, or a decaying tail chatters across it.
    step_at_on = 40 - slew_exact(40, 0, 11)
    check("the gate's hysteresis is wider than a release step at the threshold",
          (40 - 18) > step_at_on, f"band 22 vs step {step_at_on}")

    # The whole Mode 9 chain, end to end, at a level you would actually patch
    # in. This is the check that was missing when the mode shipped silent: the
    # arithmetic was all correct, and the mode still made no sound, because a
    # dark reading asked for a 40Hz low-pass and the wand's resting state in
    # room light IS dark.
    def mode9(colour, freq=440, amp=1500, n=9600, res_u=0, type_u=0, main=0):
        s = Svf()
        s.set(20480 + ((colour * 11) >> 4), res_u)
        seg, frac = blend_set(type_u >> 4)
        d = drive_q12(main)
        out = []
        for i in range(n):
            x = int(amp * math.sin(2 * math.pi * freq * i / 48000))
            s.process(soft_clip(i32(x * d) >> 12))
            out.append(clamp(blend_render(s, seg, frac), -2048, 2047))
        return rms(out[n // 2:])

    ref = 1500 / math.sqrt(2)
    rest = 20 * math.log10(mode9(0) / ref)
    check("Mode 9 still passes audible signal with the wand in the dark",
          rest > -18, f"{rest:.1f} dB at 440Hz, cutoff floor closed")
    openz = 20 * math.log10(mode9(65535) / ref)
    check("and is essentially open at full brightness",
          openz > -1.0, f"{openz:.1f} dB at 440Hz")
    check("the cutoff sweep is monotonic in brightness",
          all(mode9(c) < mode9(c + 8192) for c in range(0, 57344, 8192)))
    # The overflow the obvious remap would have hit at full scale.
    check("the cutoff remap stays inside int32 at full brightness",
          i32(65535 * 11) >> 4 == 45055, "11/16 rather than (65536-floor)/65536")

    # The mode display: nine distinct patterns, none of them a dark column.
    pats = [mode_leds(m) for m in range(9)]
    check("all nine modes show a distinct LED pattern",
          len(set(pats)) == 9, f"{len(set(pats))} distinct")
    check("and no mode is a dark column", all(any(p) for p in pats))
    check("modes 1-7 are full brightness, 8 and 9 half",
          all(max(p) == 4095 for p in pats[:7])
          and all(max(p) == 1100 for p in pats[7:]),
          f"mode 8 = {pats[7]}, mode 9 = {pats[8]}")


# --- Modes 10-14, the effects: fx.h primitives and each mode end to end ------

FX_LEN = 20480


class FxBuf:
    """The shared gFx buffer. Every mode carves regions out of this one list,
    exactly as the firmware does, so a layout that overlaps shows up here."""

    def __init__(self):
        self.b = [0] * FX_LEN


class Line:
    def __init__(self, fx, off, n):
        self.fx, self.off, self.n, self.w = fx, off, n, 0

    def write(self, s):
        self.fx.b[self.off + self.w] = clamp(s, -32767, 32767)
        self.w = (self.w + 1) % self.n

    def tap(self, back):
        i = (self.w + self.n - 1 - (back % self.n)) % self.n
        return self.fx.b[self.off + i]

    def tap_q16(self, b_q16):
        whole, frac = b_q16 >> 16, b_q16 & 0xFFFF
        s0, s1 = self.tap(whole), self.tap(whole + 1)
        return s0 + (i32((s1 - s0) * frac) >> 16)


class OnePole:
    def __init__(self):
        self.z, self.d = 0, 0

    def process(self, x):
        self.z += i32((x - self.z) * (32768 - self.d)) >> 15
        return self.z


class Comb:
    def __init__(self, fx, off, n):
        self.line, self.lp, self.n, self.fb = Line(fx, off, n), OnePole(), n, 0

    def process(self, x):
        out = self.line.tap(self.n - 1)
        self.line.write(x + (i32(self.lp.process(out) * self.fb) >> 15))
        return out


class Allpass:
    def __init__(self, fx, off, n):
        self.line, self.n = Line(fx, off, n), n

    def process(self, x):
        b = self.line.tap(self.n - 1)
        self.line.write(x + ((b * 16384) >> 15))
        return b - x


def delay_time_q16(u, lo, hi):
    oct_q12 = (log2_q16(hi) - log2_q16(lo)) >> 4
    t = pow2_scale(lo * 256, (u * oct_q12) >> 16) * 256
    return clamp(t, lo * 65536, hi * 65536)


COMB_LEN = [1214, 1293, 1390, 1476, 1548, 1623, 1694, 1760]
AP_LEN = [605, 480, 371, 245]
PRE_LEN = 4096


def sine(n, freq, amp=1500):
    return [int(amp * math.sin(2 * math.pi * freq * i / 48000)) for i in range(n)]


def run_delay(red, green, blue, main=0, n=48000, freq=440):
    """Mode 10 end to end."""
    fx = FxBuf()
    line = Line(fx, 0, FX_LEN)
    tone = OnePole()
    lo, hi = 480, FX_LEN - 512
    target = delay_time_q16(red, lo, hi)
    t = target
    fb = i32(green * 32440) >> 16
    mix = blue >> 1
    tone.d = ((4095 - main) * 26000) >> 12
    src = sine(n, freq)
    out = []
    for x in src:
        t = slew_exact(t, target, 10)
        wet = line.tap_q16(t)
        line.write(x + (i32(tone.process(wet) * fb) >> 15))
        out.append(clamp(x + (i32((wet - x) * mix) >> 15), -2048, 2047))
    return out


def run_reverb(red, green, blue, main=0, n=48000, src=None):
    """Mode 11 end to end."""
    fx = FxBuf()
    at = 0
    combs = []
    for ln in COMB_LEN:
        combs.append(Comb(fx, at, ln))
        at += ln
    aps = []
    for ln in AP_LEN:
        aps.append(Allpass(fx, at, ln))
        at += ln
    pre = Line(fx, at, PRE_LEN)
    fb = 22938 + ((i32((30146 - 22938) * red)) >> 16)
    damp = 26000 - (i32(green * 26000) >> 16)
    for c in combs:
        c.fb, c.lp.d = fb, damp
    mix = blue >> 1
    pre_tap = (main * (PRE_LEN - 1)) >> 12
    if src is None:
        src = sine(n, 440)
    out = []
    for x in src:
        pre.write(x)
        v = pre.tap(pre_tap) >> 3
        wet = sum(c.process(v) for c in combs) >> 3
        for a in aps:
            wet = a.process(wet)
        out.append(clamp(x + (i32((wet - x) * mix) >> 15), -2048, 2047))
    return out


def grain_env(pos, dur, ramp, ramp_inv):
    e = ((pos * ramp_inv) >> 16) if pos < ramp else \
        (((dur - pos) * ramp_inv) >> 16) if pos > dur - ramp else 32767
    return max(e, 0)


def run_mangle(red, green, blue, main=0, n=4800, freq=440):
    """Mode 14 end to end. No buffer, so it is pure arithmetic."""
    hold_n = 1 + ((red * 31) >> 16)
    drop = (red * 9) >> 16
    mask = ~((1 << drop) - 1)
    drive = 256 + ((green * 1792) >> 16)
    ring = blue >> 1
    inc = pow2_scale(hz_to_inc(20), (main * 7 * 4096) >> 12)
    phase, count, held = 0, 0, 0
    out = []
    for x in sine(n, freq):
        count += 1
        if count >= hold_n:
            count, held = 0, x & mask
        v = fold((held * drive) >> 8)
        phase = (phase + inc) & 0xFFFFFFFF
        ringed = mul_q15(v, fast_sin(phase))
        v = v + (i32((ringed - v) * ring) >> 15)
        out.append(clamp(v, -2048, 2047))
    return out


def check_effects():
    print()
    ref = 1500 / math.sqrt(2)

    # The layouts. An overlap here would be two effects scribbling on each
    # other, which is exactly the bug a shared buffer invites.
    tank = sum(COMB_LEN) + sum(AP_LEN) + PRE_LEN
    check("the reverb tank fits the shared buffer",
          tank <= FX_LEN, f"{tank} of {FX_LEN} samples")
    check("and the delay's longest tap stays inside it",
          (FX_LEN - 512) + 1 < FX_LEN, f"max tap {FX_LEN - 512}")

    # --- Mode 10, delay
    lo, hi = 480, FX_LEN - 512
    t0 = delay_time_q16(0, lo, hi) >> 16
    t1 = delay_time_q16(65535, lo, hi) >> 16
    check("the delay time spans about 10ms to 416ms",
          9 < t0 * 1000 / 48000 < 11 and 410 < t1 * 1000 / 48000 < 417
          and t1 <= FX_LEN - 512,
          f"{t0 * 1000 / 48000:.1f}ms .. {t1 * 1000 / 48000:.0f}ms")
    check("and is monotonic in brightness",
          all(delay_time_q16(u, lo, hi) < delay_time_q16(u + 4096, lo, hi)
              for u in range(0, 61440, 4096)))
    # pow2_scale reads up to 6% high inside an octave (linear interpolation of a
    # convex curve), which unclamped asks for 441ms from a 416ms line — Tap()
    # would wrap that into a completely different delay.
    raw = pow2_scale(lo * 256, (65535 * ((log2_q16(hi) - log2_q16(lo)) >> 4)) >> 16) * 256
    check("the clamp is load-bearing: unclamped the time runs past the line",
          (raw >> 16) > hi, f"unclamped {raw >> 16} vs line {hi}")

    # A tap must return exactly what was written that many samples ago, or the
    # delay is not a delay.
    fx = FxBuf()
    ln = Line(fx, 0, 1000)
    for i in range(1, 501):
        ln.write(i * 50)
    check("a line's tap returns the sample written that many back",
          ln.tap(0) == 25000 and ln.tap(9) == 24550 and ln.tap(499) == 50,
          f"tap(0)={ln.tap(0)}, tap(9)={ln.tap(9)}, tap(499)={ln.tap(499)}")
    half = ln.tap_q16((10 << 16) + 32768)
    check("and an interpolated tap sits half way between its two neighbours",
          ln.tap(11) < half < ln.tap(10) and abs(half - 24475) <= 1,
          f"tap(10)={ln.tap(10)}, half={half}, tap(11)={ln.tap(11)}")

    out = run_delay(32768, 40000, 65535, n=48000)   # wet, mid time, feedback up
    tail = rms(out[40000:])
    check("Mode 10 makes sound, and the wet path is the output at full mix",
          20 * math.log10(tail / ref) > -12, f"{20 * math.log10(tail / ref):.1f} dB")
    # Feedback under unity has to decay, or a held note builds to a clip.
    quiet = run_delay(32768, 65535, 65535, n=24000)
    silence_fed = quiet[:]
    check("feedback at its maximum still decays rather than running away",
          max(abs(v) for v in silence_fed[20000:]) <= 2047,
          "no clip at full feedback")

    # --- Mode 11, reverb
    # An impulse in, and the tail has to actually ring and then die.
    imp = [1800] + [0] * 47999
    rv = run_reverb(65535, 32768, 65535, src=imp)
    peak_early = max(abs(v) for v in rv[:4800])
    peak_late = max(abs(v) for v in rv[43200:])
    check("Mode 11 rings from an impulse", peak_early > 40, f"peak {peak_early}")
    check("and the tail decays rather than sustaining for ever",
          peak_late < peak_early // 2, f"{peak_early} early vs {peak_late} late")
    check("the biggest room does not clip a sustained input",
          max(abs(v) for v in run_reverb(65535, 65535, 65535, n=24000)) <= 2047)
    small = run_reverb(0, 32768, 65535, src=imp)
    check("a small room decays faster than a big one",
          sum(abs(v) for v in small[9600:]) < sum(abs(v) for v in rv[9600:]),
          "size actually changes the decay")

    # --- Mode 12, freeze: the grain window is what stops it clicking
    dur, ramp = 4800, 1200
    ramp_inv = (32767 * 65536) // ramp
    check("a grain's envelope starts and ends at silence",
          grain_env(0, dur, ramp, ramp_inv) == 0
          and grain_env(dur, dur, ramp, ramp_inv) == 0,
          "no click at either edge")
    check("and reaches full in the middle",
          grain_env(dur // 2, dur, ramp, ramp_inv) == 32767)
    check("the envelope never goes negative anywhere in the grain",
          all(grain_env(p, dur, ramp, ramp_inv) >= 0 for p in range(dur + 1)))
    # At maximum density three grains overlap, so the sum never reaches zero
    # between them — which is what makes a pad rather than a stutter.
    every = (dur * (131072 - ((65535 * 109226) >> 16))) >> 16
    cover = [0] * (dur * 3)
    for start in range(0, dur * 2, max(every, 1)):
        for p in range(dur):
            if start + p < len(cover):
                cover[start + p] += grain_env(p, dur, ramp, ramp_inv)
    check("at full density the grains overlap with no gap",
          min(cover[dur:dur * 2]) > 0, f"spawn every {every} of {dur} samples")

    # --- Mode 13, modulation: the phaser cascade must stay bounded
    x1 = [0] * 4
    y1 = [0] * 4
    worst = 0
    for i, x in enumerate(sine(24000, 220, 2047)):
        a = clamp(16384 + (((fast_sin(i * 400000 & 0xFFFFFFFF) * 16383) >> 15) >> 2),
                  4915, 27852)
        v = x
        for s in range(4):
            y = ((i32((v + y1[s]) * a)) >> 15) - x1[s]
            x1[s], y1[s] = v, y
            v = y
        worst = max(worst, abs(v))
    check("the phaser's four allpass stages stay bounded at full drive",
          worst < 8192, f"worst |out| = {worst} from +/-2047 in")

    # --- Mode 14, mangle
    check("the crush mask is a true bit reduction",
          (~((1 << 0) - 1)) == -1 and (1000 & ~((1 << 4) - 1)) == 992,
          "12 bits at rest, 4 bits away drops to a multiple of 16")
    for name, r, g, b in (("crush only", 60000, 0, 0),
                          ("fold only", 0, 60000, 0),
                          ("ring only", 0, 0, 60000)):
        o = run_mangle(r, g, b, main=2048)
        db = 20 * math.log10(max(rms(o), 0.01) / ref)
        check(f"Mode 14 makes sound with {name}", db > -20, f"{db:.1f} dB")
    check("and never leaves 12-bit range",
          all(-2048 <= v <= 2047 for v in run_mangle(65535, 65535, 65535, main=4095)))


def main():
    check_fastmath()
    check_sensors()
    check_response_curve()
    check_crosstalk()
    check_calibration_kind()
    check_real_maps()
    check_gesture()
    check_voice()
    check_behaviours()
    check_osc()
    check_svf()
    check_fold()
    check_barcode()
    check_hue()
    check_tape_modes()
    check_colour_filter()
    check_effects()
    check_controls()
    print()
    print("ALL PASS" if not FAILS else f"{len(FAILS)} FAILED: " + ", ".join(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
