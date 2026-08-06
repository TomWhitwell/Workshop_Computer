# `info.yaml` schema

Each program card under `releases/NN_name/` includes an `info.yaml` beside `README.md` and its `.uf2` firmware. Sitegen and `update-readme.py` read these files when building the GitHub Pages site and `releases/README.md`.

Keys are case-insensitive. Hyphens and spaces in key names are equivalent (`audio-sample`, `Audio Sample`, and `audiosample` all map to the same field).

## Minimal example

```yaml
draft: true
Name: Card Display Name
Description: One-line summary of what the card does
Language: C++ (Pico SDK)
Creator: Your Name
Version: 1.0
Status: Released
License: MIT
```

## Core fields

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `draft` | no | boolean | When `true`, structured metadata in this file is still under author review. Set to `false` when `Name`, `contact`, `License`, `panel`, and related fields are confirmed. Not rendered on detail pages yet; parsed for tooling and future site UI. |
| `Name` | yes | string | Display title on the site index and detail page (see sitegen). |
| `Description` | yes | string | Short blurb shown on the index and detail aside. |
| `Language` | yes | string | Implementation language or stack (e.g. `C++ (Pico SDK)`, `Lua / Blackbird`). |
| `Creator` | yes | string | Author or maintainer name. |
| `Version` | yes | string | Semantic or project version string. |
| `Status` | yes | string | Release state (e.g. `Released`, `Beta`, `WIP`). Shown with version on the index. |
| `License` | no | string | SPDX identifier or short license name (e.g. `MIT`, `GPL-3.0`, `GPLv3 or later`). Use the license stated in the card's `README.md` or `LICENSE` file. Not rendered on detail pages yet; parsed for tooling and future site UI. |
| `date` | no | string | Last-update date (`YYYY-MM-DD`). If omitted, sitegen uses the last git commit date for the card folder. |

## Contact

Optional creator/maintainer contact details. Not rendered on detail pages yet; parsed for tooling and future site UI.

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `contact.email` | no | string | Contact email address. |
| `contact.website` | no | string (URL) | Personal or project website. |
| `contact.social` | no | object | Map of platform name → profile URL (keys normalized to lowercase, no spaces). |

```yaml
contact:
  email: you@example.com
  website: https://example.com
  social:
    instagram: https://instagram.com/username
    github: https://github.com/username
    mastodon: https://mastodon.social/@username
```

## Web editor

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `Editor` | no | string | Controls the **Web Editor** button and static deploy. See values below. |
| `web-entry` | no | string | Entry HTML file when not `index.html` (e.g. `app.html`). |

**`Editor` values**

| Value | Behavior |
|-------|----------|
| *(omitted)* | If `web/` exists in the card folder, sitegen copies it to GitHub Pages and links `programs/<slug>/web/index.html`. |
| `web` or `dist` | Copy that folder to Pages (relative to the card directory). |
| `https://…` | External editor URL; no local copy. |
| `none` | Hide the Web Editor button. |

## Discovery and linking

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `repository` | no | string (URL) | Upstream source repo when firmware or docs live outside this monorepo. Example: [64_voices_of_sid](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/64_voices_of_sid/info.yaml) points at Codeberg. |
| `tags` | no | string[] | Labels for card type and function. Use lowercase kebab-case (e.g. `sequencer`, `midi-host`, `effect`, `synthesizer`, `polyphonic`, `utility`). Sitegen normalizes and deduplicates. A single comma-separated string is also accepted. |

## Media

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `demo-link` | no | string (URL) | Optional YouTube URL (`watch`, `youtu.be`, `/shorts/`, `/embed/`). Detail-page embed UI is not wired yet; README YouTube links still get inline embeds when present. |
| `audio-sample` | no | string (path or URL) | Demo audio clip. **External:** full `http(s)://` URL. **Relative:** path under the card folder (e.g. `samples/demo.wav`); sitegen resolves it to a raw GitHub URL for the built site. Detail-page playback is not wired yet; the field is parsed for future use. |

## Structured metadata *(author now; detail-page UI later)*

These blocks document I/O, controls, and host connectivity. Author them in `info.yaml` for tooling and future sitegen sections; they are **not** rendered on program detail pages today.

| Field | Type | Description |
|-------|------|-------------|
| `manual` | string (Markdown) | Abbreviated operator summary (distinct from `README.md`). |
| `panel.inputs` | object[] | Panel jacks in. Each item: `id`, `name`, optional `description`, optional `type` (`audio` / `cv` / `pulse` / `other`). |
| `panel.outputs` | object[] | Panel jacks out; same shape as inputs. |
| `controls.knobs` | object[] | Knob metadata per context. Each row has `when` (`z`, `layer`, `gesture`) and `main` / `x` / `y` entries with `name` and optional `description`. Use `when.z` (`up` / `middle` / `down`, or `any` for every position) to give a knob **different metadata per switch position**. Describes knobs only — switch-position meaning lives in `controls.switch`. |
| `controls.switch` | object | Metadata for the three-position Z switch, independent of the knobs. Keys `up` / `middle` / `down`, each an object with `name` and optional `description`. This is the sole source of switch-position meaning. |
| `controls.leds` | object[] | LED meaning per context. Each row has `when`, `display` (e.g. `list`), and `items` with `id`, `name`, optional `description`. |
| `host` | object | Host/USB connectivity (e.g. `usb` list with `name`, `role`, `description`) and optional `notes` (Markdown). |

See [`releases/82_Computer_Grids/info.yaml`](../releases/82_Computer_Grids/info.yaml) for a full structured example.

### Switch and knobs relationship

Switch-position meaning and knob metadata are two independent things:

- **`controls.switch`** documents what each Z position *does*.
- **`controls.knobs`** documents the knobs, and may optionally give a knob different metadata per position via `when: { z: ... }`. Knob rows never describe the switch itself.

```yaml
controls:
  switch:                     # what the switch positions mean
    up:     { name: Double Length, description: Toggle write cell each clock for a stable double-length loop }
    middle: { name: Unlock,        description: Write from data input / noise when data exceeds Chaos }
    down:   { name: Write,         description: Force the write cell to a fixed value }
  knobs:
    - when: { z: any }          # knob metadata that applies in every position
      main: { name: Chaos, description: Lock through Chaos probability }
      x:    { name: Offset }
      y:    { name: Chaos VCA }
    - when: { z: up }           # optional: a knob whose role changes in this position
      x: { name: Loop Length, description: Sets the loop length while held up }
```

A card that only needs to describe its switch uses `controls.switch` alone; a card whose knobs change per position adds `when: { z: ... }` knob rows. The two blocks are read independently.

## Authoring guidance

When filling in structured metadata for a release folder:

**Source priority (do not invent behavior):**

1. `README.md` in the release folder (operator intent)
2. In-repo source (`.cpp`, `.c`, `.lua`, etc.) — ground truth for jack wiring when README is thin or wrong
3. External repo linked from `Repository` or README
4. Music Thing program card pages (for legacy cards with minimal local README)

**`id` vs `name`:**

- **`id`** — exact ComputerCard API identifier (`PulseIn1`, `CVOut1`, `AudioIn1`, …). See `Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h`.
- **`name`** — functional role on this card (e.g. `External Clock`, not `Pulse In 1`; `Beat Tick`, not `LED 0`).
- **`description`** — longer behavior note; may reference the API id in prose.

Only list jacks the firmware actually uses. Repurposed jacks (e.g. CV on `AudioOut1`) still use the real API id.

**Placeholder folders** (`77_Placeholder`, `88_Blank`, `02_comingsoon`): core fields only — do not fabricate `panel` / `controls`.

**Low certainty:** when a block is inferred from code but not stated in README, add a YAML comment above it:

```yaml
# certainty: low — knob roles inferred from main.cpp; not documented in README
controls:
  knobs: ...
```

**Preserve** existing `Description`, `Language`, `Creator`, `Version`, `Status`, `License`, `Editor`, and `Repository` values unless deployment rules require a change.

Every card must include **`Name`** — the site index and detail page title come from this field (not `Description`). Legacy `Title` is still read as a fallback if `Name` is absent.

## Automation

- **`pages.yml`** — runs `tools/sitegen`, deploys `site/` (including copied `web/` folders).
- **`update-readme.yml`** — regenerates `releases/README.md` from each card’s `info.yaml`.

**Future:** per-card `npm` builds in CI before copying `dist/` to Pages; commit built assets and set `Editor: dist` (or whatever your output folder is) until then.
