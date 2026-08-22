// Unified visual/YAML author page for new and existing cards.

import { applyContentSecurityPolicy, CSP_PLACEHOLDER } from './csp.js';
import { externalLinkArrow } from './icons.js';
import { renderSocialMeta } from './socialMeta.js';

export function renderAuthorPage({ documentKind = 'new', suggestions = {}, social } = {}) {
  const existing = documentKind === 'existing';
  const baseHref = existing ? './' : '../';
  const escape = value => String(value ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const datalist = (id, values) => `<datalist id="${id}">${(values || []).map(value => `<option value="${escape(value)}"></option>`).join('')}</datalist>`;
  const html = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="Content-Security-Policy" content="${CSP_PLACEHOLDER}">
  <base href="${baseHref}">
  <title>Author page – Workshop Computer</title>
  ${renderSocialMeta({ title: 'Author page – Workshop Computer', ...social })}
  <link rel="icon" type="image/png" href="../assets/favicon/favicon.png">
  <link rel="stylesheet" href="../assets/style.css">
  <link rel="stylesheet" href="../assets/program-cards.css">
  <link rel="stylesheet" href="../assets/github-markdown.css">
  <link rel="stylesheet" href="./monaco-editor.css?v=1">
  <link rel="stylesheet" href="./author.css?v=29">
  <script type="importmap">
  {
    "imports": {
      "yaml": "./vendor/yaml/index.js",
      "marked": "./vendor/marked.esm.js",
      "sanitize-html": "./vendor/sanitize-html.esm.js",
      "ajv": "./vendor/ajv.esm.js"
    }
  }
  </script>
</head>
<body class="theme-light">
  <main class="author-page${existing ? ' is-loading' : ''}" data-document-kind="${documentKind}">
    <header class="author-toolbar">
      <div>
        <h1>Card metadata author</h1>
        <p>${existing ? 'Inspect and edit an existing card, with a production-matched live preview.' : 'Create a card page visually, then download the generated <code>info.yaml</code>.'}</p>
      </div>
      <div class="author-toolbar__actions">
        <div class="author-toolbar__primary">
          <label class="author-document-picker">Card <select id="card-select" aria-label="Card"><option value="new">＋ NEW</option></select></label>${existing ? `<a id="production-card-link" class="author-production-link" href="#" target="_blank" rel="noopener noreferrer">View card page${externalLinkArrow()}</a><span id="editor-status" class="author-progress" aria-live="polite"></span>` : '<a class="author-production-link" href="../index.html">Program cards home</a>'}
          <span id="required-progress" class="author-progress" aria-live="polite"></span>
          <div class="author-mode-switch" role="group" aria-label="Editing mode">
            <button type="button"${existing ? ' disabled title="Basic availability is checked after the card loads"' : ' class="is-active"'} data-mode="author" aria-pressed="${existing ? 'false' : 'true'}">Basic</button>
            <button type="button"${existing ? ' class="is-active"' : ''} data-mode="yaml" aria-pressed="${existing ? 'true' : 'false'}">Advanced</button>
          </div>
        </div>
        <div class="author-toolbar__downloads">
          <a class="btn secondary" href="https://github.com/TomWhitwell/Workshop_Computer/blob/main/documentation/info.yaml.md" target="_blank" rel="noopener noreferrer">Schema documentation${externalLinkArrow()}</a>
          <button id="download-source" class="btn download" type="button">Download info.yaml</button>
          <button id="download-panel-image" class="btn secondary" type="button">Download panel SVG</button>
          <button id="start-fresh" class="btn secondary" type="button">Start fresh</button>
        </div>
      </div>
    </header>

    <div id="author-status" class="author-status" role="status" aria-live="polite"></div>
    ${existing ? '<div class="author-loading" role="status" aria-live="polite"><span class="author-spinner" aria-hidden="true"></span><span>Loading cards…</span></div>' : ''}

    <div class="author-workspace">
      <section id="author-editor" class="author-editor" aria-label="Card fields"${existing ? ' hidden' : ''}>
        <section class="author-form-card" id="card-details-editor">
          <header><div><span class="author-step">Start here</span><h2>Card details</h2></div><span class="author-required-key">Required</span></header>
          <div class="author-form-grid">
            <label class="author-field author-field--wide"><span>Name <strong>Required</strong></span><input data-field="Name" required placeholder="Card display name"></label>
            <label class="author-field author-field--wide"><span>Short description <strong>Required</strong> <small class="author-field-guidance">(used in card search and the all cards index; <a href="../archive/" target="_blank" rel="noopener noreferrer">see example${externalLinkArrow()}</a>)</small></span><textarea data-field="short-description" required rows="2" placeholder="A concise tagline for indexes, shelves, and archive rows"></textarea></label>
            <label class="author-field author-field--wide"><span>Summary <strong>Required</strong> <small class="author-field-guidance">(used beneath the title on card pages; <a href="../programs/15-mlrws/" target="_blank" rel="noopener noreferrer">see example${externalLinkArrow()}</a>)</small></span><textarea data-field="summary" required rows="4" placeholder="A longer operator overview for the card detail page"></textarea><small>Markdown is supported, including links, emphasis, and inline code.</small></label>
            <label class="author-field"><span>Creator <strong>Required</strong></span><input data-field="Creator" required list="creator-suggestions" placeholder="Your name or handle"></label>
            <label class="author-field"><span>Language <strong>Required</strong></span><input data-field="Language" required list="language-suggestions" placeholder="ie. Pico SDK"></label>
            <label class="author-field"><span>Version <strong>Required</strong></span><input data-field="Version" required placeholder="For example, 1.0.0"></label>
            <label class="author-field"><span>Status <strong>Required</strong></span><input data-field="Status" required list="status-suggestions" placeholder="Choose or add a status"></label>
            <label class="author-field"><span>Date created</span><input data-field="date-created" type="date"><small>Optional publication or original release date.</small></label>
            <label class="author-field"><span>Date updated</span><input data-field="date-updated" type="date"><small>Optional date of the most recent substantial update.</small></label>
            <div id="license-recommended-field" class="author-field author-field--wide"><span>License <em class="author-recommended-label">Recommended</em></span><div class="author-license-row"><div><strong id="license-value">No license selected</strong><p id="license-help">Choose how other people may use and adapt your work. The validator will warn if this is omitted.</p></div><button id="open-license" class="btn secondary" type="button">Choose license</button></div></div>
          </div>
        </section>

        <section class="author-form-card" id="optional-fields-editor">
          <header><div><span class="author-step">Build out the page</span><h2>Add details</h2></div><span class="author-optional-key">Optional</span></header>
          <p>Choose only the details this card needs.</p>
          <div id="optional-catalog" class="author-add-list" aria-label="Available optional fields">
            <button type="button" data-add-optional="demo-link"><span><strong>Demo video</strong><small>Add a YouTube or Instagram demonstration.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="Editor"><span><strong>Web editor</strong><small>Add, change, or hide the browser editor action.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="tags"><span><strong>Tags</strong><small>Help people discover the card.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="contact"><span><strong>Contact email</strong><small>Add a public email address for the creator or maintainer.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="discussion"><span><strong>Support / discussion URL</strong><small>Link to card-specific support, questions, or feedback.</small></span><b aria-hidden="true">+</b></button>
          </div>
          <div id="optional-editors" class="author-optional-editors">
            <div class="author-optional-editor" data-optional="tags" hidden><header><h3>Tags</h3><button type="button" data-remove-optional="tags" aria-label="Remove tags">×</button></header><label class="author-field"><span class="author-token-field" data-token-field="tags"><span class="author-token-list" data-token-list="tags"></span><input data-token-input="tags" list="tag-suggestions" placeholder="Add a tag…" autocomplete="off"><input type="hidden" data-list-field="tags"></span><small>Choose an existing tag or type a new one, then press Enter or comma. Select × to remove it.</small></label></div>
            <div class="author-optional-editor" data-optional="readme" hidden><header><h3>Inline README</h3><button type="button" data-remove-optional="readme" aria-label="Remove inline README">×</button></header><label class="author-field"><textarea data-field="readme" rows="7" placeholder="Full operating instructions; Markdown is supported"></textarea><small>When present, this replaces the rendered README.md section. Documentation PDFs remain visible.</small></label></div>
            <div class="author-optional-editor" data-optional="demo-link" hidden><header><h3>Demo video</h3><button type="button" data-remove-optional="demo-link" aria-label="Remove demo video">×</button></header><label class="author-field"><input data-field="demo-link" type="url" placeholder="https://www.youtube.com/... or https://www.instagram.com/reel/..."></label></div>
            <div class="author-optional-editor" data-optional="Editor" hidden><header><h3>Web editor</h3><button type="button" data-remove-optional="Editor" aria-label="Remove web editor metadata">×</button></header><label class="author-field"><span>Editor</span><input data-field="Editor" list="editor-suggestions" placeholder="https://…, web, dist, or none"><small>Use an HTTPS URL, <code>web</code> or <code>dist</code> for a local folder, or <code>none</code> to hide the action.</small></label><label class="author-field"><span>Web entry</span><input data-field="web-entry" placeholder="index.html"><small>Optional entry file when the local editor does not use <code>index.html</code>.</small></label></div>
            <div class="author-optional-editor" data-optional="contact" hidden><header><h3>Contact email</h3><button type="button" data-remove-optional="contact" aria-label="Remove contact email">×</button></header><label class="author-field"><input data-nested-field="contact.email" type="email" placeholder="name@example.com"><small>This address will be public.</small></label></div>
            <div class="author-optional-editor" data-optional="discussion" hidden><header><h3>Support / discussion URL</h3><button type="button" data-remove-optional="discussion" aria-label="Remove support or discussion URL">×</button></header><label class="author-field"><input data-field="discussion" type="url" placeholder="https://..."><small>Link to a Discord thread, issue tracker, forum topic, or other card-specific support page.</small></label></div>
          </div>
        </section>
      </section>

      <section id="yaml-editor" class="author-yaml" aria-label="Advanced YAML editor"${existing ? '' : ' hidden'}>
        <div class="author-yaml__diagnostics"><h2>Diagnostics</h2><div id="diagnostics"></div></div>
        <div class="author-yaml__head"><div><h2 id="source-path-title">${existing ? 'info.yaml' : 'new_card/info.yaml'}</h2><p>Edit the source directly. YAML language and Workshop schema diagnostics update as you type.</p></div><div class="author-yaml__actions"><div class="author-yaml__action-row"><button id="format-yaml" class="btn secondary" type="button">Auto-format YAML</button><button id="yaml-fullscreen" class="author-icon-button" type="button" aria-label="Enter full screen" title="Enter full screen" aria-pressed="false"><span class="author-fullscreen-expand" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/></svg></span><span class="author-fullscreen-close" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="m5 5 14 14M19 5 5 19"/></svg></span></button></div><label class="author-toggle" title="Show spaces as dots and tabs as arrows"><input id="toggle-whitespace" type="checkbox"><span class="author-toggle__track" aria-hidden="true"><span></span></span><span>Show whitespace</span></label></div></div>
        <div class="author-editor-wrap"><textarea id="yaml-source" hidden aria-hidden="true"></textarea><div id="yaml-monaco" class="author-monaco-editor" aria-label="Raw info.yaml source"><p class="author-monaco-loading">Loading advanced editor…</p></div></div>
      </section>

      <div id="author-splitter" class="author-splitter" role="separator" aria-label="Resize editor and preview" aria-orientation="vertical" aria-valuemin="320" aria-valuemax="1000" aria-valuenow="560" tabindex="0"><span aria-hidden="true"></span></div>

      <section class="author-preview-column" aria-label="Live card preview">
        <div class="author-preview-heading"><div class="author-preview-heading__copy"><h2>Live preview</h2><span>Click a panel component to edit it</span></div><div class="author-preview-heading__actions"><button id="preview-fullscreen" class="author-icon-button" type="button" aria-label="Enter preview full screen" title="Enter preview full screen" aria-pressed="false"><span class="author-fullscreen-expand" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/></svg></span><span class="author-fullscreen-close" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="m5 5 14 14M19 5 5 19"/></svg></span></button></div></div>
        <div id="card-preview"></div>
      </section>
    </div>
    ${datalist('creator-suggestions', suggestions.creators)}
    ${datalist('language-suggestions', suggestions.languages)}
    ${datalist('status-suggestions', suggestions.statuses)}
    ${datalist('tag-suggestions', suggestions.tags)}
    ${datalist('editor-suggestions', ['none', 'web', 'dist'])}
  </main>

  <dialog id="license-dialog" class="author-license-dialog" aria-labelledby="license-title">
    <form method="dialog">
      <header><div><span class="author-step">License assistant</span><h2 id="license-title">How may others use your work?</h2></div><button value="cancel" aria-label="Close">×</button></header>
      <p class="author-legal-note">This assistant explains common choices and records your selection. It is not legal advice. Confirm that third-party code and assets are compatible. For more guidance, visit <a href="https://choosealicense.com/" target="_blank" rel="noopener noreferrer">Choose a License${externalLinkArrow()}</a>.</p>
      <div class="author-license-boundary author-license-boundary--legal"><strong>Legal license</strong><span>The choices in this section determine the legal permissions and obligations attached to the source code.</span></div>
      <div class="author-license-quick">
        <h3>Choose a legal license directly</h3>
        <div><button type="button" data-license="MIT" aria-pressed="false">MIT</button><button type="button" data-license="GPL-3.0-or-later" aria-pressed="false">GPL-3.0-or-later</button><button type="button" data-license="CC0-1.0" aria-pressed="false">CC0-1.0</button></div>
      </div>
      <fieldset class="author-license-questions">
        <legend>Or answer a few legal-license questions</legend>
        <label>This includes or derives from third-party code<select id="license-inherited"><option value="" selected disabled>Choose answer…</option><option value="no">No</option><option value="yes">Yes</option></select></label>
        <label>May people distribute closed-source versions?<select id="license-closed"><option value="" selected disabled>Choose answer…</option><option value="yes">Yes</option><option value="no">No, derivatives must stay open</option></select></label>
        <label>Do you want to dedicate your rights as broadly as possible?<select id="license-public"><option value="" selected disabled>Choose answer…</option><option value="no">No</option><option value="yes">Yes</option></select></label>
        <label>May others sell or commercially distribute modified software or hardware versions?<select id="license-commercial"><option value="" selected disabled>Choose answer…</option><option value="yes">Yes</option><option value="no">No / ask me first</option></select></label>
        <p class="author-question-help">This concerns commercial distribution of the card software, ports, and hardware derivatives. It does not restrict selling or releasing music made with the card.</p>
      </fieldset>
      <div class="author-license-boundary author-license-boundary--preferences"><strong>Author preferences — separate from the legal license</strong><span>These answers express community preferences only. Courtesy requests cannot override permissions granted by the selected legal license.</span></div>
      <fieldset id="community-preferences">
        <legend>Author preferences for ports and adaptations</legend>
        <label>Someone publishes a derivative or modified Workshop System card<select data-scenario><option value="" selected disabled>Choose answer…</option><option value="credit">OK without asking, with credit</option><option value="open">OK without asking</option><option value="ask">Please contact me first</option><option value="never">I am not comfortable with this</option></select></label>
        <label>Someone publishes a VCV Rack port<select data-scenario><option value="" selected disabled>Choose answer…</option><option value="credit">OK without asking, with credit</option><option value="open">OK without asking</option><option value="ask">Please contact me first</option><option value="never">I am not comfortable with this</option></select></label>
        <label>Someone publishes and sells a plugin port<select data-scenario><option value="" selected disabled>Choose answer…</option><option value="credit">OK without asking, with credit</option><option value="open">OK without asking</option><option value="ask">Please contact me first</option><option value="never">I am not comfortable with this</option></select></label>
        <label>Someone publishes and sells a hardware version<select data-scenario><option value="" selected disabled>Choose answer…</option><option value="credit">OK without asking, with credit</option><option value="open">OK without asking</option><option value="ask">Please contact me first</option><option value="never">I am not comfortable with this</option></select></label>
      </fieldset>
      <div id="license-result" class="author-license-result" aria-live="polite"></div>
      <footer><button value="cancel" class="btn secondary">Cancel</button><button id="use-license" value="default" class="btn download" disabled>Use this license</button></footer>
    </form>
  </dialog>

  <script type="module" src="./author-client.js?v=50"></script>
</body>
</html>`;
  return applyContentSecurityPolicy(html, { preview: true });
}
