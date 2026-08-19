import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { resolvePreviewUf2Downloads } from '../src/utils/previewFirmware.js';

const available = [
  { path: 'Firmware/Card.UF2', name: 'Card.UF2', url: 'https://raw.test/Card.UF2', sha256: 'a'.repeat(64) },
  { path: 'old/Card-1.uf2', name: 'Card-1.uf2', url: 'https://raw.test/Card-1.uf2', sha256: 'b'.repeat(64) },
];

test('author preview uses auto-discovered UF2 downloads when metadata is absent', () => {
  assert.deepEqual(resolvePreviewUf2Downloads({}, available), available);
  assert.deepEqual(resolvePreviewUf2Downloads({ uf2: [] }, available), available);
});

test('author preview curated UF2 metadata replaces, labels, and reorders downloads', () => {
  const result = resolvePreviewUf2Downloads({ uf2: [
    { path: 'OLD/card-1.UF2', name: 'Legacy firmware' },
    { path: 'firmware/card.uf2', name: 'Current firmware' },
  ] }, available);
  assert.deepEqual(result.map(item => item.name), ['Legacy firmware', 'Current firmware']);
  assert.equal(result[0].sha256, 'b'.repeat(64));
});

test('author preview external UF2 metadata matches card-detail download data', () => {
  const result = resolvePreviewUf2Downloads({ uf2: [{
    name: 'Firmware mirror',
    download: { url: 'https://downloads.example/card.uf2', sha256: 'c'.repeat(64), flashable: true },
  }] }, available);
  assert.deepEqual(result, [{
    name: 'Firmware mirror', url: 'https://downloads.example/card.uf2', host: 'downloads.example', external: true,
    sha256: 'c'.repeat(64), flashable: true,
  }]);
});

test('author preview infers and honours curated flash sizes', () => {
  const available = [
    { path: 'UF2/goldfish.2.0.2mb.uf2', name: 'goldfish.2.0.2mb.uf2', url: 'https://raw.test/2.uf2' },
    { path: 'UF2/goldfish.2.0.16mb.uf2', name: 'goldfish.2.0.16mb.uf2', url: 'https://raw.test/16.uf2' },
  ];
  const inferred = resolvePreviewUf2Downloads({}, available);
  assert.equal(inferred[0].flash, undefined);
  assert.equal(inferred[1].flash, '16mb');
  const curated = resolvePreviewUf2Downloads({ uf2: [
    { path: 'UF2/goldfish.2.0.16mb.uf2', name: 'Goldfish 16MB', flash: '16mb' },
  ] }, available);
  assert.equal(curated[0].flash, '16mb');
  assert.equal(curated[0].name, 'Goldfish 16MB');
});

test('author preview CSS keeps download and web-editor action tiles visible', () => {
  const css = fs.readFileSync(new URL('../assets/preview/author.css', import.meta.url), 'utf8');
  assert.doesNotMatch(css, /#card-preview \.program-card-actions[^}]*display:none/);
});