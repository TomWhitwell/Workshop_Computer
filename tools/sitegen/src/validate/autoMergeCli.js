// Choose the pull requests the auto-merge job should merge now.
// Usage: node autoMergeCli.js
//
// Run from a full checkout of main. For each open PR it fetches the head as
// Git objects, recomputes the trusted verdict with this checkout's code, and
// times the grace period from trusted verdict artifacts (see autoMerge.js).
// Writes `candidates=[{ number, sha, reason }]` to $GITHUB_OUTPUT and a table
// to $GITHUB_STEP_SUMMARY. Holds no write access: auto-merge.yml merges the
// candidates in a separate job.
//
// Environment:
//   GITHUB_REPOSITORY, GITHUB_TOKEN   read-only API access
//   GITHUB_API_URL                    defaults to https://api.github.com
//   GITHUB_SERVER_URL                 defaults to https://github.com

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { assessPullRequest } from './trustedEligibility.js';
import {
  VALIDATION_CHECK, decideAutoMerge, isTrustedVerdictRun, parseVerdictArtifact, prGateReason,
} from './autoMerge.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const repo = process.env.GITHUB_REPOSITORY;
const token = process.env.GITHUB_TOKEN || '';
const apiBase = process.env.GITHUB_API_URL || 'https://api.github.com';
// PR heads live on the repository itself, which a local clone's origin may not be.
const remote = `${process.env.GITHUB_SERVER_URL || 'https://github.com'}/${repo}.git`;
if (!repo) {
  console.error('GITHUB_REPOSITORY is required.');
  process.exit(2);
}

async function api(route) {
  const headers = { accept: 'application/vnd.github+json', 'x-github-api-version': '2022-11-28' };
  if (token) headers.authorization = `Bearer ${token}`;
  const response = await fetch(`${apiBase}${route}`, { headers });
  if (!response.ok) throw new Error(`GET ${route}: ${response.status}`);
  return { body: await response.json(), next: /<([^>]+)>;\s*rel="next"/.exec(response.headers.get('link') || '')?.[1] };
}

/** Every item of a paginated list; `key` names the array in wrapped responses. */
async function all(route, key = null, maxPages = 10) {
  const items = [];
  let url = route;
  for (let page = 0; url && page < maxPages; page += 1) {
    const { body, next } = await api(url);
    items.push(...(key ? body[key] : body));
    url = next ? next.slice(apiBase.length) : null;
  }
  return items;
}

const git = (...args) => spawnSync('git', args, { cwd: root, encoding: 'utf8' });
const mainSha = git('rev-parse', 'HEAD').stdout.trim();

// Verdict artifacts, newest first. Listing stops after a few pages; dropping
// the oldest can only shorten a commit's run of verdicts, delaying a merge.
const runs = new Map();
async function trustedRun(id) {
  if (!runs.has(id)) runs.set(id, isTrustedVerdictRun((await api(`/repos/${repo}/actions/runs/${id}`)).body));
  return runs.get(id);
}
const verdictsByPr = new Map();
for (const artifact of await all(`/repos/${repo}/actions/artifacts?per_page=100`, 'artifacts')) {
  const verdict = parseVerdictArtifact(artifact.name);
  if (!verdict) continue;
  const list = verdictsByPr.get(verdict.number) || [];
  list.push({ ...verdict, created_at: artifact.created_at, runId: artifact.workflow_run?.id });
  verdictsByPr.set(verdict.number, list);
}

const candidates = [];
const rows = [];
for (const pr of await all(`/repos/${repo}/pulls?state=open&base=main&per_page=100`)) {
  const record = reason => rows.push(`| #${pr.number} | ${pr.head.sha.slice(0, 7)} | ${String(reason).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
  try {
    const gate = prGateReason(pr);
    if (gate) { record(gate); continue; }

    const verdicts = [];
    for (const verdict of verdictsByPr.get(pr.number) || []) {
      if (verdict.runId && await trustedRun(verdict.runId)) verdicts.push(verdict);
    }

    if (git('fetch', '--no-tags', '--no-recurse-submodules', remote, `refs/pull/${pr.number}/head`).status !== 0
      || git('cat-file', '-e', `${pr.head.sha}^{commit}`).status !== 0) {
      record('Could not fetch the head commit.');
      continue;
    }
    const { eligibility } = await assessPullRequest({
      cwd: root,
      base: mainSha,
      head: pr.head.sha,
      author: pr.user.login,
      association: pr.author_association,
      draft: pr.draft,
      labels: (pr.labels || []).map(label => label.name),
      repo,
      token,
    });

    // filter=latest: the most recent Validate run is the one that counts.
    const validation = (await api(`/repos/${repo}/commits/${pr.head.sha}/check-runs?${new URLSearchParams({
      check_name: VALIDATION_CHECK, filter: 'latest', per_page: '100',
    })}`)).body.check_runs
      .filter(run => run.app?.slug === 'github-actions')
      .sort((a, b) => Date.parse(b.started_at) - Date.parse(a.started_at))[0] || null;

    const decision = decideAutoMerge({ pr, eligibility, verdicts, validation, now: Date.now() });
    record(`${decision.merge ? '✅ ' : ''}${decision.reason}`);
    if (decision.merge) candidates.push({ number: pr.number, sha: pr.head.sha, reason: decision.reason });
  } catch (error) {
    record(`Skipped after an error: ${error.message}`);
  }
}

console.log(rows.join('\n') || 'No open PRs.');
if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, `candidates=${JSON.stringify(candidates)}\n`);
if (process.env.GITHUB_STEP_SUMMARY) {
  fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, ['## Auto-merge candidates', '', '| PR | Head | Decision |', '|:--|:--|:--|', ...rows, ''].join('\n'));
}
