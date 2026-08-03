import { test } from 'node:test';
import assert from 'node:assert/strict';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repositoryRoot = fileURLToPath(new URL('../../..', import.meta.url));

function run(command, args, cwd) {
  return spawnSync(command, args, { cwd, encoding: 'utf8' });
}

async function repositoryFixture(t) {
  const root = await fsp.mkdtemp(path.join(os.tmpdir(), 'workshop-staged-hook-'));
  t.after(() => fsp.rm(root, { recursive: true, force: true }));
  await fsp.mkdir(path.join(root, 'tools', 'sitegen'), { recursive: true });
  await fsp.cp(path.join(repositoryRoot, 'tools', 'sitegen', 'src'), path.join(root, 'tools', 'sitegen', 'src'), { recursive: true });
  await fsp.copyFile(path.join(repositoryRoot, 'tools', 'sitegen', 'package.json'), path.join(root, 'tools', 'sitegen', 'package.json'));
  await fsp.symlink(path.join(repositoryRoot, 'tools', 'sitegen', 'node_modules'), path.join(root, 'tools', 'sitegen', 'node_modules'), 'dir');
  assert.equal(run('git', ['init', '-q'], root).status, 0);
  run('git', ['config', 'user.email', 'test@example.com'], root);
  run('git', ['config', 'user.name', 'Test'], root);
  return root;
}

async function write(root, relative, contents) {
  const file = path.join(root, relative);
  await fsp.mkdir(path.dirname(file), { recursive: true });
  await fsp.writeFile(file, contents);
}

const validInfo = `Name: Test\nshort-description: Short\nsummary: Long\nLanguage: C++\nCreator: Test\nVersion: "1.0"\nStatus: WIP\n`;

test('staged validator ignores invalid unstaged edits', async t => {
  const root = await repositoryFixture(t);
  await write(root, 'releases/42_test/info.yaml', validInfo);
  await write(root, 'releases/42_test/README.md', '# Test');
  await write(root, 'releases/42_test/card.uf2', Buffer.from([0, 1, 2, 255]));
  run('git', ['add', '.'], root);
  assert.equal(run('git', ['commit', '-qm', 'base'], root).status, 0);

  await write(root, 'releases/42_test/info.yaml', `${validInfo}tags: [utility]\n`);
  run('git', ['add', 'releases/42_test/info.yaml'], root);
  await write(root, 'releases/42_test/info.yaml', 'Name: [broken\n');

  const result = run(process.execPath, ['tools/sitegen/src/validate/validateStaged.js'], root);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.match(result.stdout, /succeeded|succeeded with warnings/);
});

test('staged validator blocks invalid staged YAML even when working file is repaired', async t => {
  const root = await repositoryFixture(t);
  await write(root, 'releases/42_test/info.yaml', validInfo);
  await write(root, 'releases/42_test/README.md', '# Test');
  await write(root, 'releases/42_test/card.uf2', 'firmware');
  run('git', ['add', '.'], root);
  assert.equal(run('git', ['commit', '-qm', 'base'], root).status, 0);

  await write(root, 'releases/42_test/info.yaml', 'Name: [broken\n');
  run('git', ['add', 'releases/42_test/info.yaml'], root);
  await write(root, 'releases/42_test/info.yaml', validInfo);

  const result = run(process.execPath, ['tools/sitegen/src/validate/validateStaged.js'], root);
  assert.equal(result.status, 1, result.stderr || result.stdout);
  assert.match(`${result.stdout}\n${result.stderr}`, /YAML|Commit blocked/);
});
