# web/

`index.html` is the browser setup tool for NIBBLE-KO. It is a single
self-contained file — no build step, no dependencies, no server. Open it
locally, or serve the directory over `http://localhost`, and it talks to the
card over WebMIDI SysEx.

## Using it

1. On the card, hold the switch **Down** and press **B + D**. All four pads
   glow slowly: the card is on USB. **The loop keeps playing** — assignment
   is instant, so a new sound can be judged against the pattern it has to sit
   in. Only recording stops. (An upload does silence the card, because
   writing flash stops everything; see below.)
2. Open `index.html` in **Chrome or Edge** (Safari and Firefox have no
   WebMIDI) and click **Connect USB**.
3. The card appears as a MIDI port named `NIBBLE-KO (Workshop)` — the page
   finds it by that name, so if `usb_descriptors.c`'s product string changes,
   change `DEVICE_RE` in `index.html` to match.

The card only enumerates once it is in WebUI mode, so "card not found"
usually means step 1 has not happened yet. The page watches for the device
appearing rather than making you press Connect again.

## What is real, and what is not

**The Kit and Samples tabs talk to the card.** Assignment (`MSG_SET_SOURCE`,
instant, no reboot), saving the kit (`MSG_SAVE_MAP`), and the whole sample
library — upload, rename, delete, erase — are all wired up.

**Patterns save and load.** The card keeps its three pattern slots in RAM
only, so they are lost at power-off; the JSON file this page writes is the
permanent copy. Save reads a slot over `MSG_PAT_GET` and offers it as a
download, Load pushes a file back with `MSG_PAT_SET`. Neither touches flash,
so neither reboots the card — a loaded pattern is playable straight away
(hold D, tap A/B/C).

A pattern carries **no audio**: each event names a voice index, not a sample.
So a pattern saved against one kit plays against any other, and re-pointing a
voice changes what an existing pattern sounds like without touching it.

**Mutes and FX are reference displays**, as is everything on the Patterns tab
below the save/load panel. The firmware has no SysEx messages for mute-group
assignment or loop settings yet, so those tabs document the card's gestures
rather than configuring anything. The page says so rather than pretending
otherwise.

## The library model

A sample is **not** tied to a pad. Uploads go into a library of up to 32
numbered entries, and each of the twelve voices names one sound: its own
synth character, a built-in sample, or a library entry. So:

- one recording can be played by several voices, costing the space of one
- re-pointing a voice is one byte, not a second copy of the audio
- patterns are unaffected — they store voice indices, so changing what a
  voice plays changes what an existing pattern sounds like without touching
  the pattern

Entries are `USER1`, `USER2`… until you name them; uploads take their name
from the filename automatically.

**Deleting frees the slot, not the space.** Uploads append and nothing
compacts the region, so the Samples tab reports live audio and the append
watermark separately — "3 samples totalling 40KB, 900KB consumed" is a real
state, and only Erase All resets it.

## Every write reboots the card

Uploading, saving the kit, renaming, deleting and erasing all write flash,
and writing flash means masking USB — after which TinyUSB cannot be resumed.
So each of those ends with the card restarting, dropping off USB, and coming
back up **playing** rather than in WebUI mode. To carry on: press
switch+B+D again and click Connect.

This is not a limitation of the browser tool but of the hardware; the
reasoning is in `docs/LESSONS.md` and `webui.cpp`'s
`CommitHeaderAndReboot()`. Assignment changes are the exception — they take
effect immediately and only need saving when you want them to survive a
power cycle.

## Audio conversion

Uploads are converted in the browser to the same 8-bit mono 48kHz format
`tools/importwav.py` produces for baked samples: sum to mono, resample, trim
silence (keeping 1ms of pre-roll so attacks survive), loudness-match by RMS
rather than peak, 4ms fade-out, TPDF dither. A file uploaded here should
sound like the same file baked at build time.

The loudness target (0.12) is applied across everything staged in one send,
so slots do not jump in level relative to each other, and it is set *before*
the 8-bit conversion because it decides how many of the eight bits get used.

## Two different size limits

The card reports both, and confusing them sends people deleting samples they
did not need to:

- **per-upload cap** (~160KB) is the card's RAM staging buffer. The whole
  transfer is buffered in RAM before any flash is written, because writing
  flash takes USB down with it.
- **region size** (~1MB) is storage. Uploads **append**, so audio too big for
  one pass usually fits across two.

## Provenance and licence

This directory is licensed under **GPLv3** (see [`LICENSE`](LICENSE)), not
the CC BY 4.0 the rest of the repository uses. Two things were carried over
from elsewhere, and each explains a different half of that:

**The visual design** — dark theme, EB Garamond, the tab/panel language, and
a fair amount of the actual CSS (hex values, spacing figures, component
structure) — was written against
[Johan Eklund's Resonator](https://johaneklund.io/resonator/) web UI
([source](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/21_resonator/docs),
GPLv3). That is real values copied across, not an independent
reimplementation from looking at the page, so this counts as a derivative
work under copyright and takes Resonator's own licence rather than this
repo's default.

**The connection handling, ack queueing and upload sequence** are ported
from `../../WorkshopBio/web/index.html` (also Andy Jenkinson), whose
comments record several bugs worth not repeating — in particular the ack
queue, which exists because installing a reply handler per wait drops any
reply arriving between waits and puts every later wait one message behind.
