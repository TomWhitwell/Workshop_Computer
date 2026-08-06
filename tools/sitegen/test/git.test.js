import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { getCommitDates, getContentUpdatedDate, getLastCommitDate, getOldestBlameDate } from '../src/utils/git.js';

test('Git path arguments never undergo shell expansion', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sitegen-git-'));
  const marker = path.join(dir, 'expanded');
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const hostile = `$(touch ${marker})`;
  getLastCommitDate(hostile);
  getCommitDates(hostile);
  getOldestBlameDate(hostile);
  getContentUpdatedDate(hostile);
  assert.equal(fs.existsSync(marker), false);
});