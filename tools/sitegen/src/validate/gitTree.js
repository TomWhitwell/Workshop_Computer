// Materialize parts of a Git tree on disk without touching the working tree,
// for validating a staged snapshot or a PR's base commit.

import { spawn, spawnSync } from 'node:child_process';

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
