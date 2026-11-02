import { execFile, execFileSync } from 'node:child_process';
import { promisify } from 'node:util';
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


const execFileAsync = promisify(execFile);

let _historyCache;

// Release folder key (`releases/<folder>`) for a repo-relative path.
function folderKey(relPath) {
  return String(relPath || '').replace(/\\/g, '/').replace(/\/+$/, '');
}

function releaseFolderOf(file) {
  const parts = file.split('/');
  return parts.length > 2 && parts[0] === 'releases' ? `releases/${parts[1]}` : '';
}

function isMetadataFile(folder, file) {
  const name = file.slice(folder.length + 1);
  return name === 'info.yaml' || name.toLowerCase() === 'readme.md';
}

/**
 * Per-folder Git history for everything under releases/, built from two
 * repo-wide log walks and one `cat-file --batch`, then cached for the process
 * lifetime. Spawning a path-limited `git log` per card costs ~40ms each, so
 * walking the history once is far cheaper than ~5 lookups for every card.
 *
 * `--no-renames` lists both sides of a move, and the combined diff lists a
 * merge's own changes, so each folder sees the same commits as a log limited
 * to that folder. Returns null when git is unavailable.
 */
function getReleaseHistory() {
  if (_historyCache !== undefined) return _historyCache;
  const run = (args, input) => execFileSync('git', ['-c', 'core.quotepath=off', ...args], {
    cwd: ROOT, input, stdio: [input == null ? 'ignore' : 'pipe', 'pipe', 'ignore'], maxBuffer: 256 * 1024 * 1024,
  });
  try {
    const folders = new Map();
    const entry = key => {
      if (!folders.has(key)) folders.set(key, { first: '', last: '', contentDate: '', mainline: [], revisions: [] });
      return folders.get(key);
    };

    // Newest first, as `git log -- <folder>` would list them.
    let date = '';
    let touched = new Set();
    for (const line of run(['log', '--no-renames', '--diff-merges=combined', '--format=@%cs', '--name-only', '--', 'releases']).toString('utf8').split('\n')) {
      if (line.startsWith('@')) {
        date = line.slice(1);
        touched = new Set();
        continue;
      }
      const key = releaseFolderOf(line);
      if (!key) continue;
      const folder = entry(key);
      if (!touched.has(key)) {
        touched.add(key);
        if (!folder.last) folder.last = date;
        folder.first = date;
      }
      if (!folder.contentDate && !isMetadataFile(key, line)) folder.contentDate = date;
    }

    // Main-line commits, oldest first, noting which ones touch info.yaml.
    let commit = null;
    for (const line of run(['log', '--first-parent', '--reverse', '--no-renames', '--format=@%H %cs', '--name-only', '--', 'releases']).toString('utf8').split('\n')) {
      if (line.startsWith('@')) {
        const [sha, cs] = line.slice(1).split(' ');
        commit = { sha, date: cs };
        continue;
      }
      const key = releaseFolderOf(line);
      if (!key || !commit) continue;
      const { mainline } = entry(key);
      const last = mainline[mainline.length - 1];
      if (last?.sha !== commit.sha) mainline.push({ ...commit, touchesInfo: false });
      if (line === `${key}/info.yaml`) mainline[mainline.length - 1].touchesInfo = true;
    }

    // Draft state can only change at a folder's first main-line commit or at a
    // commit touching its info.yaml, so read just those revisions, all at once.
    const wanted = [];
    for (const [key, folder] of folders) {
      folder.mainline.forEach((c, i) => {
        if (i === 0 || c.touchesInfo) wanted.push({ folder, date: c.date, rev: `${c.sha}:${key}/info.yaml` });
      });
      delete folder.mainline;
    }
    if (wanted.length) {
      // Batch output per object: "<oid> blob <size>\n<content>\n", or "<rev> missing\n".
      const out = run(['cat-file', '--batch'], wanted.map(({ rev }) => `${rev}\n`).join(''));
      let pos = 0;
      for (const { folder, date: revDate } of wanted) {
        const eol = out.indexOf(10, pos);
        const header = out.toString('utf8', pos, eol).split(' ');
        pos = eol + 1;
        let source = null;
        if (header[1] === 'blob') {
          const size = Number(header[2]);
          source = out.toString('utf8', pos, pos + size);
          pos += size + 1;
        }
        folder.revisions.push({ date: revDate, source });
      }
    }
    _historyCache = folders;
  } catch (e) {
    debugLog('getReleaseHistory failed:', e?.message || e);
    _historyCache = null;
  }
  return _historyCache;
}

/**
 * Returns { first, last } short commit dates (YYYY-MM-DD) for a release folder, or '' when unavailable.
 * Mirrors the MTM importer's git_date(:first/:last) fallback used for created/updated metadata.
 */
export function getCommitDates(folderRel) {
  const folder = getReleaseHistory()?.get(folderKey(folderRel));
  return { first: folder?.first || '', last: folder?.last || '' };
}

/**
 * "Phil's method": the OLDEST surviving per-line author date in a file, via
 * `git blame`. A repo-wide bulk edit (e.g. a schema migration) rewrites most
 * lines to a recent date and clobbers the folder's last-commit date, but the
 * original lines keep their real author dates. Taking the minimum blame date is
 * therefore a bulk-edit-resistant estimate of when a file's content first
 * existed — a stable date floor for cards that don't declare one explicitly.
 * Returns a YYYY-MM-DD string, or '' when unavailable (e.g. untracked file).
 * Asynchronous so callers can run several blames at once.
 */
export async function getOldestBlameDate(relPath) {
  try {
    const { stdout } = await execFileAsync('git', ['blame', '--date=short', '--', String(relPath)], {
      cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
    });
    const dates = stdout.match(/\d{4}-\d{2}-\d{2}/g);
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
 * (pre-metadata cards) counts as published. Returns '' when never published or
 * when git is unavailable.
 */
export function getPublishedDate(folderRel, isDraftSource) {
  const folder = getReleaseHistory()?.get(folderKey(folderRel));
  for (const { date, source } of folder?.revisions || []) {
    // A missing info.yaml at this revision is a legacy card: published.
    if (source == null || !isDraftSource(source)) return date;
  }
  return '';
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
  return getReleaseHistory()?.get(folderKey(folderRel))?.contentDate || '';
}
