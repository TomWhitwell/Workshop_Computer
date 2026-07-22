// Presentation-only curation for the program-card discovery experience.
//
// Ported from MTM_Newsite_2022 _data/program_cards/{tags,discovery}.yml. These
// are moderator-authored files (tag vocabulary + per-card flair assignments +
// shelf layout) and are intentionally kept out of releases/*/info.yaml.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function loadYaml(name) {
  try {
    return YAML.parse(fs.readFileSync(path.join(__dirname, name), 'utf8')) || {};
  } catch {
    return {};
  }
}

function slugify(value) {
  return String(value == null ? '' : value).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
}

const tagsData = loadYaml('tags.yml');
const discovery = loadYaml('discovery.yml');

const rawAvailable = Array.isArray(tagsData.available_tags) ? tagsData.available_tags : [];
const assignments = (tagsData.assignments && typeof tagsData.assignments === 'object') ? tagsData.assignments : {};

// Index the tag vocabulary by both id-slug and label-slug for case-insensitive lookup.
const tagBySlug = new Map();
const availableTags = rawAvailable.map(tag => {
  const entry = {
    id: slugify(tag.id),
    label: tag.label || tag.id,
    color: tag.color || '',
    textColor: tag.text_color || '',
    description: tag.description || '',
  };
  tagBySlug.set(entry.id, entry);
  tagBySlug.set(slugify(entry.label), entry);
  return entry;
});

/**
 * Resolve a card's curated flair tags to full tag objects.
 * Flair comes only from the curation assignments (not info.yaml tags), matching
 * the MTM moderator model.
 */
export function resolveFlair(cardId) {
  const assigned = Array.isArray(assignments[cardId]) ? assignments[cardId] : [];
  const out = [];
  const seen = new Set();
  for (const tag of assigned) {
    const key = slugify(typeof tag === 'object' ? (tag.id || tag.label) : tag);
    const entry = tagBySlug.get(key) || { id: key, label: String(tag), color: '', textColor: '' };
    if (!entry.id || seen.has(entry.id)) continue;
    seen.add(entry.id);
    out.push(entry);
  }
  return out;
}

/** Card ids assigned a given flair tag (by id or label), in curation order. */
export function cardIdsForTag(tagSlug) {
  const wanted = slugify(tagSlug);
  const ids = [];
  for (const [cardId] of Object.entries(assignments)) {
    if (resolveFlair(cardId).some(t => t.id === wanted)) ids.push(cardId);
  }
  return ids;
}

export const curation = {
  availableTags,
  assignments,
  discovery,
  resolveFlair,
  cardIdsForTag,
  slugify,
};
