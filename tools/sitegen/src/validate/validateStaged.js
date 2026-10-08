// Materialize the exact Git index state for affected releases, plus the
// branch point on main as a baseline, then run the staged copy of the card
// validator. Like PR validation in CI, the whole branch is evaluated (staged
// tree against the branch point), so the hook reports what the PR will; issues
// already present on main are hidden. Unstaged working-tree edits are
// excluded. If the installed validator dependencies no longer match
// package-lock.json (e.g. after pulling a Dependabot update), they are
// reinstalled with `npm ci` first.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { archiveTree, treeContains } from './gitTree.js';
import { color, interactive, step } from './stagedOutput.js';

const sourceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const sitegenDir = path.join(sourceRoot, 'tools', 'sitegen');
const dependencyDir = path.join(sitegenDir, 'node_modules');

function git(args, options = {}) {
  const result = spawnSync('git', args, { cwd: sourceRoot, encoding: 'utf8', ...options });
  if (result.status !== 0) throw new Error(result.stderr?.trim() || `git ${args.join(' ')} failed`);
  return result.stdout;
}

function fieldsFrom(buffer) {
  const fields = buffer.toString('utf8').split('\0');
  if (fields.at(-1) === '') fields.pop();
  return fields;
}

function affectedReleases(changeBuffer) {
  const fields = fieldsFrom(changeBuffer);
  const releases = new Set();
  for (let index = 0; index < fields.length;) {
    const status = fields[index++];
    const paths = /^[RC]/.test(status)
      ? [fields[index++], fields[index++]]
      : [fields[index++]];
    for (const value of paths) {
      const parts = String(value).replaceAll('\\', '/').split('/');
      if (parts[0] === 'releases' && parts.length >= 3) releases.add(parts[1]);
    }
  }
  return [...releases].sort();
}

/**
 * Why node_modules does not match package-lock.json, or null when it does.
 * npm records what it installed in node_modules/.package-lock.json; optional
 * packages for other platforms appear only in the lockfile and are ignored.
 */
function dependencyDrift() {
  if (!fs.existsSync(dependencyDir)) return 'not installed';
  let locked;
  let installed;
  try {
    locked = JSON.parse(fs.readFileSync(path.join(sitegenDir, 'package-lock.json'), 'utf8')).packages || {};
  } catch {
    return null; // Nothing to compare against; let npm report problems itself.
  }
  try {
    installed = JSON.parse(fs.readFileSync(path.join(dependencyDir, '.package-lock.json'), 'utf8')).packages || {};
  } catch {
    return 'install record missing';
  }
  for (const name of new Set([...Object.keys(locked), ...Object.keys(installed)])) {
    if (!name) continue;
    if (!installed[name] && locked[name]?.optional) continue;
    if (locked[name]?.version !== installed[name]?.version) {
      return 'package-lock.json changed since the last install';
    }
  }
  return null;
}

function installDependencies(reason) {
  const progress = step(`Installing validator dependencies (${reason})`);
  const result = spawnSync('npm', ['ci', '--no-audit', '--no-fund'], {
    cwd: sitegenDir, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
  });
  // npm can crash ("Exit handler never called!") yet exit 0 with a partial
  // install, so trust the install record rather than the exit status.
  if (result.status !== 0 || dependencyDrift()) {
    progress.fail();
    const output = `${result.stdout || ''}${result.stderr || ''}`.trim().split('\n')
      .filter(line => !line.startsWith('npm warn')).slice(-15).join('\n');
    throw new Error(`npm ci failed${result.error ? ` (${result.error.message})` : ''}. Run it manually: npm ci --prefix tools/sitegen${output ? `\n${output}` : ''}`);
  }
  progress.done();
}

// Candidate refs for the main branch a PR will target, most authoritative
// first. `git config workshop.baseRef <ref>` overrides them.
const BASE_REF_CANDIDATES = ['upstream/main', 'origin/main', 'main'];

function revParse(ref) {
  const result = spawnSync('git', ['rev-parse', '--verify', '-q', ref], { cwd: sourceRoot, encoding: 'utf8' });
  return result.status === 0 ? result.stdout.trim() : null;
}

/**
 * The commit a PR from this branch will be compared against: the merge base
 * with the most up-to-date available main. Falls back to HEAD, or null for an
 * initial commit.
 */
function findBase() {
  const head = revParse('HEAD^{commit}');
  if (!head) return null;
  const configured = spawnSync('git', ['config', '--get', 'workshop.baseRef'], { cwd: sourceRoot, encoding: 'utf8' });
  const refs = configured.status === 0 && configured.stdout.trim() ? [configured.stdout.trim()] : BASE_REF_CANDIDATES;
  let best = null;
  for (const ref of refs) {
    if (!revParse(`${ref}^{commit}`)) continue;
    const mergeBase = spawnSync('git', ['merge-base', 'HEAD', ref], { cwd: sourceRoot, encoding: 'utf8' });
    if (mergeBase.status !== 0) continue;
    const commit = mergeBase.stdout.trim();
    const distance = Number(git(['rev-list', '--count', `${commit}..HEAD`]).trim());
    if (!best || distance < best.distance) best = { ref, commit, distance };
  }
  return best || { ref: 'HEAD', commit: head, distance: 0 };
}

function nameStatus(args) {
  const result = spawnSync('git', ['diff', '--cached', '--name-status', '--find-renames=50%', '-z', ...args], {
    cwd: sourceRoot, encoding: null, maxBuffer: 64 * 1024 * 1024,
  });
  if (result.status !== 0) throw new Error(result.stderr?.toString().trim() || 'Could not inspect staged changes.');
  return result.stdout;
}

let temporary;
let snapshotStep;
try {
  // Only commits touching cards are validated...
  const staged = nameStatus([]);
  if (!staged.length) {
    console.log('No staged changes to validate.');
    process.exit(0);
  }
  if (!affectedReleases(staged).length) {
    console.log('No staged program card changes; validation skipped.');
    process.exit(0);
  }
  // ...but against everything the branch changes, as CI will see it.
  const base = findBase();
  const changes = base ? nameStatus([base.commit]) : staged;
  const releases = affectedReleases(changes);

  if (interactive) console.log(color.bold('Checking staged program cards'));
  const drift = dependencyDrift();
  if (drift) installDependencies(drift);
  snapshotStep = step(`Snapshotting ${releases.length} release${releases.length === 1 ? '' : 's'}`);
  const tree = git(['write-tree']).trim();
  const baseTree = base ? revParse(`${base.commit}^{tree}`) : null;
  temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-card-staged-'));
  const snapshot = path.join(temporary, 'snapshot');
  fs.mkdirSync(snapshot);
  const releasePaths = treeish => releases
    .map(release => `releases/${release}`)
    .filter(relative => treeContains(sourceRoot, treeish, relative));
  await archiveTree(sourceRoot, tree, ['tools/sitegen/src', 'tools/sitegen/package.json', ...releasePaths(tree)], snapshot);
  fs.symlinkSync(dependencyDir, path.join(snapshot, 'tools', 'sitegen', 'node_modules'), 'dir');
  let baseline = null;
  if (baseTree) {
    baseline = path.join(temporary, 'baseline');
    fs.mkdirSync(baseline);
    const paths = releasePaths(baseTree);
    if (paths.length) await archiveTree(sourceRoot, baseTree, paths, baseline);
  }
  const changesFile = path.join(temporary, 'changes.bin');
  fs.writeFileSync(changesFile, changes);
  // A configured base may be a raw commit id; keep labels readable.
  const baseLabel = base && (/^[0-9a-f]{40}$/i.test(base.ref) ? base.ref.slice(0, 8) : base.ref);
  snapshotStep.done(!base
    ? 'staged; nothing to compare against'
    : base.ref === 'HEAD' || baseLabel === base.commit.slice(0, 8)
      ? `staged vs ${baseLabel}`
      : `staged vs ${baseLabel} branch point (${base.commit.slice(0, 8)})`);

  const runner = spawnSync(process.execPath, [
    path.join(snapshot, 'tools', 'sitegen', 'src', 'validate', 'stagedChangeSetCli.js'),
    changesFile,
    // The snapshot only holds the changed cards; index the rest from the repo.
    '--releases', path.join(sourceRoot, 'releases'),
    ...(baseline ? ['--baseline', baseline, '--base-label', baseLabel] : []),
  ], { cwd: snapshot, stdio: 'inherit' });
  process.exitCode = runner.status ?? 2;
} catch (error) {
  snapshotStep?.fail();
  console.error(`${color.red('Pre-commit validation could not run:')} ${error.message}`);
  process.exitCode = 2;
} finally {
  if (temporary) fs.rmSync(temporary, { recursive: true, force: true });
}
