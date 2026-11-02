import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs, toPosix } from './utils/fs.js';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { build as esbuild } from 'esbuild';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { discoverRelease as discoverReleaseMod } from './discover/release.js';
import { resolveSiteBase, copyWebAssets } from './discover/webEditor.js';
import { getInfoYamlSchemaAdapter } from './schema/schemaAdapter.js';
import { infoYamlJsonSchema } from './schema/infoYamlJsonSchema.js';
import { renderCardArticle, renderReadmeAndDocs } from './render/cardPage.js';
import { renderDiscovery, renderArchive, renderArchiveRows } from './render/discovery.js';
import { renderFilterBar } from './render/filterBar.js';
import { externalLinkArrow } from './render/icons.js';
import { curation } from './curation/index.js';
import { parseSource } from './validate/parseSource.js';
import { validateInfoYaml } from './validate/validateInfoYaml.js';
import { collectKnownValues } from './utils/similarValues.js';
import { renderAuthorPage } from './render/authorPage.js';
import { buildFirmwareFingerprints } from './firmware/fingerprints.js';
import {
  DEFAULT_OG_DESCRIPTION,
  DEFAULT_OG_IMAGE_PATH,
  absoluteSiteUrl,
} from './render/socialMeta.js';
import { ogImageOptionsForCard, ogImageOptionsForSite, writeOgPng } from './render/ogImage.js';
import { validateInfoFile } from './validate/validateFile.js';
import { loadKnownValues } from './validate/knownValues.js';
import { reportText } from './validate/reporters/index.js';

// ========== Path & Globals ==========
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const ROOT = path.resolve(__dirname, '../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');
const OUT_DIR = path.join(ROOT, 'site');
const DEV_CACHE_FILE = path.join(ROOT, 'tools', 'sitegen', 'node_modules', '.cache', 'sitegen-releases.json');

// Resolve repo details (for GitHub raw links)
const DEFAULT_REPO = 'TomWhitwell/Workshop_Computer';
const DEFAULT_BRANCH = 'main';


// GitHub Actions supplies the canonical repository and deployed commit. Local
// builds deliberately use the upstream defaults rather than the origin remote,
// which may point at a contributor's fork. SITE_REPOSITORY/SITE_REF remain
// available for an explicit preview override.
const REPO = process.env.SITE_REPOSITORY || process.env.GITHUB_REPOSITORY || DEFAULT_REPO;
const BRANCH = process.env.SITE_REF || process.env.GITHUB_SHA || process.env.GITHUB_REF_NAME || DEFAULT_BRANCH;
const SITE_BASE = resolveSiteBase({
  configured: process.env.SITE_BASE_URL,
  preview: process.argv.includes('--preview'),
  repoSlug: REPO,
});
const schemaAdapter = getInfoYamlSchemaAdapter();


// (info parsing handled in discover/release.js)

function makeRawUrl(relPathFromRepoRoot) {
  return makeRawUrlExternal(REPO, BRANCH, relPathFromRepoRoot);
}

async function discoverRelease(folderName) {
  const outPrograms = path.join(OUT_DIR, 'programs');
  return discoverReleaseMod(RELEASES_DIR, folderName, outPrograms, makeRawUrl, SITE_BASE, REPO, BRANCH);
}

function escapeAttr(s) {
  return String(s ?? '').replaceAll('"', '&quot;');
}

function pageSocial(relativePath, { title, description, imagePath = DEFAULT_OG_IMAGE_PATH, imageAlt } = {}) {
  return {
    title,
    description: description || DEFAULT_OG_DESCRIPTION,
    url: absoluteSiteUrl(SITE_BASE, relativePath),
    image: absoluteSiteUrl(SITE_BASE, imagePath),
    imageAlt: imageAlt || title,
  };
}

function detailPage(rel) {
  const { docs, readmeHtml, card } = rel;
  const uf2Url = rel.latestUf2?.url || '';
  const yamlUrl = card?.source_file
    ? `https://github.com/${REPO}/blob/${BRANCH}/${card.source_file}`
    : `https://github.com/${REPO}`;
  const title = `${card.title} – Workshop Computer`;

  const article = renderCardArticle({
    card,
    flairs: curation.resolveFlair(card.id),
    panelImg: '../../assets/program_cards/Standalone_computer_rev1.svg',
    root: '../..',
    yamlUrl,
    uf2Url,
    extraDocs: renderReadmeAndDocs({ readmeHtml, docs, includeReadme: !card.documentation?.intro }),
    basic: !!card.draft,
  });

  return renderLayout({
    title,
    relativeRoot: '../..',
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial(`programs/${rel.slug}/`, {
      title,
      description: String(card.short_description || '').trim() || DEFAULT_OG_DESCRIPTION,
      imagePath: `programs/${rel.slug}/og.png`,
      imageAlt: `${card.title} program card`,
    }),
    content: `
<nav class="program-card-top-nav" aria-label="Card navigation">
  <a href="../../index.html">← BACK TO PROGRAM CARDS</a>
  <a class="program-card-author-link" href="../../preview/#${encodeURIComponent(rel.slug)}">Author Metadata${externalLinkArrow()}</a>
</nav>
${article}
<nav class="program-card-top-nav program-card-top-nav--footer" aria-label="Card navigation">
  <a href="../../index.html">← BACK TO PROGRAM CARDS</a>
  <a href="#page-top">Top ↑</a>
</nav>
`
  });
}

// Per-card work (git blame, OG rendering) runs off the main thread, so a few
// cards can be in flight at once.
const CONCURRENCY = 8;

/** Map `items` through async `fn`, at most `limit` at a time, preserving order. */
async function mapLimit(items, limit, fn) {
  const results = new Array(items.length);
  let next = 0;
  const worker = async () => {
    while (next < items.length) {
      const i = next++;
      results[i] = await fn(items[i], i);
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker));
  return results;
}

async function readDevCache() {
  try {
    const parsed = JSON.parse(await fs.readFile(DEV_CACHE_FILE, 'utf8'));
    return parsed.version === 2 && Array.isArray(parsed.releases) ? parsed.releases : null;
  } catch {
    return null;
  }
}

async function writeDevCache(releases) {
  await writeFileEnsured(DEV_CACHE_FILE, JSON.stringify({ version: 2, releases }));
}

/** Copy the stylesheets, fonts, icons and scripts every page loads. */
async function copySharedAssets() {
  const assetsDir = path.join(ROOT, 'tools', 'sitegen', 'assets');
  const outAssets = path.join(OUT_DIR, 'assets');
  await ensureDir(outAssets);
  // Site, GitHub-flavoured markdown (embedded README bodies) and detail-page stylesheets.
  for (const css of ['style.css', 'github-markdown.css', 'program-cards.css']) {
    await fs.copyFile(path.join(assetsDir, css), path.join(outAssets, css));
  }
  await ensureDir(path.join(outAssets, 'favicon'));
  await fs.copyFile(path.join(assetsDir, 'favicon', 'favicon.png'), path.join(outAssets, 'favicon', 'favicon.png'));

  // Variable Inter (weight axis 100-900) is the only self-hosted face; it
  // backs both the body text and the "Workshop Panel" font (panel rendering
  // and SVG export select their weight from its axis).
  await ensureDir(path.join(outAssets, 'fonts'));
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'node_modules', '@fontsource-variable', 'inter', 'files', 'inter-latin-wght-normal.woff2'),
    path.join(outAssets, 'fonts', 'inter-latin-wght-normal.woff2')
  );

  // Program-card panel diagrams.
  await ensureDir(path.join(outAssets, 'program_cards'));
  for (const f of await fs.readdir(path.join(assetsDir, 'program_cards'))) {
    await fs.copyFile(path.join(assetsDir, 'program_cards', f), path.join(outAssets, 'program_cards', f));
  }

  // JS assets (picoboot / uf2 libs for WebUSB programmer).
  await ensureDir(path.join(outAssets, 'js'));
  for (const f of await fs.readdir(path.join(assetsDir, 'js'))) {
    if (f.endsWith('.js')) await fs.copyFile(path.join(assetsDir, 'js', f), path.join(outAssets, 'js', f));
  }
}

/** Write a card's raw info.yaml and the build-only extras the author preview reads. */
async function writeRawInfo(rel) {
  if (!rel.rawInfoSource) return;
  await writeFileEnsured(path.join(OUT_DIR, 'raw-info', rel.folderName, 'info.yaml'), rel.rawInfoSource);
  // Ship the build-only "extras" (rendered README + PDF links with absolute
  // raw URLs) so the author preview can show the same Documentation section.
  const extraDocs = (rel.docs || []).map(d => ({
    name: d.name,
    url: makeRawUrl(toPosix(path.join('releases', rel.folderName, d.rel))),
  }));
  await writeFileEnsured(
    path.join(OUT_DIR, 'raw-info', rel.folderName, 'extras.json'),
    JSON.stringify({ readmeHtml: rel.readmeHtml || '', docs: extraDocs }),
  );
}

/** Write a card's detail page, share image and (optionally) its web editor. */
async function writeCardPage(rel, { copyWeb = true } = {}) {
  const base = path.join(OUT_DIR, 'programs', rel.slug);
  await ensureDir(base);
  await writeFileEnsured(path.join(base, 'index.html'), detailPage(rel));
  await writeOgPng(path.join(base, 'og.png'), ogImageOptionsForCard(rel.card));
  if (copyWeb && rel.web?.copySrc) {
    await copyWebAssets(rel.web.copySrc, path.join(base, rel.web.siteSubdir || 'web'));
  }
}

/**
 * Build just one card's detail page (plus the shared assets it loads) for
 * `npm run dev:card`. Skips the indexes, author tools and every other card, so
 * a rebuild takes about a second. Prints the card's full validation report.
 */
async function buildCard(folder) {
  if (!(await listSubdirs(RELEASES_DIR)).includes(folder)) {
    throw new Error(`No card folder releases/${folder}`);
  }
  await fs.rm(OUT_DIR, { recursive: true, force: true });
  await copySharedAssets();
  const rel = await discoverRelease(folder);
  const panelErrors = (rel.panelDiagnostics || []).filter(diagnostic => diagnostic.severity === 'error');
  if (panelErrors.length) {
    throw new Error(`${folder} has invalid custom panels: ${panelErrors.map(item => item.message).join(' ')}`);
  }
  await writeRawInfo(rel);
  await writeCardPage(rel);

  // The detail page links back to the home page and author tools, which this
  // build skips; say so instead of serving a 404.
  const cardHref = `programs/${encodeURIComponent(rel.slug)}/`;
  await writeFileEnsured(path.join(OUT_DIR, 'index.html'), renderLayout({
    title: `${rel.card.title} (single-card preview) – Workshop Computer`,
    relativeRoot: '.',
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial('', { title: rel.card.title }),
    content: `<h1>Single-card preview</h1>
<p>This development server only builds <a href="${escapeAttr(cardHref)}">${escapeAttr(rel.card.title)}</a>.
Run <code>npm run dev</code> to preview the whole site, including the home page and author tools.</p>`,
  }));

  console.log(`Built releases/${folder} -> ${path.join(OUT_DIR, 'programs', rel.slug)}`);
  const infoFile = path.join(RELEASES_DIR, folder, 'info.yaml');
  if (await fs.stat(infoFile).catch(() => null)) {
    console.log(reportText([await validateInfoFile(infoFile, { root: ROOT, knownValues: loadKnownValues(RELEASES_DIR) })]));
  }
  reportPanelValidation((rel.panelDiagnostics || []).map(diagnostic => ({ ...diagnostic, file: `releases/${folder}/${diagnostic.path || 'panels'}` })));
}

async function build({ incrementalRelease = '', incrementalCuration = '' } = {}) {
  const incremental = Boolean(incrementalRelease || incrementalCuration);
  // The output is fully generated. Clear it first so renamed/removed pages and
  // assets cannot survive from an older build with stale repository links.
  if (!incremental) await fs.rm(OUT_DIR, { recursive: true, force: true });
  await ensureDir(OUT_DIR);
  if (!incremental) {
    await writeFileEnsured(path.join(OUT_DIR, 'schema', 'info-yaml.json'), JSON.stringify(infoYamlJsonSchema, null, 2));
    await copySharedAssets();
    await writeOgPng(path.join(OUT_DIR, 'assets', 'og', 'default.png'), ogImageOptionsForSite());
  }

  const releaseFolders = (await listSubdirs(RELEASES_DIR)).sort();

  let releases = incremental ? await readDevCache() : [];
  if (incremental && (!releases || !await fs.stat(path.join(OUT_DIR, 'cards.json')).catch(() => null))) {
    console.log('[sitegen] Incremental cache unavailable; running a clean build.');
    return build();
  }
  if (incrementalRelease && !releaseFolders.includes(incrementalRelease)) {
    console.log(`[sitegen] Release ${incrementalRelease} was removed; running a clean build.`);
    return build();
  }
  const normalizedCards = [];
  const rawInfoIndex = [];
  const validationResults = []; // non-fatal info.yaml conformance pass
  const panelValidationResults = [];
  const creatorSet = new Set();
  const foldersToDiscover = incrementalRelease ? [incrementalRelease] : (incrementalCuration ? [] : releaseFolders);
  // Discovery waits mostly on `git blame`, so overlap several folders.
  const discovered = await mapLimit(foldersToDiscover, CONCURRENCY, async folder => {
    const hasFiles = (await fs.readdir(path.join(RELEASES_DIR, folder))).length > 0;
    return hasFiles ? discoverRelease(folder) : null;
  });
  for (const rel of discovered) {
    if (!rel) continue;
    const folder = rel.folderName;
    if (incrementalRelease) {
      const index = releases.findIndex(candidate => candidate.folderName === folder);
      if (index >= 0) releases[index] = rel;
      else releases.push(rel);
    } else {
      releases.push(rel);
    }
    const unsafePanelDiagnostics = (rel.panelDiagnostics || []).filter(diagnostic => diagnostic.severity === 'error');
    if (unsafePanelDiagnostics.length) {
      throw new Error(`${folder} has invalid custom panels: ${unsafePanelDiagnostics.map(item => item.message).join(' ')}`);
    }
    for (const diagnostic of rel.panelDiagnostics || []) {
      panelValidationResults.push({ ...diagnostic, file: `releases/${rel.folderName}/${diagnostic.path || 'panels'}` });
    }
  }

  // Validate the raw author source against the canonical schema. This is a
  // non-fatal reporting pass: it never blocks the build. It runs after
  // discovery so near-duplicate checks can compare against every card.
  const knownValues = collectKnownValues(releases.filter(rel => rel.rawInfoSource).map(rel => ({ id: rel.folderName, data: rel.rawYaml })));
  for (const rel of releases.filter(rel => rel.rawInfoSource && foldersToDiscover.includes(rel.folderName))) {
    const source = parseSource(rel.rawInfoSource, `releases/${rel.folderName}/info.yaml`);
    validationResults.push(validateInfoYaml(source, { knownValues }));
  }

  // Reconstruct shared indexes from the cached release models. This avoids
  // rescanning Git history, firmware, documentation, and panels for unchanged
  // cards during development.
  normalizedCards.push(...releases.filter(rel => rel.rawInfoSource).map(rel => rel.card));
  rawInfoIndex.push(...releases.filter(rel => rel.rawInfoSource).map(rel => ({
    id: rel.folderName,
    slug: rel.slug,
    sourceFile: rel.card.source_file,
    path: `raw-info/${rel.folderName}/info.yaml`,
    uf2Url: rel.latestUf2?.url || '',
    uf2Downloads: (rel.uf2Downloads || []).map(d => ({
      name: d.name,
      url: d.url,
      ...(d.external ? { external: true } : {}),
      ...(d.host ? { host: d.host } : {}),
      ...(d.sha256 ? { sha256: d.sha256 } : {}),
    })),
    availableUf2Downloads: (rel.availableUf2Downloads || []).map(d => ({
      name: d.name,
      url: d.url,
      path: d.path || String(d.rel || '').replace(new RegExp(`^releases/${rel.folderName}/`, 'i'), ''),
      ...(d.rel ? { rel: d.rel } : {}),
      ...(d.sha256 ? { sha256: d.sha256 } : {}),
    })),
    uf2Files: rel.trackedUf2 || [],
    sourceUrl: rel.card?.source_url || '',
    readmeUrl: rel.card?.readme_url || '',
    web: rel.web ? {
      mode: rel.web.mode || 'none',
      editorUrl: rel.web.editorUrl || '',
      siteSubdir: rel.web.siteSubdir || 'web',
      entry: rel.web.entry || '',
    } : null,
    yamlUrl: rel.card?.source_file
      ? `https://github.com/${REPO}/blob/${BRANCH}/${rel.card.source_file}`
      : '',
  })));
  for (const card of normalizedCards) {
    const creatorVal = (card.metadata?.creator || 'Unknown').toString().trim() || 'Unknown';
    creatorSet.add(creatorVal);
  }

  // Canonical index order is numeric by card number, not lexical folder name,
  // so 12 appears before 100 (and 100 before 303/433).
  normalizedCards.sort((a, b) => {
    const number = card => Number.parseInt(String(card.release || card.id || '').split('/')[0].split('_')[0], 10);
    const aNumber = number(a);
    const bNumber = number(b);
    if (Number.isNaN(aNumber) && Number.isNaN(bNumber)) return String(a.id).localeCompare(String(b.id), undefined, { numeric: true });
    if (Number.isNaN(aNumber)) return 1;
    if (Number.isNaN(bNumber)) return -1;
    return aNumber - bNumber || String(a.id).localeCompare(String(b.id), undefined, { numeric: true });
  });
  if (!incrementalCuration) {
    const firmwareFingerprints = await buildFirmwareFingerprints(normalizedCards, ROOT);
    await writeFileEnsured(
      path.join(OUT_DIR, 'firmware-fingerprints.json'),
      JSON.stringify(firmwareFingerprints, null, 2),
    );
  }

  // Index page
  const discoveryHtml = renderDiscovery(normalizedCards, '.');
  // Results use the same one-line rows as the archive page.
  const resultsRows = renderArchiveRows(normalizedCards, '.');
  const creatorOptions = ['<option value="">All</option>'].concat(
    Array.from(creatorSet).sort((a,b)=>a.localeCompare(b)).map(v=>`<option value="${escapeAttr(v)}">${v}</option>`)
  ).join('');
  const flairIds = new Set(curation.availableFlairs.map(flair => flair.id));
  const filterTags = new Map(curation.availableFlairs.map(flair => [flair.id, flair.label]));
  for (const tag of normalizedCards.flatMap(card => Array.isArray(card.tags) ? card.tags : [])) {
    const id = curation.slugify(tag);
    if (id && !filterTags.has(id)) filterTags.set(id, tag);
  }
  const tagOptions = [...filterTags.entries()]
    .sort((a, b) => {
      const sourceOrder = Number(flairIds.has(b[0])) - Number(flairIds.has(a[0]));
      return sourceOrder || a[1].localeCompare(b[1]);
    })
    .map(([id, label])=>{
      const flair = curation.availableFlairs.find(candidate => candidate.id === id);
      // Match the badge styling used on tiles and rows (see renderFlairBadges).
      const tagStyle = [
        flair?.color ? `--program-card-tag-bg: ${flair.color}; --program-card-tag-border: ${flair.color};` : '',
        flair?.textColor ? ` --program-card-tag-ink: ${flair.textColor};` : '',
      ].join('').trim();
      const style = tagStyle ? ` style="${escapeAttr(tagStyle)}"` : '';
      const tagClass = flair ? `program-card-tag--${escapeAttr(id)}` : 'program-card-tag--author';
      return `<label class="tag-filter-option program-card-tag ${tagClass}" data-tag-option data-tag-label="${escapeAttr(label.toLowerCase())}" data-tag-source="${flair ? 'flair' : 'author'}"${flair ? '' : ' hidden'}${style}><input type="checkbox" name="filter-tag" value="${escapeAttr(id)}"> <span>${escapeAttr(label)}</span></label>`;
    }).join('');
  const sortOptions = [
    ['', 'Card number'],
    ['created-desc', 'Newest added'],
    ['created-asc', 'Oldest added'],
    ['name-asc', 'Name A\u2013Z'],
    ['name-desc', 'Name Z\u2013A'],
    ['number-desc', 'Number (high to low)'],
  ].map(([v,l])=>`<option value="${v}">${l}</option>`).join('');
  const indexHtml = renderLayout({
    title: 'Workshop Computer Program Cards',
    relativeRoot: '.',
    showProgramIdentity: true,
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial('', { title: 'Workshop Computer Program Cards' }),
    content: `
${renderFilterBar({ creatorOptions, sortOptions, tagOptions, linkHref: 'archive/', linkText: `Browse all ${normalizedCards.length} cards`, linkId: 'all-cards-toggle' })}
<div class="program-cards program-cards--index">
  ${discoveryHtml}
  <div id="search-results" hidden>
    <section class="program-card-archive">
      <header class="program-card-shelf__header"><h2 id="cards-count"></h2></header>
      <div class="program-card-archive-list">${resultsRows}</div>
      <p id="no-results" class="program-card-empty" hidden>No matching cards.</p>
    </section>
  </div>
</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'index.html'), indexHtml);

  const randomCandidates = normalizedCards
    .filter(card => !['02_comingsoon', '77_Placeholder', '88_Blank'].includes(card.id))
    .map(card => ({ slug: card.slug, title: card.title }));
  const randomHtml = renderLayout({
    title: 'Finding a random card – Workshop Computer',
    relativeRoot: '..',
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial('random/', {
      title: 'Finding a random card – Workshop Computer',
      description: 'Choosing a random Workshop Computer program card.',
    }),
    content: `
<section class="program-card-random" aria-live="polite">
  <span class="program-card-random__spinner" aria-hidden="true"></span>
  <h1>Taking you to a random card</h1>
  <p>Choosing from ${normalizedCards.length} program cards.</p>
  <noscript><p>JavaScript is required to choose a random card. <a href="../index.html">Return to Program Cards</a>.</p></noscript>
</section>
<script>
(function(){
  var cards=${JSON.stringify(randomCandidates).replace(/</g, '\\u003c')};
  if(!cards.length){window.location.replace('../index.html');return;}
  var card=cards[Math.floor(Math.random()*cards.length)];
  window.setTimeout(function(){window.location.replace('../programs/'+encodeURIComponent(card.slug)+'/');},2000);
})();
</script>`
  });
  if (!incrementalCuration) await writeFileEnsured(path.join(OUT_DIR, 'random', 'index.html'), randomHtml);

  // Archive page (complete one-line index, with search + sort)
  const archiveHtml = renderLayout({
    title: 'All cards – Workshop Computer',
    relativeRoot: '..',
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial('archive/', {
      title: 'All cards – Workshop Computer',
      description: 'Browse all Workshop Computer program cards.',
    }),
    content: `
<div class="program-cards program-cards--archive">
  <header class="program-cards__title">
    <h1>All cards</h1>
    <nav class="program-cards__links" aria-label="Program card links">
      <a href="../index.html">Program cards home</a>
      <a href="https://www.musicthing.co.uk/workshopsystem/program-cards/install/" target="_blank" rel="noopener noreferrer">Installation${externalLinkArrow()}<span class="sr-only"> (opens in a new tab)</span></a>
      <a href="https://github.com/${REPO}" target="_blank" rel="noopener noreferrer">Make a card${externalLinkArrow()}<span class="sr-only"> (opens in a new tab)</span></a>
    </nav>
  </header>
  ${renderFilterBar({ creatorOptions, sortOptions, tagOptions, linkHref: '../index.html', linkText: 'Program cards home', label: 'Search cards' })}
  ${renderArchive(normalizedCards, '..')}
</div>`
  });
  if (incrementalCuration !== 'discovery') await writeFileEnsured(path.join(OUT_DIR, 'archive', 'index.html'), archiveHtml);
  if (!incrementalCuration) await writeFileEnsured(path.join(OUT_DIR, 'cards.json'), JSON.stringify({
    schema: {
      id: schemaAdapter.id,
      version: schemaAdapter.version,
      source: schemaAdapter.source,
      requiredFields: schemaAdapter.requiredFields().map(field => field.path),
    },
    cards: normalizedCards,
  }, null, 2));
  if (!incrementalCuration) await writeFileEnsured(path.join(OUT_DIR, 'raw-info', 'index.json'), JSON.stringify(rawInfoIndex, null, 2));

  const releaseOutputs = incrementalRelease
    ? releases.filter(rel => rel.folderName === incrementalRelease)
    : (incrementalCuration ? [] : releases);
  for (const rel of releaseOutputs) await writeRawInfo(rel);

  const detailOutputs = incrementalCuration === 'flairs' ? releases : releaseOutputs;
  await mapLimit(detailOutputs, CONCURRENCY, rel => writeCardPage(rel, { copyWeb: !incrementalCuration }));

  // 404 fallback
  if (!incremental) await writeFileEnsured(path.join(OUT_DIR, '404.html'), renderLayout({
    title: 'Not found',
    relativeRoot: '.',
    // GitHub Pages preserves the missing URL when it serves 404.html. Load the
    // redirect helper from the custom-domain root regardless of path depth.
    legacyRedirectRoot: '',
    repoUrl: `https://github.com/${REPO}`,
    social: pageSocial('404.html', { title: 'Not found', description: 'Page not found.' }),
    content: '<h1>404</h1><p>Page not found.</p>'
  }));

  // Author preview/editor (static, client-side; reuses the shared engine).
  const suggestionValues = key => [...new Set(normalizedCards.map(card => card.metadata?.[key]).filter(Boolean).map(String))]
    .sort((a, b) => a.localeCompare(b));
  const suggestions = {
    creators: suggestionValues('creator'),
    languages: suggestionValues('language'),
    statuses: [...new Set(['WIP', 'Beta', 'Released', ...suggestionValues('status')])].sort((a, b) => a.localeCompare(b)),
    tags: [...new Set([
      ...curation.availableFlairs.map(flair => flair.label),
      ...normalizedCards.flatMap(card => Array.isArray(card.tags) ? card.tags : []),
    ])].sort((a, b) => a.localeCompare(b)),
    // Lets the author page warn about near-duplicate tag/Language/Status values.
    knownValues,
  };
  if (!incremental) await buildPreviewTool(suggestions);
  else if (!incrementalCuration || incrementalCuration === 'flairs') await buildPreviewPages(suggestions);
  if (!incremental || incrementalCuration === 'flairs') await buildCurationEditor(normalizedCards);

  if (!incrementalCuration) await writeDevCache(releases);

  const buildKind = incrementalRelease
    ? `Incrementally rebuilt ${incrementalRelease}`
    : incrementalCuration
      ? `Incrementally rebuilt ${incrementalCuration} curation`
      : `Built site with ${rawInfoIndex.length} metadata cards from ${releases.length} release folders`;
  console.log(`${buildKind} -> ${OUT_DIR}`);
  reportValidation(validationResults);
  reportPanelValidation(panelValidationResults);
}

// Shared source modules shipped to the browser author preview. Copied verbatim
// (preserving relative paths) so client-side validation/rendering matches the
// build exactly. Keep this list in sync with the preview client's imports.
const PREVIEW_LIB_FILES = [
  'utils/strings.js',
  'utils/similarValues.js',
  'utils/youtube.js',
  'utils/instagram.js',
  'utils/video.js',
  'utils/audio.js',
  'utils/markdown.js',
  'utils/flash.js',
  'utils/previewFirmware.js',
  'utils/previewWeb.js',
  'schema/schemaDefinition.js',
  'schema/schemaAdapter.js',
  'schema/infoYamlJsonSchema.js',
  'validate/parseSource.js',
  'validate/ajvStructural.js',
  'validate/validateInfoYaml.js',
  'validate/rules/index.js',
  'model/card.js',
  'render/panelPositions.js',
  'render/icons.js',
  'render/githubIssue.js',
  'render/flashChip.js',
  'render/cardPage.js',
];

/** Build the static author preview/editor under site/preview/. */
async function buildPreviewTool(suggestions = {}) {
  const previewDir = path.join(OUT_DIR, 'preview');
  const libDir = path.join(previewDir, 'lib');
  const vendorDir = path.join(previewDir, 'vendor');
  await ensureDir(previewDir);

  // Shared engine + renderer source (mirrors src/ layout under lib/).
  for (const rel of PREVIEW_LIB_FILES) {
    const dest = path.join(libDir, rel);
    await ensureDir(path.dirname(dest));
    await fs.copyFile(path.join(__dirname, rel), dest);
  }

  // Vendor browser builds for the bare `yaml` / `marked` / `ajv` imports.
  const nodeModules = path.join(ROOT, 'tools', 'sitegen', 'node_modules');
  await fs.cp(path.join(nodeModules, 'yaml', 'browser'), path.join(vendorDir, 'yaml'), { recursive: true });
  await ensureDir(vendorDir);
  await fs.copyFile(
    path.join(nodeModules, 'marked', 'lib', 'marked.esm.js'),
    path.join(vendorDir, 'marked.esm.js')
  );
  await esbuild({
    stdin: {
      contents: "export { default } from 'sanitize-html';",
      resolveDir: __dirname,
      sourcefile: 'sanitize-html-browser-entry.js',
      loader: 'js',
    },
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2020'],
    outfile: path.join(vendorDir, 'sanitize-html.esm.js'),
    minify: true,
  });
  await esbuild({
    stdin: {
      contents: "export { default } from 'ajv';",
      resolveDir: __dirname,
      sourcefile: 'ajv-browser-entry.js',
      loader: 'js',
    },
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2020'],
    outfile: path.join(vendorDir, 'ajv.esm.js'),
    minify: true,
  });

  // Monaco remains a separate browser bundle so the author page only downloads
  // and initializes it when Advanced YAML mode is opened.
  await esbuild({
    entryPoints: [path.join(ROOT, 'tools', 'sitegen', 'assets', 'preview', 'monaco-editor.js')],
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2020'],
    outfile: path.join(previewDir, 'monaco-editor.js'),
    loader: { '.ttf': 'dataurl' },
    minify: true,
  });
  await esbuild({
    entryPoints: [path.join(nodeModules, 'monaco-editor', 'esm', 'vs', 'editor', 'editor.worker.js')],
    bundle: true,
    format: 'iife',
    platform: 'browser',
    target: ['es2020'],
    outfile: path.join(previewDir, 'monaco-editor-worker.js'),
    minify: true,
  });
  await esbuild({
    entryPoints: [path.join(nodeModules, 'monaco-yaml', 'yaml.worker.js')],
    bundle: true,
    format: 'iife',
    platform: 'browser',
    target: ['es2020'],
    outfile: path.join(previewDir, 'monaco-yaml-worker.js'),
    minify: true,
  });

  // Client script + page.
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'preview', 'author-client.js'),
    path.join(previewDir, 'author-client.js')
  );
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'preview', 'author.css'),
    path.join(previewDir, 'author.css')
  );
  await buildPreviewPages(suggestions);
}

/** Refresh only the suggestion-bearing author pages, leaving vendor bundles intact. */
async function buildPreviewPages(suggestions = {}) {
  const previewDir = path.join(OUT_DIR, 'preview');
  const authorSocial = (relativePath, description) => pageSocial(relativePath, {
    title: 'Author page – Workshop Computer',
    description,
  });
  await writeFileEnsured(path.join(previewDir, 'index.html'), renderAuthorPage({
    documentKind: 'existing',
    suggestions,
    social: authorSocial('preview/', 'Inspect and edit program card metadata.'),
  }));
  await writeFileEnsured(path.join(previewDir, 'new', 'index.html'), renderAuthorPage({
    documentKind: 'new',
    suggestions,
    social: authorSocial('preview/new/', 'Create a program card page visually, then download the generated info.yaml.'),
  }));
}

/** Build the self-contained flair editor under site/documentation/. */
async function buildCurationEditor(cards = []) {
  const editorDir = path.join(OUT_DIR, 'documentation');
  await ensureDir(editorDir);

  const cardRows = cards
    .map(card => ({
      id: card.id,
      title: card.title || card.id,
      shortDescription: card.short_description || '',
    }))
    .sort((a, b) => a.id.localeCompare(b.id, undefined, { numeric: true }));

  const shelfReferences = {};
  for (const shelf of curation.discovery?.shelves || []) {
    for (const flairId of [...(shelf.cards_from_flairs || []), ...(shelf.hide_flairs || [])]) {
      const id = curation.slugify(flairId);
      if (!id) continue;
      if (!shelfReferences[id]) shelfReferences[id] = [];
      shelfReferences[id].push(shelf.title || shelf.id || 'Unnamed shelf');
    }
  }

  const data = JSON.stringify({
    cards: cardRows,
    availableTags: curation.availableFlairs,
    assignments: curation.assignments,
    shelfReferences,
  }).replaceAll('<', '\\u003c');
  const template = await fs.readFile(path.join(ROOT, 'documentation', 'flair-editor.html'), 'utf8');
  if (!template.includes('__CURATION_DATA_JSON__')) {
    throw new Error('documentation/flair-editor.html is missing its data placeholder');
  }
  await writeFileEnsured(
    path.join(editorDir, 'flair-editor.html'),
    template.replace('__CURATION_DATA_JSON__', data),
  );
}

/** Print a concise, non-fatal summary of the info.yaml conformance pass. */
function reportValidation(results) {
  if (!results.length) return;
  const errorCount = results.reduce((n, r) => n + r.errorCount, 0);
  const warningCount = results.reduce((n, r) => n + r.warningCount, 0);
  const failing = results.filter(r => r.errorCount > 0);
  if (!errorCount && !warningCount) {
    console.log('info.yaml validation: all cards conform to documentation/info.yaml.md.');
    return;
  }
  console.log(`info.yaml validation (non-fatal): ${errorCount} error(s), ${warningCount} warning(s) across ${results.length} card(s).`);
  if (failing.length) {
    console.log(`  ${failing.length} card(s) with errors: ${failing.map(r => r.file.replace(/^releases\//, '').replace(/\/info\.yaml$/, '')).join(', ')}`);
    console.log('  Run `npm run validate-info` for details.');
  }
}

function reportPanelValidation(diagnostics) {
  if (!diagnostics.length) return;
  const errors = diagnostics.filter(item => item.severity === 'error').length;
  const warnings = diagnostics.filter(item => item.severity === 'warning').length;
  console.log(`custom panel validation (non-fatal): ${errors} error(s), ${warnings} warning(s).`);
  for (const item of diagnostics) console.log(`  ${item.severity}: ${item.file}: ${item.message}`);
}

const args = process.argv.slice(2);
const releaseArg = args.indexOf('--incremental-release');
const curationArg = args.indexOf('--incremental-curation');
const cardArg = args.indexOf('--card');
const options = {
  incrementalRelease: releaseArg >= 0 ? String(args[releaseArg + 1] || '') : '',
  incrementalCuration: curationArg >= 0 ? String(args[curationArg + 1] || '') : '',
};
if (options.incrementalCuration && !['discovery', 'flairs'].includes(options.incrementalCuration)) {
  throw new Error(`Unknown incremental curation target: ${options.incrementalCuration}`);
}

(cardArg >= 0 ? buildCard(String(args[cardArg + 1] || '')) : build(options)).catch(err => {
  console.error(err);
  process.exit(1);
});
