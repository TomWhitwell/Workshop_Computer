"""blackbird as an ii follower (crow address 4), led by a real crow.

Needs crow running tests/crow/ii_follow_driver.lua, on the same bus, and the
ii test patch: blackbird CV Out 1 -> crow input 1, crow output 1 -> blackbird
CV In 1. Skips if crow doesn't respond."""
from bbdbg import Skip

TOLERANCE = 0.3  # an uncalibrated Computer is a few % off (see test_ii.py)


def follow_stats(bb):
    w = bb.mem.read_words(bb.symbol("g_ii_follow_stats"), 5)
    return dict(zip(["commands", "queries_irq", "queries_lua", "two_step", "unknown"], w))


def round_(bb, n, wait=0.3):
    """Raise CV Out 1 to n volts: crow's driver runs round n."""
    bb.eval("output[1].volts = 0")
    bb.wait(0.1)
    bb.eval(f"output[1].volts = {n}")
    bb.wait(wait)


def setup(bb):
    bb.eval("""
        calls = {}
        ii.self.call = function(args) calls[#calls + 1] = args[1] end
        ii.self.query = function(args) return args[1] + 1 end  -- query answers are whole numbers (s16)
    """)
    before = follow_stats(bb)
    round_(bb, 1)
    if follow_stats(bb)["commands"] == before["commands"]:
        raise Skip("crow isn't driving blackbird (upload tests/crow/ii_follow_driver.lua to crow)")


def test_follow_commands_and_lua_query(bb):
    setup(bb)
    for n in (2, 3, 4, 5, 1):
        before = follow_stats(bb)
        bb.eval("calls = {}")
        round_(bb, n)
        # volts(2, n/2): crow's built-in ii.self.volts drives output 2
        assert abs(bb.query("output[2].volts") - n / 2) < 0.01, bb.query("output[2].volts")
        # call1(n) reached the script
        assert bb.query("#calls") == 1 and bb.query("calls[1]") == n, bb.query("table.concat(calls, ',')")
        # query1(n): Lua answered n + 1, crow put it on its output 1
        want = n + 1
        got = bb.query("input[1].volts")
        assert abs(got - want) < TOLERANCE, f"round {n}: crow mirrored {got}, want {want}"
        after = follow_stats(bb)
        assert after["commands"] - before["commands"] == 2, (before, after)
        assert after["queries_lua"] - before["queries_lua"] == 1, (before, after)
        assert after["unknown"] == before["unknown"]


def test_follow_builtin_output_getter(bb):
    setup(bb)
    for volts in (-3.0, 1.75, 4.0):
        bb.eval(f"output[2].volts = {volts}")
        bb.wait(0.05)
        before = follow_stats(bb)
        round_(bb, 6)
        got = bb.query("input[1].volts")
        assert abs(got - volts) < TOLERANCE, f"get('output', 2) at {volts}V: crow mirrored {got}"
        assert follow_stats(bb)["queries_irq"] - before["queries_irq"] == 1


def test_follow_survives_our_own_leading(bb):
    # Leading switches the controller away from following, then back
    setup(bb)
    bb.eval("for i = 1, 8 do ii.crow.volts(3, i / 4) end")  # to the real crow, at 0x01
    bb.wait(0.2)
    before = follow_stats(bb)
    round_(bb, 3)
    assert follow_stats(bb)["commands"] - before["commands"] == 2
    assert abs(bb.query("output[2].volts") - 1.5) < 0.01
