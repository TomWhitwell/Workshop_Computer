import path from 'node:path';
import YAML from 'yaml';
import { fsAsync as fs, fileExists } from '../utils/fs.js';
import { slugify, normalizeYamlKey } from '../utils/strings.js';
import { toPosix } from '../utils/fs.js';
import { discoverDocs } from './docs.js';
import { discoverCustomPanels, validateCustomPanelReferences } from './customPanels.js';
import { discoverDownloads, curateUf2Downloads } from './downloads.js';
import { getCommitDates, getOldestBlameDate, getContentUpdatedDate } from '../utils/git.js';
import { resolveWebConfig } from './webEditor.js';
import { normalizeTags, normalizeRepository, normalizeDiscussion, normalizeContact, normalizeDraft, resolveAudioSample } from './infoFields.js';
import { videoEmbedHtml } from '../utils/video.js';
import { resolveAudioSamples, getAudioField } from '../utils/audio.js';
import { buildCanonicalCardModel } from '../model/card.js';
import { renderMarkdownBlock } from '../utils/markdown.js';

// Read the top-level `uf2` field from parsed YAML, case-insensitively.
function readUf2Field(obj) {
  if (!obj || typeof obj !== 'object') return undefined;
  for (const [k, v] of Object.entries(obj)) {
    if (normalizeYamlKey(k) === 'uf2') return v;
  }
  return undefined;
}

export function normalizeInfo(raw, fallbackTitle) {
  const out = {};
  for (const [k, v] of Object.entries(raw || {})) out[normalizeYamlKey(k)] = v;
  return {
    draft: normalizeDraft(out.draft),
    title: out.title || out.name || fallbackTitle,
    // `Description` was split into a concise discovery label and a longer
    // detail-page overview. Keep the old value only as an import fallback.
    shortdescription: out.shortdescription || out.description || '',
    summary: out.summary || out.description || '',
    language: out.language || '',
    creator: out.creator || '',
    version: out.version || '',
    status: out.status || '',
    license: String(out.license || '').trim(),
    editor: out.editor || '',
    audiosample: String(out.audiosample || '').trim(),
    audiosampleurl: '',
    tags: normalizeTags(out.tags),
    repository: normalizeRepository(out.repository),
    discussion: normalizeDiscussion(out.discussion),
    contact: normalizeContact(out.contact),
  };
}

export async function discoverRelease(rootReleasesDir, folderName, outDirPrograms, makeRawUrl, pagesBaseUrl, repoSlug, refName) {
  const abs = path.join(rootReleasesDir, folderName);
  const slug = slugify(folderName);

  // info.yaml
  const infoPath = path.join(abs, 'info.yaml');
  let rawYaml = {};
  let info = { title: folderName };
  let rawInfoSource = '';
  if (await fileExists(infoPath)) {
    const raw = await fs.readFile(infoPath, 'utf8');
    rawInfoSource = raw;
    try {
      rawYaml = YAML.parse(raw) || {};
      info = normalizeInfo(rawYaml, folderName);
    } catch {
      info = normalizeInfo({}, folderName);
    }
  } else {
    info = normalizeInfo({}, folderName);
  }

  const web = await resolveWebConfig(rawYaml, abs, slug, pagesBaseUrl);
  if (web.editorUrl) info.editor = web.editorUrl;
  else if (web.mode === 'none') info.editor = '';

  // Helper: rewrite relative links in README HTML to raw GitHub URLs
  function rewriteHtmlLinksToRaw(html, repoRelBase) {
    if (!html) return html;
    const posixBase = path.posix.join(...repoRelBase.split(path.sep));
    return html.replace(/\b(href|src)=(['"])([^'"#]+)(\#[^'"]*)?\2/gi, (full, attr, quote, url, hash = '') => {
      const u = String(url).trim();
      // Skip anchors, data URIs, protocol URLs, protocol-relative URLs
      if (!u || u.startsWith('#') || /^(?:[a-zA-Z][a-zA-Z0-9+.-]*:)?\/\//.test(u) || /^(?:[a-zA-Z][a-zA-Z0-9+.-]*:)/.test(u) || u.startsWith('data:')) {
        return full;
      }
      let relPath;
      if (u.startsWith('/')) {
        relPath = u.replace(/^\/+/, '');
      } else {
        relPath = path.posix.normalize(path.posix.join(posixBase, u));
      }
      const newUrl = makeRawUrl(relPath) + (hash || '');
      return `${attr}=${quote}${newUrl}${quote}`;
    });
  }

  // README.md
  const readmePath = path.join(abs, 'README.md');
  let readmeHtml = '<p>No README.md found.</p>';
  if (await fileExists(readmePath)) {
    const md = await fs.readFile(readmePath, 'utf8');
    readmeHtml = renderMarkdownBlock(md);
  }

  // docs
  const outProgramDir = path.join(outDirPrograms, slug);
  const { docs } = await discoverDocs(abs, outProgramDir);
  const customPanels = await discoverCustomPanels(abs, outProgramDir);
  customPanels.diagnostics.push(...validateCustomPanelReferences(rawYaml, customPanels.present ? customPanels.panels : null));

  // downloads and README asset link rewriting base
  const repoRelBase = path.join('releases', folderName);
  // Rewrite relative links in README HTML to raw GitHub URLs
  readmeHtml = rewriteHtmlLinksToRaw(readmeHtml, repoRelBase);
  // Inject YouTube / Instagram embeds after links, preserving the links
  readmeHtml = injectVideoEmbeds(readmeHtml);
  
  // downloads
  const { downloads, latestUf2, uf2Downloads, availableUf2Downloads, trackedUf2 } = await discoverDownloads(abs, repoRelBase, makeRawUrl);

  // A curated `uf2:` list in info.yaml fully replaces auto-discovery for this
  // card, so authors can trim noise and annotate firmware (name/description/hash).
  // An empty/null `uf2:` is treated as absent so downloads aren't lost by a
  // half-authored field (validation warns about it separately).
  let effectiveUf2Downloads = uf2Downloads;
  const uf2Field = readUf2Field(rawYaml);
  const hasCuratedUf2 = uf2Field != null && !(Array.isArray(uf2Field) && uf2Field.length === 0);
  if (hasCuratedUf2) {
    const { uf2Downloads: curated, errors } = await curateUf2Downloads(uf2Field, abs, repoRelBase, makeRawUrl);
    effectiveUf2Downloads = curated;
    if (errors.length) throw new Error(`${folderName} has invalid curated firmware metadata: ${errors.join(' ')}`);
  }
  const primaryUf2 = effectiveUf2Downloads[0] || null;

  // Audio samples: uploaded files (repo-relative), SoundCloud, or Bandcamp.
  const audioSamples = resolveAudioSamples(
    getAudioField(rawYaml),
    (rel) => resolveAudioSample(rel, repoRelBase, makeRawUrl),
  );

  const sourceFile = toPosix(path.join('releases', folderName, 'info.yaml'));
  const sourceUrl = `https://github.com/${repoSlug}/tree/${refName}/releases/${folderName}`;
  const readmeRelPath = toPosix(path.join('releases', folderName, 'README.md'));
  const readmeUrl = `https://github.com/${repoSlug}/blob/${refName}/releases/${folderName}/README.md`;
  const { first: gitFirstDate, last: gitLastDate } = getCommitDates(path.join('releases', folderName));
  // "Phil's method", two signals:
  //  - created: oldest surviving blame date of info.yaml (bulk-edit-resistant
  //    genesis of the card's metadata).
  //  - updated: most recent commit touching the card's release *content*
  //    (firmware, source, assets) — i.e. the folder minus the bulk-edited
  //    info.yaml/README. A content commit is a real update, and metadata bulk
  //    edits are excluded, so this advances on each release and survives the
  //    bulk clobber that ruins the folder's last-commit date.
  const blameDate = getOldestBlameDate(sourceFile);
  const contentDate = getContentUpdatedDate(path.join('releases', folderName));
  const card = buildCanonicalCardModel({
    folderName,
    slug,
    info,
    rawYaml,
    docs,
    downloads,
    latestUf2: primaryUf2,
    uf2Downloads: effectiveUf2Downloads,
    web,
    audioSamples,
    readmePath: readmeRelPath,
    sourceFile,
    sourceUrl,
    readmeUrl,
    gitFirstDate,
    gitLastDate,
    blameDate,
    contentDate,
    customPanels: customPanels.present ? customPanels.panels : null,
  });

  return {
    folderName,
    slug,
    rawInfoSource,
    rawYaml,
    readmeHtml,
    docs,
    panelDiagnostics: customPanels.diagnostics,
    downloads,
    latestUf2: primaryUf2,
    uf2Downloads: effectiveUf2Downloads,
    availableUf2Downloads,
    trackedUf2,
    web,
    card,
  };
}

// Append embeds after YouTube / Instagram links in README HTML, keeping the
// link text. When a link sits mid-sentence inside a block (e.g. a list item),
// place the embed after that block so trailing prose isn't split in half.
function injectVideoEmbeds(html) {
  if (!html) return html;
  const anchorRe = /<a\s+[^>]*href=(['"])([^'"#]+)\1[^>]*>([\s\S]*?)<\/a>/gi;
  let out = '';
  let last = 0;
  const pending = [];
  for (const match of html.matchAll(anchorRe)) {
    const full = match[0];
    const href = match[2];
    const embed = videoEmbedHtml(href);
    const start = match.index;
    out += html.slice(last, start);
    out += full;
    last = start + full.length;
    if (!embed) continue;
    pending.push({ after: last, embed });
  }
  out += html.slice(last);
  if (!pending.length) return out;

  // Re-scan the rebuilt string and move each embed to the end of its block.
  let rebuilt = '';
  let cursor = 0;
  // Work from the original pending offsets in `out` (same prefix lengths).
  let source = out;
  for (const { after, embed } of pending) {
    const blockClose = source.slice(after).search(/<\/(li|p|td|blockquote)>/i);
    if (blockClose === -1) {
      // No block boundary: keep embed immediately after the anchor.
      rebuilt += source.slice(cursor, after) + embed;
      cursor = after;
      continue;
    }
    const insertAt = after + blockClose;
    rebuilt += source.slice(cursor, insertAt) + embed;
    cursor = insertAt;
  }
  rebuilt += source.slice(cursor);
  return rebuilt;
}
