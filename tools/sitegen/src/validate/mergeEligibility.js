// Decide whether a pull request may be merged without human review.
//
// A PR is eligible only when it changes exactly one program card and nothing
// else, passes validation, introduces no finding a maintainer should look at,
// is not a draft, and its author is trusted for that card:
//   - a new card: the author has had a contribution merged into the repository
//     before (GitHub's author_association), so a human approved them once;
//   - an existing card: the author has already committed to that card on the
//     base branch.
// Everything else is left for a maintainer to review. This module is pure; the
// caller supplies facts gathered from Git and the GitHub API.

// author_association values GitHub assigns once an account has had a commit
// merged here (CONTRIBUTOR) or has repository access.
const TRUSTED_ASSOCIATIONS = new Set(['CONTRIBUTOR', 'COLLABORATOR', 'MEMBER', 'OWNER']);

// Warnings that do not block merging but should not be merged unseen: a new
// near-duplicate tag/Language/Status spelling, or a card left without firmware.
// They count only when the PR introduces them.
const REVIEW_RULES = new Set(['similar-values', 'uf2-required']);

const releaseOf = file => String(file).match(/^releases\/([^/]+)\/.+/)?.[1] || null;

/** The single card a change set is confined to, or the reasons it is not confined to one. */
export function cardScope(changes) {
  const cards = new Set();
  const outside = new Set();
  for (const change of changes) {
    for (const file of change.oldPath ? [change.oldPath, change.path] : [change.path]) {
      const card = releaseOf(file);
      if (card) cards.add(card);
      else outside.add(file);
    }
  }
  return { cards: [...cards].sort(), outside: [...outside].sort() };
}

/** True when a change set touches no program card at all. */
export function touchesNoCards(changes) {
  return cardScope(changes).cards.length === 0;
}

/** Reasons a change set is not confined to a single card (empty when it is). */
export function scopeReasons(changes) {
  const reasons = [];
  const { cards, outside } = cardScope(changes);
  if (cards.length > 1) reasons.push(`Changes ${cards.length} cards (${cards.join(', ')}); only single-card PRs merge automatically.`);
  if (!cards.length) reasons.push('Changes no program card.');
  if (outside.length) {
    const listed = outside.slice(0, 5).join(', ');
    reasons.push(`Changes files outside releases/<card>/: ${listed}${outside.length > 5 ? `, and ${outside.length - 5} more` : ''}.`);
  }
  return reasons;
}

/** Reasons introduced findings need a maintainer's look before merging. */
export function reviewReasons(introduced) {
  return introduced
    .filter(finding => REVIEW_RULES.has(finding.ruleId))
    .map(finding => `Needs a maintainer's look: ${finding.message}`);
}

/**
 * Facts:
 *   changes          parsed name-status list for the whole PR
 *   author           PR author login
 *   association      PR author_association
 *   draft            PR is a draft
 *   errorCount       validation errors in the PR
 *   introduced       diagnostics the PR introduces relative to its base
 *   cardOnBase       the card's directory exists on the base branch
 *   cardInHead       the card's directory exists in the PR
 *   authorCommitted  the author has committed to the card on the base
 *                    branch, or null when that could not be determined
 *
 * Returns { eligible, card, reasons, basis }: `reasons` explain an ineligible
 * result, `basis` explains an eligible one.
 */
export function evaluateMergeEligibility({
  changes, author, association, draft = false, errorCount = 0, introduced = [],
  cardOnBase = false, cardInHead = true, authorCommitted = null,
}) {
  const reasons = [...scopeReasons(changes), ...reviewReasons(introduced)];
  const { cards } = cardScope(changes);
  const card = cards.length === 1 ? cards[0] : null;

  if (errorCount > 0) reasons.push(`Validation reports ${errorCount} error${errorCount === 1 ? '' : 's'}.`);
  if (draft) reasons.push('The pull request is a draft.');
  if (card && !cardInHead) reasons.push(`Deletes card ${card}.`);

  let basis = '';
  if (card && cardInHead) {
    if (!cardOnBase) {
      if (TRUSTED_ASSOCIATIONS.has(association)) {
        basis = `New card from ${author}, who has contributed to this repository before.`;
      } else {
        reasons.push(`New card from ${author}, whose first contribution needs a maintainer's review.`);
      }
    } else if (authorCommitted === null) {
      reasons.push(`Could not determine who has committed to ${card}.`);
    } else if (authorCommitted) {
      basis = `${author} has committed to ${card} before.`;
    } else {
      reasons.push(`${author} has not committed to ${card} before; updates from new authors need a maintainer's review.`);
    }
  }

  const eligible = reasons.length === 0;
  return { eligible, card, reasons, basis: eligible ? basis : '' };
}
