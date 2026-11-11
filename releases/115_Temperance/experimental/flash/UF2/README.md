# Firmware and attribution

## Current test build

Use **`temperance0.1.1.flash.uf2`**, embedded version `0.1.1-flash-exp.1`.
It detects supported **2MB or 16MB Winbond cards** at startup and sizes both
loops to the actual chip. This universal build has passed software tests;
the user confirmed functional hardware tests pass on both 2MB and 16MB cards
on 2026-11-11.

- `temperance0.1.0.16mb.uf2`: previous 16MB-only exp.2, user-reported hardware
  checks passed on 2026-11-07. It refuses a 2MB card.
- `temperance.0.1.0-flash-exp.1.16mb.uf2`: historical first experiment with a
  known 48kHz parameter-change freeze.

Historical corresponding sources are retained in Git: `85f0fa9c` for the
16MB-only exp.2 build and `f17dad15` for exp.1.

`SHA256SUMS.txt` covers all packaged files. The original RAM version remains
at `../../../UF2/temperance.0.1.0.uf2`.

## Attribution

This experimental firmware derives from **Temperance by Matt Allison**,
with Goldfish/MLRws flash recording and ADPCM code by **Dune Desormeaux**,
ComputerCard by **Chris Johnson**, and a plate reverb design by **Jon Dattorro**.
Experimental integration was generated with OpenAI Codex at the user's direction.

Keep [ATTRIBUTION.md](../ATTRIBUTION.md) and [LICENSES/](../LICENSES/) with
firmware copies, together with the overall [GPL-3.0 licence](../LICENSE).
The combined experiment uses GPL version 3 following the Goldfish/MLRws
author's recommendation, as reported by the maintainer. Matt's original MIT
notice and the other upstream notices remain preserved separately.
Corresponding source for this UF2 is in the parent experimental directory;
its unchanged ComputerCard dependency is in `../../../lib/`.

`picotool info -a temperance0.1.1.flash.uf2` displays embedded credits.
