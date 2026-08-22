import path from 'node:path';
import { fsAsync as fs, ensureDir } from '../utils/fs.js';
import { normalizeYamlKey } from '../utils/strings.js';

const SKIP_DIRS = new Set(['node_modules', '.git', '.github']);
const SKIP_FILES = new Set(['package-lock.json', 'tsconfig.json', 'vite.config.ts', 'vite.config.js']);
const LOCAL_EDITOR_DIRS = new Set(['web', 'dist']);
const LOCAL_PREVIEW_BASE = 'http://localhost:5173/';

/** GitHub Pages base URL for a user/org project site. */
export function githubPagesBase(repoSlug) {
  const [owner, name] = String(repoSlug || '').split('/');
  if (!owner || !name) return 'https://tomwhitwell.github.io/Workshop_Computer/';
  return `https://${owner.toLowerCase()}.github.io/${name}/`;
}

/**
 * Public site origin used for copied (local) web-editor links.
 * Preview builds (`npm run dev`) pin those links to the Vite origin so they
 * stay on localhost instead of the production Pages URL. SITE_BASE_URL still
 * wins when set, including during a preview build.
 */
export function resolveSiteBase({ configured = '', preview = false, repoSlug = '' } = {}) {
  const value = String(configured || '').trim();
  if (value) {
    const url = new URL(value);
    if (!/^https?:$/.test(url.protocol)) throw new Error('SITE_BASE_URL must be an HTTP(S) URL.');
    url.search = '';
    url.hash = '';
    if (!url.pathname.endsWith('/')) url.pathname += '/';
    return url.href;
  }
  if (preview) return LOCAL_PREVIEW_BASE;
  return githubPagesBase(repoSlug);
}

function normalizeRelFolder(loc) {
  const s = String(loc || '').trim().replace(/^\/+/, '').replace(/\/+$/, '');
  return s || 'web';
}

function isExternalUrl(s) {
  try {
    return new URL(String(s || '').trim()).protocol === 'https:';
  } catch {
    return false;
  }
}

async function folderExists(dir) {
  try {
    const stat = await fs.lstat(dir);
    return stat.isDirectory() && !stat.isSymbolicLink();
  } catch {
    return false;
  }
}

function safeRelativeFile(value) {
  const raw = String(value || '').trim().replaceAll('\\', '/');
  if (!raw || raw.includes('\0') || path.posix.isAbsolute(raw)) return '';
  const normalized = path.posix.normalize(raw);
  if (normalized === '..' || normalized.startsWith('../') || normalized.includes('/../')) return '';
  return normalized;
}

async function containedRegularFile(root, relative) {
  const safe = safeRelativeFile(relative);
  if (!safe) return null;
  const candidate = path.join(root, ...safe.split('/'));
  try {
    const stat = await fs.lstat(candidate);
    if (!stat.isFile() || stat.isSymbolicLink()) return null;
    const [realRoot, realFile] = await Promise.all([fs.realpath(root), fs.realpath(candidate)]);
    if (realFile !== realRoot && !realFile.startsWith(realRoot + path.sep)) return null;
    return safe;
  } catch {
    return null;
  }
}

async function resolveEntryFile(srcDir, webEntry) {
  if (webEntry) return (await containedRegularFile(srcDir, webEntry)) || '';
  return (await containedRegularFile(srcDir, 'index.html')) || '';
}

async function resolveLocal(cardAbsPath, slug, pagesBaseUrl, relFolder, webEntry, externalFallback) {
  const folder = normalizeRelFolder(relFolder).toLowerCase();
  if (!LOCAL_EDITOR_DIRS.has(folder)) {
    throw new Error(`Local Editor must be "web" or "dist", got "${relFolder}".`);
  }
  const copySrc = path.join(cardAbsPath, folder);
  if (!(await folderExists(copySrc))) {
    return { mode: 'none', editorUrl: '', copySrc: null, siteSubdir: 'web', entry: '' };
  }
  const [realCard, realSource] = await Promise.all([fs.realpath(cardAbsPath), fs.realpath(copySrc)]);
  if (!realSource.startsWith(realCard + path.sep)) throw new Error(`Local Editor folder escapes its release directory: ${relFolder}`);
  const entry = await resolveEntryFile(copySrc, webEntry);
  if (webEntry && !entry) throw new Error(`Web entry must be a contained regular file: ${webEntry}`);
  if (!entry && externalFallback && isExternalUrl(externalFallback)) return { mode: 'external', editorUrl: externalFallback, copySrc: null, siteSubdir: 'web', entry: '' };
  const editorUrl = entry ? `${pagesBaseUrl}programs/${slug}/web/${entry}` : '';
  return { mode: 'local', editorUrl, copySrc, siteSubdir: 'web', entry };
}

/**
 * Resolve Editor field and optional web/ auto-detect for GitHub Pages deploy.
 * Editor: none | https://… | web | dist | (empty + web/ folder → local Pages URL)
 */
export async function resolveWebConfig(raw, cardAbsPath, slug, pagesBaseUrl) {
  const out = {};
  for (const [k, v] of Object.entries(raw || {})) out[normalizeYamlKey(k)] = v;

  const editor = String(out.editor ?? '').trim();
  const webEntry = String(out.webentry || '').trim();

  if (editor && editor.toLowerCase() === 'none') {
    return { mode: 'none', editorUrl: '', copySrc: null, siteSubdir: 'web', entry: '' };
  }

  if (editor && isExternalUrl(editor)) {
    return { mode: 'external', editorUrl: editor, copySrc: null, siteSubdir: 'web', entry: '' };
  }

  if (editor && !isExternalUrl(editor)) {
    if (/^[a-z][a-z0-9+.-]*:/i.test(editor)) throw new Error(`Editor URL must use HTTPS: ${editor}`);
    return resolveLocal(cardAbsPath, slug, pagesBaseUrl, editor, webEntry, '');
  }

  const defaultWeb = path.join(cardAbsPath, 'web');
  if (await folderExists(defaultWeb)) {
    return resolveLocal(cardAbsPath, slug, pagesBaseUrl, 'web', webEntry, '');
  }

  return { mode: 'none', editorUrl: '', copySrc: null, siteSubdir: 'web', entry: '' };
}

function shouldSkipEntry(name, isDir) {
  if (SKIP_DIRS.has(name)) return true;
  if (!isDir && SKIP_FILES.has(name)) return true;
  if (isDir && name === 'src') return true;
  if (!isDir && name === 'package.json') return true;
  return false;
}

/** Recursively copy static web assets with dev-only paths skipped. */
export async function copyWebAssets(src, dest, sourceRoot = null) {
  const root = sourceRoot || await fs.realpath(src);
  const realSrc = await fs.realpath(src);
  if (realSrc !== root && !realSrc.startsWith(root + path.sep)) throw new Error(`Web asset directory escapes source root: ${src}`);
  await ensureDir(dest);
  const entries = await fs.readdir(src, { withFileTypes: true });
  for (const ent of entries) {
    if (ent.isSymbolicLink()) throw new Error(`Web assets must not contain symbolic links: ${path.join(src, ent.name)}`);
    if (shouldSkipEntry(ent.name, ent.isDirectory())) continue;
    const from = path.join(src, ent.name);
    const to = path.join(dest, ent.name);
    if (ent.isDirectory()) {
      await copyWebAssets(from, to, root);
    } else {
      const stat = await fs.lstat(from);
      if (!stat.isFile()) continue;
      await ensureDir(path.dirname(to));
      await fs.copyFile(from, to);
    }
  }
}
