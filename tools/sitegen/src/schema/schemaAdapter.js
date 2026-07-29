import { infoYamlSchema } from './schemaDefinition.js';
import { normalizeYamlKey } from '../utils/strings.js';

function normalizePath(path) {
  return String(path || '').trim();
}

export function createSchemaAdapter(schema = infoYamlSchema) {
  const fieldMap = new Map(schema.fields.map(field => [normalizePath(field.path), field]));
  // Case/hyphen/space-insensitive lookup so author keys (`audio-sample`, `Audio Sample`)
  // resolve to the same canonical field. Aliases share a field's normalized key.
  const byNormalized = new Map();
  for (const field of schema.fields) {
    byNormalized.set(normalizeYamlKey(field.path), field);
    for (const alias of field.aliases || []) byNormalized.set(normalizeYamlKey(alias), field);
  }

  return {
    id: schema.id,
    version: schema.version,
    source: schema.source,
    listFields() {
      return schema.fields.slice();
    },
    getField(path) {
      return fieldMap.get(normalizePath(path)) || null;
    },
    getFieldByKey(key) {
      return byNormalized.get(normalizeYamlKey(key)) || null;
    },
    knownKeys() {
      return new Set(byNormalized.keys());
    },
    requiredFields() {
      return schema.fields.filter(field => field.required);
    },
  };
}

export function getInfoYamlSchemaAdapter() {
  return createSchemaAdapter(infoYamlSchema);
}