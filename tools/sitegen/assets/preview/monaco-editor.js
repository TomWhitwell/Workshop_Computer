import * as monaco from 'monaco-editor/esm/vs/editor/editor.api.js';
import 'monaco-editor/esm/vs/editor/contrib/hover/browser/hoverContribution.js';
import 'monaco-editor/esm/vs/editor/contrib/snippet/browser/snippetController2.js';
import 'monaco-editor/esm/vs/editor/contrib/suggest/browser/suggestController.js';
import 'monaco-editor/esm/vs/basic-languages/yaml/yaml.contribution.js';
import { configureMonacoYaml } from 'monaco-yaml';
import { infoYamlJsonSchema } from '../../src/schema/infoYamlJsonSchema.js';

const MARKER_OWNER = 'workshop-info-yaml';
const MODEL_URI = monaco.Uri.parse('file:///info.yaml');
const THEME_NAME = 'workshop-yaml-light';
const PANEL_IDS = {
  inputs: ['AudioIn1', 'AudioIn2', 'CVIn1', 'CVIn2', 'PulseIn1', 'PulseIn2'],
  outputs: ['AudioOut1', 'AudioOut2', 'CVOut1', 'CVOut2', 'PulseOut1', 'PulseOut2'],
};

self.MonacoEnvironment = {
  getWorkerUrl: (_moduleId, label) => new URL(
    label === 'yaml' ? './monaco-yaml-worker.js' : './monaco-editor-worker.js',
    import.meta.url
  ).href,
};

const yamlService = configureMonacoYaml(monaco, {
  completion: true,
  enableSchemaRequest: false,
  format: { enable: false },
  hover: true,
  hoverSchemaSource: false,
  indentation: '  ',
  schemas: [{
    fileMatch: [MODEL_URI.toString()],
    schema: infoYamlJsonSchema,
    uri: infoYamlJsonSchema.$id,
  }],
  validate: true,
  yamlVersion: '1.2',
});

monaco.editor.defineTheme(THEME_NAME, {
  base: 'vs',
  inherit: true,
  rules: [],
  colors: { 'editorWhitespace.foreground': '#7a8089' },
});

function markerSeverity(severity) {
  return severity === 'error' ? monaco.MarkerSeverity.Error : monaco.MarkerSeverity.Warning;
}

function panelIdContext(model, position) {
  if (!position) return null;
  const line = model.getLineContent(position.lineNumber);
  const match = line.match(/^(\s*)(?:-\s*)?id:\s*([A-Za-z0-9]*)$/);
  if (!match) return null;
  const itemIndent = match[1].length;
  for (let lineNumber = position.lineNumber - 1; lineNumber >= 1; lineNumber -= 1) {
    const section = model.getLineContent(lineNumber).match(/^(\s*)(inputs|outputs):\s*$/);
    if (!section || section[1].length >= itemIndent) continue;
    const valueLength = match[2].length;
    return {
      ids: PANEL_IDS[section[2]],
      range: new monaco.Range(position.lineNumber, position.column - valueLength, position.lineNumber, position.column),
    };
  }
  return null;
}

const panelIdCompletion = monaco.languages.registerCompletionItemProvider('yaml', {
  provideCompletionItems(model, position) {
    const context = panelIdContext(model, position);
    if (!context) return { suggestions: [] };
    return {
      suggestions: context.ids.map(id => ({
        label: id,
        kind: monaco.languages.CompletionItemKind.EnumMember,
        insertText: id,
        range: context.range,
        detail: 'Workshop Computer panel ID',
      })),
    };
  },
});

export function createYamlEditor({ container, source, showWhitespace = false }) {
  const model = monaco.editor.createModel(source.value, 'yaml', MODEL_URI);
  const editor = monaco.editor.create(container, {
    model,
    automaticLayout: true,
    fixedOverflowWidgets: true,
    fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace',
    fontSize: 13,
    hover: { enabled: true, delay: 150, sticky: true },
    lineHeight: 20,
    minimap: { enabled: false },
    padding: { top: 12, bottom: 12 },
    quickSuggestions: { other: true, comments: false, strings: false },
    renderWhitespace: showWhitespace ? 'all' : 'none',
    scrollBeyondLastLine: false,
    snippetSuggestions: 'top',
    suggestOnTriggerCharacters: true,
    tabSize: 2,
    theme: THEME_NAME,
    insertSpaces: true,
    wordBasedSuggestions: 'off',
    wordWrap: 'off',
    glyphMargin: true,
    lightbulb: { enabled: false },
    overviewRulerBorder: false,
    renderValidationDecorations: 'on',
    accessibilityPageSize: 20,
  });
  const inputArea = editor.getDomNode()?.querySelector('textarea.inputarea');
  if (inputArea) {
    inputArea.setAttribute('autocapitalize', 'off');
    inputArea.setAttribute('autocomplete', 'off');
    inputArea.setAttribute('autocorrect', 'off');
    inputArea.setAttribute('spellcheck', 'false');
  }

  let settingValue = false;
  const nativeValue = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value');
  Object.defineProperty(source, 'value', {
    configurable: true,
    get: () => model.getValue(),
    set: value => {
      const next = String(value ?? '');
      if (next === model.getValue()) return;
      settingValue = true;
      model.setValue(next);
      settingValue = false;
      nativeValue?.set?.call(source, next);
    },
  });

  const changeSubscription = model.onDidChangeContent(() => {
    nativeValue?.set?.call(source, model.getValue());
    if (!settingValue) source.dispatchEvent(new Event('input', { bubbles: true }));
  });
  const typeSubscription = editor.onDidType(text => {
    if (text === ' ' && panelIdContext(model, editor.getPosition())) {
      editor.trigger('panel-id-completion', 'editor.action.triggerSuggest', {});
    }
  });
  const insertConsecutiveSpace = event => {
    const selection = editor.getSelection();
    if (!selection?.isEmpty() || selection.startColumn <= 1) return false;
    const preceding = model.getValueInRange(new monaco.Range(
      selection.startLineNumber,
      selection.startColumn - 1,
      selection.startLineNumber,
      selection.startColumn
    ));
    if (preceding !== ' ') return false;
    event.preventDefault();
    event.stopImmediatePropagation();
    const nextColumn = selection.startColumn + 1;
    editor.executeEdits('preserve-consecutive-space', [{ range: selection, text: ' ', forceMoveMarkers: true }], [
      new monaco.Selection(selection.startLineNumber, nextColumn, selection.startLineNumber, nextColumn),
    ]);
    return true;
  };
  const preserveSpaceKey = event => {
    if ((event.code !== 'Space' && event.key !== ' ') || event.ctrlKey || event.metaKey || event.altKey || event.isComposing) return;
    insertConsecutiveSpace(event);
  };
  const preserveSpaceReplacement = event => {
    if (event.inputType !== 'insertReplacementText' || !/^\.\s?$/.test(event.data || '')) return;
    insertConsecutiveSpace(event);
  };
  inputArea?.addEventListener('keydown', preserveSpaceKey, true);
  inputArea?.addEventListener('beforeinput', preserveSpaceReplacement, true);
  let languageDiagnosticSignature = '';
  const markerSubscription = monaco.editor.onDidChangeMarkers(resources => {
    if (!resources.some(resource => resource.toString() === model.uri.toString())) return;
    const diagnostics = monaco.editor.getModelMarkers({ resource: model.uri })
      .filter(marker => marker.owner !== MARKER_OWNER)
      .map(marker => ({
        severity: marker.severity >= monaco.MarkerSeverity.Error ? 'error' : 'warning',
        ruleId: 'monaco-yaml',
        path: '',
        message: marker.message,
        line: marker.startLineNumber,
        col: marker.startColumn,
      }));
    const signature = JSON.stringify(diagnostics);
    if (signature === languageDiagnosticSignature) return;
    languageDiagnosticSignature = signature;
    source.dispatchEvent(new CustomEvent('yaml-language-diagnostics', { detail: diagnostics }));
  });

  return {
    focus() {
      editor.focus();
    },
    layout() {
      editor.layout();
    },
    setShowWhitespace(visible) {
      editor.updateOptions({ renderWhitespace: visible ? 'all' : 'none' });
    },
    setDiagnostics(diagnostics = []) {
      const lineCount = model.getLineCount();
      monaco.editor.setModelMarkers(model, MARKER_OWNER, diagnostics
        .filter(item => item.ruleId !== 'yaml-syntax' && item.ruleId !== 'ajv-schema')
        .filter(item => Number.isInteger(item.line))
        .map(item => {
          const startLineNumber = Math.max(1, Math.min(lineCount, item.line));
          const maxColumn = model.getLineMaxColumn(startLineNumber);
          const startColumn = Math.max(1, Math.min(maxColumn, Number(item.col) || 1));
          return {
            severity: markerSeverity(item.severity),
            message: `${item.path ? `${item.path}: ` : ''}${item.message}`,
            source: item.ruleId || 'info.yaml',
            startLineNumber,
            startColumn,
            endLineNumber: startLineNumber,
            endColumn: maxColumn,
          };
        }));
    },
    reveal(line, col = 1, selectLine = false) {
      const lineNumber = Math.max(1, Math.min(model.getLineCount(), Number(line) || 1));
      const maxColumn = model.getLineMaxColumn(lineNumber);
      const column = Math.max(1, Math.min(maxColumn, Number(col) || 1));
      editor.setSelection(selectLine
        ? { startLineNumber: lineNumber, startColumn: 1, endLineNumber: lineNumber, endColumn: maxColumn }
        : { startLineNumber: lineNumber, startColumn: column, endLineNumber: lineNumber, endColumn: maxColumn });
      editor.revealLineInCenter(lineNumber);
      editor.focus();
    },
    dispose() {
      changeSubscription.dispose();
      typeSubscription.dispose();
      inputArea?.removeEventListener('keydown', preserveSpaceKey, true);
      inputArea?.removeEventListener('beforeinput', preserveSpaceReplacement, true);
      markerSubscription.dispose();
      monaco.editor.setModelMarkers(model, MARKER_OWNER, []);
      editor.dispose();
      model.dispose();
      yamlService.dispose();
    },
  };
}
