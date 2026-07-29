import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {
  cmakeHasXosc64,
  cmakeUsesPicoSdk,
  evaluatePrRules,
  parseNameStatusZ,
  summarizePrTrigger,
} from '../src/validate/prRules.js';

async function fixture(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-pr-rules-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  return root;
}

async function write(root, relative, contents = '') {
  const file = path.join(root, relative);
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, contents);
}

test('release scope rules are advisory warnings', async t => {
  const root = await fixture(t);
  await write(root, 'releases/04_card/info.yaml', 'Name: Four');
  await write(root, 'releases/05_card/README.md', 'Five');
  const diagnostics = await evaluatePrRules([
    { status: 'M', path: 'releases/04_card/info.yaml' },
    { status: 'M', path: 'releases/05_card/README.md' },
    { status: 'M', path: 'releases/README.md' },
    { status: 'M', path: 'documentation/info.yaml.md' },
  ], { root });
  const byRule = rule => diagnostics.filter(item => item.ruleId === rule);
  assert.equal(byRule('multiple-release-directories').length, 1);
  assert.match(byRule('multiple-release-directories')[0].message, /04_card, 05_card/);
  assert.equal(byRule('change-at-releases-root').length, 1);
  assert.equal(byRule('change-outside-release-directory').length, 1);
  assert.equal(byRule('uf2-required').length, 2);
  assert.ok(diagnostics.every(item => item.severity === 'warning'));
});

test('UF2 and Pico SDK rules detect missing oscillator definition and duplicate firmware', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_card/a.uf2', 'same firmware');
  await write(root, 'releases/42_card/archive/b.UF2', 'same firmware');
  await write(root, 'releases/42_card/CMakeLists.txt', `
+pico_sdk_init()
+# target_compile_definitions(card PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
+add_executable(card main.cpp)
+`);
  const diagnostics = await evaluatePrRules([
    { status: 'M', path: 'releases/42_card/info.yaml' },
    { status: 'A', path: 'releases/42_card/a.uf2' },
  ], { root });
  assert.ok(diagnostics.some(item => item.ruleId === 'duplicate-uf2'));
  assert.ok(diagnostics.some(item =>
    item.ruleId === 'pico-xosc64-recommended' && item.severity === 'warning'));
  assert.ok(!diagnostics.some(item => item.ruleId === 'uf2-required'));
});

test('CMake detection accepts active multiline XOSC64 but ignores comments', () => {
  const valid = `
+pico_sdk_init()
+target_compile_definitions(card
+  PUBLIC
+  PICO_XOSC_STARTUP_DELAY_MULTIPLIER = 64
+)
+`;
  assert.equal(cmakeUsesPicoSdk(valid), true);
  assert.equal(cmakeHasXosc64(valid), true);
  assert.equal(cmakeUsesPicoSdk('# pico_sdk_init()'), false);
  assert.equal(cmakeHasXosc64('# target_compile_definitions(x PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)'), false);
});

test('Pico rule accepts XOSC64 supplied by the selected custom board header', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_card/card.uf2', 'firmware');
  await write(root, 'releases/42_card/CMakeLists.txt', `
set(PICO_BOARD mtm_computer_16mb CACHE STRING "Board type")
pico_sdk_init()
`);
  await write(root, 'releases/42_card/mtm_computer_16mb.h', `
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif
`);
  const diagnostics = await evaluatePrRules([
    { status: 'M', path: 'releases/42_card/info.yaml' },
    { status: 'M', path: 'releases/42_card/card.uf2' },
  ], { root });
  assert.ok(!diagnostics.some(item => item.ruleId === 'pico-xosc64-recommended'));
});

test('NUL name-status parsing preserves renames and spaces', () => {
  const parsed = parseNameStatusZ(Buffer.from(
    'M\0releases/04 card/info.yaml\0R100\0old/file.uf2\0releases/04 card/new.uf2\0'
  ));
  assert.deepEqual(parsed, [
    { status: 'M', path: 'releases/04 card/info.yaml' },
    { status: 'R100', oldPath: 'old/file.uf2', path: 'releases/04 card/new.uf2' },
  ]);
  assert.deepEqual(summarizePrTrigger(parsed), {
    affectedReleases: ['04 card'],
    changedPaths: parsed,
  });
});

test('deleting a complete release reports an explicit warning', async t => {
  const root = await fixture(t);
  const diagnostics = await evaluatePrRules([
    { status: 'D', path: 'releases/42_deleted/info.yaml' },
    { status: 'D', path: 'releases/42_deleted/card.uf2' },
  ], { root });
  assert.ok(diagnostics.some(item =>
    item.ruleId === 'release-directory-deleted' && item.severity === 'warning'));
  assert.ok(!diagnostics.some(item => item.ruleId === 'uf2-required'));
});

test('release without a local README warns', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_card/info.yaml', 'Name: Card');
  await write(root, 'releases/42_card/card.uf2', 'firmware');
  const diagnostics = await evaluatePrRules([
    { status: 'M', path: 'releases/42_card/info.yaml' },
    { status: 'M', path: 'releases/42_card/card.uf2' },
  ], { root });
  assert.ok(diagnostics.some(item =>
    item.ruleId === 'release-readme-recommended' && item.severity === 'warning'));
});

test('existing UF2 in a nested release subdirectory satisfies the firmware rule', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_card/info.yaml', 'Name: Card');
  await write(root, 'releases/42_card/README.md', '# Card');
  await write(root, 'releases/42_card/UF2/archive/card.UF2', 'firmware');
  const diagnostics = await evaluatePrRules([
    { status: 'M', path: 'releases/42_card/info.yaml' },
  ], { root });
  assert.ok(!diagnostics.some(item => item.ruleId === 'uf2-required'));
});

test('malformed release-local panels are blocking errors', async t => {
  const root = await fixture(t);
  await write(root, 'releases/42_card/info.yaml', 'Name: Card');
  await write(root, 'releases/42_card/README.md', '# Card');
  await write(root, 'releases/42_card/card.uf2', 'firmware');
  await write(root, 'releases/42_card/panels/manifest.yaml', `
version: 1
default: broken
panels:
  - id: Not Valid
    name: Broken
    image: ../outside.svg
    content: missing.md
`);
  const diagnostics = await evaluatePrRules([
    { status: 'A', path: 'releases/42_card/panels/manifest.yaml' },
    { status: 'A', path: 'releases/42_card/card.uf2' },
  ], { root });
  assert.ok(diagnostics.some(item =>
    item.ruleId === 'custom-panels' && item.severity === 'error'));
});
