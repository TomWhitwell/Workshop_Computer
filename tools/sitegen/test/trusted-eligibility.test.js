import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { assessPullRequest } from '../src/validate/trustedEligibility.js';
import { listTree, writeTreeFiles } from '../src/validate/gitTree.js';

const validInfo = `Name: Test\nshort-description: Short\nsummary: Long\nLanguage: C++\nCreator: Test\nVersion: "1.0"\nStatus: WIP\n`;

async function repository(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-trusted-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const git = (...args) => {
    const result = spawnSync('git', args, { cwd: root, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim();
  };
  const write = async (relative, contents) => {
    const file = path.join(root, relative);
    await fs.mkdir(path.dirname(file), { recursive: true });
    await fs.writeFile(file, contents);
  };
  git('init', '-q');
  git('config', 'user.email', 'test@example.com');
  git('config', 'user.name', 'Test');
  await write('releases/42_card/info.yaml', validInfo);
  await write('releases/42_card/card.uf2', 'firmware');
  await write('releases/42_card/README.md', '# Card');
  await write('tools/sitegen/src/validate/mergeEligibility.js', '// validator');
  git('add', '.');
  git('commit', '-qm', 'base');
  const base = git('rev-parse', 'HEAD');

  /** Commit `edit` on a branch from base, then return the working tree to base. */
  async function propose(edit) {
    git('checkout', '-q', '--detach', base);
    await edit({ write, git });
    git('add', '-A');
    git('commit', '-qm', 'proposal');
    const head = git('rev-parse', 'HEAD');
    git('checkout', '-q', '--detach', base);
    return head;
  }
  return { root, base, propose };
}

const assess = (root, base, head, overrides = {}) => assessPullRequest({
  cwd: root, base, head, author: 'maker', association: 'CONTRIBUTOR', repo: 'owner/repo',
  committerLookup: async () => true, ...overrides,
});

test('a card update is judged from the PR commit while the working tree stays on base', async t => {
  const { root, base, propose } = await repository(t);
  const head = await propose(({ write }) => write('releases/42_card/info.yaml', `${validInfo}# tweak\n`));
  const { eligibility, errorCount } = await assess(root, base, head);
  assert.equal(errorCount, 0);
  assert.equal(eligibility.eligible, true, eligibility.reasons.join('\n'));
  assert.match(eligibility.basis, /maker has committed to 42_card/);

  const broken = await propose(({ write }) => write('releases/42_card/info.yaml', validInfo.replace('Creator: Test\n', '')));
  const rejected = await assess(root, base, broken);
  assert.ok(rejected.errorCount > 0);
  assert.equal(rejected.eligibility.eligible, false);
  assert.match(rejected.eligibility.reasons.join('\n'), /Validation reports/);
});

test('a PR that edits the validator is not eligible', async t => {
  const { root, base, propose } = await repository(t);
  const head = await propose(async ({ write }) => {
    await write('releases/42_card/info.yaml', `${validInfo}# tweak\n`);
    await write('tools/sitegen/src/validate/mergeEligibility.js', 'export const evaluateMergeEligibility = () => ({ eligible: true });');
  });
  const { eligibility } = await assess(root, base, head);
  assert.equal(eligibility.eligible, false);
  assert.match(eligibility.reasons.join('\n'), /outside releases\/<card>\/: tools\/sitegen\/src\/validate\/mergeEligibility\.js/);
});

test('symlinks in the card are not followed and make the PR ineligible', async t => {
  const { root, base, propose } = await repository(t);
  const secretDir = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-secret-'));
  t.after(() => fs.rm(secretDir, { recursive: true, force: true }));
  const secret = path.join(secretDir, 'token');
  await fs.writeFile(secret, 'Name: SECRET-VALUE\n');
  const head = await propose(async ({ git }) => {
    await fs.rm(path.join(root, 'releases/42_card/info.yaml'));
    await fs.symlink(secret, path.join(root, 'releases/42_card/info.yaml'));
    await fs.symlink('../../tools', path.join(root, 'releases/42_card/tools'));
    git('add', '-A');
  });
  const { eligibility, report } = await assess(root, base, head);
  assert.equal(eligibility.eligible, false);
  assert.match(eligibility.reasons.join('\n'), /symlink or submodule \(releases\/42_card\/info\.yaml, releases\/42_card\/tools\)/);
  assert.doesNotMatch(JSON.stringify({ eligibility, report }), /SECRET-VALUE/);
});

test('the PR cannot hide files from validation with .gitattributes', async t => {
  const { root, base, propose } = await repository(t);
  const head = await propose(async ({ write }) => {
    await write('releases/42_card/info.yaml', validInfo.replace('Creator: Test\n', ''));
    await write('releases/42_card/.gitattributes', 'info.yaml export-ignore\n');
  });
  const { eligibility, errorCount } = await assess(root, base, head);
  assert.ok(errorCount > 0, 'the export-ignored info.yaml is still validated');
  assert.equal(eligibility.eligible, false);
});

test('drafts, deletions and PRs without cards are not eligible', async t => {
  const { root, base, propose } = await repository(t);
  const update = await propose(({ write }) => write('releases/42_card/info.yaml', `${validInfo}# tweak\n`));
  assert.match((await assess(root, base, update, { draft: true })).eligibility.reasons.join('\n'), /draft/);

  const deletion = await propose(async () => fs.rm(path.join(root, 'releases/42_card'), { recursive: true }));
  assert.match((await assess(root, base, deletion)).eligibility.reasons.join('\n'), /Deletes card 42_card/);

  const tooling = await propose(({ write }) => write('tools/sitegen/src/validate/mergeEligibility.js', '// changed'));
  const result = await assess(root, base, tooling);
  assert.equal(result.report, null);
  assert.match(result.eligibility.reasons.join('\n'), /Changes no program card/);
});

test('an unknown committer history fails closed', async t => {
  const { root, base, propose } = await repository(t);
  const head = await propose(({ write }) => write('releases/42_card/info.yaml', `${validInfo}# tweak\n`));
  const { eligibility } = await assess(root, base, head, { committerLookup: async () => null });
  assert.match(eligibility.reasons.join('\n'), /Could not determine/);
});

test('writeTreeFiles writes regular files only, with their contents and modes', async t => {
  const { root, base } = await repository(t);
  const destination = await fs.mkdtemp(path.join(os.tmpdir(), 'workshop-tree-'));
  t.after(() => fs.rm(destination, { recursive: true, force: true }));
  const { files, special } = listTree(root, base, ['releases/42_card']);
  assert.deepEqual(files.map(entry => entry.path).sort(), ['releases/42_card/README.md', 'releases/42_card/card.uf2', 'releases/42_card/info.yaml']);
  assert.deepEqual(special, []);
  writeTreeFiles(root, files, destination);
  assert.equal(await fs.readFile(path.join(destination, 'releases/42_card/info.yaml'), 'utf8'), validInfo);
  assert.throws(() => writeTreeFiles(root, [{ ...files[0], path: '../escape' }], destination), /outside the tree/);
});

test('the trusted verdict applies size limits and the hold label', async t => {
  const { root, base, propose } = await repository(t);
  const big = await propose(({ write }) => write('releases/42_card/card.uf2', Buffer.alloc(17 * 1024 * 1024)));
  assert.match((await assess(root, base, big)).eligibility.reasons.join('\n'), /card\.uf2 is 17\.0 MB/);

  const update = await propose(({ write }) => write('releases/42_card/info.yaml', `${validInfo}# tweak\n`));
  assert.match((await assess(root, base, update, { labels: ['hold'] })).eligibility.reasons.join('\n'), /"hold" label/);
});

test('minified web code in the card needs review; readable web code does not', async t => {
  const { root, base, propose } = await repository(t);
  const readable = Array.from({ length: 80 }, (_, i) => `function update${i}(value) {\n  document.title = String(value);\n}`).join('\n');
  const minified = Array.from({ length: 80 }, (_, i) => `function n${i}(e){document.title=String(e)}`).join(';');
  const clean = await propose(({ write }) => write('releases/42_card/web/app.js', readable));
  assert.equal((await assess(root, base, clean)).eligibility.eligible, true);
  const packed = await propose(({ write }) => write('releases/42_card/web/app.js', minified));
  assert.match((await assess(root, base, packed)).eligibility.reasons.join('\n'),
    /Publishes minified script \(releases\/42_card\/web\/app\.js\); minified or compiled web code needs a maintainer's review/);
});

test('an oversized PR is judged from the tree listing without being read', async t => {
  const { root, base, propose } = await repository(t);
  const big = await propose(({ write }) => write('releases/42_card/card.uf2', Buffer.alloc(17 * 1024 * 1024)));
  const { report, eligibility } = await assess(root, base, big);
  assert.equal(report, null);
  assert.match(eligibility.reasons.join('\n'), /card\.uf2 is 17\.0 MB/);
});
