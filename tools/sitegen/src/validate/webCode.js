// Detect minified or compiled code in the parts of a card the site publishes.
//
// A card's web editor (web/ or dist/) and its custom panels (panels/) are
// copied to the production site, where their scripts run on the site's own
// origin. They may merge unreviewed only while a maintainer could read them,
// so minified, obfuscated or compiled code needs a review. Only code is
// checked: dense markup and stylesheets run nothing and stay readable enough.
//
// Calibrated on the cards merged so far: hand-written editors keep at most
// about a third of their script on very long lines (dense one-liners, inline
// SVG path data), while any minifier, even whitespace-only, puts all of it
// there.

const PUBLISHED = /^releases\/[^/]+\/(?:web|dist|panels)\//;
const SCRIPT_FILE = /\.(?:m?js|cjs)$/i;
const MARKUP_FILE = /\.(?:html?|svg)$/i;
const COMPILED_FILE = /\.wasm$/i;

const LONG_LINE = 500;
const MIN_CHECKED_CHARS = 1000;
const LONG_SHARE = 0.6;
const SHORT_NAME_SHARE = 0.45;

const KEYWORDS = new Set(('break case catch class const continue debugger default delete do else export extends false '
  + 'finally for function if import in instanceof let new null return super switch this throw true try typeof var '
  + 'void while with yield await async of static get set undefined').split(' '));

/** True for files the site build publishes from a card. */
export function isPublishedWebFile(file) {
  return PUBLISHED.test(file);
}

const inlineScripts = markup => [...markup.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)].map(match => match[1]).join('\n');
const visibleChars = text => text.replace(/\s/g, '').length;

/** Share of non-whitespace characters that sit on lines over LONG_LINE characters. */
function longLineShare(text) {
  const total = visibleChars(text);
  if (total < MIN_CHECKED_CHARS) return 0;
  return visibleChars(text.split('\n').filter(line => line.length > LONG_LINE).join('')) / total;
}

/** Share of local-looking identifiers on long lines that are one or two characters, as minifiers rename them. */
function shortNameShare(code) {
  const long = code.split('\n').filter(line => line.length > LONG_LINE).join('\n');
  if (long.length < MIN_CHECKED_CHARS) return 0;
  const names = [...long.matchAll(/(?<![.\w$])[A-Za-z_$][\w$]*/g)].map(match => match[0]).filter(name => !KEYWORDS.has(name));
  return names.length ? names.filter(name => name.length <= 2).length / names.length : 0;
}

const count = (code, pattern) => (code.match(pattern) || []).length;

/**
 * Signs of obfuscation, calibrated like the minification checks: on the
 * published cards there are none, while every javascript-obfuscator setting
 * and each hand-rolled trick (eval/Function loaders, JSFuck, character-code or
 * escape-encoded strings) shows at least one.
 */
function obfuscationReason(code) {
  if (/(?<![\w$.])eval\s*\(|\bnew\s+Function\s*\(|(?<![\w$.])Function\s*\(/.test(code)) return 'dynamically evaluated code';
  if (/[[\]()!+]{48,}/.test(code)) return 'JSFuck-encoded script';
  if (/fromCharCode\s*\(\s*(?:\d+\s*,\s*){15,}/.test(code)) return 'character-code-encoded strings';
  const escapes = count(code, /\\x[0-9a-fA-F]{2}|\\u[0-9a-fA-F]{4}/g);
  if (escapes >= 40 || (escapes >= 8 && (escapes * 5) / code.length >= 0.1)) return 'escape-encoded strings';
  if (/\b_0x[0-9a-f]{4,}\b/i.test(code)) return 'obfuscated script';
  // javascript-obfuscator rewrites every `.name` as `['name']` and every number
  // as hex, whatever its other settings; readable code is the other way round.
  const names = count(code, /(?<![.\w$'"])[A-Za-z_$][\w$]*/g);
  if (names >= 200) {
    const hex = count(code, /(?<![\w.$])0x[0-9a-fA-F]+\b/g);
    const decimal = count(code, /(?<![\w.$])\d+(?:\.\d+)?\b/g);
    const bracketed = count(code, /\[\s*(['"])[A-Za-z_$][\w$]*\1\s*\]/g);
    const dotted = count(code, /\.[A-Za-z_$][\w$]*/g);
    if ((hex >= 50 && hex > decimal) || (bracketed >= 20 && bracketed > dotted)) return 'obfuscated script';
  }
  return '';
}

/**
 * Share of declared names that are one or two characters, which catches
 * minifiers that rename variables but keep the layout. Published cards reach
 * 0.44 (loop counters and short handler arguments); renamed code 0.57 and up.
 */
function shortDeclarationShare(code) {
  const declared = [...code.matchAll(/\b(?:let|const|var|function)\s+([A-Za-z_$][\w$]*)/g)].map(match => match[1]);
  return declared.length >= 100 ? declared.filter(name => name.length <= 2).length / declared.length : 0;
}

function scriptReason(code) {
  const obfuscated = obfuscationReason(code);
  if (obfuscated) return obfuscated;
  if (/[#@]\s*sourceMappingURL=/.test(code)) return 'built script with a source map';
  if (longLineShare(code) >= LONG_SHARE || shortNameShare(code) >= SHORT_NAME_SHARE
    || shortDeclarationShare(code) >= 0.52) return 'minified script';
  return '';
}

/** Why a published file needs review as minified or compiled, or '' when it is readable. */
export function minificationReason(file, contents) {
  if (COMPILED_FILE.test(file)) return 'compiled WebAssembly';
  if (/\.min\.[a-z]+$/i.test(file)) return 'minified file';
  const text = Buffer.isBuffer(contents) ? contents.toString('utf8') : String(contents ?? '');
  if (SCRIPT_FILE.test(file)) return scriptReason(text);
  if (MARKUP_FILE.test(file)) return scriptReason(inlineScripts(text));
  return '';
}

/**
 * Published files a change set adds or modifies that are minified or compiled,
 * as [{ path, reason }]. `read(path)` returns a proposed file's contents, or
 * null when it cannot be read.
 */
export function minifiedWebFiles(changes, read) {
  const found = [];
  for (const change of changes) {
    if (change.status.startsWith('D') || !isPublishedWebFile(change.path)) continue;
    const contents = COMPILED_FILE.test(change.path) ? '' : read(change.path);
    if (contents === null) continue;
    const reason = minificationReason(change.path, contents);
    if (reason) found.push({ path: change.path, reason });
  }
  return found;
}
