#!/usr/bin/env bash
# Self-contained build entry point. Run --help for prerequisites and examples.
set -euo pipefail
card_dir="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
  cat <<'HELP'
Build Alien Sitar for a Music Thing Modular Workshop Computer program card.

Prerequisites:
  Arm GNU Embedded GCC (arm-none-eabi-gcc/g++), CMake >=3.20, Ninja, Python 3.
  Tested with xPack ARM GCC 14.2.1-1.1 and Pico SDK 2.1.1.

Obtain the SDK once (no other card repository is needed):
  git clone --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git /your/path/pico-sdk

Build from this card folder:
  PICO_SDK_PATH=/your/path/pico-sdk ./scripts/build.sh

The firmware is written to alien_sitar.uf2 in this folder, replacing that file.
Intermediate ELF/BIN and build files stay under the ignored build/ directory.
To regenerate DSP coefficient tables after editing their formulas:
  python3 tools/generate_tables.py

Optional overrides:
  ARM_TOOLCHAIN_BIN    Directory containing arm-none-eabi-gcc (otherwise PATH).
  ALIEN_SITAR_CMAKE   CMake executable (default: cmake).
  ALIEN_SITAR_NINJA   Ninja executable (default: ninja).
  CMAKE_BUILD_PARALLEL_LEVEL  Concurrent build jobs (default: 4).
HELP
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi
if [[ $# -ne 0 ]]; then
  usage >&2
  exit 2
fi
if [[ -n "${ARM_TOOLCHAIN_BIN:-}" ]]; then
  export PATH="$ARM_TOOLCHAIN_BIN:$PATH"
fi
cmake_bin="${ALIEN_SITAR_CMAKE:-cmake}"
ninja_bin="${ALIEN_SITAR_NINJA:-ninja}"
for command_name in "$cmake_bin" "$ninja_bin" arm-none-eabi-gcc arm-none-eabi-g++ python3; do
  command -v "$command_name" >/dev/null || { echo "Missing tool: $command_name (see --help)" >&2; exit 1; }
done
if [[ ! -f "${PICO_SDK_PATH:-}/pico_sdk_init.cmake" ]]; then
  echo 'Set PICO_SDK_PATH to Pico SDK 2.1.1 (see --help).' >&2
  exit 1
fi
export PICO_SDK_PATH
# Resolve the generator executable so CMake also works with a tool override.
"$cmake_bin" -S "$card_dir" -B "$card_dir/build" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$(command -v "$ninja_bin")" \
  -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
"$cmake_bin" --build "$card_dir/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
python3 "$card_dir/tools/uf2.py" "$card_dir/build/alien_sitar.bin" "$card_dir/alien_sitar.uf2"
