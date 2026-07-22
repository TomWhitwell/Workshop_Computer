import YAML from 'yaml';
import { parseSource } from './lib/validate/parseSource.js';
import { validateInfoYaml } from './lib/validate/validateInfoYaml.js';
import { buildCanonicalCardModel } from './lib/model/card.js';
import { renderCardArticle } from './lib/render/cardPage.js';
import { renderReadmeAndDocs } from './lib/render/cardPage.js';
import { panelPositions } from './lib/render/panelPositions.js';
import { resolveAudioSamples, getAudioField } from './lib/utils/audio.js';

const REQUIRED = ['Name', 'short-description', 'summary', 'Language', 'Creator', 'Version', 'Status'];
const STORAGE_KEY = 'workshop-computer-author-new';
const DIFFERENTIAL_STORAGE_KEY = 'workshop-computer-author-differential-controls';
const SWITCH_POSITIONS = ['up', 'middle', 'down'];
const OPTIONAL_KEYS = ['tags', 'readme', 'demo-link', 'contact'];
const SPLIT_STORAGE_KEY = 'workshop-computer-author-editor-width';
const DOCUMENT_KIND = document.querySelector('.author-page')?.dataset.documentKind || 'new';
const IS_EXISTING = DOCUMENT_KIND === 'existing';
const INITIAL = {
  draft: false,
  Name: '',
  'short-description': '',
  summary: '',
  Language: '',
  Creator: '',
  Version: '',
  Status: '',
};

const inputIds = {
  audio_l: 'AudioIn1', audio_r: 'AudioIn2', cv_1: 'CVIn1', cv_2: 'CVIn2',
  pulse_1: 'PulseIn1', pulse_2: 'PulseIn2',
};
const outputIds = {
  audio_out_l: 'AudioOut1', audio_out_r: 'AudioOut2', cv_out_1: 'CVOut1', cv_out_2: 'CVOut2',
  pulse_out_1: 'PulseOut1', pulse_out_2: 'PulseOut2',
};
const componentDefs = [
  ...panelPositions.controls.map(p => ({ ...p, kind: p.key === 'z' ? 'switch' : 'control', label: p.name })),
  ...panelPositions.inputs.map(p => ({ ...p, kind: 'input', id: inputIds[p.key], label: `Input · ${p.name}` })),
  ...panelPositions.outputs.map(p => ({ ...p, kind: 'output', id: outputIds[p.key], label: `Output · ${p.name}` })),
];

const els = {
  editor: document.getElementById('author-editor'),
  yamlPanel: document.getElementById('yaml-editor'),
  yaml: document.getElementById('yaml-source'),
  sourcePathTitle: document.getElementById('source-path-title'),
  diagnostics: document.getElementById('diagnostics'),
  preview: document.getElementById('card-preview'),
  progress: document.getElementById('required-progress'),
  status: document.getElementById('author-status'),
  download: document.getElementById('download-source'),
  panelDownload: document.getElementById('download-panel-image'),
  startFresh: document.getElementById('start-fresh'),
  licenseDialog: document.getElementById('license-dialog'),
  licenseValue: document.getElementById('license-value'),
  licenseHelp: document.getElementById('license-help'),
  licenseResult: document.getElementById('license-result'),
  useLicense: document.getElementById('use-license'),
  select: document.getElementById('card-select'),
  editorStatus: document.getElementById('editor-status'),
  page: document.querySelector('.author-page'),
  gutter: document.getElementById('gutter-inner'),
  productionLink: document.getElementById('production-card-link'),
  workspace: document.querySelector('.author-workspace'),
  splitter: document.getElementById('author-splitter'),
  yamlFullscreen: document.getElementById('yaml-fullscreen'),
  previewColumn: document.querySelector('.author-preview-column'),
  previewFullscreen: document.getElementById('preview-fullscreen'),
};

let data = IS_EXISTING ? clone(INITIAL) : loadDraft();
let index = [];
let currentEntry = null;
let selectedLicense = '';
let licenseWasManuallySelected = false;
let differentControls = localStorage.getItem(DIFFERENTIAL_STORAGE_KEY) === 'true';
let activePosition = 'middle';
let currentMode = IS_EXISTING ? 'yaml' : 'author';
const openOptionals = new Set(OPTIONAL_KEYS.filter(key => hasOptionalValue(key)));
let yamlTimer;
let positionClickTimer;
let yamlTargetCacheSource = '';
let yamlTargetCacheDocument = null;

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function loadDraft() {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved) {
      const parsed = YAML.parse(saved) || {};
      const legacyDescription = cleanText(parsed.Description);
      if (!cleanText(parsed['short-description']) && legacyDescription) parsed['short-description'] = legacyDescription;
      if (!cleanText(parsed.summary) && legacyDescription) parsed.summary = legacyDescription;
      delete parsed.Description;
      return { ...clone(INITIAL), ...parsed };
    }
  } catch {}
  return clone(INITIAL);
}

function saveDraft() {
  if (IS_EXISTING) return;
  try { localStorage.setItem(STORAGE_KEY, sourceText()); } catch {}
}

function sourceText() {
  return YAML.stringify(data, { lineWidth: 0 });
}

function cleanText(value) {
  return String(value ?? '').trim();
}

function isObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function basicCompatibility(source, parsed) {
  const reasons = [];
  const top = new Set(['draft', 'Name', 'short-description', 'summary', 'Language', 'Creator', 'Version', 'Status', 'License', 'tags', 'readme', 'demo-link', 'contact', 'panel', 'controls']);
  const textFields = ['Name', 'short-description', 'summary', 'Language', 'Creator', 'Version', 'Status', 'License', 'readme', 'demo-link'];
  const positions = new Set(SWITCH_POSITIONS);
  const inputs = new Set(Object.values(inputIds));
  const outputs = new Set(Object.values(outputIds));
  const keysOnly = (value, allowed) => isObject(value) && Object.keys(value).every(key => allowed.has(key));
  const conditionOk = value => value == null || (keysOnly(value, new Set(['z'])) && positions.has(value.z));
  const partOk = value => keysOnly(value, new Set(['name', 'description'])) && Object.values(value).every(item => typeof item === 'string');
  if (!isObject(parsed)) return { compatible: false, reasons: ['The YAML root must be a mapping.'] };
  if (/^\s*#|\s+#|(^|\s)[&*!][\w-]+|^\s*<<\s*:/m.test(source)) reasons.push('YAML comments, anchors, aliases, tags, or merge keys cannot be preserved.');
  for (const key of Object.keys(parsed)) if (!top.has(key)) reasons.push(`Unsupported field: ${key}`);
  for (const key of textFields) if (parsed[key] != null && typeof parsed[key] !== 'string') reasons.push(`${key} must be text.`);
  if (parsed.draft != null && typeof parsed.draft !== 'boolean') reasons.push('draft must be true or false.');
  if (parsed.tags != null && (!Array.isArray(parsed.tags) || parsed.tags.some(item => typeof item !== 'string'))) reasons.push('tags must be a list of text values.');
  if (parsed.contact != null && (!keysOnly(parsed.contact, new Set(['website'])) || (parsed.contact.website != null && typeof parsed.contact.website !== 'string'))) reasons.push('contact contains fields Basic mode cannot edit.');
  const controls = parsed.controls;
  if (controls != null) {
    if (!keysOnly(controls, new Set(['knobs', 'switch']))) reasons.push('controls contains unsupported sections.');
    if (controls?.switch != null) {
      if (!keysOnly(controls.switch, new Set(['up', 'middle', 'down', 'tap']))) reasons.push('controls.switch contains unsupported positions.');
      else for (const [position, value] of Object.entries(controls.switch)) if (!partOk(value)) reasons.push(`controls.switch.${position} has unsupported data.`);
    }
    if (controls?.knobs != null) {
      if (!Array.isArray(controls.knobs)) reasons.push('controls.knobs must be a list.');
      else {
        const seen = new Set();
        for (const row of controls.knobs) {
          if (!keysOnly(row, new Set(['when', 'main', 'x', 'y'])) || !conditionOk(row.when)) reasons.push('A knob row has unsupported fields or conditions.');
          for (const key of ['main', 'x', 'y']) if (row?.[key] != null && !partOk(row[key])) reasons.push(`A ${key} knob has unsupported data.`);
          const context = row?.when?.z || 'shared';
          if (seen.has(context)) reasons.push(`Multiple knob rows use the ${context} context.`);
          seen.add(context);
        }
      }
    }
  }
  const panel = parsed.panel;
  if (panel != null) {
    if (!keysOnly(panel, new Set(['inputs', 'outputs']))) reasons.push('panel contains unsupported sections.');
    for (const [side, ids] of [['inputs', inputs], ['outputs', outputs]]) {
      if (panel?.[side] == null) continue;
      if (!Array.isArray(panel[side])) { reasons.push(`panel.${side} must be a list.`); continue; }
      const seen = new Set();
      for (const item of panel[side]) {
        if (!keysOnly(item, new Set(['id', 'name', 'description', 'when'])) || !ids.has(item?.id) || !conditionOk(item?.when)
          || (item.name != null && typeof item.name !== 'string') || (item.description != null && typeof item.description !== 'string')) reasons.push(`panel.${side} contains an unsupported jack.`);
        const identity = `${item?.id}:${item?.when?.z || 'shared'}`;
        if (seen.has(identity)) reasons.push(`panel.${side} repeats ${identity}.`);
        seen.add(identity);
      }
    }
  }
  return { compatible: reasons.length === 0, reasons: [...new Set(reasons)] };
}

function updateBasicAvailability(source = els.yaml.value, parsed = data) {
  const button = document.querySelector('[data-mode="author"]');
  if (!IS_EXISTING || !button) return;
  const result = basicCompatibility(source, parsed);
  button.disabled = !result.compatible;
  button.title = result.compatible ? 'Edit this card visually' : `Basic mode unavailable: ${result.reasons.join(' ')}`;
  button.setAttribute('aria-description', button.title);
  if (!result.compatible && currentMode === 'author') setMode('yaml');
}

function updateGutter() {
  if (!els.gutter) return;
  els.gutter.textContent = Array.from({ length: els.yaml.value.split('\n').length }, (_, index) => index + 1).join('\n');
  els.gutter.style.transform = `translateY(${-els.yaml.scrollTop}px)`;
}

function splitBounds() {
  const width = els.workspace.getBoundingClientRect().width || Math.max(780, window.innerWidth - 36);
  return { min: 320, max: Math.max(320, width - 460) };
}

function setEditorWidth(width, { persist = true } = {}) {
  const { min, max } = splitBounds();
  const next = Math.round(Math.min(max, Math.max(min, Number(width) || 560)));
  els.workspace.style.setProperty('--author-editor-width', `${next}px`);
  els.splitter.setAttribute('aria-valuemin', String(min));
  els.splitter.setAttribute('aria-valuemax', String(max));
  els.splitter.setAttribute('aria-valuenow', String(next));
  if (persist) localStorage.setItem(SPLIT_STORAGE_KEY, String(next));
}

function initWorkspaceSplitter() {
  setEditorWidth(localStorage.getItem(SPLIT_STORAGE_KEY) || 560, { persist: false });
  let dragging = false;
  const finish = () => {
    if (!dragging) return;
    dragging = false;
    els.workspace.classList.remove('is-resizing');
    localStorage.setItem(SPLIT_STORAGE_KEY, els.splitter.getAttribute('aria-valuenow'));
  };
  els.splitter.addEventListener('pointerdown', event => {
    if (matchMedia('(max-width: 1050px)').matches) return;
    dragging = true;
    els.splitter.setPointerCapture(event.pointerId);
    els.workspace.classList.add('is-resizing');
    event.preventDefault();
  });
  els.splitter.addEventListener('pointermove', event => {
    if (!dragging) return;
    const left = els.workspace.getBoundingClientRect().left;
    setEditorWidth(event.clientX - left, { persist: false });
  });
  els.splitter.addEventListener('pointerup', finish);
  els.splitter.addEventListener('pointercancel', finish);
  els.splitter.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const { min, max } = splitBounds();
    const current = Number(els.splitter.getAttribute('aria-valuenow')) || 560;
    if (event.key === 'Home') setEditorWidth(min);
    else if (event.key === 'End') setEditorWidth(max);
    else setEditorWidth(current + (event.key === 'ArrowLeft' ? -24 : 24));
  });
  window.addEventListener('resize', () => setEditorWidth(els.splitter.getAttribute('aria-valuenow'), { persist: false }));
}

async function toggleYamlFullscreen() {
  if (document.fullscreenElement === els.yamlPanel) await document.exitFullscreen();
  else await els.yamlPanel.requestFullscreen();
}

async function togglePreviewFullscreen() {
  if (document.fullscreenElement === els.previewColumn) await document.exitFullscreen();
  else await els.previewColumn.requestFullscreen();
}

function updateFullscreenButtons() {
  const yamlActive = document.fullscreenElement === els.yamlPanel;
  els.yamlFullscreen.setAttribute('aria-pressed', String(yamlActive));
  els.yamlFullscreen.setAttribute('aria-label', yamlActive ? 'Exit full screen' : 'Enter full screen');
  els.yamlFullscreen.title = yamlActive ? 'Exit full screen' : 'Enter full screen';
  const previewActive = document.fullscreenElement === els.previewColumn;
  els.previewFullscreen.setAttribute('aria-pressed', String(previewActive));
  els.previewFullscreen.setAttribute('aria-label', previewActive ? 'Exit preview full screen' : 'Enter preview full screen');
  els.previewFullscreen.title = previewActive ? 'Exit preview full screen' : 'Enter preview full screen';
  if (yamlActive) els.yaml.focus();
}

function getNested(path) {
  return path.split('.').reduce((value, key) => value && value[key], data);
}

function setNested(path, value) {
  const keys = path.split('.');
  let target = data;
  for (const key of keys.slice(0, -1)) target = target[key] ||= {};
  const last = keys.at(-1);
  if (cleanText(value)) target[last] = value;
  else delete target[last];
  prune(data);
}

function prune(value) {
  if (!value || typeof value !== 'object') return;
  for (const key of Object.keys(value)) {
    const child = value[key];
    if (child && typeof child === 'object') prune(child);
    if (Array.isArray(child) && child.length === 0) delete value[key];
    else if (child && typeof child === 'object' && !Array.isArray(child) && Object.keys(child).length === 0) delete value[key];
  }
}

function renderDiagnostics(result) {
  if (!result.diagnostics.length) {
    els.diagnostics.innerHTML = '<p class="diag-clean">✓ No schema issues.</p>';
    return;
  }
  els.diagnostics.innerHTML = `<p class="diag-summary">${result.errorCount} error(s), ${result.warningCount} warning(s)</p><ul class="diag-list">${result.diagnostics.map(d => `<li class="diag--${escapeHtml(d.severity)}"><strong>${d.severity === 'error' ? 'Error' : 'Warning'}:</strong> ${escapeHtml(d.path || '')} ${escapeHtml(d.message)}</li>`).join('')}</ul>`;
}

function escapeHtml(value) {
  return String(value ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function previewData() {
  if (!differentControls) return data;
  const preview = clone(data);
  const knobs = ((preview.controls ||= {}).knobs ||= []);
  for (const position of SWITCH_POSITIONS) {
    if (!knobs.some(row => row?.when?.z === position)) knobs.push({ when: { z: position } });
  }
  return preview;
}

function buildCard() {
  const base = rawBaseForCurrent();
  const audioSamples = resolveAudioSamples(getAudioField(data), rel => base ? base + rel.split('/').filter(Boolean).map(encodeURIComponent).join('/') : '');
  return buildCanonicalCardModel({
    folderName: currentEntry?.id || 'new_card', slug: currentEntry?.slug || 'new-card', info: {}, rawYaml: previewData(),
    docs: [], downloads: [], latestUf2: currentEntry?.uf2Url ? { url: currentEntry.uf2Url } : null, uf2Downloads: currentEntry?.uf2Downloads || [], web: {}, audioSamples,
    readmePath: '', sourceFile: currentEntry?.sourceFile || 'info.yaml', sourceUrl: currentEntry?.sourceUrl || '', readmeUrl: currentEntry?.readmeUrl || '',
    gitFirstDate: '', gitLastDate: '',
  });
}

function renderPreview() {
  try {
    const card = buildCard();
    if (card.panel_views?.items.some(item => item.id === activePosition)) {
      card.panel_views.default = activePosition;
    }
    els.preview.innerHTML = renderCardArticle({
      card,
      panelImg: '../assets/program_cards/Standalone_computer_rev1.svg',
      yamlUrl: currentEntry?.yamlUrl || '#', uf2Url: currentEntry?.uf2Url || '',
      extraDocs: currentEntry?.extras ? renderReadmeAndDocs({ ...currentEntry.extras, inlinePdf: false, includeReadme: !card.documentation?.intro }) : '',
    });
    if (!cleanText(data.summary)) {
      const summary = els.preview.querySelector('.program-card-hero__main > p');
      if (summary) summary.textContent = 'Add an operator summary to introduce this card.';
    }
    if (currentMode === 'author') {
      renderAuthorControlReference();
      addPanelHitTargets();
    } else {
      decorateAdvancedPreviewTargets();
    }
    addDifferentControlsToggle();
  } catch (error) {
    els.preview.innerHTML = `<p class="author-status is-error">Preview could not render: ${escapeHtml(error.message)}</p>`;
  }
}

function rawBaseForCurrent() {
  if (!currentEntry) return '';
  const sample = currentEntry.uf2Url || currentEntry.uf2Downloads?.[0]?.url || '';
  const marker = `/releases/${currentEntry.id}/`;
  const index = sample.indexOf(marker);
  return index >= 0 ? sample.slice(0, index + marker.length) : '';
}

function referenceItem(def) {
  const value = componentValue(def);
  if (def.kind === 'switch') {
    const rows = ['up', 'middle', 'down', 'tap'].map(position => {
      const name = cleanText(data.controls?.switch?.[position]?.name);
      const description = cleanText(data.controls?.switch?.[position]?.description);
      const label = position === 'tap' ? 'Tap down' : position[0].toUpperCase() + position.slice(1);
      return `<div class="author-switch-inline-row${name ? ' has-value' : ''}"><span>${label}</span><button type="button" class="author-switch-label" data-component="switch:${def.key}:${position}">${name ? `<strong>${escapeHtml(name)}</strong>` : '<em>Add label</em><b aria-hidden="true">+</b>'}</button><button type="button" class="author-reference-description" data-component="switch:${def.key}:${position}" data-edit-description>${description ? escapeHtml(description) : '+ Add description'}</button></div>`;
    }).join('');
    return `<div class="author-reference-component author-reference-switch"><span class="author-reference-key">${escapeHtml(def.label)}</span><div>${rows}</div></div>`;
  }
  const name = cleanText(value.name);
  const description = cleanText(value.description);
  const content = name
    ? `<span class="author-reference-copy"><strong>${escapeHtml(name)}</strong></span><span class="author-reference-edit">Edit label</span>`
    : '<span class="author-reference-empty">Add label</span><b aria-hidden="true">+</b>';
  return `<div class="author-reference-component${name ? ' has-value' : ''}"><span class="author-reference-key">${escapeHtml(def.label.replace(/^(Input|Output) · /, ''))}</span><button type="button" class="author-reference-label" data-component="${def.kind}:${def.key}">${content}</button><button type="button" class="author-reference-description" data-component="${def.kind}:${def.key}" data-edit-description>${description ? escapeHtml(description) : '+ Add description'}</button></div>`;
}

function renderAuthorControlReference() {
  const controls = componentDefs.filter(def => def.kind === 'control' || def.kind === 'switch');
  const inputs = componentDefs.filter(def => def.kind === 'input');
  const outputs = componentDefs.filter(def => def.kind === 'output');
  const markup = `<details class="program-card-section program-card-collapsible author-reference-section" open>
    <summary><h3>Controls</h3></summary><p class="author-reference-note">${differentControls ? `Editing ${activePosition}. Shared labels are inherited until overridden.` : 'Labels apply to all switch positions in basic mode.'}</p>
    <div class="author-reference-grid author-reference-grid--controls">${controls.map(referenceItem).join('')}</div>
  </details>
  <details class="program-card-section program-card-collapsible author-reference-section" open>
    <summary><span class="program-card-io-headings"><h3>Inputs</h3><h3>Outputs</h3></span></summary>
    <div class="author-reference-io"><div class="author-reference-grid">${inputs.map(referenceItem).join('')}</div><div class="author-reference-grid">${outputs.map(referenceItem).join('')}</div></div>
  </details>`;
  for (const reference of els.preview.querySelectorAll('.program-card-use__reference')) reference.innerHTML = markup;
}

function addPanelHitTargets() {
  const panels = els.preview.querySelectorAll('.program-card-use__panel .program-card-panel');
  for (const panel of panels) {
    for (const label of panel.querySelectorAll('[data-panel-switch-position]')) {
      const position = label.dataset.panelSwitchPosition;
      if (!differentControls) {
        label.removeAttribute('data-panel-position-button');
        label.setAttribute('aria-pressed', 'false');
      }
      label.dataset.component = `switch:z:${position}`;
      label.classList.add('author-panel-value');
      label.setAttribute('role', 'button');
      label.setAttribute('tabindex', '0');
      label.setAttribute('aria-label', `Edit Z switch ${position} label`);
      if (!label.classList.contains('has-value')) {
        const circle = document.createElement('span');
        circle.className = 'author-switch-add-circle';
        circle.setAttribute('aria-hidden', 'true');
        label.appendChild(circle);
      }
    }
    for (const def of componentDefs) {
      if (def.kind === 'switch') continue;
      const label = [...panel.querySelectorAll('.program-card-panel__label')].find(candidate =>
        Math.abs(Number.parseFloat(candidate.style.left) - def.left) < 0.2
        && Math.abs(Number.parseFloat(candidate.style.top) - def.top) < 0.2);
      if (label) {
        label.dataset.component = `${def.kind}:${def.key}`;
        label.classList.add('author-panel-value');
        label.setAttribute('role', 'button');
        label.setAttribute('tabindex', '0');
        label.setAttribute('aria-label', `Edit ${def.label}`);
      }
      if (componentHasVisibleValue(def)) continue;
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'author-panel-hit';
      button.dataset.component = `${def.kind}:${def.key}`;
      button.setAttribute('aria-label', `Edit ${def.label}`);
      button.title = `Edit ${def.label}`;
      button.style.left = `${def.left}%`;
      button.style.top = `${def.top}%`;
      panel.appendChild(button);
    }
  }
}

function addDifferentControlsToggle() {
  for (const panelArea of els.preview.querySelectorAll('.program-card-use__panel')) {
    const label = document.createElement('label');
    label.className = 'author-different-controls-toggle';
    const advanced = currentMode === 'yaml';
    label.title = advanced ? 'This indicator follows the conditions in info.yaml.' : '';
    label.innerHTML = `<input type="checkbox" data-different-controls-toggle${differentControls ? ' checked' : ''}${advanced ? ' disabled aria-readonly="true"' : ''}><span>Different panel based on switch position</span>`;
    panelArea.appendChild(label);
  }
}

function markYamlTarget(element, target, label) {
  if (!element) return;
  if (currentMode === 'yaml' && !yamlNodeForTarget(target)) return;
  element.dataset.yamlTarget = target;
  element.classList.add('author-yaml-jump-target');
  element.title = `Double-click to find ${label} in info.yaml`;
}

function decorateAdvancedPreviewTargets() {
  for (const panel of els.preview.querySelectorAll('.program-card-use__panel .program-card-panel')) {
    for (const label of panel.querySelectorAll('[data-panel-switch-position]')) {
      markYamlTarget(label, `switch:${label.dataset.panelSwitchPosition}`, `the ${label.dataset.panelSwitchPosition} switch position`);
    }
    for (const def of componentDefs) {
      if (def.kind === 'switch') continue;
      const label = [...panel.querySelectorAll('.program-card-panel__label')].find(candidate =>
        Math.abs(Number.parseFloat(candidate.style.left) - def.left) < 0.2
        && Math.abs(Number.parseFloat(candidate.style.top) - def.top) < 0.2);
      markYamlTarget(label, `component:${def.kind}:${def.key}`, def.label);
    }
  }

  for (const key of ['main', 'x', 'y']) {
    for (const card of els.preview.querySelectorAll(`.program-card-control--${key}`)) markYamlTarget(card, `component:control:${key}`, `${key} control`);
  }
  for (const card of els.preview.querySelectorAll('.program-card-control--switch')) markYamlTarget(card, 'section:controls', 'switch controls');
  for (const [side, definitions] of [['input', componentDefs.filter(def => def.kind === 'input')], ['output', componentDefs.filter(def => def.kind === 'output')]]) {
    for (const section of els.preview.querySelectorAll(`.program-card-socket-section--${side}s`)) {
      for (const card of section.querySelectorAll('.program-card-socket')) {
        const key = cleanText(card.querySelector('.program-card-component-key')?.textContent).toLowerCase();
        const def = definitions.find(item => cleanText(item.name || item.label.replace(/^(Input|Output) · /, '')).toLowerCase() === key);
        if (def) markYamlTarget(card, `component:${side}:${def.key}`, def.label);
      }
    }
  }

  markYamlTarget(els.preview.querySelector('.program-card-hero'), 'field:Name', 'card details');
  for (const section of els.preview.querySelectorAll('.program-card-controls-section')) markYamlTarget(section, 'section:controls', 'controls');
  for (const section of els.preview.querySelectorAll('.program-card-io-section')) markYamlTarget(section, 'section:panel', 'panel connections');
  markYamlTarget(els.preview.querySelector('.program-card-documentation'), 'field:readme', 'inline README');
  markYamlTarget(els.preview.querySelector('.program-card-demo'), 'field:demo-link', 'demo link');
  markYamlTarget(els.preview.querySelector('.program-card-about'), 'field:Creator', 'creator details');
}

function yamlNodeForTarget(target) {
  let document = yamlTargetCacheDocument;
  if (yamlTargetCacheSource !== els.yaml.value || !document) {
    try { document = YAML.parseDocument(els.yaml.value); } catch { return null; }
    yamlTargetCacheSource = els.yaml.value;
    yamlTargetCacheDocument = document;
  }
  if (document.errors?.length) return null;
  const [type, kind, key] = target.split(':');
  const keyNode = path => {
    const parent = path.length === 1 ? document.contents : document.getIn(path.slice(0, -1), true);
    return parent?.items?.find(pair => String(pair?.key?.value) === path.at(-1))?.key || null;
  };
  const rowKeyNode = (row, name) => row?.items?.find(pair => String(pair?.key?.value) === name)?.key || null;
  if (type === 'field') return keyNode([kind]);
  if (type === 'section') return keyNode(kind.split('.'));
  if (type === 'switch') {
    const explicit = keyNode(['controls', 'switch', kind]);
    if (explicit) return explicit;
    const rows = document.getIn(['controls', 'knobs'], true)?.items || [];
    const row = rows.find(item => item.getIn?.(['when', 'z']) === kind);
    return rowKeyNode(row, 'when');
  }
  if (type !== 'component') return null;
  if (kind === 'control') {
    const rows = document.getIn(['controls', 'knobs'], true)?.items || [];
    const preferred = rows.find(row => row.getIn?.(['when', 'z']) === activePosition && row.get?.(key, true));
    const shared = rows.find(row => !row.get?.('when', true) && row.get?.(key, true));
    return rowKeyNode(preferred || shared || rows.find(row => row.get?.(key, true)), key);
  }
  const def = componentDefs.find(item => item.kind === kind && item.key === key);
  if (!def) return null;
  const rows = document.getIn(['panel', `${kind}s`], true)?.items || [];
  const aliases = kind === 'input' ? {
    cv_1: ['CVIn1', 'CV1'], cv_2: ['CVIn2', 'CV2'],
    pulse_1: ['PulseIn1', 'Pulse1'], pulse_2: ['PulseIn2', 'Pulse2'],
  }[key] : null;
  const sourceIds = new Set(aliases || [def.id]);
  const matching = rows.filter(row => sourceIds.has(row.get?.('id')));
  const row = matching.find(row => row.getIn?.(['when', 'z']) === activePosition)
    || matching.find(row => !row.get?.('when', true))
    || matching[0]
    || null;
  return rowKeyNode(row, 'id');
}

function jumpToYamlTarget(target) {
  const node = yamlNodeForTarget(target);
  const offset = node?.range?.[0];
  if (!Number.isInteger(offset)) {
    els.status.textContent = 'That preview item is generated or is not explicitly present in info.yaml.';
    els.status.className = 'author-status';
    return;
  }
  const start = els.yaml.value.lastIndexOf('\n', Math.max(0, offset - 1)) + 1;
  let end = els.yaml.value.indexOf('\n', offset);
  if (end < 0) end = els.yaml.value.length;
  els.yaml.focus();
  els.yaml.setSelectionRange(start, end);
  const line = els.yaml.value.slice(0, start).split('\n').length - 1;
  const lineHeight = Number.parseFloat(getComputedStyle(els.yaml).lineHeight) || 20;
  els.yaml.scrollTop = Math.max(0, line * lineHeight - els.yaml.clientHeight / 3);
  updateGutter();
  els.yaml.classList.remove('author-yaml-target-flash');
  requestAnimationFrame(() => els.yaml.classList.add('author-yaml-target-flash'));
  setTimeout(() => els.yaml.classList.remove('author-yaml-target-flash'), 900);
}

function validateAndRender({ syncYaml = true, syncForm = false } = {}) {
  const source = parseSource(sourceText(), 'info.yaml');
  const result = validateInfoYaml(source);
  renderDiagnostics(result);
  if (syncYaml) els.yaml.value = sourceText();
  if (syncForm) syncFormFromData();
  updateProgress();
  updateLicenseDisplay();
  renderPreview();
  saveDraft();
}

function updateProgress() {
  const complete = REQUIRED.filter(key => cleanText(data[key])).length;
  els.progress.textContent = `${complete} of ${REQUIRED.length} required fields complete`;
  for (const input of els.editor.querySelectorAll('[required]')) {
    input.closest('.author-field')?.classList.toggle('is-missing', !cleanText(input.value));
  }
}

function syncFormFromData() {
  for (const input of els.editor.querySelectorAll('[data-field]')) input.value = data[input.dataset.field] ?? '';
  for (const input of els.editor.querySelectorAll('[data-list-field]')) input.value = Array.isArray(data[input.dataset.listField]) ? data[input.dataset.listField].join(', ') : (data[input.dataset.listField] ?? '');
  for (const input of els.editor.querySelectorAll('[data-lines-field]')) input.value = Array.isArray(data[input.dataset.linesField]) ? data[input.dataset.linesField].join('\n') : '';
  for (const input of els.editor.querySelectorAll('[data-nested-field]')) input.value = getNested(input.dataset.nestedField) ?? '';
  syncOptionalEditors();
}

function hasOptionalValue(key) {
  const value = data[key];
  if (Array.isArray(value)) return value.length > 0;
  if (value && typeof value === 'object') return Object.keys(value).length > 0;
  return cleanText(value) !== '';
}

function setOptionalOpen(key, open) {
  const editor = document.querySelector(`[data-optional="${CSS.escape(key)}"]`);
  const add = document.querySelector(`[data-add-optional="${CSS.escape(key)}"]`);
  if (editor) editor.hidden = !open;
  if (add) add.hidden = open;
}

function syncOptionalEditors() {
  for (const key of OPTIONAL_KEYS) {
    if (hasOptionalValue(key)) openOptionals.add(key);
    setOptionalOpen(key, openOptionals.has(key));
  }
}

function addOptional(key) {
  openOptionals.add(key);
  setOptionalOpen(key, true);
  const editor = document.querySelector(`[data-optional="${CSS.escape(key)}"]`);
  editor?.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  const focusTarget = editor?.querySelector('input,textarea,button');
  if (key === 'License') document.getElementById('open-license').click();
  else setTimeout(() => focusTarget?.focus(), 180);
}

function removeOptional(key) {
  openOptionals.delete(key);
  delete data[key];
  prune(data);
  syncFormFromData();
  validateAndRender();
}

function componentDefinition(component) {
  const [kind, key] = component.split(':');
  return componentDefs.find(def => def.kind === kind && def.key === key);
}

function isUnconditioned(item) {
  return !item?.when || !Object.keys(item.when).length;
}

function isSimplePosition(item, position) {
  const when = item?.when;
  return when && Object.keys(when).length === 1 && when.z === position;
}

function componentValueAt(def, position = '') {
  if (!def) return {};
  if (def.kind === 'control') {
    const rows = data.controls?.knobs || [];
    const base = rows.find(isUnconditioned)?.[def.key] || {};
    const override = position ? rows.find(row => isSimplePosition(row, position))?.[def.key] || {} : {};
    return { ...base, ...override };
  }
  if (def.kind === 'input') {
    const items = data.panel?.inputs || [];
    const base = items.find(item => item.id === def.id && isUnconditioned(item)) || {};
    const override = position ? items.find(item => item.id === def.id && isSimplePosition(item, position)) || {} : {};
    return { ...base, ...override };
  }
  if (def.kind === 'output') {
    const items = data.panel?.outputs || [];
    const base = items.find(item => item.id === def.id && isUnconditioned(item)) || {};
    const override = position ? items.find(item => item.id === def.id && isSimplePosition(item, position)) || {} : {};
    return { ...base, ...override };
  }
  return {};
}

function componentValue(def) {
  return componentValueAt(def, differentControls ? activePosition : '');
}

function componentHasVisibleValue(def) {
  if (!def) return false;
  if (def.kind === 'switch') {
    return Object.values(data.controls?.switch || {}).some(mode => cleanText(mode?.name));
  }
  return cleanText(componentValue(def).name) !== '';
}

function setBaseComponent(def, name, description) {
  if (!def || def.kind === 'switch') return;
  name = cleanText(name); description = cleanText(description);
  if (def.kind === 'control') {
    const rows = (data.controls ||= {}).knobs ||= [];
    let row = rows.find(isUnconditioned);
    if (!row && (name || description)) {
      row = {};
      rows.unshift(row);
    }
    if (row) {
      if (name || description) row[def.key] = { ...(name ? { name } : {}), ...(description ? { description } : {}) };
      else delete row[def.key];
      if (!Object.keys(row).length) rows.splice(rows.indexOf(row), 1);
    }
    if (!rows.length) delete data.controls.knobs;
  } else {
    const side = def.kind === 'input' ? 'inputs' : 'outputs';
    const list = (data.panel ||= {})[side] ||= [];
    const index = list.findIndex(item => item.id === def.id && isUnconditioned(item));
    if (name || description) {
      const item = { id: def.id, ...(name ? { name } : {}), ...(description ? { description } : {}) };
      if (index >= 0) list[index] = item; else list.push(item);
    } else if (index >= 0) list.splice(index, 1);
  }
  prune(data);
}

function setPositionOverride(def, position, name) {
  const baseName = cleanText(componentValueAt(def).name);
  name = cleanText(name);
  if (def.kind === 'control') {
    const rows = (data.controls ||= {}).knobs ||= [];
    let row = rows.find(item => isSimplePosition(item, position));
    if (name && name !== baseName) {
      if (!row) { row = { when: { z: position } }; rows.push(row); }
      row[def.key] = { ...(row[def.key] || {}), name };
    } else if (row?.[def.key]) {
      delete row[def.key].name;
      if (!Object.keys(row[def.key]).length) delete row[def.key];
      if (Object.keys(row).length === 1) rows.splice(rows.indexOf(row), 1);
    }
  } else {
    const side = def.kind === 'input' ? 'inputs' : 'outputs';
    const list = (data.panel ||= {})[side] ||= [];
    let item = list.find(entry => entry.id === def.id && isSimplePosition(entry, position));
    if (name && name !== baseName) {
      if (!item) { item = { id: def.id, when: { z: position } }; list.push(item); }
      item.name = name;
    } else if (item) {
      delete item.name;
      if (Object.keys(item).every(key => key === 'id' || key === 'when')) list.splice(list.indexOf(item), 1);
    }
  }
  prune(data);
}

function normalizeSharedPositionValues(def) {
  const values = Object.fromEntries(SWITCH_POSITIONS.map(position => [position, cleanText(componentValueAt(def, position).name)]));
  const counts = new Map();
  for (const value of Object.values(values)) if (value) counts.set(value, (counts.get(value) || 0) + 1);
  const shared = [...counts.entries()].find(([, count]) => count >= 2)?.[0];
  if (!shared) return;
  const description = componentValueAt(def).description || '';
  setBaseComponent(def, shared, description);
  for (const position of SWITCH_POSITIONS) setPositionOverride(def, position, values[position]);
}

function hasAuthoredComponentValue(def) {
  if (def.kind === 'control') {
    return (data.controls?.knobs || []).some(row => cleanText(row?.[def.key]?.name));
  }
  const side = def.kind === 'input' ? 'inputs' : 'outputs';
  return (data.panel?.[side] || []).some(item => item.id === def.id && cleanText(item.name));
}

function setComponent(def, name, description) {
  if (!differentControls) {
    setBaseComponent(def, name, description);
    return;
  }
  // A lone value is shared by default. Position-specific YAML is introduced
  // only when the author subsequently gives another position a different value.
  if (!hasAuthoredComponentValue(def)) {
    setBaseComponent(def, name, description);
    return;
  }
  setPositionOverride(def, activePosition, name);
  normalizeSharedPositionValues(def);
}

function hasAuthoredComponentDescription(def) {
  if (def.kind === 'control') {
    return (data.controls?.knobs || []).some(row => cleanText(row?.[def.key]?.description));
  }
  const side = def.kind === 'input' ? 'inputs' : 'outputs';
  return (data.panel?.[side] || []).some(item => item.id === def.id && cleanText(item.description));
}

function setPositionDescription(def, position, description) {
  const baseDescription = cleanText(componentValueAt(def).description);
  description = cleanText(description);
  if (def.kind === 'control') {
    const rows = (data.controls ||= {}).knobs ||= [];
    let row = rows.find(item => isSimplePosition(item, position));
    if (description && description !== baseDescription) {
      if (!row) { row = { when: { z: position } }; rows.push(row); }
      row[def.key] = { ...(row[def.key] || {}), description };
    } else if (row?.[def.key]) {
      delete row[def.key].description;
      if (!Object.keys(row[def.key]).length) delete row[def.key];
      if (Object.keys(row).length === 1) rows.splice(rows.indexOf(row), 1);
    }
  } else {
    const side = def.kind === 'input' ? 'inputs' : 'outputs';
    const list = (data.panel ||= {})[side] ||= [];
    let item = list.find(entry => entry.id === def.id && isSimplePosition(entry, position));
    if (description && description !== baseDescription) {
      if (!item) { item = { id: def.id, when: { z: position } }; list.push(item); }
      item.description = description;
    } else if (item) {
      delete item.description;
      if (Object.keys(item).every(key => key === 'id' || key === 'when')) list.splice(list.indexOf(item), 1);
    }
  }
  prune(data);
}

function normalizeSharedPositionDescriptions(def) {
  const values = Object.fromEntries(SWITCH_POSITIONS.map(position => [position, cleanText(componentValueAt(def, position).description)]));
  const counts = new Map();
  for (const value of Object.values(values)) if (value) counts.set(value, (counts.get(value) || 0) + 1);
  const shared = [...counts.entries()].find(([, count]) => count >= 2)?.[0];
  if (!shared) return;
  setBaseComponent(def, componentValueAt(def).name || '', shared);
  for (const position of SWITCH_POSITIONS) setPositionDescription(def, position, values[position]);
}

function setComponentDescription(def, description) {
  if (!differentControls || !hasAuthoredComponentDescription(def)) {
    setBaseComponent(def, componentValueAt(def).name || '', description);
    return;
  }
  setPositionDescription(def, activePosition, description);
  normalizeSharedPositionDescriptions(def);
}

function updateSwitchDescription(position, description) {
  description = cleanText(description);
  const controls = data.controls ||= {};
  const modes = controls.switch ||= {};
  const mode = modes[position] ||= {};
  if (description) mode.description = description;
  else delete mode.description;
  if (!Object.keys(mode).length) delete modes[position];
  prune(data);
}

function beginInlineComponentEdit(component, anchor) {
  if (anchor.querySelector?.('.author-inline-label-input')) return;
  const def = componentDefinition(component);
  if (!def) return;
  const [, , requestedPosition] = component.split(':');
  const position = def.kind === 'switch' ? (requestedPosition || 'middle') : '';
  const current = def.kind === 'switch'
    ? cleanText(data.controls?.switch?.[position]?.name)
    : cleanText(componentValue(def).name);
  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'author-inline-label-input';
  input.value = current;
  input.placeholder = 'Type label…';
  input.setAttribute('aria-label', `${def.label}${position ? ` ${position}` : ''} label`);

  if (anchor.classList.contains('author-reference-component') || anchor.classList.contains('author-switch-inline-row')) {
    const wrapper = document.createElement('div');
    wrapper.className = anchor.className;
    wrapper.dataset.component = component;
    if (anchor.classList.contains('author-switch-inline-row')) {
      const positionLabel = anchor.querySelector('span')?.textContent || position;
      wrapper.innerHTML = `<span>${escapeHtml(positionLabel)}</span>`;
    } else {
      const key = anchor.querySelector('.author-reference-key')?.textContent || def.label;
      wrapper.innerHTML = `<span class="author-reference-key">${escapeHtml(key)}</span>`;
    }
    wrapper.appendChild(input);
    anchor.replaceWith(wrapper);
  } else if (anchor.classList.contains('program-card-panel-switch-position')) {
    const label = document.createElement('span');
    label.className = `${anchor.className} author-inline-switch-position`;
    label.dataset.panelSwitchPosition = position;
    label.dataset.component = component;
    label.style.cssText = anchor.style.cssText;
    label.appendChild(input);
    anchor.replaceWith(label);
  } else {
    const label = document.createElement('span');
    label.className = 'program-card-panel__label author-inline-panel-label';
    label.style.left = `${def.left}%`;
    label.style.top = `${def.top}%`;
    label.appendChild(input);
    anchor.replaceWith(label);
  }

  let finished = false;
  const finish = (commit) => {
    if (finished) return;
    finished = true;
    if (commit) {
      if (def.kind === 'switch') updateSwitch(position, input.value);
      else setComponent(def, input.value, componentValue(def).description || '');
      validateAndRender();
    } else {
      renderPreview();
    }
  };
  input.addEventListener('blur', () => finish(true));
  input.addEventListener('keydown', event => {
    if (event.key === 'Enter') { event.preventDefault(); input.blur(); }
    if (event.key === 'Escape') { event.preventDefault(); finish(false); }
  });
  input.focus();
  input.select();
}

function beginInlineDescriptionEdit(component, anchor) {
  if (anchor.querySelector?.('.author-inline-description-input')) return;
  const def = componentDefinition(component);
  if (!def) return;
  const [, , requestedPosition] = component.split(':');
  const position = def.kind === 'switch' ? (requestedPosition || 'middle') : '';
  const current = def.kind === 'switch'
    ? cleanText(data.controls?.switch?.[position]?.description)
    : cleanText(componentValue(def).description);
  const textarea = document.createElement('textarea');
  textarea.className = 'author-inline-description-input';
  textarea.rows = 3;
  textarea.value = current;
  textarea.placeholder = 'Describe how this control is used…';
  textarea.setAttribute('aria-label', `${def.label}${position ? ` ${position}` : ''} description`);
  anchor.replaceWith(textarea);

  let finished = false;
  const finish = (commit) => {
    if (finished) return;
    finished = true;
    if (commit) {
      if (def.kind === 'switch') updateSwitchDescription(position, textarea.value);
      else setComponentDescription(def, textarea.value);
      validateAndRender();
    } else {
      renderPreview();
    }
  };
  textarea.addEventListener('blur', () => finish(true));
  textarea.addEventListener('keydown', event => {
    if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') { event.preventDefault(); textarea.blur(); }
    if (event.key === 'Escape') { event.preventDefault(); finish(false); }
  });
  textarea.focus();
  textarea.select();
}

function updateSwitch(position, value) {
  value = cleanText(value);
  const controls = data.controls ||= {};
  const modes = controls.switch ||= {};
  if (value) modes[position] = { ...modes[position], name: value };
  else delete modes[position];
  prune(data);
}

function updateLicenseDisplay() {
  if (data.License) {
    els.licenseValue.textContent = data.License;
    els.licenseHelp.textContent = 'Recorded in the generated info.yaml.';
    document.getElementById('open-license').textContent = 'Review license';
  } else {
    els.licenseValue.textContent = 'No license selected';
    els.licenseHelp.textContent = 'Choose how other people may use and adapt your work.';
    document.getElementById('open-license').textContent = 'Add license';
  }
}

function setManualLicenseHighlight(id = '') {
  document.querySelectorAll('[data-license]').forEach(button => {
    button.setAttribute('aria-pressed', String(button.dataset.license === id));
  });
}

function licenseFileGuidance(id) {
  const guidance = {
    MIT: {
      url: 'https://choosealicense.com/licenses/mit/',
      text: 'Add a file named LICENSE beside info.yaml. Copy the full MIT text, then replace the copyright year and holder with the correct details.',
    },
    'GPL-3.0-or-later': {
      url: 'https://choosealicense.com/licenses/gpl-3.0/',
      text: 'Add a file named LICENSE beside info.yaml containing the complete GPLv3 text. State “GPL-3.0-or-later” in source or README notices and preserve dependency notices.',
    },
    'CC0-1.0': {
      url: 'https://choosealicense.com/licenses/cc0-1.0/',
      text: 'Add a file named LICENSE beside info.yaml containing the complete CC0 1.0 legal text. Confirm that all included material can be dedicated this broadly.',
    },
  };
  const item = guidance[id];
  if (!item) {
    return `<div class="author-license-file-guidance"><strong>Include the license file</strong><p>Add the complete authoritative license text as <code>LICENSE</code> beside <code>info.yaml</code>, and preserve all third-party notices.</p><a href="https://choosealicense.com/licenses/" target="_blank" rel="noopener noreferrer">Find official license text ↗</a></div>`;
  }
  return `<div class="author-license-file-guidance"><strong>Include the license file</strong><p>${escapeHtml(item.text)}</p><a href="${item.url}" target="_blank" rel="noopener noreferrer">Get the ${escapeHtml(id)} license text ↗</a></div>`;
}

function chooseLicense(id, explanation, { manual = false, provisional = false } = {}) {
  selectedLicense = id;
  licenseWasManuallySelected = manual;
  setManualLicenseHighlight(manual ? id : '');
  const label = manual ? 'Selected license' : 'Suggested license';
  const provisionalNote = provisional ? '<p class="author-license-provisional"><strong>Provisional:</strong> answer the remaining legal questions to confirm this suggestion.</p>' : '';
  els.licenseResult.innerHTML = `<span class="author-license-result-label">${label}</span><strong>${escapeHtml(id)}</strong><p>${escapeHtml(explanation)}</p>${provisionalNote}${licenseFileGuidance(id)}`;
  els.useLicense.disabled = !selectedLicense;
}

function recommendLicense() {
  licenseWasManuallySelected = false;
  setManualLicenseHighlight();
  const scenarios = [...document.querySelectorAll('[data-scenario]')].map(select => select.value);
  const inherited = document.getElementById('license-inherited').value;
  if (inherited === 'yes') {
    selectedLicense = '';
    els.useLicense.disabled = true;
    els.licenseResult.innerHTML = '<span class="author-license-result-label">Suggested license</span><strong>Check the upstream license first.</strong><p>Derived and third-party work may already determine which licenses are compatible.</p>';
    return;
  }
  const legalAnswers = ['license-inherited', 'license-closed', 'license-public', 'license-commercial']
    .map(id => document.getElementById(id).value);
  const provisional = legalAnswers.some(answer => !answer);
  if (scenarios.includes('never')) {
    selectedLicense = '';
    els.useLicense.disabled = true;
    els.licenseResult.innerHTML = '<span class="author-license-result-label">Suggested license</span><strong>No automatic open-source recommendation.</strong><p>Standard open-source licenses permit public adaptations. Discuss a source-available or unpublished approach with a maintainer.</p>';
    return;
  }
  if (document.getElementById('license-commercial').value === 'no') {
    selectedLicense = '';
    els.useLicense.disabled = true;
    els.licenseResult.innerHTML = '<span class="author-license-result-label">Suggested license</span><strong>No automatic open-source recommendation.</strong><p>A non-commercial or permission-only condition is not compatible with standard open-source licenses. Ask a maintainer before publishing.</p>';
    return;
  }
  if (document.getElementById('license-public').value === 'yes') {
    chooseLicense('CC0-1.0', 'A broad public-domain-style dedication. Review software patent implications before choosing it for code.', { provisional });
  } else if (document.getElementById('license-closed').value === 'no') {
    chooseLicense('GPL-3.0-or-later', 'Distributed derivatives must remain available under the same open-source terms.', { provisional });
  } else if (document.getElementById('license-closed').value === 'yes') {
    chooseLicense('MIT', 'Permissive reuse, including commercial and closed-source derivatives, with the copyright and license notice retained.', { provisional });
  } else {
    selectedLicense = '';
    els.useLicense.disabled = true;
    els.licenseResult.innerHTML = '<span class="author-license-result-label">Suggested license</span><strong>Keep answering the legal questions.</strong><p>The suggestion will narrow as each answer is provided. You may also choose a license directly.</p>';
    return;
  }
  if (scenarios.includes('ask') && selectedLicense) {
    els.licenseResult.insertAdjacentHTML('beforeend', '<p><strong>Courtesy note:</strong> this license does not require advance permission. “Please contact me first” remains a community request.</p>');
  }
}

function openLicenseAssistant() {
  if (data.License) {
    const descriptions = {
      MIT: 'Permissive reuse with copyright and license notices retained.',
      'GPL-3.0-or-later': 'Derivatives distributed to others must remain open under compatible GPL terms.',
      'CC0-1.0': 'A broad public-domain-style dedication; review its suitability for software.',
    };
    chooseLicense(data.License, descriptions[data.License] || 'Current license recorded in info.yaml.', { manual: true });
  } else {
    recommendLicense();
  }
  els.licenseDialog.showModal();
}

function setMode(mode) {
  const requested = document.querySelector(`[data-mode="${CSS.escape(mode)}"]`);
  if (requested?.disabled) return;
  const yamlMode = mode === 'yaml';
  if (yamlMode) differentControls = hasPositionSpecificData();
  currentMode = mode;
  els.editor.hidden = yamlMode;
  els.yamlPanel.hidden = !yamlMode;
  for (const button of document.querySelectorAll('[data-mode]')) {
    const active = button.dataset.mode === mode;
    button.classList.toggle('is-active', active);
    button.setAttribute('aria-pressed', String(active));
  }
  if (yamlMode) {
    els.yaml.value = sourceText();
    els.yaml.focus();
  }
  renderPreview();
}

function downloadSource() {
  const missing = REQUIRED.filter(key => !cleanText(data[key]));
  if (missing.length && !confirm(`This draft is missing: ${missing.join(', ')}. Download it anyway?`)) return;
  const blob = new Blob([currentMode === 'yaml' ? els.yaml.value : sourceText()], { type: 'text/yaml' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url; link.download = 'info.yaml'; document.body.appendChild(link); link.click(); link.remove();
  URL.revokeObjectURL(url);
}

async function loadExportImage(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Could not load panel artwork (HTTP ${response.status}).`);
  const objectUrl = URL.createObjectURL(await response.blob());
  const image = new Image();
  try {
    await new Promise((resolve, reject) => {
      image.onload = resolve;
      image.onerror = () => reject(new Error('The browser could not render the panel artwork.'));
      image.src = objectUrl;
    });
    return image;
  } finally {
    URL.revokeObjectURL(objectUrl);
  }
}

function wrapCanvasText(context, text, maxWidth, breakAnywhere) {
  const lines = [];
  const appendCharacters = value => {
    let line = '';
    for (const character of value) {
      const candidate = line + character;
      if (line && context.measureText(candidate).width > maxWidth) {
        lines.push(line.trimEnd());
        line = character.trimStart();
      } else {
        line = candidate;
      }
    }
    if (line) lines.push(line.trim());
  };
  for (const paragraph of String(text).split('\n')) {
    if (breakAnywhere) {
      appendCharacters(paragraph);
      continue;
    }
    let line = '';
    for (const word of paragraph.trim().split(/\s+/).filter(Boolean)) {
      const candidate = line ? `${line} ${word}` : word;
      if (line && context.measureText(candidate).width > maxWidth) {
        lines.push(line);
        line = word;
      } else {
        line = candidate;
      }
      if (context.measureText(line).width > maxWidth) {
        const oversized = line;
        line = '';
        appendCharacters(oversized);
      }
    }
    if (line) lines.push(line);
  }
  return lines.filter(Boolean);
}

function drawPanelLabel(context, panelRect, element) {
  const rect = element.getBoundingClientRect();
  const textElement = element.matches('.program-card-panel-switch-position') ? element.querySelector('strong') : element;
  if (!textElement) return;
  const style = getComputedStyle(element);
  const textStyle = getComputedStyle(textElement);
  const neutralSwitchPosition = element.matches('.program-card-panel-switch-position[aria-pressed="true"]');
  const x = rect.left - panelRect.left;
  const y = rect.top - panelRect.top;
  const backgroundColor = neutralSwitchPosition ? '#fdfdfd' : style.backgroundColor;
  if (backgroundColor !== 'rgba(0, 0, 0, 0)') {
    context.fillStyle = backgroundColor;
    context.fillRect(x, y, rect.width, rect.height);
  }
  const borderWidth = neutralSwitchPosition ? 0 : Number.parseFloat(style.borderTopWidth) || 0;
  if (borderWidth && style.borderTopColor !== 'rgba(0, 0, 0, 0)') {
    context.strokeStyle = style.borderTopColor;
    context.lineWidth = borderWidth;
    context.strokeRect(x + borderWidth / 2, y + borderWidth / 2, rect.width - borderWidth, rect.height - borderWidth);
  }
  const paddingLeft = Number.parseFloat(style.paddingLeft) || 0;
  const paddingRight = Number.parseFloat(style.paddingRight) || 0;
  const maxWidth = Math.max(1, rect.width - paddingLeft - paddingRight);
  context.font = `${textStyle.fontWeight} ${textStyle.fontSize} ${textStyle.fontFamily}`;
  context.fillStyle = textStyle.color;
  context.textAlign = 'center';
  context.textBaseline = 'alphabetic';
  const rawText = textElement.innerText || textElement.textContent || '';
  const text = textStyle.textTransform === 'uppercase' ? rawText.toUpperCase() : rawText;
  const lines = wrapCanvasText(context, text, maxWidth, element.classList.contains('program-card-panel-text--long'));
  const lineHeight = Number.parseFloat(textStyle.lineHeight) || Number.parseFloat(textStyle.fontSize) || 11;
  const totalHeight = lines.length * lineHeight;
  const firstBaseline = y + (rect.height - totalHeight) / 2 + lineHeight * 0.82;
  lines.forEach((line, index) => context.fillText(line, x + rect.width / 2, firstBaseline + index * lineHeight));
}

function canvasBlob(canvas) {
  return new Promise((resolve, reject) => canvas.toBlob(blob => {
    if (blob) resolve(blob);
    else reject(new Error('The browser could not create the panel image.'));
  }, 'image/png'));
}

async function downloadPanelImage() {
  const panel = [...els.preview.querySelectorAll('.program-card-use__panel .program-card-panel')]
    .find(element => element.offsetParent !== null);
  if (!panel) {
    els.status.textContent = 'No visible generated panel is available to download.';
    els.status.className = 'author-status is-error';
    return;
  }
  const originalLabel = els.panelDownload.textContent;
  els.panelDownload.disabled = true;
  els.panelDownload.textContent = 'Preparing image…';
  try {
    const rect = panel.getBoundingClientRect();
    const panelArtwork = panel.querySelector('img');
    if (!panelArtwork) throw new Error('The panel artwork is missing.');
    const image = await loadExportImage(panelArtwork.currentSrc || panelArtwork.src);
    const scale = 2;
    const canvas = document.createElement('canvas');
    canvas.width = Math.ceil(rect.width * scale);
    canvas.height = Math.ceil(rect.height * scale);
    const context = canvas.getContext('2d');
    context.scale(scale, scale);
    context.fillStyle = '#fff';
    context.fillRect(0, 0, rect.width, rect.height);
    context.drawImage(image, 0, 0, rect.width, rect.height);
    for (const label of panel.querySelectorAll('.program-card-panel__label,.program-card-panel-switch-position.has-value')) {
      drawPanelLabel(context, rect, label);
    }
    const pngUrl = URL.createObjectURL(await canvasBlob(canvas));
    const filename = cleanText(currentEntry?.id || data.Name || 'new-card').replace(/[^a-z0-9_-]+/gi, '-').replace(/^-+|-+$/g, '').toLowerCase() || 'new-card';
    const link = document.createElement('a');
    link.href = pngUrl;
    link.download = `${filename}-panel.png`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(pngUrl), 1000);
    els.status.textContent = '';
    els.status.className = 'author-status';
  } catch (error) {
    els.status.textContent = `Panel image could not be created: ${error.message}`;
    els.status.className = 'author-status is-error';
  } finally {
    els.panelDownload.disabled = false;
    els.panelDownload.textContent = originalLabel;
  }
}

function startFresh() {
  localStorage.removeItem(STORAGE_KEY);
  localStorage.removeItem(DIFFERENTIAL_STORAGE_KEY);
  const newCardUrl = new URL('new/', document.baseURI);
  if (location.origin === newCardUrl.origin && location.pathname === newCardUrl.pathname) {
    location.reload();
  } else {
    location.href = newCardUrl.href;
  }
}

function slugFromHash() {
  const hash = decodeURIComponent(String(location.hash || '').replace(/^#/, '')).trim();
  const match = hash.match(/^card=(.*)$/);
  return (match ? match[1] : hash).trim();
}

async function loadExistingCard(entry) {
  currentEntry = entry;
  els.editorStatus.textContent = 'Loading…';
  els.sourcePathTitle.textContent = `${entry.id}/info.yaml`;
  if (els.productionLink) {
    els.productionLink.href = `../programs/${encodeURIComponent(entry.slug)}/`;
    els.productionLink.setAttribute('aria-label', `View the published card page for ${entry.id}`);
  }
  try {
    const raw = await fetch(`../${entry.path}`, { cache: 'no-store' }).then(response => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.text();
    });
    entry.extras = await fetch(`../raw-info/${entry.id}/extras.json`, { cache: 'no-store' })
      .then(response => response.ok ? response.json() : null).catch(() => null);
    const source = parseSource(raw, entry.sourceFile || 'info.yaml');
    els.yaml.value = raw;
    if (source.error || !source.data) {
      renderDiagnostics(validateInfoYaml(source));
      updateBasicAvailability(raw, null);
    } else {
      data = source.data;
      differentControls = hasPositionSpecificData();
      syncFormFromData();
      const result = validateInfoYaml(source);
      renderDiagnostics(result);
      renderPreview();
      updateProgress();
      updateLicenseDisplay();
      updateBasicAvailability(raw, data);
      els.editorStatus.textContent = `${result.errorCount} error(s), ${result.warningCount} warning(s)`;
    }
    updateGutter();
  } catch (error) {
    els.editorStatus.textContent = 'Load failed';
    els.status.textContent = `Could not load ${entry.path}: ${error.message}`;
    els.status.className = 'author-status is-error';
  } finally {
    els.page.classList.remove('is-loading');
  }
}

function hasPositionSpecificData() {
  const conditional = item => SWITCH_POSITIONS.includes(item?.when?.z);
  return (data.controls?.knobs || []).some(conditional)
    || (data.panel?.inputs || []).some(conditional)
    || (data.panel?.outputs || []).some(conditional);
}

function applyExistingHash() {
  if (!index.length) return;
  const requested = slugFromHash();
  const entry = index.find(item => item.slug === requested) || index[0];
  const hash = `#${encodeURIComponent(entry.slug)}`;
  if (location.hash !== hash) history.replaceState(null, '', hash);
  els.select.value = String(index.indexOf(entry));
  setMode('yaml');
  loadExistingCard(entry);
}

function autoIndentOnEnter(event) {
  if (event.key !== 'Enter' || event.shiftKey || event.ctrlKey || event.metaKey || event.altKey) return;
  const start = els.yaml.selectionStart;
  const lineStart = els.yaml.value.lastIndexOf('\n', start - 1) + 1;
  const indent = (els.yaml.value.slice(lineStart, start).match(/^[ \t]*/) || [''])[0];
  if (!indent) return;
  event.preventDefault();
  els.yaml.setRangeText(`\n${indent}`, els.yaml.selectionStart, els.yaml.selectionEnd, 'end');
  els.yaml.dispatchEvent(new Event('input', { bubbles: true }));
}

async function initCardPicker() {
  try {
    index = await fetch('../raw-info/index.json', { cache: 'no-store' }).then(response => response.json());
    index.sort((a, b) => String(a.id).localeCompare(String(b.id), undefined, { numeric: true }));
    els.select.innerHTML = `<option value="new">＋ NEW</option>${index.map((entry, position) => `<option value="${position}">${escapeHtml(entry.id)}</option>`).join('')}`;
    els.select.addEventListener('change', () => {
      if (els.select.value === 'new') {
        startFresh();
        return;
      }
      const entry = index[Number(els.select.value)];
      if (!entry) return;
      if (IS_EXISTING) location.hash = encodeURIComponent(entry.slug);
      else location.href = new URL(`./#${encodeURIComponent(entry.slug)}`, document.baseURI).href;
    });
    if (IS_EXISTING) {
      window.addEventListener('hashchange', applyExistingHash);
      applyExistingHash();
    } else {
      els.select.value = 'new';
    }
  } catch (error) {
    if (els.editorStatus) els.editorStatus.textContent = 'Could not load card index';
    els.status.textContent = error.message;
    els.status.className = 'author-status is-error';
    els.page.classList.remove('is-loading');
  }
}

function handleAuthorInput(event) {
  const target = event.target;
  if (target.matches('[data-field]')) {
    const key = target.dataset.field;
    if (cleanText(target.value) || REQUIRED.includes(key)) data[key] = target.value;
    else delete data[key];
  } else if (target.matches('[data-list-field]')) {
    const list = target.value.split(',').map(item => item.trim().toLowerCase().replace(/\s+/g, '-')).filter(Boolean);
    if (list.length) data[target.dataset.listField] = [...new Set(list)]; else delete data[target.dataset.listField];
  } else if (target.matches('[data-lines-field]')) {
    const list = target.value.split('\n').map(item => item.trim()).filter(Boolean);
    if (list.length) data[target.dataset.linesField] = list; else delete data[target.dataset.linesField];
  } else if (target.matches('[data-nested-field]')) {
    setNested(target.dataset.nestedField, target.value);
  }
  validateAndRender();
}

function init() {
  initWorkspaceSplitter();
  initCardPicker();
  syncFormFromData();
  if (!IS_EXISTING) validateAndRender();

  els.editor.addEventListener('input', handleAuthorInput);

  els.preview.addEventListener('click', event => {
    const descriptionButton = event.target.closest('[data-edit-description]');
    if (descriptionButton) {
      beginInlineDescriptionEdit(descriptionButton.dataset.component, descriptionButton);
      return;
    }
    const positionButton = event.target.closest('[data-panel-position-button]');
    if (positionButton) {
      if (positionButton.matches('[data-panel-switch-position][data-component]')
        && positionButton.getAttribute('aria-pressed') === 'true') {
        beginInlineComponentEdit(positionButton.dataset.component, positionButton);
        return;
      }
      const selectPosition = () => {
        activePosition = positionButton.dataset.panelPositionButton;
        renderPreview();
      };
      if (currentMode === 'yaml') {
        clearTimeout(positionClickTimer);
        positionClickTimer = setTimeout(selectPosition, 240);
      } else {
        selectPosition();
      }
      return;
    }
    const hit = event.target.closest('.author-panel-hit,.author-panel-value,.author-reference-component,.author-switch-inline-row');
    if (hit) {
      beginInlineComponentEdit(hit.dataset.component, hit);
      return;
    }
    let editorId = '';
    if (event.target.closest('.program-card-hero')) editorId = 'card-details-editor';
    else if (event.target.closest('.program-card-quick-start,.program-card-documentation,.program-card-about')) editorId = 'optional-fields-editor';
    if (editorId) {
      document.getElementById(editorId).scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  });

  els.preview.addEventListener('keydown', event => {
    if ((event.key === 'Enter' || event.key === ' ') && event.target.matches('.author-panel-value,.author-reference-label,.author-switch-label,.author-reference-description')) {
      event.preventDefault();
      event.target.click();
    }
  });
  els.preview.addEventListener('dblclick', event => {
    if (currentMode !== 'yaml') return;
    const target = event.target.closest('[data-yaml-target]');
    if (!target) return;
    event.preventDefault();
    clearTimeout(positionClickTimer);
    jumpToYamlTarget(target.dataset.yamlTarget);
  });
  els.preview.addEventListener('change', event => {
    if (!event.target.matches('[data-different-controls-toggle]')) return;
    differentControls = event.target.checked;
    localStorage.setItem(DIFFERENTIAL_STORAGE_KEY, String(differentControls));
    activePosition = 'middle';
    renderPreview();
  });

  document.getElementById('optional-catalog').addEventListener('click', event => {
    const button = event.target.closest('[data-add-optional]');
    if (button) addOptional(button.dataset.addOptional);
  });
  document.getElementById('optional-editors').addEventListener('click', event => {
    const button = event.target.closest('[data-remove-optional]');
    if (button) removeOptional(button.dataset.removeOptional);
  });

  els.yaml.addEventListener('input', () => {
    updateGutter();
    clearTimeout(yamlTimer);
    yamlTimer = setTimeout(() => {
      const source = parseSource(els.yaml.value, 'info.yaml');
      if (source.error || !source.data) {
        els.status.textContent = 'Fix the YAML syntax error before returning to visual editing.';
        els.status.className = 'author-status is-error';
        renderDiagnostics(validateInfoYaml(source));
        updateBasicAvailability(els.yaml.value, null);
        return;
      }
      data = source.data;
      if (IS_EXISTING) differentControls = hasPositionSpecificData();
      els.status.textContent = '';
      els.status.className = 'author-status';
      validateAndRender({ syncYaml: false, syncForm: true });
      updateBasicAvailability(els.yaml.value, data);
    }, 250);
  });
  els.yaml.addEventListener('scroll', updateGutter);
  els.yaml.addEventListener('keydown', autoIndentOnEnter);

  document.querySelectorAll('[data-mode]').forEach(button => button.addEventListener('click', () => {
    if (button.dataset.mode === 'author') {
      const source = parseSource(els.yaml.value, 'info.yaml');
      if (!els.yamlPanel.hidden && (source.error || !source.data)) return;
    }
    setMode(button.dataset.mode);
  }));

  els.download.addEventListener('click', downloadSource);
  els.panelDownload.addEventListener('click', downloadPanelImage);
  els.startFresh.addEventListener('click', startFresh);
  els.yamlFullscreen.addEventListener('click', toggleYamlFullscreen);
  els.previewFullscreen.addEventListener('click', togglePreviewFullscreen);
  document.addEventListener('fullscreenchange', updateFullscreenButtons);
  document.getElementById('open-license').addEventListener('click', openLicenseAssistant);
  document.querySelectorAll('[data-license]').forEach(button => button.addEventListener('click', () => {
    const descriptions = {
      MIT: 'Permissive reuse with copyright and license notices retained.',
      'GPL-3.0-or-later': 'Derivatives distributed to others must remain open under compatible GPL terms.',
      'CC0-1.0': 'A broad public-domain-style dedication; review its suitability for software.',
    };
    chooseLicense(button.dataset.license, descriptions[button.dataset.license], { manual: true });
  }));
  document.querySelectorAll('#license-inherited, #license-closed, #license-public, #license-commercial, [data-scenario]')
    .forEach(control => control.addEventListener('change', recommendLicense));
  els.useLicense.addEventListener('click', event => {
    if (!selectedLicense) { event.preventDefault(); return; }
    data.License = selectedLicense;
    validateAndRender({ syncForm: true });
    els.licenseDialog.close();
  });
}

init();
