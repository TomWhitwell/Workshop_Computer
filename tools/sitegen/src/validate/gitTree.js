// Materialize parts of a Git tree on disk without touching the working tree,
// for validating a staged snapshot or a PR's base commit.

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const REGULAR_MODES = new Set(['100644', '100755']);

function git(cwd, args, options = {}) {
  const result = spawnSync('git', args, { cwd, maxBuffer: 1 << 30, ...options });
  if (result.status !== 0) {
    throw new Error(`git ${args[0]} failed: ${String(result.stderr || '').trim() || `exit ${result.status}`}`);
  }
  return result.stdout;
}

/**
 * Entries under `paths` in `tree`, as { files, special }: `files` are regular
 * blobs ({ mode, object, size, path }), `special` are symlinks and submodules
 * ({ mode, path }). Paths are repository-relative.
 */
export function listTree(cwd, tree, paths) {
  const files = [];
  const special = [];
  const output = git(cwd, ['ls-tree', '-r', '-z', '-l', '--full-tree', tree, '--', ...paths]).toString('utf8');
  for (const entry of output.split('\0')) {
    if (!entry) continue;
    const tab = entry.indexOf('\t');
    const [mode, , object, size] = entry.slice(0, tab).split(/ +/);
    const file = entry.slice(tab + 1);
    if (REGULAR_MODES.has(mode)) files.push({ mode, object, size: Number(size), path: file });
    else special.push({ mode, path: file });
  }
  return { files, special };
}

/**
 * Write the regular files of `entries` (from listTree) under `destination`.
 * Unlike archiveTree this reads blobs directly, so the tree's own
 * .gitattributes cannot hide or rewrite files, and symlinks are never created:
 * safe for trees from an untrusted pull request.
 */
export function writeTreeFiles(cwd, entries, destination) {
  if (!entries.length) return;
  const output = git(cwd, ['cat-file', '--batch'], {
    input: `${entries.map(entry => entry.object).join('\n')}\n`,
  });
  let offset = 0;
  for (const entry of entries) {
    const newline = output.indexOf(0x0a, offset);
    const [object, type, size] = output.subarray(offset, newline).toString('utf8').split(' ');
    if (object !== entry.object || type !== 'blob') throw new Error(`Unexpected object for ${entry.path}.`);
    const start = newline + 1;
    const end = start + Number(size);
    const target = path.resolve(destination, entry.path);
    if (!target.startsWith(path.resolve(destination) + path.sep)) throw new Error(`Refusing to write outside the tree: ${entry.path}`);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, output.subarray(start, end), { mode: entry.mode === '100755' ? 0o755 : 0o644 });
    offset = end + 1;
  }
}

/** True when `relative` exists in `tree` (any tree-ish). */
export function treeContains(cwd, tree, relative) {
  return spawnSync('git', ['cat-file', '-e', `${tree}:${relative}`], { cwd, stdio: 'ignore' }).status === 0;
}

/** Extract `paths` from `tree` into `destination`. */
export function archiveTree(cwd, tree, paths, destination) {
  return new Promise((resolve, reject) => {
    const gitArchive = spawn('git', ['archive', '--format=tar', tree, '--', ...paths], {
      cwd, stdio: ['ignore', 'pipe', 'pipe'],
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
      else reject(new Error(errors.trim() || `Could not extract ${tree}.`));
    };
    gitArchive.on('close', code => { gitStatus = code; finish(); });
    tar.on('close', code => { tarStatus = code; finish(); });
  });
}
