// Near-duplicate detection for free-text metadata values (tags, Language,
// Status). Shared by the `similar-values` validator rule (CLI, PR checks, the
// author preview in the browser) and the `metadata-values` hand-merge tool, so
// keep it dependency-free apart from the browser-safe string helpers.

import { normalizeYamlKey } from './strings.js';

/** Fields whose values should stay consistent across cards. */
export const VALUE_FIELDS = [
  { key: 'tags', label: 'Tag', list: true },
  { key: 'Language', label: 'Language' },
  { key: 'Status', label: 'Status' },
];

/** The raw value of a top-level field, matching keys case-insensitively. */
function topLevel(data, key) {
  if (!data || typeof data !== 'object') return undefined;
  const wanted = normalizeYamlKey(key);
  for (const [k, v] of Object.entries(data)) {
    if (normalizeYamlKey(k) === wanted) return v;
  }
  return undefined;
}

/** A card's values for one field as trimmed, non-empty strings. */
export function fieldValues(data, field) {
  const raw = topLevel(data, field.key);
  if (raw == null) return [];
  const list = field.list ? (Array.isArray(raw) ? raw : String(raw).split(',')) : [raw];
  return [...new Set(list
    .filter(value => value != null && typeof value !== 'object')
    .map(value => String(value).trim())
    .filter(Boolean))];
}

/**
 * Index every card's values: `{ [field.key]: { [value]: [cardId, ...] } }`.
 * `entries` is `[{ id, data }]` where `data` is the parsed info.yaml.
 */
export function collectKnownValues(entries) {
  const known = Object.fromEntries(VALUE_FIELDS.map(field => [field.key, {}]));
  for (const { id, data } of entries || []) {
    for (const field of VALUE_FIELDS) {
      for (const value of fieldValues(data, field)) {
        (known[field.key][value] ??= []).push(id);
      }
    }
  }
  return known;
}

/**
 * Spelling-insensitive comparison key: case, punctuation and separators
 * (except the + and # in C++ / C#), "and", UK/US -ise/-ize, and a trailing
 * plural "s" (but not the "s" of chorus or bass) are ignored.
 */
export function similarityKey(value) {
  return String(value || '')
    .toLowerCase()
    .split(/[^a-z0-9+#]+/)
    .filter(token => token && token !== 'and')
    .join('')
    .replace(/iz(e|er|es|ed|ing|ation)/g, 'is$1')
    .replace(/(?<=[a-z]{2}[^su])s$/, '');
}

/** Optimal-string-alignment edit distance, giving up once it exceeds `max`. */
function editDistance(a, b, max) {
  if (Math.abs(a.length - b.length) > max) return max + 1;
  let prevPrev = null;
  let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
  for (let i = 1; i <= a.length; i++) {
    const row = [i];
    let rowMin = i;
    for (let j = 1; j <= b.length; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      let d = Math.min(prev[j] + 1, row[j - 1] + 1, prev[j - 1] + cost);
      if (prevPrev && i > 1 && j > 1 && a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) {
        d = Math.min(d, prevPrev[j - 2] + 1);
      }
      row.push(d);
      rowMin = Math.min(rowMin, d);
    }
    if (rowMin > max) return max + 1;
    prevPrev = prev;
    prev = row;
  }
  return prev[b.length];
}

/** True when two distinct values look like spellings of the same thing. */
export function areSimilar(a, b) {
  if (a === b) return false;
  const ka = similarityKey(a);
  const kb = similarityKey(b);
  if (!ka || !kb) return false;
  if (ka === kb) return true;
  // Agent-noun / verb forms: bitcrush ~ bitcrusher, pitch-shift ~ pitch-shifter.
  const [short, long] = ka.length <= kb.length ? [ka, kb] : [kb, ka];
  if (short.length >= 4 && long.startsWith(short) && ['r', 'er', 'ed', 'ing'].includes(long.slice(short.length))) return true;
  // Typos: allow ~15% edits on longer values; short values must match exactly.
  if (long.length < 5) return false;
  const max = Math.max(1, Math.floor(long.length * 0.15));
  return editDistance(ka, kb, max) <= max;
}

/** Values in `candidates` (an array of strings) similar to `value`. */
export function findSimilar(value, candidates) {
  return candidates.filter(candidate => areSimilar(value, candidate));
}

/**
 * Partition values into groups of mutually reachable similar values (singletons
 * included). Groups and members keep the input order.
 */
export function groupSimilar(values) {
  const parent = values.map((_, i) => i);
  const find = i => (parent[i] === i ? i : (parent[i] = find(parent[i])));
  for (let i = 0; i < values.length; i++) {
    for (let j = i + 1; j < values.length; j++) {
      if (areSimilar(values[i], values[j])) parent[find(j)] = find(i);
    }
  }
  const groups = new Map();
  values.forEach((value, i) => {
    const root = find(i);
    if (!groups.has(root)) groups.set(root, []);
    groups.get(root).push(value);
  });
  return [...groups.values()];
}
