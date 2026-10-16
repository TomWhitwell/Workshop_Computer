import { execFileSync } from 'node:child_process';
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
    const out = execFileSync('git', ['-c', 'core.quotepath=off', 'ls-files', '--', 'releases'], {
      cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
    });
    _trackedCache = new Set(out.split('\n').map(s => s.trim()).filter(Boolean));
  } catch (e) {
    debugLog('getTrackedFileSet failed:', e?.message || e);
    _trackedCache = null;
  }
  return _trackedCache;
}

export function getLastCommitDate(relPath) {
  try {
    // Returns ISO 8601 date (e.g. 2023-01-01T12:00:00+00:00) of last change to path
    const date = execFileSync('git', ['log', '-1', '--format=%cI', '--', String(relPath)], { cwd: ROOT, encoding: 'utf8' }).trim();
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
    const out = execFileSync('git', ['log', '--format=%cs', '--', String(relPath)], { cwd: ROOT, encoding: 'utf8' });
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
    const out = execFileSync('git', ['blame', '--date=short', '--', String(relPath)], {
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
 * The date (YYYY-MM-DD) a card was first published on the main line: the
 * earliest first-parent commit (i.e. the merge/squash onto main, not the PR's
 * own commit dates) where the card folder exists and its info.yaml is not a
 * draft. `isDraftSource(yamlText)` decides draft state; a missing info.yaml
 * (pre-metadata cards) counts as published. Draft state can only change at the
 * folder's first commit or at a commit touching info.yaml, so only those
 * revisions are read (in one `git cat-file --batch` call). Returns '' when
 * never published or when git is unavailable.
 */
export function getPublishedDate(folderRel, isDraftSource) {
  const f = String(folderRel || '').replace(/\/+$/, '');
  if (!f) return '';
  const infoPath = `${f}/info.yaml`;
  const run = (args, input) => execFileSync('git', args, {
    cwd: ROOT, input, stdio: [input == null ? 'ignore' : 'pipe', 'pipe', 'ignore'], maxBuffer: 64 * 1024 * 1024,
  });
  try {
    // One log over the folder; each commit line is followed by the files it changed.
    const commits = [];
    for (const line of run(['-c', 'core.quotepath=off', 'log', '--first-parent', '--reverse', '--format=@%H %cs', '--name-only', '--', f]).toString('utf8').split('\n')) {
      if (line.startsWith('@')) {
        const [sha, date] = line.slice(1).split(' ');
        commits.push({ sha, date, touchesInfo: false });
      } else if (line === infoPath && commits.length) {
        commits[commits.length - 1].touchesInfo = true;
      }
    }
    if (!commits.length) return '';
    const revisions = commits.filter((c, i) => i === 0 || c.touchesInfo);
    // Batch output per object: "<oid> blob <size>\n<content>\n", or "<rev> missing\n".
    const out = run(['cat-file', '--batch'], revisions.map(({ sha }) => `${sha}:${infoPath}\n`).join(''));
    let pos = 0;
    for (const { date } of revisions) {
      const eol = out.indexOf(10, pos);
      const header = out.toString('utf8', pos, eol).split(' ');
      pos = eol + 1;
      let source = null;
      if (header[1] === 'blob') {
        const size = Number(header[2]);
        source = out.toString('utf8', pos, pos + size);
        pos += size + 1;
      }
      // A missing info.yaml at this revision is a legacy card: published.
      if (source == null || !isDraftSource(source)) return date;
    }
    return '';
  } catch (e) {
    debugLog(`getPublishedDate failed for ${f}:`, e?.message || e);
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
    const out = execFileSync(
      'git',
      ['log', '-1', '--format=%cs', '--', f, `:(exclude)${f}/info.yaml`, `:(exclude,icase)${f}/readme.md`],
      { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
    ).trim();
    return out || '';
  } catch (e) {
    debugLog(`getContentUpdatedDate failed for ${f}:`, e?.message || e);
    return '';
  }
}
