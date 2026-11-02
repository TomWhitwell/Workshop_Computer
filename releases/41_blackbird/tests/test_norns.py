"""blackbird and a real norns, driven from both ends.

Plug blackbird into norns over USB and keep the SWD probe on blackbird. These
tests run Lua on norns through matron's REPL websocket (the one maiden uses)
and check the result on the card over SWD, and the other way round.

Set NORNS_HOST to norns' address (norns.local doesn't resolve inside the dev
container). If norns can't be reached, every test here skips within a couple
of seconds and the rest of the run carries on.

The tests unload whatever script is running on norns (norns.script.clear()).

Script corpus: NORNS_SCRIPTS lists scripts installed on norns to load and run,
each with optional setup Lua so it actually uses crow. Scripts that aren't
installed are skipped. Add more with BB_NORNS_SCRIPTS="path/a.lua,path/b.lua"
(relative to dust/code).
"""
import os
import re
import statistics
import time

from bbdbg import GENERIC_STIMULUS, Skip, apply_stimulus, expected_failure, outputs_moving
from nornsws import Norns, NornsError, NornsUnavailable

NORNS_PORT = int(os.environ.get("NORNS_PORT", "5555"))
SCRIPT_SECONDS = float(os.environ.get("BB_NORNS_SECONDS", "10"))
FLOOD_SECONDS = float(os.environ.get("BB_NORNS_FLOOD_SECONDS", "5"))
LATENCY_NOTES = int(os.environ.get("BB_LATENCY_NOTES", "50"))
LATENCY_P95_MS = float(os.environ.get("BB_LATENCY_P95_MS", "10"))
LATENCY_MAX_MS = float(os.environ.get("BB_LATENCY_MAX_MS", "25"))

# path under dust/code -> Lua to run on norns after the script starts, to point it at crow
NORNS_SCRIPTS = {
    # tehn/awake: "out" option 4 = "crow out 1+2" (pitch on 1, ar() envelope action on 2)
    "awake/awake.lua": "params:set('out', 4)",
    # schollz/wobblewobble: streams both crow inputs; its engine sends each
    # output's value at 15Hz with slew. Its default is a constant per output, so
    # start four different LFOs over -5..10V, then keep re-randomising one
    # output's modulation type, frequency, meta and slew every 0.3-1s (each
    # slew change also sends output[n].slew). Seeded, so runs repeat.
    "wobblewobble/wobblewobble.lua": """
        math.randomseed(1)
        for i, m in ipairs({4, 5, 6, 14}) do  -- wobbly sine, snek, lorenz, pulse
            params:set(i .. "minval", -5); params:set(i .. "maxval", 10)
            params:set(i .. "freq", ({0.5, 3, 11, 0.07})[i])
            params:set(i .. "modulation", m)
        end
        bbt_wobble = clock.run(function()
            while true do
                clock.sleep(0.3 + math.random() * 0.7)
                local i = math.random(4)
                params:set(i .. "modulation", math.random(2, 14))
                params:set(i .. "freq", math.random() ^ 3 * 40)
                params:set(i .. "meta", math.random(-50, 50))
                params:set(i .. "meta2", math.random(-50, 50))
                params:set(i .. "slewrate", math.random(0, 15))
            end
        end)
    """,
    # lylepmills/buoys: crow input 1 is a clock by default. Outputs come from
    # per-buoy options, and buoys only exist on a grid, so with a grid attached
    # make four buoys, one per crow output in voltage mode.
    "buoys/buoys.lua": """
        if g and g.cols > 0 then
            for i = 1, 4 do
                local x, y = i * 2, 2
                buoys[y][x] = buoys[y][x] or Buoy:new()
                buoys[y][x].options["crow output"] = i
                buoys[y][x].options["crow output mode"] = 1
                buoys[y][x]:activate()
            end
        end
    """,
    # markwheeler/loom: "output" option 4 = "Crow Out 1+2" (pitch on 1,
    # output[2].execute() of a to() envelope). Notes normally come from a grid;
    # encoder 3 on the first page adds random ones.
    "loom/loom.lua": "params:set('output', 4); for _ = 1, 12 do enc(3, 1) end",
    # schollz/acid-test: "out_crow" 2 = yes (pitch with per-note slew for slides
    # on 1, gate on 2). With the jf output off it also sends ii.jf.mode(0) on
    # every note, so blackbird's ii stub gets steady traffic. It doesn't play
    # until started (K3), hence toggle_playing.
    "acid-test/acid-test.lua": "params:set('out_crow', 2); toggle_playing(true)",
    # vicimity-dndrks/less-concepts: voice 1 -> crow 1/2, voice 2 -> crow 3/4
    # (pitch on 1 and 3, 5ms trigger actions "{to(5,0),to(0,0.005)}" fired on 2
    # and 4). Plays from init with its default seed. init() also sends raw
    # "ii.wsyn.ar_mode(1)" strings, which go to blackbird's ii stub.
    "less-concepts/less-concepts.lua": "params:set('voice_1_crow_1', 2); params:set('voice_2_crow_2', 2)",
    # oliviercreurer/dunes: "ext_out" 3 = "crow 1+2" (pitch on 1, an ASL
    # envelope action per step on 2); its step commands also send random
    # voltages to 3 and envelopes to 4. The sequencer waits for K2/transport.
    "dunes/dunes.lua": "params:set('ext_out', 3); running = true",
}
for _p in filter(None, os.environ.get("BB_NORNS_SCRIPTS", "").split(",")):
    NORNS_SCRIPTS.setdefault(_p.strip(), "")

# path -> reason, for known issues (reported as XFAIL)
EXPECTED = {
}

NORNS_ERRORS = re.compile(r"load fail|stack traceback|attempt to |cleanup failed|### .*error", re.IGNORECASE)

# Errors from norns itself that happen with or without blackbird (checked by
# running the scripts with crow output off). Reported as info, not failures.
NORNS_SIDE_ERRORS = re.compile(
    r"clock\.lua:\d+: bad argument #1 to 'resume' \(thread expected\)"  # norns clock cleanup race
)

_norns = None
_norns_error = None


def norns_or_skip():
    """The shared norns connection; Skip if norns is unreachable (tried once per run)."""
    global _norns, _norns_error
    if _norns is None and _norns_error is None:
        try:
            _norns = Norns(port=NORNS_PORT, timeout=2.0)
            _norns.run("return 1")
        except (NornsUnavailable, NornsError, OSError) as e:
            _norns_error = f"norns not reachable ({e}); set NORNS_HOST"
            _norns = None
    if _norns_error:
        raise Skip(_norns_error)
    return _norns


def connected_norns(bb):
    """norns, with blackbird connected to it and a clean slate on both sides."""
    n = norns_or_skip()
    if not n.query("norns.crow.connected()"):
        raise Skip("blackbird isn't connected to norns (see test_norns_detects_blackbird)")
    n.run("norns.script.clear()", timeout=10)
    n.run("norns.crow.init()")  # crow.reset() on blackbird, clear norns-side crow callbacks
    bb.wait(0.2)
    return n


def norns_errors(n, since, bb=None):
    """Error lines in norns' output since `since`, each with the two lines
    before it (a traceback's message comes before 'stack traceback:').
    Known norns-side errors (NORNS_SIDE_ERRORS) and their tracebacks are left
    out, and counted in bb.info if bb is given."""
    lines = [l for l in n.output_since(since) if l.strip() and l.strip() != "<ok>" and not l.startswith("@@bbt")]
    out, skipping, ignored = [], False, 0
    for i, l in enumerate(lines):
        if NORNS_SIDE_ERRORS.search(l):
            skipping, ignored = True, ignored + 1
            continue
        if skipping and (l.startswith("\t") or l.startswith("stack traceback")):
            continue
        skipping = False
        if NORNS_ERRORS.search(l):
            out += [c for c in lines[max(0, i - 2):i + 1] if c not in out[-3:] and not NORNS_SIDE_ERRORS.search(c)]
    if bb is not None and ignored:
        bb.info["norns-side errors ignored"] = ignored
    return out


def close(a, b, tol):
    return isinstance(a, (int, float)) and abs(a - b) <= tol


# ---------------------------------------------------------------------------
# protocol
# ---------------------------------------------------------------------------
def test_norns_detects_blackbird(bb):
    n = norns_or_skip()
    assert n.query("norns.crow.connected()"), "norns doesn't see blackbird as a crow (USB plugged into norns?)"
    since = n.mark()
    n.run("crow.identity()")
    n.wait_for(r"crow identity:", since)
    n.run("crow.version()")
    n.wait_for(r"crow version:", since)


def test_norns_sets_output_volts(bb):
    n = connected_norns(bb)
    for i, v in ((1, 3.3), (2, -2.5), (3, 0.0), (4, 7.25)):
        n.run(f"crow.output[{i}].volts = {v}")
    bb.wait(0.1)
    for i, v in ((1, 3.3), (2, -2.5), (3, 0.0), (4, 7.25)):
        got = bb.query(f"output[{i}].volts")
        assert close(got, v, 0.02), f"output[{i}] = {got}, norns set {v}"


def test_norns_output_action(bb):
    n = connected_norns(bb)
    n.run("crow.output[2].action = 'lfo(0.25, 4)'; crow.output[2]()")
    values = []
    for _ in range(10):
        bb.wait(0.05)
        values.append(bb.query("output[2].volts"))
    assert max(values) - min(values) > 2, f"output[2] barely moved: {values}"


def test_input_stream_reaches_norns(bb):
    n = connected_norns(bb)
    bb.set_input("cv1", 2.0)
    n.run("bbt_v = nil; crow.input[1].stream = function(v) bbt_v = v end; crow.input[1].mode('stream', 0.05)")
    bb.wait(0.4)
    assert close(n.query("bbt_v"), 2.0, 0.05), n.query("bbt_v")
    bb.set_input("cv1", -4.0)
    bb.wait(0.4)
    assert close(n.query("bbt_v"), -4.0, 0.05), n.query("bbt_v")


def test_input_query_reaches_norns(bb):
    n = connected_norns(bb)
    bb.set_input("cv2", 1.5)
    n.run("bbt_q = nil; crow.input[2].stream = function(v) bbt_q = v end; crow.input[2].mode('none')")
    bb.wait(0.1)
    n.run("crow.input[2].query()")
    bb.wait(0.3)
    assert close(n.query("bbt_q"), 1.5, 0.05), n.query("bbt_q")


def test_input_change_reaches_norns(bb):
    n = connected_norns(bb)
    n.run("bbt_n = 0; crow.input[1].change = function(s) bbt_n = bbt_n + 1 end;"
          "crow.input[1].mode('change', 1.0, 0.1, 'both')")
    bb.wait(0.1)
    t0 = time.monotonic()
    bb.square("cv1", 5, low=0, high=3)
    bb.wait(2.0)
    bb.release_input("cv1")
    elapsed = time.monotonic() - t0  # the host's view of how long the square ran
    bb.wait(0.3)  # let in-flight events reach norns
    got = n.query("bbt_n")
    expected = 2 * 5 * elapsed  # both edges of a 5Hz square
    assert abs(got - expected) <= 3, f"norns saw {got} changes, expected ~{expected:.0f} over {elapsed:.2f}s"


def test_forwarded_function_arguments(bb):
    """norns assigns a function on crow; crow calls it; norns gets the arguments
    back through _c.tell and quote, with their types intact. (Only nested keys
    are forwarded this way, so it goes in a table.)"""
    n = connected_norns(bb)
    n.run('crow "bbt = {}"')
    n.run("bbt_args = nil; crow.bbt.cb = function(...) bbt_args = {...} end")
    bb.wait(0.2)
    bb.eval("bbt.cb(1, 'two', 3.5, true)")
    bb.wait(0.3)
    ok = n.query("bbt_args ~= nil and bbt_args[1] == 1 and bbt_args[2] == 'two' "
                 "and bbt_args[3] == 3.5 and bbt_args[4] == true")
    if not ok:
        got = n.run("return bbt_args and (tostring(bbt_args[1]) .. ',' .. tostring(bbt_args[2]) .. ','"
                    " .. tostring(bbt_args[3]) .. ',' .. tostring(bbt_args[4]))")
        raise AssertionError(f"norns got {got}, expected 1,two,3.5,true")


def test_output_flood_from_norns(bb):
    """The wobblewobble case: norns sending output volts as fast as it can.
    blackbird must keep up without overruns, dropped USB input or losing the
    last value."""
    n = connected_norns(bb)
    rate = 100  # per output, 4 outputs
    n.run(f"""
        bbt_flood = clock.run(function()
            local t = 0
            while true do
                t = t + 1
                for i = 1, 4 do crow.output[i].volts = ((t + i) % 100) / 20 end
                clock.sleep({1 / rate})
            end
        end)
    """)
    bb.wait(FLOOD_SECONDS)
    n.run("clock.cancel(bbt_flood)")
    n.run("for i = 1, 4 do crow.output[i].volts = i end")
    bb.wait(0.3)
    for i in range(1, 5):
        assert close(bb.query(f"output[{i}].volts"), i, 0.02), f"output[{i}] lost the final value"
    st = bb.stats()
    assert st["drops_usb_rx"] == 0, f"blackbird dropped {st['drops_usb_rx']} USB packets"
    bb.info["main loop worst"] = f"{st['loop_worst_us']}us"


# ---------------------------------------------------------------------------
# latency: timed entirely on blackbird's microsecond clock
# ---------------------------------------------------------------------------
# On blackbird, bb.asap (run every main loop iteration) watches output[1] and
# notes when each note's voltage lands. The note number is recovered from the
# voltage, (note - 60) / 12, so late or lost replies can't be mismatched.
LATENCY_PROBE = """
    lat = { sent = {}, got = {} }
    local last = output[1].volts
    bb.asap = function()
        local v = output[1].volts
        if v ~= last then
            last = v
            local note = math.floor(v * 12 + 60.5)
            local t0 = lat.sent[note]
            if t0 and not lat.got[note] then lat.got[note] = bb_dbg_us() - t0 end
        end
    end
"""

# The norns side of the MIDI path, written the way norns scripts usually are:
# find the device, connect, and set crow output 1 from note-ons.
NORNS_MIDI_HANDLER = """
    bbt_port = nil
    for i, v in pairs(midi.vports) do
        if v.name and (v.name:find("telephone") or v.name:find("crow")) then bbt_port = i end
    end
    if bbt_port then
        bbt_m = midi.connect(bbt_port)
        bbt_m.event = function(data)
            local msg = midi.to_msg(data)
            if msg.type == "note_on" then crow.output[1].volts = (msg.note - 60) / 12 end
        end
    end
    return bbt_port
"""


def midi_note_trigger(bb):
    """Lua that sends a note-on for note %d, in whichever MIDI API the firmware has
    (bb.midi_note_on since the iii-aligned API; bb.midi.tx.note before, e.g. 1.1M)."""
    if bb.query("bb.midi_note_on ~= nil"):
        return "bb.midi_note_on(%d, 100, 1)"
    return "bb.midi.tx.note(%d, 100, 0, 1)"


def measure_round_trips(bb, trigger):
    """Send LATENCY_NOTES notes with `trigger` (Lua on blackbird, with %d for the
    note) ~120ms apart, and return (latencies in ms, notes that never came back)."""
    import random
    bb.eval("output[1].volts = -5")  # a voltage no note maps to
    bb.eval(LATENCY_PROBE)
    notes = [40 + (i % 60) for i in range(LATENCY_NOTES)]  # distinct within any 60
    try:
        for note in notes:
            bb.eval(f"lat.sent[{note}] = bb_dbg_us(); lat.got[{note}] = nil; " + trigger % note)
            bb.wait(random.uniform(0.09, 0.15))  # off the USB frame grid
        bb.wait(0.5)
        out = bb.eval("for n, d in pairs(lat.got) do print(n, d) end")
    finally:
        bb.eval("bb.asap = nil")
    got = {int(n): int(d) / 1000.0 for n, d in (line.split() for line in out.splitlines() if line.strip())}
    lost = sorted(set(notes) - set(got))
    return sorted(got.values()), lost


def report_latency(bb, label, ms, lost):
    if not ms:
        raise AssertionError(f"{label}: no notes came back ({len(lost)} sent)")
    p95 = ms[min(len(ms) - 1, int(0.95 * len(ms)))]
    bb.info[label] = (f"min {ms[0]:.1f}  median {statistics.median(ms):.1f}  "
                      f"p95 {p95:.1f}  max {ms[-1]:.1f} ms  ({len(ms)} notes, {len(lost)} lost)")
    assert not lost, f"{label}: {len(lost)} of {LATENCY_NOTES} notes never reached output 1"
    assert p95 <= LATENCY_P95_MS, f"{label}: p95 {p95:.1f}ms > {LATENCY_P95_MS}ms (BB_LATENCY_P95_MS)"
    assert ms[-1] <= LATENCY_MAX_MS, f"{label}: worst {ms[-1]:.1f}ms > {LATENCY_MAX_MS}ms (BB_LATENCY_MAX_MS)"


def setup_midi_path(bb):
    n = connected_norns(bb)
    port = n.run(NORNS_MIDI_HANDLER)
    if port == "nil":
        names = n.run("local t = {} for i, v in pairs(midi.vports) do t[#t+1] = i .. '=' .. tostring(v.name) end "
                      "return table.concat(t, ', ')")
        raise AssertionError(f"norns has no MIDI port for blackbird (vports: {names})")
    return n


def teardown_midi_path(n):
    n.run("if bbt_m then bbt_m.event = nil end")


def test_latency_midi_note_to_norns_to_bb(bb):
    """The 'play a note' path: MIDI note-on from blackbird's USB MIDI port ->
    norns MIDI handler -> crow.output[1].volts -> blackbird output 1."""
    n = setup_midi_path(bb)
    try:
        ms, lost = measure_round_trips(bb, midi_note_trigger(bb))
    finally:
        teardown_midi_path(n)
    report_latency(bb, "midi -> norns -> bb", ms, lost)


def test_latency_crow_event_to_norns_to_bb(bb):
    """Baseline without MIDI: a crow event (_c.tell over serial) -> norns ->
    crow.output[1].volts -> blackbird. Compare with the MIDI path to see which
    leg costs what."""
    n = connected_norns(bb)
    n.run('crow "bbt_lat = {}"')
    n.run("crow.bbt_lat.note = function(note) crow.output[1].volts = (note - 60) / 12 end")
    bb.wait(0.2)
    ms, lost = measure_round_trips(bb, "bbt_lat.note(%d)")
    report_latency(bb, "crow event -> norns -> bb", ms, lost)


def test_latency_midi_while_busy(bb):
    """The MIDI path again, in a busy session: LFOs on outputs 2-4 and input 1
    streaming to norns at 100Hz."""
    n = setup_midi_path(bb)
    bb.eval("for i = 2, 4 do output[i](lfo(0.2 + i * 0.1, 5)) end")
    bb.triangle("cv1", 1, -5, 5)
    n.run("bbt_s = 0; crow.input[1].stream = function(v) bbt_s = bbt_s + 1 end; crow.input[1].mode('stream', 0.01)")
    try:
        ms, lost = measure_round_trips(bb, midi_note_trigger(bb))
    finally:
        teardown_midi_path(n)
        n.run("crow.input[1].mode('none')")
    report_latency(bb, "midi -> norns -> bb (busy)", ms, lost)


CHORD_SPREAD_MS = float(os.environ.get("BB_CHORD_SPREAD_MS", "1.5"))


def test_midi_chord_arrival_spread(bb):
    """norns plays an 8-note chord into blackbird's MIDI port. How far apart do
    the notes reach Lua? Timed on blackbird's microsecond clock, first note to
    last. Asserted with the chord sent as one write, which measures
    blackbird's part; sent as 8 note_on calls, norns puts the notes in separate
    USB frames, so that spread (reported only) is mostly norns."""
    n = setup_midi_path(bb)
    bb.eval("chord = {}; bb.event_midi = function(b1, b2, b3) "
            "if b1 & 0xF0 == 0x90 and b3 > 0 then chord[#chord + 1] = bb_dbg_us() end end")
    sends = {
        "one write": "local d = {} for note = 60, 67 do d[#d+1] = 0x90; d[#d+1] = note; d[#d+1] = 100 end bbt_m:send(d)",
        "8 note_on calls": "for note = 60, 67 do bbt_m:note_on(note, 100, 1) end",
    }
    results = {}
    try:
        for label, send in sends.items():
            spreads, counts = [], []
            for _ in range(10):
                bb.eval("chord = {}")
                n.run(send)
                n.run("for note = 60, 67 do bbt_m:note_off(note, 0, 1) end")
                bb.wait(0.3)
                count, spread = bb.eval("print(#chord, #chord > 1 and (chord[#chord] - chord[1]) or 0)").split()
                counts.append(int(count))
                spreads.append(int(spread) / 1000)
            spreads.sort()
            results[label] = (spreads, counts)
            bb.info[f"chord spread, {label}"] = f"median {statistics.median(spreads):.2f}  max {spreads[-1]:.2f} ms"
    finally:
        bb.eval("bb.event_midi = nil")
        teardown_midi_path(n)
    for label, (spreads, counts) in results.items():
        assert all(c == 8 for c in counts), f"{label}: notes per chord {counts}"
    spreads = results["one write"][0]
    assert spreads[-1] <= CHORD_SPREAD_MS, \
        f"chord (one write) spread up to {spreads[-1]:.2f}ms > {CHORD_SPREAD_MS}ms (BB_CHORD_SPREAD_MS)"


def test_midi_flood_into_allocating_handler(bb):
    """2000 note-ons from norns in bursts of 100, into a Lua handler that makes
    garbage on every event. With no GC step forced per MIDI event, Lua's own
    GC must keep up: every note arrives, and memory levels off."""
    n = setup_midi_path(bb)
    bb.eval("flood_n = 0; bb.event_midi = function(b1, b2, b3) "
            "if b1 & 0xF0 == 0x90 then flood_n = flood_n + 1; local t = { b1, b2, b3, tostring(b2) } end end")
    bb.reset_stats()
    try:
        for burst in range(20):
            n.run("local d = {} for i = 1, 100 do d[#d+1] = 0x90; d[#d+1] = (i % 60) + 30; d[#d+1] = 100 end bbt_m:send(d)")
            bb.wait(0.05)
        bb.wait(1.0)
        got = bb.query("flood_n")
    finally:
        bb.eval("bb.event_midi = nil")
        teardown_midi_path(n)
    st = bb.stats()
    bb.info["flood"] = (f"{got}/2000 notes, lua peak {st['lua_mem_peak_bytes'] // 1024} KB, "
                        f"alloc failures {st['lua_alloc_failures']}")
    assert got == 2000, f"{got} of 2000 notes reached Lua"
    assert st["lua_mem_peak_bytes"] < 160 * 1024, f"Lua memory peaked at {st['lua_mem_peak_bytes'] // 1024} KB"


def test_midi_echo_burst_reaches_norns_intact(bb):
    """norns sends blackbird 128 MIDI note-offs at once (what awake's
    all_notes_off() does). blackbird echoes each to the host as ^^midi; the
    echoes are paced so norns' crow reader, which loses bytes if a second read
    lands before Lua has handled the first, gets every one intact."""
    n = setup_midi_path(bb)
    since = n.mark()
    n.run("bbt_echo = 0; norns.crow.events.midi = function(...) bbt_echo = bbt_echo + 1 end")
    n.run("for note = 0, 127 do bbt_m:note_off(note, 0, 1) end")
    bb.wait(1.5)  # 128 echoes at 4ms each
    got = n.query("bbt_echo")
    errors = norns_errors(n, since, bb)
    teardown_midi_path(n)
    assert not errors, "errors on norns:\n    " + "\n    ".join(errors)
    assert got == 128, f"norns got {got} of 128 echoes"
    assert bb.stats()["drops_midi_echo"] == 0


# ---------------------------------------------------------------------------
# script corpus
# ---------------------------------------------------------------------------
def run_norns_script(bb, path, setup):
    n = norns_or_skip()
    if not n.query("norns.crow.connected()"):
        raise Skip("blackbird isn't connected to norns")
    if not n.query(f"util.file_exists(_path.code .. {path!r})"):
        raise Skip(f"{path} isn't installed on norns")
    n.run("norns.script.clear()", timeout=10)
    since = n.mark()
    n.run(f"norns.script.load(_path.code .. {path!r})", timeout=20)
    try:
        m = n.wait_for(r"# script init|load fail|required", since, timeout=30)
    except NornsError:
        raise AssertionError(f"{path} didn't start within 30s")
    assert "# script init" in m.string, f"{path} didn't load: {m.string}"
    bb.wait(1.0)  # let init() finish
    if setup:
        n.run(setup)

    apply_stimulus(bb, GENERIC_STIMULUS)
    bb.info["outputs moving"] = outputs_moving(bb, SCRIPT_SECONDS) or "none"

    errors = norns_errors(n, since, bb)
    still_connected = n.query("norns.crow.connected()")
    n.run("norns.script.clear()", timeout=10)
    assert not errors, "errors on norns:\n    " + "\n    ".join(errors)
    assert still_connected, "norns lost blackbird during the script"
    drops = {k: v for k, v in bb.stats().items() if k.startswith("drops_") and v}
    assert not drops, f"dropped events on blackbird: {drops}"


def _make_script_test(path, setup):
    def test(bb):
        run_norns_script(bb, path, setup)
    test.__name__ = "test_norns_script_" + re.sub(r"\W", "_", path[:-4] if path.endswith(".lua") else path)
    if path in EXPECTED:
        test = expected_failure(EXPECTED[path])(test)
    return test


for _path, _setup in NORNS_SCRIPTS.items():
    _t = _make_script_test(_path, _setup)
    globals()[_t.__name__] = _t
