// Browser-side Workshop Computer panel SVG exporter.
// Shared by the Author page and headless CLI so generated custom panels retain
// the vector base artwork, deterministic embedded font, and identical labels.

export const PANEL_EXPORT_WIDTH = 560;
export const PANEL_EXPORT_HEIGHT = 1785;
export const PANEL_DISPLAY_WIDTH = 280;
export const PANEL_DISPLAY_HEIGHT = PANEL_EXPORT_HEIGHT * PANEL_DISPLAY_WIDTH / PANEL_EXPORT_WIDTH;
const PANEL_FONT_URL = new URL('./fonts/inter-latin-800-normal.woff2', import.meta.url);

function escapeXml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;');
}

function bytesToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = '';
  for (let offset = 0; offset < bytes.length; offset += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  }
  return btoa(binary);
}

async function fetchRequired(url, description) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Could not load ${description} (HTTP ${response.status}).`);
  return response;
}

function wrapMeasuredText(context, text, maxWidth) {
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

function labelSvg(panelRect, element, measure, scaleX, scaleY) {
  const rect = element.getBoundingClientRect();
  const textElement = element.matches('.program-card-panel-switch-position') ? element.querySelector('strong') : element;
  if (!textElement) return '';
  const style = getComputedStyle(element);
  const textStyle = getComputedStyle(textElement);
  const paddingTop = Number.parseFloat(style.paddingTop) || 0;
  const paddingBottom = Number.parseFloat(style.paddingBottom) || 0;
  const paddingLeft = Number.parseFloat(style.paddingLeft) || 0;
  const paddingRight = Number.parseFloat(style.paddingRight) || 0;
  const maxWidth = Math.max(1, rect.width - paddingLeft - paddingRight);
  measure.font = `${textStyle.fontWeight} ${textStyle.fontSize} ${textStyle.fontFamily}`;
  const rawText = textElement.innerText || textElement.textContent || '';
  const text = textStyle.textTransform === 'uppercase' ? rawText.toUpperCase() : rawText;
  const lines = wrapMeasuredText(measure, text, maxWidth);
  if (!lines.length) return '';

  const lineHeight = Number.parseFloat(textStyle.lineHeight) || Number.parseFloat(textStyle.fontSize) || 11;
  const contentHeight = lines.length * lineHeight + paddingTop + paddingBottom;
  const height = Math.max(rect.height, contentHeight);
  const x = (rect.left - panelRect.left) * scaleX;
  const y = (rect.top - panelRect.top - (height - rect.height) / 2) * scaleY;
  const width = rect.width * scaleX;
  const scaledHeight = height * scaleY;
  const centerX = x + width / 2;
  const totalTextHeight = lines.length * lineHeight;
  const firstBaseline = (rect.top - panelRect.top - (height - rect.height) / 2 + (height - totalTextHeight) / 2 + lineHeight * 0.82) * scaleY;
  const neutralSwitchPosition = element.matches('.program-card-panel-switch-position[aria-pressed="true"]');
  const backgroundColor = neutralSwitchPosition ? '#fdfdfd' : style.backgroundColor;
  const borderWidth = neutralSwitchPosition ? 0 : Number.parseFloat(style.borderTopWidth) || 0;
  const rectMarkup = backgroundColor === 'rgba(0, 0, 0, 0)'
    ? ''
    : `<rect x="${x}" y="${y}" width="${width}" height="${scaledHeight}" fill="${escapeXml(backgroundColor)}"${borderWidth && style.borderTopColor !== 'rgba(0, 0, 0, 0)' ? ` stroke="${escapeXml(style.borderTopColor)}" stroke-width="${borderWidth * scaleX}"` : ''}/>`;
  const fontSize = (Number.parseFloat(textStyle.fontSize) || 11) * scaleY;
  const tspans = lines.map((line, index) => `<tspan x="${centerX}" y="${firstBaseline + index * lineHeight * scaleY}">${escapeXml(line)}</tspan>`).join('');
  return `${rectMarkup}<text text-anchor="middle" font-family="Workshop Panel" font-size="${fontSize}" font-weight="800" fill="${escapeXml(textStyle.color)}">${tspans}</text>`;
}

function serializeBaseSvg(source) {
  const documentNode = new DOMParser().parseFromString(source, 'image/svg+xml');
  if (documentNode.querySelector('parsererror')) throw new Error('The panel artwork is not valid SVG.');
  const root = documentNode.documentElement;
  if (root.localName !== 'svg') throw new Error('The panel artwork root is not SVG.');
  const viewBox = root.getAttribute('viewBox');
  if (!viewBox) throw new Error('The panel artwork has no viewBox.');
  const serializer = new XMLSerializer();
  const contents = [...root.childNodes].map(node => serializer.serializeToString(node)).join('');
  return `<svg x="0" y="0" width="${PANEL_EXPORT_WIDTH}" height="${PANEL_EXPORT_HEIGHT}" viewBox="${escapeXml(viewBox)}" preserveAspectRatio="none">${contents}</svg>`;
}

export async function renderPanelElementToSvg(panel) {
  if (!(panel instanceof Element)) throw new Error('A rendered panel element is required.');
  if (document.fonts) {
    await document.fonts.load('800 11px "Workshop Panel"');
    await document.fonts.ready;
  }
  const panelRect = panel.getBoundingClientRect();
  if (!panelRect.width || !panelRect.height) throw new Error('The rendered panel has no dimensions.');
  const panelArtwork = panel.querySelector('img');
  if (!panelArtwork) throw new Error('The panel artwork is missing.');

  const [artwork, fontResponse] = await Promise.all([
    (await fetchRequired(panelArtwork.currentSrc || panelArtwork.src, 'panel artwork')).text(),
    fetchRequired(PANEL_FONT_URL, 'panel font'),
  ]);
  const fontBase64 = bytesToBase64(await fontResponse.arrayBuffer());
  const scaleX = PANEL_EXPORT_WIDTH / panelRect.width;
  const scaleY = PANEL_EXPORT_HEIGHT / panelRect.height;
  const measure = document.createElement('canvas').getContext('2d');
  if (!measure) throw new Error('The browser could not create a text measurement context.');
  const labels = [...panel.querySelectorAll('.program-card-panel__label,.program-card-panel-switch-position.has-value')]
    .map(element => labelSvg(panelRect, element, measure, scaleX, scaleY))
    .join('');

  return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="${PANEL_DISPLAY_WIDTH}" height="${PANEL_DISPLAY_HEIGHT}" viewBox="0 0 ${PANEL_EXPORT_WIDTH} ${PANEL_EXPORT_HEIGHT}"><style>@font-face{font-family:"Workshop Panel";font-style:normal;font-weight:800;src:url(data:font/woff2;base64,${fontBase64}) format("woff2")}text{font-family:"Workshop Panel",sans-serif}</style>${serializeBaseSvg(artwork)}<g>${labels}</g></svg>`;
}

export async function renderPanelElementToSvgBlob(panel) {
  return new Blob([await renderPanelElementToSvg(panel)], { type: 'image/svg+xml' });
}
