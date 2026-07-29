---
name: choose-card-license
description: "Interview a Workshop Computer card author to choose a legal License value for info.yaml. Use when: selecting a license, discussing commercial reuse, ports, derivatives, modified Workshop System cards, VCV Rack, plugins, or hardware adaptations."
argument-hint: "Describe the card, its source dependencies, and reuse preferences"
---

# Choose a Workshop Computer Card License

Help a card author select the `License` value required by `documentation/info.yaml.md`.

## Boundaries

- State that this is general guidance, not legal advice.
- Clearly separate the **legal license** from the author's **community preferences**.
- Explain that courtesy requests cannot override permissions granted by a legal license.
- Do not imply that commercial software/hardware permissions restrict selling or releasing music made with the card.
- Link to [Choose a License](https://choosealicense.com/) for further guidance.

## Interview

Ask these questions one at a time, using plain language:

After each answer, briefly update the provisional recommendation or explain which remaining legal answer is needed. Do not present a provisional result as final.

1. Does the card include or derive from third-party code, libraries, samples, documentation, or hardware designs? If yes, identify their licenses before recommending anything.
2. May others distribute closed-source versions of the card software?
3. Must distributed derivatives remain open under the same terms?
4. Does the author want a broad public-domain-style dedication?
5. May others sell or commercially distribute modified software, plugin ports, hardware versions, or other derivatives? Clarify that this does not concern music created with the card.
6. Ask separately for non-legal author preferences for:
   - a derivative or modified Workshop System card;
   - a VCV Rack port;
   - a plugin port, including a sold plugin;
   - a hardware version, including a sold hardware version.

For each preference, offer:

- OK without asking, with credit
- OK without asking
- Please contact me first
- I am not comfortable with this

## Recommendation Rules

- Recommend `MIT` for permissive reuse, including commercial and closed-source derivatives, with notice retention.
- Recommend `GPL-3.0-or-later` when distributed derivatives must remain open under compatible GPL terms.
- Recommend `CC0-1.0` only for a requested broad public-domain-style dedication, with a warning to review its suitability for software and patent concerns.
- If commercial reuse is prohibited or permission is required, explain that standard open-source licenses are not a match and ask the project maintainers for a source-available/custom policy.
- If inherited or third-party licensing is unresolved, do not make a final recommendation.

## Output

Return:

1. Recommended legal license and short rationale.
2. Permissions and obligations in plain language.
3. Compatibility or dependency warnings.
4. A clear note that author preferences are separate from legal conditions.
5. A link to the authoritative full license text and instructions to add it as `LICENSE` beside `info.yaml`, including any copyright placeholders or source notices that must be completed.
6. The exact YAML line:

`License: <SPDX identifier or approved short name>`

Do not serialize community preferences into `notes`, `readme`, or another unrelated field.
Do not invent or paraphrase the legal text; direct the author to copy the complete authoritative text and preserve third-party notices.
