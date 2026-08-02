# Curating Program Card flairs and the index

This guide is for the site editor who chooses the editorial badges (“flairs”) shown on Program Cards and arranges the curated shelves on the Program Cards index page.

Curation is presentation-only. It belongs in `tools/sitegen/src/curation/`, not in a card author's `releases/*/info.yaml`.

## The two curation files

| File | Controls |
| --- | --- |
| `tools/sitegen/src/curation/flairs.yml` | Flair names, colours, descriptions, and the flairs assigned to each card |
| `tools/sitegen/src/curation/discovery.yml` | Index heading, featured cards, shelf order, shelf membership, and shelf layout |

Every release with an `info.yaml` is automatically included in the complete index and searchable card list. Curation decides whether it also receives an editorial flair or appears on a curated shelf.

## Before editing

From the repository root, synchronize the card list:

```sh
npm run sync-curation
```

This adds an empty assignment for every new card. It does not award a flair: an empty list means that the card has been registered but has not been given an editorial badge.

The command preserves existing assignments and reports stale entries instead of deleting them. A stale entry usually means that a release folder was renamed or removed and should be investigated before editing `flairs.yml`.

## Curating flairs

### Assign or remove a flair

Find the card ID under `assignments` in `flairs.yml`. Card IDs exactly match folder names under `releases/`.

```yaml
assignments:
  "41_blackbird":
    - "deep"
    - "useful"

  "72_motorik": []
```

Use a flair's `id` in assignments. IDs are lowercase and stable even if the visible label changes.

To remove a flair, remove that list item. Keep the card entry with `[]` when it has no flairs; this records that the card is known to the curation system.

### Current flair vocabulary

| ID | Visible label | Suggested use |
| --- | --- | --- |
| `included` | Included | Ships with the Workshop Computer as an included Program Card |
| `new` | New | A recent release that should receive temporary attention |
| `classic` | Classic | An established core card or familiar reference |
| `start-here` | Start Here | A classic card recommended for new users and everyone else |
| `updated` | Updated | Recently received a meaningful firmware or documentation revision |
| `toms-pick` | Tom's Pick | Recommended by Tom Whitwell |
| `chris-pick` | Chris's Pick | Recommended by Chris Johnson |
| `wild` | Wild | Unexpected, unusual, or deliberately unruly cards |
| `useful` | Useful | Solves practical problems or makes the system easier to use |
| `deep` | Deep | An intricate or ambitious card to explore in depth |
| `magic` | Magic | An extraordinary card that feels impossible |
| `instant-win` | Instant Win | A simple, immediately satisfying card |

`New` and `Updated` are temporary editorial judgements. Review and remove them when they are no longer useful.

### Add or change a flair

The `available_flairs` section defines each flair:

```yaml
available_flairs:
  - id: "useful"
    label: "Useful"
    description: "Properly useful cards that solve problems and make the system easier."
    color: "#a63d00"
    text_color: "#ffffff"
```

- Keep `id` short, lowercase, and stable.
- `label` is the text visitors see.
- `description` records the editorial meaning.
- Use hexadecimal colours with enough contrast between `color` and `text_color`.
- Removing or changing an ID requires updating every assignment and every shelf that refers to it.

### Editorial flairs versus author tags

These are intentionally different:

- **Editorial flairs** are maintained in `flairs.yml` and appear as curated badges. They may also drive shelves.
- **Author tags** come from `releases/*/info.yaml`. They describe a card for search and filtering, but do not place it on a curated shelf.

Do not copy editorial choices into an author's metadata.

## Curating the index page

Edit `tools/sitegen/src/curation/discovery.yml`. The page is rendered in this order:

1. `hero`: the prominent first shelf.
2. `shelves`: the remaining shelves, in file order.
3. Search results: shown when a visitor searches or filters.

The complete numeric list remains available through **Browse all cards**, independently of the curated shelves.

### Hero cards

The hero uses an explicit, ordered list:

```yaml
hero:
  title: Included cards
  layout: grid
  featured:
    - 03_Turing_Machine
    - 20_reverb
```

Use exact release-folder IDs. Reordering this list reorders the cards.

The four included cards have custom artwork in the current design. Adding an unrelated card to the hero does not automatically create matching artwork.

### Explicit shelves

Use `cards` when membership and order are deliberate:

```yaml
- id: start-here
  title: Try these first
  layout: video-lead
  cards:
    - 15_MLRws
    - 71_degenerator
    - 25_utility_pair
```

This is the best choice when the first card matters or when Tom wants a hand-picked sequence.

### Flair-driven shelves

Use `cards_from_flairs` when the shelf should follow flair assignments:

```yaml
- id: properly-useful
  title: Properly useful
  layout: list
  cards_from_flairs:
    - useful
  hide_flairs:
    - useful
  limit: 6
```

- A card is included when it has any listed flair.
- `limit` caps the number displayed.
- `hide_flairs` hides a redundant badge within that shelf; it does not remove the assignment.
- Flair-driven shelves currently use the site's canonical card-number order. Assignment order in `flairs.yml` does not control them. Use an explicit `cards` shelf when editorial ordering is required.

Do not put both `cards` and `cards_from_flairs` on one shelf.

### Layouts

| Layout | Intended presentation |
| --- | --- |
| omitted or `grid` | Normal three-column card grid |
| `list` | Tighter two-column editorial list |
| `lead` | One large first card with supporting cards |
| `video-lead` | One prominent demo thumbnail with supporting cards |
| `video-strip` | Compact row of cards with demo thumbnails |

Video layouts use a card's first valid demo video when one is available. They do not create a video for cards without one.

### Common recipes

#### Feature a newly released card

1. Run `npm run sync-curation`.
2. Add `new` to the card in `flairs.yml`.
3. It will enter the flair-driven **New** shelf, subject to that shelf's limit and numeric ordering.
4. If it must appear first, use or create an explicit shelf instead.

#### Mark a card as properly useful

Add `useful` to its assignment. The card enters the **Properly useful** shelf and receives the badge elsewhere. Remove the flair when it should leave both.

#### Put a card on one shelf without awarding a badge

Add its ID to an explicit `cards` list in `discovery.yml`. Do not create a flair merely to support one hand-picked shelf.

#### Reorder the homepage

Move whole shelf blocks to change shelf order. Reorder IDs inside an explicit `cards` list to change card order. Flair-driven shelves cannot be manually reordered.

## Check and preview changes

Run these commands from the repository root:

```sh
npm run check-curation
npm run build
```

The curation check catches:

- cards missing from `flairs.yml`
- stale card assignments
- unknown or duplicate flairs
- invalid assignment shapes
- missing cards referenced by the hero, embeds, or shelves
- unknown layouts
- shelves with conflicting membership rules
- invalid limits
- malformed YAML

Then serve the generated `site/` directory with the usual local preview and inspect:

- the default index with no search active
- each changed shelf
- badges on affected card tiles and detail pages
- the **Browse all cards** archive
- tag filtering under **Advanced search**

The GitHub Pages workflow synchronizes missing empty assignments before checking and building. After a card is merged to `main`, the curation workflow commits the synchronized `flairs.yml`. Invalid references, stale assignments, unknown flairs, and malformed curation still fail with an actionable error.

## Safe editing rules

- Change only curation files when making an editorial decision.
- Never rename a release folder merely to alter its displayed title.
- Never delete a stale assignment until the corresponding release rename or deletion is understood.
- Prefer flair IDs, not visible labels, in assignments and shelves.
- Keep empty assignment entries.
- Treat `New`, `Updated`, `Tom's Pick`, and `Chris's Pick` as editorial choices, not objective card metadata.
- Run the curation check before committing.
