// Render one current validation report for GitHub Actions.
// Usage: node githubReportCli.js INFO_REPORT.json RULES_REPORT.json [SUMMARY.md]

import fs from 'node:fs';
import { reportGithub, reportMarkdown, reportOtherRulesGithub, reportText } from './reporters/index.js';

const [reportFile, rulesFile, summaryFile] = process.argv.slice(2);
if (!reportFile || !rulesFile) {
  console.error('Usage: node githubReportCli.js INFO_REPORT.json RULES_REPORT.json [SUMMARY.md]');
  process.exit(2);
}

const parsed = JSON.parse(fs.readFileSync(reportFile, 'utf8'));
const results = Array.isArray(parsed) ? parsed : parsed.results || [];
const otherRules = JSON.parse(fs.readFileSync(rulesFile, 'utf8'));
console.log(reportGithub(results));
console.log(reportOtherRulesGithub(otherRules));
console.log(reportText(results));
if (summaryFile) fs.writeFileSync(summaryFile, reportMarkdown(results, otherRules));