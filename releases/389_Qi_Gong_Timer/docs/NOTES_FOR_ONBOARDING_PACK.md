# Notes worth adding to the onboarding pack (from the QiGong Timer project)

1. **A real build works from a Claude sandbox** (7 Oct 2026): no apt (archive.ubuntu.com unreachable), but CMake 3.31 from PyPI
   (`pip install "cmake<4"`), xPack ARM GCC 14.2.1-1.1 from GitHub releases, Pico SDK 2.1.1 + tinyusb from GitHub. The SDK
   fetches and builds picotool itself. See `scripts/build_uf2.sh`. First card from the pack to reach a `.uf2` and run on hardware.
2. **Never make the card object a function-level `static`** in `main()`: thread-safe statics pull ~67 KB of C++ runtime
   (demangler, exception support) into RAM with `copy_to_ram`. A plain local object, big buffers as globals.
3. **A real `ComputerCard.h` next to your headers hides the host mock** (quoted includes search the including file's folder first).
   Keep the real one in `lib/`.
4. **Test-only names leak**: compile the card code without the test macro on the PC too (`make cardcheck`).
5. **Story OST reverb, possible latent overflow:** `MulTZ15(fdnLp_, revFb_)` overflows int32 once |fdnLp_| > ~65,000, but the
   clamp is +/-1,000,000. Probably never reached with Story OST's quiet inputs; worth a check / a split multiply.
6. **DC and feedback networks:** the Computer's audio inputs pass DC; a long reverb multiplies DC by up to 1/(1-g) (x200).
   High-pass the effect send. Our fuzz test found it (constant inputs held for seconds).
7. **Dead bands:** any one-pole with a small coefficient in a feedback loop needs extra fraction bits, and every "settle" step must
   be able to reach its target from both sides (`>>` rounds negative values away from zero).
8. **Cycle counting works:** Equanimity's `tools/bench/cycles.py` (Matt Allison, MIT), adapted as `scripts/bench_cycles.py`.
   Gotchas: the emulator has no boot ROM, so the SDK's float routines (used at start-up) crash it: load the start-up tables from
   the PC instead; keep bench symbols alive past `--gc-sections` (reference them on a never-taken path; pass arrays by address).
9. **ComputerCard v0.4.0** is now the official copy (14 June 2026), a superset of v0.3.0 for the usual API (adds input calibration).
10. **`info.yaml` schema** (documentation/info.yaml.md): required Name, short-description, summary, Language, Creator, Version, Status; License recommended.

11. **Network differs between conversations:** Robopera reached archive.ubuntu.com (apt worked); Qi Timer did not, the same week.
    Keep both routes (apt, or xPack GCC + pip CMake from GitHub/PyPI).
12. **Directive V1.9 still says ComputerCard v0.2.8** in its framework section and 144 MHz as the default clock "to reduce ADC tonal
    artifacts": cards that process Audio In should A/B any overclock by ear.
13. **ComputerCard v0.4.0 has no DAC zero-crossing workaround** (AudioOut maps 0 -> 0x800, 1 -> 0x7FF: the glitchy transition).
