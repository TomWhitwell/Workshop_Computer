"""Run the bbbowery scripts and monome/bowery against the card.

Each script is uploaded the way druid's `r` does (^^s, script, ^^e), so it is
loaded and its init() runs. Then it runs for BB_BOWERY_SECONDS (default 5)
with a generic set of fake inputs: clocks on the pulse inputs and CV 1, a sweep
on CV 2, the knobs part-way. A script passes if it loads, and the runner's
checks hold: no Lua errors, no ProcessSample overruns, no stalled main loop.
It must also drop no events. Output activity is reported, not asserted, because
some scripts legitimately sit still with this input.

monome/bowery is fetched at BOWERY_REF into tests/.cache/bowery, or set
BB_BOWERY_DIR to use your own checkout. bowery/legacy (crow 1.x scripts) only
runs with BB_BOWERY_LEGACY=1.

Known issues go in EXPECTED (reported as XFAIL) or SKIP. Remove an entry when
its script starts passing (the run reports XPASS).
"""
import os
import subprocess
import sys

from bbdbg import GENERIC_STIMULUS, Skip, apply_stimulus, expected_failure, outputs_moving

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
BOWERY_URL = "https://github.com/monome/bowery"
BOWERY_REF = os.environ.get("BB_BOWERY_REF", "a09b6aeb91088e6dc16dfbc61fea9b68e58a8006")
BOWERY_DIR = os.environ.get("BB_BOWERY_DIR", os.path.join(HERE, ".cache", "bowery"))
SECONDS = float(os.environ.get("BB_BOWERY_SECONDS", "5"))

# script id -> reason. Scripts that fail for a known, accepted reason.
EXPECTED = {
}

# script id -> reason. Scripts that shouldn't be run at all.
SKIP = {
    "bowery/cali": "crow's calibration utility: needs crow's cal backend (calibrate_save etc.), "
                   "which blackbird doesn't have; blackbird calibrates from EEPROM",
}

# Crow scripts mostly take a clock or gate on input 1 and a CV on input 2;
# bbbowery scripts also use the pulse inputs and knobs. GENERIC_STIMULUS
# (util/bbdbg.py) covers both.
STIMULUS = GENERIC_STIMULUS

# script id -> replacement or extra stimulus entries, for scripts that need
# something specific to do anything.
STIMULUS_FOR = {
}


def fetch_bowery():
    if os.path.isdir(os.path.join(BOWERY_DIR, ".git")) or os.environ.get("BB_BOWERY_DIR"):
        head = subprocess.run(["git", "-C", BOWERY_DIR, "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        if os.environ.get("BB_BOWERY_DIR") or head == BOWERY_REF:
            return True
    try:
        os.makedirs(BOWERY_DIR, exist_ok=True)
        git = ["git", "-C", BOWERY_DIR]
        if not os.path.isdir(os.path.join(BOWERY_DIR, ".git")):
            subprocess.run(git + ["init", "-q"], check=True)
            subprocess.run(git + ["remote", "add", "origin", BOWERY_URL], check=True)
        subprocess.run(git + ["fetch", "-q", "--depth", "1", "origin", BOWERY_REF], check=True, timeout=60)
        subprocess.run(git + ["checkout", "-q", "FETCH_HEAD"], check=True)
        return True
    except (subprocess.SubprocessError, OSError) as e:
        print(f"test_bowery: couldn't fetch {BOWERY_URL}@{BOWERY_REF[:8]}: {e}", file=sys.stderr)
        return False


def scripts():
    """(script id, path) for every script to run, in a stable order."""
    found = []
    bb_dir = os.path.join(PROJECT, "bbbowery")
    found += [(f"bbbowery/{f[:-4]}", os.path.join(bb_dir, f)) for f in sorted(os.listdir(bb_dir)) if f.endswith(".lua")]
    if fetch_bowery():
        subdirs = ["", "snippets"] + (["legacy"] if os.environ.get("BB_BOWERY_LEGACY") else [])
        for sub in subdirs:
            d = os.path.join(BOWERY_DIR, sub)
            prefix = "bowery/" + (sub + "/" if sub else "")
            found += [(prefix + f[:-4], os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".lua")]
    return found


def run_script(bb, script_id, path):
    if script_id in SKIP:
        raise Skip(SKIP[script_id])
    with open(path, encoding="utf-8") as f:
        source = f.read()

    # Inputs first, so init() sees patched jacks, as it would with real cables
    apply_stimulus(bb, STIMULUS + STIMULUS_FOR.get(script_id, []))
    bb.wait(0.05)

    out = bb.upload(source)
    assert "script evaluation failed" not in out, f"didn't load:\n{out.strip()}"
    assert "^^ready()" in out, f"no ^^ready() after upload:\n{out.strip()}"

    # Run, sampling the outputs to report whether anything moved
    bb.info["outputs moving"] = outputs_moving(bb, SECONDS) or "none"

    st = bb.stats()
    drops = {k: v for k, v in st.items() if k.startswith("drops_") and v}
    assert not drops, f"dropped events: {drops}"


def _make_test(script_id, path):
    def test(bb):
        run_script(bb, script_id, path)
    test.__name__ = "test_" + script_id.replace("/", "_").replace("-", "_")
    if script_id in EXPECTED:
        test = expected_failure(EXPECTED[script_id])(test)
    return test


for _id, _path in scripts():
    _t = _make_test(_id, _path)
    globals()[_t.__name__] = _t
