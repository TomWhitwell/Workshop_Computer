--- bb ii follower test
-- Upload to a crow on the same ii bus as blackbird (blackbird answers at
-- crow address 4). Patch: blackbird CV Out 1 -> crow input 1, crow output 1 ->
-- blackbird CV In 1. Run tests/test_ii_follow.py on blackbird.
--
-- blackbird starts a round by raising its CV Out 1 to a whole number of
-- volts, n. crow then leads at blackbird (ii.crow[4]) and mirrors the answer
-- it gets onto its output 1, where blackbird reads it:
--   n = 1..5: volts(2, n/2) and call1(n), then query1(n) -> output 1
--   n = 6:    get('output', 2) -> output 1   (a built-in getter)

local bb = ii.crow[4]

function init()
    output[1].volts = 0
    input[1].mode('change', 0.5, 0.1, 'rising')
end

input[1].change = function()
    -- read n once blackbird's CV out has settled (it's smoothed PWM: a few ms)
    clock.run(function()
        clock.sleep(0.02)
        local n = math.floor(input[1].volts + 0.5)
        if n >= 1 and n <= 5 then
            bb.volts(2, n / 2)
            bb.call1(n)
            ii.crow[4].get('query1', n) -- query1: one argument (query0 has none)
        elseif n == 6 then
            ii.crow[4].get('output', 2)
        end
    end)
end

ii.crow.event = function(e, value)
    if e.device == 4 then output[1].volts = value end
end
