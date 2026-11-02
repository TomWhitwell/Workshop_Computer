"""ii (I2C) leader on the UART pads.

The first tests need no ii hardware. The crow tests need a crow on the bus,
and patch cables: crow output 1 -> blackbird input 1, blackbird output 1 ->
crow input 1. Crow with no script loaded answers ii.crow.* (its defaults).
They skip if no crow answers."""
from bbdbg import Skip

RESULTS = ["idle", "busy", "done", "nak", "timeout", "error"]
NOBODY = 0x0F  # an address no ii device uses


def ii_results(bb):
    """How transactions have ended so far: {'done': n, 'nak': n, ...}."""
    w = bb.mem.read_words(bb.symbol("g_ii_results"), len(RESULTS))
    return dict(zip(RESULTS, w))


def ii_wait(bb, before, count, seconds=1.0):
    """Wait until `count` more transactions have ended; their outcomes."""
    for _ in range(int(seconds / 0.02)):
        now = ii_results(bb)
        delta = {k: now[k] - before[k] for k in RESULTS if now[k] != before[k]}
        if sum(delta.values()) >= count:
            return delta
        bb.wait(0.02)
    return delta


def test_ii_loads_on_first_use(bb):
    # ii.lua and the device tables (~7KB of Lua) load only when a script uses
    # ii, then stay (^^k keeps them): the size is only measurable the first time
    bb.eval("collectgarbage('collect')")
    first_use = bb.query("rawget(_G, 'ii') == nil")
    before = bb.stats()["lua_mem_bytes"]
    assert bb.query("type(ii.jf)") == "table"
    assert bb.query("type(ii.raw)") == "function"
    assert bb.query("rawget(ii.crow, '_name')") == "crow"
    if first_use:
        bb.eval("collectgarbage('collect')")
        grew = (bb.stats()["lua_mem_bytes"] - before) / 1024
        assert grew < 12, f"ii took {grew:.1f}KB"


def test_ii_survives_kill(bb):
    # Regression: ii.lua loads lazily, from inside a script, and its globals
    # (ii_LeadRx_handler, which delivers every reply) were recorded as the
    # script's, so the next ^^k deleted them and replies silently stopped.
    # ^^k now unloads ii on purpose (handlers and all, to free its memory):
    # the next use must load it whole again.
    assert bb.query("type(ii.crow)") == "table"
    bb.kill_lua()
    assert bb.query("rawget(_G, 'ii') == nil") is True
    assert bb.query("type(ii.crow)") == "table"
    assert bb.query("type(ii_LeadRx_handler)") == "function"


def test_ii_kill_clears_script_handlers(bb):
    # Regression: ii stays loaded across scripts (crow rebuilds Lua instead), so
    # a script's ii.<device>.event handler outlived it, and the next reply
    # called into the script's cleared globals (a runtime error per reply)
    bb.eval("ii.crow.event = function(e, v) replies = replies + 1 end; ii.jf.my_field = 1")
    bb.kill_lua()
    assert bb.query("rawget(ii.crow, 'event') == nil")
    assert bb.query("rawget(ii.jf, 'my_field') == nil")
    assert bb.query("rawget(ii.crow, '_name')") == "crow"
    assert bb.query("type(ii.crow.event)") == "function"  # back to the default (tells the host)


def test_ii_to_nobody_is_dropped_quickly(bb):
    # Regression: the handover to the UART pads looked to I2C0 like another
    # leader starting a transaction, and every message timed out (5ms, then a
    # retry) instead of ending at once with no answer
    before = ii_results(bb)
    bb.eval(f"for i = 1, 8 do ii.raw({NOBODY}, string.char(1, i)) end")
    got = ii_wait(bb, before, 8, 0.5)
    assert got == {"nak": 8}, got


def test_ii_queue_overflow_drops_the_excess(bb):
    # 16 queue slots: the rest of a burst made in one go is refused
    before = ii_results(bb)
    bb.eval(f"for i = 1, 40 do ii.raw({NOBODY}, string.char(1, i)) end")
    got = ii_wait(bb, before, 16, 1.0)
    bb.wait(0.1)
    got = {k: ii_results(bb)[k] - before[k] for k in RESULTS if ii_results(bb)[k] != before[k]}
    assert got == {"nak": 16}, got


def test_ii_traffic_does_not_disturb_audio_rate_asl(bb):
    # ii is polled from the main loop and never waits on the bus: a stream of
    # messages must not delay ASL segment changes (exact cycle lengths)
    rate = bb.stats()["sample_rate_hz"]
    starts = bb.symbol("g_slope_seg_starts")
    count = bb.symbol("g_slope_seg_start_count")
    bb.eval(f"""
        output[1](lfo(1/4000, 2))
        m = metro.init(function() for i = 1, 4 do ii.raw({NOBODY}, string.char(1, i)) end end, 0.002)
        m:start()
    """)
    bb.wait(0.3)
    lengths = []
    for _ in range(4):
        bb.mem.write_u32(count, 0)
        bb.wait(0.2)
        seg = bb.mem.read_words(starts, 64)
        lengths += [(seg[i + 2] - seg[i]) & 0xFFFFFFFF for i in range(0, 62, 2)]
    bb.eval("m:stop()")
    assert set(lengths) == {rate / 4000}, f"cycle lengths {sorted(set(lengths))}, want {rate / 4000}"


# --- with a crow on the bus ------------------------------------------------

def need_crow(bb):
    before = ii_results(bb)
    bb.eval("ii.crow.get('output', 1)")
    got = ii_wait(bb, before, 1, 0.5)
    if got.get("done") != 1:
        raise Skip(f"no crow answering on ii ({got})")


# Voltages go through crow's DAC/ADC and blackbird's; an uncalibrated
# Computer is a few % off (measured up to 0.27V at -4V). A broken ii value
# would be far off, or out of order.
VOLTS = (-4.0, -2.0, 0.0, 2.5, 4.0)
TOLERANCE = 0.3


def test_ii_crow_volts_reaches_crow(bb):
    # crow output 1 -> blackbird input 1
    need_crow(bb)
    readings = []
    for volts in VOLTS:
        bb.eval(f"ii.crow.volts(1, {volts})")
        bb.wait(0.1)
        got = bb.query("input[1].volts")
        assert abs(got - volts) < TOLERANCE, f"ii.crow.volts(1, {volts}): blackbird input 1 reads {got}"
        readings.append(got)
    assert readings == sorted(readings), readings


def test_ii_crow_query_reply_reaches_event(bb):
    # blackbird output 1 -> crow input 1; crow's reply comes back as an event
    need_crow(bb)
    bb.eval("ii.crow.event = function(e, value) got = {name = e.name, value = value} end")
    readings = []
    for volts in VOLTS:
        bb.eval(f"output[1].volts = {volts}")
        bb.wait(0.1)
        bb.eval("got = nil; ii.crow.get('input', 1)")
        bb.wait(0.15)
        assert bb.query("got and got.name") == "input", f"no reply to ii.crow.get('input', 1) at {volts}V"
        value = bb.query("got.value")
        assert abs(value - volts) < TOLERANCE, f"output {volts}V: crow reports {value}"
        readings.append(value)
    assert readings == sorted(readings), readings


def test_ii_raw_bytes_reach_crow(bb):
    # ii.raw: crow's volts command by hand: cmd 1, channel 1, 2V as Teletype
    # s16 (2 * 1638.3 = 3276 = 0x0CCC), big-endian
    need_crow(bb)
    bb.eval("ii.raw(0x01, string.char(1, 1, 0x0C, 0xCC))")
    bb.wait(0.1)
    got = bb.query("input[1].volts")
    assert abs(got - 2.0) < TOLERANCE, got


def test_ii_fastmode(bb):
    need_crow(bb)
    try:
        bb.eval("ii.fastmode(true)")
        bb.eval("ii.crow.volts(1, 1.5)")
        bb.wait(0.1)
        assert abs(bb.query("input[1].volts") - 1.5) < TOLERANCE
    finally:
        bb.eval("ii.fastmode(false)")


def test_ii_crow_soak(bb):
    # Bus reliability over the case's ground (and the patch cables'): 800
    # commands, each followed by a query, with no timeouts or bus errors, and
    # every reply in range. A weak ground shows up here as errors.
    need_crow(bb)
    bb.eval("""
        replies, bad = 0, {}
        ii.crow.event = function(e, value)
            replies = replies + 1
            if math.abs(value - expect) > 0.3 then bad[#bad + 1] = value end
        end
        output[1].volts = 1.5; expect = 1.5
    """)
    bb.wait(0.1)
    before = ii_results(bb)
    for burst in range(100):  # 8 commands + 8 queries per burst: fits the 16-deep queue
        bb.eval("for i = 1, 8 do ii.crow.volts(1, i / 4); ii.crow.get('input', 1) end")
        bb.wait(0.05)
    bb.wait(0.3)
    after = ii_results(bb)
    got = {k: after[k] - before[k] for k in RESULTS if after[k] != before[k]}
    assert got == {"done": 1600}, got
    assert bb.query("replies") == 800, bb.query("replies")
    # (a corrupted byte shows as a jump in steps of 1/II_TT_VOLT*256 = 0.156V)
    bad = bb.query("table.concat(bad, ' ')")
    assert bad == "", f"replies off from 1.5V: {bad}"
