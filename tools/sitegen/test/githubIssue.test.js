import { test } from 'node:test';
import assert from 'node:assert/strict';
import { releaseCards } from '../src/curation/cli.js';
import { missingCardIssueLabels } from '../src/github/syncCardLabels.js';
import {
  GITHUB_LABEL_MAX,
  cardFeedbackHostLabel,
  cardFeedbackUrl,
  cardIssueLabel,
  cardIssueLabelsFromCards,
} from '../src/render/githubIssue.js';

test('cardIssueLabel follows the existing GitHub number-plus-name pattern', () => {
  assert.equal(cardIssueLabel({ id: '00_Simple_MIDI', title: 'Simple MIDI' }), '00 Simple MIDI');
  assert.equal(cardIssueLabel({ id: '03_Turing_Machine', title: 'Turing Machine' }), '03 Turing Machine');
  assert.equal(cardIssueLabel({ id: '20_reverb', title: 'Reverb+' }), '20 Reverb+');
  assert.equal(cardIssueLabel({ id: '41_blackbird', title: 'Blackbird' }), '41 Blackbird');
  assert.equal(cardIssueLabel({ id: '433_sense_of_space', title: '433 Sense of Space' }), '433 Sense of Space');
});

test('cardIssueLabel truncates to GitHub’s 50-character limit', () => {
  const title = 'A very long program card title that must be truncated for GitHub';
  const label = cardIssueLabel({ id: '355_long', title });
  assert.equal(label.length, GITHUB_LABEL_MAX);
  assert.equal(label.startsWith('355 A very long'), true);
});

test('cardIssueLabelsFromCards skips placeholders and dedupes', () => {
  assert.deepEqual(cardIssueLabelsFromCards([
    { id: '02_comingsoon', title: 'Coming Soon' },
    { id: '41_blackbird', title: 'Blackbird' },
    { id: '77_Placeholder', title: 'Placeholder' },
    { id: '41_blackbird', title: 'Blackbird' },
  ]), ['41 Blackbird']);
});

test('cardFeedbackUrl uses the catalogue template for local cards', () => {
  const local = { id: '41_blackbird', title: 'Blackbird' };
  const inMonorepo = {
    id: '60_markov',
    title: 'Markov',
    metadata: { repository: 'https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/60_markov' },
  };
  for (const card of [local, inMonorepo]) {
    const url = new URL(cardFeedbackUrl(card));
    assert.equal(url.origin + url.pathname, 'https://github.com/TomWhitwell/Workshop_Computer/issues/new');
    assert.equal(url.searchParams.get('template'), 'card_feedback.yml');
    assert.equal(url.searchParams.get('card'), cardIssueLabel(card));
  }
});

test('cardFeedbackUrl sends offsite cards to their own issue tracker', () => {
  const githubCard = {
    id: '61_ZX_Spectrum',
    title: 'ZX',
    metadata: { repository: 'https://github.com/uglifruit/WorkshopZX' },
  };
  assert.equal(cardFeedbackUrl(githubCard), 'https://github.com/uglifruit/WorkshopZX/issues/new?title=61+ZX%3A+');
  assert.equal(cardFeedbackHostLabel(githubCard), 'GitHub');

  const githubTree = cardFeedbackUrl({
    id: '53_glitter',
    title: 'Glitter',
    metadata: { repository: 'https://github.com/sdrjones/mtws/tree/main/53_glitter' },
  });
  assert.equal(githubTree, 'https://github.com/sdrjones/mtws/issues/new?title=53+Glitter%3A+');

  const codebergCard = {
    id: '64_voices_of_sid',
    title: 'Voices of SID',
    metadata: { repository: 'https://codeberg.org/johantv/voices-of-sid' },
  };
  assert.equal(cardFeedbackUrl(codebergCard), 'https://codeberg.org/johantv/voices-of-sid/issues/new?title=64+Voices+of+SID%3A+');
  assert.equal(cardFeedbackHostLabel(codebergCard), 'Codeberg');

  const codebergPath = cardFeedbackUrl({
    id: '42_backyard_rain',
    title: 'Backyard Rain',
    metadata: { repository: 'https://codeberg.org/briandorsey/mtmws_cards/src/branch/main/backyard_rain' },
  });
  assert.equal(codebergPath, 'https://codeberg.org/briandorsey/mtmws_cards/issues/new?title=42+Backyard+Rain%3A+');
});

test('missingCardIssueLabels only creates names that are not already on the repo', () => {
  assert.deepEqual(
    missingCardIssueLabels(['41 Blackbird', '00 Simple MIDI', '97 Alloy'], ['41 Blackbird', 'Website']),
    ['00 Simple MIDI', '97 Alloy'],
  );
});

test('catalogue cards produce the labels already used on GitHub', () => {
  const labels = cardIssueLabelsFromCards(releaseCards());
  for (const expected of ['00 Simple MIDI', '03 Turing Machine', '20 Reverb+', '41 Blackbird', '88 Blank', '433 Sense of Space']) {
    assert.equal(labels.includes(expected), true, `expected ${expected}`);
  }
  assert.equal(labels.includes('433 433 Sense of Space'), false);
  assert.equal(labels.includes('02 Coming Soon'), false);
});
