#!/usr/bin/env python3
"""importwav.py — turn recordings into the .raw one-shots the card bakes in.

Drop WAV files into samples/incoming/ named for their mode and variant, then:

    python tools/importwav.py

It reads samples/incoming/*.wav, converts each to 8-bit signed mono 48kHz,
trims silence, normalises, applies a short fade-out, and writes samples/*.raw
ready for the build. Uses only the Python standard library — no ffmpeg needed.

NAMING
    horses_1.wav  horses_2.wav  horses_3.wav  horses_4.wav
    geese_1.wav   frogs_1.wav   rain_1.wav    meteors_1.wav   cicadas_1.wav

The number is the round-robin variant, and in HORSES it is the hoof:
    1 = left hind   2 = left fore   3 = right hind   4 = right fore
Hind hooves strike lower and heavier than fores on a real animal, so putting
them in the right slots is most of what makes a gait sound like an animal.
Elsewhere the four are just different individuals.

Fewer than four is fine — missing variants reuse the ones you did supply, and a
bare `horses.wav` with no number is used for all four.

WHAT MAKES A GOOD SOURCE
  * A single isolated hit. No reverb tail you cannot trim, no other animals.
  * Trimmed tight to the transient; the attack is what identifies the sound.
  * Dry. The card has no reverb, so recorded ambience is baked in forever.
  * Mono or stereo (stereo is summed), any sample rate, 8/16/24/32-bit or float.

LENGTH GUIDE (the card holds ~41 seconds of audio in total)
    horses   ~180ms    rain     ~120ms
    geese    ~400ms    meteors  ~700ms
    frogs    ~300ms    cicadas  ~200ms
Longer is allowed; you just spend more flash. The script warns past 1 second.

Options:
    --keep-level   skip normalisation (use if you tuned relative levels yourself)
    --no-trim      skip silence trimming
"""
import array
import math
import os
import random
import struct
import sys
import wave
import zlib

SR = 48000
IN_DIR = os.path.join("samples", "incoming")
OUT_DIR = "samples"
MODES = ["horses", "geese", "frogs", "rain", "meteors", "cicadas"]
NUM_VARIANTS = 8          # must match kNumVariants in voices.h
WARN_MS = 1000

# Sample libraries are named after the animal, not the mode. Accept both.
ALIASES = {
    "horse": "horses", "hoof": "horses", "clop": "horses",
    "goose": "geese", "honk": "geese",
    "frog": "frogs", "ribbit": "frogs", "toad": "frogs",
    "drip": "rain", "water": "rain", "droplet": "rain",
    "whoosh": "meteors", "swoosh": "meteors", "meteor": "meteors",
    "cicada": "cicadas", "cricket": "cicadas", "insect": "cicadas",
}


def decode(raw, width, nch):
    """Bytes -> list of float -1..1, summed to mono. Handles 8/16/24/32-bit
    PCM and 32-bit float. Written out longhand because the stdlib `audioop`
    module was removed in Python 3.13."""
    if width == 1:
        # WAV 8-bit is UNSIGNED, unlike every other depth.
        chans = [(b - 128) / 128.0 for b in raw]
    elif width == 3:
        n = len(raw) // 3
        chans = []
        for i in range(n):
            b0, b1, b2 = raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]
            v = b0 | (b1 << 8) | (b2 << 16)
            if v & 0x800000:
                v -= 0x1000000
            chans.append(v / 8388608.0)
    elif width == 2:
        a = array.array("h")
        a.frombytes(raw[:len(raw) // 2 * 2])
        chans = [v / 32768.0 for v in a]
    elif width == 4:
        a = array.array("i")
        a.frombytes(raw[:len(raw) // 4 * 4])
        chans = [v / 2147483648.0 for v in a]
    else:
        raise ValueError(f"unsupported sample width {width}")

    if nch <= 1:
        return chans
    # Sum channels to mono.
    return [sum(chans[i:i + nch]) / nch for i in range(0, len(chans) - nch + 1, nch)]


def decode_float32(raw, nch):
    n = len(raw) // 4
    chans = list(struct.unpack("<%df" % n, raw[:n * 4]))
    if nch <= 1:
        return chans
    return [sum(chans[i:i + nch]) / nch for i in range(0, len(chans) - nch + 1, nch)]


def resample(data, src_rate):
    """Linear resample to SR. Fine for one-shots; these are short and we are
    heading for 8-bit anyway."""
    if src_rate == SR or not data:
        return data
    ratio = src_rate / float(SR)
    n = int(len(data) / ratio)
    out = []
    for i in range(n):
        x = i * ratio
        j = int(x)
        f = x - j
        a = data[j]
        b = data[j + 1] if j + 1 < len(data) else 0.0
        out.append(a + (b - a) * f)
    return out


def read_wav(path):
    """Return (samples as list of float -1..1 at SR, source description)."""
    with wave.open(path, "rb") as w:
        nch, width, rate, nframes = (w.getnchannels(), w.getsampwidth(),
                                     w.getframerate(), w.getnframes())
        comp = w.getcomptype()
        raw = w.readframes(nframes)

    kind = "float" if comp == "FLOA" else f"{width * 8}-bit"
    desc = f"{rate}Hz {kind} {'stereo' if nch == 2 else 'mono'}"

    if comp == "FLOA" or (comp not in ("NONE", "PCM ") and width == 4):
        data = decode_float32(raw, nch)
    else:
        data = decode(raw, width, nch)

    return resample(data, rate), desc


def trim(data, floor=0.004):
    """Drop leading and trailing near-silence, keeping a tiny pre-roll."""
    start = 0
    while start < len(data) and abs(data[start]) < floor:
        start += 1
    end = len(data)
    while end > start and abs(data[end - 1]) < floor:
        end -= 1
    start = max(0, start - 48)          # 1ms of pre-roll so attacks stay intact
    return data[start:end] if end > start else data


def rms(data):
    if not data:
        return 0.0
    return (sum(v * v for v in data) / len(data)) ** 0.5


def loudness_match(data, target_rms, ceiling=0.94):
    """Match PERCEIVED loudness, then tame whatever peaks that produces.

    Peak normalisation is the obvious thing and it does not work here: a sample's
    peak is usually a single transient, so two recordings normalised to the same
    peak can still differ hugely in how loud they sound. A honk whose body sits
    at RMS 0.004 and one at 0.059 both peak near 1.0 and sound nothing alike.

    So: scale by RMS to equalise the bodies, then soft-limit the result. The
    limiter is a smooth tanh-ish curve rather than a hard clip, applied only to
    the part above the knee, so transients round over instead of squaring off.
    """
    r = rms(data)
    if r < 1e-9:
        return data
    g = target_rms / r
    out = [v * g for v in data]

    knee = ceiling * 0.7
    limited = []
    for v in out:
        a = abs(v)
        if a > knee:
            over = (a - knee) / (1.0 - knee) if knee < 1.0 else 0.0
            # x/(1+x) compresses [0,inf) into [0,1) smoothly.
            a = knee + (ceiling - knee) * (over / (1.0 + over))
            v = a if v >= 0 else -a
        limited.append(v)
    return limited


def normalise(data, target=0.92):
    peak = max((abs(v) for v in data), default=0.0)
    if peak < 1e-6:
        return data
    g = target / peak
    return [v * g for v in data]


def fade_out(data, ms=4):
    n = min(int(SR * ms / 1000), len(data))
    for i in range(n):
        data[len(data) - n + i] *= 1.0 - (i / n)
    return data


def write_raw(name, data):
    """Quantise to 8-bit signed, with TPDF dither.

    Without dither, quantisation error CORRELATES with the signal, which is why
    8-bit reads as grit riding on a decaying tail rather than as hiss. A
    triangular dither (the sum of two uniform randoms, +/-1 LSB peak) decorrelates
    it: technically more noise, audibly much less objectionable, and it costs
    nothing at runtime because it is baked in here.

    Seeded per file so a rebuild is reproducible - a sample library that changes
    byte-for-byte on every bake would make it impossible to tell a real change
    from dither noise in a diff.
    """
    rng = random.Random(zlib.crc32(name.encode()))
    out = bytearray()
    for v in data:
        d = rng.random() + rng.random() - 1.0      # TPDF, +/-1 LSB
        s = int(math.floor(v * 127 + d + 0.5))
        out.append((max(-128, min(127, s))) & 0xFF)
    path = os.path.join(OUT_DIR, f"{name}.raw")
    with open(path, "wb") as f:
        f.write(out)
    return path, len(out)


def resolve_mode(stem):
    """Map a source filename onto one of the card's mode names.

    Sample libraries are named after the animal, not after the mode, so
    HORSE_3.wav and WHOOSH_2.wav need to land in horses_ and meteors_.
    """
    head = stem.split("_")[0].lower()
    if head in MODES:
        return head
    return ALIASES.get(head)


def main():
    keep_level = "--keep-level" in sys.argv
    no_trim = "--no-trim" in sys.argv

    # Source directory: samples/incoming by default, or any folder given as a
    # bare argument (e.g. AnimalSFX).
    src = IN_DIR
    for a in sys.argv[1:]:
        if not a.startswith("--"):
            src = a
            break

    if not os.path.isdir(src):
        os.makedirs(IN_DIR, exist_ok=True)
        print(f"No such directory: {src}")
        print(f"Put .wav files in {IN_DIR}/ (or pass a folder) and re-run.")
        return

    wavs = sorted(f for f in os.listdir(src) if f.lower().endswith(".wav"))
    if not wavs:
        print(f"No .wav files in {src}/")
        print(__doc__)
        return

    # --- Pass 1: read everything and measure it. ---
    loaded = {}          # mode -> list of (filename, samples, desc)
    unknown = []
    for fn in wavs:
        stem = os.path.splitext(fn)[0]
        mode = resolve_mode(stem)
        if mode is None:
            unknown.append(fn)
            continue
        data, desc = read_wav(os.path.join(src, fn))
        if not no_trim:
            data = trim(data)
        loaded.setdefault(mode, []).append((fn, data, desc))

    if not loaded:
        print("Nothing recognised. Names must start with a mode or animal:")
        print(f"  modes:   {', '.join(MODES)}")
        print(f"  aliases: {', '.join(sorted(ALIASES))}")
        return

    # A single loudness target across the whole library, so switching modes on
    # the card does not jump in level. The median of all sources keeps that
    # target near the material rather than pinned to the loudest outlier.
    all_rms = sorted(rms(d) for lst in loaded.values() for _, d, _ in lst)
    target = all_rms[len(all_rms) // 2] if all_rms else 0.1
    # Lift quiet libraries so the card is not needlessly timid, but never so far
    # that the limiter is doing all the work.
    #
    # 0.12, not 0.06. Normalisation happens BEFORE quantisation, so this figure
    # directly sets how many of the eight bits get used — and 0.06 was throwing
    # away about 1.5 of them before the file was even written. Measured across
    # the whole library: peaks went from a mean of 50/127 to 86/127, worth +0.8
    # effective bits, with the loudest still at 111 so the limiter is barely
    # engaging. That is audible as less quantisation grit on quiet tails, which
    # is where 8-bit shows up worst.
    target = max(target, 0.12)
    if not keep_level:
        print(f"Loudness target: RMS {target:.4f} "
              f"(median of {len(all_rms)} sources)\n")

    # --- Pass 2: level, fade, write. ---
    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for mode in MODES:
        if mode not in loaded:
            continue
        items = loaded[mode]
        if len(items) > NUM_VARIANTS:
            print(f"  {mode}: {len(items)} sources, using the first "
                  f"{NUM_VARIANTS}")
            items = items[:NUM_VARIANTS]
        for v, (fn, data, desc) in enumerate(items, start=1):
            before = rms(data)
            out = data if keep_level else loudness_match(data, target)
            out = fade_out(list(out))
            path, n = write_raw(f"{mode}_{v}", out)
            total += n
            ms = n / 48.0
            gain_db = 20 * math.log10(max(rms(out), 1e-9) / max(before, 1e-9))
            flag = "  <-- long" if ms > WARN_MS else ""
            print(f"  {fn:<18} -> {mode}_{v:<10} {ms:5.0f}ms "
                  f"{gain_db:+6.1f}dB{flag}")

    for fn in unknown:
        print(f"  {fn}: SKIPPED — unrecognised name")

    print(f"\nWrote {total} bytes ({total / 48000.0:.2f}s) into {OUT_DIR}/. "
          f"Flash holds about 41s.")
    print("Now rebuild:  cmake --build build")


if __name__ == "__main__":
    main()
