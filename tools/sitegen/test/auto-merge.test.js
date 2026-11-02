import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  GRACE_MS, decideAutoMerge, eligibleSince, isTrustedVerdictRun, parseVerdictArtifact, verdictArtifactName,
} from '../src/validate/autoMerge.js';

const now = Date.parse('2026-10-25T12:00:00Z');
const ago = minutes => new Date(now - minutes * 60000).toISOString();
const head = 'a'.repeat(40);
const older = 'b'.repeat(40);
const verdict = (sha, eligible, minutesAgo) => ({ sha, eligible, created_at: ago(minutesAgo) });
const pr = overrides => ({ number: 1, draft: false, base: { ref: 'main' }, head: { sha: head }, labels: [], ...overrides });
const eligible = { eligible: true, reasons: [], basis: 'maker has committed to 42_card before.' };
const validated = { status: 'completed', conclusion: 'success' };
const decide = ({ verdicts = [verdict(head, true, 61)], validation = validated, eligibility = eligible, ...prOverrides } = {}) =>
  decideAutoMerge({ pr: pr(prOverrides), eligibility, verdicts, validation, now });

test('verdict artifact names round-trip and nothing else parses', () => {
  const name = verdictArtifactName(42, head, true);
  assert.equal(name, `merge-eligibility-42-${head}-eligible`);
  assert.deepEqual(parseVerdictArtifact(name), { number: 42, sha: head, eligible: true });
  assert.equal(parseVerdictArtifact(verdictArtifactName(42, head, false)).eligible, false);
  for (const other of ['merge-eligibility-42', 'validation-report-42', `merge-eligibility-42-${head}-maybe`, `x-merge-eligibility-42-${head}-eligible`]) {
    assert.equal(parseVerdictArtifact(other), null, other);
  }
});

test('only pull_request_target runs of the verdict workflow are trusted', () => {
  assert.equal(isTrustedVerdictRun({ path: '.github/workflows/merge-eligibility.yml', event: 'pull_request_target' }), true);
  // A PR can run its own copy of the workflow file on pull_request, or a branch's copy by dispatch.
  assert.equal(isTrustedVerdictRun({ path: '.github/workflows/merge-eligibility.yml', event: 'pull_request' }), false);
  assert.equal(isTrustedVerdictRun({ path: '.github/workflows/merge-eligibility.yml', event: 'workflow_dispatch' }), false);
  assert.equal(isTrustedVerdictRun({ path: '.github/workflows/fake.yml', event: 'pull_request_target' }), false);
});

test('an eligible, validated PR merges once it has been eligible for the grace period', () => {
  assert.equal(GRACE_MS, 60 * 60 * 1000);
  const decision = decide();
  assert.equal(decision.merge, true, decision.reason);
  assert.match(decision.reason, /committed to 42_card before\. Eligible for 61 min/);
  assert.match(decide({ verdicts: [verdict(head, true, 30)] }).reason, /grace period \(30 min left\)/);
});

test('the clock starts at the first eligible verdict, not the first verdict', () => {
  // Draft for 90 minutes, then marked ready 5 minutes ago on the same commit.
  const draftThenReady = [verdict(head, false, 90), verdict(head, true, 5)];
  assert.equal(eligibleSince(draftThenReady, head), now - 5 * 60000);
  assert.match(decide({ verdicts: draftThenReady }).reason, /grace period \(55 min left\)/);
  // Eligible, held, then released: the earliest eligible verdict in the run counts.
  assert.equal(eligibleSince([verdict(head, true, 90), verdict(head, false, 60), verdict(head, true, 5)], head), now - 90 * 60000);
});

test('pushing an older commit back starts the clock afresh', () => {
  const pushedBack = [verdict(head, true, 600), verdict(older, true, 300), verdict(head, true, 5)];
  assert.equal(eligibleSince(pushedBack, head), now - 5 * 60000);
  assert.equal(eligibleSince([verdict(older, true, 90)], head), null);
  assert.match(decide({ verdicts: [] }).reason, /No trusted eligible verdict/);
});

test('the recomputed verdict decides eligibility', () => {
  const decision = decide({ eligibility: { eligible: false, reasons: ['Changes 2 cards.'] } });
  assert.equal(decision.merge, false);
  assert.match(decision.reason, /Not eligible: Changes 2 cards\./);
});

test('validation must have passed on the head commit', () => {
  assert.match(decide({ validation: null }).reason, /awaiting approval/);
  assert.match(decide({ validation: { status: 'completed', conclusion: 'failure' } }).reason, /has not passed/);
  assert.match(decide({ validation: { status: 'in_progress', conclusion: null } }).reason, /has not passed/);
});

test('drafts, held PRs and other base branches are skipped', () => {
  assert.match(decide({ draft: true }).reason, /Draft/);
  assert.match(decide({ labels: [{ name: 'hold' }] }).reason, /"hold" label/);
  assert.match(decide({ base: { ref: 'workshop-ops' } }).reason, /not main/);
});
