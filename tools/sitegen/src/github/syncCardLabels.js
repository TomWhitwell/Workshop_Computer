import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { releaseCards } from '../curation/cli.js';
import {
  NEW_CARD_LABEL_COLOR,
  UPSTREAM_REPO,
  cardIssueLabelsFromCards,
} from '../render/githubIssue.js';

const DEFAULT_DESCRIPTION = 'Program card';

export function missingCardIssueLabels(desired, existing) {
  const have = new Set((existing || []).map(name => String(name)));
  return (desired || []).filter(label => !have.has(label));
}

function gh(args, { token } = {}) {
  const env = { ...process.env };
  if (token) {
    env.GH_TOKEN = token;
    env.GITHUB_TOKEN = token;
  }
  const result = spawnSync('gh', args, { encoding: 'utf8', env });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    const detail = (result.stderr || result.stdout || '').trim() || `gh exited ${result.status}`;
    throw new Error(detail);
  }
  return result.stdout;
}

function listExistingLabels(repo, token) {
  const stdout = gh(['label', 'list', '--repo', repo, '--json', 'name', '--limit', '1000'], { token });
  const parsed = JSON.parse(stdout || '[]');
  return parsed.map(entry => entry.name).filter(Boolean);
}

function createLabel(repo, name, token) {
  gh([
    'label', 'create', name,
    '--repo', repo,
    '--color', NEW_CARD_LABEL_COLOR,
    '--description', DEFAULT_DESCRIPTION,
  ], { token });
}

export function syncCardIssueLabels({
  cards = releaseCards(),
  repo = process.env.GH_REPO || UPSTREAM_REPO,
  token = process.env.GH_TOKEN || process.env.GITHUB_TOKEN || '',
  dryRun = process.argv.includes('--dry-run') || !token,
  log = console,
} = {}) {
  const desired = cardIssueLabelsFromCards(cards);
  if (dryRun) {
    log.log(`Would ensure ${desired.length} program-card label(s) on ${repo} (dry run).`);
    for (const label of desired) log.log(`  ${label}`);
    return { desired, created: [], skipped: desired, dryRun: true };
  }

  const existing = listExistingLabels(repo, token);
  const missing = missingCardIssueLabels(desired, existing);
  const created = [];
  for (const label of missing) {
    try {
      createLabel(repo, label, token);
      created.push(label);
      log.log(`Created label: ${label}`);
    } catch (error) {
      if (/already exists/i.test(error.message)) continue;
      throw new Error(`Failed to create label "${label}": ${error.message}`);
    }
  }
  if (!created.length) log.log(`Program-card labels are already synchronized (${desired.length} labels).`);
  else log.log(`Created ${created.length} program-card label(s).`);
  return { desired, created, skipped: missing.filter(label => !created.includes(label)), dryRun: false };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    syncCardIssueLabels();
  } catch (error) {
    console.error(`error: ${error.message}`);
    process.exitCode = 1;
  }
}
