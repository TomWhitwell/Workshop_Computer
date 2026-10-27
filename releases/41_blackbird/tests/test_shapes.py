"""The 'over', 'under' and 'rebound' slope shapes follow crow's easings.

Regression: all three were placeholders that rendered as 'linear'."""


def under(t):
    return t * t * (2.70158 * t - 1.70158)


def over(t):
    x = t - 1
    return x * x * (2.70158 * x + 1.70158) + 1


def rebound(t):
    if t < 1 / 2.75:
        return 7.5625 * t * t
    if t < 2 / 2.75:
        x = t - 1.5 / 2.75
        return 7.5625 * x * x + 0.75
    if t < 2.5 / 2.75:
        x = t - 2.25 / 2.75
        return 7.5625 * x * x + 0.9375
    x = t - 2.625 / 2.75
    return 7.5625 * x * x + 0.984375


EASE = {"over": over, "under": under, "rebound": rebound}


def capture(bb, start, seconds):
    """Run Lua `start` on output 1 and sample output[1].volts every 10ms for
    `seconds`: [(seconds since start, volts)], timed by the card's clock."""
    bb.eval(f"""
        cap = {{}}
        local t0 = bb_dbg_us()
        {start}
        metro[1].event = function() cap[#cap + 1] = {{ (bb_dbg_us() - t0) / 1e6, output[1].volts }} end
        metro[1].time = 0.01
        metro[1]:start()
    """)
    bb.wait(seconds + 0.2)
    bb.eval("metro[1]:stop()")
    n = bb.query("#cap")
    return [(bb.query(f"cap[{i}][1]"), bb.query(f"cap[{i}][2]")) for i in range(1, n + 1)]


def worst_error(samples, expected, upto):
    return max(abs(v - expected(t)) for t, v in samples if t < upto)


def test_slope_shapes_follow_crows_easings(bb):
    for name, ease in EASE.items():
        bb.eval("output[1].volts = 0")
        bb.wait(0.1)
        samples = capture(bb, f"output[1].volts = 0 output[1](to(5, 2, '{name}'))", 2.0)
        err = worst_error(samples, lambda t: 5 * ease(min(t / 2, 1)), 2.0)
        assert err < 0.05, f"{name}: off crow's curve by up to {err:.3f}V"
        assert abs(samples[-1][1] - 5) < 0.01, f"{name} ends at {samples[-1][1]}"


TIMERAWL = 0x40054028  # RP2040 microsecond timer


def capture_dac(bb, start, seconds):
    """Run Lua `start`, then read output 1's value (what core 1 last wrote)
    over SWD for `seconds`, for audio-rate paths that output[1].volts doesn't
    see. SWD reads take milliseconds, so each sample comes with the window of
    time since the start it could belong to: [(earliest, latest, volts)]."""
    out = bb.symbol("_ZL17g_output_state_mv")  # main.cpp's static g_output_state_mv
    now = lambda: bb.mem.read_u32(TIMERAWL)
    before = now()
    bb.eval(start)
    after = now()
    samples = []
    while True:
        t1 = now()
        mv = bb.mem.read_u32(out)
        t2 = now()
        earliest = ((t1 - after) & 0xFFFFFFFF) / 1e6
        latest = ((t2 - before) & 0xFFFFFFFF) / 1e6
        if latest > seconds:
            return samples
        samples.append((earliest, latest, (mv - (1 << 32) if mv & 0x80000000 else mv) / 1000))


def outside_curve(samples, expected, tolerance):
    """Samples further than `tolerance` volts from every value `expected`
    takes in their time window."""
    bad = []
    for earliest, latest, v in samples:
        curve = [expected(earliest + (latest - earliest) * k / 20) for k in range(21)]
        if not min(curve) - tolerance <= v <= max(curve) + tolerance:
            bad.append((round(earliest, 3), round(latest, 3), v))
    return bad


def test_oscillate_shapes_follow_crows_easings(bb):
    for name, ease in EASE.items():
        # 0.25Hz, 4V: each 2s half sweeps -4 -> +4 (rising) then +4 -> -4 through the shape
        samples = capture_dac(bb, f"output[1](oscillate(0.25, 4, '{name}'))", 3.8)

        def expected(t):
            return -4 + 8 * ease(min(t / 2, 1)) if t < 2 else 4 - 8 * ease(min(t / 2 - 1, 1))

        assert len(samples) > 100, len(samples)
        bad = outside_curve(samples, expected, 0.03)
        assert not bad, f"oscillate '{name}': {len(bad)} samples off crow's curve, e.g. {bad[:3]}"
        bb.eval("output[1].volts = 0")
