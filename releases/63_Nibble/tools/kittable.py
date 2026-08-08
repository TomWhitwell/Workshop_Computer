#!/usr/bin/env python3
"""kittable.py - print the DRUMS kit as documentation, read from drums.cpp.

The voice table is the kind of thing that drifts silently: someone nudges a
decay shift and the README keeps quoting the old number for a year. This parses
the actual source and prints the markdown, so the docs are generated from the
thing they describe rather than remembered alongside it.

Run:  python tools/kittable.py
"""

import io
import os
import re
import sys

# The tables contain arrows and em-dashes, and the Windows console defaults to
# cp1252, which cannot encode them. Force UTF-8 rather than degrading the output.
if hasattr(sys.stdout, "buffer"):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8",
                                  errors="replace", line_buffering=True)

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "drums.cpp")

FS = 48000
PHASE_BITS = 18
ENV_FRAC = 6

NAMES = ["A", "B", "C", "D"]


def parse():
    src = open(SRC, encoding="utf-8").read()

    block = src[src.index("const DrumSpec kVoices"):]
    block = block[:block.index("};")]
    voices = []
    for line in block.split("\n"):
        m = re.match(
            r"\s*\{\s*HzToInc\((\d+)\)\s*,\s*HzToInc\((\d+)\)\s*,"
            r"\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
            r"\s*(\d+)\s*\}\s*,\s*//\s*\d+\s+(.*)", line)
        if m:
            g = m.groups()
            voices.append(dict(
                hi=int(g[0]), lo=int(g[1]), decay=int(g[2]), noise=int(g[3]),
                mix=int(g[4]), fall=int(g[5]), metal=int(g[6]),
                level=int(g[7]), name=g[8].strip()))

    gblock = src[src.index("const int8_t kGestureVoice"):]
    gblock = gblock[:gblock.index("};")]
    gest = []
    for line in gblock.split("\n"):
        m = re.match(r"\s*/\*\s*[ABCD]\s*\*/\s*\{([^}]*)\}", line)
        if m:
            gest.append([int(x) for x in m.group(1).split(",")])

    return voices, gest


def tail_ms(v):
    """How long the voice stays above ~1% of full scale."""
    e = 4095 << ENV_FRAC
    ne = (4095 << ENV_FRAC) if v["mix"] else 0
    n = last = 0
    while (e > 0 or ne > 0) and n < FS * 20:
        if e > 0:
            e -= (e >> v["decay"]) + 1
        if ne > 0:
            ne -= (ne >> v["noise"]) + 1
        body = (2047 * (e >> ENV_FRAC)) >> 12
        nz = (2031 * (ne >> ENV_FRAC)) >> 12
        amp = ((((body * (256 - v["mix"])) + (nz * v["mix"])) >> 8)
               * v["level"]) >> 8
        if abs(amp) > 20:
            last = n
        n += 1
    return last / FS * 1000.0


def fall_ms(v):
    """Time for the pitch to fall 90% of the way to its floor."""
    if not v["fall"] or v["hi"] == v["lo"]:
        return 0.0
    inc = lambda hz: (hz << PHASE_BITS) // FS
    d = (inc(v["hi"]) - inc(v["lo"])) << 8
    tgt = int(d * 0.1)
    n = 0
    while d > tgt and n < FS * 2:
        d -= (d >> v["fall"]) + 1
        n += 1
    return n / FS * 1000.0


def main():
    voices, gest = parse()
    if len(voices) != 12 or len(gest) != 4:
        print("kittable: parse failed (%d voices, %d gesture rows)"
              % (len(voices), len(gest)))
        return 1

    print("### The gestures\n")
    print("| Hold | tap A | tap B | tap C | tap D |")
    print("|------|-------|-------|-------|-------|")
    for i, row in enumerate(gest):
        cells = []
        for j, vi in enumerate(row):
            cells.append("—" if vi < 0 else voices[vi]["name"])
        print("| **%s** | %s |" % (NAMES[i], " | ".join(cells)))

    print("\n### The voices\n")
    print("| Sound | Pitch | Fall | Length | Level |")
    print("|-------|-------|------|--------|-------|")
    for v in voices:
        pitch = ("%d Hz" % v["hi"] if v["hi"] == v["lo"]
                 else "%d → %d Hz" % (v["hi"], v["lo"]))
        f = fall_ms(v)
        print("| %s | %s | %s | %.0f ms | %d%% |"
              % (v["name"], pitch, ("%.0f ms" % f) if f else "—",
                 tail_ms(v), round(100 * v["level"] / 256)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
