#!/usr/bin/env python3
"""Generate VocalSamples.h from the card-ready Punk Confusion WAV files."""

from pathlib import Path
import wave


ROOT = Path(__file__).resolve().parents[1]
SAMPLE_DIR = ROOT / "samples"
OUT_FILE = ROOT / "VocalSamples.h"

SAMPLES = (
    ("marquee_oi.wav", "kVocalMarqueeOi", "Marquee: Oi"),
    ("cbgb_hey_ho.wav", "kVocalCbgbHeyHo", "CBGB: Hey Ho"),
    ("club100_no_future.wav", "kVocalClub100NoFuture", "100 Club: No Future"),
    ("whisky_lets_go.wav", "kVocalWhiskyLetsGo", "Whisky a Go Go: Let's Go"),
)


def read_wav(path):
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        frame_rate = wav.getframerate()
        compression = wav.getcomptype()
        frames = wav.getnframes()
        data = wav.readframes(frames)

    if channels != 1 or sample_width != 2 or compression != "NONE":
        raise ValueError(f"{path} must be mono signed 16-bit PCM WAV")
    if frame_rate != 24000:
        raise ValueError(f"{path} must be 24 kHz; found {frame_rate} Hz")

    samples = [
        int.from_bytes(data[i : i + 2], byteorder="little", signed=True)
        for i in range(0, len(data), 2)
    ]
    return samples, frame_rate


def emit_array(name, label, samples, sample_rate):
    seconds = len(samples) / sample_rate
    lines = [f"// {label}, {seconds:.3f} seconds.", f"constexpr int16_t {name}[] = {{"]
    for offset in range(0, len(samples), 12):
        chunk = ", ".join(str(value) for value in samples[offset : offset + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    body = [
        "#ifndef PUNK_CONFUSION_VOCAL_SAMPLES_H",
        "#define PUNK_CONFUSION_VOCAL_SAMPLES_H",
        "",
        "#include <cstdint>",
        "",
        "// Generated from the WAVs in samples/.",
        "// Format: 24 kHz mono signed 16-bit PCM, peak-normalised to about -6 dBFS.",
        "// Regenerate with tools/generate_vocal_samples.py.",
        "",
    ]

    for filename, symbol, label in SAMPLES:
        samples, sample_rate = read_wav(SAMPLE_DIR / filename)
        body.append(emit_array(symbol, label, samples, sample_rate))

    body.append("#endif // PUNK_CONFUSION_VOCAL_SAMPLES_H")
    body.append("")
    OUT_FILE.write_text("\n".join(body))


if __name__ == "__main__":
    main()
