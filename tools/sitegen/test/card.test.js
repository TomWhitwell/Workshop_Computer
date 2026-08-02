// Tests for the pure card-model assembly (buildCanonicalCardModel).
// All inputs are injected, so these run with no filesystem or git.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildCanonicalCardModel } from '../src/model/card.js';

function build(rawYaml = {}, extra = {}) {
  return buildCanonicalCardModel({
    folderName: '42_test_card',
    slug: 'test-card',
    rawYaml,
    ...extra,
  });
}

test('minimal input falls back to titleized folder name and records warnings', () => {
  const card = build({});
  assert.equal(card.title, 'Test Card');
  assert.equal(card.id, '42_test_card');
  assert.equal(card.url, 'programs/test-card/');
  assert.ok(card.warnings.some(w => w.includes('Name missing')));
});

test('Name wins, legacy Title is the fallback', () => {
  assert.equal(build({ Name: 'Real Name', Title: 'Old Title' }).title, 'Real Name');
  assert.equal(build({ Title: 'Old Title' }).title, 'Old Title');
});

test('summary and short-description are carried through verbatim', () => {
  const card = build({
    'short-description': 'One line.',
    summary: 'A full multi-sentence summary that must not be truncated.',
  });
  assert.equal(card.short_description, 'One line.');
  assert.equal(card.summary, 'A full multi-sentence summary that must not be truncated.');
});

test('created derives from the earliest git signal; updated from content date', () => {
  const card = build({}, {
    gitFirstDate: '2024-06-01',
    blameDate: '2024-01-15',
    contentDate: '2025-03-01',
    gitLastDate: '2025-05-01',
  });
  assert.equal(card.metadata.created, '2024-01-15');
  assert.equal(card.metadata.created_inferred, true);
  assert.equal(card.metadata.updated, '2025-03-01');
});

test('updated is never presented as older than created', () => {
  const card = build({ created: '2025-01-01' }, { contentDate: '2024-06-01' });
  assert.equal(card.metadata.created, '2025-01-01');
  assert.equal(card.metadata.updated, '2025-01-01');
});

test('authored creation and update dates remain independent', () => {
  const card = build({
    'date-created': '2024-02-03',
    'date-updated': '2025-06-07',
  }, {
    gitFirstDate: '2023-01-01',
    contentDate: '2026-01-01',
  });
  assert.equal(card.metadata.created, '2024-02-03');
  assert.equal(card.metadata.created_inferred, undefined);
  assert.equal(card.metadata.updated, '2025-06-07');
});

test('legacy date is treated as the creation date', () => {
  const card = build({ date: '2024-11-30' }, { contentDate: '2025-03-01' });
  assert.equal(card.metadata.created, '2024-11-30');
  assert.equal(card.metadata.updated, '2025-03-01');
});

test('inline readme becomes documentation.intro', () => {
  const card = build({ readme: '# Hello\n\nInline docs.' });
  assert.equal(card.documentation.intro, '# Hello\n\nInline docs.');
  assert.equal(build({}).documentation, undefined);
});

test('tags split on commas and drop blanks', () => {
  const card = build({ tags: ['midi, utility', '', 'sequencer'] });
  assert.deepEqual(card.tags, ['midi', 'utility', 'sequencer']);
});

test('draft accepts boolean and string forms', () => {
  assert.equal(build({ draft: true }).draft, true);
  assert.equal(build({ draft: 'yes' }).draft, true);
  assert.equal(build({ draft: 'false' }).draft, false);
  assert.equal(build({}).draft, false);
});

test('authored UF2 entries are retained as a download-availability signal', () => {
  assert.equal(build({ uf2: [{ download: { url: 'https://example.com/card.uf2' } }] }).has_uf2_metadata, true);
  assert.equal(build({ uf2: [] }).has_uf2_metadata, undefined);
  assert.equal(build({}).has_uf2_metadata, undefined);
});

test('demo-link YouTube URL produces a videos entry', () => {
  const card = build({ 'demo-link': 'https://www.youtube.com/watch?v=dQw4w9WgXcQ' });
  assert.equal(card.videos.length, 1);
  assert.equal(card.videos[0].id, 'dQw4w9WgXcQ');
});
