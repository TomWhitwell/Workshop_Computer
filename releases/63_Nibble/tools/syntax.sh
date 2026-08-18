#!/bin/sh
# syntax.sh — compile every source file to /dev/null, purely to see the errors.
#
# There is no HOST C++ compiler on this machine, so the usual "just build it"
# loop is not available for quick checks. But the ARM cross-compiler from the
# Pico VS Code install IS here, and it will happily parse and type-check the
# card's own sources against the real SDK headers.
#
# This is NOT a substitute for a full `cmake --build`: it does not link, so it
# cannot catch a missing symbol or an over-budget image. What it does catch, in
# about a second, is every syntax error, type error, bad override and unused
# variable — which is the overwhelming majority of what goes wrong while
# writing this much code without a compiler in the loop.
#
# Usage:  sh tools/syntax.sh [file.cpp ...]     (default: all card sources)

set -e
cd "$(dirname "$0")/.."

TC="$USERPROFILE/.pico-sdk/toolchain/14_2_Rel1/bin"
SDK="$USERPROFILE/.pico-sdk/sdk/2.2.0/src"
CXX="$TC/arm-none-eabi-g++.exe"

# The SDK header set the card actually reaches, plus the generated config that
# CMake would normally produce. rp2040 / cortex-m0plus match PICO_BOARD=pico.
INC="
-I.
-I$SDK/common/pico_base_headers/include
-I$SDK/common/pico_stdlib_headers/include
-I$SDK/common/pico_time/include
-I$SDK/common/pico_sync/include
-I$SDK/common/pico_util/include
-I$SDK/common/boot_picoboot_headers/include
-I$SDK/common/boot_picobin_headers/include
-I$SDK/common/hardware_claim/include
-I$SDK/rp2_common/hardware_gpio/include
-I$SDK/rp2_common/hardware_pwm/include
-I$SDK/rp2_common/hardware_adc/include
-I$SDK/rp2_common/hardware_dma/include
-I$SDK/rp2_common/hardware_i2c/include
-I$SDK/rp2_common/hardware_spi/include
-I$SDK/rp2_common/hardware_irq/include
-I$SDK/rp2_common/hardware_clocks/include
-I$SDK/rp2_common/hardware_vreg/include
-I$SDK/rp2_common/hardware_sync/include
-I$SDK/rp2_common/hardware_timer/include
-I$SDK/rp2_common/hardware_base/include
-I$SDK/rp2_common/hardware_resets/include
-I$SDK/rp2_common/hardware_xosc/include
-I$SDK/rp2_common/hardware_flash/include
-I$SDK/rp2_common/hardware_uart/include
-I$SDK/rp2_common/hardware_sync_spin_lock/include
-I$SDK/rp2_common/boot_bootrom_headers/include
-I$SDK/rp2_common/pico_flash/include
-I$SDK/rp2_common/hardware_pll/include
-I$SDK/rp2_common/hardware_watchdog/include
-I$SDK/rp2_common/hardware_ticks/include
-I$SDK/rp2_common/hardware_boot_lock/include
-I$SDK/rp2_common/pico_platform_common/include
-I$SDK/rp2_common/pico_platform_compiler/include
-I$SDK/rp2_common/pico_platform_sections/include
-I$SDK/rp2_common/pico_platform_panic/include
-I$SDK/rp2_common/pico_bootrom/include
-I$SDK/rp2_common/pico_runtime/include
-I$SDK/rp2_common/pico_runtime_init/include
-I$SDK/rp2_common/pico_multicore/include
-I$SDK/rp2_common/pico_stdio/include
-I$SDK/rp2_common/pico_printf/include
-I$SDK/rp2_common/pico_stdlib/include
-I$SDK/rp2_common/pico_aon_timer/include
-I$SDK/rp2040/pico_platform/include
-I$SDK/rp2040/hardware_regs/include
-I$SDK/rp2040/hardware_structs/include
-I$SDK/rp2040/boot_stage2/include
-I$SDK/boards/include
-Itools/fakeinc
"

DEF="-DPICO_RP2040=1 -DPICO_ON_DEVICE=1 -DPICO_NO_HARDWARE=0 \
-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 -DLIB_PICO_MULTICORE=0"

FILES="$*"
if [ -z "$FILES" ]; then
  FILES=$(ls *.cpp 2>/dev/null)
fi

RC=0
for f in $FILES; do
  printf '%-16s' "$f"
  if "$CXX" -std=c++17 -mcpu=cortex-m0plus -mthumb \
       -Wall -Wextra -Wdouble-promotion -Wfloat-conversion \
       $DEF $INC -fsyntax-only "$f" 2>/tmp/syn.$$; then
    echo "ok"
  else
    echo "FAILED"
    cat /tmp/syn.$$
    RC=1
  fi
  rm -f /tmp/syn.$$
done
exit $RC
