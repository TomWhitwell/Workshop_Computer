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

export function normalizeDiscussion(raw) {
  return String(raw || '').trim();
}

/** Card metadata still under author review; defaults to false when omitted. */
export function normalizeDraft(raw) {
  if (raw === true || raw === 1) return true;
  if (raw === false || raw === 0) return false;
  const s = String(raw ?? '').trim().toLowerCase();
  if (s === 'true' || s === 'yes' || s === '1') return true;
  if (s === 'false' || s === 'no' || s === '0' || s === '') return false;
  return Boolean(raw);
}

/** Normalize optional contact block (email, website, social platform links). */
export function normalizeContact(raw) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;

  const email = String(raw.email || '').trim();
  const website = String(raw.website || '').trim();

  let social = null;
  const socialRaw = raw.social;
  if (socialRaw && typeof socialRaw === 'object' && !Array.isArray(socialRaw)) {
    const links = {};
    for (const [platform, url] of Object.entries(socialRaw)) {
      const key = String(platform || '').trim().toLowerCase().replace(/[\s-]+/g, '');
      const href = String(url || '').trim();
      if (key && href) links[key] = href;
    }
    if (Object.keys(links).length) social = links;
  }

  if (!email && !website && !social) return null;
  return {
    ...(email ? { email } : {}),
    ...(website ? { website } : {}),
    ...(social ? { social } : {}),
  };
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
