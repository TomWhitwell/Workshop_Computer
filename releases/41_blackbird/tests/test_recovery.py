"""Recovering from a script that ran the Lua heap out.

Regression: ^^k and uploads cleared the old script by compiling Lua
("crow.reset()", a loop over _user), which fails with the heap full. A script
that held on to all the memory could then only be cleared by a power cycle."""
from bbdbg import HarnessError


def fill_heap(bb):
    """Fill the heap from a script, keeping it all referenced (a global)."""
    try:
        bb.eval("chain = nil for k = 1, 1000000 do chain = {chain, k} end")
    except HarnessError as e:
        assert "not enough memory" in str(e), e
    else:
        raise AssertionError("the heap didn't fill")
    try:
        bb.query("1 + 1")
    except HarnessError as e:
        assert "not enough memory" in str(e), e  # can't even compile a line
    else:
        raise AssertionError("the heap isn't full")


def test_kill_recovers_a_full_heap(bb):
    # Libraries loaded on first use stay loaded, so what a clean kill leaves
    # depends on what ran before: compare with one
    bb.kill_lua()
    bb.wait(0.2)
    clean = bb.stats()["lua_mem_bytes"]
    fill_heap(bb)
    bb.kill_lua()
    bb.wait(0.2)
    assert bb.query("chain == nil")
    assert bb.query("type(init)") == "function"
    mem = bb.stats()["lua_mem_bytes"]
    assert mem < clean + 4 * 1024, f"{mem} bytes after the kill, {clean} after a clean one"


def test_upload_recovers_a_full_heap(bb):
    fill_heap(bb)
    out = bb.upload("recovered = 7\nfunction init() init_ran = true end\n")
    assert "^^ready()" in out, out
    assert bb.query("recovered") == 7
    assert bb.query("init_ran") is True
    assert bb.query("chain == nil")


def test_kill_unloads_libraries_loaded_on_first_use(bb):
    # Regression: sequins, timeline, hotswap and ii load on first use and
    # stayed loaded through ^^k (~31KB), so after a session of scripts a big
    # one (nb_drumcrow's) ran out of memory compiling. ^^k now unloads them.
    bb.kill_lua()
    bb.wait(0.2)
    bb.eval("collectgarbage('collect')")
    clean = bb.stats()["lua_mem_bytes"]
    bb.eval("local a, b, c, d = sequins, timeline, hotswap, ii")
    loaded = bb.stats()["lua_mem_bytes"]
    assert loaded > clean + 20 * 1024, (clean, loaded)
    bb.kill_lua()
    bb.wait(0.2)
    bb.eval("collectgarbage('collect')")
    after = bb.stats()["lua_mem_bytes"]
    assert after < clean + 2 * 1024, f"{after} bytes after ^^k, {clean} before the libraries loaded"
    # and they load again on use
    assert bb.query("type(sequins) .. type(timeline) .. type(hotswap) .. type(ii) .. type(ii_followRx_handler)") \
        == "tabletabletabletablefunction"
