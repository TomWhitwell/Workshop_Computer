#!/usr/bin/env python3
"""simulate.py — sanity-check the physics engines' behaviour off-hardware.

A Python port of the integer math in engines.cpp (same Q16 fixed point, same
xorshift, same constants), used to confirm each mode behaves musically across
the knob range before committing to a hardware trip.

This is a MODEL of engines.cpp, not the code itself — if you change the C++,
change this too, or delete it. It exists because there is no host C++ compiler
on this machine (tools/simulate.cpp is the real harness if you get one).

    python tools/simulate.py          # gaits + sync curve + trigger rates
    python tools/simulate.py gaits    # gait footfall check only
"""

import math
import sys

Q = 65536
CTRL = 1500
MASK32 = 0xFFFFFFFF
NUM_AGENTS = 4
SWARM_SIZE = 12
SWARM_PER_AGENT = SWARM_SIZE // NUM_AGENTS


def xorshift(s):
    s ^= (s << 13) & MASK32
    s ^= s >> 17
    s ^= (s << 5) & MASK32
    return s & MASK32


def rand_q16(s):
    s = xorshift(s)
    return s, s >> 16


def rand_bipolar(s):
    s = xorshift(s)
    return s, (s >> 17) - 16384


def mul_q16(a, b):
    return (a * b) >> 16


def decay(v, shift):
    d = v >> shift
    return 0 if d == 0 else v - d


def fast_sqrt_q16(x):
    """Mirror of fast_sqrt_q16 in fastmath.h -- restoring bitwise integer sqrt.

    Deliberately transcribed rather than using math.sqrt, so this model fails the
    same way the firmware would if the algorithm is ever wrong."""
    if x <= 0:
        return 0
    v = x << 16
    res = 0
    bit = 1 << 30
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


def slew(v, target, shift):
    return v + ((target - v) >> shift)


SIN = [round(32767 * math.sin((i / 256) * (math.pi / 2))) for i in range(257)]


def fast_sin(phase):
    phase &= MASK32
    quad = phase >> 30
    frac = (phase >> 6) & 0xFFFFFF
    idx = frac >> 16
    mu = frac & 0xFFFF
    if quad & 1:
        idx = 255 - idx
        mu = 65536 - mu
        if mu == 65536:
            mu = 0
            idx += 1
    a, b = SIN[idx], SIN[idx + 1]
    v = a + (((b - a) * mu) >> 16)
    return -v if quad & 2 else v


# ===========================================================================
# Mode 1: Horses — one stride clock, real footfall offsets
# ===========================================================================
# Landing time within the stride, Q16. hoofPhase = stride - offset.
#              LH      LF      RH      RF
GAIT_OFFSET = [
    [0, 16384, 32768, 49152],   # walk   4-beat lateral
    [0, 32768, 32768, 0],       # trot   2-beat diagonal
    [0, 14418, 14418, 28836],   # canter 3-beat + suspension
    [0, 13763, 6554, 20315],    # gallop 4-beat rotary + float
]
HORSE_SPEED = [65536, 63700, 67600, 62000]
# Flam in control ticks for the second foot of a "simultaneous" pair. Two
# identical clops on the same sample sum into one louder clop, so trot and
# canter audibly halved in density until these were added.
FLAM_TICKS = [
    [0, 0, 0, 0],     # walk   - already four distinct landings
    [0, 27, 0, 18],   # trot
    [0, 24, 0, 0],    # canter
    [0, 0, 0, 0],     # gallop - already four distinct landings
]
GAIT_HZ_MIN = [115, 205, 300, 380]
GAIT_HZ_MAX = [205, 330, 420, 640]
GAIT_NAME = ["Walk", "Trot", "Canter", "Gallop"]
LEG_NAME = ["LH", "LF", "RH", "RF"]


def horses(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    """A HERD: one stride clock per horse, each keeping all four hooves."""
    rng = 0x1234
    gait = min(physics >> 14, 3)
    within = (physics & 0x3FFF) << 2
    hz = GAIT_HZ_MIN[gait] + mul_q16(within, GAIT_HZ_MAX[gait] - GAIT_HZ_MIN[gait])
    if clock_period:
        ch = (256 * CTRL) // clock_period
        if ch > 32:
            hz = ch
    base = (hz * (1 << 32)) // (256 * CTRL)
    stride = [h * 0x30000000 for h in range(4)]
    last = [[0xFF] * 4 for _ in range(4)]
    jit = [[0] * 4 for _ in range(4)]
    pend = [[0] * 4 for _ in range(4)]
    fires = [0] * 4
    for t in range(ticks):
        if spook_every and t % spook_every == 0 and t:
            stride = [0] * 4
            last = [[0xFF] * 4 for _ in range(4)]
        for h in range(pop):
            inc = (base * HORSE_SPEED[h]) >> 16
            stride[h] = (stride[h] + inc) & MASK32
            for i in range(4):
                hp = (stride[h] - (GAIT_OFFSET[gait][i] << 16)) & MASK32
                if chaos:
                    rng, rb = rand_bipolar(rng)
                    jit[h][i] = slew(jit[h][i], mul_q16(rb, chaos), 5)
                    hp = (hp + (jit[h][i] << 10)) & MASK32
                step = hp >> 28
                if step == 0 and last[h][i] != 0:
                    if FLAM_TICKS[gait][i] == 0:
                        fires[h] += 1
                    else:
                        pend[h][i] = FLAM_TICKS[gait][i]
                last[h][i] = step
                if pend[h][i] > 0:
                    pend[h][i] -= 1
                    if pend[h][i] == 0:
                        fires[h] += 1
    return fires


def check_gaits():
    """Print each gait's footfall order and verify it biomechanically."""
    print("Gait footfall order within one stride")
    print("(landing time as a fraction of the stride)\n")
    ok = True
    for g in range(4):
        lands = sorted((GAIT_OFFSET[g][i] / Q, LEG_NAME[i]) for i in range(4))
        seq = "  ".join(f"{leg}@{p:.2f}" for p, leg in lands)
        ps = [p for p, _ in lands] + [lands[0][0] + 1.0]
        float_pct = max(ps[i + 1] - ps[i] for i in range(4)) * 100
        print(f"  {GAIT_NAME[g]:<7} {seq}   float {float_pct:.0f}%")

        order = [leg for _, leg in lands]
        if g == 0:    # walk: 4-beat lateral, same-side legs consecutive
            if order != ["LH", "LF", "RH", "RF"]:
                print("      FAIL: walk must be lateral LH-LF-RH-RF")
                ok = False
        elif g == 1:  # trot: diagonal pairs together, half a stride apart
            pairs = {}
            for p, leg in lands:
                pairs.setdefault(p, []).append(leg)
            if sorted(map(sorted, pairs.values())) != [["LF", "RH"], ["LH", "RF"]]:
                print("      FAIL: trot pairs must be diagonals")
                ok = False
        elif g == 2:  # canter: 3 beats (one simultaneous pair) + suspension
            if len({p for p, _ in lands}) != 3:
                print("      FAIL: canter must have exactly 3 beats")
                ok = False
            if float_pct < 40:
                print("      FAIL: canter needs a real suspension phase")
                ok = False
        elif g == 3:  # gallop: both hinds before both fores, long float
            hinds = [i for i, leg in enumerate(order) if leg in ("LH", "RH")]
            fores = [i for i, leg in enumerate(order) if leg in ("LF", "RF")]
            if not max(hinds) < min(fores):
                print("      FAIL: gallop is rotary - both hinds precede fores")
                ok = False
            if float_pct < 50:
                print("      FAIL: gallop needs a long suspension")
                ok = False
    print("\n  " + ("All gaits biomechanically correct."
                    if ok else "SOME GAITS ARE WRONG - see FAIL lines above."))
    return ok


# ===========================================================================
# Mode 2: Geese — contagion across a swarm folded onto 4 outputs
# ===========================================================================
def geese(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    excite = [0] * SWARM_SIZE
    refr = [0] * SWARM_SIZE
    rng = 0x1234
    fires = [0] * 4
    REFRACTORY = CTRL // 6
    flock = pop * SWARM_PER_AGENT
    spark = (2 + (chaos >> 12)) * (NUM_AGENTS + 1 - pop)
    contagion = (physics + mul_q16(physics, physics)) >> 1
    nudge = Q // 16
    for t in range(ticks):
        if spook_every and t % spook_every == 0 and t:
            for i in range(flock):
                excite[i] = Q
        if clock_period and t % clock_period == 0 and t:
            for i in range(flock):
                excite[i] = min(excite[i] + Q // 3, Q)
        fired = 0
        for i in range(flock):
            if refr[i] > 0:
                refr[i] -= 1
            else:
                p = spark + (mul_q16(excite[i], contagion) >> 6)
                rng, r = rand_q16(rng)
                if r < p:
                    fired |= 1 << i
                    refr[i] = REFRACTORY
        if fired:
            for i in range(flock):
                if not (fired & (1 << i)):
                    continue
                for j in range(flock):
                    if j != i:
                        excite[j] = min(excite[j] + nudge, Q)
        for a in range(pop):
            for k in range(SWARM_PER_AGENT):
                if fired & (1 << (a * SWARM_PER_AGENT + k)):
                    fires[a] += 1
                    break
        for i in range(flock):
            excite[i] = decay(excite[i], 9)
    return fires


# ===========================================================================
# Mode 3: Frogs — Kuramoto, optionally entrained to an external clock
# ===========================================================================
DETUNE = [0, 3000, -2200, 5000]


# Frogs coupling curve. MIRRORS kCouple[] in engines.cpp - a geometric sweep of
# K from 0.40 down to 0.004, because the pond is locked above K~0.25 and
# scattered below K~0.03, so the whole audible transition is under one octave of
# K. The cube this replaced spent half the knob above K=0.125 (all locked).
FROG_COUPLE = [26214, 14741, 8290, 4662, 2621, 1474, 829, 466, 262]


def frog_k(physics):
    """Knob Main -> Kuramoto coupling K, Q16. Linear interp on a Q16 table."""
    x = max(0, min(Q, physics))
    idx = (x * 8) >> 16
    if idx > 7:
        idx = 7
    frac = (x * 8) - (idx << 16)
    return FROG_COUPLE[idx] + mul_q16(FROG_COUPLE[idx + 1] - FROG_COUPLE[idx], frac)


def frogs(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    rng = 0x1234
    phase = []
    for _ in range(4):
        rng = xorshift(rng)
        phase.append(rng)
    fires = [0] * 4
    base_hz = 400
    if clock_period:
        ch = (256 * CTRL) // clock_period
        if 32 < ch < 8192:
            base_hz = ch
    base_inc = (base_hz * (1 << 32)) // (256 * CTRL)
    K = frog_k(physics)
    nat = [base_inc + ((base_inc >> 4) * mul_q16(d, chaos) >> 12) for d in DETUNE]
    n = max(pop, 1)
    clock_phase = 0
    for t in range(ticks):
        if spook_every and t % spook_every == 0 and t:
            for i in range(4):
                rng = xorshift(rng)
                phase[i] = rng
        if clock_period:
            clock_phase = (clock_phase + base_inc) & MASK32
            if t % clock_period == 0:
                clock_phase = 0
        for i in range(n):
            coup = 0
            partners = 0
            for j in range(n):
                if j != i:
                    coup += fast_sin((phase[j] - phase[i]) & MASK32)
                    partners += 1
            if clock_period:
                coup += fast_sin((clock_phase - phase[i]) & MASK32)
                partners += 1
            if partners > 1:
                coup = int(coup / partners)
            adjust = (nat[i] * ((coup * (K >> 1)) >> 15)) >> 16
            before = phase[i]
            phase[i] = (phase[i] + nat[i] + adjust) & MASK32
            if phase[i] < before:
                fires[i] += 1
    return fires


def frogs_order(physics, chaos=Q // 2, ticks=CTRL * 15):
    """Kuramoto order parameter R: 1.0 = locked, 0 = scattered."""
    rng = 0x1234
    phase = []
    for _ in range(4):
        rng = xorshift(rng)
        phase.append(rng)
    base_inc = (400 * (1 << 32)) // (256 * CTRL)
    K = frog_k(physics)
    nat = [base_inc + ((base_inc >> 4) * mul_q16(d, chaos) >> 12) for d in DETUNE]
    acc = []
    for t in range(ticks):
        for i in range(4):
            coup = 0
            for j in range(4):
                if j != i:
                    coup += fast_sin((phase[j] - phase[i]) & MASK32)
            coup = int(coup / 3)
            phase[i] = (phase[i] + nat[i]
                        + ((nat[i] * ((coup * (K >> 1)) >> 15)) >> 16)) & MASK32
        if t > ticks // 2:
            xs = sum(math.cos(2 * math.pi * p / 2 ** 32) for p in phase)
            ys = sum(math.sin(2 * math.pi * p / 2 ** 32) for p in phase)
            acc.append(math.hypot(xs, ys) / 4)
    return sum(acc) / len(acc)


# ===========================================================================
# Mode 4: Rain — leaky integrate-and-fire with cross-feeding splash
# ===========================================================================
THRESH = [Q, (Q * 115) // 100, (Q * 88) // 100, (Q * 103) // 100]
LEAK_BIAS = [0, -1, 1, 0]
SPLASH = Q // 12
METEOR_GAIN_MIN = Q // 5
METEOR_GAIN_SPAN = Q - Q // 5

RAIN_FLOOR = 9000
RAIN_GAIN_MIN = (Q * 40) // 100
RAIN_GAIN_SPAN = (Q * 40) // 100


def rain(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    level = [0] * 4
    rng = 0x1234
    fires = [0] * 4
    x = physics
    downpour = min(RAIN_FLOOR + mul_q16(Q - RAIN_FLOOR, fast_sqrt_q16(x)), Q)
    rain_gain = RAIN_GAIN_MIN + mul_q16(RAIN_GAIN_SPAN, x)
    leak_base = 10 - (chaos * 3 // Q)
    for t in range(ticks):
        if spook_every and t % spook_every == 0 and t:
            for i in range(4):
                rng, r = rand_q16(rng)
                level[i] += (Q >> 1) + (r >> 1)
        if clock_period and t % clock_period == 0 and t:
            for i in range(4):
                level[i] += Q // 6
        splash = [0] * 4
        for i in range(pop):
            rng, r = rand_q16(rng)
            drop = max(mul_q16(mul_q16(mul_q16(mul_q16(r, r), r), downpour),
                               rain_gain) >> 4, 0)
            level[i] += drop
            shift = max(5, min(12, leak_base + LEAK_BIAS[i]))
            level[i] = decay(level[i], shift)
            if level[i] >= THRESH[i]:
                fires[i] += 1
                level[i] = 0
                nxt = i + 1 if i + 1 < pop else 0
                if nxt != i:
                    splash[nxt] += SPLASH
        for i in range(pop):
            level[i] += splash[i]
    return fires


# ===========================================================================
# Mode 5: Meteors — inhomogeneous Poisson over a swarm
# ===========================================================================
def meteors(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    rng = 0x1234
    density = 0
    target = 0
    until = 1
    fires = [0] * 4
    for t in range(ticks):
        until -= 1
        if until == 0:
            rng, target = rand_q16(rng)
            rng = xorshift(rng)
            until = CTRL + (rng % (3 * CTRL))
        density = slew(density, target, 9)
        if spook_every and t % spook_every == 0 and t:
            density = Q
        if clock_period and t % clock_period == 0 and t:
            density = min(density + Q // 4, Q)
        if physics < Q // 2:
            depth, floor = physics * 2, 0
        else:
            tt = (physics - Q // 2) * 2
            depth, floor = Q - tt, tt
        eff = floor + mul_q16(density, depth)
        meteor_gain = METEOR_GAIN_MIN + mul_q16(METEOR_GAIN_SPAN, physics)
        p = mul_q16(mul_q16(eff, eff), meteor_gain) >> 8
        if chaos:
            rng, r = rand_q16(rng)
            p += mul_q16(mul_q16(r, chaos), meteor_gain) >> 11
        for a in range(pop):
            for k in range(SWARM_PER_AGENT):
                rng, r = rand_q16(rng)
                if r < p:
                    fires[a] += 1
                    break
    return fires


# ===========================================================================
# Mode 6: Cicadas - density-dependent chorus (amplitude coupling)
# ===========================================================================
def cicadas(physics, chaos, pop, ticks, spook_every=0, clock_period=0):
    """Patched chorus: 4 patches of 3, each hearing mostly itself.

    One shared field made this a relaxation oscillator - everyone recovers,
    everyone calls, everyone tires, repeat - which sounded like a gallop.
    Patches swell out of step, so the whole field is irregular.
    """
    PATCHES, PER = 4, 3
    rng = 0x1234 | 1
    phase, rec, sta, tem = [], [], [], []
    for _ in range(SWARM_SIZE):
        rng = xorshift(rng)
        phase.append(rng)
    for _ in range(SWARM_SIZE):
        rng = xorshift(rng); rec.append(11 + (rng % 3))
        rng = xorshift(rng); sta.append(Q // 2 + (rng % Q))
        rng = xorshift(rng); tem.append(Q * 42 // 64 + (rng % (Q * 58 // 64)))
    fat = [0] * SWARM_SIZE
    pf = [0] * PATCHES
    fires = [0] * 4
    swarm = pop * SWARM_PER_AGENT
    # A footstep is its OWN state, not a slam of fatigue: a fully fatigued
    # insect still calls at a quarter rate, so the old spook read as a dip
    # rather than a hush. Mirrors startle_ in engines.cpp.
    startle = 0
    for t in range(ticks):
        if spook_every and t % spook_every == 0 and t:
            startle = Q
        startle = decay(startle, 12)
        if clock_period and t % clock_period == 0 and t:
            for p in range(PATCHES):
                pf[p] = min(pf[p] + Q // 4, Q)
        base = 600 + mul_q16(600 * 5 // 4, physics)
        glob = sum(pf) // PATCHES
        called = [0] * PATCHES
        fired = 0
        for i in range(swarm):
            p = i // PER
            drive = (pf[p] * 3 + glob) >> 2
            rs = Q + mul_q16(mul_q16(drive, physics), Q) * 6
            hz = mul_q16(base << 4, rs) >> 4
            tired = Q - (fat[i] * 3 // 4)
            if startle > 0:
                tired = mul_q16(tired, Q - startle)
            my = mul_q16(mul_q16(hz, tem[i]), tired)
            if chaos:
                rng, rb = rand_bipolar(rng)
                my += (my >> 3) * mul_q16(rb << 1, chaos) >> 15
            my = max(my, 16)
            inc = (my * (1 << 32)) // (256 * CTRL)
            b = phase[i]
            phase[i] = (phase[i] + inc) & MASK32
            if phase[i] < b:
                called[p] += 1
                fired |= 1 << i
                fat[i] = min(fat[i] + mul_q16(mul_q16(Q // 2, physics), sta[i]), Q)
            fat[i] = min(fat[i] + mul_q16(mul_q16(drive, physics), Q // 150), Q)
            fat[i] = decay(fat[i], rec[i])
        for p in range(PATCHES):
            if p * PER >= swarm:
                pf[p] = 0
                continue
            inst = min(called[p] * Q * 8 // PER, Q)
            pf[p] = slew(pf[p], inst, 4) if inst > pf[p] else slew(pf[p], inst, 8)
        for a in range(pop):
            for k in range(SWARM_PER_AGENT):
                if fired & (1 << (a * SWARM_PER_AGENT + k)):
                    fires[a] += 1
                    break
    return fires


def cicadas_swing(physics, secs=30):
    """Swing of ONE patch. The mean across patches is deliberately flat: the
    patches swell independently (correlation ~0.01), and that cancellation is
    exactly what stops the field pulsing like a gallop."""
    PATCHES, PER = 4, 3
    rng = 0x1234 | 1
    phase, rec, sta, tem = [], [], [], []
    for _ in range(SWARM_SIZE):
        rng = xorshift(rng)
        phase.append(rng)
    for _ in range(SWARM_SIZE):
        rng = xorshift(rng); rec.append(11 + (rng % 3))
        rng = xorshift(rng); sta.append(Q // 2 + (rng % Q))
        rng = xorshift(rng); tem.append(Q * 42 // 64 + (rng % (Q * 58 // 64)))
    fat = [0] * SWARM_SIZE
    pf = [0] * PATCHES
    hist = []
    for t in range(secs * CTRL):
        base = 600 + mul_q16(600 * 5 // 4, physics)
        glob = sum(pf) // PATCHES
        called = [0] * PATCHES
        for i in range(SWARM_SIZE):
            p = i // PER
            drive = (pf[p] * 3 + glob) >> 2
            rs = Q + mul_q16(mul_q16(drive, physics), Q) * 6
            hz = mul_q16(base << 4, rs) >> 4
            tired = Q - (fat[i] * 3 // 4)
            my = max(mul_q16(mul_q16(hz, tem[i]), tired), 16)
            inc = (my * (1 << 32)) // (256 * CTRL)
            b = phase[i]
            phase[i] = (phase[i] + inc) & MASK32
            if phase[i] < b:
                called[p] += 1
                fat[i] = min(fat[i] + mul_q16(mul_q16(Q // 2, physics), sta[i]), Q)
            fat[i] = min(fat[i] + mul_q16(mul_q16(drive, physics), Q // 150), Q)
            fat[i] = decay(fat[i], rec[i])
        for p in range(PATCHES):
            inst = min(called[p] * Q * 8 // PER, Q)
            pf[p] = slew(pf[p], inst, 4) if inst > pf[p] else slew(pf[p], inst, 8)
        hist.append(pf[0])
    seg = hist[CTRL * 5:]
    return min(seg) / Q, max(seg) / Q


ENGINES = [("Horses", horses), ("Geese", geese), ("Frogs", frogs),
           ("Rain", rain), ("Meteors", meteors), ("Cicadas", cicadas)]


def rates():
    secs = 20
    ticks = secs * CTRL
    print(f"\nTrigger rates per agent (per second), {secs}s runs\n")
    warn = []
    for name, fn in ENGINES:
        print(f"{name}")
        for label, phys, chaos, pop, spook, clk in [
            ("physics 0.00", 0, 0, 4, 0, 0),
            ("physics 0.25", Q // 4, 0, 4, 0, 0),
            ("physics 0.50", Q // 2, 0, 4, 0, 0),
            ("physics 0.75", (3 * Q) // 4, 0, 4, 0, 0),
            ("physics 1.00", Q - 1, 0, 4, 0, 0),
            ("phys 0.5 chaos 1.0", Q // 2, Q - 1, 4, 0, 0),
            ("phys 0.5 +spook/s", Q // 2, 0, 4, CTRL, 0),
            ("phys 0.5 +clock 2Hz", Q // 2, 0, 4, 0, CTRL // 2),
            ("phys 0.5 pop=2", Q // 2, 0, 2, 0, 0),
        ]:
            f = fn(phys, chaos, pop, ticks, spook, clk)
            r = " ".join(f"{x / secs:6.2f}" for x in f)
            print(f"  {label:<21} {r}")
            if max(f) / secs > 30:
                warn.append(f"{name} {label}: {max(f)/secs:.0f}/s reads as a buzz")
        print()
    if warn:
        print("Warnings:")
        for w in warn:
            print("  " + w)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "gaits":
        check_gaits()
        return
    check_gaits()
    print("\nFrogs sync curve (Kuramoto order parameter)")
    for f in [0.0, 0.25, 0.5, 0.75, 1.0]:
        R = frogs_order(min(int(f * Q), Q - 1))
        print(f"  physics {f:.2f}  R={R:.3f}  {'#' * int(R * 40)}")
    print("\nCicadas field swell (coupling depth)")
    for f in [0.0, 0.25, 0.5, 0.75, 1.0]:
        lo, hi = cicadas_swing(min(int(f * Q), Q - 1))
        print(f"  physics {f:.2f}  field {lo:.2f}..{hi:.2f}  "
              f"swing {hi-lo:.2f}  {'#' * int((hi-lo) * 50)}")
    rates()


if __name__ == "__main__":
    main()
