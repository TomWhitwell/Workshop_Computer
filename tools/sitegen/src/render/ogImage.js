import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Resvg } from '@resvg/resvg-js';
import wawoff2 from 'wawoff2';
import { writeFileEnsured } from '../utils/fs.js';
import {
  DEFAULT_OG_DESCRIPTION,
  OG_IMAGE_HEIGHT,
  OG_IMAGE_WIDTH,
} from './socialMeta.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const FONT_SOURCE_DIR = path.join(here, '../../node_modules/@fontsource-variable/inter/files');
const FONT_CACHE_DIR = path.join(here, '../../node_modules/.cache/og-fonts');
const FONT_SOURCES = [
  'inter-latin-wght-normal.woff2',
  'inter-latin-ext-wght-normal.woff2',
  'inter-cyrillic-wght-normal.woff2',
];

const MARK_WIDTH = 306;
const MARK_HEIGHT = 178;
// At the right edge, the green card spans y=13..143 after the source SVG's
// vertical flip. Centre the vertical wordmark within that visible area.
const MARK_RIGHT_GREEN_CENTER_Y = (13 + 143) / 2;
// The shorter lower edge on the right begins at y=156 in the flipped artwork.
const MARK_RIGHT_GREEN_BOTTOM_Y = 156;
const DESCRIPTION_MAX_LINES = 5;
const DESCRIPTION_LINE_HEIGHT = 40;
const BYLINE_LINE_HEIGHT = 44;
const FONT_FAMILY = 'Inter';
const CARD_LABEL = '#e3d69e';
const CARD_GREEN = '#27743a';
const GROUND = '#111';
const BRAND = 'Workshop Computer';
const markSource = readFileSync(new URL('../../assets/program_cards/88-blank.svg', import.meta.url), 'utf8');
const markInner = markSource
  .replace(/<\/?svg\b[^>]*>/gi, '')
  .replace(/<title\b[^>]*>[\s\S]*?<\/title>/gi, '')
  .replace(/<circle\b[^>]*\/>/gi, '')
  .trim();
// 88-blank.svg flips its artwork (origin at the bottom). Map flipped circle
// coordinates back into the unflipped viewBox used for labels.
const HOLE_X = 39;
const HOLE_Y = MARK_HEIGHT - 138;

let preparedFontFiles;

function escapeXml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;');
}

function truncateLine(text, maxChars) {
  const value = String(text || '').trim();
  if (value.length <= maxChars) return value;
  return value.slice(0, Math.max(0, maxChars - 1)).trimEnd() + '\u2026';
}

/** Greedy wrap for SVG text, which has no automatic line breaking. */
export function wrapText(text, maxChars, maxLines) {
  const cleaned = String(text || '').replace(/\s+/g, ' ').trim();
  if (!cleaned || maxChars < 1 || maxLines < 1) return [];
  const words = cleaned.split(' ');
  const lines = [];
  let current = '';
  for (let i = 0; i < words.length; i++) {
    const word = words[i];
    const candidate = current ? `${current} ${word}` : word;
    if (candidate.length > maxChars && current) {
      lines.push(current);
      current = word;
      if (lines.length === maxLines - 1) {
        lines.push(truncateLine([current, ...words.slice(i + 1)].join(' '), maxChars));
        return lines;
      }
    } else if (word.length > maxChars && !current) {
      lines.push(truncateLine(word, maxChars));
      if (lines.length === maxLines) return lines;
    } else {
      current = candidate;
    }
  }
  if (current) lines.push(current);
  return lines.slice(0, maxLines);
}

export function formatCardNumber(card) {
  const raw = String(card?.release || card?.id || '').split('/')[0].split('_')[0].trim();
  const number = Number.parseInt(raw, 10);
  return Number.isNaN(number) ? raw : String(number).padStart(2, '0');
}

export function ogImageOptionsForSite() {
  return {
    title: 'Program Cards',
    description: DEFAULT_OG_DESCRIPTION,
    sitewide: true,
  };
}

export function ogImageOptionsForCard(card) {
  return {
    number: formatCardNumber(card),
    title: card?.title || card?.id || 'Untitled card',
    creator: card?.metadata?.creator || '',
    description: card?.short_description || '',
  };
}

function textLinesMarkup(lines, { x, y, fontSize, fontWeight, fill, lineHeight }) {
  if (!lines.length) return '';
  const tspans = lines.map((line, index) => (
    `<tspan x="${x}" dy="${index === 0 ? 0 : lineHeight}">${escapeXml(line)}</tspan>`
  )).join('');
  return `<text x="${x}" y="${y}" font-family="${FONT_FAMILY}" font-size="${fontSize}" font-weight="${fontWeight}" fill="${fill}">${tspans}</text>`;
}

export function renderOgSvg({
  number = '',
  title = 'Program Cards',
  creator = '',
  description = '',
  sitewide = false,
} = {}) {
  const pad = 20;
  const scale = Math.min((OG_IMAGE_WIDTH - pad * 2) / MARK_WIDTH, (OG_IMAGE_HEIGHT - pad * 2) / MARK_HEIGHT);
  const cardX = (OG_IMAGE_WIDTH - MARK_WIDTH * scale) / 2;
  const cardY = (OG_IMAGE_HEIGHT - MARK_HEIGHT * scale) / 2;
  const holeX = cardX + HOLE_X * scale;
  const holeY = cardY + HOLE_Y * scale;
  const holeR = 27 * scale;
  const textX = holeX + holeR + 24;
  const titleLines = wrapText(title, 14, 2);
  const caption = description || (sitewide ? DEFAULT_OG_DESCRIPTION : '');
  const descriptionLines = wrapText(caption, 38, DESCRIPTION_MAX_LINES);
  const byline = creator ? `By ${creator}` : '';
  const numberSize = String(number).length > 2 ? 144 : 192;
  const numberX = cardX + 56 * scale;
  const numberY = cardY + (MARK_HEIGHT - 52) * scale;
  const titleSize = 88;
  const titleLineHeight = 98;

  const titleY = holeY + 22;
  const numberOnCard = number
    ? `<text x="${numberX}" y="${numberY}" text-anchor="middle" transform="rotate(-90 ${numberX} ${numberY})" font-family="${FONT_FAMILY}" font-size="${numberSize}" font-weight="650" fill="${CARD_LABEL}">${escapeXml(number)}</text>`
    : '';
  const titleOnCard = textLinesMarkup(titleLines, {
    x: textX, y: titleY, fontSize: titleSize, fontWeight: 650, fill: CARD_LABEL, lineHeight: titleLineHeight,
  });
  const titleBottom = titleY + (Math.max(titleLines.length, 1) - 1) * titleLineHeight + 16;
  const contentBottom = cardY + MARK_RIGHT_GREEN_BOTTOM_Y * scale;
  const contentHeight = (byline ? BYLINE_LINE_HEIGHT : 0) + DESCRIPTION_MAX_LINES * DESCRIPTION_LINE_HEIGHT;
  const contentTop = titleBottom + Math.max(0, contentBottom - titleBottom - contentHeight) / 2;
  let cursor = contentTop + (byline ? 32 : 30);
  const bylineMarkup = byline
    ? textLinesMarkup([truncateLine(byline, 38)], {
      x: textX, y: cursor, fontSize: 32, fontWeight: 650, fill: CARD_LABEL, lineHeight: 40,
    })
    : '';
  if (byline) cursor += BYLINE_LINE_HEIGHT;
  const descriptionMarkup = textLinesMarkup(descriptionLines, {
    x: textX, y: cursor, fontSize: 30, fontWeight: 500, fill: '#fff', lineHeight: DESCRIPTION_LINE_HEIGHT,
  });
  const brandX = cardX + (MARK_WIDTH - 10) * scale;
  const brandY = cardY + MARK_RIGHT_GREEN_CENTER_Y * scale;
  const brandMarkup = `<text x="${brandX}" y="${brandY}" text-anchor="middle" transform="rotate(-90 ${brandX} ${brandY})" font-family="${FONT_FAMILY}" font-size="40" font-weight="650" fill="${CARD_LABEL}">${escapeXml(BRAND)}</text>`;

  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${OG_IMAGE_WIDTH}" height="${OG_IMAGE_HEIGHT}" viewBox="0 0 ${OG_IMAGE_WIDTH} ${OG_IMAGE_HEIGHT}">
  <rect width="${OG_IMAGE_WIDTH}" height="${OG_IMAGE_HEIGHT}" fill="${GROUND}"/>
  <g transform="translate(${cardX} ${cardY}) scale(${scale})">${markInner}</g>
  <circle cx="${holeX}" cy="${holeY}" r="${holeR}" fill="${GROUND}"/>
  ${numberOnCard}
  ${titleOnCard}
  ${bylineMarkup}
  ${descriptionMarkup}
  ${brandMarkup}
</svg>`;
}

async function ogFontFiles() {
  if (preparedFontFiles) return preparedFontFiles;
  mkdirSync(FONT_CACHE_DIR, { recursive: true });
  const files = [];
  for (const name of FONT_SOURCES) {
    const dest = path.join(FONT_CACHE_DIR, name.replace(/\.woff2$/, '.ttf'));
    if (!existsSync(dest)) {
      const ttf = Buffer.from(await wawoff2.decompress(readFileSync(path.join(FONT_SOURCE_DIR, name))));
      writeFileSync(dest, ttf);
    }
    files.push(dest);
  }
  preparedFontFiles = files;
  return files;
}

export async function renderOgPng(options) {
  const svg = renderOgSvg(options);
  const resvg = new Resvg(svg, {
    fitTo: { mode: 'original' },
    font: {
      fontFiles: await ogFontFiles(),
      loadSystemFonts: false,
      defaultFontFamily: FONT_FAMILY,
    },
  });
  return resvg.render().asPng();
}

export async function writeOgPng(filePath, options) {
  await writeFileEnsured(filePath, await renderOgPng(options));
}
