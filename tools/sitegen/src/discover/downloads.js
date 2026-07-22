import path from 'node:path';
import { fsAsync as fs, toPosix, fileExists, sha256File } from '../utils/fs.js';
import { getTrackedFileSet } from '../utils/git.js';

// Read a string property from an object by a case-insensitive key, trimmed.
function readKeyCi(obj, name) {
  if (!obj || typeof obj !== 'object') return '';
  const target = String(name).toLowerCase();
  for (const [k, v] of Object.entries(obj)) {
    if (k.toLowerCase() === target && typeof v === 'string') return v.trim();
  }
  return '';
}

// A sensible label for an external URL: its filename, else host, else the URL.
function nameFromUrl(u) {
  try {
    const parsed = new URL(u);
    const base = parsed.pathname.split('/').filter(Boolean).pop();
    return base || parsed.hostname || u;
  } catch {
    return u;
  }
}

// The bare host of a URL (without a leading www.), for display on the tile.
function hostFromUrl(u) {
  try {
    return new URL(u).hostname.replace(/^www\./i, '');
  } catch {
    return '';
  }
}

export async function discoverDownloads(absReleaseDir, repoRelBase, makeRawUrl) {
  const downloads = [];
  const uf2Items = [];

  async function walk(dir) {
    const ents = await fs.readdir(dir, { withFileTypes: true });
    for (const ent of ents) {
      const fullPath = path.join(dir, ent.name);
      if (ent.isDirectory()) {
        await walk(fullPath);
        continue;
      }
      if (!/\.(uf2|zip|bin|hex)$/i.test(ent.name)) continue;
      const relFromRelease = path.relative(absReleaseDir, fullPath);
      const relFromRepoRoot = toPosix(path.join(repoRelBase, relFromRelease));
      let mtime = 0;
      try {
        mtime = (await fs.stat(fullPath)).mtimeMs;
      } catch {}
      const url = makeRawUrl(relFromRepoRoot);
      const item = { name: ent.name, rel: relFromRepoRoot, url, mtime };
      downloads.push(item);
      if (/\.uf2$/i.test(ent.name)) {
        const segs = relFromRelease.split(path.sep);
        const dirs = segs.slice(0, -1).map(s => s.toLowerCase());
        const inWeb = dirs.includes('web');
        const isOld = dirs.some(p => p === 'old' || p === 'old versions' || p === 'older versions');
        uf2Items.push({ ...item, abs: fullPath, relRelease: toPosix(relFromRelease), inWeb, isOld });
      }
    }
  }

  await walk(absReleaseDir);

  // Restrict to committed/tracked UF2s when git is available: this drops
  // locally-built firmware that was never pushed (its raw URL would 404). When
  // git is unavailable, keep everything (matches the previous behaviour).
  const tracked = getTrackedFileSet();
  let uf2s = tracked ? uf2Items.filter(it => tracked.has(it.rel)) : uf2Items.slice();

  // Every tracked UF2 under the card (release-relative), so authors can point a
  // curated `uf2.path` at any committed firmware and tools can validate it.
  const trackedUf2 = uf2s.map(it => it.relRelease);

  // Ignore a firmware under a `web/` folder when an identically-named UF2 also
  // exists outside web/ (avoids duplicate links); keep uniquely-named web ones.
  const nonWebNames = new Set(uf2s.filter(it => !it.inWeb).map(it => it.name.toLowerCase()));
  uf2s = uf2s.filter(it => !(it.inWeb && nonWebNames.has(it.name.toLowerCase())));

  // Order: current firmware (not in old/older-versions) first, then newest by
  // mtime, then by name for a stable, natural ordering.
  uf2s.sort((a, b) => {
    if (a.isOld !== b.isOld) return a.isOld ? 1 : -1;
    if (b.mtime !== a.mtime) return b.mtime - a.mtime;
    return a.name.localeCompare(b.name, undefined, { numeric: true });
  });

  // Record a sha256 for each served firmware (metadata for UI; no fetching).
  const uf2Downloads = [];
  for (const it of uf2s) {
    const entry = { name: it.name, url: it.url, rel: it.rel };
    const sha256 = await sha256File(it.abs);
    if (sha256) entry.sha256 = sha256;
    uf2Downloads.push(entry);
  }
  const latestUf2 = uf2Downloads[0] || null;
  return { downloads, latestUf2, uf2Downloads, trackedUf2 };
}

/**
 * Resolve a release-relative path case-insensitively against the real files on
 * disk, returning the actual on-disk relative path (POSIX) or null when no file
 * matches. Prefers an exact segment match, then a case-insensitive one, so the
 * emitted raw URL always uses the true filename casing.
 */
async function resolveInsensitivePath(baseAbs, relPath) {
  const parts = String(relPath).split(/[\\/]+/).filter(Boolean);
  let curAbs = baseAbs;
  const realParts = [];
  for (const part of parts) {
    let entries;
    try {
      entries = await fs.readdir(curAbs, { withFileTypes: true });
    } catch {
      return null;
    }
    const match = entries.find(e => e.name === part)
      || entries.find(e => e.name.toLowerCase() === part.toLowerCase());
    if (!match) return null;
    realParts.push(match.name);
    curAbs = path.join(curAbs, match.name);
  }
  return realParts.length ? realParts.join('/') : null;
}

/**
 * Build a curated UF2 download list from an author-specified `uf2:` field. When
 * present this fully replaces auto-discovery, letting authors trim noise and
 * annotate firmware. Each entry is an object:
 *   { path?, name?, download?: { url?, sha256? } }
 * An entry needs EITHER a `path` (repo firmware, resolved case-insensitively;
 * missing file is an error) OR a `download.url` (an external/mirror/store link
 * that opens in a new tab and is tagged as external). For repo files the build
 * computes the sha256; external links carry no hash. Returns { uf2Downloads,
 * errors }.
 */
export async function curateUf2Downloads(uf2Field, absReleaseDir, repoRelBase, makeRawUrl) {
  const uf2Downloads = [];
  const errors = [];
  const entries = Array.isArray(uf2Field) ? uf2Field : [uf2Field];
  for (const entry of entries) {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
      errors.push('uf2 entry must be an object with a "path" or "download.url".');
      continue;
    }
    const download = entry.download && typeof entry.download === 'object' && !Array.isArray(entry.download)
      ? entry.download : null;
    const externalUrl = readKeyCi(download, 'url');
    const relPath = typeof entry.path === 'string' ? entry.path.trim() : '';

    // External link (store/mirror): no repo file required.
    if (externalUrl) {
      const item = {
        name: (typeof entry.name === 'string' && entry.name.trim()) || nameFromUrl(externalUrl),
        url: externalUrl,
        host: hostFromUrl(externalUrl),
        external: true,
      };
      const authorHash = readKeyCi(download, 'sha256');
      if (authorHash) item.sha256 = authorHash;
      uf2Downloads.push(item);
      continue;
    }

    // Otherwise a repo-hosted firmware file is required.
    if (!relPath) {
      errors.push('uf2 entry needs a "path" or a "download.url".');
      continue;
    }
    const realRel = await resolveInsensitivePath(absReleaseDir, relPath);
    if (!realRel) {
      errors.push(`uf2 path not found: ${relPath}`);
      continue;
    }
    const relFromRepoRoot = toPosix(path.join(repoRelBase, realRel));
    const item = {
      name: (typeof entry.name === 'string' && entry.name.trim()) || path.basename(realRel),
      url: makeRawUrl(relFromRepoRoot),
      rel: relFromRepoRoot,
    };
    const sha256 = await sha256File(path.join(absReleaseDir, realRel));
    if (sha256) item.sha256 = sha256;
    uf2Downloads.push(item);
  }
  return { uf2Downloads, errors };
}

