"""Harness round trips: Lua eval, error reporting, the REPL path, ^^k."""
from bbdbg import HarnessError


def test_eval_returns_values(bb):
    assert bb.query("1 + 2") == 3
    assert bb.query("'blackbird'") == "blackbird"
    assert bb.query("nil") is None


def test_eval_captures_print(bb):
    assert bb.eval("print('a', 1, true)") == "a\t1\ttrue\n"


def test_eval_reports_errors(bb):
    for code, kind in (("this is not lua", "load error"), ("error('boom')", "runtime error")):
        try:
            bb.eval(code)
        except HarnessError as e:
            assert kind in str(e), e
        else:
            raise AssertionError(f"{code!r} should have failed")
    assert bb.query("1") == 1  # still alive


def test_repl_path_runs_lua(bb):
    bb.repl("harness_x = 40 + 2\n")
    assert bb.query("harness_x") == 42


def test_repl_multiline(bb):
    bb.repl("```\nharness_f = function(a)\n  return a * 2\nend\n```")
    assert bb.query("harness_f(21)") == 42


def test_kill_lua_clears_user_globals(bb):
    bb.eval("harness_y = 1")
    bb.kill_lua()
    assert bb.query("harness_y") is None


def test_main_loop_is_running(bb):
    a = bb.stats()["heartbeat"]
    bb.wait(0.1)
    assert bb.stats()["heartbeat"] > a
