#!/usr/bin/env python3
"""importbank.py — build the baked sample BANK from a folder of WAVs.

    python tools/importbank.py <folder> [--start N] [--keep-level] [--no-trim]

Converts every .wav in <folder> to 8-bit signed mono 48kHz and writes them as
samples/smpNN.raw, numbered in filename order from --start (default 0).
tools/mksamples.py then bakes those into samples.h at build time.

A BANK, not a per-voice list. Entry NN is not "voice NN's sound" — which
voice plays which entry is a separate mapping (kVoiceSample in
samples_default.h), so the same library can be re-pointed without
re-importing, and the WebUI can eventually re-point it at runtime.

All the actual audio work — resampling, silence trimming, loudness matching,
the TPDF dither on the way down to 8 bits — is importwav.py's, reused rather
than reimplemented. See that file for why each step is the way it is.

Example:
    python tools/importbank.py samples/Cheetah_SpecDrum/orig

Prints the index each file landed on, which is what you need to fill in
kVoiceSample.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import importwav as iw

OUT_DIR = "samples"
MAX_SAMPLES = 64


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    keep_level = "--keep-level" in sys.argv
    no_trim = "--no-trim" in sys.argv

    start = 0
    for i, a in enumerate(sys.argv):
        if a == "--start" and i + 1 < len(sys.argv):
            start = int(sys.argv[i + 1])

    if not args:
        print(__doc__)
        return
    src = args[0]

    if not os.path.isdir(src):
        print(f"No such directory: {src}")
        return

    wavs = sorted(f for f in os.listdir(src) if f.lower().endswith(".wav"))
    if not wavs:
        print(f"No .wav files in {src}/")
        return

    # --- pass 1: read and measure ---
    loaded = []
    for fn in wavs:
        data, desc = iw.read_wav(os.path.join(src, fn))
        if not no_trim:
            data = iw.trim(data)
        loaded.append((fn, data, desc))

    # One loudness target across the whole bank, so swapping a voice's sample
    # does not jump the mix. Median of the sources, floored so a quiet library
    # still uses most of the eight bits available to it.
    all_rms = sorted(iw.rms(d) for _, d, _ in loaded)
    target = all_rms[len(all_rms) // 2] if all_rms else 0.1
    target = max(target, 0.12)
    if not keep_level:
        print(f"Loudness target: RMS {target:.4f} "
              f"(median of {len(all_rms)} sources)\n")

    # --- pass 2: level, fade, write ---
    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for i, (fn, data, desc) in enumerate(loaded):
        idx = start + i
        if idx >= MAX_SAMPLES:
            print(f"  {fn}: past the {MAX_SAMPLES}-entry limit, skipped")
            continue
        out = data if keep_level else iw.loudness_match(data, target)
        out = iw.fade_out(list(out))
        name = f"smp{idx:02d}"
        _path, n = iw.write_raw(name, out)
        total += n
        print(f"  [{idx:2d}] {fn:<24} {desc:<22} {n/48.0:5.0f} ms")

    print(f"\nWrote {total} bytes ({total/48000.0:.2f}s) into {OUT_DIR}/.")
    print("Now map indices to voices in samples_default.h, then rebuild.")


if __name__ == "__main__":
    main()
