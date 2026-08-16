#!/usr/bin/env python3
"""crosscheck.py - prove the C++ LevelTracker matches the Python model.

tools/ghostsim.py verifies the ALGORITHM. This file verifies the PORT: that
levels.cpp actually implements the thing ghostsim.py tested, rather than
something subtly different that happens to compile.

It works by compiling the real levels.cpp for the host with a tiny C++ driver,
then replaying the same gestures through both implementations and diffing the
event streams. There is no host g++ on this machine, so it SKIPS cleanly if one
is unavailable - it is a bonus check, not a gate. The ARM cross-compiler cannot
help here because the output has to actually run.

Run:  python tools/crosscheck.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import ghostsim as gs   # noqa: E402


DRIVER = r'''
// Host driver for levels.cpp. Reads a gesture script on stdin, prints events.
//
// Line format:  <combo> <ticks>
// Output:       one "EVENT NAME" per fired event.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "levels.h"

using namespace nib;

static const char *kNames[10] = {"A","B","C","D","AB","AC","AD","BC","BD","CD"};

// Mirror of ghostsim.py's FourVoltages: finite slew, no rest state.
struct Hw {
    const int32_t *levels;
    double value, target;
    int slewTicks;
    void setCombo(int c) { target = (double)levels[c]; }
    int32_t tick() {
        double d = target - value;
        if (d < 0.5 && d > -0.5) value = target;
        else value += d / slewTicks;
        // round-half-away-from-zero, matching Python's round() on .5 well
        // enough for integers this size
        return (int32_t)(value < 0 ? value - 0.5 : value + 0.5);
    }
};

int main(int argc, char **argv)
{
    int32_t levels[kNumLevels];
    // levels come in as argv[1..10]
    if (argc < 1 + kNumLevels) { fprintf(stderr, "need %d levels\n", kNumLevels); return 2; }
    for (int i = 0; i < kNumLevels; i++) levels[i] = atoi(argv[1 + i]);

    LevelTracker t;
    t.LearnFrom(levels);

    Hw hw{levels, (double)levels[0], (double)levels[0], 6};

    int combo, ticks;
    while (scanf("%d %d", &combo, &ticks) == 2) {
        // A combo index of 10+ means "an unlearned level", supplied as a raw
        // value on the next token - used for the triple test.
        if (combo >= kNumLevels) {
            int raw; if (scanf("%d", &raw) != 1) break;
            hw.target = (double)raw;
        } else {
            hw.setCombo(combo);
        }
        for (int i = 0; i < ticks; i++) {
            int8_t idx = -1;
            LevelEvent ev = t.Step(hw.tick(), idx);
            if (ev == LevelEvent::Trigger)         printf("Trigger %s\n", kNames[idx]);
            else if (ev == LevelEvent::GhostArmed) printf("GhostArmed %s\n", kNames[idx]);
        }
    }
    return 0;
}
'''

# levels.cpp includes pico.h for __not_in_flash_func. On the host we just
# neutralise it.
SHIM = r'''
#pragma once
#define __not_in_flash_func(f) f
'''


def find_host_cxx():
    for c in ("g++", "clang++", "c++"):
        p = shutil.which(c)
        if p:
            return p
    return None


def run_cpp(exe, levels, script):
    args = [exe] + [str(v) for v in levels]
    inp = "".join("%d %d\n" % (c, t) if r is None else "%d %d %d\n" % (c, t, r)
                  for (c, t, r) in script)
    out = subprocess.run(args, input=inp, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("driver failed: %s" % out.stderr)
    return [tuple(l.split(None, 1)) for l in out.stdout.splitlines() if l.strip()]


def run_py(levels, script):
    t = gs.LevelTracker()
    t.learn_from(levels)
    hw = gs.FourVoltages(levels)
    ev = []
    for (combo, ticks, raw) in script:
        if raw is not None:
            hw.target = float(raw)
        else:
            hw.set_combo(combo)
        for _ in range(ticks):
            e, idx = t.step(hw.tick())
            if e != gs.NONE:
                ev.append((e, gs.NAMES[idx]))
    return ev


SCRIPTS = {
    "drums hold-C tap-A/B/A": [(2, 120, None), (5, 80, None), (2, 80, None),
                               (7, 80, None), (2, 80, None), (5, 80, None),
                               (2, 80, None)],
    "AB -> A(ghost) -> B":    [(0, 120, None), (4, 80, None), (0, 80, None),
                               (1, 80, None)],
    "ghost cleared on leave": [(0, 120, None), (4, 80, None), (0, 80, None),
                               (2, 80, None), (0, 80, None)],
    "pair -> pair":           [(0, 120, None), (4, 80, None), (9, 80, None)],
    "pair -> non-member":     [(0, 120, None), (4, 80, None), (2, 80, None)],
    "long hold":              [(0, 120, None), (4, 1200, None)],
}


def main():
    cxx = find_host_cxx()
    if not cxx:
        print("crosscheck: no host C++ compiler found - SKIPPED")
        print("            (the ARM cross-compiler cannot help; the code must run)")
        print("            ghostsim.py still verifies the algorithm itself.")
        return 0

    tmp = tempfile.mkdtemp(prefix="nibblexc")
    try:
        with open(os.path.join(tmp, "driver.cpp"), "w") as f:
            f.write(DRIVER)
        os.makedirs(os.path.join(tmp, "pico"), exist_ok=True)
        with open(os.path.join(tmp, "pico.h"), "w") as f:
            f.write(SHIM)

        exe = os.path.join(tmp, "driver")
        cmd = [cxx, "-std=c++17", "-O1", "-I" + ROOT, "-I" + tmp,
               os.path.join(tmp, "driver.cpp"),
               os.path.join(ROOT, "levels.cpp"),
               "-o", exe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print("crosscheck: build FAILED")
            print(r.stderr)
            return 1

        levels = gs.spaced_levels()
        bad = 0
        for name, script in SCRIPTS.items():
            got = run_cpp(exe, levels, script)
            want = run_py(levels, script)
            if got == want:
                print("  PASS  %s" % name)
            else:
                print("  FAIL  %s" % name)
                print("          C++:    %r" % (got,))
                print("          Python: %r" % (want,))
                bad += 1

        if bad:
            print("%d mismatch(es): the port does not match the model" % bad)
            return 1
        print("C++ matches the Python model on every gesture")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
