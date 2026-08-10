import { normalizeYamlKey } from './strings.js';

function readUf2Field(raw) {
  for (const [key, value] of Object.entries(raw || {})) {
    if (normalizeYamlKey(key) === 'uf2') return value;
  }
  return undefined;
}

function hostFromUrl(value) {
  try {
    return new URL(value).hostname.replace(/^www\./i, '');
  } catch {
    return '';
  }
}

function nameFromUrl(value) {
  try {
    const url = new URL(value);
    return url.pathname.split('/').filter(Boolean).pop() || url.hostname || value;
  } catch {
    return value;
  }
}

/** Resolve author-page UF2 metadata into the download actions used by card details. */
export function resolvePreviewUf2Downloads(raw, availableDownloads = []) {
  const field = readUf2Field(raw);
  if (field == null || (Array.isArray(field) && field.length === 0)) {
    return availableDownloads.map(item => ({ ...item }));
  }

  const available = new Map();
  for (const item of availableDownloads) {
    const path = String(item.path || '').replaceAll('\\', '/').toLowerCase();
    if (path) available.set(path, item);
  }

  const resolved = [];
  for (const entry of Array.isArray(field) ? field : [field]) {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) continue;
    const externalUrl = String(entry.download?.url || '').trim();
    if (/^https?:\/\//i.test(externalUrl)) {
      const item = {
        name: String(entry.name || '').trim() || nameFromUrl(externalUrl),
        url: externalUrl,
        host: hostFromUrl(externalUrl),
        external: true,
      };
      const sha256 = String(entry.download?.sha256 || '').trim().toLowerCase();
      if (sha256) item.sha256 = sha256;
      if (entry.download?.flashable === true) item.flashable = true;
      resolved.push(item);
      continue;
    }

    const path = String(entry.path || '').trim().replaceAll('\\', '/');
    const match = available.get(path.toLowerCase());
    if (match) resolved.push({ ...match, name: String(entry.name || '').trim() || match.name });
  }
  return resolved;
}