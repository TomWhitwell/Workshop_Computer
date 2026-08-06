// Presentation-only curation for the program-card discovery experience.
//
// Ported from MTM_Newsite_2022 _data/program_cards/{tags,discovery}.yml. These
// are moderator-authored files (flair vocabulary + per-card assignments +
// shelf layout) and are intentionally kept out of releases/*/info.yaml.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import YAML from 'yaml';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function loadYaml(name) {
  try {
    return YAML.parse(fs.readFileSync(path.join(__dirname, name), 'utf8')) || {};
  } catch (error) {
    throw new Error(`Unable to load curation/${name}: ${error.message}`, { cause: error });
  }
}

function slugify(value) {
  return String(value == null ? '' : value).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
}

const flairsData = loadYaml('flairs.yml');
const discovery = loadYaml('discovery.yml');

const rawAvailable = Array.isArray(flairsData.available_flairs) ? flairsData.available_flairs : [];
const assignments = (flairsData.assignments && typeof flairsData.assignments === 'object') ? flairsData.assignments : {};

// Index the tag vocabulary by both id-slug and label-slug for case-insensitive lookup.
const flairBySlug = new Map();
const availableFlairs = rawAvailable.map(flair => {
  const entry = {
    id: slugify(flair.id),
    label: flair.label || flair.id,
    color: flair.color || '',
    textColor: flair.text_color || '',
    description: flair.description || '',
  };
  flairBySlug.set(entry.id, entry);
  flairBySlug.set(slugify(entry.label), entry);
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
    const entry = flairBySlug.get(key) || { id: key, label: String(tag), color: '', textColor: '' };
    if (!entry.id || seen.has(entry.id)) continue;
    seen.add(entry.id);
    out.push(entry);
  }
  return out;
}

/** Card ids assigned a given flair (by id or label), in curation order. */
export function cardIdsForFlair(flairSlug) {
  const wanted = slugify(flairSlug);
  const ids = [];
  for (const [cardId] of Object.entries(assignments)) {
    if (resolveFlair(cardId).some(t => t.id === wanted)) ids.push(cardId);
  }
  return ids;
}

export const curation = {
  availableFlairs,
  assignments,
  discovery,
  resolveFlair,
  cardIdsForFlair,
  slugify,
};
