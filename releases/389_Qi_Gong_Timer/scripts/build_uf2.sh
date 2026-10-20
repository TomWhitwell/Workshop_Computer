#!/usr/bin/env bash
# Build qi_timer.uf2 (144 MHz, the default) and qi_timer_192mhz.uf2 (option) in a Claude sandbox or any Linux x64 machine.
# THIS IS WHAT ACTUALLY WORKED on 7 Oct 2026 (archive.ubuntu.com was unreachable, so no apt):
#   CMake 3.31 from PyPI (CMake 4.x drops old-policy support some SDK parts may need),
#   xPack ARM GCC 14.2.1-1.1 from GitHub, Pico SDK 2.1.1 + tinyusb from GitHub (picotool is fetched and built by the SDK).
set -euo pipefail
CARD="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="${TOOLS:-$HOME/cardtools}"; mkdir -p "$TOOLS"; cd "$TOOLS"
GCCV=14.2.1-1.1
command -v cmake >/dev/null && cmake --version | grep -q " 3\." || pip install "cmake<4" --break-system-packages -q
if [ ! -x "xpack-arm-none-eabi-gcc-$GCCV/bin/arm-none-eabi-gcc" ]; then
  curl -sL -o gcc.tgz "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$GCCV/xpack-arm-none-eabi-gcc-$GCCV-linux-x64.tar.gz"
  tar xzf gcc.tgz && rm gcc.tgz
fi
if [ ! -d pico-sdk ]; then
  git clone -q --depth 1 -b 2.1.1 https://github.com/raspberrypi/pico-sdk.git
  (cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb)
fi
export PICO_SDK_PATH="$TOOLS/pico-sdk" PATH="$TOOLS/xpack-arm-none-eabi-gcc-$GCCV/bin:$PATH"
cd "$CARD"
build() {  # $1 = folder, $2 = extra cmake args
  rm -rf "$1" && mkdir "$1" && (cd "$1" && cmake .. -DPICO_BOARD=pico $2 >cmake.log 2>&1 && make -j8 >make.log 2>&1) \
    || { echo "BUILD FAILED in $1: first error:"; grep -m1 -E "error" "$1"/*.log; exit 1; }
  echo "== $1: $(grep -c 'warning:' "$1/make.log") warnings"; grep -A2 "Memory region" "$1/make.log" | tail -2
}
build build ""
build build192 "-DCARD_192MHZ=ON"
mkdir -p out && rm -f out/*.uf2 && cp build/qi_timer.uf2 out/qi_timer.uf2 && cp build192/qi_timer.uf2 out/qi_timer_192mhz.uf2
ls -la out/*.uf2
