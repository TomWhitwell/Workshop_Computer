"""metro[1]..metro[8], counts and stages, as on crow.

Regression: Lua's metro ids (1-8) went to the C side (0-7) unconverted, so
metro[8] never started (nb_drumcrow's update loop runs on it), and start()
ignored count and stage."""


def test_all_eight_metros_tick(bb):
    bb.eval("""
        m_ticks = {}
        for i = 1, 8 do
            m_ticks[i] = 0
            metro[i].event = function() m_ticks[i] = m_ticks[i] + 1 end
            metro[i].time = 0.01
            metro[i]:start()
        end
    """)
    bb.wait(0.5)
    bb.eval("for i = 1, 8 do metro[i]:stop() end")
    ticks = [bb.query(f"m_ticks[{i}]") for i in range(1, 9)]
    assert all(t >= 30 for t in ticks), f"ticks per metro in 0.5s at 10ms: {ticks}"


def test_count_stops_the_metro_and_stages_start_at_1(bb):
    bb.eval("""
        m_stages = {}
        m = metro.init{ event = function(s) m_stages[#m_stages + 1] = s end, time = 0.01, count = 4 }
        m:start()
    """)
    bb.wait(0.3)
    assert bb.query("table.concat(m_stages, ',')") == "1,2,3,4"
    # starting again counts from the start again
    bb.eval("m_stages = {} m:start()")
    bb.wait(0.3)
    assert bb.query("table.concat(m_stages, ',')") == "1,2,3,4"
