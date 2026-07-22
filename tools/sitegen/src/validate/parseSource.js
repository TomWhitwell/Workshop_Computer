// Parse raw info.yaml source into a validation-ready payload.
//
// Keeps the raw text alongside the parsed data so reporters can point at source
// lines, and surfaces YAML syntax errors as structured diagnostics (with
// line/column when the parser provides them) rather than throwing.
//
// This module is browser-safe: it imports only the `yaml` parser and pure
// string helpers, so it can be shipped to the client-side author preview. The
// filesystem-backed loader lives in `readSource.js` (Node only).

import YAML from 'yaml';
import { normalizeYamlKey } from '../utils/strings.js';

/** Convert a character offset in `raw` to a 1-based {line, col}. */
function offsetToLineCol(raw, offset) {
  if (typeof offset !== 'number' || offset < 0) return null;
  let line = 1;
  let col = 1;
  for (let i = 0; i < offset && i < raw.length; i += 1) {
    if (raw[i] === '\n') { line += 1; col = 1; } else { col += 1; }
  }
  return { line, col };
}

/** Map each top-level key to its source {line, col}, keyed by normalized name. */
function collectKeyLines(doc, raw) {
  const keyLines = {};
  const items = doc?.contents?.items;
  if (!Array.isArray(items)) return keyLines;
  for (const item of items) {
    const keyNode = item?.key;
    if (!keyNode) continue;
    const name = keyNode.value != null ? String(keyNode.value) : '';
    const start = Array.isArray(keyNode.range) ? keyNode.range[0] : undefined;
    const pos = offsetToLineCol(raw, start);
    if (name) keyLines[normalizeYamlKey(name)] = pos || { line: undefined, col: undefined };
  }
  return keyLines;
}

/** Parse raw YAML text. Never throws; syntax errors become `result.error`. */
export function parseSource(raw, file = '<memory>') {
  const result = { file, raw, data: null, keyLines: {}, error: null };
  let doc;
  try {
    doc = YAML.parseDocument(raw, { prettyErrors: true });
  } catch (err) {
    result.error = {
      severity: 'error', ruleId: 'yaml-syntax', file, path: '',
      message: `YAML parse failed: ${err.message}`,
    };
    return result;
  }
  if (doc.errors && doc.errors.length) {
    const err = doc.errors[0];
    const pos = err.linePos && err.linePos[0];
    result.error = {
      severity: 'error', ruleId: 'yaml-syntax', file, path: '',
      message: err.message,
      line: pos?.line, col: pos?.col,
    };
    return result;
  }
  result.data = doc.toJS() ?? {};
  result.keyLines = collectKeyLines(doc, raw);
  return result;
}
