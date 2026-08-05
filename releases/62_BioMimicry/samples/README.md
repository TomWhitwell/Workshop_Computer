# Samples

The card's voice: 8-bit signed mono 48 kHz one-shots, baked into the firmware at
build time by `tools/mksamples.py`.

## Provenance

Sourced from [**Pixabay**](https://pixabay.com) and redistributed here under the
[Pixabay Content License](https://pixabay.com/service/license-summary/), which
permits use and redistribution without attribution. They are committed to the
repository because without them a fresh clone builds a silent instrument.

**These recordings are not covered by the card's CC BY 4.0 licence** — they are
not the card author's to relicense. The [LICENSE](../LICENSE) at the repository
root covers the source code only.

Levels were matched across the whole library by `tools/importwav.py` — RMS rather
than peak, because a peak is usually a single transient and peak-normalising
leaves quiet-bodied recordings still sounding quiet. Corrections ranged from
−15.5 dB to +20.6 dB; the result sits within 1.07× across all 41 files.

## Naming

`<mode>_<variant>.raw`, where the variant is the round robin:

| Mode | Variants | What a variant is |
|---|---|---|
| `horses` | 1–4 | **The hooves** — 1 left hind, 2 left fore, 3 right hind, 4 right fore |
| `geese` | 1–8 | Birds of different size |
| `frogs` | 1–8 | Species in the chorus |
| `rain` | 1–8 | Drip sizes |
| `meteors` | 1–5 | Distances |
| `cicadas` | 1–8 | Insects |

**Horses is the one that matters.** The firmware asks for the hoof that actually
landed, and hind hooves strike lower and heavier than fores on a real animal, so
those four slots are not interchangeable — getting them in the right order is
most of what makes a gait sound like an animal rather than a drum machine.

## Replacing them

Three ways, easiest first:

1. **In a browser, no rebuild** — open `web/index.html`, drag WAVs onto the mode
   slots, upload over USB. See the README.
2. **Rebuild with your own** — drop WAVs into `samples/incoming/` and run
   `python tools/importwav.py`, then rebuild. Accepts any sample rate, mono or
   stereo, 8/16/24/32-bit or float.
3. **Placeholders** — `python tools/gensamples.py` writes procedural stand-ins in
   the right layout, so the build works with no recordings at all.
