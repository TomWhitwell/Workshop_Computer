// Validate a NUL-delimited staged change list against a materialized Git-index
// snapshot. This file is executed from inside that snapshot.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';
import { readCustomPanelManifest } from '../discover/customPanels.js';
import { evaluatePrRules, parseNameStatusZ, summarizePrTrigger } from './prRules.js';
import { reportText } from './reporters/index.js';

const changesFile = process.argv[2];
if (!changesFile) {
  console.error('Usage: stagedChangeSetCli.js CHANGES_FILE');
  process.exit(2);
}

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const changes = parseNameStatusZ(fs.readFileSync(changesFile));
const infoFiles = [...new Set(changes
  .filter(change => !change.status.startsWith('D') && /(?:^|\/)info\.yaml$/i.test(change.path))
  .map(change => change.path))];

const results = [];
for (const relative of infoFiles) {
  const file = path.join(root, relative);
  if (!fs.existsSync(file)) continue;
  const source = await parseSourceFile(file);
  source.file = relative;
  const customPanels = await readCustomPanelManifest(path.dirname(file));
  results.push(validateInfoYaml(source, {
    customPanelsPresent: customPanels.present,
    panelIds: customPanels.items.map(item => item.id),
    externalDiagnostics: customPanels.diagnostics.map(diagnostic => ({
      ...diagnostic,
      ruleId: 'custom-panel-manifest',
      key: 'panels',
    })),
  }));
}

const ruleDiagnostics = await evaluatePrRules(changes, { root });
const trigger = summarizePrTrigger(changes);
const infoErrors = results.reduce((count, result) => count + result.errorCount, 0);
const infoWarnings = results.reduce((count, result) => count + result.warningCount, 0);
const ruleErrors = ruleDiagnostics.filter(item => item.severity === 'error').length;
const ruleWarnings = ruleDiagnostics.filter(item => item.severity === 'warning').length;

const errors = infoErrors + ruleErrors;
const warnings = infoWarnings + ruleWarnings;
const icon = errors ? '❌' : warnings ? '⚠️' : '✅';
const status = errors ? 'failed' : warnings ? 'succeeded with warnings' : 'succeeded';
console.log(`${icon} Program card commit validation ${status}`);
console.log(`Staged changes: ${trigger.changedPaths.length}; info.yaml files: ${results.length}; errors: ${errors}; warnings: ${warnings}.`);

if (trigger.changedPaths.length) {
  console.log('\nTriggered by:');
  for (const change of trigger.changedPaths) {
    console.log(`  ${change.status} ${change.oldPath ? `${change.oldPath} -> ` : ''}${change.path}`);
  }
}
if (results.some(result => result.diagnostics.length)) {
  console.log('\ninfo.yaml validation:');
  console.log(reportText(results));
}
if (ruleDiagnostics.length) {
  console.log('\nOther rules:');
  for (const diagnostic of ruleDiagnostics) {
    console.log(`  ${diagnostic.severity} [${diagnostic.ruleId}] ${diagnostic.file}: ${diagnostic.message}`);
  }
}
if (!errors && !warnings) console.log('\nAll staged program card checks passed.');
if (errors) {
  console.error('\nCommit blocked. Fix the errors above, or use git commit --no-verify to bypass locally. CI will still validate the PR.');
}
process.exit(errors ? 1 : 0);
