import { test } from 'node:test';
import assert from 'node:assert/strict';
import { renderCardArticle, renderPanelArtwork, renderReadmeAndDocs } from '../src/render/cardPage.js';
import { renderArchive, renderShelf, renderTile } from '../src/render/discovery.js';
import { renderLayout } from '../src/render/layout.js';
import { renderAuthorPage } from '../src/render/authorPage.js';

function card(extra = {}) {
  return {
    id: '42_test', slug: '42-test', title: 'Test & "Card"', release: '42 / 1.0',
    summary: 'Safe **summary**', short_description: 'A short description',
    metadata: { creator: 'A & B', version: '1.0', status: 'Released' },
    panel: {}, switch_modes: {}, leds: [], tags: [], source: [],
    source_url: 'https://example.test/source',
    ...extra,
  };
}

test('card renderer exposes accessible generated panel tabs and default state', () => {
  const generated = card({
    panel_views: {
      source: 'generated', default: 'middle', items: [
        { id: 'up', name: 'Up', panel: { controls: { main: { label: 'Upper\nmode' } } }, switch_modes: {}, leds: [] },
        { id: 'middle', name: 'Middle', panel: { controls: { main: { label: 'Normal' } } }, switch_modes: { tap: 'Set tempo' }, leds: [] },
      ],
    },
  });
  const html = renderCardArticle({ card: generated, panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.match(html, /role="tablist" aria-label="Panel view"/);
  assert.match(html, /data-panel-position-button="middle"[^>]*aria-selected="true"/);
  assert.match(html, /data-panel-position-view="up" hidden aria-hidden="true"/);
  assert.match(html, /data-panel-position-view="middle"/);
  assert.match(html, /Upper<br>mode/);
  assert.match(html, /program-card-switch-position--tap">\s*<button type="button" class="program-card-position-button" disabled>Tap<\/button>\s*<p>Set tempo<\/p>/);
  assert.doesNotMatch(html, /data-panel-position-button="tap"/);
  assert.match(html, /Test &amp; &quot;Card&quot;/);
  assert.match(html, /By A &amp; B/);
});

test('generated socket descriptions preserve unused physical jack positions', () => {
  const generated = card({
    panel_views: {
      source: 'generated', default: 'middle', items: [{
        id: 'middle', name: 'Middle', switch_modes: {}, leds: [],
        panel: { inputs: {
          audio_l: { label: 'Audio input' },
          cv_1: { label: 'Speed CV' },
        } },
      }],
    },
  });
  const html = renderCardArticle({ card: generated, panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.match(html, /Audio 1[\s\S]*program-card-socket--empty" aria-hidden="true"><span>Unused<\/span>[\s\S]*CV 1/);
});

test('tap labels the panel down position only when no down mode is provided', () => {
  const tapOnly = renderPanelArtwork({ panel: {}, switch_modes: { tap: 'Tap Tempo: Set the clock' } }, 'panel.svg');
  assert.match(tapOnly, /program-card-panel-switch-position--down[^>]*aria-label="down switch position: Tap Tempo"[^>]*>[\s\S]*<strong>Tap Tempo<\/strong>/);
  assert.doesNotMatch(tapOnly, /data-panel-position-button="tap"/);

  const withDown = renderPanelArtwork({
    panel: {},
    switch_modes: { down: 'Reset: Hold to clear', tap: 'Tap Tempo: Set the clock' },
  }, 'panel.svg');
  assert.match(withDown, /program-card-panel-switch-position--down[^>]*aria-label="down switch position: Reset"[^>]*>[\s\S]*<strong>Reset<\/strong>/);
  assert.doesNotMatch(withDown, /aria-label="down switch position: Tap Tempo"/);
});

test('custom panel rendering sanitizes authored content and escapes image metadata', () => {
  const custom = card({ panel_views: {
    source: 'custom', default: 'face-a', items: [{
      id: 'face-a', name: 'Face <A>', panel: {}, switch_modes: {}, leds: [],
      image: { url: 'panels/a.svg?x=1&y=2', width: 560, height: 1785 },
      content_html: '<p onclick="alert(1)"><strong>Authored documentation</strong><script>alert(2)</script><a href="javascript:alert(3)">bad</a></p>',
    }],
  } });
  const html = renderCardArticle({ card: custom, panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.match(html, /program-card-panel-views--custom/);
  assert.match(html, /src="panels\/a\.svg\?x=1&amp;y=2"/);
  assert.match(html, /alt="Face &lt;A&gt; panel"/);
  assert.match(html, /<strong>Authored documentation<\/strong>/);
  assert.doesNotMatch(html, /onclick|<script|javascript:/i);
  assert.doesNotMatch(html, /program-card-panel-switch-position/);
});

test('basic rendering omits generated features but keeps actions, metadata, and extra docs', () => {
  const html = renderCardArticle({
    card: card({ videos: [{ id: 'abc', url: 'https://youtu.be/abc' }], audio_samples: [{ kind: 'file', url: 'demo.mp3' }] }),
    panelImg: 'panel.svg', yamlUrl: 'source.yaml', basic: true,
    extraDocs: '<section id="fixture-docs">Docs</section>',
  });
  assert.match(html, /program-card-actions/);
  assert.match(html, /About this card/);
  assert.match(html, /fixture-docs/);
  assert.doesNotMatch(html, /data-panel-views/);
  assert.doesNotMatch(html, /program-card-demo/);
  assert.doesNotMatch(html, /program-card-audio/);
});

test('demo section renders YouTube thumbnails and Instagram official embeds', () => {
  const youtube = renderCardArticle({
    card: card({ videos: [{ id: 'abc123', url: 'https://youtu.be/abc123', provider: 'youtube', aspect: 'landscape' }] }),
    panelImg: 'panel.svg', yamlUrl: 'source.yaml',
  });
  assert.match(youtube, /data-video-provider="youtube"/);
  assert.match(youtube, /data-video-id="abc123"/);
  assert.match(youtube, /img\.youtube\.com\/vi\/abc123\/hqdefault\.jpg/);

  const instagram = renderCardArticle({
    card: card({ videos: [{ id: 'DMKkotPsItQ', url: 'https://www.instagram.com/reel/DMKkotPsItQ/', provider: 'instagram', kind: 'reel', aspect: 'portrait' }] }),
    panelImg: 'panel.svg', yamlUrl: 'source.yaml',
  });
  assert.match(instagram, /program-card-demo--instagram/);
  assert.match(instagram, /class="instagram-media"/);
  assert.match(instagram, /data-instgrm-permalink="https:\/\/www\.instagram\.com\/reel\/DMKkotPsItQ\/"/);
  assert.match(instagram, /program-card-demo__text/);
  assert.match(instagram, /<strong>Demo video<\/strong>/);
  assert.doesNotMatch(instagram, /program-card-demo__placeholder/);
});

test('cards without generated or custom panels omit both panel regions', () => {
  const html = renderCardArticle({ card: card(), panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.doesNotMatch(html, /program-card-use-section/);
  assert.doesNotMatch(html, /program-card-panel-rail/);
  assert.doesNotMatch(html, /data-panel-views/);
  assert.doesNotMatch(html, />Panel<\/h2>/);
});

test('downloads and documentation use the right security and embedding attributes', () => {
  const html = renderCardArticle({ card: card({ uf2_downloads: [
    { name: 'Local', url: 'firmware.uf2', sha256: 'abc&123' },
    { name: 'Mirror', url: 'https://downloads.test/fw', external: true, host: 'downloads.test', flashable: true, sha256: 'a'.repeat(64) },
  ] }), panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.match(html, /href="firmware\.uf2" download data-uf2-url="firmware\.uf2" data-sha256="abc&amp;123"/);
  assert.match(html, /href="https:\/\/downloads\.test\/fw" target="_blank" rel="noopener noreferrer"/);
  assert.match(html, /data-uf2-url="https:\/\/downloads\.test\/fw" data-sha256="a{64}"/);

  const inline = renderReadmeAndDocs({ readmeHtml: '<p>README</p>', docs: [{ name: 'Guide & Notes.pdf', url: 'Guide?x=1&y=2' }] });
  assert.match(inline, /<object[^>]+data="Guide\?x=1&amp;y=2"/);
  assert.match(inline, /Download Guide &amp; Notes\.pdf/);
  const preview = renderReadmeAndDocs({ docs: [{ name: 'Guide.pdf', url: 'guide.pdf' }], inlinePdf: false });
  assert.match(preview, /target="_blank" rel="noopener noreferrer"/);
  assert.match(preview, /inline PDF preview appears/);
});

test('download action requires firmware, an external link, or authored UF2 metadata', () => {
  const absent = renderCardArticle({ card: card(), panelImg: 'panel.svg', yamlUrl: 'source.yaml' });
  assert.doesNotMatch(absent, /program-card-action--download/);

  const declared = renderCardArticle({
    card: card({ has_uf2_metadata: true }), panelImg: 'panel.svg', yamlUrl: 'source.yaml',
  });
  assert.match(declared, /program-card-action--download/);
  assert.match(declared, /href="https:\/\/example\.test\/source"/);
});

test('card details render configured creation and update dates', () => {
  const html = renderCardArticle({
    card: card({ metadata: { created: '2024-02-03', updated: '2025-06-07' } }),
    panelImg: 'panel.svg', yamlUrl: 'source.yaml',
  });
  assert.match(html, /<dt>Created<\/dt><dd>2024-02-03<\/dd>/);
  assert.match(html, /<dt>Updated<\/dt><dd>2025-06-07<\/dd>/);
});

test('card details hide inferred creation dates', () => {
  const html = renderCardArticle({
    card: card({ metadata: { created: '2024-02-03', created_inferred: true, updated: '2025-06-07' } }),
    panelImg: 'panel.svg', yamlUrl: 'source.yaml',
  });
  assert.doesNotMatch(html, /<dt>Created<\/dt>/);
  assert.doesNotMatch(html, /2024-02-03/);
  assert.match(html, /<dt>Updated<\/dt><dd>2025-06-07<\/dd>/);
});

test('discovery renderers escape searchable attributes and ignore absent shelf cards', () => {
  const testCard = card({
    title: 'A "quoted" <card>', slug: 'safe-slug',
    short_description: 'x'.repeat(220),
    metadata: { creator: 'A&B <maker>', created: '2025-01-01', updated: '2026-01-01' },
  });
  const tile = renderTile(testCard, { showCreator: true });
  assert.match(tile, /data-creator="A&amp;B &lt;maker&gt;"/);
  assert.match(tile, /data-date="2025-01-01"/);
  assert.match(tile, /data-name="a &quot;quoted&quot; &lt;card&gt;"/);
  assert.match(tile, /…/);
  assert.match(renderArchive([testCard]), /\.\.\/programs\/safe-slug\//);
  const shelf = renderShelf({ title: 'Shelf <One>', cards: ['missing', testCard.id] }, new Map([[testCard.id, testCard]]));
  assert.match(shelf, /Shelf &lt;One&gt;/);
  assert.equal((shelf.match(/program-card-tile__link/g) || []).length, 1);
});

test('catalogue sorting uses inferred creation dates', () => {
  const inferred = card({
    metadata: { created: '2026-07-29', created_inferred: true },
  });
  assert.match(renderTile(inferred), /data-date="2026-07-29"/);
  assert.match(renderArchive([inferred]), /data-date="2026-07-29"/);
});

test('featured blank card overlays its label artwork on the randomized card icon', () => {
  const blank = card({ id: '88_Blank', title: 'Blank', slug: '88-blank' });
  const tile = renderTile(blank, { showArtwork: true, root: '..' });
  assert.match(tile, /data-random-blank-card/);
  assert.match(tile, /fill="currentColor"/);
  assert.match(tile, /href="\.\.\/assets\/program_cards\/blank\.svg"/);
  assert.match(tile, /<svg x="-13\.4" y="11\.36"/);
  assert.match(tile, /viewBox="398\.58 362\.95 54\.38 29\.99"/);
});

test('panel artwork converts authored newlines to visual line breaks', () => {
  const html = renderPanelArtwork({ panel: { controls: { main: { label: 'Line one\nLine two' } } } }, 'panel.svg');
  assert.match(html, /Line one<br>Line two/);
});

test('layout uses relative external runtime assets and CSP hashes only remaining inline scripts', () => {
  const html = renderLayout({ title: 'Safe', content: '<p>Content</p>', relativeRoot: '../..' });
  const policy = html.match(/Content-Security-Policy" content="([^"]+)/)?.[1] || '';
  assert.match(html, /<script src="\.\.\/\.\.\/assets\/js\/site-menu\.js"><\/script>/);
  assert.match(html, /<script type="module" src="\.\.\/\.\.\/assets\/js\/program-cards\.js"><\/script>/);
  assert.match(html, /<script src="\.\.\/\.\.\/assets\/js\/catalogue-filters\.js"><\/script>/);
  assert.doesNotMatch(html, /<script(?![^>]*\bsrc=)[^>]*>[\s\S]*?<\/script>/i);
  assert.match(policy, /script-src 'self'/);
  assert.doesNotMatch(policy, /sha256-/);
  assert.doesNotMatch(policy.match(/script-src[^;]*/)?.[0] || '', /unsafe-inline/);
  assert.match(policy, /object-src 'self'/);

  const withInline = renderLayout({ title: 'Inline', content: '<script>window.example = true;</script>' });
  assert.match(withInline, /script-src 'self' 'sha256-/);
});

test('author preview permits AJV schema compilation without weakening published pages', () => {
  const preview = renderAuthorPage();
  const previewPolicy = preview.match(/Content-Security-Policy" content="([^"]+)/)?.[1] || '';
  const published = renderLayout({ title: 'Published', content: '' });
  const publishedPolicy = published.match(/Content-Security-Policy" content="([^"]+)/)?.[1] || '';
  assert.match(previewPolicy.match(/script-src[^;]*/)?.[0] || '', /unsafe-eval/);
  assert.match(previewPolicy, /worker-src 'self' blob:/);
  assert.doesNotMatch(publishedPolicy.match(/script-src[^;]*/)?.[0] || '', /unsafe-eval/);
  assert.doesNotMatch(publishedPolicy, /worker-src/);
});

test('advanced author editor includes highlighting, diagnostics, and YAML formatting controls', () => {
  const preview = renderAuthorPage();
  assert.match(preview, /id="format-yaml"/);
  assert.match(preview, /id="toggle-whitespace" type="checkbox"/);
  assert.match(preview, /id="yaml-monaco"/);
  assert.match(preview, /monaco-editor\.css/);
  assert.match(preview, /id="yaml-source" hidden/);
  assert.doesNotMatch(preview, /<script[^>]+src="\.\/monaco-editor\.js/);
  assert.match(preview, /YAML language and Workshop schema diagnostics update as you type/);
  assert.match(preview, /data-field="date-created" type="date"/);
  assert.match(preview, /data-field="date-updated" type="date"/);
});

test('basic author fields link to new-tab examples of their published usage', () => {
  const preview = renderAuthorPage();
  assert.match(preview, /Short description[\s\S]*class="author-field-guidance">\(used in card search and the all cards index; <a href="\.\.\/archive\/" target="_blank" rel="noopener noreferrer">see example<svg class="external-link-arrow"[\s\S]*?<\/svg><\/a>/);
  assert.match(preview, /Summary[\s\S]*used beneath the title on card pages; <a href="\.\.\/programs\/15-mlrws\/" target="_blank" rel="noopener noreferrer">see example<svg class="external-link-arrow"[\s\S]*?<\/svg><\/a>/);
});

test('basic author mode exposes live-preview web editor metadata', () => {
  const preview = renderAuthorPage();
  assert.match(preview, /data-add-optional="Editor"/);
  assert.match(preview, /data-field="Editor"/);
  assert.match(preview, /data-field="web-entry"/);
});