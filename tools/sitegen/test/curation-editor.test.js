import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
const output = path.join(root, 'site', 'documentation', 'flair-editor.html');

test('build emits one self-contained flair editor with the complete curation model', () => {
  const html = fs.readFileSync(output, 'utf8');
  assert.match(html, /Program Card Flair Editor/);
  assert.match(html, /Download flairs\.yml/);
  assert.match(html, /Flair totals/);
  assert.doesNotMatch(html, /__CURATION_DATA_JSON__/);
  assert.doesNotMatch(html, /<script[^>]+src=/);
  assert.doesNotMatch(html, /<link[^>]+stylesheet/);

  const payload = html.match(/<script id="curation-data" type="application\/json">(.*)<\/script>/)?.[1];
  assert.ok(payload, 'curation payload should be embedded');
  const data = JSON.parse(payload);
  assert.equal(data.cards.length, Object.keys(data.assignments).length);
  assert.ok(data.availableTags.length > 0);
  assert.ok(data.cards.some(card => card.id === '03_Turing_Machine'));
  assert.ok(data.cards.some(card => card.shortDescription));
});
