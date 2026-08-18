#!/usr/bin/env python3
"""importwav.py — turn recordings into the .raw one-shots the card bakes in.

Drop WAV files into samples/incoming/ named for their VOICE SLOT, then:

    python tools/importwav.py

It reads samples/incoming/*.wav, converts each to 8-bit signed mono 48kHz,
trims silence, normalises, applies a short fade-out, and writes samples/*.raw
ready for the build. Uses only the Python standard library — no ffmpeg needed.

Ported from WorkshopBio/tools/importwav.py (see that file for the reasoning
behind loudness-match-then-limit, TPDF dither, etc. — carried over unchanged).
The difference: NIBBLE-KO has no modes or round-robin variants, just 12 flat
voice slots matching drums.cpp's kGestureVoice table. One sample per slot.

NAMING
    Either the slot number directly:
        voice00.wav .. voice11.wav
    Or the drum's name, per drums.cpp's kVoices comments:
        kick.wav  kickdeep.wav  snare.wav  snaresnappy.wav  hatclosed.wav
        hatmetal.wav  hatopen.wav  crash.wav  cowbell.wav  tom1.wav
        tom2.wav  tom3.wav

TODO(design session): one sample per slot only. Round-robin variants (several
samples per slot, cycled) are undecided — see drums.h's VoiceSource TODO.

WHAT MAKES A GOOD SOURCE
  * A single isolated hit. No reverb tail you cannot trim, no bleed from other
    drums.
  * Trimmed tight to the transient; the attack is what identifies the sound.
  * Dry. The card has no reverb, so recorded ambience is baked in forever.
  * Mono or stereo (stereo is summed), any sample rate, 8/16/24/32-bit or float.

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
NUM_VOICES = 12
WARN_MS = 2000

# Slot index <-> name, matching drums.cpp's kVoices comments. Either the
# numeric voiceNN form or these names is accepted on the input side; output
# files are always written as voiceNN.raw so mksamples.py stays simple.
VOICE_NAMES = [
    "kick", "kickdeep", "snare", "snaresnappy", "hatclosed", "hatmetal",
    "hatopen", "crash", "cowbell", "tom1", "tom2", "tom3",
]
ALIASES = {
    "kick1": "kick", "kick2": "kickdeep", "deepkick": "kickdeep",
    "snare1": "snare", "snare2": "snaresnappy", "rimshot": "snaresnappy",
    "hat": "hatclosed", "closedhat": "hatclosed", "hh": "hatclosed",
    "hihat": "hatmetal", "hihatmetal": "hatmetal", "metalhat": "hatmetal",
    "openhat": "hatopen", "hatopen1": "hatopen",
    "ride": "crash", "cymbal": "crash",
    "bell": "cowbell", "cow": "cowbell",
    "tom": "tom1", "tomlow": "tom1", "tommid": "tom2", "tomhigh": "tom3",
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

    Peak normalisation is the obvious thing and it does not work here: a
    sample's peak is usually a single transient, so two recordings normalised
    to the same peak can still differ hugely in how loud they sound.

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
            a = knee + (ceiling - knee) * (over / (1.0 + over))
            v = a if v >= 0 else -a
        limited.append(v)
    return limited


def fade_out(data, ms=4):
    n = min(int(SR * ms / 1000), len(data))
    for i in range(n):
        data[len(data) - n + i] *= 1.0 - (i / n)
    return data


def write_raw(name, data):
    """Quantise to 8-bit signed, with TPDF dither.

    Without dither, quantisation error CORRELATES with the signal, which is
    why 8-bit reads as grit riding on a decaying tail rather than as hiss. A
    triangular dither (the sum of two uniform randoms, +/-1 LSB peak)
    decorrelates it: technically more noise, audibly much less objectionable.

    Seeded per file so a rebuild is reproducible.
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


def resolve_slot(stem):
    """Map a source filename onto one of the 12 voice slots.

    Accepts voiceNN directly, a name from VOICE_NAMES, or an alias. Returns
    the slot index 0..11, or None if unrecognised.
    """
    head = stem.split("_")[0].lower()
    if head.startswith("voice") and head[5:].isdigit():
        n = int(head[5:])
        if 0 <= n < NUM_VOICES:
            return n
    name = ALIASES.get(head, head)
    if name in VOICE_NAMES:
        return VOICE_NAMES.index(name)
    return None


def main():
    keep_level = "--keep-level" in sys.argv
    no_trim = "--no-trim" in sys.argv

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
    loaded = {}          # slot index -> (filename, samples, desc)
    unknown = []
    for fn in wavs:
        stem = os.path.splitext(fn)[0]
        slot = resolve_slot(stem)
        if slot is None:
            unknown.append(fn)
            continue
        if slot in loaded:
            print(f"  {fn}: SKIPPED — slot {slot} ({VOICE_NAMES[slot]}) "
                  f"already filled by {loaded[slot][0]}")
            continue
        data, desc = read_wav(os.path.join(src, fn))
        if not no_trim:
            data = trim(data)
        loaded[slot] = (fn, data, desc)

    if not loaded:
        print("Nothing recognised. Names must be voiceNN or one of:")
        print(f"  {', '.join(VOICE_NAMES)}")
        return

    # A single loudness target across the whole kit, so switching voices does
    # not jump in level.
    all_rms = sorted(rms(d) for _, d, _ in loaded.values())
    target = all_rms[len(all_rms) // 2] if all_rms else 0.1
    target = max(target, 0.12)
    if not keep_level:
        print(f"Loudness target: RMS {target:.4f} "
              f"(median of {len(all_rms)} sources)\n")

    # --- Pass 2: level, fade, write. ---
    os.makedirs(OUT_DIR, exist_ok=True)
    total = 0
    for slot in sorted(loaded):
        fn, data, desc = loaded[slot]
        before = rms(data)
        out = data if keep_level else loudness_match(data, target)
        out = fade_out(list(out))
        name = f"voice{slot:02d}"
        path, n = write_raw(name, out)
        total += n
        ms = n / 48.0
        gain_db = 20 * math.log10(max(rms(out), 1e-9) / max(before, 1e-9))
        flag = "  <-- long" if ms > WARN_MS else ""
        print(f"  {fn:<18} -> {name} ({VOICE_NAMES[slot]:<12}) {ms:5.0f}ms "
              f"{gain_db:+6.1f}dB{flag}")

    for fn in unknown:
        print(f"  {fn}: SKIPPED — unrecognised name")

    print(f"\nWrote {total} bytes ({total / 48000.0:.2f}s) into {OUT_DIR}/.")
    print("Now rebuild:  cmake --build build")


if __name__ == "__main__":
    main()
