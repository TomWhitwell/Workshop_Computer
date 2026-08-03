import path from 'node:path';
import YAML from 'yaml';
import { fsAsync as fs, ensureDir, encodePathSegments } from '../utils/fs.js';
import { renderMarkdownBlock } from '../utils/markdown.js';

export const CUSTOM_PANEL_WIDTH = 560;
export const CUSTOM_PANEL_HEIGHT = 1785;
const PANEL_ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const CANONICAL_VIEW_BOX = `0 0 ${CUSTOM_PANEL_WIDTH} ${CUSTOM_PANEL_HEIGHT}`;

function diagnostic(severity, pathName, message) {
  return { severity, path: pathName, message };
}

function safeRelativePath(value) {
  if (typeof value !== 'string' || !value.trim()) return null;
  const normalized = path.posix.normalize(value.trim().replace(/\\/g, '/'));
  if (normalized === '.' || normalized.startsWith('../') || normalized.includes('/../') || path.posix.isAbsolute(normalized)) return null;
  return normalized;
}

async function svgMetadata(file) {
  const source = await fs.readFile(file, 'utf8');
  const open = source.match(/<svg\b([^>]*)>/i);
  if (!open) return { error: 'is not a valid SVG document.' };
  const viewBox = open[1].match(/\bviewBox\s*=\s*(["'])(.*?)\1/i)?.[2]?.trim().replace(/\s+/g, ' ');
  if (viewBox !== CANONICAL_VIEW_BOX) return { error: `must use viewBox="${CANONICAL_VIEW_BOX}".` };
  const forbidden = [
    [/<\s*(?:script|foreignObject|iframe|object|embed)\b/i, 'contains forbidden active content.'],
    [/\son[a-z]+\s*=/i, 'contains a forbidden event handler.'],
    [/@import\b/i, 'contains a forbidden external stylesheet import.'],
    [/\b(?:href|xlink:href)\s*=\s*(["'])(?!(?:#|data:))/i, 'contains a non-self-contained resource reference.'],
    [/url\(\s*(["']?)(?:https?:|\/\/)/i, 'contains an external resource URL.'],
  ];
  for (const [pattern, message] of forbidden) if (pattern.test(source)) return { error: message };
  return { width: CUSTOM_PANEL_WIDTH, height: CUSTOM_PANEL_HEIGHT, source };
}

async function regularFileInside(root, relative) {
  const file = path.join(root, ...relative.split('/'));
  try {
    const stat = await fs.lstat(file);
    if (!stat.isFile() || stat.isSymbolicLink()) return null;
    const [realRoot, realFile] = await Promise.all([fs.realpath(root), fs.realpath(file)]);
    if (realFile !== realRoot && !realFile.startsWith(realRoot + path.sep)) return null;
    return file;
  } catch {
    return null;
  }
}

function assetUrl(relative) {
  return `panels/${encodePathSegments(relative)}`;
}

function rewritePanelHtmlLinks(html, markdownPath) {
  const base = path.posix.dirname(markdownPath);
  return String(html || '').replace(/\b(href|src)=(['"])([^'"#]+)(#[^'"]*)?\2/gi, (full, attr, quote, rawUrl, hash = '') => {
    const value = String(rawUrl).trim();
    if (!value || /^(?:[a-z][a-z0-9+.-]*:)?\/\//i.test(value) || /^[a-z][a-z0-9+.-]*:/i.test(value) || value.startsWith('/')) return full;
    // Markdown renderers percent-encode spaces before this pass. Decode once
    // before applying our segment encoder so `%20` does not become `%2520`.
    let decoded;
    try { decoded = decodeURIComponent(value); } catch { return full; }
    const relative = safeRelativePath(path.posix.join(base, decoded));
    return relative ? `${attr}=${quote}${assetUrl(relative)}${hash}${quote}` : full;
  });
}

/**
 * Read and validate panel declarations separately from the referenced files.
 * Publishing discovery adds file, SVG-safety, and Markdown validation below.
 */
export async function readCustomPanelManifest(absReleaseDir) {
  const panelsDir = path.join(absReleaseDir, 'panels');
  const manifestPath = path.join(panelsDir, 'manifest.yaml');
  const diagnostics = [];
  try {
    await fs.lstat(manifestPath);
  } catch (error) {
    if (error?.code === 'ENOENT' || error?.code === 'ENOTDIR') {
      return { present: false, panelsDir, manifest: null, items: [], diagnostics };
    }
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', `Could not inspect the custom-panel manifest: ${error.message}`));
    return { present: true, panelsDir, manifest: null, items: [], diagnostics };
  }

  let stat;
  try {
    stat = await fs.lstat(panelsDir);
  } catch (error) {
    diagnostics.push(diagnostic('error', 'panels', `Could not inspect the custom-panel directory: ${error.message}`));
    return { present: true, panelsDir, manifest: null, items: [], diagnostics };
  }
  if (!stat.isDirectory() || stat.isSymbolicLink()) {
    diagnostics.push(diagnostic('error', 'panels', 'panels must be a real directory, not a file or symbolic link.'));
    return { present: true, panelsDir, manifest: null, items: [], diagnostics };
  }

  let manifest;
  try {
    manifest = YAML.parse(await fs.readFile(manifestPath, 'utf8')) || {};
  } catch (error) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', `Could not read the custom-panel manifest: ${error.message}`));
    return { present: true, panelsDir, manifest: null, items: [], diagnostics };
  }
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'The manifest root must be a mapping.'));
    return { present: true, panelsDir, manifest: null, items: [], diagnostics };
  }
  if (manifest.version !== 1) diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'version must be 1.'));
  if (!Array.isArray(manifest.panels) || !manifest.panels.length) {
    diagnostics.push(diagnostic('error', 'panels/manifest.yaml', 'panels must be a non-empty list.'));
    return { present: true, panelsDir, manifest, items: [], diagnostics };
  }

  const ids = new Set();
  const items = [];
  for (let index = 0; index < manifest.panels.length; index += 1) {
    const raw = manifest.panels[index];
    const at = `panels/manifest.yaml panels[${index}]`;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
      diagnostics.push(diagnostic('error', at, 'Panel entry must be a mapping.'));
      continue;
    }
    const id = typeof raw.id === 'string' ? raw.id.trim() : '';
    const name = typeof raw.name === 'string' ? raw.name.trim() : '';
    const imageOmitted = !Object.hasOwn(raw, 'image');
    const imageRaw = typeof raw.image === 'string' ? raw.image.trim() : '';
    const isImageAuto = imageOmitted || imageRaw.toLowerCase() === 'auto';
    const image = isImageAuto ? 'auto' : safeRelativePath(raw.image);
    const contentOmitted = !Object.hasOwn(raw, 'content');
    const contentRaw = typeof raw.content === 'string' ? raw.content.trim() : '';
    const isContentAuto = contentOmitted || contentRaw.toLowerCase() === 'auto';
    const content = isContentAuto ? 'auto' : safeRelativePath(raw.content);
    let valid = true;
    if (!PANEL_ID.test(id)) { diagnostics.push(diagnostic('error', `${at}.id`, 'id must be unique lowercase kebab-case.')); valid = false; }
    if (ids.has(id.toLowerCase())) { diagnostics.push(diagnostic('error', `${at}.id`, `Duplicate panel id "${id}".`)); valid = false; }
    if (!name) { diagnostics.push(diagnostic('error', `${at}.name`, 'name must be non-empty text.')); valid = false; }
    if (!isImageAuto && !image) { diagnostics.push(diagnostic('error', `${at}.image`, 'image must be a safe relative SVG path or "auto".')); valid = false; }
    if (!isContentAuto && !content) { diagnostics.push(diagnostic('error', `${at}.content`, 'content must be a safe relative Markdown path or "auto".')); valid = false; }
    if (!isImageAuto && image && path.posix.extname(image).toLowerCase() !== '.svg') { diagnostics.push(diagnostic('error', `${at}.image`, 'image must be an SVG file or "auto".')); valid = false; }
    if (!isContentAuto && content && path.posix.extname(content).toLowerCase() !== '.md') { diagnostics.push(diagnostic('error', `${at}.content`, 'content must be a Markdown file or "auto".')); valid = false; }
    if (id) ids.add(id.toLowerCase());
    if (valid) items.push({ id, name, image, content, index, at, isImageAuto, isContentAuto });
  }

  const requestedDefault = typeof manifest.default === 'string' ? manifest.default.trim() : '';
  if (!requestedDefault) diagnostics.push(diagnostic('error', 'panels/manifest.yaml default', 'default must name one panel id.'));
  else if (!items.some(item => item.id === requestedDefault)) diagnostics.push(diagnostic('error', 'panels/manifest.yaml default', `default references missing panel "${requestedDefault}".`));

  return { present: true, panelsDir, manifest, default: requestedDefault, items, diagnostics };
}

/**
 * Discover an optional release-local panels/ presentation override.
 * Manifest presence is authoritative: an asset-only panels/ directory is
 * ignored, while an invalid manifest suppresses generated presentation views.
 */
export async function discoverCustomPanels(absReleaseDir, outProgramDir, { copyAssets = true } = {}) {
  const source = await readCustomPanelManifest(absReleaseDir);
  const { panelsDir } = source;
  if (!source.present) return { present: false, panels: null, diagnostics: [] };
  const diagnostics = [...source.diagnostics];
  const empty = { source: 'custom', default: '', items: [] };
  if (!source.manifest || !source.items.length) return { present: true, panels: empty, diagnostics };
  const items = [];
  for (const declared of source.items) {
    const { id, name, at, isImageAuto, isContentAuto } = declared;
    const imagePath = declared.image;
    const contentPath = declared.content;
    let valid = true;
    const imageFile = isImageAuto ? null : await regularFileInside(panelsDir, imagePath);
    const contentFile = isContentAuto ? null : await regularFileInside(panelsDir, contentPath);
    if (!isImageAuto && !imageFile) { diagnostics.push(diagnostic('error', `${at}.image`, `Missing or unsafe file "${imagePath}".`)); valid = false; }
    if (!isContentAuto && !contentFile) { diagnostics.push(diagnostic('error', `${at}.content`, `Missing or unsafe file "${contentPath}".`)); valid = false; }
    if (!valid) continue;

    let image;
    if (!isImageAuto) {
      const metadata = await svgMetadata(imageFile);
      if (metadata.error) {
        diagnostics.push(diagnostic('error', `${at}.image`, `"${imagePath}" ${metadata.error}`));
        continue;
      }
      image = { url: assetUrl(imagePath), format: 'svg', width: metadata.width, height: metadata.height };
    }

    let contentHtml = null;
    if (!isContentAuto) {
      const markdown = await fs.readFile(contentFile, 'utf8');
      if (!markdown.trim()) diagnostics.push(diagnostic('warning', `${at}.content`, `"${contentPath}" is empty.`));
      contentHtml = rewritePanelHtmlLinks(renderMarkdownBlock(markdown), contentPath);
    }

    const item = {
      id,
      name,
      kind: isContentAuto ? 'generated' : 'custom',
      image_kind: isImageAuto ? 'generated' : 'custom',
      content_kind: isContentAuto ? 'generated' : 'custom',
      content_html: contentHtml,
      source: {},
    };
    if (image) item.image = image;
    if (!isImageAuto) item.source.image = `panels/${imagePath}`;
    if (!isContentAuto) item.source.content = `panels/${contentPath}`;
    items.push(item);
  }

  const requestedDefault = source.default;
  const defaultItem = items.find(item => item.id === requestedDefault);
  if (requestedDefault && !defaultItem) diagnostics.push(diagnostic('error', 'panels/manifest.yaml default', `default references missing or invalid panel "${requestedDefault}".`));

  // Preserve the complete authored directory so Markdown can use supplementary
  // relative images. Referenced primary files were checked above; symlinks are
  // rejected by fs.cp rather than followed.
  if (copyAssets) {
    try {
      await ensureDir(outProgramDir);
      await fs.cp(panelsDir, path.join(outProgramDir, 'panels'), {
        recursive: true,
        force: true,
        filter: async source => !(await fs.lstat(source)).isSymbolicLink(),
      });
    } catch (error) {
      diagnostics.push(diagnostic('error', 'panels', `Could not copy custom-panel assets: ${error.message}`));
    }
  }

  return {
    present: true,
    panels: { source: 'custom', default: defaultItem?.id || items[0]?.id || '', items },
    diagnostics,
  };
}

function objectField(value, wanted) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return undefined;
  const normalized = wanted.toLowerCase().replace(/[-_\s]/g, '');
  const entry = Object.entries(value).find(([key]) => key.toLowerCase().replace(/[-_\s]/g, '') === normalized);
  return entry?.[1];
}

/** Validate cross-file when.panel references after info.yaml and the manifest are available. */
export function validateCustomPanelReferences(info, customPanels) {
  const diagnostics = [];
  const ids = new Set((customPanels?.items || []).map(item => item.id));
  const controls = objectField(info, 'controls');
  const panel = objectField(info, 'panel');
  const lists = [
    ['controls.knobs', objectField(controls, 'knobs')],
    ['controls.leds', objectField(controls, 'leds')],
    ['panel.inputs', objectField(panel, 'inputs')],
    ['panel.outputs', objectField(panel, 'outputs')],
  ];
  for (const [listPath, rows] of lists) {
    if (!Array.isArray(rows)) continue;
    rows.forEach((row, index) => {
      const when = objectField(row, 'when');
      const panelId = objectField(when, 'panel');
      if (panelId === undefined) return;
      if (!customPanels) {
        diagnostics.push(diagnostic('error', `${listPath}[${index}].when.panel`, 'when.panel requires a panels/manifest.yaml custom-panel override.'));
      } else if (typeof panelId === 'string' && !ids.has(panelId)) {
        diagnostics.push(diagnostic('error', `${listPath}[${index}].when.panel`, `Unknown custom panel id "${panelId}".`));
      }
    });
  }
  return diagnostics;
}
