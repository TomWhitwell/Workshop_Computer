// Program-card detail page renderer.
//
// Port of the MTM program-card detail layout (TomWhitwell/MTM_Newsite_2022
// _layouts/program_card.html and the renderCardPage function in
// assets/program_cards/preview-tools.js), adapted to consume the canonical
// card model produced by src/model/card.js and to render inside the local
// sitegen layout.

import { marked } from 'marked';
import { panelPositions } from './panelPositions.js';

const DEFAULT_DISCUSSION = 'https://discord.com/channels/1210238368898879569/1484219323039092938';

function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function stripTags(value) {
  return String(value == null ? '' : value).replace(/<[^>]*>/g, '');
}

function inline(value) {
  // Panel labels may carry newlines for two-line panel rendering; collapse them
  // when the label is used in flowing text (controls, sockets, headings).
  return stripTags(value).replace(/\s*\n\s*/g, ' ').trim();
}

function truncate(value, length) {
  const text = stripTags(value).trim();
  if (text.length <= length) return text;
  return text.slice(0, Math.max(0, length - 1)).trimEnd() + '\u2026';
}

function markdownBlock(text) {
  const val = String(text ?? '').trim();
  if (!val) return '';
  return marked.parse(val);
}

function cardNumber(card) {
  const release = card.release || card.id || '';
  const raw = String(release).split('/')[0].split('_')[0].trim();
  const number = Number.parseInt(raw, 10);
  return Number.isNaN(number) ? raw : String(number);
}

function renderTags(card) {
  const tags = Array.isArray(card.tags) ? card.tags.filter(Boolean) : [];
  if (!tags.length) return '';
  return `<span class="program-card-tags">${tags
    .map(tag => `<span class="program-card-tag program-card-tag--${esc(String(tag).toLowerCase())}">${esc(tag)}</span>`)
    .join('')}</span>`;
}

// At the panel label's 11px type and narrow socket width, roughly 15 visible
// characters (counting normalized whitespace) fit across three word-wrapped
// lines. No unbroken token longer than eight characters is allowed to overrun
// its background, even when the complete label is otherwise short.
const PANEL_MIDWORD_BREAK_THRESHOLD = 15;
const PANEL_UNBROKEN_TOKEN_THRESHOLD = 8;

function panelWrapClass(value) {
  const text = stripTags(value || '').replace(/\s+/g, ' ').trim();
  const longestToken = text.split(/[\s\-/]+/).reduce((length, token) => Math.max(length, token.length), 0);
  return text.length > PANEL_MIDWORD_BREAK_THRESHOLD || longestToken > PANEL_UNBROKEN_TOKEN_THRESHOLD
    ? ' program-card-panel-text--long'
    : '';
}

function renderPanelLabel(kind, pos, item) {
  if (!item || !item.label) return '';
  const label = esc(stripTags(item.label)).replace(/\n/g, '<br>');
  return `<span class="program-card-panel__label program-card-panel__label--${kind}${panelWrapClass(item.label)}" style="left: ${esc(pos.left)}%; top: ${esc(pos.top)}%;">${label}</span>`;
}

function switchModeName(value) {
  const text = inline(value || '');
  if (!text) return '';
  return text.split(': ')[0].trim();
}

function renderPanelSwitchPositions(switchModes = {}, positionControl = null) {
  const selected = positionControl?.activeId || 'middle';
  const selectable = new Set((positionControl?.items || []).map(item => item.id));
  const positions = ['up', 'middle', 'down'].map(position => {
    const role = switchModeName(switchModes[position]);
    const selectAttrs = selectable.has(position)
      ? ` data-panel-position-button="${position}" aria-pressed="${position === selected}" title="Select ${position} switch position"`
      : ` aria-pressed="false" title="${role ? 'Edit' : 'Add'} ${position} switch position"`;
    return `<button type="button" class="program-card-panel-switch-position program-card-panel-switch-position--${position}${role ? ' has-value' : ''}${panelWrapClass(role)}" data-panel-switch-position="${position}" aria-label="${position} switch position${role ? `: ${esc(role)}` : ''}"${selectAttrs}>${role ? `<strong>${esc(role)}</strong>` : ''}</button>`;
  }).join('');
  return `<span class="program-card-panel-switch-positions">${positions}</span>`;
}

function renderPanel(panel, panelImg, positionControl = null, switchModes = {}) {
  const labels = [];
  for (const pos of panelPositions.controls) {
    if (pos.key !== 'z') labels.push(renderPanelLabel('control', pos, (panel.controls || {})[pos.key]));
  }
  for (const pos of panelPositions.inputs) labels.push(renderPanelLabel('input', pos, (panel.inputs || {})[pos.key]));
  for (const pos of panelPositions.outputs) labels.push(renderPanelLabel('output', pos, (panel.outputs || {})[pos.key]));
  return `<figure class="program-card-panel" aria-label="Workshop Computer panel"><img src="${esc(panelImg)}" alt="Workshop Computer panel">${labels.join('')}${renderPanelSwitchPositions(switchModes, positionControl)}</figure>`;
}

function renderSwitchSection(snapshot, positionControl = null) {
  const switchModes = snapshot.switch_modes || {};
  if (positionControl) {
    const rows = positionControl.items.map(item => {
      const value = switchModes[item.id];
      return `<div class="program-card-switch-position">
        <button type="button" class="program-card-position-button" data-panel-position-button="${esc(item.id)}" aria-controls="${esc(positionControl.groupId)}-${esc(item.id)}" aria-pressed="${item.id === positionControl.activeId}">${esc(item.name)}</button>
        ${value ? `<p>${esc(truncate(value, 240))}</p>` : ''}
      </div>`;
    }).join('');
    const tap = switchModes.tap
      ? `<div class="program-card-switch-position program-card-switch-action"><strong>Tap</strong><p>${esc(truncate(switchModes.tap, 240))}</p></div>`
      : '';
    if (!rows && !tap) return '';
    return `<div class="program-card-control program-card-control--switch">
      <strong class="program-card-component-key">Switch</strong>
      <div class="program-card-switch-positions" role="group" aria-label="Switch position">${rows}${tap}</div>
    </div>`;
  }

  const entries = Object.entries(switchModes).filter(entry => entry[1]);
  if (!entries.length) return '';
  const markup = entries.map(([key, value]) => {
    const label = key === 'tap' ? 'Tap (Down)' : key.charAt(0).toUpperCase() + key.slice(1);
    const className = key === 'tap' ? ' class="program-card-switch-action"' : '';
    return `<p${className}><strong>${esc(label)}</strong> ${esc(truncate(value, 240))}</p>`;
  }).join('');
  return `<div class="program-card-control program-card-control--switch">
    <strong class="program-card-component-key">Switch</strong>
    <div class="program-card-switch-positions">${markup}</div>
  </div>`;
}

function renderPanelReference(snapshot, panelImg, positionControl = null) {
  const panel = snapshot.panel || {};
  const controls = panel.controls || {};
  const controlsMarkup = panelPositions.controls.filter(pos => pos.key !== 'z').map(pos => {
    const control = controls[pos.key];
    if (!control || (!control.label && !control.description)) return '';
    return `<div class="program-card-control program-card-control--${esc(pos.key)}">
      <strong class="program-card-component-key">${esc(pos.name)}</strong>
      ${control.label || control.description ? `<p>${control.label ? `<span class="program-card-component-role">${esc(inline(control.label))}</span>` : ''}${control.label && control.description ? '<br>' : ''}${control.description ? esc(truncate(control.description, 220)) : ''}</p>` : ''}
    </div>`;
  }).join('');
  const switchMarkup = renderSwitchSection(snapshot, positionControl);
  const inputsMarkup = renderSocketList('Inputs', panel.inputs, panelPositions.inputs);
  const outputsMarkup = renderSocketList('Outputs', panel.outputs, panelPositions.outputs);
  const ledsMarkup = renderLedList(snapshot.leds);

  return `<div class="program-card-use">
    <div class="program-card-use__panel">${renderPanel(panel, panelImg, positionControl, snapshot.switch_modes)}</div>
    <div class="program-card-use__reference">
      ${controlsMarkup || switchMarkup ? `<details class="program-card-section program-card-collapsible program-card-controls-section" open><summary><h3>Controls</h3></summary><div class="program-card-control-list">${controlsMarkup}${switchMarkup}</div></details>` : ''}
      ${inputsMarkup || outputsMarkup ? `<details class="program-card-section program-card-collapsible program-card-io-section" open><summary class="program-card-io-summary"><span class="program-card-io-headings">${inputsMarkup ? '<h3>Inputs</h3>' : '<span></span>'}${outputsMarkup ? '<h3>Outputs</h3>' : ''}</span></summary><div class="program-card-io-columns">${inputsMarkup}${outputsMarkup}</div></details>` : ''}
      ${ledsMarkup}
    </div>
  </div>`;
}

function renderPanelViews(card, panelImg) {
  const items = Array.isArray(card.panel_views?.items) ? card.panel_views.items : [];
  if (!items.length) {
    return renderPanelReference({ panel: card.panel || {}, switch_modes: card.switch_modes, leds: card.leds }, panelImg);
  }

  const selected = items.find(item => item.id === card.panel_views.default) || items[0];
  const groupId = `panel-positions-${card.slug || card.id || 'card'}`;
  const views = items.map(item => `<div id="${esc(groupId)}-${esc(item.id)}" class="program-card-position-view" data-panel-position-view="${esc(item.id)}"${item.id === selected.id ? '' : ' hidden aria-hidden="true"'}>
    ${renderPanelReference(item, panelImg, { items, groupId, activeId: item.id })}
  </div>`).join('');
  return `<div class="program-card-panel-views">${views}</div>`;
}

function renderPanelRail(card, panelImg) {
  const items = Array.isArray(card.panel_views?.items) ? card.panel_views.items : [];
  if (!items.length) {
    return `<aside class="program-card-panel-rail" aria-label="Panel visualization">${renderPanel(card.panel || {}, panelImg, null, card.switch_modes)}</aside>`;
  }

  const selected = items.find(item => item.id === card.panel_views.default) || items[0];
  const groupId = `panel-positions-${card.slug || card.id || 'card'}`;
  const panels = items.map(item => `<div class="program-card-rail-view" data-panel-position-panel="${esc(item.id)}"${item.id === selected.id ? '' : ' hidden aria-hidden="true"'}>${renderPanel(item.panel || {}, panelImg, { items, groupId, activeId: item.id }, item.switch_modes)}</div>`).join('');
  return `<aside class="program-card-panel-rail" aria-label="Panel visualization">${panels}</aside>`;
}

function renderSocketList(title, sockets, positions) {
  if (!sockets) return '';
  const items = (positions || []).map(pos => {
    const socket = sockets[pos.key];
    if (!socket || (!socket.description && !socket.label)) return '';
    return `<div class="program-card-socket">
      <strong class="program-card-component-key">${esc(pos.name || pos.key)}</strong>
      ${socket.label || socket.description ? `<p>${socket.label ? `<span class="program-card-component-role">${esc(inline(socket.label))}</span>` : ''}${socket.label && socket.description ? '<br>' : ''}${socket.description ? esc(truncate(socket.description, 220)) : ''}</p>` : ''}
    </div>`;
  }).join('');
  if (!items.trim()) return '';
  return `<section class="program-card-socket-section program-card-socket-section--${esc(title.toLowerCase())}"><div class="program-card-socket-list">${items}</div></section>`;
}

function renderLedList(leds) {
  if (!Array.isArray(leds) || !leds.length) return '';
  const items = leds.map(led => {
    const item = typeof led === 'string' ? { label: led } : led;
    if (!item || (!item.id && !item.label && !item.description)) return '';
    const match = String(item.id || '').match(/^LED(\d+)$/i);
    const key = match ? `LED ${Number(match[1]) + 1}` : (item.id || 'LED note');
    return `<div class="program-card-led">
      <strong class="program-card-component-key">${esc(key)}</strong>
      ${item.label || item.description ? `<p>${item.label ? `<span class="program-card-component-role">${esc(inline(item.label))}</span>` : ''}${item.label && item.description ? '<br>' : ''}${item.description ? esc(truncate(item.description, 220)) : ''}</p>` : ''}
    </div>`;
  }).join('');
  if (!items.trim()) return '';
  return `<details class="program-card-section program-card-collapsible program-card-led-section" open><summary><h3>LEDs</h3></summary><div class="program-card-led-list">${items}</div></details>`;
}

function renderDocumentation(card, extraSections = '', includeStructured = true) {
  const documentation = card.documentation || {};
  const blocks = [];
  if (String(documentation.intro || '').trim()) {
    blocks.push(`<details class="program-card-section program-card-collapsible" id="card-readme" open><summary><h3>README</h3></summary><div class="markdown-body">${markdownBlock(documentation.intro)}</div></details>`);
  }
  if (includeStructured) {
    for (const section of (Array.isArray(documentation.sections) ? documentation.sections : [])) {
      const title = inline(section?.title || '');
      const body = String(section?.body || '').trim();
      if (!body) continue;
      const heading = title ? title.replace(/\b\w/g, c => c.toUpperCase()) : 'Documentation';
      blocks.push(`<details class="program-card-section program-card-collapsible" open><summary><h3>${esc(heading)}</h3></summary><div>${markdownBlock(body)}</div></details>`);
    }
  }
  const joined = blocks.join('') + (extraSections || '');
  if (!joined.trim()) return '';
  return `<section class="program-card-documentation" id="card-documentation">${joined}</section>`;
}

/**
 * Build the "extra" documentation HTML appended into the Documentation section:
 * the rendered README followed by any PDF docs. Shared by the site build and the
 * author preview so both show the same thing. `docs[].url` is whatever URL the
 * caller resolved (relative for the built detail page, raw GitHub for preview).
 * `inlinePdf` embeds an inline `<object>` preview of the first PDF; disable it
 * for the preview, where raw-GitHub PDF URLs would trigger a download.
 */
export function renderReadmeAndDocs({ readmeHtml = '', docs = [], inlinePdf = true, includeReadme = true } = {}) {
  const readmeSection = includeReadme && readmeHtml
    ? `<details class="program-card-section program-card-collapsible" id="card-readme" open><summary><h3>README</h3></summary><div class="markdown-body">${readmeHtml}</div></details>`
    : '';
  const list = Array.isArray(docs) ? docs : [];
  let pdfSection = '';
  if (list.length && inlinePdf) {
    pdfSection = `
    <details class="program-card-section program-card-collapsible docs-section" open>
      <summary><h3>Documentation PDF</h3></summary>
      <object data="${esc(list[0].url)}" type="application/pdf" width="100%" height="700px">
        <p>PDF preview not available.</p>
      </object>
      <div style="margin-top:16px;text-align:center">
        <a class="btn download" href="${esc(list[0].url)}" download>Download ${esc(list[0].name)}</a>
      </div>
      ${list.length > 1 ? `<ul class="docs-list">${list.slice(1).map(d => `<li><a class="btn download" href="${esc(d.url)}" download>${esc(d.name)}</a></li>`).join('')}</ul>` : ''}
    </details>`;
  } else if (list.length) {
    pdfSection = `
    <details class="program-card-section program-card-collapsible docs-section" open>
      <summary><h3>Documentation PDF</h3></summary>
      <ul class="docs-list">${list.map(d => `<li><a class="btn download" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer">${esc(d.name)}</a></li>`).join('')}</ul>
      <p class="preview-note">An inline PDF preview appears on the published card page.</p>
    </details>`;
  }
  return readmeSection + pdfSection;
}

/** Render the Audio section from classified audio-sample items. */
function renderAudio(samples) {
  if (!Array.isArray(samples) || !samples.length) return '';
  const items = samples.map(s => {
    const title = s.title ? `<p class="program-card-audio__title">${esc(s.title)}</p>` : '';
    if (s.kind === 'file') {
      return `<div class="program-card-audio__item">${title}<audio class="program-card-audio__player" controls preload="none" src="${esc(s.url)}"></audio></div>`;
    }
    if ((s.kind === 'soundcloud' || s.kind === 'bandcamp') && s.embedUrl) {
      const height = s.height || (s.kind === 'soundcloud' ? 166 : 120);
      return `<div class="program-card-audio__item">${title}<iframe class="program-card-audio__embed program-card-audio__embed--${esc(s.kind)}" src="${esc(s.embedUrl)}" width="100%" height="${height}" loading="lazy" frameborder="0" allow="autoplay" title="${esc(s.title || (s.kind + ' player'))}"></iframe></div>`;
    }
    return `<div class="program-card-audio__item">${title}<a class="program-card-audio__link" href="${esc(s.url)}" target="_blank" rel="noopener noreferrer">${esc(s.host || s.url)} ↗</a></div>`;
  }).join('');
  return `<section class="program-card-audio"><h2>Audio samples</h2>${items}</section>`;
}

/**
 * Render the full program-card detail article from a canonical card model.
 *
 * @param {object} opts
 * @param {object} opts.card         canonical card model
 * @param {string} opts.panelImg     href to the panel SVG (relative to page)
 * @param {string} opts.yamlUrl      GitHub URL to the source info.yaml
 * @param {string} [opts.uf2Url]     direct .uf2 download URL (enables WebUSB)
 * @param {string} [opts.extraDocs]  extra HTML appended into the documentation area (README/PDFs)
 * @param {boolean} [opts.basic]    draft mode: render only basic fields + README/PDFs (skip the generated model sections)
 */
export function renderCardArticle({ card, panelImg, yamlUrl, uf2Url, extraDocs = '', basic = false }) {
  const metadata = card.metadata || {};
  const summary = card.summary || '';
  const sourceUrl = card.source_url || '';
  const readmeUrl = card.readme_url || '';
  const documentation = renderDocumentation(card, extraDocs, !basic);
  const panelRail = basic ? '' : renderPanelRail(card, panelImg);
  const discussionUrl = metadata.discussion_url || DEFAULT_DISCUSSION;
  const firstVideo = Array.isArray(card.videos) && card.videos[0];

  const dataSources = !basic && Array.isArray(card.source) && card.source.length
    ? `<div class="program-card-data-sources"><details><summary>Data sources</summary><p>${card.source.map(item => `<code title="${esc(item)}">${esc(truncate(item, 56))}</code>`).join(', ')}</p></details></div>`
    : '';

  const draftBar = card.draft
    ? `<aside class="program-card-draft-bar" aria-label="Draft documentation notice"><div class="program-card-draft-bar__message"><strong>Draft:</strong> <span>This documentation is work in progress and has not yet been approved by the card designer.</span></div><details class="program-card-draft-bar__details"><summary>I'm the designer. How should I fix this?</summary><p>Check the generated documentation against the card, then edit <a href="${esc(yamlUrl)}">the relevant <code>info.yaml</code> file</a> in the Workshop Computer repo. When everything is accurate, set <code>draft: false</code> in that YAML and submit the change.</p></details></aside>`
    : '';

  const uf2Downloads = Array.isArray(card.uf2_downloads) ? card.uf2_downloads : [];
  const downloadActions = uf2Downloads.length
    ? uf2Downloads.map(d => {
        // An external (mirror/store) link opens in a new tab, shows its host and
        // an External tag, and is never flashed or given a SHA readout.
        if (d.external) {
          const host = d.host ? `<small class="program-card-action__host">${esc(d.host)}</small>` : '';
          const tag = '<small class="program-card-action__tag">External \u2197</small>';
          return `<a class="program-card-action program-card-action--download program-card-action--external" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer"><span>Download</span><small>${esc(d.name)}</small>${host}${tag}</a>`;
        }
        // A repo file downloads directly, enables WebUSB, and exposes its SHA256.
        const hashAttr = d.sha256 ? ` data-sha256="${esc(d.sha256)}"` : '';
        return `<a class="program-card-action program-card-action--download" href="${esc(d.url)}" download data-uf2-url="${esc(d.url)}"${hashAttr}><span>Download</span><small>${esc(d.name)}</small></a>`;
      }).join('')
    : (() => {
        const downloadHref = uf2Url || sourceUrl;
        const downloadAttrs = uf2Url ? ` download data-uf2-url="${esc(uf2Url)}"` : '';
        return `<a class="program-card-action program-card-action--download" href="${esc(downloadHref)}"${downloadAttrs}><span>Download</span>${metadata.version ? `<small>Firmware ${esc(metadata.version)}</small>` : ''}</a>`;
      })();
  const editorAction = metadata.editor_url
    ? `<a class="program-card-action program-card-action--editor" href="${esc(metadata.editor_url)}"><span>Launch web editor</span><small>${esc(metadata.editor_note || 'Configure this card in your browser')}</small></a>`
    : '';

  const memoryMarkup = card.memory && card.memory.size
    ? `<span>${esc(String(card.memory.size).toUpperCase())} card ${esc(card.memory.requirement || 'supported')}</span>`
    : '';

  const hero = `<header class="program-card-hero">
    <div class="program-card-hero__main">
      ${basic ? '' : renderTags(card)}
      <h1><span class="program-card-page__number">${esc(cardNumber(card))}</span> ${esc(inline(card.title || card.id || 'Untitled card'))}</h1>
      ${summary ? `<p>${esc(summary)}</p>` : ''}
      <div class="program-card-hero__meta">${metadata.creator ? `<span>By ${esc(metadata.creator)}</span>` : ''}${basic ? '' : memoryMarkup}</div>
      <div class="program-card-actions" aria-label="Card actions">${downloadActions}${editorAction}</div>
      <div class="program-card-sha" data-sha-display role="status" aria-live="polite" hidden>SHA256: <code class="program-card-sha__value" data-sha-value></code> <button type="button" class="program-card-sha__verify" data-verify-open>How to verify</button></div>
      <div class="program-card-hero__links" aria-label="Further card links">${documentation ? `<a href="#card-documentation">Read more</a>` : ''}<a href="${esc(discussionUrl)}">Support &amp; questions</a><button id="connectToggle" class="connect-toggle" type="button" role="switch" aria-checked="false" aria-label="Connect to RP2040 via WebUSB" title="Reboot computer into programming mode before connecting"><span class="c-status" aria-hidden="true"></span><span class="c-label">Connect workshop computer</span></button></div>
    </div>
  </header>`;

  const demo = !basic && firstVideo
    ? `<section class="program-card-demo"><a href="${esc(firstVideo.url)}" data-youtube-id="${esc(firstVideo.id)}"><span class="program-card-demo__media" aria-hidden="true"><img src="https://img.youtube.com/vi/${esc(firstVideo.id)}/hqdefault.jpg" alt="" loading="lazy"></span><span class="program-card-demo__text"><span>Watch</span><strong>${esc(firstVideo.title || 'Demo video')}</strong></span></a></section>`
    : '';

  const audio = basic ? '' : renderAudio(card.audio_samples);

  const quickStart = !basic && Array.isArray(card.quick_start) && card.quick_start.length
    ? `<section class="program-card-quick-start"><h2>Quick start</h2><ol>${card.quick_start.map(step => `<li>${esc(stripTags(step))}</li>`).join('')}</ol></section>`
    : '';

  const use = basic ? '' : `<section class="program-card-use-section">
    <h2 class="program-card-use__title">Panel</h2>
    ${renderPanelViews(card, panelImg)}
  </section>`;

  const notesMarkup = !basic && Array.isArray(card.notes) && card.notes.length
    ? `<details class="program-card-section program-card-collapsible" open><summary><h3>Notes</h3></summary><div>${card.notes.map(note => `<p>${esc(truncate(note, 220))}</p>`).join('')}</div></details>`
    : '';

  const about = `<footer class="program-card-about">
    <details class="program-card-section program-card-collapsible" open><summary><h3>About this card</h3></summary><dl>
      ${metadata.creator ? `<div><dt>Creator</dt><dd>${esc(metadata.creator)}</dd></div>` : ''}
      ${metadata.language ? `<div><dt>Language</dt><dd>${esc(metadata.language)}</dd></div>` : ''}
      ${metadata.version ? `<div><dt>Version</dt><dd>${esc(metadata.version)}</dd></div>` : ''}
      ${metadata.status ? `<div><dt>Status</dt><dd>${esc(metadata.status)}</dd></div>` : ''}
      ${metadata.license ? `<div><dt>License</dt><dd>${esc(metadata.license)}</dd></div>` : ''}
      ${metadata.updated && metadata.updated !== 'n/a' ? `<div><dt>Updated</dt><dd>${esc(metadata.updated)}</dd></div>` : ''}
      ${metadata.created && metadata.created !== 'n/a' ? `<div><dt>Created</dt><dd>${esc(metadata.created)}</dd></div>` : ''}
      ${card.memory && card.memory.size ? `<div><dt>Card memory</dt><dd>${esc(String(card.memory.size).toUpperCase())} ${esc(card.memory.requirement || 'supported')}</dd></div>` : ''}
      ${readmeUrl ? `<div><dt>Read more</dt><dd><a href="${esc(readmeUrl)}">README in the Workshop Computer repo</a></dd></div>` : ''}
      ${sourceUrl ? `<div><dt>Source</dt><dd><a href="${esc(sourceUrl)}">Release folder on GitHub</a></dd></div>` : ''}
      <div><dt>Support</dt><dd><a href="${esc(discussionUrl)}">Ask questions, contact the designer, or share feedback</a></dd></div>
    </dl></details>
    ${notesMarkup}
    ${dataSources}
  </footer>`;

  const verifyModal = `<dialog class="verify-modal" data-verify-modal aria-label="How to verify your download">
    <div class="verify-modal__body">
      <h2>Verify your download</h2>
      <p>Confirm the file you downloaded really is that new firmware.</p>
      <h3>macOS / Linux (Terminal)</h3>
      <pre><code>shasum -a 256 firmware.uf2</code></pre>
      <p class="verify-modal__note">Linux also has <code>sha256sum firmware.uf2</code>.</p>
      <h3>Windows (PowerShell)</h3>
      <pre><code>Get-FileHash firmware.uf2 -Algorithm SHA256</code></pre>
      <p>Compare the result to the SHA256 on the website — it should match exactly.</p>
      <button type="button" class="btn verify-modal__close" data-verify-close>Close</button>
    </div>
  </dialog>`;

  return `<article class="program-cards program-card-page"${basic ? '' : ' data-panel-views'}>
    ${draftBar}
    ${panelRail}
    ${hero}
    ${demo}
    ${audio}
    ${quickStart}
    ${use}
    ${documentation}
    ${about}
    ${verifyModal}
  </article>`;
}
