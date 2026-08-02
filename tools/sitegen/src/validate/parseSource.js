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

/** Map keys and sequence entries to source positions for schema diagnostics. */
function collectSourceLines(doc, raw) {
  const keyLines = {};
  const pathLines = {};
  const visit = (node, path = []) => {
    if (!Array.isArray(node?.items)) return;
    for (let index = 0; index < node.items.length; index += 1) {
      const item = node.items[index];
      if (item?.key) {
        const name = item.key.value != null ? String(item.key.value) : '';
        if (!name) continue;
        const nextPath = [...path, name];
        const pos = offsetToLineCol(raw, item.key.range?.[0]);
        if (pos) pathLines[nextPath.join('.')] = pos;
        if (path.length === 0) keyLines[normalizeYamlKey(name)] = pos || { line: undefined, col: undefined };
        visit(item.value, nextPath);
      } else {
        const nextPath = [...path, String(index)];
        const pos = offsetToLineCol(raw, item?.range?.[0]);
        if (pos) pathLines[nextPath.join('.')] = pos;
        visit(item, nextPath);
      }
    }
  };
  visit(doc?.contents);
  return { keyLines, pathLines };
}

/** Parse raw YAML text. Never throws; syntax errors become `result.error`. */
export function parseSource(raw, file = '<memory>') {
  const result = { file, raw, data: null, keyLines: {}, pathLines: {}, error: null, errors: [] };
  let doc;
  try {
    doc = YAML.parseDocument(raw, { prettyErrors: true });
  } catch (err) {
    result.error = {
      severity: 'error', ruleId: 'yaml-syntax', file, path: '',
      message: `YAML parse failed: ${err.message}`,
    };
    result.errors = [result.error];
    return result;
  }
  if (doc.errors && doc.errors.length) {
    result.errors = doc.errors.map(err => {
      const pos = err.linePos && err.linePos[0];
      return {
        severity: 'error', ruleId: 'yaml-syntax', file, path: '',
        message: err.message,
        line: pos?.line, col: pos?.col,
      };
    });
    result.error = result.errors[0];
    return result;
  }
  result.data = doc.toJS() ?? {};
  const lines = collectSourceLines(doc, raw);
  result.keyLines = lines.keyLines;
  result.pathLines = lines.pathLines;
  return result;
}
