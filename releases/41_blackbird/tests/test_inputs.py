"""Mocked hardware inputs reach Lua through the real ComputerCard/detection paths."""


def close(a, b, tol):
    return a is not None and abs(a - b) <= tol


def test_cv_inputs(bb):
    for volts in (3.0, -2.0, 0.0):
        bb.set_input("cv1", volts)
        bb.set_input("cv2", -volts)
        bb.wait(0.05)  # CV inputs are smoothed (~240Hz LPF)
        assert close(bb.query("input[1].volts"), volts, 0.05), bb.query("input[1].volts")
        assert close(bb.query("input[2].volts"), -volts, 0.05), bb.query("input[2].volts")


def test_audio_inputs(bb):
    bb.set_input("audio1", 1.5)
    bb.set_input("audio2", -4.0)
    bb.wait(0.02)
    assert close(bb.query("bb.audioin[1].volts"), 1.5, 0.05)
    assert close(bb.query("bb.audioin[2].volts"), -4.0, 0.05)


def test_knobs(bb):
    for value in (0.25, 0.5, 0.75):
        bb.set_input("knob_main", value)
        bb.set_input("knob_x", 1 - value)
        bb.set_input("knob_y", value)
        want = (value, 1 - value, value)
        for _ in range(15):  # the knobs settle in ~20ms, but the first change after a reset can take longer
            bb.wait(0.02)
            got = (bb.query("bb.knob.main"), bb.query("bb.knob.x"), bb.query("bb.knob.y"))
            if all(close(g, w, 0.01) for g, w in zip(got, want)):
                break
        assert all(close(g, w, 0.01) for g, w in zip(got, want)), \
            f"knobs main/x/y = {got}, set {value}/{1 - value}/{value}"


def test_switch_position_and_change_callback(bb):
    bb.set_input("switch", "middle")
    bb.wait(0.15)
    bb.eval("sw_hits = {}; bb.switch.change = function(p) table.insert(sw_hits, p) end")
    for pos in ("up", "middle", "down"):
        bb.set_input("switch", pos)
        bb.wait(0.15)  # the main loop polls the switch every 50ms
        assert bb.query("bb.switch.position") == pos
    assert bb.query("table.concat(sw_hits, ',')") == "up,middle,down"


def test_overridden_jacks_report_connected(bb):
    bb.set_input("pulse1", False)
    bb.set_input("cv2", 0)
    bb.wait(0.02)
    assert bb.query("bb.connected.pulse1") is True
    assert bb.query("bb.connected.cv2") is True
    bb.set_connected(pulse1=False)
    bb.wait(0.02)
    assert bb.query("bb.connected.pulse1") is False


def test_pulse_in_counts_edges(bb):
    bb.eval("""
        pulse_n = 0
        bb.pulsein[1]{ mode = 'change', direction = 'rising' }
        bb.pulsein[1].change = function(s) pulse_n = pulse_n + 1 end
    """)
    bb.square("pulse1", 50, duty=0.2)
    bb.wait(1.0)
    bb.release_input("pulse1")
    n = bb.query("pulse_n")
    assert 45 <= n <= 55, n


def test_input_change_detection(bb):
    bb.eval("""
        change_n = 0
        input[1].mode('change', 1.0, 0.1, 'both')
        input[1].change = function(s) change_n = change_n + 1 end
    """)
    t0 = bb.query("bb_dbg_us()")
    bb.square("cv1", 10, low=0, high=3)
    bb.wait(1.0)
    bb.release_input("cv1")
    secs = (bb.query("bb_dbg_us()") - t0) / 1e6  # the card's time: the host's wait can run long
    n = bb.query("change_n")
    expected = 2 * 10 * secs  # 10Hz square, both edges
    assert abs(n - expected) <= 3, f"{n} changes in {secs:.2f}s, expected ~{expected:.0f}"
