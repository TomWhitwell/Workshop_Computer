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

The command first performs a complete build, then serves the generated site at <http://localhost:5173/>.

While it is running, the development server watches:

- card metadata at `releases/*/info.yaml`
- discovery and tag curation under `tools/sitegen/src/curation/`
- other site-generator source files and assets

Successful rebuilds automatically refresh connected browsers.

## Incremental rebuilds

After the initial complete build, common metadata and curation edits use faster delta builds:

- Changing one `info.yaml` rediscovers that release and updates its page and dependent indexes.
- Changing `discovery.yml` updates the program-card home page.
- Changing `flairs.yml` updates curation-dependent indexes and card pages.

Mixed changes, deleted metadata, and structural or generator changes safely fall back to a clean complete build.