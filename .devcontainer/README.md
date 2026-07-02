# Dev container build harness

This folder plus the top-level `Makefile` and the helpers under `scripts/`
(including the `start_openocd_host.*` wrappers) provide a reproducible way to
build (and optionally flash/debug) the Pico SDK firmware in this repo from
inside a VS Code Dev Container.

Nothing here changes how cards are structured: a card under `releases/<card>/`
only needs its `CMakeLists.txt` and `pico_sdk_import.cmake`. The harness supplies
the toolchain and `make` behavior.

## Quick start

1. Install Docker and the VS Code **Dev Containers** extension.
2. Open this repo in VS Code and **Reopen in Container**.
3. Build a card:

   ```sh
   cd releases/10_twists/src
   make            # configure + build into ./build/
   make uf2        # stage produced *.uf2 into ./UF2/
   ```

The container ships CMake, Ninja, the ARM GCC toolchain, a pinned Pico SDK at
`/opt/pico-sdk`, and picotool. The Pico SDK / TinyUSB versions are selected at
build time in [devcontainer.json](devcontainer.json) under `build.args`.

## How `make` works without a per-card Makefile

The container sets `MAKEFILES` to auto-include
[`scripts/pico_auto_make.mk`](../scripts/pico_auto_make.mk). In any directory
that looks like a Pico SDK CMake project but has no local `Makefile`, this
provides:

- `make` / `make build` — configure + build into `./build/`
- `make uf2` — stage produced `*.uf2` into `./UF2/`
- `make clean` — remove the build directory
- `make flash` — flash via GDB against a host-side OpenOCD (see below)

From the repo root you can also build any project directory with
`make <dir>` (delegates to [`scripts/build.sh`](../scripts/build.sh)), or build
every compatible `releases/*` card with `make all`.

## Pico SDK resolution order

1. `PICO_SDK_PATH` if already set
2. `/opt/pico-sdk` (the container default)
3. Otherwise Pico SDK FetchContent (`PICO_SDK_FETCH_FROM_GIT*` variables)

## Flashing / debugging (host-side OpenOCD)

Flashing runs OpenOCD **on the host** and connects from inside the container to
its GDB server at `host.docker.internal:3333`. This keeps USB probe access and
permissions simple across platforms.

Start OpenOCD on the host:

- macOS/Linux: `./scripts/start_openocd_host.sh`
- Windows: `powershell -ExecutionPolicy Bypass -File .\scripts\start_openocd_host.ps1`

Then, inside the container:

```sh
cd releases/10_twists/src
make flash
```

`make flash` selects the target from the newest `UF2/*.uf2` (or `FLASH_UF2=...`)
and flashes the matching `build/<stem>.elf`. Useful variables:
`OPENOCD_HOST`, `OPENOCD_GDB_PORT`, `FLASH_UF2`, `FLASH_ELF`, `GDB`.

Debug probe reference: https://learn.adafruit.com/raspberry-pi-pico-debug-probe

## VS Code tasks and F5 debugging

The committed `.vscode/` configs let you build, flash, and debug without the
terminal. They ask for the **card directory** via a prompt (default
`releases/04_BYO_Benjolin`) instead of guessing from the open file, so they work
no matter what you have focused:

- **Tasks** (`Run Task`): `ComputerCard: build`, `ComputerCard: flash`.
- **Debug (F5)**: `ComputerCard: attach (OpenOCD)` builds the card, attaches
  Cortex-Debug to the host OpenOCD GDB server, and breaks at `main`. Start
  OpenOCD on the host first (see above).

The debug config loads `<cardDir>/build/.last.elf` — a symlink the build step
updates to the most recently built ELF — so it is card-name agnostic.

Only `tasks.json`, `launch.json`, and `extensions.json` are tracked; personal
VS Code settings under `.vscode/` remain ignored.

## Notes

- Submodules (e.g. `releases/41_blackbird/lua`) are not initialized
  automatically; run `git submodule update --init --recursive <path>` only for
  the card you are building.
