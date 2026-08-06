#!/usr/bin/env python3
"""gensamples.py — synthesise placeholder PCM one-shots into samples/.

These are procedural stand-ins so the alt-boot PCM path can be built and tested
end to end before real field recordings exist. Replace samples/*.raw with actual
recordings (see tools/mksamples.py for the ffmpeg conversion) and rebuild —
no code changes needed.

    python tools/gensamples.py

Writes 8-bit signed mono 48kHz .raw files, one per mode.
"""
import math
import os
import random
import struct

SR = 48000
OUT = "samples"


def write_raw(name, samples):
    path = os.path.join(OUT, f"{name}.raw")
    data = bytearray()
    for s in samples:
        v = max(-128, min(127, int(s * 127)))
        data += struct.pack("b", v)
    with open(path, "wb") as f:
        f.write(data)
    print(f"  {name}.raw: {len(data)} bytes ({len(data)/SR*1000:.0f}ms)")


def env(i, n, decay):
    return math.exp(-decay * i / n)


def horses(dur=0.18):
    """Hoof clop: pitch-dropping thump with a wooden click."""
    n = int(SR * dur)
    out = []
    for i in range(n):
        e = env(i, n, 9)
        f = 190 * math.exp(-7.0 * i / n) + 55
        body = math.sin(2 * math.pi * f * i / SR)
        click = (random.uniform(-1, 1) * math.exp(-70.0 * i / n)) * 0.6
        out.append((body * 0.8 + click) * e)
    return out


def geese(dur=0.30):
    """Honk: buzzy saw with vibrato and a nasal formant."""
    n = int(SR * dur)
    out = []
    ph = 0.0
    lp = 0.0
    for i in range(n):
        e = env(i, n, 4)
        f = 420 + 45 * math.sin(2 * math.pi * 11 * i / SR)
        ph = (ph + f / SR) % 1.0
        saw = 2 * ph - 1
        lp += (saw - lp) * 0.30
        out.append(lp * e)
    return out


def frogs(dur=0.22):
    """Ribbit: two-pulse croak with a resonant body."""
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / n
        # Two bursts, the classic ribb-it
        g = math.exp(-30 * t) + 0.8 * math.exp(-30 * abs(t - 0.45))
        f = 300 + 120 * math.sin(2 * math.pi * 24 * i / SR)
        buzz = math.sin(2 * math.pi * f * i / SR)
        buzz += 0.5 * math.sin(2 * math.pi * f * 2.02 * i / SR)
        out.append(buzz * g * 0.55)
    return out


def rain(dur=0.09):
    """Water drip: high ping with a fast pitch drop."""
    n = int(SR * dur)
    out = []
    for i in range(n):
        e = env(i, n, 14)
        f = 2100 * math.exp(-11.0 * i / n) + 700
        out.append(math.sin(2 * math.pi * f * i / SR) * e)
    return out


def meteors(dur=0.55):
    """Swoosh: filtered noise sweeping down, with a low rumble."""
    n = int(SR * dur)
    out = []
    lp = 0.0
    lp2 = 0.0
    for i in range(n):
        t = i / n
        e = math.sin(math.pi * min(t * 1.15, 1.0)) ** 1.5
        k = 0.45 * math.exp(-2.6 * t) + 0.012
        lp += (random.uniform(-1, 1) - lp) * k
        lp2 += (lp - lp2) * 0.5
        rumble = math.sin(2 * math.pi * 65 * i / SR) * 0.25 * math.exp(-3 * t)
        out.append((lp2 * 1.7 + rumble) * e)
    return out


def cicadas(dur=0.16):
    """Stridulation: a high tone ring-modulated by a fast wing-beat buzz."""
    n = int(SR * dur)
    out = []
    for i in range(n):
        e = env(i, n, 5)
        tone = math.sin(2 * math.pi * 3800 * i / SR)
        buzz = 0.5 + 0.5 * math.sin(2 * math.pi * 190 * i / SR)
        hiss = random.uniform(-1, 1) * 0.12
        out.append((tone * buzz + hiss) * e * 0.7)
    return out


# Per-variant character. In Horses the variant IS THE HOOF (1=LH 2=LF 3=RH
# 4=RF), and a real animal's hind hooves strike lower and heavier than its
# fores — so these are not arbitrary detunes, they carry the front/back shape
# of the gait. Elsewhere the variants are simply different individuals.
VARIANT_PITCH = {
    "horses":  [0.82, 1.12, 0.88, 1.20],   # LH, LF, RH, RF
    "geese":   [1.00, 0.83, 1.19, 0.71],
    "frogs":   [1.00, 0.78, 1.32, 0.93],
    "rain":    [1.00, 1.24, 0.84, 1.41],
    "meteors": [1.00, 0.76, 1.30, 0.88],
    "cicadas": [1.00, 1.05, 0.95, 1.10],
}


def resample(data, ratio):
    """Crude linear resample — adequate for placeholder one-shots."""
    n = int(len(data) / ratio)
    out = []
    for i in range(n):
        x = i * ratio
        j = int(x)
        f = x - j
        a = data[j] if j < len(data) else 0.0
        b = data[j + 1] if j + 1 < len(data) else 0.0
        out.append(a + (b - a) * f)
    return out


def main():
    random.seed(1)
    os.makedirs(OUT, exist_ok=True)
    print(f"Writing placeholder samples to {OUT}/ (8-bit signed mono {SR}Hz)")
    gens = {"horses": horses, "geese": geese, "frogs": frogs,
            "rain": rain, "meteors": meteors, "cicadas": cicadas}
    for mode, fn in gens.items():
        base = fn()
        for v, ratio in enumerate(VARIANT_PITCH[mode], start=1):
            write_raw(f"{mode}_{v}", resample(base, ratio))
    print("\nPlaceholders with four round-robin variants each. Swap in real "
          "recordings\nusing the same _1.._4 naming and rebuild — no code "
          "changes needed.")


if __name__ == "__main__":
    main()
