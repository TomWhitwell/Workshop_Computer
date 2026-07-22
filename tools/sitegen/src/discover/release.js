import path from 'node:path';
import YAML from 'yaml';
import { marked } from 'marked';
import { fsAsync as fs, fileExists } from '../utils/fs.js';
import { slugify, normalizeYamlKey } from '../utils/strings.js';
import { toPosix } from '../utils/fs.js';
import { discoverDocs } from './docs.js';
import { discoverDownloads, curateUf2Downloads } from './downloads.js';
import { getLastCommitDate, getCommitDates, getOldestBlameDate, getContentUpdatedDate } from '../utils/git.js';
import { resolveWebConfig } from './webEditor.js';
import { normalizeTags, normalizeRepository, normalizeDiscussion, normalizeContact, normalizeDraft, resolveAudioSample } from './infoFields.js';
import { parseYoutubeId, youtubeEmbedHtml } from '../utils/youtube.js';
import { resolveAudioSamples, getAudioField } from '../utils/audio.js';
import { buildCanonicalCardModel } from '../model/card.js';

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
    shortdescription: out.shortdescription || '',
    summary: out.summary || '',
    language: out.language || '',
    creator: out.creator || '',
    version: out.version || '',
    status: out.status || '',
    license: String(out.license || '').trim(),
    editor: out.editor || '',
    date: out.date || out.releasedate || '',
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

  // Fallback date from git if not specified in YAML
  if (!info.date) {
    const relPath = path.join('releases', folderName);
    const gitDate = getLastCommitDate(relPath);
    if (gitDate) {
      // Keep ISO string or take YYYY-MM-DD. ISO string sorts better if we want time too, 
      // but UI logic just compares strings. Let's keep full ISO for better precision
      // or just YYYY-MM-DD if preferred. The user asked for "date".
      // Let's use YYYY-MM-DD for consistency with manual entry usually.
      info.date = gitDate.split('T')[0];
    }
  }

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
    readmeHtml = marked.parse(md);
  }

  // docs
  const outProgramDir = path.join(outDirPrograms, slug);
  const { docs } = await discoverDocs(abs, outProgramDir);

  // downloads and README asset link rewriting base
  const repoRelBase = path.join('releases', folderName);
  // Rewrite relative links in README HTML to raw GitHub URLs
  readmeHtml = rewriteHtmlLinksToRaw(readmeHtml, repoRelBase);
  // Inject YouTube embeds after links, preserving the links (minimal logic)
  readmeHtml = injectYouTubeEmbeds(readmeHtml);
  
  // downloads
  const { downloads, latestUf2, uf2Downloads, trackedUf2 } = await discoverDownloads(abs, repoRelBase, makeRawUrl);

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
    for (const e of errors) console.error(`[sitegen] ${folderName}: ${e}`);
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
  });

  return {
    folderName,
    slug,
    rawInfoSource,
    rawYaml,
    readmeHtml,
    docs,
    downloads,
    latestUf2: primaryUf2,
    uf2Downloads: effectiveUf2Downloads,
    trackedUf2,
    web,
    card,
  };
}

// Append an embed after YouTube links in README HTML, keep link text
function injectYouTubeEmbeds(html) {
  if (!html) return html;
  const anchorRe = /<a\s+[^>]*href=(['"])([^'"#]+)\1[^>]*>([\s\S]*?)<\/a>/gi;
  return html.replace(anchorRe, (full, q, href) => {
    const embed = youtubeEmbedHtml(href);
    if (!embed || !parseYoutubeId(href)) return full;
    return `${full}${embed}`;
  });
}
