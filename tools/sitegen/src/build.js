import { fsAsync as fs, ensureDir, writeFileEnsured, listSubdirs, toPosix } from './utils/fs.js';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { build as esbuild } from 'esbuild';
import { makeRawUrl as makeRawUrlExternal } from './links.js';
import { renderLayout } from './render/layout.js';
import { discoverRelease as discoverReleaseMod } from './discover/release.js';
import { githubPagesBase, copyWebAssets } from './discover/webEditor.js';
import { getInfoYamlSchemaAdapter } from './schema/schemaAdapter.js';
import { infoYamlJsonSchema } from './schema/infoYamlJsonSchema.js';
import { renderCardArticle, renderReadmeAndDocs } from './render/cardPage.js';
import { renderDiscovery, renderArchive, renderArchiveRows } from './render/discovery.js';
import { renderFilterBar } from './render/filterBar.js';
import { externalLinkArrow } from './render/icons.js';
import { curation } from './curation/index.js';
import { parseSource } from './validate/parseSource.js';
import { validateInfoYaml } from './validate/validateInfoYaml.js';
import { renderAuthorPage } from './render/authorPage.js';
import { buildFirmwareFingerprints } from './firmware/fingerprints.js';

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
const SITE_BASE = (() => {
  const configured = String(process.env.SITE_BASE_URL || '').trim();
  if (!configured) return githubPagesBase(REPO);
  const url = new URL(configured);
  if (!/^https?:$/.test(url.protocol)) throw new Error('SITE_BASE_URL must be an HTTP(S) URL.');
  url.search = '';
  url.hash = '';
  if (!url.pathname.endsWith('/')) url.pathname += '/';
  return url.href;
})();
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

function detailPage(rel) {
  const { docs, readmeHtml, card } = rel;
  const uf2Url = rel.latestUf2?.url || '';
  const yamlUrl = card?.source_file
    ? `https://github.com/${REPO}/blob/${BRANCH}/${card.source_file}`
    : `https://github.com/${REPO}`;

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
    title: `${card.title} – Workshop Computer`,
    relativeRoot: '../..',
    repoUrl: `https://github.com/${REPO}`,
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

async function readDevCache() {
  try {
    const parsed = JSON.parse(await fs.readFile(DEV_CACHE_FILE, 'utf8'));
    return parsed.version === 1 && Array.isArray(parsed.releases) ? parsed.releases : null;
  } catch {
    return null;
  }
}

async function writeDevCache(releases) {
  await writeFileEnsured(DEV_CACHE_FILE, JSON.stringify({ version: 1, releases }));
}

async function build({ incrementalRelease = '', incrementalCuration = '' } = {}) {
  const incremental = Boolean(incrementalRelease || incrementalCuration);
  // The output is fully generated. Clear it first so renamed/removed pages and
  // assets cannot survive from an older build with stale repository links.
  if (!incremental) await fs.rm(OUT_DIR, { recursive: true, force: true });
  await ensureDir(OUT_DIR);
  if (!incremental) {
    await ensureDir(path.join(OUT_DIR, 'assets'));
    await writeFileEnsured(path.join(OUT_DIR, 'schema', 'info-yaml.json'), JSON.stringify(infoYamlJsonSchema, null, 2));
  }
  // Copy physical CSS asset
  const cssSrc = path.join(ROOT, 'tools', 'sitegen', 'assets', 'style.css');
  const cssDest = path.join(OUT_DIR, 'assets', 'style.css');
  if (!incremental) await fs.copyFile(cssSrc, cssDest);

  // Copy GitHub-flavoured markdown stylesheet (used by embedded README bodies)
  if (!incremental) await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'github-markdown.css'),
    path.join(OUT_DIR, 'assets', 'github-markdown.css')
  );

  // Copy program-card detail-page stylesheet
  if (!incremental) await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'program-cards.css'),
    path.join(OUT_DIR, 'assets', 'program-cards.css')
  );
  if (!incremental) {
    const faviconDestDir = path.join(OUT_DIR, 'assets', 'favicon');
    await ensureDir(faviconDestDir);
    await fs.copyFile(
      path.join(ROOT, 'tools', 'sitegen', 'assets', 'favicon', 'favicon.png'),
      path.join(faviconDestDir, 'favicon.png')
    );
  }
  if (!incremental) {
    const fontsDir = path.join(OUT_DIR, 'assets', 'fonts');
    const nodeModules = path.join(ROOT, 'tools', 'sitegen', 'node_modules');
    await ensureDir(fontsDir);
    // Variable Inter (weight axis 100-900) is the only self-hosted face; it
    // backs both the body text and the "Workshop Panel" font (panel rendering
    // and SVG export select their weight from its axis).
    await fs.copyFile(
      path.join(nodeModules, '@fontsource-variable', 'inter', 'files', 'inter-latin-wght-normal.woff2'),
      path.join(fontsDir, 'inter-latin-wght-normal.woff2')
    );
  }

  // Copy program-card panel diagram asset
  const panelSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'program_cards');
  const panelDestDir = path.join(OUT_DIR, 'assets', 'program_cards');
  if (!incremental) {
    await ensureDir(panelDestDir);
    for (const f of await fs.readdir(panelSrcDir)) {
      await fs.copyFile(path.join(panelSrcDir, f), path.join(panelDestDir, f));
    }
  }

  // Copy JS assets (picoboot / uf2 libs for WebUSB programmer)
  const jsSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'js');
  const jsDestDir = path.join(OUT_DIR, 'assets', 'js');
  if (!incremental) {
    await ensureDir(jsDestDir);
    for (const f of await fs.readdir(jsSrcDir)) {
      if (f.endsWith('.js')) await fs.copyFile(path.join(jsSrcDir, f), path.join(jsDestDir, f));
    }
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
  for (const folder of foldersToDiscover) {
    const relPath = path.join(RELEASES_DIR, folder);
    const hasFiles = (await fs.readdir(relPath)).length > 0;
    if (!hasFiles) continue;
    const rel = await discoverRelease(folder);
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
    if (rel.rawInfoSource) {
      // Validate the raw author source against the canonical schema. This is a
      // non-fatal reporting pass: it never blocks the build.
      const source = parseSource(rel.rawInfoSource, `releases/${rel.folderName}/info.yaml`);
      validationResults.push(validateInfoYaml(source));
    }
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
    ['created-desc', 'Newest created'],
    ['created-asc', 'Oldest created'],
    ['name-asc', 'Name A\u2013Z'],
    ['name-desc', 'Name Z\u2013A'],
    ['number-desc', 'Number (high to low)'],
  ].map(([v,l])=>`<option value="${v}">${l}</option>`).join('');
  const indexHtml = renderLayout({
    title: 'Workshop Computer Program Cards',
    relativeRoot: '.',
    showProgramIdentity: true,
  repoUrl: `https://github.com/${REPO}`,
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
  for (const rel of releaseOutputs) {
    if (rel.rawInfoSource) {
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
  }

  const detailOutputs = incrementalCuration === 'flairs' ? releases : releaseOutputs;
  for (const rel of detailOutputs) {
    const base = path.join(OUT_DIR, 'programs', rel.slug);
    await ensureDir(base);
    const html = detailPage(rel);
    await writeFileEnsured(path.join(base, 'index.html'), html);

    if (!incrementalCuration && rel.web?.copySrc) {
      const webDest = path.join(base, rel.web.siteSubdir || 'web');
      await copyWebAssets(rel.web.copySrc, webDest);
    }
  }

  // 404 fallback
  if (!incremental) await writeFileEnsured(path.join(OUT_DIR, '404.html'), renderLayout({
    title: 'Not found',
    relativeRoot: '.',
    // GitHub Pages preserves the missing URL when it serves 404.html. Load the
    // redirect helper from the custom-domain root regardless of path depth.
    legacyRedirectRoot: '',
  repoUrl: `https://github.com/${REPO}`,
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
  };
  if (!incremental) await buildPreviewTool(suggestions);
  else if (!incrementalCuration || incrementalCuration === 'flairs') await buildPreviewPages(suggestions);

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
  'utils/youtube.js',
  'utils/audio.js',
  'utils/markdown.js',
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
  await writeFileEnsured(path.join(previewDir, 'index.html'), renderAuthorPage({ documentKind: 'existing', suggestions }));
  await writeFileEnsured(path.join(previewDir, 'new', 'index.html'), renderAuthorPage({ documentKind: 'new', suggestions }));
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
const options = {
  incrementalRelease: releaseArg >= 0 ? String(args[releaseArg + 1] || '') : '',
  incrementalCuration: curationArg >= 0 ? String(args[curationArg + 1] || '') : '',
};
if (options.incrementalCuration && !['discovery', 'flairs'].includes(options.incrementalCuration)) {
  throw new Error(`Unknown incremental curation target: ${options.incrementalCuration}`);
}

build(options).catch(err => {
  console.error(err);
  process.exit(1);
});

