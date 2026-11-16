#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project_root="$(cd ../.. && pwd)"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$project_root/upstream/pico-sdk}"
export PATH="${MTM_TOOLCHAIN_BIN:-$project_root/toolchain/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin}:$PATH"
cmake_bin="${MTM_CMAKE:-$project_root/toolchain/python/cmake/data/bin/cmake}"
ninja_bin="${MTM_NINJA:-$project_root/toolchain/python/bin/ninja}"
test -f "$PICO_SDK_PATH/pico_sdk_init.cmake" || { echo 'Set PICO_SDK_PATH or run ../../scripts/setup.sh'; exit 1; }
test -f "$PICO_SDK_PATH/lib/tinyusb/src/tusb.c" || { echo 'Initialise the Pico SDK lib/tinyusb submodule or run ../../scripts/setup.sh'; exit 1; }
python3 tools/prepare_header.py
mkdir -p reports dist
# Reuse the existing USB cache, explicitly enabling USB even in an older cache.
# Internal target names do not affect the single public firmware filename.
"$cmake_bin" -S . -B build-usb -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja_bin" -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release -DMTM_USB_EDITOR=ON
"$cmake_bin" --build build-usb -j4 2>&1 | tee reports/usb-build.txt
cp build-usb/mtm_sitar.bin dist/alien_sitar.bin
cp build-usb/mtm_sitar.elf dist/alien_sitar.elf
python3 tools/uf2.py dist/alien_sitar.bin dist/alien_sitar.uf2
arm-none-eabi-size dist/alien_sitar.elf | tee reports/usb-size.txt
arm-none-eabi-nm -C dist/alien_sitar.elf | sed -n '/__aeabi_f\|__aeabi_d\|__atomic/p' > reports/soft-float-symbols.txt
python3 tools/audit_firmware.py
python3 tools/audit_saved_flash.py dist/alien_sitar.elf
python3 tools/audit_usb.py
python3 tools/source_hashes.py
(cd dist && sha256sum alien_sitar.bin alien_sitar.elf alien_sitar.uf2 > SHA256SUMS)
# Retire old outputs only after the new firmware passes its audits.
rm -f dist/mtm_sitar.bin dist/mtm_sitar.elf dist/mtm_sitar.uf2 \
      dist/mtm_sitar_usb.bin dist/mtm_sitar_usb.elf dist/mtm_sitar_usb.uf2 \
      dist/USB-SHA256SUMS dist/size.txt
