#!/usr/bin/env python3
"""Generate embedded samples and build a custom Punk Confusion UF2."""

from pathlib import Path
import argparse
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = ROOT / "build-custom"
DEFAULT_OUTPUT = ROOT / "uf2" / "punk_confusion_custom.uf2"
SAMPLE_FILENAMES = (
    "marquee_oi.wav",
    "cbgb_hey_ho.wav",
    "club100_no_future.wav",
    "whisky_lets_go.wav",
)


def run(command, cwd=ROOT):
    print("+", " ".join(str(part) for part in command))
    subprocess.run(command, cwd=cwd, check=True)


def copy_samples(sample_dir):
    sample_dir = sample_dir.resolve()
    target_dir = ROOT / "samples"

    for filename in SAMPLE_FILENAMES:
        source = sample_dir / filename
        if not source.exists():
            raise FileNotFoundError(f"Missing {source}")
        shutil.copy2(source, target_dir / filename)


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Build a custom Punk Confusion UF2 from the four WAVs in samples/ "
            "or from a supplied directory."
        )
    )
    parser.add_argument(
        "--samples",
        type=Path,
        help="Directory containing the four card-ready WAV files.",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help=f"CMake build directory. Default: {DEFAULT_BUILD_DIR}",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Output UF2 path. Default: {DEFAULT_OUTPUT}",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete the build directory before configuring.",
    )
    args = parser.parse_args()

    if args.samples:
        copy_samples(args.samples)

    run([sys.executable, "tools/generate_vocal_samples.py"])

    build_dir = args.build_dir.resolve()
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    run(["cmake", "-S", str(ROOT), "-B", str(build_dir)])
    run(["cmake", "--build", str(build_dir), "-j2"])

    built_uf2 = build_dir / "punk_confusion.uf2"
    if not built_uf2.exists():
        raise FileNotFoundError(f"Expected build output not found: {built_uf2}")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built_uf2, output)
    print(f"Built {output}")


if __name__ == "__main__":
    main()
