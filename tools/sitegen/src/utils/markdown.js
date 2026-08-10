import { marked, Marked } from 'marked';
import sanitizeHtml from 'sanitize-html';
import { slugify } from './strings.js';

const BLOCK_TAGS = [
  'p', 'br', 'hr', 'blockquote', 'pre', 'code', 'div',
  'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
  'ul', 'ol', 'li', 'dl', 'dt', 'dd',
  'strong', 'em', 'del', 's', 'a', 'img',
  'table', 'thead', 'tbody', 'tfoot', 'tr', 'th', 'td',
  'details', 'summary', 'kbd', 'sup', 'sub',
];

const INLINE_TAGS = ['br', 'code', 'strong', 'em', 'del', 's', 'a', 'kbd', 'sup', 'sub'];

const HEADING_TAGS = ['h1', 'h2', 'h3', 'h4', 'h5', 'h6'];

// Slug-shaped ids only, so an authored `id` can't clobber the DOM (e.g. shadow
// a global via `window.<id>`) or carry a payload.
const SAFE_ID = /^[a-z0-9][a-z0-9-]{0,128}$/;

// Presentational-only style declarations. Whitelisting both the property and a
// value shape keeps `url(...)`, `expression(...)`, `position`, etc. out.
const DIMENSION = /^\d+(?:\.\d+)?(?:px|%|em|rem|vw|vh)$/;
const ALLOWED_STYLES = {
  '*': {
    'text-align': [/^(?:left|right|center|justify)$/],
    'float': [/^(?:left|right|none)$/],
    'width': [DIMENSION],
    'max-width': [DIMENSION],
    'height': [DIMENSION],
    'max-height': [DIMENSION],
    'margin': [/^(?:auto|\d+(?:\.\d+)?(?:px|%|em|rem)?)(?:\s+(?:auto|\d+(?:\.\d+)?(?:px|%|em|rem)?)){0,3}$/],
    'margin-left': [DIMENSION, /^auto$/],
    'margin-right': [DIMENSION, /^auto$/],
    'margin-top': [DIMENSION],
    'margin-bottom': [DIMENSION],
  },
};

function dropUnsafeId(tagName, attribs) {
  if (attribs.id && !SAFE_ID.test(attribs.id)) {
    const { id, ...rest } = attribs;
    return { tagName, attribs: rest };
  }
  return { tagName, attribs };
}

function transformAnchor(tagName, attribs) {
  const href = String(attribs.href || '').trim();
  const external = /^https?:\/\//i.test(href);
  const next = {
    ...attribs,
    ...(external ? { target: '_blank', rel: 'noopener noreferrer' } : {}),
  };
  return dropUnsafeId(tagName, next);
}

function sanitizeOptions(inline = false) {
  const transformTags = { a: transformAnchor };
  if (!inline) for (const tag of HEADING_TAGS) transformTags[tag] = dropUnsafeId;
  return {
    allowedTags: inline ? INLINE_TAGS : BLOCK_TAGS,
    allowedSchemes: ['http', 'https', 'mailto'],
    allowedSchemesByTag: { img: ['http', 'https'] },
    allowProtocolRelative: false,
    disallowedTagsMode: 'discard',
    transformTags,
    allowedStyles: ALLOWED_STYLES,
    allowedAttributes: {
      '*': ['align', 'style'],
      a: ['href', 'title', 'target', 'rel', 'id'],
      img: ['src', 'alt', 'title', 'width', 'height'],
      h1: ['id'], h2: ['id'], h3: ['id'], h4: ['id'], h5: ['id'], h6: ['id'],
      th: ['align'],
      td: ['align'],
    },
  };
}

export function sanitizeAuthoredHtml(html, { inline = false } = {}) {
  return sanitizeHtml(String(html || ''), sanitizeOptions(inline));
}

// A marked instance whose headings carry GitHub-style slug ids so in-document
// `#anchor` links resolve. A fresh `seen` map per render keeps duplicate
// headings unique without leaking counts between documents.
function markedWithHeadingIds() {
  const seen = new Map();
  const instance = new Marked();
  instance.use({
    renderer: {
      heading(text, level) {
        const base = slugify(String(text).replace(/<[^>]*>/g, ''));
        let id = '';
        if (base) {
          const count = seen.get(base) || 0;
          seen.set(base, count + 1);
          id = count ? `${base}-${count}` : base;
        }
        return `<h${level}${id ? ` id="${id}"` : ''}>${text}</h${level}>\n`;
      },
    },
  });
  return instance;
}

export function renderMarkdownBlock(markdown) {
  const source = String(markdown ?? '').trim();
  return source ? sanitizeAuthoredHtml(markedWithHeadingIds().parse(source)) : '';
}

export function renderMarkdownInline(markdown) {
  const source = String(markdown ?? '').trim();
  return source ? sanitizeAuthoredHtml(marked.parseInline(source), { inline: true }) : '';
}
