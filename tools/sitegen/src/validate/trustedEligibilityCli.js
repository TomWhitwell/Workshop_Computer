// Decide a pull request's auto-merge eligibility from base-branch code.
// Usage: node trustedEligibilityCli.js [VERDICT.json]
//
// Run from a checkout of the base branch that also holds the PR head commit
// as a Git object (never a checkout of the PR). Writes the verdict as JSON,
// `verdict=eligible|ineligible` to $GITHUB_OUTPUT, and a Markdown summary to
// $GITHUB_STEP_SUMMARY, when those are set. Exits 0 with a
// verdict either way; a non-zero exit means no verdict could be reached.
//
// Environment:
//   BASE_SHA, HEAD_SHA     the PR's base branch commit and head commit
//   PR_AUTHOR              PR author login
//   PR_AUTHOR_ASSOCIATION  PR author_association
//   PR_DRAFT               "true" for draft PRs
//   PR_LABELS              JSON array of the PR's label names
//   AUTO_MERGE             "true" once auto-merge is on (summary wording)
//   GITHUB_REPOSITORY, GITHUB_TOKEN   card committer lookup

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { assessPullRequest } from './trustedEligibility.js';
import { parseLabels } from './mergeEligibility.js';
import { reportEligibilityMarkdown } from './reporters/index.js';

const verdictFile = process.argv[2];
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const { BASE_SHA: base, HEAD_SHA: head, PR_AUTHOR: author } = process.env;
if (!base || !head || !author) {
  console.error('BASE_SHA, HEAD_SHA and PR_AUTHOR are required.');
  process.exit(2);
}

const { eligibility, errorCount } = await assessPullRequest({
  cwd: root,
  base,
  head,
  author,
  association: process.env.PR_AUTHOR_ASSOCIATION || 'NONE',
  draft: process.env.PR_DRAFT === 'true',
  labels: parseLabels(process.env.PR_LABELS),
  repo: process.env.GITHUB_REPOSITORY || '',
  token: process.env.GITHUB_TOKEN || '',
});

const verdict = { base, head, author, errorCount, ...eligibility };
console.log(`Auto-merge eligibility (trusted): ${eligibility.eligible ? 'eligible' : 'not eligible'} at ${head}`);
for (const line of eligibility.eligible ? [eligibility.basis] : eligibility.reasons) console.log(`  ${line}`);

if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, `verdict=${eligibility.eligible ? 'eligible' : 'ineligible'}\n`);
if (verdictFile) fs.writeFileSync(verdictFile, `${JSON.stringify(verdict, null, 2)}\n`);
if (process.env.GITHUB_STEP_SUMMARY) {
  fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, `_Trusted verdict for \`${head}\`, decided by base-branch code._\n\n${reportEligibilityMarkdown(eligibility, { autoMerge: process.env.AUTO_MERGE === 'true' })}`);
}
