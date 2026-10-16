// Validate a NUL-delimited change list against a materialized Git-index
// snapshot. This file is executed from inside that snapshot.
//
// The change list covers the whole branch as CI will see it (the staged tree
// against the branch point on main), and the baseline directory holds that
// branch point. Only diagnostics the branch introduces are itemized and block
// the commit. Existing errors are still counted, because PR validation in CI
// fails on any error in a changed card, so the author learns that before
// pushing.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { evaluateChangeSet } from './changeSet.js';
import { reviewReasons, scopeReasons } from './mergeEligibility.js';
import { parseNameStatusZ } from './prRules.js';
import { color, printReport, step } from './stagedOutput.js';

const usage = 'Usage: stagedChangeSetCli.js CHANGES_FILE [--releases RELEASES_DIR] [--baseline BASELINE_DIR] [--base-label LABEL]';
const args = process.argv.slice(2);
const options = {};
let changesFile;
while (args.length) {
  const arg = args.shift();
  if (arg === '--releases' || arg === '--baseline' || arg === '--base-label') {
    if (!args.length) {
      console.error(usage);
      process.exit(2);
    }
    options[arg.slice(2)] = args.shift();
  } else if (!arg.startsWith('--') && !changesFile) {
    changesFile = arg;
  } else {
    console.error(usage);
    process.exit(2);
  }
}
if (!changesFile) {
  console.error(usage);
  process.exit(2);
}
const baseLabel = options['base-label'] || 'the base commit';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const changes = parseNameStatusZ(fs.readFileSync(changesFile));

const plural = (count, word) => `${count} ${word}${count === 1 ? '' : 's'}`;
const countBy = (diagnostics, severity) => diagnostics.filter(item => item.severity === severity).length;

function summarize(introduced, existing) {
  const parts = [];
  const errors = countBy(introduced, 'error');
  const warnings = countBy(introduced, 'warning');
  if (errors) parts.push(plural(errors, 'new error'));
  if (warnings) parts.push(plural(warnings, 'new warning'));
  if (!parts.length) parts.push('no new issues');
  const existingErrors = countBy(existing, 'error');
  const existingWarnings = countBy(existing, 'warning');
  if (existingErrors) parts.push(color.yellow(plural(existingErrors, 'existing error')));
  if (existingWarnings) parts.push(`${plural(existingWarnings, 'existing warning')} hidden`);
  return parts.join(', ');
}

async function track(label, work) {
  const progress = step(label);
  const outcome = await work();
  if (!outcome) progress.done('not in snapshot');
  else (countBy(outcome.introduced, 'error') ? progress.fail : progress.done)(summarize(outcome.introduced, outcome.existing));
  return outcome;
}

const report = await evaluateChangeSet(changes, {
  root,
  baselineRoot: options.baseline,
  // The snapshot only holds the changed cards; index the rest from the repo.
  releasesDir: options.releases,
  track,
});

const sections = [];
const existingIn = [];
let introducedAll = [];
let existingAll = [];
for (const entry of report.info) {
  if (entry.existing.length) existingIn.push(path.posix.dirname(entry.file));
  existingAll = existingAll.concat(entry.existing.map(item => ({ ...item, file: entry.file })));
  if (entry.introduced.length) sections.push({ title: entry.file, diagnostics: entry.introduced });
  introducedAll = introducedAll.concat(entry.introduced.map(item => ({ ...item, file: entry.file })));
}
if (report.rules.introduced.length) sections.push({ title: 'Submission rules', diagnostics: report.rules.introduced });
introducedAll = introducedAll.concat(report.rules.introduced);
existingAll = existingAll.concat(report.rules.existing);

function formatDiagnostic(diagnostic, showFile) {
  const tag = diagnostic.severity === 'error' ? color.red('error') : color.yellow('warning');
  const where = diagnostic.line == null ? '' : diagnostic.col == null ? `:${diagnostic.line}` : `:${diagnostic.line}:${diagnostic.col}`;
  const field = diagnostic.path ? ` [${diagnostic.path}]` : '';
  const file = showFile && diagnostic.file ? ` ${diagnostic.file}:` : '';
  const lines = [`  ${tag}${where}${field}${file} ${diagnostic.message} ${color.dim(`(${diagnostic.ruleId})`)}`];
  if (diagnostic.suggestion) lines.push(`    hint: ${diagnostic.suggestion}`);
  return lines;
}

const body = [];
for (const section of sections) {
  if (body.length) body.push('');
  body.push(color.bold(section.title));
  const showFile = section.title === 'Submission rules';
  for (const diagnostic of section.diagnostics) body.push(...formatDiagnostic(diagnostic, showFile));
}

const errors = countBy(introducedAll, 'error');
const warnings = countBy(introducedAll, 'warning');
const firstError = introducedAll.find(item => item.severity === 'error');
const existingErrors = existingAll.filter(item => item.severity === 'error');
const existingErrorFiles = [...new Set(existingErrors.map(item => item.file))];
const ciWarning = existingErrors.length
  ? `CI will still reject the PR: ${existingErrorFiles.join(', ')} already ${existingErrorFiles.length === 1 ? 'has' : 'have'} ${plural(existingErrors.length, 'error')}.`
  : '';

let headline;
if (errors) {
  const where = firstError.path ? `${firstError.file} [${firstError.path}]` : firstError.file;
  headline = `${color.red('✗ Commit blocked:')} ${plural(errors, 'new error')}${warnings ? `, ${plural(warnings, 'new warning')}` : ''}. First: ${where}: ${firstError.message}`;
} else if (existingErrors.length) {
  headline = `${color.yellow('⚠ Commit allowed, but')} ${ciWarning}${warnings ? ` Also ${plural(warnings, 'new warning')}.` : ''}`;
} else if (warnings) {
  headline = `${color.yellow('⚠')} Program card checks passed with ${plural(warnings, 'new warning')}.`;
} else {
  headline = `${color.green('✓')} Program card checks passed.`;
}

const footer = [];
if (errors && ciWarning) footer.push(color.yellow(ciWarning));
// The author is unknown locally, so only the branch's own content is assessed.
const manualReview = [...scopeReasons(changes), ...reviewReasons(introducedAll)];
if (manualReview.length) footer.push(color.dim(`Won't merge automatically: ${manualReview.join(' ')}`));
if (existingAll.length) {
  const targets = [...new Set(existingIn)];
  footer.push(color.dim(`Issues that already existed on ${baseLabel} are not listed${targets.length ? `; see them with: npm run validate-info -- ${targets.join(' ')}` : '.'}`));
}
if (errors) {
  footer.push('Fix the errors above, or bypass with git commit --no-verify (CI still validates the PR).');
}

printReport({ headline, body, footer });
process.exit(errors ? 1 : 0);
