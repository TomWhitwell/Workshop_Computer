export const FLASH_2MB = '2mb';
export const FLASH_16MB = '16mb';

/** Normalize an authored flash size, or null when absent/unknown. */
export function normalizeFlash(value) {
  if (value == null || value === '') return null;
  const text = String(value).trim().toLowerCase();
  if (text === '2mb' || text === '2m') return FLASH_2MB;
  if (text === '16mb' || text === '16m') return FLASH_16MB;
  return null;
}

/**
 * Infer 2mb/16mb from a firmware path or filename. 16MB tokens win so
 * names like goldfish.2.0.16mb.uf2 are not parsed as 2MB.
 */
export function inferFlashFromFilename(name) {
  const text = String(name || '');
  if (/(?:^|[^0-9])16m(?:b)?(?![a-z0-9])/i.test(text)) return FLASH_16MB;
  if (/(?:^|[^0-9])2m(?:b)?(?![a-z0-9])/i.test(text)) return FLASH_2MB;
  return null;
}

/** Resolve flash for a download: explicit authored value, else filename, else 2MB. */
export function resolveDownloadFlash(item = {}, authoredFlash) {
  const explicit = normalizeFlash(authoredFlash);
  if (explicit) return explicit;
  const source = [item.rel, item.path, item.name, item.url].filter(Boolean).join('/');
  return inferFlashFromFilename(source) || FLASH_2MB;
}

/**
 * Attach flash onto a download item. Implicit 2MB is omitted so ordinary
 * firmware stays unmarked; explicit 2mb is kept to honour an override.
 */
export function assignDownloadFlash(item, authoredFlash) {
  if (!item || typeof item !== 'object') return item;
  const authored = normalizeFlash(authoredFlash);
  const flash = resolveDownloadFlash(item, authoredFlash);
  if (flash === FLASH_16MB) item.flash = FLASH_16MB;
  else if (authored === FLASH_2MB) item.flash = FLASH_2MB;
  else delete item.flash;
  return item;
}

/** Space-separated sizes for data-flash: "2mb", "16mb", or "2mb 16mb". */
export function flashAttrFromDownloads(downloads) {
  const sizes = new Set();
  for (const item of Array.isArray(downloads) ? downloads : []) {
    sizes.add(item && item.flash === FLASH_16MB ? FLASH_16MB : FLASH_2MB);
  }
  if (!sizes.size) sizes.add(FLASH_2MB);
  return [FLASH_2MB, FLASH_16MB].filter(size => sizes.has(size)).join(' ');
}

export function has16mbFirmware(downloads) {
  return (Array.isArray(downloads) ? downloads : []).some(item => item && item.flash === FLASH_16MB);
}

/** Card-level memory badge only when every UF2 is 16MB. */
export function deriveMemoryFromDownloads(downloads) {
  const list = Array.isArray(downloads) ? downloads.filter(Boolean) : [];
  if (!list.length) return undefined;
  if (list.every(item => item.flash === FLASH_16MB)) {
    return { size: FLASH_16MB, requirement: 'only' };
  }
  return undefined;
}
