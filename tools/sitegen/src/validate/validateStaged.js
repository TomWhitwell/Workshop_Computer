// Materialize the exact Git index state for affected releases, then run the
// staged copy of the card validator. Unstaged working-tree edits are excluded.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const sourceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

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

function treeContains(tree, relative) {
  return spawnSync('git', ['cat-file', '-e', `${tree}:${relative}`], {
    cwd: sourceRoot, stdio: 'ignore',
  }).status === 0;
}

function archive(tree, paths, destination) {
  return new Promise((resolve, reject) => {
    const gitArchive = spawn('git', ['archive', '--format=tar', tree, '--', ...paths], {
      cwd: sourceRoot, stdio: ['ignore', 'pipe', 'pipe'],
    });
    const tar = spawn('tar', ['-xf', '-', '-C', destination], { stdio: ['pipe', 'ignore', 'pipe'] });
    gitArchive.stdout.pipe(tar.stdin);
    let errors = '';
    gitArchive.stderr.on('data', chunk => { errors += chunk; });
    tar.stderr.on('data', chunk => { errors += chunk; });
    let gitStatus;
    let tarStatus;
    const finish = () => {
      if (gitStatus === undefined || tarStatus === undefined) return;
      if (gitStatus === 0 && tarStatus === 0) resolve();
      else reject(new Error(errors.trim() || 'Could not materialize the staged snapshot.'));
    };
    gitArchive.on('close', code => { gitStatus = code; finish(); });
    tar.on('close', code => { tarStatus = code; finish(); });
  });
}

let temporary;
try {
  const dependencyDir = path.join(sourceRoot, 'tools', 'sitegen', 'node_modules');
  if (!fs.existsSync(dependencyDir)) {
    throw new Error('Validator dependencies are missing. Run: npm ci --prefix tools/sitegen');
  }

  const changes = spawnSync('git', ['diff', '--cached', '--name-status', '--find-renames=50%', '-z'], {
    cwd: sourceRoot, encoding: null, maxBuffer: 64 * 1024 * 1024,
  });
  if (changes.status !== 0) throw new Error(changes.stderr?.toString().trim() || 'Could not inspect staged changes.');
  if (!changes.stdout.length) {
    console.log('No staged changes to validate.');
    process.exit(0);
  }
  const releases = affectedReleases(changes.stdout);
  if (!releases.length) {
    console.log('No staged program card changes; validation skipped.');
    process.exit(0);
  }

  const tree = git(['write-tree']).trim();
  temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-card-staged-'));
  const snapshot = path.join(temporary, 'snapshot');
  fs.mkdirSync(snapshot);
  const paths = ['tools/sitegen/src', 'tools/sitegen/package.json'];
  for (const release of releases) {
    const relative = `releases/${release}`;
    if (treeContains(tree, relative)) paths.push(relative);
  }
  await archive(tree, paths, snapshot);
  fs.symlinkSync(dependencyDir, path.join(snapshot, 'tools', 'sitegen', 'node_modules'), 'dir');
  const changesFile = path.join(temporary, 'changes.bin');
  fs.writeFileSync(changesFile, changes.stdout);

  const runner = spawnSync(process.execPath, [
    path.join(snapshot, 'tools', 'sitegen', 'src', 'validate', 'stagedChangeSetCli.js'),
    changesFile,
  ], { cwd: snapshot, stdio: 'inherit' });
  process.exitCode = runner.status ?? 2;
} catch (error) {
  console.error(`Pre-commit validation could not run: ${error.message}`);
  process.exitCode = 2;
} finally {
  if (temporary) fs.rmSync(temporary, { recursive: true, force: true });
}
