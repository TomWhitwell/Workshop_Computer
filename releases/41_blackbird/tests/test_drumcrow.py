"""nb_drumcrow's crow script, played and stressed on the card.

nb_drumcrow (a norns mod) uploads lib/update_loop.lua to crow and drives it:
four audio-rate ASL voices whose dynamics a 100Hz loop on metro[8] updates,
with bit-crush quantizers. One of the heaviest crow scripts around. These
tests run that script directly (as norns would have it run), play it hard, and
check: no ProcessSample overruns, sample rate held, the update loop keeping
time, no dropped events or failed allocations, Lua memory levelling off.

Regression: metro[8] never started (Lua's metro ids went to C unconverted), so
drumcrow's update loop never ran and it made no sound.

nb_drumcrow has no licence, so it isn't copied here: it's fetched at
DRUMCROW_REF into tests/.cache/nb_drumcrow, or set BB_DRUMCROW_DIR to a
checkout. Without it these tests skip.
"""
import os
import re
import subprocess
import sys
import time

from bbdbg import Skip

HERE = os.path.dirname(os.path.abspath(__file__))
DRUMCROW_URL = "https://github.com/entzmingerc/nb_drumcrow"
DRUMCROW_REF = os.environ.get("BB_DRUMCROW_REF", "892ee538d88a3af20dc77fb3a5fdd070df0dc56f")
DRUMCROW_DIR = os.environ.get("BB_DRUMCROW_DIR", os.path.join(HERE, ".cache", "nb_drumcrow"))
STRESS_SECONDS = float(os.environ.get("BB_DRUMCROW_SECONDS", "15"))
SOAK_SECONDS = float(os.environ.get("BB_DRUMCROW_SOAK", "30"))


def fetch_drumcrow():
    if os.path.isdir(os.path.join(DRUMCROW_DIR, ".git")) or os.environ.get("BB_DRUMCROW_DIR"):
        head = subprocess.run(["git", "-C", DRUMCROW_DIR, "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        if os.environ.get("BB_DRUMCROW_DIR") or head == DRUMCROW_REF:
            return True
    try:
        os.makedirs(DRUMCROW_DIR, exist_ok=True)
        git = ["git", "-C", DRUMCROW_DIR]
        if not os.path.isdir(os.path.join(DRUMCROW_DIR, ".git")):
            subprocess.run(git + ["init", "-q"], check=True)
            subprocess.run(git + ["remote", "add", "origin", DRUMCROW_URL], check=True)
        subprocess.run(git + ["fetch", "-q", "--depth", "1", "origin", DRUMCROW_REF], check=True, timeout=60)
        subprocess.run(git + ["checkout", "-q", "FETCH_HEAD"], check=True)
        return True
    except (subprocess.SubprocessError, OSError) as e:
        print(f"test_drumcrow: couldn't fetch {DRUMCROW_URL}@{DRUMCROW_REF[:8]}: {e}", file=sys.stderr)
        return False


_script = None


def script():
    """drumcrow's crow script, and its parameter names -> the 1-based indexes
    dc_param_update_receive takes."""
    global _script
    if _script is None:
        if not fetch_drumcrow():
            raise Skip(f"nb_drumcrow not available (fetch {DRUMCROW_URL} or set BB_DRUMCROW_DIR)")
        with open(os.path.join(DRUMCROW_DIR, "lib", "update_loop.lua")) as f:
            text = f.read()
        names = re.findall(r"'(\w+)'", text[text.index("local dc_names"):text.index("for ch = 1, DC_VOICES")])
        _script = (text, {n: i + 1 for i, n in enumerate(names)})
    return _script


def load(bb):
    text, ix = script()
    out = bb.upload(text)
    assert "^^ready()" in out, out
    # count update-loop runs (metro[8] calls dc_update_loop through the global, set on the first note)
    bb.eval("dc_runs = 0 local f = dc_update_loop dc_update_loop = function() dc_runs = dc_runs + 1 f() end")
    return ix


def set_params(bb, ix, ch, **values):
    body = ", ".join(f"[{ix[k]}] = {v}" for k, v in values.items())
    bb.eval(f"dc_param_update_receive({{[{ch}] = {{{body}}}}})")


def heavy_voices(bb, ix, note=100):
    """All four voices playing, high: a mix of models and shapes, sustained
    (looping) amp envelopes, bit-crush with a mutating scale, random splash."""
    for ch, (model, shape) in enumerate([(1, 2), (2, 7), (5, 8), (7, 9)], start=1):
        bb.eval(f"dc_set_synth({ch}, {model}, {shape})")
        set_params(bb, ix, ch, mfreq=1, a_loop=2, a_cyc=5, bit=0.5, species=3, mut=0.7, splash=0.5, a_limit=4)
    bb.eval(f"for ch = 1, 4 do dc_note_on(ch, {note}, 1) end")


def play(bb, every, period, seconds):
    """Run Lua `every` from a metro every `period` seconds for `seconds`,
    alongside drumcrow; what the card did meanwhile."""
    bb.eval(f"stress_n = 0 stress_m = metro.init{{ time = {period}, "
            f"event = function() stress_n = stress_n + 1 {every} end }} stress_m:start()")
    bb.reset_stats()
    failures0 = bb.stats()["lua_alloc_failures"]  # a running total: earlier tests fill the heap on purpose
    runs0, t0 = bb.query("dc_runs"), bb.query("bb_dbg_us()")
    load = bb.load(min(seconds, 3.0))
    time.sleep(max(0.0, seconds - 3.0))
    stats = bb.stats()
    runs, t1 = bb.query("dc_runs"), bb.query("bb_dbg_us()")
    bb.eval("stress_m:stop()")
    result = {
        "core1_pct": round(load["core1_busy_pct"], 1),
        "isr_rate_hz": round(load["isr_rate_hz"]),
        "update_hz": round((runs - runs0) / ((t1 - t0) / 1e6), 1),
        "events": bb.query("stress_n"),
        "lua_kb": round(stats["lua_mem_bytes"] / 1024),
        "alloc_failures": stats["lua_alloc_failures"] - failures0,
    }
    bb.info.update(result)
    return result, stats


def check(bb, result, stats, update_hz=95):
    rate = stats["sample_rate_hz"]
    assert stats["ps_overruns"] == 0, f"{stats['ps_overruns']} ProcessSample overruns"
    assert abs(result["isr_rate_hz"] / rate - 1) < 0.01, f"sample rate {result['isr_rate_hz']}Hz of {rate}"
    assert result["update_hz"] >= update_hz, f"drumcrow's update loop ran at {result['update_hz']}Hz of 100"
    assert result["alloc_failures"] == 0, f"{result['alloc_failures']} Lua allocations failed while playing"
    drops = {k: v for k, v in stats.items() if k.startswith("drops_") and v}
    assert not drops, f"dropped events: {drops}"


def test_drumcrow_plays_a_note(bb):
    load(bb)
    bb.eval("for ch = 1, 4 do dc_set_synth(ch, 1, 2) end")
    bb.eval("dc_note_on(1, 60, 0.8)")
    bb.wait(0.05)
    # the update loop has tuned voice 1 to middle C (from the 440Hz default; drumcrow scales
    # frequencies by 1.023996 to tune its ASL oscillators) and opened its envelope
    assert abs(bb.query("output[1].asl.dyn.cyc") - 1 / (261.63 * 1.023996)) < 2e-6, bb.query("output[1].asl.dyn.cyc")
    assert bb.query("output[1].asl.dyn.dcAmp") > 1
    bb.wait(0.6)
    assert bb.query("output[1].asl.dyn.dcAmp") < 0.1  # and closed it again


def test_four_high_voices_with_fast_notes(bb):
    ix = load(bb)
    heavy_voices(bb, ix)
    result, stats = play(bb, "dc_note_on(stress_n % 4 + 1, 96 + math.random(12), 0.5 + math.random() / 2)",
                         0.0125, STRESS_SECONDS)
    check(bb, result, stats)


def test_changing_synths_while_playing(bb):
    ix = load(bb)
    heavy_voices(bb, ix)
    # dc_set_synth stops and restarts the update metro, so it misses a few ticks
    result, stats = play(bb, "local ch = stress_n % 4 + 1 dc_set_synth(ch, math.random(7), math.random(9)) "
                             "dc_note_on(ch, 60 + math.random(48), 1)", 0.1, STRESS_SECONDS)
    check(bb, result, stats, update_hz=80)


def test_parameter_flood(bb):
    ix = load(bb)
    heavy_voices(bb, ix, note=84)
    # what norns sends (crow.dc_param_update_receive, 4 voices x 8 params), every 20ms instead of 250ms
    voice = (f"{{[{ix['pw']}] = math.random(), [{ix['pw2']}] = math.random(), [{ix['bit']}] = math.random(), "
             f"[{ix['mut']}] = math.random(), [{ix['transpose']}] = math.random(12), [{ix['a_cyc']}] = 1 + math.random(9), "
             f"[{ix['splash']}] = math.random(), [{ix['detune']}] = 0.95 + math.random() / 10}}")
    update = "{" + ", ".join(f"[{ch}] = {voice}" for ch in range(1, 5)) + "}"
    result, stats = play(bb, f"dc_param_update_receive({update}) "
                             "if stress_n % 5 == 0 then dc_note_on(stress_n % 4 + 1, 72 + math.random(24), 1) end",
                         0.02, STRESS_SECONDS)
    check(bb, result, stats)


def test_soak_lua_memory_levels_off(bb):
    ix = load(bb)
    heavy_voices(bb, ix)
    every = ("local ch = stress_n % 4 + 1 dc_note_on(ch, 60 + math.random(48), 1) "
             f"dc_param_update_receive({{[ch] = {{[{ix['bit']}] = math.random(), [{ix['pw']}] = math.random()}}}}) "
             "if stress_n % 40 == 0 then dc_set_synth(ch, math.random(7), math.random(9)) end")
    bb.eval(f"stress_n = 0 stress_m = metro.init{{ time = 0.025, event = function() stress_n = stress_n + 1 {every} end }} stress_m:start()")
    bb.reset_stats()
    failures0 = bb.stats()["lua_alloc_failures"]
    mems = []
    for _ in range(6):
        time.sleep(SOAK_SECONDS / 6)
        mems.append(round(bb.stats()["lua_mem_bytes"] / 1024))
    stats = bb.stats()
    bb.eval("stress_m:stop()")
    bb.info["lua_kb"] = mems
    assert stats["ps_overruns"] == 0, f"{stats['ps_overruns']} ProcessSample overruns"
    failures = stats["lua_alloc_failures"] - failures0
    assert failures == 0, f"{failures} Lua allocations failed while playing"
    assert max(mems[3:]) <= max(mems[:3]) + 8, f"Lua memory still growing (KB): {mems}"
