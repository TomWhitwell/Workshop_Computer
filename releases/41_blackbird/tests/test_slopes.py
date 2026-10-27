"""Slope timing: instant, short and very long slopes."""
import time


def slope_rate(bb, seconds, wait=1.0):
    """Start a 0 -> 5V slope of `seconds` on output 1; measured volts/second."""
    bb.eval("output[1].volts = 0")
    bb.wait(0.1)
    bb.eval(f"output[1](to(5, {seconds}))")
    bb.wait(0.2)
    t1, v1 = time.monotonic(), bb.query("output[1].volts")
    bb.wait(wait)
    t2, v2 = time.monotonic(), bb.query("output[1].volts")
    return v1, (v2 - v1) / (t2 - t1)


def test_instant_and_short_slopes(bb):
    bb.eval("output[1].volts = 0")
    bb.wait(0.05)
    bb.eval("output[1](to(3, 0))")
    bb.wait(0.02)
    assert abs(bb.query("output[1].volts") - 3) < 0.01
    bb.eval("output[1](to(-2, 0.3))")
    bb.wait(0.1)
    mid = bb.query("output[1].volts")
    assert -2 < mid < 3, mid
    bb.wait(0.4)
    assert abs(bb.query("output[1].volts") + 2) < 0.01


def test_long_slopes_keep_their_time(bb):
    # Regression: slope times went through Q16.16 milliseconds, which overflow
    # past 32.8s: 40s and 1h slopes jumped straight to the destination, and a
    # 70s slope ran ~16x too fast. 40000s is past what Q16.16 seconds could hold.
    for seconds in (20, 40, 70, 3600, 40000):
        v1, rate = slope_rate(bb, seconds)
        want = 5 / seconds
        assert v1 < 0.5, f"to(5, {seconds}): already at {v1}V after 0.2s"
        assert abs(rate - want) <= 0.1 * want + 0.0005, \
            f"to(5, {seconds}): {rate:.5f} V/s, want {want:.5f}"


def test_long_slope_reaches_destination_and_calls_back(bb):
    # Just past the old 32.8s limit, where the slope used to finish (and call
    # back) immediately
    bb.eval("""
        output[1].volts = 0
        done_us = nil
        output[1].done = function() done_us = bb_dbg_us() end
    """)
    bb.wait(0.1)
    # (volts = 0 is an instant slope, and calls done too: clear that first)
    bb.eval("done_us = nil; start_us = bb_dbg_us(); output[1](to(5, 34))")
    bb.wait(2.0)
    assert bb.query("done_us == nil"), "callback fired early"
    assert 0.2 < bb.query("output[1].volts") < 0.4
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline and bb.query("done_us == nil"):
        bb.wait(0.5)
    assert bb.query("done_us ~= nil"), "no callback after 40s"
    took = bb.query("(done_us - start_us) / 1e6")
    assert abs(took - 34) < 0.1, took
    assert abs(bb.query("output[1].volts") - 5) < 0.01
