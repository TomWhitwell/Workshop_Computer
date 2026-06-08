# `info.yaml` schema

Each program card under `releases/NN_name/` includes an `info.yaml` beside `README.md` and its `.uf2` firmware. Sitegen and `update-readme.py` read these files when building the GitHub Pages site and `releases/README.md`.

Keys are case-insensitive. Hyphens and spaces in key names are equivalent (`audio-sample`, `Audio Sample`, and `audiosample` all map to the same field).

## Minimal example

```yaml
Description: One-line summary of what the card does
Language: C++ (Pico SDK)
Creator: Your Name
Version: 1.0
Status: Released
```

## Core fields

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `Description` | yes | string | Short blurb shown on the index and detail aside. |
| `Language` | yes | string | Implementation language or stack (e.g. `C++ (Pico SDK)`, `Lua / Blackbird`). |
| `Creator` | yes | string | Author or maintainer name. |
| `Version` | yes | string | Semantic or project version string. |
| `Status` | yes | string | Release state (e.g. `Released`, `Beta`, `WIP`). Shown with version on the index. |
| `date` | no | string | Last-update date (`YYYY-MM-DD`). If omitted, sitegen uses the last git commit date for the card folder. |

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
| `controls.knobs` | object[] | Knob roles per context. Each row has `when` (`z`, `layer`, `gesture`) and `main` / `x` / `y` entries with `name` and optional `description`. |
| `controls.leds` | object[] | LED meaning per context. Each row has `when`, `display` (e.g. `list`), and `items` with `id`, `name`, optional `description`. |
| `host` | object | Host/USB connectivity (e.g. `usb` list with `name`, `role`, `description`) and optional `notes` (Markdown). |

See [`releases/82_Computer_Grids/info.yaml`](../releases/82_Computer_Grids/info.yaml) for a full structured example.

## Automation

- **`pages.yml`** — runs `tools/sitegen`, deploys `site/` (including copied `web/` folders).
- **`update-readme.yml`** — regenerates `releases/README.md` from each card’s `info.yaml`.

**Future:** per-card `npm` builds in CI before copying `dist/` to Pages; commit built assets and set `Editor: dist` (or whatever your output folder is) until then.
