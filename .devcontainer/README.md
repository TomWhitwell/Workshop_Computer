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
   cd releases/11_goldfish
   make            # configure + build into ./build/, and stage *.uf2 into ./UF2/
   ```

The container ships CMake, Ninja, the ARM GCC toolchain, a pinned Pico SDK at
`/opt/pico-sdk`, and picotool. The Pico SDK / TinyUSB versions are selected at
build time in [devcontainer.json](devcontainer.json) under `build.args`.

## How `make` works without a per-card Makefile

The container sets `MAKEFILES` to auto-include
[`scripts/pico_auto_make.mk`](../scripts/pico_auto_make.mk). In any directory
that looks like a Pico SDK CMake project but has no local `Makefile`, this
provides:

- `make` / `make build` — configure + build into `./build/`, and stage any
  produced `*.uf2` into `./UF2/`
- `make uf2` — same as `make build`, then print the staged `*.uf2` path(s)
  (staging already happens during the build; this is just for convenience)
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

Flashing runs OpenOCD **on the host (outside the container!)** and connects from inside the container to
its GDB server at `host.docker.internal:3333`. This keeps USB probe access and
permissions simple across platforms.

Start OpenOCD on the host:

- macOS/Linux: `./scripts/start_openocd_host.sh`
- Windows: `powershell -ExecutionPolicy Bypass -File .\scripts\start_openocd_host.ps1`

Then, inside the container:

```sh
cd releases/11_goldfish
make flash
```

`make flash` selects the target from the newest `UF2/*.uf2` (or `FLASH_UF2=...`)
and flashes the matching `build/<stem>.elf`. Useful variables:
`OPENOCD_HOST`, `OPENOCD_GDB_PORT`, `FLASH_UF2`, `FLASH_ELF`, `GDB`.

Debug probe reference: https://learn.adafruit.com/raspberry-pi-pico-debug-probe

## VS Code tasks and F5 debugging

The committed `.vscode/` configs let you build, flash, and debug without the
terminal. Instead of guessing the card from the open file, they ask **once** for
the card folder and remember it.

### Choosing the card

The first time you build/flash/debug, a native **folder picker** opens (starting
in `releases/`). Pick a card folder and the choice is remembered under the key
`cardDir` and reused for every later build/flash/debug — no more prompts.

- The choice is persisted to `.vscode/.card.json` (git-ignored) so it survives
  window reloads and container restarts.
- To switch cards, run the **`ComputerCard: select card`** task; it reopens the
  folder picker and overwrites the stored choice (takes effect immediately, no
  reload needed).

This is powered by the `rioj7.command-variable` extension (installed
automatically in the container). Both `tasks.json` and `launch.json` share the
same `cardDir` key, so pressing play prompts at most once — the `preLaunchTask`
reuses the remembered value.

### Tasks (`Run Task`)

- `ComputerCard: build` — `make build`, then refresh the `build/.last.elf`
  symlink (see below).
- `ComputerCard: flash` — `make flash` (programs the chip via host OpenOCD).
- `ComputerCard: build + flash` — build then flash; used as the debug
  pre-launch step.
- `ComputerCard: select card` — change the remembered card folder.

### Debug (F5)

`ComputerCard: attach (OpenOCD)`:

1. Runs `ComputerCard: build + flash`, so the **selected card is programmed onto
   the chip** first.
2. Attaches Cortex-Debug to the host OpenOCD GDB server and breaks at `main`.

Start OpenOCD on the host first (see the section above). The config loads
`<cardDir>/build/.last.elf` — a symlink that the build step
([`scripts/last_elf.sh`](../scripts/last_elf.sh)) points at the most recently
built ELF. That script runs after every build so the symlink exists even for
cards that ship their own `Makefile`/`GNUmakefile` (which the Pico auto-make
harness does not create), keeping the debug config card-name agnostic.

### Live Watch

The debug config enables Cortex-Debug **Live Watch** (`liveWatch.enabled`,
sampling a few times per second). To use it:

- During a session, open the **LIVE WATCH** tree in the Run and Debug sidebar
  and click **+** to add expressions (it starts empty).
- Values update only while the target is **running** — press **Continue** after
  the initial break at `main`.
- Only globals/statics can be live-watched (Live Watch reads memory by fixed
  address while the CPU runs; stack locals have no fixed address).

### Required extensions

Installed automatically in the container (and recommended in
`.vscode/extensions.json`): `marus25.cortex-debug`,
`mcu-debug.debug-tracker-vscode` (a Cortex-Debug dependency),
`ms-vscode.cpptools`, and `rioj7.command-variable`.

`tasks.json`, `launch.json`, `extensions.json`, and `settings.json` are tracked;
the remembered-card file `.vscode/.card.json` and other personal `.vscode/`
files remain ignored.
