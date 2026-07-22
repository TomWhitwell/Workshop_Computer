# Program Card Site Migration

## Decision

Replace the current local card metadata site implementation in `tools/sitegen` with a Jekyll-free port of the MTM program-card site.

Do **not** preserve the current `tools/sitegen` UI as a parallel product unless a regression forces it. The cleaner architecture is:

- keep the existing build contract
- keep `releases/*/info.yaml` as the source of truth
- keep the output folder as `site/`
- replace the current importer/renderer internals with the richer MTM data model and page templates

This keeps GitHub Actions stable while removing the dependency on Jekyll and the external MTM site repo.

## Why This Is Cleaner

The current Pages workflow only cares that `npm run build --prefix tools/sitegen` produces `site/`; see `.github/workflows/pages.yml`.

That means we can swap the implementation in place without changing deployment wiring.

The MTM site appears to split into two concerns:

1. data normalization from `Workshop_Computer/releases/*/info.yaml`
2. static HTML rendering of richer card pages

Neither concern requires Jekyll in principle. Jekyll is acting as a static site renderer around an imported card dataset, not as a required runtime platform.

## Target Architecture

Keep `tools/sitegen` as the public build surface, but reorganize it around a canonical card model.

### Stable external contract

- Command: `npm run build --prefix tools/sitegen`
- Input: `releases/*/info.yaml`, `README.md`, docs, downloads, web assets
- Output: `site/`
- Deploy path: unchanged GitHub Pages workflow

### New internal shape

`tools/sitegen/src/`

- `build.js`
  - orchestration only
  - loads all releases
  - writes card pages, index pages, assets, and optional debug JSON
- `model/`
  - card normalization logic ported from the MTM Ruby importer
  - turns mixed YAML/README/release-folder state into one canonical JS object per card
- `render/`
  - pure HTML renderers for index, detail page, panel, tags, sockets, docs sections
- `curation/`
  - optional local data files for presentation-only overrides that do not belong in firmware metadata
- `assets/`
  - CSS and browser JS ported from the MTM site where useful

### Canonical card object

The build should normalize each release into a single object, similar to the MTM site's generated `cards.yml` entries:

- `id`
- `slug`
- `title`
- `draft`
- `release`
- `summary`
- `short_description`
- `source_file`
- `source_url`
- `readme_url`
- `download_url`
- `tags`
- `panel`
- `panel_views` (optional resolved Up/Middle/Down snapshots; unconditioned metadata is inherited by each view)
- `switch_modes`
- `leds`
- `documentation`
- `notes`
- `quick_start`
- `videos`
- `memory`
- `metadata`

`metadata` should hold compact card facts such as:

- `creator`
- `language`
- `version`
- `status`
- `license`
- `created`
- `updated`
- `editor_url`
- `editor_note`
- `discussion_url`
- `contact`

## Migration Strategy

### Status

- **Phase 1: complete.** The MTM importer is fully ported into `tools/sitegen/src/model/card.js` (case-insensitive field lookup, API-jack to panel-slot mapping, `compactPanelLabel`, controls/switch/LED synthesis, host to notes, source provenance, cleaned metadata). `discoverRelease()` returns the canonical model; `utils/git.js` gained `getCommitDates()` for created/updated fallback. Build passes for all releases and emits `site/cards.json`, `site/raw-info/<id>/info.yaml`, and `site/raw-info/index.json`.
- **Phase 2: complete.** The MTM program-card detail page is ported into `tools/sitegen/src/render/cardPage.js` (draft banner, hero + tags + actions, demo video, quick start, panel SVG diagram with positioned labels, controls/switch/socket lists, documentation, about/notes/data-sources). Panel coordinates live in `src/render/panelPositions.js`; the panel SVG and ported detail-page CSS (`assets/program-cards.css`) are copied into `site/`. The embedded README + PDF docs are retained as a documentation extra. WebUSB flashing is wired to the hero download via `data-uf2-url`. Also fixed: headline download now skips `old` / `old versions` firmware, and `github-markdown.css` is copied to the output.
- **Phase 3: complete (hybrid).** MTM discovery ported into `tools/sitegen/src/render/discovery.js` (tiles, flair tags, shelves with `cards`/`cards_from_tags`/layout variants, hero, archive). Curation seeded from MTM verbatim in `src/curation/{tags.yml,discovery.yml}` and loaded via `src/curation/index.js` (flair resolution, case-insensitive). The index shows curated shelves by default; the existing search + type/creator/language filters (plus a new tag filter) toggle to a flat results grid. A `/archive/` one-line index page is generated. Tiles link to detail pages (per-tile WebUSB dropped; flashing lives on detail pages, the MTM model). The index/archive search control now has an `✕` reset affordance that appears whenever any filter is active and clears search + all filters back to the default curated shelf.
- **Schema-source layer: partial.** `src/schema/schemaDefinition.js` holds the canonical field list (path, type, required, aliases) sourced from `documentation/info.yaml.md`; `src/schema/schemaAdapter.js` exposes it (`listFields`, `requiredFields`, `getField`, case/hyphen-insensitive `getFieldByKey`, `knownKeys`) and feeds `cards.json`'s `schema` block. Still to do: `schemaSource.js`/`schemaDocs.js`/`schemaDefaults.js` split and eventual JSON-Schema canonical source.
- **Switch-position panel views: implemented.** Unconditioned controls, sockets, and LEDs form a shared base. `when.z` Up/Middle/Down rows override matching physical items, and sitegen emits resolved `panel_views` while retaining the default view in legacy `panel`, `switch_modes`, and `leds`. `controls.switch.tap` is rendered as a Down-switch action and never creates a panel view.
- **Shared validator: first cut complete.** Reusable `src/validate/` module (see the Shared Validation Component section). Standalone from rendering and the normalized model; consumes the schema adapter. `npm run validate-info` runs it over all releases, and the site build runs it as a **non-fatal** pass (prints an error/warning summary; never blocks the build). Current baseline: **40 errors across 11 cards, 203 warnings across 64**; every hard error is confined to the 11 cards whose `info.yaml` was overwritten with generated build output (see finding below).
- **Phase 4 (collapse to one build path): substantially done.** All consumers now read the canonical card model: the site index derives its type/creator/language facets from `card.metadata`, and the detail page title from `card.title`, instead of the parallel thin `info` object. The dead parallel surface was removed from the release loader (`discover/release.js` no longer builds or returns `info`/`display`; unused `parseDisplayFromFolder`/`formatDisplayTitle` imports dropped). Verified behavior-preserving by diffing the full generated site before/after (only real change: `18_chord_organ`'s browser-tab `<title>` now matches its `Name`/H1 — a pre-existing inconsistency fixed). `discover/*` remains as the single ingestion/loader layer (parse README/docs/downloads → `model/card.js`); it is not a second renderer, so no further renderer removal is needed. Remaining optional tidy: a small internal `normalizeInfo` helper still feeds the web-editor fallback.
- **Author preview/editor: first cut complete.** Static, client-side tool at `site/preview/` (built by `buildPreviewTool()` in `build.js`). An author picks a release, edits its raw `info.yaml`, and gets live schema diagnostics + a live-rendered card preview. It reuses the *exact* shared modules the build uses (validator + `model/card.js` + `render/cardPage.js`), copied verbatim to `site/preview/lib/` and loaded as browser ES modules with an import map for the `yaml`/`marked` vendor builds (`site/preview/vendor/`). No bundler added. To keep the validator browser-safe, its fs loader was split into `validate/readSource.js`, leaving `validate/parseSource.js` free of Node imports. There is **no Normalize button** — normalization happens implicitly only to render the preview; the editable form is always the raw source. Client logic: `assets/preview/preview-client.js`; page: `render/previewPage.js`. Verified live: conforming cards render clean; non-conforming cards (e.g. `26_clockwork`) show the same error/warning set as the CLI; live edits re-validate and re-render, including YAML syntax errors.

#### Finding: split-brain `info.yaml` in 11 cards

The validator surfaced that 11 release folders have had their author `info.yaml` overwritten with the **normalized/generated card model** (they carry generator-only keys `source_file`/`source_url`/`readme_url`/`download_url`/`slug`/`url`, and store `Language`/`Version`/`Status` under `metadata:` instead of as top-level author fields). This is exactly the split-brain this plan warns against. Affected: `26_clockwork`, `34_dual_quant`, `43_Castle_Process`, `43_clockwork`, `54_Tapegrade`, `58_LoChoVibes`, `59_BitPhase`, `74_Wild_Pebble`, `84_CosmikC1zzl3`, `87_fr330hfr33`, `93_Turing_Matrix`. These files should be restored to author-shaped source per `documentation/info.yaml.md`. The `generated-model-shape` rule flags them so the backlog stays visible. **Caveat:** the canonical schema lives in `documentation/info.yaml.md` and not all cards have been migrated to it yet, so validator warnings largely track that migration backlog rather than fresh regressions.

### Phase 1: Replace the data model first

Port the MTM importer behavior into JavaScript inside `tools/sitegen`.

This is the highest-value step because the current local generator already parses only a small subset of the richer metadata. The schema in `documentation/info.yaml.md` already describes fields such as `draft`, `contact`, `panel`, `controls`, and `host`, but several are not rendered by the current site.

Implementation notes:

- keep the existing `discoverRelease()` entry surface if convenient, but change its return shape to the new canonical card model
- move field normalization out of the current thin `discover/infoFields.js` into dedicated model normalizers
- emit `site/cards.json` during development for parity checking and debugging

### Phase 2: Port the MTM detail-page UI into static HTML renderers

Once the card object is stable, recreate the MTM program-card detail page in JS render functions rather than Liquid templates.

Priority sections:

1. hero/header
2. draft banner
3. tags
4. quick start
5. panel diagram and socket lists
6. controls and switch modes
7. documentation sections
8. about/notes/data sources

This should replace the current simple README-centric detail page rather than trying to merge both layouts.

### Phase 3: Port the index/archive experience

After detail pages reach parity, rebuild the index/archive pages using the new card model.

Priority behavior:

- search and filtering
- tag display
- clearer card summaries
- stable sorting and release-number presentation

The existing WebUSB flashing affordance can then be reintroduced into the new UI once the structural migration is complete.

#### What the MTM "discover" experience actually is

The MTM discovery UI is **curated-shelf plus static archive rendering**, driven by two curation data files. It is not a faceted search engine. Concrete inventory to port:

| MTM source | Role |
| --- | --- |
| `_layouts/program_cards_home.html` + `_data/program_cards/discovery.yml` | Discovery home: hero shelf plus themed shelves grouped by tag (`cards_from_tags`) or explicit card lists, including video-lead layouts |
| `_layouts/program_cards_archive.html` | Complete one-line-per-card archive index (number, title, summary, draft flag, tags) |
| `_includes/program_cards/{shelf,card_tile,tags,discovery_embed}.html` | Reusable shelf / tile / tag-chip / embed fragments |
| `_data/program_cards/tags.yml` | Tag curation: `available_tags` (id, label, color, text_color) plus `assignments` (card_id -> tags) |
| `_data/program_cards/discovery.yml` | Discovery config: page title, hero, shelf definitions, embeds |
| `assets/program_cards/preview-tools.js` (`renderDiscovery`, `renderShelf`, `renderCardTile`, `renderTags`, `setupDiscoveryPreview`) | Browser mirror used by the discovery-YAML dev preview |
| `_sass/minima/_program-cards.scss` | Full visual system: tiles, shelves, archive rows, tags, hero, panel |

#### Where it lands in `tools/sitegen`

- **Curation data** (`tags.yml`, `discovery.yml`) -> `tools/sitegen/src/curation/`. These are presentation-only (tag colors, shelf grouping, ordering) and must stay in site curation files, never in `releases/*/info.yaml`. Seed tag assignments from the `tags` already normalized out of `info.yaml` in Phase 1.
- **Render functions** (shelf, tile, tag chip, archive row) -> `tools/sitegen/src/render/`.
- **Styles** -> port `_program-cards.scss` into `tools/sitegen/assets/`.

#### Open decisions for Phase 3

- **Search vs curated shelves.** MTM ships no live text search; the "search and filtering" priority above is an addition beyond MTM parity. Decide between faithful parity (curated shelves plus archive), parity plus a client-side tag/text filter, or keeping the current `tools/sitegen` search UI and layering shelves/tags on top.
- **Tag assignment seeding.** The MTM importer only appends blank tag stubs for moderators. Phase 1 now normalizes `info.yaml` `tags`, so those can seed the curation file rather than starting empty.


### Phase 4: Remove obsolete implementation paths

After parity is reached:

- delete render paths that only exist for the current simple site
- remove now-unused normalization helpers
- collapse old and new code into one clean build path

Do not carry both renderers long term.

## What To Port Versus What To Keep

### Keep from the current local generator

- Node-based build pipeline
- existing Pages workflow
- Git-aware date detection
- README markdown rendering
- raw GitHub URL generation
- download discovery
- local web-editor asset copying
- WebUSB programming support, if still wanted

### Port from the MTM site

- richer canonical card model
- draft-state presentation
- tags rendering
- panel-centric detail layout
- socket/control/switch/LED/documentation sections
- card-level notes and data-source display
- any curated presentation rules that are not firmware metadata

### Do not port blindly

- Jekyll/Liquid structure
- MTM site-wide layout assumptions unrelated to program cards
- any duplicated logic that now already exists cleanly in `tools/sitegen`

## Data Ownership Rules

To avoid repeating the MTM split-brain setup, define ownership clearly.

## Authoring Boundary

The YAML preview/editor is an author-facing tool.

That means card authors should work directly against the source schema documented in `documentation/info.yaml.md` and should not need to understand, inspect, or edit the normalized card model used by the metadata site.

### Author-facing contract

- authors paste or edit YAML that matches `documentation/info.yaml.md`
- preview tooling validates and previews that source shape directly
- preview tooling should explain source-schema problems in source-schema terms
- the tool should never require authors to learn the normalized card object
- the edit pane should load the current raw `info.yaml` for the selected card
- the author workflow should not require a separate "normalize" action

### Site-facing contract

- the site build is free to normalize source YAML into any internal card object needed for rendering
- the normalized object is an implementation detail of the metadata site
- if exposed at all for debugging, it should be clearly labeled as generated/internal and not part of the author workflow

### Design implication

The normalization layer should sit strictly behind the authoring tools.

In practice this means:

- source editing and preview should start from raw `info.yaml` content
- normalization should happen after parse/validation, as a build-time or preview-time transform
- preview pages may render from normalized data internally, but they should present errors, hints, and examples in terms of the source schema
- do not make the normalized representation the editable form in the browser

### Preview tool consequences

The current concept of a manual normalize button should go away.

> **Status: satisfied.** The delivered author tool (`site/preview/`) has no normalize step. It loads the selected card's raw `info.yaml`, and validates + previews live from that source on every edit; normalization runs implicitly (behind the scenes) only to render the preview and is never presented as the editable form.

Preferred flow:

1. select a card
2. load that card's current raw `info.yaml` into the edit pane
3. edit raw YAML directly
4. validate and re-render preview from that source

Normalization may still happen internally, but it should be implicit and invisible in the author workflow.

## Build Artifacts For Preview And Debugging

To support an author-facing preview without exposing normalized data as the editable format, the build should emit both raw-source and normalized artifacts.

### Required artifacts

- `site/cards.json`
  - normalized internal card model used by the site renderer
- `site/raw-info/`
  - one raw `info.yaml` payload per card, keyed by card id or slug
- optionally `site/raw-info/index.json`
  - lookup metadata for mapping card id/slug to raw source payload

### Purpose of raw-source artifacts

- let the preview/editor load the current `info.yaml` exactly as authored
- keep the browser authoring experience aligned with `documentation/info.yaml.md`
- avoid deriving editable YAML by reverse-transforming normalized card data

### Important constraint

Do not regenerate editable YAML from normalized JSON.

That would create drift, formatting loss, comment loss, and author confusion. The editable source should come from the raw `info.yaml` content itself, or from a preserved raw parse payload that is semantically identical to it.

### Implementation preference

For preview tooling, prefer shipping the literal raw file contents per card rather than a re-serialized YAML object.

That preserves:

- comments
- field ordering
- formatting choices
- nearby author notes

If comments or exact formatting are not needed in a specific preview mode, a parsed-source companion artifact can still be emitted, but the primary editor source should remain the raw file text.

## Shared Validation Component

Yes, this should be designed as a reusable component.

The preview/editor, the site build, and future PR-level or commit-level checks should all use the same validation engine so that the repo has one definition of what constitutes valid `info.yaml`.

> **Status: first cut delivered.** `tools/sitegen/src/validate/` implements the shared engine below. It depends only on the schema adapter and rule set (no page rendering, no normalized card model), so the one engine is reusable across every consumer. Wired now: the `validate-info` CLI, and the site build (non-fatal reporting pass — prints a summary, never fails the build; authors can fix cards over time). Planned (not built yet): a PR GitHub Action (`--github` annotations reporter is ready) and a commit/pre-push hook. `--json` output supports any other machine consumer.
>
> Delivered layout:
> - `src/validate/parseSource.js` — YAML parse; syntax errors become diagnostics with line/col; emits a top-level-key → source-line map.
> - `src/validate/rules/index.js` — the rule set (see below); each rule is `{ id, check(ctx) -> diagnostic[] }`.
> - `src/validate/validateInfoYaml.js` — orchestrator; builds the rule context (schema adapter + normalized-key data lookups + source lines) and returns `{ file, ok, errorCount, warningCount, diagnostics }`.
> - `src/validate/reporters/` — `text` (CLI), `json` (machine-readable), `github` (Actions `::error`/`::warning` annotations).
> - `src/validate/cli.js` — `npm run validate-info [-- --json|--github|--strict|--quiet] [paths…]`; defaults to all releases; exit 1 on any error (or on warnings with `--strict`).
> - `src/validate/index.js` — barrel export for programmatic consumers (build/hooks).
>
> Severity policy (so a future CI gate can fail on errors without choking on the migration backlog): **errors** = YAML syntax + missing/mistyped **required core** author fields only; **warnings** = everything else (unknown keys, legacy/optional structural shapes, tag-object shape, URL/format hints, draft completeness, and the `generated-model-shape` split-brain flag).

### Required consumers

- author-facing preview/editor
- local site build
- future CI validation on pull requests
- future commit or pre-push validation hooks

### Recommended shape

Build a standalone validation module inside the site toolchain, with no dependency on page rendering.

Suggested structure:

- `tools/sitegen/src/validate/`
  - `parseSource.js`
  - `validateInfoYaml.js`
  - `rules/`
  - `reporters/`

### Responsibilities

#### `parseSource.js`

- parse raw YAML source
- preserve file identity and source path
- surface syntax errors with line/column information when available

#### `validateInfoYaml.js`

- validate against the author-facing schema documented in `documentation/info.yaml.md`
- return structured diagnostics
- avoid any dependency on HTML rendering or browser-only code

#### `rules/`

Organize validation as reusable rule functions, for example:

- required core fields
- field type checks
- enum-like checks where applicable
- URL/path validation
- `Editor` mode validation
- `panel` / `controls` / `switch` structural validation
- duplicate or conflicting metadata checks
- warning-level completeness checks for draft vs non-draft cards

#### `reporters/`

Multiple outputs should be supported from the same diagnostic model:

- preview-friendly messages
- terminal/CLI messages
- CI annotations or machine-readable JSON

### Diagnostic model

The validation result should be source-schema-oriented and reusable.

Each diagnostic should ideally include:

- severity: `error` or `warning`
- rule id
- source file path
- field path, for example `controls.knobs[1].main.name`
- human-readable message
- optional line/column range
- optional suggestion or autofix hint

### Architectural rule

Normalization should consume validated source data, but validation must not depend on normalized output.

That keeps the validator aligned with author intent and makes it safe to reuse in CI without running the full site renderer.

### CLI entrypoint

Plan for a small CLI wrapper around the shared validator, for example:

- `npm run validate-info -- releases/43_clockwork/info.yaml`
- `npm run validate-info -- releases/**/info.yaml`

That wrapper can later be called by:

- GitHub Actions on pull requests
- commit hooks
- local author tooling

### Scope split

Keep validation in two layers:

1. **schema/source validation**
  - reusable everywhere
  - checks author-facing `info.yaml`
2. **site/build validation**
  - used by the site build only
  - checks renderer-specific assumptions, asset existence, derived-link resolution, and similar concerns

This avoids forcing CI validation to understand full site rendering while still allowing the build to catch site-specific problems.

### Recommendation

Treat shared validation as a first-class migration deliverable, not an afterthought.

If the normalization and preview work land before validation is factored out, the repo will likely end up with three different rule sets:

- preview rules
- build-time normalization assumptions
- future CI rules

That would be the wrong direction. One reusable validator should sit underneath all three.

## Schema Evolution Strategy

Yes, this changes the design slightly.

The system should be designed so that the schema definition itself is swappable without forcing a rewrite of validation, normalization, preview tooling, or documentation generation.

Today, the practical canonical source is `documentation/info.yaml.md`.

Later, the likely target is:

- JSON Schema as the canonical machine-readable schema
- `documentation/info.yaml.md` generated from that schema plus hand-authored explanatory text where needed

The migration design should therefore avoid hard-coding schema rules directly into renderers, normalizers, or UI components.

### Design rule

Every downstream component should depend on a schema interface, not on the current documentation file format.

That means:

- preview/editor should consume schema metadata through a shared adapter
- validator rules should be generated from or aligned with that same schema source
- normalizers should consume validated parsed data, not re-encode field requirements themselves unless they are performing site-specific derivation
- documentation generation should be able to read the same schema source

## Schema Source Layer

Introduce a dedicated schema-source layer inside the toolchain.

Suggested structure:

- `tools/sitegen/src/schema/`
  - `schemaSource.js`
  - `schemaAdapter.js`
  - `schemaDocs.js`
  - `schemaDefaults.js`

### Responsibilities

#### `schemaSource.js`

Defines how canonical schema data is loaded.

Initial mode:

- derive a structured internal schema description from `documentation/info.yaml.md` and any local hard-coded supplements needed temporarily

Future mode:

- load canonical JSON Schema directly

This module is the seam that allows the source of truth to change later.

#### `schemaAdapter.js`

Presents one stable in-memory schema interface to the rest of the system, regardless of whether the underlying source currently comes from Markdown-derived data or JSON Schema.

Example responsibilities:

- list fields
- identify required fields
- expose types
- expose enums or constrained values
- expose descriptions/help text
- expose nested object/list shapes
- expose draft/completeness semantics

#### `schemaDocs.js`

Owns documentation generation or synchronization.

Short term:

- may simply verify that implementation assumptions still match `documentation/info.yaml.md`

Long term:

- can generate `documentation/info.yaml.md` from JSON Schema plus curated prose sections

#### `schemaDefaults.js`

Central place for non-schema defaults or policy values that should not be duplicated in validators and normalizers.

Examples:

- default discussion URL
- accepted `Editor` keywords
- known tag policies
- completeness thresholds for `draft: false`

## Downstream Dependency Rules

To make future schema changes cheap, each subsystem should depend on the schema layer in a narrow way.

### Validator

The validator should read field structure and basic constraints from the schema adapter, then layer custom semantic rules on top.

This keeps simple schema edits from requiring manual changes across multiple rule files.

### Preview/editor

The preview/editor should use the schema adapter for:

- field help text
- validation messaging
- required-vs-optional indicators
- example generation where relevant

It should not embed its own separate notion of field structure.

### Normalizer

The normalizer should assume validated input and focus on deriving the internal card model.

It may still contain transformation logic, but it should not be the primary place where schema truth lives.

### Site renderer

The renderer should depend only on the normalized card model and should know nothing about the source schema beyond what is already normalized for display.

### CI and automation

Future PR and commit validation should call the shared validator through the same schema adapter used everywhere else.

## Short-Term Reality

Because `documentation/info.yaml.md` is currently the canonical source, do not over-engineer a Markdown parser that pretends the doc is already a formal schema language.

A pragmatic first implementation is:

1. define a structured internal schema object in code
2. ensure it matches `documentation/info.yaml.md`
3. treat the Markdown doc as the current human canonical reference
4. keep the schema object isolated so it can later be replaced by JSON Schema loading

This is slightly less pure than deriving everything from the Markdown file, but much more maintainable.

In other words: do not make the future JSON Schema migration harder by tightly coupling the new system to Markdown parsing.

## Recommended Transition Path

1. Build a stable internal schema adapter now.
2. Back it initially with a local structured schema definition that mirrors `documentation/info.yaml.md`.
3. Use that adapter in validator and preview tooling.
4. Keep normalization and rendering downstream of validated data.
5. Later swap the adapter backend from local structured schema data to JSON Schema.
6. Generate or partially generate `documentation/info.yaml.md` from that JSON Schema when ready.

## Recommendation

Yes, the design should change in one important way: schema knowledge must become its own layer.

Without that layer, moving from Markdown-documented schema to JSON Schema later will force edits across:

- validator rules
- preview UI
- normalization assumptions
- documentation tooling
- CI checks

With the layer in place, the future migration becomes mostly a schema-source swap instead of a system rewrite.

### Belongs in `releases/*/info.yaml`

- card behavior
- panel I/O
- controls
- switch meanings
- documentation sections
- notes tied to card operation
- demo/video links
- source repository links
- status/version/creator/license/contact

### Belongs in local site curation files

- presentation-only tag colors or grouping
- global discussion defaults
- archive ordering overrides if ever needed
- hand-maintained exceptions that should not pollute release metadata

If a value is intrinsic to a card and useful outside the website, it should live in `info.yaml`, not a site-only curation file.

## Recommended File Moves

These are proposed structure changes, not yet implemented.

- add `tools/sitegen/src/model/`
- add `tools/sitegen/src/render/pages/`
- add `tools/sitegen/src/render/partials/`
- add `tools/sitegen/src/curation/`
- optionally add `tools/sitegen/src/debug/`

Likely deletions after migration:

- the current simple detail-page renderer inside `tools/sitegen/src/build.js`
- thin metadata-only helpers that no longer match the richer model

## Parity Checklist

Before switching the site over, verify that the new `tools/sitegen` build can surface:

- draft banners
- creator/language/version/status metadata
- tags
- README links
- download links
- editor links
- videos
- quick start sections
- panel inputs and outputs
- control descriptions
- switch-mode descriptions
- LEDs
- documentation sections
- notes
- source/data provenance

Also verify that cards with only minimal metadata still render acceptably.

## Risks

### 1. Hidden MTM curation data

The MTM site may contain auxiliary data or presentation defaults outside `info.yaml`.

Mitigation:

- audit the MTM repo for `_data/program_cards/*`, includes, and preview tooling
- move any required non-firmware data into `tools/sitegen/src/curation/`

### 2. Mixed metadata generations in this repo

Some cards already include richer blocks such as `switch_modes`, `notes`, and `documentation`, while others remain sparse.

Mitigation:

- make every richer section optional
- preserve graceful fallback to README-only cards

### 3. Regressing current conveniences

The current local site has working asset discovery and WebUSB flashing integration.

Mitigation:

- treat those as features to reattach after the new page structure is stable
- do not entangle device programming code with card-data normalization

## Recommended Execution Order

1. Audit the MTM repo for all program-card-specific data files and includes.
2. Define the canonical JS card schema in code.
3. Port the Ruby importer behavior into JS normalizers.
4. Emit `cards.json` and compare a sample of cards with the MTM site's generated data.
5. Rebuild one detail page using the new renderer.
6. Rebuild the index/archive pages.
7. Reattach WebUSB/programming affordances.
8. Delete the old simple renderer.

## Recommendation

Replace the current `tools/sitegen` implementation in place.

That is the cleanest path because it:

- avoids maintaining two local site architectures
- removes the Jekyll dependency entirely
- keeps existing CI and deployment entrypoints stable
- moves the MTM program-card experience into the repo that already owns the card metadata