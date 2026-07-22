// CLI wrapper around the shared validator.
//
//   node src/validate/cli.js [options] [paths...]
//
// With no path arguments it validates every releases/<card>/info.yaml. Paths
// may be individual info.yaml files or card folders. Designed to be called from
// npm (`npm run validate-info`), and later from GitHub Actions / commit hooks.
//
// Options:
//   --json            emit machine-readable JSON
//   --github          emit GitHub Actions annotations (::error / ::warning)
//   --strict          treat warnings as failures (non-zero exit)
//   --quiet           only print the summary line (text format)

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { fsAsync as fs, fileExists, listSubdirs } from '../utils/fs.js';
import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';
import { reportText, reportJson, reportGithub } from './reporters/index.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');

function parseArgs(argv) {
  const opts = { json: false, github: false, strict: false, quiet: false, paths: [] };
  for (const arg of argv) {
    if (arg === '--json') opts.json = true;
    else if (arg === '--github') opts.github = true;
    else if (arg === '--strict') opts.strict = true;
    else if (arg === '--quiet') opts.quiet = true;
    else if (arg.startsWith('--')) { console.error(`Unknown option: ${arg}`); process.exit(2); }
    else opts.paths.push(arg);
  }
  return opts;
}

/** Resolve a CLI argument to zero or more info.yaml file paths. */
async function resolveInput(arg) {
  // Glob-ish patterns (releases/**/info.yaml) are handled by scanning releases.
  if (arg.includes('*')) return scanReleases();
  // Accept both cwd-relative and repo-root-relative paths so the documented
  // `npm run validate-info -- releases/<card>/info.yaml` works even though npm
  // runs scripts from the package directory.
  const candidates = [path.resolve(process.cwd(), arg), path.resolve(ROOT, arg)];
  for (const abs of candidates) {
    if (!(await fileExists(abs))) continue;
    const stat = await fs.stat(abs);
    if (stat.isDirectory()) {
      const candidate = path.join(abs, 'info.yaml');
      return (await fileExists(candidate)) ? [candidate] : [];
    }
    return [abs];
  }
  return [];
}

/** Every releases/<card>/info.yaml. */
async function scanReleases() {
  const out = [];
  let folders = [];
  try { folders = await listSubdirs(RELEASES_DIR); } catch { return out; }
  for (const folder of folders) {
    const candidate = path.join(RELEASES_DIR, folder, 'info.yaml');
    if (await fileExists(candidate)) out.push(candidate);
  }
  return out;
}

async function collectFiles(opts) {
  if (!opts.paths.length) return scanReleases();
  const files = new Set();
  for (const arg of opts.paths) {
    for (const file of await resolveInput(arg)) files.add(file);
  }
  return [...files];
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const files = await collectFiles(opts);
  if (!files.length) {
    console.error('No info.yaml files found to validate.');
    process.exit(2);
  }

  const results = [];
  for (const file of files) {
    const source = await parseSourceFile(file);
    // Report paths relative to repo root for stable, readable output.
    source.file = path.relative(ROOT, file) || file;
    results.push(validateInfoYaml(source));
  }

  if (opts.json) console.log(reportJson(results));
  else if (opts.github) console.log(reportGithub(results));
  else if (opts.quiet) {
    const errors = results.reduce((n, r) => n + r.errorCount, 0);
    const warnings = results.reduce((n, r) => n + r.warningCount, 0);
    console.log(`${results.length} file(s) — ${errors} error(s), ${warnings} warning(s).`);
  } else console.log(reportText(results));

  const hasErrors = results.some(r => r.errorCount > 0);
  const hasWarnings = results.some(r => r.warningCount > 0);
  process.exit(hasErrors || (opts.strict && hasWarnings) ? 1 : 0);
}

main().catch(err => { console.error(err); process.exit(2); });
