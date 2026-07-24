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
import { renderDiscovery, renderArchive, renderTile } from './render/discovery.js';
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

// Resolve repo details (for GitHub raw links)
const DEFAULT_REPO = 'TomWhitwell/Workshop_Computer';
const DEFAULT_BRANCH = 'main';


// GitHub Actions supplies the canonical repository and deployed commit. Local
// builds deliberately use the upstream defaults rather than the origin remote,
// which may point at a contributor's fork. SITE_REPOSITORY/SITE_REF remain
// available for an explicit preview override.
const REPO = process.env.SITE_REPOSITORY || process.env.GITHUB_REPOSITORY || DEFAULT_REPO;
const BRANCH = process.env.SITE_REF || process.env.GITHUB_SHA || process.env.GITHUB_REF_NAME || DEFAULT_BRANCH;
const PAGES_BASE = githubPagesBase(REPO);
const schemaAdapter = getInfoYamlSchemaAdapter();


// (info parsing handled in discover/release.js)

function makeRawUrl(relPathFromRepoRoot) {
  return makeRawUrlExternal(REPO, BRANCH, relPathFromRepoRoot);
}

async function discoverRelease(folderName) {
  const outPrograms = path.join(OUT_DIR, 'programs');
  return discoverReleaseMod(RELEASES_DIR, folderName, outPrograms, makeRawUrl, PAGES_BASE, REPO, BRANCH);
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
    curatedTags: curation.resolveFlair(card.id),
    panelImg: '../../assets/program_cards/Standalone_computer_rev1.svg',
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
  <a class="program-card-author-link" href="../../preview/#${encodeURIComponent(rel.slug)}">Author Metadata ↗</a>
</nav>
${article}
<div class="actions actions-duo">
  <a class="program-card-nav-link" href="../../index.html">Back to all programs</a>
  <a class="program-card-nav-link" href="#page-top">Back to top</a>
</div>
`
  });
}

async function build() {
  // The output is fully generated. Clear it first so renamed/removed pages and
  // assets cannot survive from an older build with stale repository links.
  await fs.rm(OUT_DIR, { recursive: true, force: true });
  await ensureDir(OUT_DIR);
  await ensureDir(path.join(OUT_DIR, 'assets'));
  await writeFileEnsured(path.join(OUT_DIR, 'schema', 'info-yaml.json'), JSON.stringify(infoYamlJsonSchema, null, 2));
  // Copy physical CSS asset
  const cssSrc = path.join(ROOT, 'tools', 'sitegen', 'assets', 'style.css');
  const cssDest = path.join(OUT_DIR, 'assets', 'style.css');
  await fs.copyFile(cssSrc, cssDest);

  // Copy GitHub-flavoured markdown stylesheet (used by embedded README bodies)
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'github-markdown.css'),
    path.join(OUT_DIR, 'assets', 'github-markdown.css')
  );

  // Copy program-card detail-page stylesheet
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'program-cards.css'),
    path.join(OUT_DIR, 'assets', 'program-cards.css')
  );
  await ensureDir(path.join(OUT_DIR, 'assets', 'fonts'));
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'node_modules', '@fontsource', 'inter', 'files', 'inter-latin-800-normal.woff2'),
    path.join(OUT_DIR, 'assets', 'fonts', 'inter-latin-800-normal.woff2')
  );

  // Copy program-card panel diagram asset
  const panelSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'program_cards');
  const panelDestDir = path.join(OUT_DIR, 'assets', 'program_cards');
  await ensureDir(panelDestDir);
  for (const f of await fs.readdir(panelSrcDir)) {
    await fs.copyFile(path.join(panelSrcDir, f), path.join(panelDestDir, f));
  }

  // Copy JS assets (picoboot / uf2 libs for WebUSB programmer)
  const jsSrcDir = path.join(ROOT, 'tools', 'sitegen', 'assets', 'js');
  const jsDestDir = path.join(OUT_DIR, 'assets', 'js');
  await ensureDir(jsDestDir);
  for (const f of await fs.readdir(jsSrcDir)) {
    if (f.endsWith('.js')) await fs.copyFile(path.join(jsSrcDir, f), path.join(jsDestDir, f));
  }

  const releaseFolders = (await listSubdirs(RELEASES_DIR)).sort();

  const releases = [];
  const normalizedCards = [];
  const rawInfoIndex = [];
  const validationResults = []; // non-fatal info.yaml conformance pass
  const panelValidationResults = [];
  const creatorSet = new Set();
  for (const folder of releaseFolders) {
    const relPath = path.join(RELEASES_DIR, folder);
    const hasFiles = (await fs.readdir(relPath)).length > 0;
    if (!hasFiles) continue;
    const rel = await discoverRelease(folder);
    releases.push(rel);
    for (const diagnostic of rel.panelDiagnostics || []) {
      panelValidationResults.push({ ...diagnostic, file: `releases/${rel.folderName}/${diagnostic.path || 'panels'}` });
    }
    if (rel.rawInfoSource) {
      normalizedCards.push(rel.card);
      rawInfoIndex.push({
        id: rel.folderName,
        slug: rel.slug,
        sourceFile: rel.card.source_file,
        path: `raw-info/${rel.folderName}/info.yaml`,
        // Build-discovered assets the browser preview cannot compute on its own,
        // so it can render the same download/source/readme links a full build
        // would produce.
        uf2Url: rel.latestUf2?.url || '',
        uf2Downloads: (rel.uf2Downloads || []).map(d => ({
          name: d.name,
          url: d.url,
          ...(d.external ? { external: true } : {}),
          ...(d.host ? { host: d.host } : {}),
          ...(d.sha256 ? { sha256: d.sha256 } : {}),
        })),
        uf2Files: rel.trackedUf2 || [],
        sourceUrl: rel.card?.source_url || '',
        readmeUrl: rel.card?.readme_url || '',
        yamlUrl: rel.card?.source_file
          ? `https://github.com/${REPO}/blob/${BRANCH}/${rel.card.source_file}`
          : '',
      });
      // Validate the raw author source against the canonical schema. This is a
      // non-fatal reporting pass: it never blocks the build.
      const source = parseSource(rel.rawInfoSource, `releases/${rel.folderName}/info.yaml`);
      validationResults.push(validateInfoYaml(source));
      const meta = rel.card?.metadata || {};
      const creatorVal = (meta.creator || 'Unknown').toString().trim() || 'Unknown';
      creatorSet.add(creatorVal);
    }
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
  const firmwareFingerprints = await buildFirmwareFingerprints(normalizedCards, ROOT);
  await writeFileEnsured(
    path.join(OUT_DIR, 'firmware-fingerprints.json'),
    JSON.stringify(firmwareFingerprints, null, 2),
  );

  // Index page
  const discoveryHtml = renderDiscovery(normalizedCards, '.');
  const resultsTiles = normalizedCards.map(card => renderTile(card, { root: '.', showAllTags: true, showCreator: true })).join('');
  const creatorOptions = ['<option value="">All</option>'].concat(
    Array.from(creatorSet).sort((a,b)=>a.localeCompare(b)).map(v=>`<option value="${escapeAttr(v)}">${v}</option>`)
  ).join('');
  const curatedTagIds = new Set(curation.availableTags.map(tag => tag.id));
  const filterTags = new Map(curation.availableTags.map(tag => [tag.id, tag.label]));
  for (const tag of normalizedCards.flatMap(card => Array.isArray(card.tags) ? card.tags : [])) {
    const id = curation.slugify(tag);
    if (id && !filterTags.has(id)) filterTags.set(id, tag);
  }
  const tagOptions = [...filterTags.entries()]
    .sort((a, b) => {
      const sourceOrder = Number(curatedTagIds.has(b[0])) - Number(curatedTagIds.has(a[0]));
      return sourceOrder || a[1].localeCompare(b[1]);
    })
    .map(([id, label])=>{
      const curated = curation.availableTags.find(tag => tag.id === id);
      const style = curated?.color ? ` style="--tag-selected-bg:${escapeAttr(curated.color)}"` : '';
      return `<label class="tag-filter-option" data-tag-option data-tag-label="${escapeAttr(label.toLowerCase())}" data-tag-source="${curated ? 'curated' : 'author'}"${curated ? '' : ' hidden'}${style}><input type="checkbox" name="filter-tag" value="${escapeAttr(id)}"> <span>${escapeAttr(label)}</span></label>`;
    }).join('');
  const sortOptions = [
    ['', 'Card number'],
    ['updated-desc', 'Recently updated'],
    ['updated-asc', 'Oldest updated'],
    ['name-asc', 'Name A\u2013Z'],
    ['name-desc', 'Name Z\u2013A'],
    ['number-desc', 'Number (high to low)'],
  ].map(([v,l])=>`<option value="${v}">${l}</option>`).join('');
  const indexHtml = renderLayout({
    title: 'Workshop Computer Program Cards',
    relativeRoot: '.',
    showProgramIdentity: true,
    programCardCount: normalizedCards.length,
  repoUrl: `https://github.com/${REPO}`,
    content: `
<section class="filter-bar" aria-label="Filter programs">
    <div class="search-bar-row">
      <div class="search-wrapper">
        <div class="search-control">
          <svg class="search-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5"></circle><path d="m15.5 15.5 5 5"></path></svg>
          <input type="text" id="filter-search" placeholder="Search by name, creator, function or tag…" class="search-input" aria-label="Search cards">
          <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
        </div>
      </div>
    </div>
    <div class="search-tools-row">
      <details class="advanced-options">
        <summary>Advanced search</summary>
        <div class="filter-row">
          <div class="filter-group">
            <label for="filter-creator">Creator</label>
            <select id="filter-creator">${creatorOptions}</select>
          </div>
          <div class="filter-group">
            <label for="sort-mode">Sort</label>
            <select id="sort-mode">${sortOptions}</select>
          </div>
          <fieldset class="filter-group tag-filter-group">
            <legend class="tag-filter-heading"><span>Tags</span><span class="tag-filter-heading__actions"><button id="clear-tags" type="button" hidden>Clear</button><button id="toggle-all-tags" type="button" aria-pressed="false">Show all</button></span></legend>
            <input id="filter-tag-search" class="tag-filter-search" type="search" placeholder="Search all tags…" aria-label="Search all tags" autocomplete="off">
            <div id="filter-tags" class="tag-filter-options">${tagOptions}</div>
          </fieldset>
        </div>
      </details>
      <a class="filter-link" href="archive/">Browse all cards</a>
      <button id="connectToggle" class="connect-toggle connect-toggle--search" type="button" role="switch" aria-checked="false" aria-label="Connect to RP2040 via WebUSB" title="Reboot computer into programming mode before connecting">
        <span class="c-status" aria-hidden="true"></span><span class="c-label">Connect workshop computer</span>
      </button>
    </div>
</section>
<div class="program-cards program-cards--index">
  ${discoveryHtml}
  <div id="search-results" hidden>
    <section class="program-card-shelf">
      <header class="program-card-shelf__header"><h2>Results <span id="cards-count"></span></h2></header>
      <div class="program-card-grid program-card-grid--list">${resultsTiles}</div>
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
  await writeFileEnsured(path.join(OUT_DIR, 'random', 'index.html'), randomHtml);

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
      <a href="../index.html">Program cards</a>
      <a href="https://github.com/${REPO}">Make a card</a>
    </nav>
  </header>
  <section class="filter-bar" aria-label="Search cards">
      <div class="search-wrapper">
        <div class="search-control">
          <svg class="search-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5"></circle><path d="m15.5 15.5 5 5"></path></svg>
          <input type="text" id="filter-search" placeholder="Search by name, creator, function or tag…" class="search-input" aria-label="Search cards">
          <button id="search-clear" class="search-clear" aria-label="Clear search" type="button">✕</button>
        </div>
      </div>
      <div class="filter-actions">
        <div class="filter-group">
          <label for="sort-mode">Sort</label>
          <select id="sort-mode">${sortOptions}</select>
        </div>
      </div>
  </section>
  ${renderArchive(normalizedCards, '..')}
</div>`
  });
  await writeFileEnsured(path.join(OUT_DIR, 'archive', 'index.html'), archiveHtml);
  await writeFileEnsured(path.join(OUT_DIR, 'cards.json'), JSON.stringify({
    schema: {
      id: schemaAdapter.id,
      version: schemaAdapter.version,
      source: schemaAdapter.source,
      requiredFields: schemaAdapter.requiredFields().map(field => field.path),
    },
    cards: normalizedCards,
  }, null, 2));
  await writeFileEnsured(path.join(OUT_DIR, 'raw-info', 'index.json'), JSON.stringify(rawInfoIndex, null, 2));

  for (const rel of releases) {
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

  for (const rel of releases) {
    const base = path.join(OUT_DIR, 'programs', rel.slug);
    await ensureDir(base);
    const html = detailPage(rel);
    await writeFileEnsured(path.join(base, 'index.html'), html);

    if (rel.web?.copySrc) {
      const webDest = path.join(base, rel.web.siteSubdir || 'web');
      await copyWebAssets(rel.web.copySrc, webDest);
    }
  }

  // 404 fallback
  await writeFileEnsured(path.join(OUT_DIR, '404.html'), renderLayout({
    title: 'Not found',
    relativeRoot: '.',
  repoUrl: `https://github.com/${REPO}`,
    content: '<h1>404</h1><p>Page not found.</p>'
  }));

  // Author preview/editor (static, client-side; reuses the shared engine).
  const suggestionValues = key => [...new Set(normalizedCards.map(card => card.metadata?.[key]).filter(Boolean).map(String))]
    .sort((a, b) => a.localeCompare(b));
  await buildPreviewTool({
    creators: suggestionValues('creator'),
    languages: suggestionValues('language'),
    statuses: [...new Set(['WIP', 'Beta', 'Released', ...suggestionValues('status')])].sort((a, b) => a.localeCompare(b)),
    tags: [...new Set([
      ...curation.availableTags.map(tag => tag.label),
      ...normalizedCards.flatMap(card => Array.isArray(card.tags) ? card.tags : []),
    ])].sort((a, b) => a.localeCompare(b)),
  });

  console.log(`Built site with ${rawInfoIndex.length} metadata cards from ${releases.length} release folders -> ${OUT_DIR}`);
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
  'schema/schemaDefinition.js',
  'schema/schemaAdapter.js',
  'schema/infoYamlJsonSchema.js',
  'validate/parseSource.js',
  'validate/ajvStructural.js',
  'validate/validateInfoYaml.js',
  'validate/rules/index.js',
  'model/card.js',
  'render/panelPositions.js',
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

  // Client script + page.
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'preview', 'author-client.js'),
    path.join(previewDir, 'author-client.js')
  );
  await fs.copyFile(
    path.join(ROOT, 'tools', 'sitegen', 'assets', 'preview', 'author.css'),
    path.join(previewDir, 'author.css')
  );
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

build().catch(err => {
  console.error(err);
  process.exit(1);
});

