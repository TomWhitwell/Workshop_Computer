import test from 'node:test';
import assert from 'node:assert/strict';
import { resolvePreviewWebConfig } from '../src/utils/previewWeb.js';

test('author preview web metadata shows, updates, and hides the editor action', () => {
  assert.equal(resolvePreviewWebConfig({ Editor: 'https://example.com/editor' }).editorUrl, 'https://example.com/editor');
  assert.equal(resolvePreviewWebConfig({ Editor: 'web' }, { slug: '42-fixture' }).editorUrl, '../programs/42-fixture/web/index.html');
  assert.equal(resolvePreviewWebConfig({ Editor: 'dist', 'web-entry': 'app/start.html' }, { slug: '42-fixture' }).editorUrl, '../programs/42-fixture/web/app/start.html');
  assert.equal(resolvePreviewWebConfig({ Editor: 'none' }).editorUrl, '');
});

test('existing-card previews retain auto-discovered editors and reflect entry edits', () => {
  const discoveredWeb = {
    mode: 'local',
    editorUrl: 'https://pages.test/programs/42-fixture/web/index.html',
    siteSubdir: 'web',
    entry: 'index.html',
  };
  assert.equal(resolvePreviewWebConfig({}, { slug: '42-fixture', discoveredWeb }).editorUrl, discoveredWeb.editorUrl);
  assert.equal(resolvePreviewWebConfig({ 'web-entry': 'manager.html' }, { slug: '42-fixture', discoveredWeb }).editorUrl, '../programs/42-fixture/web/manager.html');
  assert.equal(resolvePreviewWebConfig({ Editor: 'none' }, { slug: '42-fixture', discoveredWeb }).editorUrl, '');
});