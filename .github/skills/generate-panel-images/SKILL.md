---
name: generate-panel-images
description: "Collaboratively design a standalone Workshop Computer panel SVG by interviewing the author about panel purpose, controls, jacks, switch labels, and wording, then generate and download the approved SVG. Use when: someone wants to design a panel image independently of any release folder, repository metadata, manifest, firmware, or card implementation."
argument-hint: "Describe the panel or card idea"
---

# Design a Standalone Workshop Computer Panel

Collaborate with an author to design one standalone Workshop Computer panel image, generate a self-contained vector SVG, preview it, revise it, and download the approved SVG. This workflow is independent of release folders and repository metadata.

## Boundaries

- Do not ask for a release directory, card folder, repository, manifest, `info.yaml`, firmware, README, or source code.
- Do not create or update `info.yaml`, `panels/manifest.yaml`, companion Markdown, release files, or site data.
- Do not inspect repository files to infer panel behavior.
- Treat only the author's descriptions and answers as the source of truth.
- Do not invent control meanings, jack roles, switch states, labels, or behavior.
- Ask concise questions when a visible panel decision is missing or ambiguous.
- Leave LEDs out entirely. Do not ask about, document, label, or generate LED meanings in this workflow.
- Output SVG only. Never create a PNG fallback or screenshot.
- Design, generate, preview, and download exactly one panel at a time. Never batch several panels into one approval or generation step.
- Keep the author's established card concept, terminology, shared controls, and jack roles in conversational context. If the author requests another panel afterward, start a new single-panel cycle using that context and ask only about meanings that differ.
- Do not commit or push.

## Interview

Conduct the interview as a natural conversation in ordinary chat.

- Do not use structured question dialogs, questionnaires, schemas, tables to fill in, or multi-question forms.
- Ask one short, plain-language question at a time.
- Begin broadly, listen to the author's description, and ask the next most useful follow-up based on that answer.
- Acknowledge each answer briefly and reflect the emerging design in everyday language.
- Do not force the author to answer in hardware IDs or metadata terminology. Translate their descriptions to physical panel positions yourself, then confirm the interpretation conversationally.
- Combine details the author volunteers; never ask again for information already provided.
- Offer concise wording suggestions only when useful, phrased as collaborative options rather than required choices.
- Always look for familiar, human-readable short-forms before finalizing visible labels, and use them consistently across related controls and jacks. Examples: `Feedback` → `FB`, `Quantized`/`Quantize` → `Quant`, `Modulation` → `Mod`, `Input` → `In`, and `Output` → `Out`. Never abbreviate so aggressively that the meaning becomes unclear.
- Propose useful short-forms conversationally, explain the full term when needed, and explicitly ask the author whether each non-obvious abbreviation makes sense to them. Keep the full wording when the author is unsure or prefers it.
- Keep technical validation and file-format details out of the interview unless there is a problem the author must resolve.

### 1. Panel concept

Open with a friendly broad prompt such as: **“Tell me what your card does and how you imagine using it.”** From the answer, naturally establish the name and purpose of the single panel being designed now. Ask only the missing detail next.

If the author mentions multiple states, ask which one to design first. Complete and download that panel before offering to continue with another. When the author subsequently requests another state, retain shared decisions from the prior conversation, confirm what carries over, and interview only about differences. Propose one concise filename for the current panel and ask for confirmation.

### 2. Knobs and switch

Work through the knobs and switch conversationally. For example: **“What does the big knob do in this state?”** Continue with X, Y, and the switch only after each prior answer. Establish the exact short visible label for:

- Main knob
- X knob
- Y knob
- Z switch Up
- Z switch Middle
- Z switch Down

The author may mark any item unused. Ask about Tap only when the author says a brief Down-switch action matters. Offer shorter label alternatives when wording will wrap awkwardly, without changing meaning.

### 3. Inputs

Ask how signals enter the card in the author's own terms, then map that description to the physical inputs. Clarify one ambiguous connection at a time. Establish the exact short visible label for each used input:

- Audio In 1
- Audio In 2
- CV In 1
- CV In 2
- Pulse In 1
- Pulse In 2

Unused inputs remain unlabeled.

### 4. Outputs

Ask what the card sends out, then map those signals to the physical outputs. Clarify one ambiguous connection at a time. Establish the exact short visible label for each used output:

- Audio Out 1
- Audio Out 2
- CV Out 1
- CV Out 2
- Pulse Out 1
- Pulse Out 2

Unused outputs remain unlabeled.

## Approval

Once the conversation has supplied enough information, present a compact design summary:

- Panel title and filename
- Main/X/Y labels
- Z switch labels
- Input labels by physical jack
- Output labels by physical jack

Ask conversationally whether it looks right or what they would change. Do not generate until the visible labels are approved. The summary is a review of the conversation, not another form for the author to complete.

Before approval, call out every shortened label alongside its full meaning and confirm that the author finds the short-form readable. Do not treat silence as approval for an abbreviation.

## Generate the SVG

After approval, create a temporary standalone design source outside `releases/` and outside tracked repository content. Use the repository's canonical panel renderer only as a rendering utility; do not create metadata in the project.

Write the approved visible labels to a temporary JSON file under `/tmp`, then run [the standalone renderer](./scripts/render-standalone-panel.mjs):

`node .github/skills/generate-panel-images/scripts/render-standalone-panel.mjs /tmp/<name>.json /tmp/<name>.svg`

The temporary JSON may contain `title`, `controls`, `switch`, `inputs`, and `outputs`. Use the renderer's physical slot keys; this file is an internal rendering input, not author-facing metadata.

The generated SVG must:

- use `viewBox="0 0 560 1785"`;
- declare the documentation-default intrinsic viewport, `width="280"` and `height="892.5"`, so opening the downloaded SVG shows the whole panel at its normal documentation scale;
- preserve the standard Workshop Computer vector panel artwork;
- contain vector label rectangles and SVG text;
- embed the panel font;
- be self-contained, with no scripts, `foreignObject`, event handlers, or external resources;
- use content-driven label rectangle heights and word-preserving wrapping;
- omit labels for unused controls and jacks.

A temporary local rendering document or script may be created under `/tmp`. Remove temporary source and intermediate files after the final SVG has been reviewed and downloaded.

## Preview and iterate

1. Open the generated `.svg.preview.html` wrapper in the browser, not the raw SVG. It displays the SVG at the same 280 CSS-pixel width used by documentation/detail pages so the author can see the whole panel at once.
2. Ask the author to review wording, wrapping, rectangle height, and placement.
3. Apply only requested changes.
4. Repeat until the author approves the design.

## Output format

Finish with:

1. **Approved design** — title and concise visible-label map.
2. **SVG details** — filename, `560 × 1785` canonical viewBox, and self-contained/vector status.
3. **Preview** — local browser preview URL.
4. **Download** — as the final operational step, trigger a browser download of the exact approved SVG and state the downloaded filename.

Do not leave the final deliverable merely in the repository or workspace; the successful single-panel workflow ends with the browser download. Afterward, the author may request a subsequent panel using the retained context, which begins a new one-panel workflow.
