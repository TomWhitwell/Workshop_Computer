// Validate a parsed info.yaml source against the author-facing schema.
//
// This is the single validation entry point shared by every consumer (site
// build, author preview, and future PR/commit-time CI). It depends only on the
// schema adapter and the rule set — never on the normalized card model or any
// HTML rendering — so it is safe to run anywhere.

import { getInfoYamlSchemaAdapter } from '../schema/schemaAdapter.js';
import { normalizeYamlKey } from '../utils/strings.js';
import { allRules } from './rules/index.js';

/** Build a normalized-key index of a raw object's own top-level entries. */
function buildNormalized(data) {
  const normalized = {};
  for (const [key, value] of Object.entries(data || {})) {
    normalized[normalizeYamlKey(key)] = { key, value };
  }
  return normalized;
}

/** Assemble the rule context: schema, data lookups, and source positions. */
function makeContext(source, schema) {
  const normalized = buildNormalized(source.data || {});
  const keyLines = source.keyLines || {};
  const entry = (key) => normalized[normalizeYamlKey(key)];
  return {
    file: source.file,
    raw: source.raw,
    data: source.data || {},
    schema,
    normalized,
    keyLines,
    normKey: normalizeYamlKey,
    entry,
    get: (key) => { const e = entry(key); return e ? e.value : undefined; },
    lineFor: (key) => keyLines[normalizeYamlKey(key)] || null,
  };
}

/** Fill in file + source line/col defaults on a rule-produced diagnostic. */
function finalizeDiagnostic(diag, ctx, ruleId) {
  const out = {
    severity: diag.severity || 'error',
    ruleId: diag.ruleId || ruleId,
    file: ctx.file,
    path: diag.path || '',
    message: diag.message || '',
  };
  // Anchor to the offending top-level key's source line when the rule didn't
  // supply its own coordinates.
  const anchorKey = diag.key || diag.path;
  const pos = diag.line != null ? { line: diag.line, col: diag.col } : ctx.lineFor(anchorKey);
  if (pos && pos.line != null) { out.line = pos.line; out.col = pos.col; }
  if (diag.suggestion) out.suggestion = diag.suggestion;
  return out;
}

/**
 * Validate a parsed source payload (from parseSource / parseSourceFile).
 * Returns { file, ok, errorCount, warningCount, diagnostics }.
 */
export function validateInfoYaml(source, opts = {}) {
  const schema = opts.schema || getInfoYamlSchemaAdapter();
  const rules = opts.rules || allRules;
  const diagnostics = [];

  // A syntax error means we cannot meaningfully run structural rules.
  if (source.error) {
    diagnostics.push({ ...source.error, file: source.file });
    return summarize(source.file, diagnostics);
  }

  const ctx = makeContext(source, schema);
  for (const rule of rules) {
    let produced = [];
    try {
      produced = rule.check(ctx) || [];
    } catch (err) {
      produced = [{ severity: 'error', ruleId: `rule-crash:${rule.id}`, path: '',
        message: `Rule "${rule.id}" failed: ${err.message}` }];
    }
    for (const diag of produced) diagnostics.push(finalizeDiagnostic(diag, ctx, rule.id));
  }
  return summarize(source.file, diagnostics);
}

function summarize(file, diagnostics) {
  const errorCount = diagnostics.filter(d => d.severity === 'error').length;
  const warningCount = diagnostics.filter(d => d.severity === 'warning').length;
  return { file, ok: errorCount === 0, errorCount, warningCount, diagnostics };
}
