import path from 'node:path';
import YAML from 'yaml';
import { marked } from 'marked';
import { fsAsync as fs, fileExists } from '../utils/fs.js';
import { slugify, parseDisplayFromFolder, formatDisplayTitle, normalizeYamlKey } from '../utils/strings.js';
import { discoverDocs } from './docs.js';
import { discoverDownloads } from './downloads.js';
import { getLastCommitDate } from '../utils/git.js';
import { resolveWebConfig } from './webEditor.js';
import { normalizeTags, normalizeRepository, normalizeContact, normalizeDraft, resolveAudioSample } from './infoFields.js';
import { parseYoutubeId, youtubeEmbedHtml } from '../utils/youtube.js';

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
    date: out.date || out.releasedate || '',
    audiosample: String(out.audiosample || '').trim(),
    audiosampleurl: '',
    tags: normalizeTags(out.tags),
    repository: normalizeRepository(out.repository),
    contact: normalizeContact(out.contact),
  };
}

export async function discoverRelease(rootReleasesDir, folderName, outDirPrograms, makeRawUrl, pagesBaseUrl) {
  const abs = path.join(rootReleasesDir, folderName);
  const slug = slugify(folderName);

  // info.yaml
  const infoPath = path.join(abs, 'info.yaml');
  let rawYaml = {};
  let info = { title: folderName };
  if (await fileExists(infoPath)) {
    const raw = await fs.readFile(infoPath, 'utf8');
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
  if (info.audiosample) {
    info.audiosampleurl = resolveAudioSample(info.audiosample, repoRelBase, makeRawUrl);
  }
  // Rewrite relative links in README HTML to raw GitHub URLs
  readmeHtml = rewriteHtmlLinksToRaw(readmeHtml, repoRelBase);
  // Inject YouTube embeds after links, preserving the links (minimal logic)
  readmeHtml = injectYouTubeEmbeds(readmeHtml);
  
  // downloads
  const { downloads, latestUf2 } = await discoverDownloads(abs, repoRelBase, makeRawUrl);

  // display fields
  const parsed = parseDisplayFromFolder(folderName);
  const finalTitle = info.title ? formatDisplayTitle(info.title) : (parsed.title || folderName);
  const display = { number: parsed.number, title: finalTitle };

  return {
    folderName,
    slug,
    info: { ...info, title: finalTitle },
    readmeHtml,
    docs,
    downloads,
    latestUf2,
    display,
    web,
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
