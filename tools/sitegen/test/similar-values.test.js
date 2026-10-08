// Tests for near-duplicate metadata detection (utils/similarValues.js), the
// `similar-values` validator rule, and the metadata-values merge tool.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { areSimilar, collectKnownValues, groupSimilar } from '../src/utils/similarValues.js';
import { parseSource } from '../src/validate/parseSource.js';
import { validateInfoYaml } from '../src/validate/validateInfoYaml.js';
import { applySheet, parseSheet, renderSheet } from '../src/metadata/valuesCli.js';

test('areSimilar catches spelling variants', () => {
  for (const [a, b] of [
    ['quantiser', 'quantizer'],
    ['effect', 'effects'],
    ['lofi', 'lo-fi'],
    ['webmidi', 'web-midi'],
    ['sample-hold', 'sample-and-hold'],
    ['bitcrush', 'bitcrusher'],
    ['Released', 'released'],
    ['Functional but WIP', 'Functional, but WIP'],
    ['C++ (Pico SDK / ComputerCard)', 'C++ (Pico SDK + ComputerCard)'],
    ['oscilator', 'oscillator'],
  ]) assert.equal(areSimilar(a, b), true, `${a} ~ ${b}`);
});

test('areSimilar keeps distinct values apart', () => {
  for (const [a, b] of [
    ['chorus', 'chord'],
    ['C (RPi Pico SDK)', 'C++ (RPi Pico SDK)'],
    ['fm', 'fx'],
    ['cv', 'cv'],
    ['delay', 'reverb'],
  ]) assert.equal(areSimilar(a, b), false, `${a} !~ ${b}`);
});

test('groupSimilar links chains of similar values', () => {
  assert.deepEqual(groupSimilar(['chord', 'chords', 'delay']), [['chord', 'chords'], ['delay']]);
});

const known = collectKnownValues([
  { id: '01_a', data: { tags: ['quantizer', 'effect'], Language: 'C++', Status: 'Released' } },
  { id: '02_b', data: { tags: ['quantizer'], Language: 'C++', Status: 'Released' } },
  { id: '03_c', data: { tags: ['quantiser'], Language: 'C++', Status: 'released' } },
]);

function similarDiagnostics(yaml, cardId) {
  const result = validateInfoYaml(parseSource(yaml, `releases/${cardId}/info.yaml`), { knownValues: known });
  return result.diagnostics.filter(d => d.ruleId === 'similar-values');
}

test('similar-values warns on a new spelling of an existing value', () => {
  const diagnostics = similarDiagnostics('tags:\n  - effects\n  - quantizer\nStatus: Released\n', '04_new');
  assert.equal(diagnostics.length, 1);
  assert.equal(diagnostics[0].severity, 'warning');
  assert.match(diagnostics[0].message, /Tag "effects" looks like existing "effect" \(1 card\)/);
  assert.equal(diagnostics[0].line, 1);
});

test("similar-values ignores the card's own values and shared values", () => {
  // 03_c is the only card using "quantiser" and "released": it should be told
  // about the more common spellings, not about its own entries.
  const own = similarDiagnostics('tags:\n  - quantiser\nStatus: released\n', '03_c');
  assert.deepEqual(own.map(d => d.message.match(/"([^"]+)" looks like existing "([^"]+)"/).slice(1)), [
    ['quantiser', 'quantizer'],
    ['released', 'Released'],
  ]);
  // A value another card already uses is established, even if similar to others.
  assert.deepEqual(similarDiagnostics('tags:\n  - quantiser\n', '04_new'), []);
  // Without an index (e.g. a single file in isolation) the rule is silent.
  const result = validateInfoYaml(parseSource('tags:\n  - effects\n', 'info.yaml'));
  assert.equal(result.diagnostics.filter(d => d.ruleId === 'similar-values').length, 0);
});

test('renderSheet groups similar values and parseSheet keeps only edits', () => {
  const sheet = renderSheet(known);
  assert.match(sheet, /# similar\n {2}quantizer: quantizer {2}# 2 cards: 01_a, 02_b\n {2}quantiser: quantiser {2}# 1 card: 03_c/);
  const edited = sheet.replace('quantiser: quantiser', 'quantiser: quantizer').replace('effect: effect', 'effect: ~');
  const parsed = parseSheet(edited);
  assert.deepEqual([...parsed.tags], [['quantiser', 'quantizer'], ['effect', null]]);
  assert.equal(parsed.Language.size, 0);
  assert.throws(() => parseSheet('Status:\n  Released: ~\n'), /cannot be removed/);
});

test('applySheet splices edits without reformatting the rest of the file', () => {
  const source = [
    'Name: Test   # keep this comment',
    'Language: "C++ (Pico SDK + ComputerCard)"',
    "Status: 'released'",
    'tags:',
    '  - quantiser',
    '  - effects',
    '  - effect',
    '  - drone',
    'summary: >-',
    '  Untouched.',
    '',
  ].join('\n');
  const sheet = {
    tags: new Map([['quantiser', 'quantizer'], ['effects', 'effect'], ['drone', null]]),
    Language: new Map([['C++ (Pico SDK + ComputerCard)', 'C++ (Pico SDK / ComputerCard)']]),
    Status: new Map([['released', 'Released']]),
  };
  const { source: out, changes } = applySheet(source, sheet);
  assert.equal(out, [
    'Name: Test   # keep this comment',
    'Language: "C++ (Pico SDK / ComputerCard)"',
    "Status: 'Released'",
    'tags:',
    '  - quantizer',
    '  - effect',
    'summary: >-',
    '  Untouched.',
    '',
  ].join('\n'));
  assert.deepEqual(changes, [
    'tags: "quantiser" → "quantizer"',
    'tags: "effects" → "effect"',
    'tags: removed duplicate "effect"',
    'tags: removed "drone"',
    'Language: "C++ (Pico SDK + ComputerCard)" → "C++ (Pico SDK / ComputerCard)"',
    'Status: "released" → "Released"',
  ]);
});

test('applySheet handles flow and comma-separated tag lists', () => {
  const sheet = { tags: new Map([['lofi', 'lo-fi'], ['noise', null]]), Language: new Map(), Status: new Map() };
  assert.equal(applySheet('tags: [lofi, noise, drone]\n', sheet).source, 'tags: [lo-fi, drone]\n');
  assert.equal(applySheet('tags: lofi, noise, drone\n', sheet).source, 'tags: lo-fi, drone\n');
  assert.equal(applySheet('tags: [drone]\n', sheet).changes.length, 0);
});
