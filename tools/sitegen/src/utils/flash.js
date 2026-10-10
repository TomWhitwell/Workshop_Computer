export const FLASH_2MB = '2mb';
export const FLASH_16MB = '16mb';

/** Normalize an authored flash size, or null when absent/unknown. */
export function normalizeFlashSize(value) {
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
export function inferFlashSizeFromFilename(name) {
  const text = String(name || '');
  if (/(?:^|[^0-9])16m(?:b)?(?![a-z0-9])/i.test(text)) return FLASH_16MB;
  if (/(?:^|[^0-9])2m(?:b)?(?![a-z0-9])/i.test(text)) return FLASH_2MB;
  return null;
}

/** Resolve flash size for a download: explicit authored value, else filename, else 2MB. */
export function resolveDownloadFlashSize(item = {}, authoredFlash) {
  const explicit = normalizeFlashSize(authoredFlash);
  if (explicit) return explicit;
  const source = [item.rel, item.path, item.name, item.url].filter(Boolean).join('/');
  return inferFlashSizeFromFilename(source) || FLASH_2MB;
}

/**
 * Attach flash_size onto a download item. Implicit 2MB is omitted so ordinary
 * firmware stays unmarked; explicit 2mb is kept to honour an override.
 */
export function assignDownloadFlashSize(item, authoredFlash) {
  if (!item || typeof item !== 'object') return item;
  const authored = normalizeFlashSize(authoredFlash);
  const flash = resolveDownloadFlashSize(item, authoredFlash);
  if (flash === FLASH_16MB) item.flash_size = FLASH_16MB;
  else if (authored === FLASH_2MB) item.flash_size = FLASH_2MB;
  else delete item.flash_size;
  return item;
}

/** Sorted flash sizes present across downloads: ['2mb'] | ['16mb'] | ['2mb','16mb']. */
export function flashSizesFromDownloads(downloads) {
  const sizes = new Set();
  for (const item of Array.isArray(downloads) ? downloads : []) {
    sizes.add(item && item.flash_size === FLASH_16MB ? FLASH_16MB : FLASH_2MB);
  }
  if (!sizes.size) sizes.add(FLASH_2MB);
  return [FLASH_2MB, FLASH_16MB].filter(size => sizes.has(size));
}

/** Space-separated sizes for data-flash: "2mb", "16mb", or "2mb 16mb". */
export function flashAttrFromDownloads(downloads) {
  return flashSizesFromDownloads(downloads).join(' ');
}

export function has16mbFirmware(downloads) {
  return (Array.isArray(downloads) ? downloads : []).some(item => item && item.flash_size === FLASH_16MB);
}

/**
 * Card-level flash profile: the set of flash sizes its firmware covers, and
 * whether every download requires a 16MB card (i.e. no 2MB download exists).
 */
export function deriveFlashProfile(downloads) {
  const list = Array.isArray(downloads) ? downloads.filter(Boolean) : [];
  if (!list.length) return undefined;
  const sizes = flashSizesFromDownloads(list);
  return { sizes, requires16mb: !sizes.includes(FLASH_2MB) };
}
