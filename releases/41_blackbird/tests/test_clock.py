"""Internal clock: tempo changes and clock.start() keep the beat grid."""


def test_tempo_change_does_not_race_beats(bb):
    # Regression: clock_internal_start() used to reset the next-beat time to 0,
    # so the clock caught up from boot at one beat per ms (~2 beats per second
    # of uptime) after every tempo change or start, firing clock.sync loops
    # each ms. The card has been up at least a few seconds here, so that would
    # show as tens of extra beats and syncs.
    bb.eval("""
        syncs = 0
        clock.run(function() while true do clock.sync(1/4); syncs = syncs + 1 end end)
        beats_before = clock.get_beats()
        clock.tempo = 120
        beats_after_tempo = clock.get_beats()
    """)
    bb.wait(1.0)
    before, after_tempo, after_1s, syncs = (bb.query(e) for e in
        ("beats_before", "beats_after_tempo", "clock.get_beats()", "syncs"))
    assert abs(after_tempo - before) <= 1.0, f"tempo change moved beats {before} -> {after_tempo}"
    assert 1.0 <= after_1s - before <= 3.5, f"beats {before} -> {after_1s} in 1s at 120bpm"
    assert 7 <= syncs <= 11, f"{syncs} clock.sync(1/4) wakeups in ~1s at 120bpm (expect ~8)"


def test_clock_start_does_not_race_beats(bb):
    # clock.start() restarts at beat 0 (then one forced beat); get_beats() only
    # sees it from core 1's next clock tick, so read it a little later
    bb.eval("clock.tempo = 120; clock.stop(); clock.start()")
    bb.wait(0.05)
    start = bb.query("clock.get_beats()")
    bb.eval("""
        syncs = 0
        clock.run(function() while true do clock.sync(1/4); syncs = syncs + 1 end end)
    """)
    bb.wait(1.0)
    now, syncs = bb.query("clock.get_beats()"), bb.query("syncs")
    assert start <= 1.5, f"beat {start} 50ms after clock.start()"
    assert 1.0 <= now - start <= 3.5, f"beats {start} -> {now} in 1s at 120bpm after clock.start()"
    assert 7 <= syncs <= 11, f"{syncs} clock.sync(1/4) wakeups in ~1s after clock.start() (expect ~8)"


def test_sync_grid_is_steady(bb):
    # clock.sync wakes on the beat grid, so intervals average exactly one
    # division with ~1ms tick quantisation
    bb.eval("""
        clock.tempo = 240
        gaps = {}
        clock.run(function()
            clock.sync(1/4)
            local last = bb_dbg_us()
            for i = 1, 16 do clock.sync(1/4); local now = bb_dbg_us(); gaps[i] = now - last; last = now end
        end)
    """)
    bb.wait(2.0)
    gaps = [bb.query(f"gaps[{i}]") for i in range(1, 17)]
    assert all(g is not None for g in gaps), gaps
    assert all(abs(g - 62500) <= 2000 for g in gaps), gaps
    assert abs(sum(gaps) / len(gaps) - 62500) <= 300, gaps
