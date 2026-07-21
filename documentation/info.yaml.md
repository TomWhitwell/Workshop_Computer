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
| `summary` | no | string | Short operator summary shown in the card header. Falls back to `Description` when absent. |
| `Language` | yes | string | Implementation language or stack (e.g. `C++ (Pico SDK)`, `Lua / Blackbird`). |
| `Creator` | yes | string | Author or maintainer name. |
| `Version` | yes | string | Semantic or project version string. |
| `Status` | yes | string | Release state (e.g. `Released`, `Beta`, `WIP`). Shown with version on the index. |
| `License` | no | string | Recommended. SPDX identifier or short license name (e.g. `MIT`, `GPL-3.0`, `GPLv3 or later`). Use the license stated in the card's `README.md` or `LICENSE` file. A missing license produces a warning when `draft: false`. |
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
| `discussion` | no | string (URL) | Card-specific feedback or support destination, normally a Discord thread. This replaces the site's general discussion link on that card's detail page. |
| `tags` | no | string[] | Labels for card type and function. Use lowercase kebab-case (e.g. `sequencer`, `midi-host`, `effect`, `synthesizer`, `polyphonic`, `utility`). Sitegen normalizes and deduplicates. A single comma-separated string is also accepted. |

```yaml
discussion: https://discord.com/channels/SERVER_ID/CHANNEL_OR_THREAD_ID
```

## Firmware downloads

By default the site auto-discovers download links from every committed (git-tracked) `.uf2` under the card folder, excluding a `web/` copy when an identically-named file exists elsewhere. Each link is labelled with the firmware filename.

Add an optional `uf2:` list to curate this. **When `uf2:` is present it fully replaces auto-discovery for that card**, so you can trim noise, control ordering, and annotate firmware. Each entry needs **either** a `path` (repo firmware) **or** a `download.url` (external link):

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `path` | either path or download | string | Path to the `.uf2` relative to the card folder (e.g. `UF2/goldfish.2.0.2mb.uf2`), matched case-insensitively. The build errors if the file is missing. |
| `name` | no | string | Friendly label shown on the download tile instead of the filename. |
| `download` | either path or download | object | `{ url, sha256 }` (both required when `download` is present). `url` is an external link (mirror, or a store/purchase page) — it opens in a new tab, shows its host, is tagged "External", and is never treated as flashable firmware. `sha256` is a mandatory hex digest of the firmware (`shasum -a 256 file.uf2` on macOS). For repo-hosted firmware (via `path`) the build computes the sha256 automatically. |

```yaml
uf2:
  - path: UF2/goldfish.2.0.2mb.uf2
    name: Goldfish 2.0 (2MB)
  - name: Buy a pre-flashed card
    download:
      url: https://example.com/store/goldfish
      sha256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
```

## Media

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `demo-link` | no | string (URL) | Optional YouTube URL (`watch`, `youtu.be`, `/shorts/`, `/embed/`). Rendered as a demo-video thumbnail on the detail page that plays inline when clicked. README YouTube links also get inline embeds. |
| `audio-sample` | no | string or list | Demo audio. Accepts a single value or a list. Each value may be: a **repo-relative file** (e.g. `samples/demo.wav`, resolved to a raw URL and rendered with an `<audio>` player); a **SoundCloud** track/set URL (embedded as a player, derived from the URL — no API key); a **Bandcamp** *EmbeddedPlayer* URL (the iframe `src` from Bandcamp's Share → Embed dialog); or any other URL (shown as a link). You may also paste a whole embed `<iframe>` snippet (we extract the player `src` + height), but you must single-quote it in YAML. List items may also be `{ url, title }` objects (`title` shows above the player). |

```yaml
# single file
audio-sample: samples/demo.wav

# or a list mixing sources. For SoundCloud/Bandcamp, paste the player src URL:
audio-sample:
  - samples/demo.wav
  - https://soundcloud.com/artist/my-demo
  - https://bandcamp.com/EmbeddedPlayer/track=123456789/size=large/

# with titles:
audio-sample:
  - url: https://soundcloud.com/artist/patch-1
    title: Patch 1 — generative drone
```

> Bandcamp: use the `EmbeddedPlayer` URL — it is the `src` of the iframe in Bandcamp's **Share → Embed** dialog. A plain album or track page URL cannot be embedded on its own and will render as a link.

## Structured metadata

These blocks document the primary inline documentation, I/O, controls, and host connectivity. Panel metadata is rendered on program detail pages. Unconditioned entries form the shared base; entries conditioned with `when.z` override that base for the selected switch position.

| Field | Type | Description |
|-------|------|-------------|
| `readme` | string (Markdown) | Full inline operator documentation. When present, it replaces the rendered `README.md` section on the card detail page. It is Markdown content, not a path. Supplementary PDF documentation remains visible. |
| `panel.inputs` | object[] | Panel jacks in. Each item has `id`, `name`, optional `description`, optional `type` (`audio` / `cv` / `pulse` / `other`), and optional `when: { z: up \| middle \| down }`. Entries with the same `id` override the shared entry in that position. |
| `panel.outputs` | object[] | Panel jacks out; same shape and position-override behavior as inputs. |
| `controls.knobs` | object[] | Knob metadata rows containing `main` / `x` / `y` entries with `name` and optional `description`. Omit `when` for controls shared by every position; use `when: { z: up \| middle \| down }` only when a role changes. |
| `controls.switch` | object | Switch metadata keyed by `up`, `middle`, `down`, and optional `tap`. The three physical positions may produce panel views. `tap` describes a brief Down-switch action and never produces a panel view. |
| `controls.leds` | object[] | LED meaning rows. Each has optional `when: { z: up \| middle \| down }`, `display` (e.g. `list`), and `items` with `id`, `name`, and optional `description`. Items override shared LEDs by `id`. |
| `host` | object | Host/USB connectivity (e.g. `usb` list with `name`, `role`, `description`) and optional `notes` (Markdown). |

```yaml
readme: |
  # Operating instructions

  Patch an audio signal to **Audio In 1**, then use Main to set the amount.
```

`readme` replaces the former `manual` field. Existing authored `manual` content should be migrated to `readme`; `summary` remains the short player-facing header text.

See [`releases/82_Computer_Grids/info.yaml`](../releases/82_Computer_Grids/info.yaml) for a full structured example.

### Switch and knobs relationship

Switch-position meaning and panel metadata are related but remain independently authored:

- **`controls.switch`** documents what each Z position does and may document a Down-switch `tap` action.
- Unconditioned knobs, sockets, and LEDs are the base inherited by Up, Middle, and Down.
- A `when: { z: ... }` row supplies only the properties that change in that position.
- Down already means the switch is being held down. There is no separate Hold position or gesture condition.
- Tap never changes knob, socket, or LED meanings, so `tap` is invalid in a `when` clause.

```yaml
controls:
  switch:                     # what the switch positions mean
    up:     { name: Double Length, description: Toggle write cell each clock for a stable double-length loop }
    middle: { name: Unlock,        description: Write from data input / noise when data exceeds Chaos }
    down:   { name: Write,         description: Hold down to force the write cell to a fixed value }
    tap:    { name: Reset,         description: Briefly press and release Down to reset the sequence }
  knobs:
    -                            # no condition: applies in every position
      main: { name: Chaos, description: Lock through Chaos probability }
      x:    { name: Offset }
      y:    { name: Chaos VCA }
    - when: { z: up }           # optional: a knob whose role changes in this position
      x: { name: Loop Length, description: Sets the loop length while held up }
```

A card that only needs to describe its switch uses `controls.switch` alone. A card whose panel changes by position adds conditioned knob, socket, or LED rows. Sitegen resolves at most three complete views as `base + up`, `base + middle`, and `base + down`; Middle is the default when present.

Legacy `when: { z: any }` rows are still read as shared base metadata but should be written without `when`. Legacy `gesture` conditions produce validation warnings while existing cards are reviewed.

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
