// Decide whether the scheduled auto-merge job should merge a pull request now.
//
// Nothing a PR can produce is trusted here. Check runs are not: a PR's own
// workflow can create one with any name. Instead the job recomputes the
// trusted verdict itself from base-branch code (trustedEligibility.js), and
// times the grace period from verdict artifacts, which only pull_request_target
// runs of merge-eligibility.yml count for: that event always runs the base
// branch's copy of the workflow. The functions here are pure; autoMergeCli.js
// gathers the facts.

import { HOLD_LABEL } from './mergeEligibility.js';

/** How long a commit must stay eligible before merging, so a maintainer can add the hold label. */
export const GRACE_MS = 60 * 60 * 1000;

/** Name of the Validate info.yaml job, which must also pass on the merged commit. */
export const VALIDATION_CHECK = 'Validate changed card metadata';

const VERDICT_WORKFLOW = '.github/workflows/merge-eligibility.yml';
const ARTIFACT = /^merge-eligibility-(\d+)-([0-9a-f]{40})-(eligible|ineligible)$/;

/** Artifact name recording a verdict, so the time it was reached can be listed without downloading it. */
export function verdictArtifactName(number, sha, eligible) {
  return `merge-eligibility-${number}-${sha}-${eligible ? 'eligible' : 'ineligible'}`;
}

/** { number, sha, eligible } from a verdict artifact name, or null for any other artifact. */
export function parseVerdictArtifact(name) {
  const match = ARTIFACT.exec(String(name));
  return match ? { number: Number(match[1]), sha: match[2], eligible: match[3] === 'eligible' } : null;
}

/** True for a workflow run whose artifacts may time the grace period. */
export function isTrustedVerdictRun(run) {
  return run?.path === VERDICT_WORKFLOW && run?.event === 'pull_request_target';
}

/** Why a PR is skipped before any verdict is computed, or ''. */
export function prGateReason(pr) {
  if (pr.base?.ref !== 'main') return `Targets ${pr.base?.ref}, not main.`;
  if (pr.draft) return 'Draft.';
  if ((pr.labels || []).some(label => label.name === HOLD_LABEL)) return `Has the "${HOLD_LABEL}" label.`;
  return '';
}

/**
 * When the PR's head commit became eligible, in milliseconds, or null.
 * `verdicts` are the PR's trusted verdict artifacts: { sha, eligible, created_at }.
 * Only the newest unbroken run of verdicts for the head counts, so pushing an
 * older commit back starts afresh, and the clock starts at the first eligible
 * one, so time spent as a draft or on hold does not count.
 */
export function eligibleSince(verdicts, headSha) {
  const newestFirst = [...verdicts].sort((a, b) => Date.parse(b.created_at) - Date.parse(a.created_at));
  let since = null;
  for (const verdict of newestFirst) {
    if (verdict.sha !== headSha) break;
    if (verdict.eligible) since = Date.parse(verdict.created_at);
  }
  return Number.isFinite(since) ? since : null;
}

/**
 * Facts:
 *   pr          { draft, base: { ref }, head: { sha }, labels: [{ name }] }
 *   eligibility the verdict recomputed now from base-branch code
 *   verdicts    the PR's trusted verdict artifacts (see eligibleSince)
 *   validation  latest Validate info.yaml check run on the head, or null
 *   now         current time in milliseconds
 *
 * Returns { merge, reason }.
 */
export function decideAutoMerge({ pr, eligibility, verdicts, validation, now, graceMs = GRACE_MS }) {
  const skip = reason => ({ merge: false, reason });
  const gate = prGateReason(pr);
  if (gate) return skip(gate);
  if (!eligibility?.eligible) return skip(`Not eligible: ${(eligibility?.reasons || []).join(' ') || 'no verdict.'}`);

  const since = eligibleSince(verdicts, pr.head.sha);
  if (since === null) return skip('No trusted eligible verdict for the head commit yet.');
  const waited = now - since;
  if (waited < graceMs) return skip(`In the grace period (${Math.ceil((graceMs - waited) / 60000)} min left).`);

  if (!validation) return skip('Validation has not run on the head commit (it may be awaiting approval).');
  if (validation.status !== 'completed' || validation.conclusion !== 'success') return skip('Validation has not passed on the head commit.');

  return { merge: true, reason: `${eligibility.basis} Eligible for ${Math.floor(waited / 60000)} min.` };
}
