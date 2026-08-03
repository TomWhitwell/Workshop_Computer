// Integration checks over the complete generated site. The package pretest
// script always rebuilds site/ first, so these assertions never inspect stale
// output from an earlier checkout.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { resolveLegacyTarget } from '../assets/js/legacy-redirects.js';

const root = fileURLToPath(new URL('../../..', import.meta.url));
const siteDir = path.join(root, 'site');

function readJson(relative) {
  return JSON.parse(fs.readFileSync(path.join(siteDir, relative), 'utf8'));
}

function existingTarget(fromFile, rawUrl) {
  if (!rawUrl || rawUrl.startsWith('#') || rawUrl.startsWith('//') || /^[a-z][a-z0-9+.-]*:/i.test(rawUrl)) return null;
  const pathname = rawUrl.split(/[?#]/, 1)[0];
  if (!pathname) return null;
  let decoded;
  try { decoded = decodeURIComponent(pathname); } catch { decoded = pathname; }
  let target = decoded.startsWith('/')
    ? path.resolve(siteDir, `.${decoded}`)
    : path.resolve(path.dirname(fromFile), decoded);
  if (pathname.endsWith('/') || (fs.existsSync(target) && fs.statSync(target).isDirectory())) target = path.join(target, 'index.html');
  return target;
}

test('generated catalogue is complete, unique, and internally consistent', () => {
  const catalogue = readJson('cards.json');
  const rawIndex = readJson('raw-info/index.json');
  assert.equal(catalogue.schema.id, 'workshop-computer-info-yaml');
  assert.ok(Number.isInteger(catalogue.schema.version));
  assert.ok(catalogue.schema.requiredFields.includes('Name'));
  assert.ok(catalogue.cards.length > 20);

  const ids = catalogue.cards.map(card => card.id);
  const slugs = catalogue.cards.map(card => card.slug);
  assert.equal(new Set(ids).size, ids.length, 'card IDs must be unique');
  assert.equal(new Set(slugs).size, slugs.length, 'card slugs must be unique');
  assert.deepEqual(new Set(rawIndex.map(item => item.id)), new Set(ids));

  let previousNumber = -Infinity;
  for (const card of catalogue.cards) {
    assert.ok(card.id && card.slug && card.title && card.source_file);
    assert.equal(card.url, `programs/${card.slug}/`);
    assert.ok(fs.existsSync(path.join(root, card.source_file)), `${card.id} source file is missing`);
    assert.ok(fs.existsSync(path.join(siteDir, card.url, 'index.html')), `${card.id} detail page is missing`);
    assert.ok(fs.existsSync(path.join(siteDir, 'raw-info', card.id, 'info.yaml')));
    assert.ok(fs.existsSync(path.join(siteDir, 'raw-info', card.id, 'extras.json')));
    const number = Number.parseInt(String(card.release || card.id).split('/')[0].split('_')[0], 10);
    if (Number.isFinite(number)) {
      assert.ok(number >= previousNumber, `${card.id} is out of numeric order`);
      previousNumber = number;
    }
  }
  assert.ok(!JSON.stringify(catalogue).includes('/workspaces/'), 'catalogue contains a build-machine path');
});

test('generated panel views and firmware fingerprints satisfy site invariants', () => {
  const { cards } = readJson('cards.json');
  const cardIds = new Set(cards.map(card => card.id));
  for (const card of cards) {
    if (!card.panel_views) continue;
    const { source, default: defaultId, items } = card.panel_views;
    assert.ok(Array.isArray(items));
    const itemIds = items.map(item => item.id);
    assert.equal(new Set(itemIds).size, itemIds.length, `${card.id} has duplicate panel views`);
    if (items.length) assert.ok(itemIds.includes(defaultId), `${card.id} has an invalid default panel view`);
    if (source === 'generated') {
      assert.ok(items.length <= 3);
      assert.ok(itemIds.every(id => ['up', 'middle', 'down'].includes(id)));
      if (itemIds.includes('middle')) assert.equal(defaultId, 'middle');
    } else {
      assert.equal(source, 'custom');
      assert.ok(itemIds.every(id => /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(id)));
      for (const item of items) {
        assert.ok(['custom', 'generated'].includes(item.image_kind));
        assert.ok(['custom', 'generated'].includes(item.content_kind));
        if (item.image_kind === 'custom') {
          assert.equal(item.image?.format, 'svg');
          assert.ok(item.image?.url && item.image.width > 0 && item.image.height > 0);
        } else {
          assert.equal(item.image, undefined);
        }
      }
    }
  }

  const fingerprints = readJson('firmware-fingerprints.json');
  assert.equal(fingerprints.algorithm, 'SHA-256');
  assert.ok(Number.isInteger(fingerprints.address) && Number.isInteger(fingerprints.length));
  assert.equal(new Set(fingerprints.entries.map(entry => entry.hash)).size, fingerprints.entries.length);
  for (const entry of fingerprints.entries) {
    assert.match(entry.hash, /^[a-f0-9]{64}$/);
    assert.ok(entry.cards.every(card => cardIds.has(card.id)));
  }
});

test('generator-owned HTML has no broken local links or assets', () => {
  const { cards } = readJson('cards.json');
  const htmlFiles = [
    'index.html', 'archive/index.html', 'random/index.html', '404.html',
    ...cards.map(card => `programs/${card.slug}/index.html`),
  ].map(relative => path.join(siteDir, relative));

  const failures = [];
  for (const file of htmlFiles) {
    const html = fs.readFileSync(file, 'utf8');
    for (const match of html.matchAll(/\b(?:href|src)=(['"])(.*?)\1/gi)) {
      const target = existingTarget(file, match[2]);
      if (!target) continue;
      if (!target.startsWith(siteDir + path.sep) || !fs.existsSync(target)) {
        failures.push(`${path.relative(siteDir, file)} -> ${match[2]}`);
      }
    }
  }
  assert.deepEqual(failures, []);
});

test('generator-owned layouts use external runtime scripts', () => {
  const { cards } = readJson('cards.json');
  const htmlFiles = [
    'index.html', 'archive/index.html', '404.html',
    ...cards.map(card => `programs/${card.slug}/index.html`),
  ];

  for (const relative of htmlFiles) {
    const html = fs.readFileSync(path.join(siteDir, relative), 'utf8');
    const executableInline = [...html.matchAll(/<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/gi)]
      .filter(match => !/\btype=(['"])importmap\1/i.test(match[1]) && match[2].trim());
    assert.deepEqual(executableInline, [], `${relative} contains executable inline runtime code`);
  }
});

test('legacy public card URLs resolve only to known current routes', () => {
  const { cards } = readJson('cards.json');
  const slugs = new Set(cards.map(card => card.slug));

  assert.equal(
    resolveLegacyTarget('https://computer.musicthing.co.uk/#15-mlrws', '/', slugs),
    'https://computer.musicthing.co.uk/programs/15-mlrws/',
  );
  assert.equal(
    resolveLegacyTarget('https://computer.musicthing.co.uk/Workshop_Computer/programs/15-mlrws/web/index.html?mode=edit#patch', '/', slugs),
    'https://computer.musicthing.co.uk/programs/15-mlrws/web/index.html?mode=edit#patch',
  );
  assert.equal(
    resolveLegacyTarget('https://computer.musicthing.co.uk/#not-a-card', '/', slugs),
    null,
  );
  assert.equal(
    resolveLegacyTarget('https://dessertplanet.github.io/Workshop_Computer/programs/15-mlrws/', '/Workshop_Computer/', slugs),
    null,
  );
});