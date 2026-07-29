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
  const t = totals(results);
  lines.push(`::notice title=info.yaml validation::${t.files} file(s), ${t.failed} failing — ${t.errors} error(s), ${t.warnings} warning(s).`);
  return lines.join('\n');
}

function escapeMarkdown(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/\|/g, '\\|')
    .replace(/\r?\n/g, '<br>');
}

function code(value) {
  const text = String(value ?? '').replace(/`/g, '\\`');
  return text ? `\`${text}\`` : '—';
}

function displayInfoPath(file) {
  const normalized = String(file || '').replaceAll('\\', '/');
  const marker = 'releases/';
  const index = normalized.indexOf(marker);
  return index >= 0 ? normalized.slice(index + marker.length) : normalized;
}

/** Rich Markdown used by the PR comment and GitHub job summary. */
export function reportMarkdown(results, otherRules = null) {
  const t = totals(results);
  const otherErrors = Number(otherRules?.errorCount || 0);
  const otherWarnings = Number(otherRules?.warningCount || 0);
  const errorCount = t.errors + otherErrors;
  const warningCount = t.warnings + otherWarnings;
  const status = errorCount
    ? { icon: '❌', label: 'failed' }
    : warningCount
      ? { icon: '⚠️', label: 'succeeded with warnings' }
      : { icon: '✅', label: 'succeeded' };
  const lines = [
    `## ${status.icon} Program card PR validation ${status.label}`,
    '',
    `_PR validation is intended for maintainers, not card authors. If you're a card author and don't understand this report, don't worry about it!_`,
    '',
    `**${t.files} info.yaml file(s) checked · ${errorCount} error(s) · ${warningCount} warning(s)**`,
    '',
  ];
  const trigger = otherRules?.trigger;
  if (trigger?.changedPaths?.length) {
    lines.push('**Triggered by:**', '');
    for (const change of trigger.changedPaths) {
      const label = change.oldPath
        ? `${change.status} ${change.oldPath} → ${change.path}`
        : `${change.status} ${change.path}`;
      lines.push(`- ${code(label)}`);
    }
    if (trigger.affectedReleases?.length) {
      lines.push('', `**Affected release directories:** ${trigger.affectedReleases.map(code).join(', ')}`, '');
    } else {
      lines.push('');
    }
  }
  if (!results.length) {
    lines.push('No `info.yaml` file was added or modified.', '');
  } else if (!results.some(result => result.diagnostics.length)) {
    lines.push('✅ All changed `info.yaml` files validate cleanly.', '');
  } else {
    for (const result of results) {
      lines.push(`### ${code(displayInfoPath(result.file))}`, '');
      if (!result.diagnostics.length) {
        lines.push('✅ This file validates cleanly.', '');
        continue;
      }
      lines.push(
        '| Severity | Field | Rule | Message |',
        '|:--|:--|:--|:--|',
      );
      for (const diagnostic of result.diagnostics) {
        const severity = diagnostic.severity === 'error' ? '❌ Error' : '⚠️ Warning';
        lines.push(`| ${severity} | ${code(diagnostic.path)} | ${code(diagnostic.ruleId)} | ${escapeMarkdown(diagnostic.message)} |`);
      }
      lines.push('');
    }
  }

  lines.push('## Other rules', '');
  const ruleDiagnostics = otherRules?.diagnostics || [];
  if (!ruleDiagnostics.length) {
    lines.push('✅ All submission rules passed.', '');
  } else {
    lines.push(
      '| Severity | Affected path | Rule | Message |',
      '|:--|:--|:--|:--|',
    );
    for (const diagnostic of ruleDiagnostics) {
      const severity = diagnostic.severity === 'error' ? '❌ Error' : '⚠️ Warning';
      lines.push(`| ${severity} | ${code(diagnostic.file)} | ${code(diagnostic.ruleId)} | ${escapeMarkdown(diagnostic.message)} |`);
    }
    lines.push('');
  }
  return lines.join('\n');
}

export function reportOtherRulesGithub(report) {
  const lines = [];
  const esc = value => String(value).replace(/%/g, '%25').replace(/\r/g, '%0D').replace(/\n/g, '%0A');
  for (const diagnostic of report?.diagnostics || []) {
    const level = diagnostic.severity === 'error' ? 'error' : 'warning';
    lines.push(`::${level} file=${diagnostic.file},title=Submission rule ${diagnostic.ruleId}::${esc(diagnostic.message)}`);
  }
  return lines.join('\n');
}

export const reporters = { text: reportText, json: reportJson, github: reportGithub, markdown: reportMarkdown };
