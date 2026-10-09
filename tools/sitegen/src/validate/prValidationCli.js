// Validate a pull request's card changes for GitHub Actions.
// Usage: git diff --name-status -z BASE...HEAD | node prValidationCli.js [SUMMARY.md]
//
// Prints GitHub annotations and a text report, writes the Markdown PR report
// to SUMMARY.md, and exits 1 when any validated file or submission rule has
// an error. A PR that touches no program card is skipped: it succeeds and
// writes no report.
//
// Environment:
//   BASE_SHA               base commit; separates issues the PR introduces
//                          from existing ones, and enables the auto-merge
//                          eligibility report
//   PR_AUTHOR              PR author login            (eligibility report)
//   PR_AUTHOR_ASSOCIATION  PR author_association      (eligibility report)
//   PR_DRAFT               "true" for draft PRs       (eligibility report)
//   GITHUB_REPOSITORY, GITHUB_TOKEN                   (card committer lookup)

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { evaluateChangeSet } from './changeSet.js';
import { hasCommittedToCard } from './cardCommitters.js';
import { archiveTree, treeContains } from './gitTree.js';
import { cardScope, evaluateMergeEligibility, touchesNoCards } from './mergeEligibility.js';
import { parseNameStatusZ } from './prRules.js';
import {
  reportEligibilityMarkdown, reportGithub, reportMarkdown, reportOtherRulesGithub, reportText,
} from './reporters/index.js';

const summaryFile = process.argv[2];
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

const changes = parseNameStatusZ(fs.readFileSync(0));
if (touchesNoCards(changes)) {
  console.log('No program card changes; card validation skipped.');
  process.exit(0);
}
// Extract the touched cards as they are on the base branch, so the report can
// tell which issues this PR introduces. Blocking still counts every error.
const base = process.env.BASE_SHA;
const { cards } = cardScope(changes);
let baselineRoot = null;
if (base) {
  baselineRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-pr-base-'));
  process.on('exit', () => fs.rmSync(baselineRoot, { recursive: true, force: true }));
  const paths = cards.map(card => `releases/${card}`).filter(relative => treeContains(root, base, relative));
  if (paths.length) await archiveTree(root, base, paths, baselineRoot);
}

const report = await evaluateChangeSet(changes, { root, baselineRoot });
const results = report.info.map(entry => entry.result);
const ruleDiagnostics = report.rules.diagnostics;
const otherRules = {
  trigger: report.trigger,
  diagnostics: ruleDiagnostics,
  errorCount: ruleDiagnostics.filter(item => item.severity === 'error').length,
  warningCount: ruleDiagnostics.filter(item => item.severity === 'warning').length,
};

if (results.length) console.log(`Validated ${results.map(result => result.file).join(', ')}`);
console.log(reportGithub(results));
const ruleAnnotations = reportOtherRulesGithub(otherRules);
if (ruleAnnotations) console.log(ruleAnnotations);
if (results.length) console.log(reportText(results));
const errorCount = results.reduce((count, result) => count + result.errorCount, 0) + otherRules.errorCount;

// Report-only: show whether this PR would be merged without review.
let eligibility = null;
if (base && process.env.PR_AUTHOR) {
  const card = cards.length === 1 ? cards[0] : null;
  const cardOnBase = card ? treeContains(root, base, `releases/${card}`) : false;
  const authorCommitted = card && cardOnBase && process.env.GITHUB_REPOSITORY
    ? await hasCommittedToCard({
      repo: process.env.GITHUB_REPOSITORY, card, ref: base, author: process.env.PR_AUTHOR, token: process.env.GITHUB_TOKEN,
    })
    : null;
  eligibility = evaluateMergeEligibility({
    changes,
    author: process.env.PR_AUTHOR,
    association: process.env.PR_AUTHOR_ASSOCIATION || 'NONE',
    draft: process.env.PR_DRAFT === 'true',
    errorCount,
    introduced: [...report.info.flatMap(entry => entry.introduced), ...report.rules.introduced],
    cardOnBase,
    cardInHead: card ? fs.existsSync(path.join(root, 'releases', card)) : false,
    authorCommitted,
  });
  console.log(`Auto-merge eligibility (report only): ${eligibility.eligible ? 'eligible' : 'not eligible'}`);
  for (const line of eligibility.eligible ? [eligibility.basis] : eligibility.reasons) console.log(`  ${line}`);
}

if (summaryFile) {
  const sections = [reportMarkdown(results, otherRules)];
  if (eligibility) sections.push(reportEligibilityMarkdown(eligibility));
  fs.writeFileSync(summaryFile, sections.join('\n'));
}

process.exit(errorCount ? 1 : 0);
