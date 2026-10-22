# Submitting the card to the community repository

The card folder is ready as **`389_QiGong_Timer`** (the number Joe asked for; checked free on 20 Oct 2026: no `389_` folder
on the main branch and no open pull request mentioning 389. Numbers are claimed first-come by pull request, so submit soon.)

## Before you submit
1. (Done: Creator is "Joe Hedinger (isbn)" in `info.yaml`, `README.md` and `LICENSE`.)
2. Optional: add a `contact:` block to `info.yaml` (email / website / social links; see the schema in
   `documentation/info.yaml.md` of the repository). Without it the validator gives one warning.
3. When the hardware test of the final build is done: set `Status: Released`, `Version: "1.0.0"`, rename the `.uf2` to match,
   update the `uf2:` entry and `date-updated`, and set `draft: false`.

## The pull request
1. Fork https://github.com/TomWhitwell/Workshop_Computer on GitHub.
2. Copy this whole folder into `releases/389_QiGong_Timer/` (the source, `UF2/`, `samples/`, `info.yaml`, `README.md`,
   `LICENSE`, `lib/`, `host/`, `scripts/`, `docs/`). Do NOT add build folders (`build*`, `out/`, `host/out/`).
3. From the repository root:
   ```sh
   npm install --prefix tools/sitegen
   npm run validate-info      # our card must show no errors (other, older cards may)
   npm run sync-curation      # adds our card to tools/sitegen/src/curation/flairs.yml: commit that change too
   ```
4. Commit, push, and open a pull request titled e.g. "Add 389_QiGong_Timer". Make sure `UF2/*.uf2` and `samples/*.mp3`
   are committed: the site only lists files that are in git.

## Checked on 20 Oct 2026 (from a sparse clone of the repository)
* `npm run validate-info`: **no errors for this card**; one warning (`contact` missing). The run exits non-zero because of
  errors in four older cards (15_MLRws, 36_GradualProcess, 45_bends, 61_ZX_Spectrum), not ours.
* `npm run sync-curation`: adds one assignment for 389_QiGong_Timer.
* The full site build could not be run from a sparse clone (it needs every card's files).

## Note on rebuilding
A rebuild on another day differs from `UF2/qigong_timer.0.9.0.uf2` in 2 bytes: the build date the Pico SDK stamps into the firmware.
The program itself is identical.
