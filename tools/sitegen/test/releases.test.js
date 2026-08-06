// Integration guard over the real card metadata in releases/.
//
// Legacy cards legitimately carry validation errors (missing required fields),
// so this does not assert "clean" — it asserts the invariants the site build
// depends on: every info.yaml parses, and no validation rule ever crashes.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseSource } from '../src/validate/parseSource.js';
import { validateInfoYaml } from '../src/validate/validateInfoYaml.js';

const releasesDir = fileURLToPath(new URL('../../../releases/', import.meta.url));
const infoFiles = fs.readdirSync(releasesDir)
  .map(dir => path.join(releasesDir, dir, 'info.yaml'))
  .filter(f => fs.existsSync(f));

test('releases/ contains info.yaml files to validate', () => {
  assert.ok(infoFiles.length > 20, `only found ${infoFiles.length}`);
});

test('every releases/*/info.yaml parses and validates without rule crashes', () => {
  for (const file of infoFiles) {
    const source = parseSource(fs.readFileSync(file, 'utf8'), file);
    assert.equal(source.error, null, `${file}: ${source.error?.message}`);
    const result = validateInfoYaml(source);
    const crashes = result.diagnostics.filter(d => d.ruleId.startsWith('rule-crash:'));
    assert.deepEqual(crashes, [], `${file} crashed rules: ${crashes.map(c => c.message).join('; ')}`);
  }
});
