import { normalizeYamlKey } from './strings.js';

function valuesByNormalizedKey(raw) {
  return Object.fromEntries(Object.entries(raw || {}).map(([key, value]) => [normalizeYamlKey(key), value]));
}

function safeEntry(value) {
  const entry = String(value || '').trim().replaceAll('\\', '/');
  if (!entry || entry.startsWith('/') || entry.includes('\0')) return entry ? '' : 'index.html';
  const parts = entry.split('/');
  if (parts.some(part => !part || part === '.' || part === '..')) return '';
  return parts.map(encodeURIComponent).join('/');
}

function localConfig(slug, entry) {
  const safe = safeEntry(entry);
  if (!safe) return { mode: 'none', editorUrl: '', siteSubdir: 'web', entry: '' };
  return {
    mode: 'local',
    editorUrl: `../programs/${encodeURIComponent(slug || 'new-card')}/web/${safe}`,
    siteSubdir: 'web',
    entry: decodeURIComponent(safe),
  };
}

/** Resolve author-page metadata into the editor action shown by the live preview. */
export function resolvePreviewWebConfig(raw, { slug = 'new-card', discoveredWeb = null } = {}) {
  const values = valuesByNormalizedKey(raw);
  const editor = String(values.editor ?? '').trim();
  const entry = String(values.webentry ?? '').trim();

  if (editor.toLowerCase() === 'none') return { mode: 'none', editorUrl: '', siteSubdir: 'web', entry: '' };
  if (/^https:\/\//i.test(editor)) return { mode: 'external', editorUrl: editor, siteSubdir: 'web', entry: '' };
  if (editor === 'web' || editor === 'dist') return localConfig(slug, entry || 'index.html');
  if (editor) return { mode: 'none', editorUrl: '', siteSubdir: 'web', entry: '' };

  if (discoveredWeb?.mode === 'local' && discoveredWeb.editorUrl) {
    return entry ? localConfig(slug, entry) : { ...discoveredWeb };
  }
  if (discoveredWeb?.mode === 'external' && discoveredWeb.editorUrl) return { ...discoveredWeb };
  return { mode: 'none', editorUrl: '', siteSubdir: 'web', entry: '' };
}