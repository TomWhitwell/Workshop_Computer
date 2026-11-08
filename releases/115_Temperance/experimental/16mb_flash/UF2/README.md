# Firmware and attribution

## Current test build

Use **`temperance.0.1.0-flash-exp.2.16mb.uf2`**. Functional hardware tests
were reported as passing on 2026-11-07. It requires a 16MB Winbond card.

The exp.1 UF2 is retained for history; it has the known 48kHz parameter-change
freeze corrected in exp.2. `SHA256SUMS.txt` covers both files.

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

`picotool info -a temperance.0.1.0-flash-exp.2.16mb.uf2` displays embedded credits.
