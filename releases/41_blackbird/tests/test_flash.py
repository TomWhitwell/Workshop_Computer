"""Flash writes while audio runs, and ^^w / ^^c taking effect without a reset.
These write the card's flash (wear, and they change the stored script), so they
only run with --allow-flash. Each one puts the card back on First.lua (^^c)
afterwards."""
import time

from bbdbg import writes_flash

SCRIPT = "-- harness flash test\nharness_flash = 1\nfunction init() harness_init = true end\n"
USER_SCRIPT_ADDR = 0x10000000 + 0x200000 - 0x4000  # last 16KB of the 2MB flash


def stored_script(bb, length):
    """What's in the user script flash area: (status word, script text)."""
    stored = bb.mem.read_bytes(USER_SCRIPT_ADDR, 4 + 32 + length)
    return int.from_bytes(stored[:4], "little"), stored[36:].decode("latin-1")


def flash_upload(bb, script):
    """Upload the way druid's `w` does: ^^s, the script, ^^w."""
    bb.repl("^^s")
    bb.repl(script)
    return bb.repl("^^w", 20.0)


def core1_samples(bb):
    """Low word of global_sample_counter, which only core 1 (audio) advances."""
    return bb.mem.read_u32(bb.symbol("global_sample_counter"))


@writes_flash
def test_audio_keeps_running_through_script_upload(bb):
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("output[1](lfo(0.05, 5))")
    try:
        s0, t0 = core1_samples(bb), time.monotonic()
        flash_upload(bb, SCRIPT)  # erase + program; core 0 has interrupts off throughout
        s1, t1 = core1_samples(bb), time.monotonic()
        expected = (t1 - t0) * rate
        got = (s1 - s0) & 0xFFFFFFFF
        # If core 1 had stopped for the write, it would be short by the write time (~100ms+)
        assert got >= expected - 0.05 * rate, f"core 1 ran {got} samples in {t1 - t0:.3f}s, expected ~{expected:.0f}"
        # Flash layout: [status word][32-byte name][script]
        status, stored = stored_script(bb, len(SCRIPT))
        assert status & 0xF == 0xA, f"no user script magic in flash (status 0x{status:08x})"
        assert stored == SCRIPT, f"flash holds {stored!r}"
    finally:
        bb.repl("^^c")


@writes_flash
def test_flash_upload_runs_without_reset(bb):
    """^^w runs the script and calls init() straight away, like ^^e (and
    crow): no reset needed for it to take effect."""
    try:
        out = flash_upload(bb, SCRIPT)
        assert "^^ready()" in out, out
        assert "saved to flash" in out, out
        assert "RESET" not in out, out
        assert bb.query("harness_flash") == 1
        assert bb.query("harness_init") is True
    finally:
        bb.repl("^^c")


@writes_flash
def test_failed_script_is_not_saved(bb):
    """A script that fails to evaluate must not replace the saved one."""
    try:
        flash_upload(bb, SCRIPT)
        out = flash_upload(bb, "harness_flash = = 2\n")
        assert "not saved" in out, out
        assert "^^ready()" not in out, out
        status, stored = stored_script(bb, len(SCRIPT))
        assert status & 0xF == 0xA and stored == SCRIPT, f"flash now holds {stored!r}"
    finally:
        bb.repl("^^c")


@writes_flash
def test_flash_clear_loads_first_lua(bb):
    """^^c clears the saved script and goes straight back to First.lua."""
    flash_upload(bb, SCRIPT)
    out = bb.repl("^^c", 20.0)
    assert "cleared" in out and "First.lua" in out, out
    assert "RESET" not in out, out
    assert bb.query("harness_flash") is None
    assert bb.query("type(play)") == "function"  # defined by First.lua
    status, _ = stored_script(bb, 0)
    assert status & 0xF != 0xA, f"user script still marked in flash (status 0x{status:08x})"


@writes_flash
def test_repeated_flash_writes(bb):
    """The old core-1 reset path leaked DMA channels and failed after ~5 writes."""
    try:
        for i in range(8):
            out = flash_upload(bb, f"harness_flash = {i}\n")
            assert "flash write failed" not in out, out
            assert bb.stats()["heartbeat"] > 0
        assert bb.query("harness_flash") == 7
    finally:
        bb.repl("^^c")


@writes_flash
def test_core1_survives_flash_writes_under_load(bb):
    """Six saves to flash while core 1 is doing as much as it can: every ASL
    shape, quantized outputs, noise, every input detection mode, metros,
    clocks and pulse-out clocks. Core 1 must never touch flash meanwhile; if it
    does, it faults (the runner reports HardFaults) or stops counting samples.
    This is the dynamic check for what util/core1_flash_audit.py can't see,
    such as flash data reached through pointers (it found a vtable read)."""
    rate = bb.stats()["sample_rate_hz"]
    # ^^w runs the script before writing flash, so the load is in the script
    load = """
        output[1].scale({0, 2, 4, 5, 7, 9, 11})
        output[1](loop{ to(5, 0.05, 'sine'), to(-5, 0.05, 'expo'), to(2, 0.03, 'log'), to(0, 0.02, 'now') })
        output[2](loop{ to(4, 0.04, 'over'), to(-4, 0.04, 'under'), to(1, 0.02, 'rebound'), to(0, 0.01, 'linear') })
        output[3](lfo(0.03, 5, 'sine'))
        output[4](bb.noise(0.5))
        input[1].mode('change', 1.0, 0.1, 'both'); input[1].change = function() end
        bbt_m = 0
        for i = 1, 4 do metro[i].event = function() bbt_m = bbt_m + 1 end; metro[i].time = 0.01 * i; metro[i]:start() end
        clock.tempo = 200
        clock.run(function() while true do clock.sync(1/8) end end)
        bb.pulseout[1]:clock(0.5)
        input[2].window = function() end; input[2].scale = function() end
        input[2].volume = function() end; input[2].peak = function() end; input[2].stream = function() end
    """
    bb.square("cv1", 60, low=0, high=4)
    bb.triangle("cv2", 2, -5, 5)
    bb.square("pulse1", 24, duty=0.3)
    modes = ["input[2].mode('window', {-1, 1, 3}, 0.1)", "input[2].mode('scale', {0, 7}, 12, 1)",
             "input[2].mode('volume', 0.05)", "input[2].mode('peak', 0.5, 0.1)", "input[2].mode('stream', 0.01)"]
    try:
        for i in range(6):
            s0, t0 = core1_samples(bb), time.monotonic()
            out = flash_upload(bb, f"{load}\n{modes[i % len(modes)]}\nharness_flash = {i}\n")
            s1, t1 = core1_samples(bb), time.monotonic()
            assert "^^ready()" in out, out
            got, expected = (s1 - s0) & 0xFFFFFFFF, (t1 - t0) * rate
            assert got >= expected - 0.05 * rate, f"write {i}: core 1 ran {got} samples in {t1 - t0:.3f}s, expected ~{expected:.0f}"
            assert bb.query("bbt_m") > 0  # the load was running
        assert not bb.faults(), bb.faults()
    finally:
        bb.repl("^^c")
