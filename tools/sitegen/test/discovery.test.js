import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { discoverRelease } from '../src/discover/release.js';
import { discoverDocs } from '../src/discover/docs.js';
import { compareFirmwareCandidates, curateUf2Downloads } from '../src/discover/downloads.js';
import { discoverCustomPanels, validateCustomPanelReferences } from '../src/discover/customPanels.js';
import { copyWebAssets, resolveWebConfig } from '../src/discover/webEditor.js';

async function fixture(t) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-sitegen-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  return dir;
}

async function write(file, contents = '') {
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, contents);
}

test('release discovery assembles metadata, rewritten README, docs, audio, and web editor', async t => {
  const root = await fixture(t);
  const releases = path.join(root, 'releases');
  const release = path.join(releases, '42_fixture');
  const out = path.join(root, 'site', 'programs');
  await write(path.join(release, 'info.yaml'), `
Name: Fixture
Creator: Tester
Language: C++
Version: "1.0"
Status: Released
short-description: Fixture card
summary: Integration fixture
audio-sample: demo.wav
`);
  await write(path.join(release, 'README.md'), '[Local](guide.txt) [Remote](https://example.test/docs)');
  await write(path.join(release, 'guide.txt'), 'guide');
  await write(path.join(release, 'demo.wav'), 'audio');
  await write(path.join(release, 'Documentation', 'Guide.pdf'), '%PDF fixture');
  await write(path.join(release, 'Documentation', 'ignored.txt'), 'ignored');
  await write(path.join(release, 'web', 'index.html'), '<h1>Editor</h1>');

  const makeRawUrl = relative => `https://raw.test/${relative}`;
  const discovered = await discoverRelease(
    releases, '42_fixture', out, makeRawUrl,
    'https://pages.test/repo/', 'owner/repo', 'test-ref',
  );
  assert.equal(discovered.card.title, 'Fixture');
  assert.equal(discovered.card.metadata.editor_url, 'https://pages.test/repo/programs/42-fixture/web/index.html');
  assert.equal(discovered.card.audio_samples[0].url, 'https://raw.test/releases/42_fixture/demo.wav');
  assert.match(discovered.readmeHtml, /href="https:\/\/raw\.test\/releases\/42_fixture\/guide\.txt"/);
  assert.match(discovered.readmeHtml, /href="https:\/\/example\.test\/docs"/);
  assert.deepEqual(discovered.docs.map(doc => doc.name), ['Guide.pdf']);
  assert.ok(await fs.stat(path.join(out, '42-fixture', 'Documentation', 'Guide.pdf')));
  assert.equal(discovered.card.source_url, 'https://github.com/owner/repo/tree/test-ref/releases/42_fixture');
});

test('document and web discovery copy only publishable assets', async t => {
  const root = await fixture(t);
  const release = path.join(root, 'release');
  const output = path.join(root, 'output');
  await write(path.join(release, 'DoCs', 'A.pdf'), 'pdf');
  await write(path.join(release, 'DoCs', 'notes.md'), 'notes');
  const docs = await discoverDocs(release, output);
  assert.equal(docs.docsDir, 'DoCs');
  assert.deepEqual(docs.docs.map(item => item.url), ['DoCs/A.pdf']);
  await assert.rejects(fs.stat(path.join(output, 'DoCs', 'notes.md')));

  await write(path.join(release, 'web', 'index.html'), 'index');
  await write(path.join(release, 'web', 'assets', 'app.js'), 'app');
  await write(path.join(release, 'web', 'src', 'source.js'), 'source');
  await write(path.join(release, 'web', 'package.json'), '{}');
  const local = await resolveWebConfig({}, release, 'fixture', 'https://pages.test/repo/');
  assert.equal(local.mode, 'local');
  const none = await resolveWebConfig({ Editor: 'none' }, release, 'fixture', 'https://pages.test/repo/');
  assert.equal(none.mode, 'none');
  await copyWebAssets(local.copySrc, path.join(output, 'web'));
  assert.ok(await fs.stat(path.join(output, 'web', 'assets', 'app.js')));
  await assert.rejects(fs.stat(path.join(output, 'web', 'src', 'source.js')));
  await assert.rejects(fs.stat(path.join(output, 'web', 'package.json')));
});

test('web editors reject traversal, unsafe protocols, and symlinks', async t => {
  const release = await fixture(t);
  await write(path.join(release, 'web', 'index.html'), 'index');
  await assert.rejects(
    resolveWebConfig({ Editor: '../outside' }, release, 'fixture', 'https://pages.test/repo/'),
    /must be "web" or "dist"/,
  );
  await assert.rejects(
    resolveWebConfig({ Editor: 'http://example.test/editor' }, release, 'fixture', 'https://pages.test/repo/'),
    /must use HTTPS/,
  );
  await fs.symlink(path.join(release, 'web', 'index.html'), path.join(release, 'web', 'linked.html'));
  await assert.rejects(copyWebAssets(path.join(release, 'web'), path.join(release, 'output')), /symbolic links/);
});

test('automatic firmware ordering is stable and independent of mtimes', () => {
  const candidates = [
    { relRelease: 'old/card-99.uf2', isOld: true, mtime: 999 },
    { relRelease: 'firmware/card-10.uf2', isOld: false, mtime: 1 },
    { relRelease: 'firmware/card-2.uf2', isOld: false, mtime: 500 },
  ];
  assert.deepEqual(candidates.sort(compareFirmwareCandidates).map(item => item.relRelease), [
    'firmware/card-2.uf2', 'firmware/card-10.uf2', 'old/card-99.uf2',
  ]);
});

test('curated firmware resolves case-insensitive paths, hashes files, and handles external links', async t => {
  const release = await fixture(t);
  const bytes = Buffer.from('fixture firmware');
  await write(path.join(release, 'Firmware', 'Card.UF2'), bytes);
  const { uf2Downloads, errors } = await curateUf2Downloads([
    { path: 'firmware/card.uf2', name: 'Local firmware' },
    { name: 'Mirror', download: { url: 'https://downloads.example/fw.uf2', flashable: true, sha256: 'a'.repeat(64) } },
    { path: 'missing.uf2' },
  ], release, 'releases/42_fixture', relative => `https://raw.test/${relative}`);
  assert.equal(errors.length, 1);
  assert.match(errors[0], /missing\.uf2/);
  assert.equal(uf2Downloads[0].url, 'https://raw.test/releases/42_fixture/Firmware/Card.UF2');
  assert.equal(uf2Downloads[0].sha256, crypto.createHash('sha256').update(bytes).digest('hex'));
  assert.deepEqual(uf2Downloads[1], {
    name: 'Mirror', url: 'https://downloads.example/fw.uf2', host: 'downloads.example', external: true, flashable: true,
    sha256: 'a'.repeat(64),
  });
});

test('external firmware rejects active protocols and unhashed browser flashing', async t => {
  const release = await fixture(t);
  const { uf2Downloads, errors } = await curateUf2Downloads([
    { download: { url: 'javascript:alert(1)' } },
    { download: { url: 'https://downloads.example/unhashed.uf2', flashable: true } },
  ], release, 'releases/42_fixture', relative => `https://raw.test/${relative}`);
  assert.deepEqual(uf2Downloads, []);
  assert.equal(errors.length, 2);
});

test('panel asset directories without a manifest do not activate custom panels', async t => {
  const release = await fixture(t);
  const output = path.join(release, 'output');
  await write(path.join(release, 'panels', 'printable-overlay.png'), 'png');

  const result = await discoverCustomPanels(release, output);
  assert.deepEqual(result, { present: false, panels: null, diagnostics: [] });
  await assert.rejects(fs.stat(path.join(output, 'panels')), { code: 'ENOENT' });
});

test('custom panel discovery validates, renders, rewrites, and copies authored presentations', async t => {
  const release = await fixture(t);
  const output = path.join(release, '..', 'output');
  await write(path.join(release, 'panels', 'manifest.yaml'), `
version: 1
default: main
panels:
  - id: main
    name: Main panel
    image: main.svg
    content: docs/main.md
  - id: alternate
    name: Alternate
    image: alternate.svg
    content: alternate.md
`);
  const svg = '<svg viewBox="0 0 560 1785"><rect width="560" height="1785"/></svg>';
  await write(path.join(release, 'panels', 'main.svg'), svg);
  await write(path.join(release, 'panels', 'alternate.svg'), svg);
  await write(path.join(release, 'panels', 'docs', 'main.md'), '# Main\n\n![Diagram](<images/My diagram.png>)');
  await write(path.join(release, 'panels', 'docs', 'images', 'My diagram.png'), 'png');
  await write(path.join(release, 'panels', 'alternate.md'), '# Alternate');

  const result = await discoverCustomPanels(release, output);
  assert.equal(result.present, true);
  assert.equal(result.panels.default, 'main');
  assert.deepEqual(result.panels.items.map(item => item.id), ['main', 'alternate']);
  assert.equal(result.panels.items[0].image.width, 560);
  assert.match(result.panels.items[0].content_html, /panels\/docs\/images\/My%20diagram\.png/);
  assert.ok(await fs.stat(path.join(output, 'panels', 'docs', 'images', 'My diagram.png')));
  assert.deepEqual(result.diagnostics, []);

  const references = validateCustomPanelReferences({
    controls: { knobs: [{ when: { panel: 'missing' }, main: { name: 'Bad reference' } }] },
  }, result.panels);
  assert.ok(references.some(diagnostic => diagnostic.path === 'controls.knobs[0].when.panel'));
});

test('custom panel discovery accepts multiple generated and hybrid presentation tabs', async t => {
  const release = await fixture(t);
  const output = path.join(release, '..', 'output');
  await write(path.join(release, 'panels', 'manifest.yaml'), `
version: 1
default: main
panels:
  - id: main
    name: Main panel
    image: main.svg
    content: auto
  - id: secondary
    name: Secondary panel
  - id: alternate
    name: Alternate
    image: alternate.svg
    content: alternate.md
  - id: textual
    name: Textual
    image: auto
    content: textual.md
`);
  const svg = '<svg viewBox="0 0 560 1785"><rect width="560" height="1785"/></svg>';
  await write(path.join(release, 'panels', 'main.svg'), svg);
  await write(path.join(release, 'panels', 'alternate.svg'), svg);
  await write(path.join(release, 'panels', 'alternate.md'), '# Alternate');
  await write(path.join(release, 'panels', 'textual.md'), '# Textual');

  const result = await discoverCustomPanels(release, output);
  assert.equal(result.present, true);
  assert.equal(result.panels.items[0].kind, 'generated');
  assert.equal(result.panels.items[0].image_kind, 'custom');
  assert.equal(result.panels.items[0].content_html, null);
  assert.equal(result.panels.items[1].kind, 'generated');
  assert.equal(result.panels.items[1].image_kind, 'generated');
  assert.equal(result.panels.items[1].image, undefined);
  assert.equal(result.panels.items[1].content_html, null);
  assert.equal(result.panels.items[2].kind, 'custom');
  assert.equal(result.panels.items[2].image_kind, 'custom');
  assert.equal(result.panels.items[3].content_kind, 'custom');
  assert.equal(result.panels.items[3].image_kind, 'generated');
  assert.equal(result.panels.items[3].image, undefined);
  assert.deepEqual(result.diagnostics, []);
});

test('custom panel discovery rejects blank and non-string auto-capable values', async t => {
  const release = await fixture(t);
  await write(path.join(release, 'panels', 'manifest.yaml'), `
version: 1
default: blank
panels:
  - { id: blank, name: Blank, image: blank.svg, content: "" }
  - { id: numeric, name: Numeric, image: numeric.svg, content: 42 }
  - { id: blank-image, name: Blank image, image: "", content: auto }
  - { id: numeric-image, name: Numeric image, image: 42, content: auto }
`);

  const result = await discoverCustomPanels(release, path.join(release, '..', 'output'));
  assert.deepEqual(result.panels.items, []);
  assert.equal(result.diagnostics.filter(diagnostic => diagnostic.path.endsWith('.content')).length, 2);
  assert.equal(result.diagnostics.filter(diagnostic => diagnostic.path.endsWith('.image')).length, 2);
});