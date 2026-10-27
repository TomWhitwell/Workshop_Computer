// Assess a pull request's auto-merge eligibility without trusting its content.
//
// prValidationCli.js validates a checkout of the PR, so the PR's own copy of
// this validator produces that report. A verdict that will merge something
// must instead come from code on the base branch, so this module reads both
// sides of the PR from Git objects: the PR's files are written out as plain
// data (no symlinks, no .gitattributes processing) and nothing from them is
// installed or executed.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { evaluateChangeSet } from './changeSet.js';
import { hasCommittedToCard } from './cardCommitters.js';
import { listTree, treeContains, writeTreeFiles } from './gitTree.js';
import { cardScope, evaluateMergeEligibility, measureChanges, sizeReasons, touchesNoCards } from './mergeEligibility.js';
import { minifiedWebFiles } from './webCode.js';
import { parseNameStatusZ } from './prRules.js';

const releaseIndexFile = /^releases\/[^/]+\/info\.yaml$/;

function diffNameStatus(cwd, base, head) {
  const result = spawnSync('git', ['diff', '--no-ext-diff', '--no-textconv', '--name-status', '-z', `${base}...${head}`], {
    cwd, maxBuffer: 1 << 30,
  });
  if (result.status !== 0) throw new Error(`git diff failed: ${String(result.stderr).trim()}`);
  return parseNameStatusZ(result.stdout);
}

function readProposed(root, file) {
  try {
    return fs.readFileSync(path.join(root, file));
  } catch {
    return null;
  }
}

/**
 * Options:
 *   cwd           repository holding both commits as objects
 *   base, head    the PR's base branch commit and head commit
 *   author, association, draft, labels   PR facts from the GitHub event
 *   repo, token   for the card committer lookup (omit repo to skip it)
 *   committerLookup  injectable hasCommittedToCard, for tests
 *
 * Returns { changes, report, errorCount, eligibility }; `report` is null when
 * the PR touches no program card.
 */
export async function assessPullRequest({
  cwd, base, head, author, association = 'NONE', draft = false, labels = [], repo = '', token = '',
  committerLookup = hasCommittedToCard,
}) {
  const changes = diffNameStatus(cwd, base, head);
  if (touchesNoCards(changes)) {
    return { changes, report: null, errorCount: 0, eligibility: evaluateMergeEligibility({ changes, author, association, draft, labels }) };
  }

  const { cards } = cardScope(changes);
  const cardDirs = cards.map(card => `releases/${card}`);
  const inCards = file => cardDirs.some(dir => file.startsWith(`${dir}/`));
  const headTree = listTree(cwd, head, ['releases']);
  const sizes = new Map(headTree.files.map(entry => [entry.path, entry.size]));
  const size = measureChanges(changes, file => sizes.get(file) ?? null);
  const specialEntries = headTree.special.filter(entry => inCards(entry.path)).map(entry => entry.path);
  if (sizeReasons(size).length) {
    // Too large to merge unreviewed: decide from the listing alone rather than
    // reading an oversized PR into memory.
    return { changes, report: null, errorCount: 0, eligibility: evaluateMergeEligibility({
      changes, author, association, draft, labels, size, specialEntries,
    }) };
  }

  const headRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-pr-head-'));
  const baseRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'workshop-pr-base-'));
  try {
    // The proposed cards, plus every card's info.yaml for the similar-values index.
    writeTreeFiles(cwd, headTree.files.filter(entry => inCards(entry.path) || releaseIndexFile.test(entry.path)), headRoot);
    writeTreeFiles(cwd, listTree(cwd, base, cardDirs).files, baseRoot);

    const report = await evaluateChangeSet(changes, { root: headRoot, baselineRoot: baseRoot });
    const errorCount = report.info.reduce((count, entry) => count + entry.result.errorCount, 0)
      + report.rules.diagnostics.filter(item => item.severity === 'error').length;

    const card = cards.length === 1 ? cards[0] : null;
    const cardOnBase = card ? treeContains(cwd, base, `releases/${card}`) : false;
    const authorCommitted = card && cardOnBase && repo
      ? await committerLookup({ repo, card, ref: base, author, token })
      : null;
    const eligibility = evaluateMergeEligibility({
      changes,
      author,
      association,
      draft,
      errorCount,
      introduced: [...report.info.flatMap(entry => entry.introduced), ...report.rules.introduced],
      cardOnBase,
      cardInHead: card ? treeContains(cwd, head, `releases/${card}`) : false,
      authorCommitted,
      specialEntries,
      size,
      labels,
      minifiedWeb: minifiedWebFiles(changes, file => readProposed(headRoot, file)),
    });
    return { changes, report, errorCount, eligibility };
  } finally {
    fs.rmSync(headRoot, { recursive: true, force: true });
    fs.rmSync(baseRoot, { recursive: true, force: true });
  }
}
