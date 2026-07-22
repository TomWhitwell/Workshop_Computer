import { execSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { debugLog } from './logger.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '../../../..');

let _trackedCache;

/**
 * Returns a Set of git-tracked file paths (repo-root-relative, POSIX) under
 * releases/, or null when git is unavailable. Cached for the process lifetime.
 * Used to restrict discovered firmware to committed files whose raw URL will
 * actually resolve (locally-built, untracked UF2s would 404).
 */
export function getTrackedFileSet() {
  if (_trackedCache !== undefined) return _trackedCache;
  try {
    const out = execSync('git -c core.quotepath=off ls-files -- releases', {
      cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
    });
    _trackedCache = new Set(out.split('\n').map(s => s.trim()).filter(Boolean));
  } catch (e) {
    debugLog('getTrackedFileSet failed:', e?.message || e);
    _trackedCache = null;
  }
  return _trackedCache;
}

export function detectRepoFromGit() {
  try {
    const url = execSync('git config --get remote.origin.url', { cwd: ROOT, encoding: 'utf8' }).trim();
    const m = url.match(/github\.com[:\/]([^\s]+?)(?:\.git)?$/i);
    if (m && m[1]) {
      return m[1].replace(/\.git$/i, '');
    }
  } catch (e) { debugLog('detectRepoFromGit failed:', e?.message || e); }
  return null;
}

export function detectRefFromGit() {
  try {
    const sha = execSync('git rev-parse HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (sha) return sha;
  } catch (e) { debugLog('detectRefFromGit sha failed:', e?.message || e); }
  try {
    const branch = execSync('git rev-parse --abbrev-ref HEAD', { cwd: ROOT, encoding: 'utf8' }).trim();
    if (branch && branch !== 'HEAD') return branch;
  } catch (e) { debugLog('detectRefFromGit branch failed:', e?.message || e); }
  return null;
}

export function getLastCommitDate(relPath) {
  try {
    // Returns ISO 8601 date (e.g. 2023-01-01T12:00:00+00:00) of last change to path
    const date = execSync(`git log -1 --format=%cI -- "${relPath}"`, { cwd: ROOT, encoding: 'utf8' }).trim();
    return date;
  } catch (e) {
    debugLog(`getLastCommitDate failed for ${relPath}:`, e?.message || e);
    return null;
  }
}

/**
 * Returns { first, last } short commit dates (YYYY-MM-DD) for a path, or '' when unavailable.
 * Mirrors the MTM importer's git_date(:first/:last) fallback used for created/updated metadata.
 */
export function getCommitDates(relPath) {
  try {
    const out = execSync(`git log --format=%cs -- "${relPath}"`, { cwd: ROOT, encoding: 'utf8' });
    const dates = out.split('\n').map(s => s.trim()).filter(Boolean);
    if (!dates.length) return { first: '', last: '' };
    return { first: dates[dates.length - 1], last: dates[0] };
  } catch (e) {
    debugLog(`getCommitDates failed for ${relPath}:`, e?.message || e);
    return { first: '', last: '' };
  }
}

/**
 * "Phil's method": the OLDEST surviving per-line author date in a file, via
 * `git blame`. A repo-wide bulk edit (e.g. a schema migration) rewrites most
 * lines to a recent date and clobbers the folder's last-commit date, but the
 * original lines keep their real author dates. Taking the minimum blame date is
 * therefore a bulk-edit-resistant estimate of when a file's content first
 * existed — a stable date floor for cards that don't declare one explicitly.
 * Returns a YYYY-MM-DD string, or '' when unavailable (e.g. untracked file).
 */
export function getOldestBlameDate(relPath) {
  try {
    const out = execSync(`git blame --date=short -- "${relPath}"`, {
      cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    const dates = out.match(/\d{4}-\d{2}-\d{2}/g);
    if (!dates || !dates.length) return '';
    return dates.reduce((a, b) => (a < b ? a : b));
  } catch (e) {
    debugLog(`getOldestBlameDate failed for ${relPath}:`, e?.message || e);
    return '';
  }
}

/**
 * The most recent commit date (YYYY-MM-DD) touching a card's release *content*
 * — everything in its folder except the bulk-edited metadata/docs (info.yaml
 * and README). Used as the "last updated" signal: a firmware or source commit
 * is a real update, while repo-wide metadata bulk edits (which clobber the
 * folder's last-commit date) only touch info.yaml/README and are excluded here.
 * So this advances on real releases and is otherwise stable. Returns '' when
 * the card has no content files (e.g. placeholder / external-only cards).
 */
export function getContentUpdatedDate(folderRel) {
  const f = String(folderRel || '').replace(/\/+$/, '');
  if (!f) return '';
  try {
    const out = execSync(
      `git log -1 --format=%cs -- "${f}" ":(exclude)${f}/info.yaml" ":(exclude,icase)${f}/readme.md"`,
      { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
    ).trim();
    return out || '';
  } catch (e) {
    debugLog(`getContentUpdatedDate failed for ${f}:`, e?.message || e);
    return '';
  }
}
