// Evaluate card-submission rules from a NUL-delimited Git name-status diff.
// Usage: git diff --name-status -z BASE...HEAD | node prRulesCli.js OUTPUT.json

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';
import { evaluatePrRules, parseNameStatusZ, summarizePrTrigger } from './prRules.js';

const output = process.argv[2];
if (!output) {
  console.error('Usage: prRulesCli.js OUTPUT.json');
  process.exit(2);
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const input = fs.readFileSync(0);
const changes = parseNameStatusZ(input);
let baseFlairs = null;
if (process.env.BASE_SHA && changes.some(change =>
  change.path === 'tools/sitegen/src/curation/flairs.yml'
  || change.oldPath === 'tools/sitegen/src/curation/flairs.yml'
)) {
  try {
    const source = execFileSync('git', [
      'show', `${process.env.BASE_SHA}:tools/sitegen/src/curation/flairs.yml`,
    ], { cwd: root, encoding: 'utf8' });
    baseFlairs = YAML.parse(source) || {};
  } catch {}
}
const diagnostics = await evaluatePrRules(changes, { root, baseFlairs });
const trigger = summarizePrTrigger(changes);
const errorCount = diagnostics.filter(item => item.severity === 'error').length;
const warningCount = diagnostics.filter(item => item.severity === 'warning').length;
fs.writeFileSync(output, JSON.stringify({ trigger, diagnostics, errorCount, warningCount }, null, 2));
process.exit(errorCount ? 1 : 0);
