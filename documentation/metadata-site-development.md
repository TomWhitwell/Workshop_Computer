# Metadata site development

The metadata site is generated from card metadata, repository content, and the curation files under `tools/sitegen/src/curation/`. Generated output is written to `site/`.

## Install dependencies

From the repository root:

```sh
npm install --prefix tools/sitegen
```

## Build once

```sh
npm run build
```

This performs a clean production build of the complete site.

Share-preview images (Open Graph / Twitter Card PNGs at 1200×630) are
generated into `site/assets/og/default.png` and `site/programs/<slug>/og.png`
so Discord, Slack, iMessage, and similar crawlers can unfurl links on the
static GitHub Pages site.

Absolute links to copied web editors default to the repository's GitHub Pages
project URL. Set `SITE_BASE_URL` to override that public base, including its
trailing path when applicable:

```sh
SITE_BASE_URL=https://computer.musicthing.co.uk/ npm run build
```

The Pages workflow applies this custom-domain base only in the upstream
repository; fork previews retain their own `github.io/<repository>/` base.

## Legacy URL compatibility

Every existing card keeps its `programs/<slug>/` route. The generated site also
redirects these client-side legacy forms after confirming the slug exists in
`cards.json`:

- a catalogue hash such as `/#15-mlrws` to `/programs/15-mlrws/`
- a retained GitHub project prefix such as
  `/Workshop_Computer/programs/15-mlrws/` to `/programs/15-mlrws/` on the root
  custom domain

Queries, fragments, and nested web-editor paths are preserved. The compatibility
code deliberately does not strip the project prefix on fork previews, where it
is part of the valid deployment path.

## Run the development server

```sh
npm run dev
```

The command first performs a complete preview build, then serves the generated site at <http://localhost:5173/>.

Copied web-editor links use that local origin (`http://localhost:5173/programs/<slug>/web/…`) so the assets served by Vite open instead of the production Pages URL. External `https://…` Editor values are unchanged. Production and CI builds keep the public base described above.

While it is running, the development server watches:

- card metadata at `releases/*/info.yaml`
- discovery and tag curation under `tools/sitegen/src/curation/`
- other site-generator source files and assets

Successful rebuilds automatically refresh connected browsers.

## Preview a single card

When working on one card, build and watch just that card instead of the whole site:

```sh
npm run dev:card -- 41_blackbird
```

The card can also be given as `releases/41_blackbird`, its page slug (`41-blackbird`) or its number (`41`). Running `npm run dev:card` with no argument from inside a card's folder uses that card.

This builds only that card's detail page and the stylesheets and scripts it loads, then opens it at `http://localhost:5173/programs/<slug>/`. Saving any file in the card's folder (`info.yaml`, `README.md`, docs, panels, firmware, web editor) rebuilds the page in about a second and refreshes the browser. Each build prints the card's full `info.yaml` validation report, the same as `npm run validate-info -- releases/<card>`.

Files Git ignores and CMake build directories (`build/`, `build-*/`) inside the card folder are not watched, so compiling firmware does not trigger rebuilds. The home page, archive and author tools are not built in this mode; use `npm run dev` for those.

## Incremental rebuilds

After the initial complete build, common metadata and curation edits use faster delta builds:

- Changing one `info.yaml` rediscovers that release and updates its page and dependent indexes.
- Changing `discovery.yml` updates the program-card home page.
- Changing `flairs.yml` updates curation-dependent indexes and card pages.

Mixed changes, deleted metadata, and structural or generator changes safely fall back to a clean complete build.
## Pull request previews

Pull requests opened from a branch in `TomWhitwell/Workshop_Computer` itself get a live preview of the whole site at `https://computer.musicthing.co.uk/pr/<number>/`, linked from a comment on the PR. Pull requests from forks do not get previews.

Each push builds the preview in the pull request's own workflow run (`.github/workflows/pr-preview.yml`), with that branch's generator and without running dependency install scripts, then redeploys the site. The Pages workflow on `main` only downloads finished previews. It never runs pull request code, and preview builds never share `main`'s Actions caches. Closing a PR removes its preview on the next deploy.

A preview that fails, times out (10 minutes) or is over 100 MB is left out with an explanation on the PR. All previews together are capped at 600 MB so the site stays under GitHub Pages' 1 GB limit. Production always deploys, with or without previews. Preview pages carry `noindex` so search engines skip them, and their firmware download links point at the PR's commit.

**Do not push someone else's fork branch into this repository to get it a preview.** Previews are served from the production origin, so a preview's JavaScript can use any WebUSB and Web Serial devices visitors have already allowed for the site, without a prompt, and can read the site's `localStorage` (for example author-tool drafts). Only branches whose code has been read and trusted by someone with write access belong here.
