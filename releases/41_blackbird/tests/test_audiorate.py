"""Audio-rate output: ASL loops keep their frequency, oscillators fit the ISR
budget, and nothing on core 0 stalls the segment changes."""
from bbdbg import STAT_FIELDS


def asl_frequency(bb, freq, outputs=4, seconds=3.0):
    """Run lfo(1/freq) on `outputs` outputs; their frequency, from segment
    ends counted against core 1's own sample clock."""
    rate = bb.stats()["sample_rate_hz"]
    counter = bb.symbol("global_sample_counter")
    actions = bb.base + 4 * STAT_FIELDS.index("slope_actions")
    bb.eval(f"for i = 1, {outputs} do output[i](lfo({1 / freq!r}, 5)) end")
    bb.wait(0.5)
    a0, s0 = bb.mem.read_u32(actions), bb.mem.read_u32(counter)
    bb.wait(seconds)
    a1, s1 = bb.mem.read_u32(actions), bb.mem.read_u32(counter)
    secs = ((s1 - s0) & 0xFFFFFFFF) / rate
    return ((a1 - a0) & 0xFFFFFFFF) / secs / 2 / outputs


def test_asl_lfo_keeps_frequency_at_audio_rate(bb):
    # Regressions, all of which played audio-rate ASL flat or sharp:
    # - each segment ran ~1 sample long (the end overshoot and the wait for
    #   core 0 to start the next segment were dropped): 1kHz played at 907Hz
    # - times were Q16.16 seconds, rounded to 15us: 1kHz 0.7% flat, 4kHz 2.3% sharp
    # - overflow longer than a whole segment was dropped: 4 x 4kHz 25% flat
    for freq in (100, 1000, 4000):
        got = asl_frequency(bb, freq)
        assert abs(got / freq - 1) < 0.01, f"lfo at {freq}Hz x4 runs at {got:.1f}Hz"
        bb.reset()


def test_main_loop_has_no_gc_stalls(bb):
    # Regression: an idle GC step kept the collector cycling with nothing
    # allocating, and each cycle stalled the main loop ~1ms, which delayed ASL
    # segment changes (audio-rate ASL clicked ~10x a second)
    bb.eval("output[1](lfo(0.001, 5))")
    bb.wait(0.5)
    bb.reset_stats()
    bb.wait(3.0)
    worst = bb.stats()["loop_worst_us"]
    assert worst < 300, f"main loop stalled {worst}us"


def test_four_oscillators_fit_the_isr_budget(bb):
    # Regression: oscillate() used software float per sample (~14% of core 1
    # per voice at 24kHz); four voices saturated core 1 and the ISR overran
    # its sample period
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("for i = 1, 4 do output[i](oscillate(1000, 5)) end")
    bb.wait(0.5)
    bb.reset_stats()
    load = bb.load(3.0)
    assert load["isr_max_us"] < 1e6 / rate * 0.75, load
    assert load["core1_busy_pct"] < 85, load
    assert abs(load["isr_rate_hz"] / rate - 1) < 0.01, load


def segment_cycle_lengths(bb, freq, outputs=1, captures=4):
    """lfo(1/freq) on `outputs` outputs: output 1's cycle lengths in samples,
    from core 1's capture of where each segment began, one list per capture
    (each of 31 consecutive cycles)."""
    starts = bb.symbol("g_slope_seg_starts")
    count = bb.symbol("g_slope_seg_start_count")
    bb.eval(f"for i = 1, {outputs} do output[i](lfo({1 / freq!r}, 2)) end")
    bb.wait(0.3)
    lengths = []
    for _ in range(captures):
        bb.mem.write_u32(count, 0)  # arm: the next 64 segment starts
        bb.wait(0.2)
        assert bb.mem.read_u32(count) >= 64, "segment capture didn't fill"
        seg = bb.mem.read_words(starts, 64)
        lengths.append([(seg[i + 2] - seg[i]) & 0xFFFFFFFF for i in range(0, 62, 2)])
    return lengths


def test_asl_cycles_survive_long_lua_callbacks(bb):
    # Regression: core 0 queues each lookahead segment's successor, from the
    # main loop, so a long Lua callback (nb_drumcrow's 100Hz update took 3.4ms)
    # left short segments' successors unqueued past core 1's render-ahead: the
    # output held, then jumped to catch up, at the callback's rate (heard as
    # clicks and distortion, worse the higher the pitch). A Lua count hook
    # now queues them during long callbacks too.
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("busy_m = metro.init{ time = 0.01, event = function() "
            "local t = bb_dbg_us() local x = 0 while bb_dbg_us() - t < 3000 do x = x + 1 end end } busy_m:start()")
    freq = 4000
    lengths = sum(segment_cycle_lengths(bb, freq, outputs=4, captures=12), [])
    bb.eval("busy_m:stop()")
    assert set(lengths) == {rate / freq}, f"cycle lengths {sorted(set(lengths))}, want {rate / freq}"


def held_samples(bb, during):
    """Audio-rate lfos on all outputs; run `during()`, and return how many
    samples core 1 held per output waiting for core 0 to queue a segment."""
    starved = bb.symbol("g_slope_starved_samples")
    bb.eval("for i = 1, 4 do output[i](lfo(1 / 4000, 2)) end")
    bb.wait(0.3)
    for i in range(4):
        bb.mem.write_u32(starved + 4 * i, 0)
    during()
    return bb.mem.read_words(starved, 4)


def test_asl_cycles_survive_garbage_collection(bb):
    # Regression: the Lua collector's atomic phase (one indivisible step, ~2.6ms
    # with nb_drumcrow loaded) and runs of sweep steps held core 0 past core 1's
    # render-ahead every collection cycle (a pop every few seconds). The
    # collector now calls back to drain queued slope callbacks as it works
    # (lua_setgcinterrupt).
    # A big live structure, rewritten (so re-marked in the atomic phase), and
    # garbage to keep collection cycles coming
    bb.eval("live = {} for i = 1, 400 do live[i] = {i, i * 2} end "
            "gc_m = metro.init{ time = 0.01, event = function() "
            "for i = 1, 100 do live[math.random(400)] = {i, i} end end } gc_m:start()")
    held = held_samples(bb, lambda: bb.wait(8.0))
    bb.eval("gc_m:stop()")
    assert held == [0, 0, 0, 0], f"samples held waiting for core 0, per output: {held}"


def test_asl_cycles_survive_repl_commands(bb):
    # Regression: compiling a chunk (each REPL line, each command norns sends)
    # is ~5ms of C with no Lua instructions for the count hook: each held
    # audio-rate outputs (a pop per command, e.g. per nb_drumcrow note). The
    # allocator, which compiling calls throughout, now drains too.
    def commands():
        for i in range(30):
            bb.eval(f"repl_x = {{{i}, 'a{i}', function(v) return v * {i} + math.sin(v) end}}")
            bb.wait(0.05)
    held = held_samples(bb, commands)
    assert held == [0, 0, 0, 0], f"samples held waiting for core 0, per output: {held}"


def capture_output(bb, channel):
    """512 consecutive samples of an output, as the ISR sends them (volts)."""
    import struct
    cap, count = bb.symbol("g_out_capture"), bb.symbol("g_out_capture_count")
    bb.mem.write_u32(bb.symbol("g_out_capture_channel"), channel - 1)
    bb.mem.write_u32(count, 0)
    while bb.mem.read_u32(count) < 512:
        bb.wait(0.01)
    words = bb.mem.read_words(cap, 256)
    return [v / 4096 for v in struct.unpack("<512h", struct.pack("<256I", *words))]


def test_asl_corners_land_between_samples(bb):
    # Regression: the sample in which a lookahead segment ended rendered that
    # segment's end value, and the next segment started on the following
    # sample (as far in as it should have been): every audio-rate corner had
    # one sample stuck at the peak, then a double step (distortion, rising
    # with pitch). The next segment now starts within the sample it begins in.
    rate = bb.stats()["sample_rate_hz"]
    freq, level = 1100, 4  # corners every 10.9 samples, so between samples
    slope = 2 * level / (rate / freq / 2)  # volts per sample along each ramp
    bb.eval(f"output[1](lfo({1 / freq!r}, {level}, 'linear'))")
    bb.wait(0.2)
    for _ in range(4):
        v = capture_output(bb, 1)
        steps = [abs(b - a) for a, b in zip(v, v[1:])]
        assert max(steps) <= slope * 1.02 + 0.002, \
            f"a step of {max(steps):.3f}V, the ramp is {slope:.3f}V/sample"


def test_asl_cycles_have_no_jitter(bb):
    # Regression: each ASL segment started only when core 0 heard the previous
    # one had ended, 1-3 samples later and varying, so audio-rate ASL had
    # cycle-to-cycle jitter (heard as noise FM). With look-ahead, core 1 starts
    # each queued segment on the exact sample: whole-sample periods repeat
    # exactly, and fractional ones alternate around the ideal.
    rate = bb.stats()["sample_rate_hz"]
    for freq, outputs in ((1000, 1), (4000, 1), (4000, 4)):
        ideal = rate / freq
        lengths = sum(segment_cycle_lengths(bb, freq, outputs), [])
        assert set(lengths) == {ideal}, f"{freq}Hz x{outputs}: cycle lengths {sorted(set(lengths))}, want {ideal}"
        bb.reset()
    ideal = rate / 2500  # 9.6 samples
    for lengths in segment_cycle_lengths(bb, 2500):
        assert set(lengths) <= {9, 10}, sorted(set(lengths))
        drift = 0.0
        for n in lengths:  # each cycle starts on the sample at or after its ideal time
            drift += n - ideal
            assert abs(drift) < 1.0, (drift, lengths)


def test_short_segment_sequence_ends_and_calls_done(bb):
    # Look-ahead queues each next segment early, including the end of the
    # sequence: done must still fire once, when the last segment ends
    bb.eval("""
        output[1].volts = 0
        done_n = 0; done_us = nil
        output[1].done = function() done_n = done_n + 1; done_us = bb_dbg_us() end
    """)
    bb.wait(0.1)
    bb.eval("done_n = 0; t0 = bb_dbg_us(); output[1]{ to(5, 0.004), to(-5, 0.004), to(-2, 0), to(3, 0.004) }")
    bb.wait(0.2)
    assert bb.query("done_n") == 1
    took = bb.query("(done_us - t0) / 1000")
    assert 11 < took < 16, f"done after {took}ms, want ~12ms"
    assert abs(bb.query("output[1].volts") - 3) < 0.01


def test_held_short_segments_sustain_and_release(bb):
    bb.eval("output[1].volts = 0; output[1].action = adsr(0.003, 0.003, 2, 0.003, 'linear')")
    bb.wait(0.05)
    bb.eval("output[1](true)")
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts") - 2) < 0.01, "didn't sustain at 2V"
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts") - 2) < 0.01, "didn't hold the sustain"
    bb.eval("output[1](false)")
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts")) < 0.01, "didn't release to 0V"


def test_restarting_a_running_lookahead_chain(bb):
    # A new ASL on a channel replaces the queued segment: the new one runs at
    # its own rate, nothing from the old chain leaks in
    bb.eval("output[1](lfo(1/3000, 2))")
    bb.wait(0.1)
    for freq in (1000, 3000, 1000):
        bb.eval(f"output[1](lfo({1 / freq!r}, 2))")
        bb.wait(0.05)
    rate = bb.stats()["sample_rate_hz"]
    lengths = sum(segment_cycle_lengths(bb, 1000), [])
    assert set(lengths) == {rate / 1000}, sorted(set(lengths))


def test_lookahead_chains_start_reliably(bb):
    # Regression: core 1 posts slope events from both its background render
    # and the audio ISR, and the ISR could interrupt a post half-way: an event
    # was lost, and the lookahead ASL chain it belonged to stopped dead (about
    # 1 in 7 restarts of four 4kHz lfos left one silent)
    starts = bb.symbol("g_slope_seg_start_count")
    actions = bb.base + 4 * STAT_FIELDS.index("slope_actions")
    for round_ in range(30):
        bb.reset()
        bb.eval("for i = 1, 4 do output[i](lfo(1/4000, 2)) end")
        bb.wait(0.15)
        bb.mem.write_u32(starts, 0)
        a0 = bb.mem.read_u32(actions)
        bb.wait(0.1)
        assert bb.mem.read_u32(starts) >= 64, f"round {round_}: output 1's lfo isn't running"
        per_s = ((bb.mem.read_u32(actions) - a0) & 0xFFFFFFFF) / 0.1
        assert per_s > 0.8 * 4 * 8000, f"round {round_}: {per_s:.0f} segments/s, want ~32000 (an lfo stopped)"


def test_short_sequences_end_exactly(bb):
    # Regression: the ISR renders a slope itself when its buffer runs dry,
    # and could preempt the background render of the same slope half-way.
    # With lookahead pausing renders at segment ends, buffers ran dry more,
    # and a slope could end a sample short (an adsr release stuck at 2V/72).
    for round_ in range(40):
        bb.eval("for i = 1, 4 do output[i]{ to(2, 0.003, 'linear'), to(0, 0.003, 'linear') } end")
        bb.wait(0.05)
        ends = [bb.query(f"output[{i}].volts") for i in range(1, 5)]
        assert all(abs(v) < 0.001 for v in ends), f"round {round_}: outputs ended at {ends}, want 0"
