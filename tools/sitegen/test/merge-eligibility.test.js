import { test } from 'node:test';
import assert from 'node:assert/strict';
import { evaluateMergeEligibility, touchesNoCards } from '../src/validate/mergeEligibility.js';
import { hasCommittedToCard } from '../src/validate/cardCommitters.js';
import { reportEligibilityMarkdown } from '../src/validate/reporters/index.js';

const update = { status: 'M', path: 'releases/42_card/info.yaml' };
const firmware = { status: 'A', path: 'releases/42_card/build/card.uf2' };

function check(overrides) {
  return evaluateMergeEligibility({
    changes: [update, firmware],
    author: 'maker',
    association: 'CONTRIBUTOR',
    cardOnBase: true,
    cardInHead: true,
    authorCommitted: true,
    ...overrides,
  });
}

test('an update from a past committer to the card is eligible', () => {
  const result = check({});
  assert.equal(result.eligible, true);
  assert.equal(result.card, '42_card');
  assert.match(result.basis, /maker has committed to 42_card/);
});

test('an update from someone who has not committed to the card is not eligible', () => {
  const result = check({ author: 'stranger', association: 'COLLABORATOR', authorCommitted: false });
  assert.equal(result.eligible, false);
  assert.match(result.reasons.join('\n'), /stranger has not committed to 42_card/);
});

test('an unknown commit history fails closed', () => {
  const result = check({ authorCommitted: null });
  assert.equal(result.eligible, false);
  assert.match(result.reasons.join('\n'), /Could not determine/);
});

test('a new card is eligible only once the author has contributed before', () => {
  for (const association of ['CONTRIBUTOR', 'COLLABORATOR', 'MEMBER', 'OWNER']) {
    assert.equal(check({ cardOnBase: false, authorCommitted: null, association }).eligible, true, association);
  }
  for (const association of ['FIRST_TIME_CONTRIBUTOR', 'FIRST_TIMER', 'NONE']) {
    const result = check({ cardOnBase: false, authorCommitted: null, association });
    assert.equal(result.eligible, false, association);
    assert.match(result.reasons.join('\n'), /first contribution needs a maintainer/);
  }
});

test('multiple cards, files outside the card, curation, and renames are not eligible', () => {
  const cases = {
    'two cards': [update, { status: 'M', path: 'releases/43_other/README.md' }],
    'tooling': [update, { status: 'M', path: 'tools/sitegen/src/build.js' }],
    'curation': [update, { status: 'M', path: 'tools/sitegen/src/curation/flairs.yml' }],
    'releases root': [update, { status: 'M', path: 'releases/README.md' }],
    'rename between cards': [{ status: 'R100', oldPath: 'releases/42_card/a.uf2', path: 'releases/43_other/a.uf2' }],
  };
  for (const [name, changes] of Object.entries(cases)) {
    assert.equal(check({ changes }).eligible, false, name);
  }
  assert.match(check({ changes: cases.curation }).reasons.join('\n'), /outside releases\/<card>\/: tools\/sitegen\/src\/curation\/flairs\.yml/);
});

test('website and curation updates are never eligible, whoever opens them', () => {
  const websiteOnly = [
    [{ status: 'M', path: 'tools/sitegen/src/build.js' }],
    [{ status: 'M', path: 'site/index.html' }],
    [{ status: 'M', path: '.github/workflows/validate-info.yml' }],
    [{ status: 'M', path: 'tools/sitegen/package-lock.json' }],
    [{ status: 'M', path: 'tools/sitegen/src/curation/discovery.yml' }],
  ];
  for (const changes of websiteOnly) {
    for (const association of ['OWNER', 'COLLABORATOR', 'CONTRIBUTOR']) {
      const result = check({ changes, association, authorCommitted: true });
      assert.equal(result.eligible, false, `${changes[0].path} by ${association}`);
      assert.match(result.reasons.join('\n'), /Changes no program card/);
    }
  }
});

test('validation errors, drafts, and deleting the card are not eligible', () => {
  assert.match(check({ errorCount: 2 }).reasons.join('\n'), /2 errors/);
  assert.match(check({ draft: true }).reasons.join('\n'), /draft/);
  assert.match(check({ cardInHead: false }).reasons.join('\n'), /Deletes card 42_card/);
});

test('curation-only and tooling-only PRs touch no cards', () => {
  assert.equal(touchesNoCards([
    { status: 'M', path: 'tools/sitegen/src/curation/flairs.yml' },
    { status: 'M', path: 'tools/sitegen/src/curation/discovery.yml' },
  ]), true);
  assert.equal(touchesNoCards([update]), false);
});

test('the committer lookup asks GitHub about the author and fails closed', async () => {
  const requested = [];
  const respond = body => async url => {
    requested.push(url);
    return { ok: true, json: async () => body };
  };
  const lookup = overrides => hasCommittedToCard({ repo: 'owner/repo', card: '42_card', ref: 'abc', author: 'maker', ...overrides });

  assert.equal(await lookup({ fetchImpl: respond([{ sha: '1' }]) }), true);
  assert.match(requested[0], /repos\/owner\/repo\/commits\?sha=abc&path=releases%2F42_card&author=maker&per_page=1$/);
  assert.equal(await lookup({ fetchImpl: respond([]) }), false);

  const failing = async () => ({ ok: false, json: async () => ({}) });
  assert.equal(await lookup({ fetchImpl: failing }), null);
  const throwing = async () => { throw new Error('offline'); };
  assert.equal(await lookup({ fetchImpl: throwing }), null);
  // An email would match the commit's self-reported address, so it is refused.
  assert.equal(await lookup({ author: 'maker@example.com', fetchImpl: respond([{ sha: '1' }]) }), null);
});

test('eligibility markdown explains the outcome', () => {
  assert.match(reportEligibilityMarkdown(check({})), /Eligible\.\*\* maker has committed/);
  const markdown = reportEligibilityMarkdown(check({ draft: true }));
  assert.match(markdown, /Not eligible/);
  assert.match(markdown, /- The pull request is a draft\./);
  assert.match(markdown, /Report only/);
});

test('introduced near-duplicate values and missing firmware need a maintainer, other warnings do not', () => {
  const finding = (ruleId, message) => ({ severity: 'warning', ruleId, message });
  assert.match(check({ introduced: [finding('similar-values', 'Tag "phaser" looks like existing "phase" (2 cards).')] }).reasons.join('\n'),
    /Needs a maintainer's look: Tag "phaser"/);
  assert.match(check({ cardOnBase: false, introduced: [finding('uf2-required', 'No UF2 firmware file exists anywhere under releases/42_card/.')] }).reasons.join('\n'),
    /Needs a maintainer's look: No UF2/);
  for (const ruleId of ['metadata-completeness', 'pico-xosc64-recommended', 'ajv-schema', 'draft-card-changed']) {
    assert.equal(check({ introduced: [finding(ruleId, 'advice')] }).eligible, true, ruleId);
  }
});
