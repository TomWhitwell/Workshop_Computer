import { test } from 'node:test';
import assert from 'node:assert/strict';
import { OG_IMAGE_HEIGHT, OG_IMAGE_WIDTH } from '../src/render/socialMeta.js';
import {
  formatCardNumber,
  ogImageOptionsForCard,
  renderOgPng,
  renderOgSvg,
  wrapText,
} from '../src/render/ogImage.js';

function pngSize(buffer) {
  assert.equal(buffer.subarray(12, 16).toString(), 'IHDR');
  return { width: buffer.readUInt32BE(16), height: buffer.readUInt32BE(20) };
}

test('og SVG escapes title, creator, and description text', () => {
  const svg = renderOgSvg({
    number: '42',
    title: 'Test & "Card"',
    creator: 'A & B',
    description: 'Uses <cv> & clocks',
  });
  assert.match(svg, /Test &amp; &quot;Card&quot;/);
  assert.match(svg, /By A &amp; B/);
  assert.match(svg, /Uses &lt;cv&gt; &amp; clocks/);
  assert.doesNotMatch(svg, /Test & "Card"/);
  assert.match(svg, /transform="rotate\(-90 /);
  assert.match(svg, /Workshop Computer/);
  assert.match(svg, /font-family="Inter"/);
  assert.match(svg, /<rect width="1200" height="630" fill="#111"/);
  assert.match(svg, /<circle cx="[\d.]+" cy="[\d.]+" r="[\d.]+" fill="#111"/);
  assert.doesNotMatch(svg, /<circle[^>]*fill="#fdfdfd"/);
  assert.match(svg, /font-size="88" font-weight="650" fill="#e3d69e"/);
  assert.match(svg, /font-size="32" font-weight="650" fill="#e3d69e"/);
  assert.match(svg, /font-size="30" font-weight="500" fill="#fff"/);
  assert.match(svg, /y="278\.[\d]+" text-anchor="middle" transform="rotate\(-90 [\d.]+ 278\.[\d]+\)"[^>]+font-size="40" font-weight="650" fill="#e3d69e">Workshop Computer/);
  assert.match(svg, /width="1200"/);
});

test('og text wrapping truncates overflowing lines', () => {
  assert.deepEqual(wrapText('alpha beta gamma delta', 10, 2), ['alpha beta', 'gamma del\u2026']);
  assert.deepEqual(wrapText('', 24, 3), []);
  assert.equal(formatCardNumber({ id: '42_test', release: '42 / 1.0' }), '42');
});

test('og short descriptions allow five lines before truncation', () => {
  const svg = renderOgSvg({
    description: 'First line has enough words here Second line has enough words here Third line has enough words here Fourth line has enough words here Fifth line is visible Sixth line is truncated',
  });
  assert.match(svg, /Fourth line has enough words here/);
  assert.match(svg, /Fifth line is visible Sixth line is t\u2026/);
  assert.doesNotMatch(svg, />Sixth line/);
});

test('og centers the author and maximum description block below the title', () => {
  const svg = renderOgSvg({ creator: 'Test Author', description: 'One two three four five' });
  const authorY = Number(svg.match(/<text x="[^"]+" y="([^"]+)"[^>]+font-size="32"/)?.[1]);
  const descriptionY = Number(svg.match(/<text x="[^"]+" y="([^"]+)"[^>]+font-size="30"/)?.[1]);
  assert.ok(authorY > 270 && authorY < 275);
  assert.equal(descriptionY, authorY + 44);
});

test('og PNG is 1200 by 630 and includes rendered title text', async () => {
  const png = await renderOgPng(ogImageOptionsForCard({
    id: '42_test',
    title: 'Test & "Card"',
    short_description: 'A short description that should appear on the share image.',
    metadata: { creator: 'A & B' },
  }));
  assert.deepEqual(pngSize(png), { width: OG_IMAGE_WIDTH, height: OG_IMAGE_HEIGHT });
  assert.ok(png.length > 20000, 'share image should include rasterized text, not just the card mark');
});
