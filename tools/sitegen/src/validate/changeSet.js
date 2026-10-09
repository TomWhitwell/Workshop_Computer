// Validate a card change set: the single definition of what a change is
// checked for, shared by PR validation in CI and the pre-commit hook.
//
// Callers supply the change list (parsed `git diff --name-status -z`), a root
// holding the proposed tree, and optionally a baseline root holding the tree
// the change is compared against. Diagnostics present in the baseline are
// reported as `existing`, everything else as `introduced`; each caller decides
// which of those block.

import fs from 'node:fs';
import path from 'node:path';
import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';
import { readCustomPanelManifest } from '../discover/customPanels.js';
import { loadKnownValues } from './knownValues.js';
import { evaluatePrRules, summarizePrTrigger } from './prRules.js';

// Rules describing the state of a release directory, which may predate the
// change. The remaining rules describe the change set itself and always count
// as introduced.
const RELEASE_STATE_RULES = new Set([
  'draft-card-changed',
  'release-readme-recommended',
  'custom-panels',
  'uf2-required',
  'pico-xosc64-recommended',
]);

/** The info.yaml files a change set validates: added, modified, or renamed under releases/. */
export function infoFilesToValidate(changes) {
  const selected = new Map();
  for (const change of changes) {
    if (!/^[ACMR]/.test(change.status)) continue;
    if (!/^releases\/(?:.+\/)?info\.yaml$/.test(change.path)) continue;
    selected.set(change.path, change);
  }
  return [...selected.values()];
}

// Line numbers shift with unrelated edits, so identity ignores position.
const identity = diagnostic => [
  diagnostic.severity, diagnostic.ruleId, diagnostic.file ?? '', diagnostic.path ?? '', diagnostic.message,
].join('\0');

/** Split diagnostics into those absent from the baseline and those already in it. */
function subtractBaseline(diagnostics, baselineDiagnostics) {
  const remaining = new Map();
  for (const diagnostic of baselineDiagnostics) {
    const key = identity(diagnostic);
    remaining.set(key, (remaining.get(key) || 0) + 1);
  }
  const introduced = [];
  const existing = [];
  for (const diagnostic of diagnostics) {
    const key = identity(diagnostic);
    if (remaining.get(key) > 0) {
      remaining.set(key, remaining.get(key) - 1);
      existing.push(diagnostic);
    } else {
      introduced.push(diagnostic);
    }
  }
  return { introduced, existing };
}

async function validateInfo(base, relative, displayPath, knownValues) {
  const file = path.join(base, relative);
  if (!fs.existsSync(file)) return null;
  const source = await parseSourceFile(file);
  source.file = displayPath;
  const customPanels = await readCustomPanelManifest(path.dirname(file));
  return validateInfoYaml(source, {
    customPanelsPresent: customPanels.present,
    panelIds: customPanels.items.map(item => item.id),
    knownValues,
    externalDiagnostics: customPanels.diagnostics.map(diagnostic => ({
      ...diagnostic,
      ruleId: 'custom-panel-manifest',
      key: 'panels',
    })),
  });
}

/**
 * Evaluate a change set.
 *
 * Options:
 *   root          directory holding the proposed tree (required)
 *   baselineRoot  directory holding the comparison tree; omit to treat every
 *                 diagnostic as introduced
 *   releasesDir   full releases/ directory indexing other cards' values for
 *                 the similar-values rule (defaults to root/releases)
 *   track         optional async (label, work) => result wrapper around each
 *                 unit of work, for progress reporting
 *
 * Returns { trigger, info: [{ file, result, introduced, existing }],
 *           rules: { diagnostics, introduced, existing } }, where `result` is
 * the full validateInfoYaml result for the proposed file.
 */
export async function evaluateChangeSet(changes, {
  root,
  baselineRoot = null,
  releasesDir = path.join(root, 'releases'),
  track = (label, work) => work(),
} = {}) {
  // Proposed and baseline runs share one index so the comparison is like-for-like.
  const knownValues = loadKnownValues(releasesDir);

  const info = [];
  for (const change of infoFilesToValidate(changes)) {
    const entry = await track(change.path, async () => {
      const result = await validateInfo(root, change.path, change.path, knownValues);
      if (!result) return null;
      const baseline = baselineRoot
        ? await validateInfo(baselineRoot, change.oldPath || change.path, change.path, knownValues)
        : null;
      return { file: change.path, result, ...subtractBaseline(result.diagnostics, baseline?.diagnostics || []) };
    });
    if (entry) info.push(entry);
  }

  const rules = await track('Submission rules', async () => {
    const diagnostics = await evaluatePrRules(changes, { root });
    const baseline = baselineRoot
      ? (await evaluatePrRules(changes, { root: baselineRoot })).filter(item => RELEASE_STATE_RULES.has(item.ruleId))
      : [];
    return { diagnostics, ...subtractBaseline(diagnostics, baseline) };
  });

  return { trigger: summarizePrTrigger(changes), info, rules };
}
