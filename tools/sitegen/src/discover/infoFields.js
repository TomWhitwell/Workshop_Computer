import path from 'node:path';

/** Normalize tags to a deduped list of lowercase slug-like strings. */
export function normalizeTags(raw) {
  let items = [];
  if (Array.isArray(raw)) {
    items = raw;
  } else if (typeof raw === 'string' && raw.trim()) {
    items = raw.split(/[,;]+/);
  }
  const seen = new Set();
  const out = [];
  for (const item of items) {
    const tag = String(item || '').trim().toLowerCase().replace(/\s+/g, '-');
    if (!tag || seen.has(tag)) continue;
    seen.add(tag);
    out.push(tag);
  }
  return out;
}

export function normalizeRepository(raw) {
  return String(raw || '').trim();
}

/**
 * Resolve audio-sample to a fetchable URL.
 * External http(s) URLs pass through; relative paths resolve via makeRawUrl under the card folder.
 */
export function resolveAudioSample(value, repoRelBase, makeRawUrl) {
  const s = String(value || '').trim();
  if (!s) return '';
  if (/^https?:\/\//i.test(s)) return s;
  const posixBase = path.posix.join(...String(repoRelBase).split(path.sep));
  const rel = s.replace(/^\/+/, '');
  return makeRawUrl(path.posix.normalize(path.posix.join(posixBase, rel)));
}
