// Evaluate card-submission rules from a NUL-delimited Git name-status diff.
// Usage: git diff --name-status -z BASE...HEAD | node prRulesCli.js OUTPUT.json

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { evaluatePrRules, parseNameStatusZ, summarizePrTrigger } from './prRules.js';

const output = process.argv[2];
if (!output) {
  console.error('Usage: prRulesCli.js OUTPUT.json');
  process.exit(2);
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const input = fs.readFileSync(0);
const changes = parseNameStatusZ(input);
const diagnostics = await evaluatePrRules(changes, { root });
const trigger = summarizePrTrigger(changes);
const errorCount = diagnostics.filter(item => item.severity === 'error').length;
const warningCount = diagnostics.filter(item => item.severity === 'warning').length;
fs.writeFileSync(output, JSON.stringify({ trigger, diagnostics, errorCount, warningCount }, null, 2));
process.exit(errorCount ? 1 : 0);
