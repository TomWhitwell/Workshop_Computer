import path from 'node:path';
import { fsAsync as fs, ensureDir, fileExists } from '../utils/fs.js';
import { normalizeYamlKey } from '../utils/strings.js';

const SKIP_DIRS = new Set(['node_modules', '.git', '.github']);
const SKIP_FILES = new Set(['package-lock.json', 'tsconfig.json', 'vite.config.ts', 'vite.config.js']);

/** GitHub Pages base URL for a user/org project site. */
export function githubPagesBase(repoSlug) {
  const [owner, name] = String(repoSlug || '').split('/');
  if (!owner || !name) return 'https://tomwhitwell.github.io/Workshop_Computer/';
  return `https://${owner.toLowerCase()}.github.io/${name}/`;
}

function normalizeRelFolder(loc) {
  const s = String(loc || '').trim().replace(/^\/+/, '').replace(/\/+$/, '');
  return s || 'web';
}

function isExternalUrl(s) {
  return /^https?:\/\//i.test(String(s || '').trim());
}

async function folderExists(dir) {
  return fileExists(dir);
}

async function resolveEntryFile(srcDir, webEntry) {
  if (webEntry) {
    const p = path.join(srcDir, webEntry);
    if (await fileExists(p)) return webEntry;
  }
  if (await fileExists(path.join(srcDir, 'index.html'))) return 'index.html';
  return webEntry || '';
}

async function resolveLocal(cardAbsPath, slug, pagesBaseUrl, relFolder, webEntry, externalFallback) {
  const copySrc = path.join(cardAbsPath, normalizeRelFolder(relFolder));
  if (!(await folderExists(copySrc))) {
    return { mode: 'none', editorUrl: '', copySrc: null, siteSubdir: 'web', entry: '' };
  }
  const entry = await resolveEntryFile(copySrc, webEntry);
  if (!entry && externalFallback && isExternalUrl(externalFallback)) {
    return { mode: 'external', editorUrl: externalFallback, copySrc: null, siteSubdir: 'web', entry: '' };
  }
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
export async function copyWebAssets(src, dest) {
  await ensureDir(dest);
  const entries = await fs.readdir(src, { withFileTypes: true });
  for (const ent of entries) {
    if (shouldSkipEntry(ent.name, ent.isDirectory())) continue;
    const from = path.join(src, ent.name);
    const to = path.join(dest, ent.name);
    if (ent.isDirectory()) {
      await copyWebAssets(from, to);
    } else {
      await ensureDir(path.dirname(to));
      await fs.copyFile(from, to);
    }
  }
}
