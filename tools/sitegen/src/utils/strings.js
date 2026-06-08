/** Lowercase info.yaml keys; strip spaces and hyphens for consistent lookup (demo-link → demolink). */
export function normalizeYamlKey(k) {
  return String(k || '').toLowerCase().replace(/[\s-]+/g, '');
}

export function slugify(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/(^-|-$)+/g, '');
}

export function parseDisplayFromFolder(folderName) {
  let number = '';
  let base = folderName;
  const m = folderName.match(/^(\d+)[-_\s]+(.+)$/);
  if (m) {
    number = m[1];
    base = m[2];
  }
  // Replace separators with single spaces, preserve original capitalization
  base = base.replace(/[_-]+/g, ' ').replace(/\s+/g, ' ').trim();
  return { number, title: base };
}

export function formatDisplayTitle(raw) {
  if (!raw) return '';
  let s = String(raw).trim();
  // Drop any numeric prefix like "00_", "03-", "11 "
  s = s.replace(/^(\d+)[-_\s]+/, '');
  // Replace underscores/dashes with spaces, collapse whitespace, preserve case
  s = s.replace(/[_-]+/g, ' ').replace(/\s+/g, ' ').trim();
  return s;
}

// Ensure version strings contain a dot for simple numeric versions
// e.g., "1" -> "1.0"; leave complex or non-numeric versions as-is
export function formatVersion(v) {
  const s = String(v ?? '').trim();
  if (!s) return '';
  if (s.includes('.')) return s;
  if (/^\d+$/.test(s)) return `${s}.0`;
  return s;
}
