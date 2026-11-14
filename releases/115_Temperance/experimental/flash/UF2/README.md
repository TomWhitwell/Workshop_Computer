# Firmware and attribution

## Available flash firmware

| Download | Recording modes | Status |
| --- | --- | --- |
| [temperance0.1.1.flash.uf2](../../flash_trigger/UF2/temperance0.1.1.flash.uf2) | Triggers only | Hardware tests passed on both card sizes |
| [temperance0.2.0.flash.uf2](temperance0.2.0.flash.uf2) | Switchable triggers and gates; triggers at boot | Software tests passed; hardware retest pending |

Both UF2s automatically detect supported 2MB and 16MB cards. In 0.2.0,
double-flick Up to change modes; the switch still taps to record in either.

## Current gate-mode test build

Use **`temperance0.2.0.flash.uf2`**, embedded version `0.2.0-flash-exp.1`.
It detects supported **2MB or 16MB Winbond cards**, adds Matt Allison's 0.2.0
trigger/gate recording controls and fixes pending edges across mode changes.
Release build, sanitizer suites and all 38 timing scenarios pass. This new
binary still needs physical retesting on both sizes; see [TEST_PROTOCOL.md](../TEST_PROTOCOL.md).

The [trigger-only 0.1.1 build](../../flash_trigger/UF2/temperance0.1.1.flash.uf2)
and its [complete source](../../flash_trigger/README.md) remain available, with
user-reported hardware tests passing on both sizes on 2026-11-11. That
hardware pass does not apply to the new gate-mode binary.

Superseded 16MB-only experiments remain in Git history (`85f0fa9c` for exp.2,
`f17dad15` for exp.1, which had a known parameter-change freeze).
`SHA256SUMS.txt` covers the packaged files. The RAM versions remain separate
in `../../../UF2/`, unchanged by this integration.

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

`picotool info -a temperance0.2.0.flash.uf2` displays embedded credits.
