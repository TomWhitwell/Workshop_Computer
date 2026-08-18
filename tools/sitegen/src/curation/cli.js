import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import YAML, { Pair, Scalar, YAMLMap } from 'yaml';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');
const FLAIRS_FILE = path.join(__dirname, 'flairs.yml');
const DISCOVERY_FILE = path.join(__dirname, 'discovery.yml');
const VALID_LAYOUTS = new Set(['grid', 'list', 'lead', 'video-lead', 'video-strip']);

function slugify(value) {
  return String(value ?? '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
}

function readDocument(file) {
  const source = fs.readFileSync(file, 'utf8');
  const document = YAML.parseDocument(source, { prettyErrors: true });
  if (document.errors.length) {
    throw new Error(`${path.relative(ROOT, file)}: ${document.errors.map(error => error.message).join('\n')}`);
  }
  return document;
}

function releaseCards() {
  return fs.readdirSync(RELEASES_DIR, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && fs.existsSync(path.join(RELEASES_DIR, entry.name, 'info.yaml')))
    .map(entry => {
      const id = entry.name;
      let title = id.replaceAll('_', ' ');
      try {
        const info = YAML.parse(fs.readFileSync(path.join(RELEASES_DIR, id, 'info.yaml'), 'utf8')) || {};
        title = String(info.Name || info.name || title).trim() || title;
      } catch {
        // The metadata validator reports malformed info.yaml separately. A card
        // still needs a curation entry even while its metadata is being fixed.
      }
      return { id, title };
    })
    .sort((a, b) => a.id.localeCompare(b.id, undefined, { numeric: true }));
}

export function validate(flairs, discovery, cards) {
  const errors = [];
  const warnings = [];
  const available = Array.isArray(flairs.available_flairs) ? flairs.available_flairs : [];
  if (!Array.isArray(flairs.available_flairs)) errors.push('flairs.yml: available_flairs must be a list');

  const vocabulary = new Map();
  const definedIds = new Set();
  const definedLabels = new Set();
  for (const [index, flair] of available.entries()) {
    if (!flair || typeof flair !== 'object' || Array.isArray(flair)) {
      errors.push(`flairs.yml: available_flairs[${index}] must be an object`);
      continue;
    }
    const id = slugify(flair.id);
    const label = slugify(flair.label || flair.id);
    if (!id) errors.push(`flairs.yml: available_flairs[${index}] needs an id`);
    if (id && definedIds.has(id)) errors.push(`flairs.yml: flair id "${id}" is defined more than once`);
    if (label && definedLabels.has(label)) errors.push(`flairs.yml: flair label "${flair.label || flair.id}" is defined more than once`);
    definedIds.add(id);
    definedLabels.add(label);
    for (const alias of new Set([id, label])) {
      if (!alias) continue;
      if (vocabulary.has(alias) && vocabulary.get(alias) !== id) {
        errors.push(`flairs.yml: flair alias "${alias}" is ambiguous`);
      } else {
        vocabulary.set(alias, id);
      }
    }
  }

  const assignments = flairs.assignments;
  if (!assignments || typeof assignments !== 'object' || Array.isArray(assignments)) {
    errors.push('flairs.yml: assignments must be a mapping');
  }
  const assignmentEntries = assignments && typeof assignments === 'object' && !Array.isArray(assignments)
    ? Object.entries(assignments)
    : [];
  for (const [cardId, assigned] of assignmentEntries) {
    if (!Array.isArray(assigned)) {
      errors.push(`flairs.yml: assignment for ${cardId} must be a list`);
      continue;
    }
    const seen = new Set();
    for (const flair of assigned) {
      const alias = slugify(typeof flair === 'object' ? flair.id || flair.label : flair);
      if (!vocabulary.has(alias)) errors.push(`flairs.yml: ${cardId} uses unknown flair "${String(flair)}"`);
      const id = vocabulary.get(alias) || alias;
      if (seen.has(id)) errors.push(`flairs.yml: ${cardId} assigns flair "${id}" more than once`);
      seen.add(id);
    }
  }

  const cardIds = new Set(cards.map(card => card.id));
  const assignmentIds = new Set(assignmentEntries.map(([id]) => id));
  for (const card of cards) {
    if (!assignmentIds.has(card.id)) errors.push(`flairs.yml: missing assignment for ${card.id}`);
  }
  for (const id of assignmentIds) {
    if (cardIds.has(id)) continue;
    if (Array.isArray(assignments[id]) && assignments[id].length === 0) {
      warnings.push(`flairs.yml: stale empty assignment for ${id}; run \`npm run sync-curation\` to remove it`);
    } else {
      errors.push(`flairs.yml: stale assignment for ${id}`);
    }
  }

  const referencedCards = [];
  const addCards = (values, location) => {
    if (!Array.isArray(values)) return;
    for (const id of values) referencedCards.push({ id: String(id), location });
  };
  addCards(discovery?.hero?.featured, 'discovery.yml: hero.featured');
  if (discovery?.hero?.layout && !VALID_LAYOUTS.has(discovery.hero.layout)) errors.push(`discovery.yml: hero has unknown layout "${discovery.hero.layout}"`);
  for (const flair of discovery?.hero?.hide_flairs || []) {
    if (!vocabulary.has(slugify(flair))) errors.push(`discovery.yml: hero.hide_flairs uses unknown flair "${flair}"`);
  }
  for (const [name, embed] of Object.entries(discovery?.embeds || {})) addCards(embed?.cards, `discovery.yml: embeds.${name}.cards`);

  const shelfIds = new Set();
  if (discovery?.shelves != null && !Array.isArray(discovery.shelves)) errors.push('discovery.yml: shelves must be a list');
  for (const [index, shelf] of (Array.isArray(discovery?.shelves) ? discovery.shelves : []).entries()) {
    const location = `discovery.yml: shelves[${index}]`;
    if (!shelf || typeof shelf !== 'object') {
      errors.push(`${location} must be an object`);
      continue;
    }
    if (!shelf.id) errors.push(`${location} needs an id`);
    if (shelf.id && shelfIds.has(shelf.id)) errors.push(`${location} duplicates shelf id "${shelf.id}"`);
    shelfIds.add(shelf.id);
    if (Array.isArray(shelf.cards) && Array.isArray(shelf.cards_from_flairs)) errors.push(`${location} cannot use both cards and cards_from_flairs`);
    if (!Array.isArray(shelf.cards) && !Array.isArray(shelf.cards_from_flairs)) errors.push(`${location} must use cards or cards_from_flairs`);
    addCards(shelf.cards, `${location}.cards`);
    for (const flair of shelf.cards_from_flairs || []) {
      if (!vocabulary.has(slugify(flair))) errors.push(`${location}.cards_from_flairs uses unknown flair "${flair}"`);
    }
    for (const flair of shelf.hide_flairs || []) {
      if (!vocabulary.has(slugify(flair))) errors.push(`${location}.hide_flairs uses unknown flair "${flair}"`);
    }
    if (shelf.layout && !VALID_LAYOUTS.has(shelf.layout)) errors.push(`${location} has unknown layout "${shelf.layout}"`);
    if (shelf.limit != null && (!Number.isInteger(shelf.limit) || shelf.limit < 1)) errors.push(`${location}.limit must be a positive integer`);
  }
  for (const reference of referencedCards) {
    if (!cardIds.has(reference.id)) errors.push(`${reference.location} references unknown card ${reference.id}`);
  }

  return { errors, warnings };
}

export function synchronize(flairsDocument, cards) {
  let assignments = flairsDocument.get('assignments', true);
  if (!(assignments instanceof YAMLMap)) throw new Error('flairs.yml: assignments must be a mapping before it can be synchronized');
  const cardIds = new Set(cards.map(card => card.id));
  const removed = [];
  assignments.items = assignments.items.filter(pair => {
    const id = String(pair.key?.value ?? pair.key);
    const assigned = pair.value?.toJSON?.() ?? pair.value;
    if (cardIds.has(id) || !Array.isArray(assigned) || assigned.length !== 0) return true;
    removed.push(id);
    return false;
  });
  const existing = new Set(assignments.items.map(pair => String(pair.key?.value ?? pair.key)));
  const added = [];
  for (const card of cards) {
    if (existing.has(card.id)) continue;
    const key = new Scalar(card.id);
    key.commentBefore = ` ${card.title}`;
    assignments.add(new Pair(key, []));
    existing.add(card.id);
    added.push(card.id);
  }
  return { added, removed };
}

function report(result) {
  for (const warning of result.warnings) console.warn(`warning: ${warning}`);
  for (const error of result.errors) console.error(`error: ${error}`);
}

function main() {
  const mode = process.argv[2] || 'check';
  if (!['check', 'sync'].includes(mode)) throw new Error('Usage: node src/curation/cli.js [check|sync]');

  const cards = releaseCards();
  const flairsDocument = readDocument(FLAIRS_FILE);
  if (mode === 'sync') {
    const { added, removed } = synchronize(flairsDocument, cards);
    if (added.length || removed.length) fs.writeFileSync(FLAIRS_FILE, String(flairsDocument));
    if (added.length) console.log(`Added ${added.length} curation assignment(s): ${added.join(', ')}`);
    if (removed.length) console.log(`Removed ${removed.length} stale empty curation assignment(s): ${removed.join(', ')}`);
    if (!added.length && !removed.length) console.log('Curation assignments are already synchronized.');
  }

  const currentFlairs = readDocument(FLAIRS_FILE).toJS() || {};
  const discovery = readDocument(DISCOVERY_FILE).toJS() || {};
  const result = validate(currentFlairs, discovery, cards);
  report(result);
  if (result.errors.length) {
    const missingAssignments = result.errors.filter(error => error.startsWith('flairs.yml: missing assignment for '));
    if (mode === 'check' && missingAssignments.length) {
      console.error('Curation assignments are out of sync. Run `npm run sync-curation` from the repository root, then commit the updated flairs.yml.');
    }
    console.error(`Curation check failed with ${result.errors.length} error(s).`);
    process.exitCode = 1;
  } else {
    console.log(`Curation check passed: ${cards.length} cards, ${Object.keys(currentFlairs.assignments || {}).length} assignments, ${(currentFlairs.available_flairs || []).length} flairs.`);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    main();
  } catch (error) {
    console.error(`error: ${error.message}`);
    process.exitCode = 1;
  }
}
