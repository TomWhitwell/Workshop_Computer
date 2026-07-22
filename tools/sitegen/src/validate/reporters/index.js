// Reporters turn the shared diagnostic model into different outputs so the same
// validation results can serve the CLI, machine consumers, and GitHub Actions.
//
// Every reporter accepts an array of per-file results as returned by
// validateInfoYaml: { file, ok, errorCount, warningCount, diagnostics }.

function totals(results) {
  return results.reduce((acc, r) => {
    acc.errors += r.errorCount;
    acc.warnings += r.warningCount;
    acc.files += 1;
    acc.failed += r.ok ? 0 : 1;
    return acc;
  }, { errors: 0, warnings: 0, files: 0, failed: 0 });
}

function location(diag) {
  if (diag.line == null) return '';
  return diag.col == null ? `:${diag.line}` : `:${diag.line}:${diag.col}`;
}

/** Human-friendly terminal output, grouped by file. */
export function reportText(results) {
  const lines = [];
  for (const result of results) {
    if (!result.diagnostics.length) continue;
    lines.push(result.file);
    for (const diag of result.diagnostics) {
      const tag = diag.severity === 'error' ? 'error' : 'warning';
      const where = location(diag);
      const path = diag.path ? ` [${diag.path}]` : '';
      lines.push(`  ${tag}${where}${path} ${diag.message} (${diag.ruleId})`);
      if (diag.suggestion) lines.push(`    hint: ${diag.suggestion}`);
    }
  }
  const t = totals(results);
  lines.push('');
  lines.push(`${t.files} file(s), ${t.failed} failing — ${t.errors} error(s), ${t.warnings} warning(s).`);
  return lines.join('\n');
}

/** Machine-readable JSON for other tools / dashboards. */
export function reportJson(results) {
  return JSON.stringify({ summary: totals(results), results }, null, 2);
}

/**
 * GitHub Actions workflow-command annotations. Emitting these from a PR job
 * surfaces validation problems inline on the diff. (Consumer to be wired later;
 * the format is ready now.)
 */
export function reportGithub(results) {
  const lines = [];
  const esc = (s) => String(s).replace(/%/g, '%25').replace(/\r/g, '%0D').replace(/\n/g, '%0A');
  for (const result of results) {
    for (const diag of result.diagnostics) {
      const level = diag.severity === 'error' ? 'error' : 'warning';
      const props = [`file=${result.file}`];
      if (diag.line != null) props.push(`line=${diag.line}`);
      if (diag.col != null) props.push(`col=${diag.col}`);
      props.push(`title=info.yaml ${diag.ruleId}`);
      const detail = diag.path ? `${diag.path}: ${diag.message}` : diag.message;
      lines.push(`::${level} ${props.join(',')}::${esc(detail)}`);
    }
  }
  return lines.join('\n');
}

export const reporters = { text: reportText, json: reportJson, github: reportGithub };
