// Unified visual/YAML author page for new and existing cards.

export function renderAuthorPage({ documentKind = 'new' } = {}) {
  const existing = documentKind === 'existing';
  const baseHref = existing ? './' : '../';
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <base href="${baseHref}">
  <title>Author page – Workshop Computer</title>
  <link rel="stylesheet" href="../assets/style.css">
  <link rel="stylesheet" href="../assets/program-cards.css">
  <link rel="stylesheet" href="../assets/github-markdown.css">
  <link rel="stylesheet" href="./author.css?v=23">
  <script type="importmap">
  {
    "imports": {
      "yaml": "./vendor/yaml/index.js",
      "marked": "./vendor/marked.esm.js"
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
          <label class="author-document-picker">Card <select id="card-select" aria-label="Card"><option value="new">＋ NEW</option></select></label>${existing ? '<a id="production-card-link" class="author-production-link" href="#" target="_blank" rel="noopener noreferrer">View card page ↗</a><span id="editor-status" class="author-progress" aria-live="polite"></span>' : ''}
          <span id="required-progress" class="author-progress" aria-live="polite"></span>
          <div class="author-mode-switch" role="group" aria-label="Editing mode">
            <button type="button"${existing ? ' disabled title="Basic availability is checked after the card loads"' : ' class="is-active"'} data-mode="author" aria-pressed="${existing ? 'false' : 'true'}">Basic</button>
            <button type="button"${existing ? ' class="is-active"' : ''} data-mode="yaml" aria-pressed="${existing ? 'true' : 'false'}">Advanced</button>
          </div>
        </div>
        <div class="author-toolbar__downloads">
          <button id="download-source" class="btn download" type="button">Download info.yaml</button>
          <button id="download-panel-image" class="btn secondary" type="button">Download panel image</button>
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
            <label class="author-field author-field--wide"><span>Short description <strong>Required</strong></span><textarea data-field="short-description" required rows="2" placeholder="A concise tagline for indexes, shelves, and archive rows"></textarea></label>
            <label class="author-field author-field--wide"><span>Summary <strong>Required</strong></span><textarea data-field="summary" required rows="4" placeholder="A longer operator overview for the card detail page"></textarea></label>
            <label class="author-field"><span>Creator <strong>Required</strong></span><input data-field="Creator" required placeholder="Your name or handle"></label>
            <label class="author-field"><span>Language <strong>Required</strong></span><input data-field="Language" required placeholder="ie. Pico SDK"></label>
            <label class="author-field"><span>Version <strong>Required</strong></span><input data-field="Version" required placeholder="For example, 1.0.0"></label>
            <label class="author-field"><span>Status <strong>Required</strong></span><select data-field="Status" required><option value="" selected disabled>Choose status…</option><option>WIP</option><option>Beta</option><option>Released</option></select></label>
            <div id="license-recommended-field" class="author-field author-field--wide"><span>License <em class="author-recommended-label">Recommended</em></span><div class="author-license-row"><div><strong id="license-value">No license selected</strong><p id="license-help">Choose how other people may use and adapt your work. The validator will warn if this is omitted.</p></div><button id="open-license" class="btn secondary" type="button">Choose license</button></div></div>
          </div>
        </section>

        <section class="author-form-card" id="optional-fields-editor">
          <header><div><span class="author-step">Build out the page</span><h2>Add details</h2></div><span class="author-optional-key">Optional</span></header>
          <p>Choose only the details this card needs.</p>
          <div id="optional-catalog" class="author-add-list" aria-label="Available optional fields">
            <button type="button" data-add-optional="tags"><span><strong>Tags</strong><small>Help people discover the card.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="readme"><span><strong>Inline README</strong><small>Full Markdown documentation that replaces README.md.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="demo-link"><span><strong>Demo video</strong><small>Add a YouTube demonstration.</small></span><b aria-hidden="true">+</b></button>
            <button type="button" data-add-optional="contact"><span><strong>Contact website</strong><small>Point readers to the creator or project.</small></span><b aria-hidden="true">+</b></button>
          </div>
          <div id="optional-editors" class="author-optional-editors">
            <div class="author-optional-editor" data-optional="tags" hidden><header><h3>Tags</h3><button type="button" data-remove-optional="tags" aria-label="Remove tags">×</button></header><label class="author-field"><input data-list-field="tags" placeholder="synthesizer, sequencer, utility"><small>Comma-separated, lowercase tags.</small></label></div>
            <div class="author-optional-editor" data-optional="readme" hidden><header><h3>Inline README</h3><button type="button" data-remove-optional="readme" aria-label="Remove inline README">×</button></header><label class="author-field"><textarea data-field="readme" rows="7" placeholder="Full operating instructions; Markdown is supported"></textarea><small>When present, this replaces the rendered README.md section. Documentation PDFs remain visible.</small></label></div>
            <div class="author-optional-editor" data-optional="demo-link" hidden><header><h3>Demo video</h3><button type="button" data-remove-optional="demo-link" aria-label="Remove demo video">×</button></header><label class="author-field"><input data-field="demo-link" type="url" placeholder="https://www.youtube.com/..."></label></div>
            <div class="author-optional-editor" data-optional="contact" hidden><header><h3>Contact website</h3><button type="button" data-remove-optional="contact" aria-label="Remove contact website">×</button></header><label class="author-field"><input data-nested-field="contact.website" type="url" placeholder="https://..."></label></div>
          </div>
        </section>
      </section>

      <section id="yaml-editor" class="author-yaml" aria-label="Advanced YAML editor"${existing ? '' : ' hidden'}>
        <div class="author-yaml__diagnostics"><h2>Diagnostics</h2><div id="diagnostics"></div></div>
        <div class="author-yaml__head"><div><h2 id="source-path-title">${existing ? 'info.yaml' : 'new_card/info.yaml'}</h2><p>Edit the source directly. Diagnostics and the preview update as you type.</p></div><button id="yaml-fullscreen" class="author-icon-button" type="button" aria-label="Enter full screen" title="Enter full screen" aria-pressed="false"><span class="author-fullscreen-expand" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/></svg></span><span class="author-fullscreen-close" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="m5 5 14 14M19 5 5 19"/></svg></span></button></div>
        <div class="author-editor-wrap"><div class="author-editor-gutter" aria-hidden="true"><div id="gutter-inner">1</div></div><textarea id="yaml-source" spellcheck="false" aria-label="Raw info.yaml source"></textarea></div>
      </section>

      <div id="author-splitter" class="author-splitter" role="separator" aria-label="Resize editor and preview" aria-orientation="vertical" aria-valuemin="320" aria-valuemax="1000" aria-valuenow="560" tabindex="0"><span aria-hidden="true"></span></div>

      <section class="author-preview-column" aria-label="Live card preview">
        <div class="author-preview-heading"><div class="author-preview-heading__copy"><h2>Live preview</h2><span>Click a panel component to edit it</span></div><div class="author-preview-heading__actions"><button id="preview-fullscreen" class="author-icon-button" type="button" aria-label="Enter preview full screen" title="Enter preview full screen" aria-pressed="false"><span class="author-fullscreen-expand" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/></svg></span><span class="author-fullscreen-close" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="m5 5 14 14M19 5 5 19"/></svg></span></button></div></div>
        <div id="card-preview"></div>
      </section>
    </div>
  </main>

  <dialog id="license-dialog" class="author-license-dialog" aria-labelledby="license-title">
    <form method="dialog">
      <header><div><span class="author-step">License assistant</span><h2 id="license-title">How may others use your work?</h2></div><button value="cancel" aria-label="Close">×</button></header>
      <p class="author-legal-note">This assistant explains common choices and records your selection. It is not legal advice. Confirm that third-party code and assets are compatible. For more guidance, visit <a href="https://choosealicense.com/" target="_blank" rel="noopener noreferrer">Choose a License ↗</a>.</p>
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

  <script type="module" src="./author-client.js?v=33"></script>
</body>
</html>`;
}
