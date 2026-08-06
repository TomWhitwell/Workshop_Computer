#!/usr/bin/env node

import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promises as fs } from 'node:fs';
import { chromium } from '../../../../tools/sitegen/node_modules/playwright/index.mjs';
import { renderPanelExportPage } from '../../../../tools/sitegen/src/panels/exportPage.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../../../..');
const ASSETS = new Map([
  ['/assets/style.css', path.join(ROOT, 'tools/sitegen/assets/style.css')],
  ['/assets/program-cards.css', path.join(ROOT, 'tools/sitegen/assets/program-cards.css')],
  ['/assets/panel.svg', path.join(ROOT, 'tools/sitegen/assets/program_cards/Standalone_computer_rev1.svg')],
  ['/assets/panel-export.js', path.join(ROOT, 'tools/sitegen/assets/js/panel-export.js')],
  ['/assets/fonts/inter-latin-800-normal.woff2', path.join(ROOT, 'tools/sitegen/node_modules/@fontsource/inter/files/inter-latin-800-normal.woff2')],
]);

function contentType(file) {
  if (file.endsWith('.css')) return 'text/css; charset=utf-8';
  if (file.endsWith('.js')) return 'text/javascript; charset=utf-8';
  if (file.endsWith('.svg')) return 'image/svg+xml';
  if (file.endsWith('.woff2')) return 'font/woff2';
  return 'text/html; charset=utf-8';
}

function control(label) {
  return label ? { label: String(label) } : undefined;
}

function mapped(items, keys) {
  const result = {};
  for (const key of keys) {
    const value = control(items?.[key]);
    if (value) result[key] = value;
  }
  return result;
}

function snapshot(source) {
  return {
    id: 'standalone',
    name: source.title || 'Panel',
    panel: {
      controls: mapped(source.controls, ['main', 'x', 'y']),
      inputs: mapped(source.inputs, ['audio_l', 'audio_r', 'cv_1', 'cv_2', 'pulse_1', 'pulse_2']),
      outputs: mapped(source.outputs, ['audio_out_l', 'audio_out_r', 'cv_out_1', 'cv_out_2', 'pulse_out_1', 'pulse_out_2']),
    },
    switch_modes: {
      up: source.switch?.up || '',
      middle: source.switch?.middle || '',
      down: source.switch?.down || '',
      tap: source.switch?.tap || '',
    },
  };
}

async function serve(html) {
  const server = http.createServer(async (request, response) => {
    try {
      const pathname = new URL(request.url, 'http://localhost').pathname;
      if (pathname === '/') {
        response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        response.end(html);
        return;
      }
      const file = ASSETS.get(pathname);
      if (!file) { response.writeHead(404); response.end('Not found'); return; }
      response.writeHead(200, { 'Content-Type': contentType(file) });
      response.end(await fs.readFile(file));
    } catch (error) {
      response.writeHead(500);
      response.end(error.message);
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return { server, url: `http://127.0.0.1:${server.address().port}/` };
}

async function main() {
  const [input, output] = process.argv.slice(2);
  if (!input || !output) throw new Error('Usage: render-standalone-panel.mjs <design.json> <output.svg>');
  const design = JSON.parse(await fs.readFile(path.resolve(input), 'utf8'));
  const html = renderPanelExportPage([snapshot(design)]);
  const { server, url } = await serve(html);
  let browser;
  try {
    browser = await chromium.launch({ headless: true });
    const page = await browser.newPage({ viewport: { width: 800, height: 1200 }, deviceScaleFactor: 1 });
    await page.goto(url, { waitUntil: 'load' });
    await page.waitForFunction(() => window.panelExporterReady === true);
    const svg = await page.evaluate(() => window.exportPanelSvg('standalone'));
    const outputPath = path.resolve(output);
    await fs.writeFile(outputPath, svg, 'utf8');
    const previewPath = `${outputPath}.preview.html`;
    const imageName = path.basename(outputPath).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    await fs.writeFile(previewPath, `<!doctype html><meta charset="utf-8"><title>${imageName} preview</title><style>html,body{margin:0;min-height:100%;background:#fdfdfd}body{display:grid;place-items:start center;gap:12px;padding:20px;box-sizing:border-box}img{display:block;width:280px;height:auto}a{background:#111;color:#fff;font:700 13px/1 Arial,sans-serif;padding:10px 14px;text-decoration:none;text-transform:uppercase}</style><img src="${imageName}" alt="Standalone Workshop Computer panel preview"><a href="${imageName}" download="${imageName}">Download SVG</a>`, 'utf8');
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
  console.log(path.resolve(output));
  console.log(`${path.resolve(output)}.preview.html`);
}

main().catch(error => { console.error(error.message); process.exit(1); });
