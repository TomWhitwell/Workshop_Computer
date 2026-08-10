import assert from 'node:assert/strict';
import test from 'node:test';
import YAML from 'yaml';
import { synchronize, validate } from '../src/curation/cli.js';

const discovery = { shelves: [] };

test('curation validation permits a stale empty assignment but rejects an assigned one', () => {
  const result = validate({
    available_flairs: [{ id: 'new' }],
    assignments: { current: [], deleted_empty: [], deleted_curated: ['new'] },
  }, discovery, [{ id: 'current', title: 'Current' }]);

  assert.ok(result.warnings.some(message => message.includes('stale empty assignment for deleted_empty')));
  assert.ok(!result.errors.some(message => message.includes('deleted_empty')));
  assert.ok(result.errors.some(message => message === 'flairs.yml: stale assignment for deleted_curated'));
});

test('curation synchronization removes only stale empty assignments', () => {
  const document = YAML.parseDocument(`
available_flairs:
  - id: new
assignments:
  current: []
  deleted_empty: []
  deleted_curated: [new]
`);
  const changes = synchronize(document, [
    { id: 'current', title: 'Current' },
    { id: 'added', title: 'Added card' },
  ]);

  assert.deepEqual(changes, { added: ['added'], removed: ['deleted_empty'] });
  assert.deepEqual(document.toJS().assignments, {
    current: [],
    deleted_curated: ['new'],
    added: [],
  });
});