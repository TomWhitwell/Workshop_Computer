// Shared "program-card silhouette" flash-size chip.
//
// Kept as plain markup strings, browser-safe, so it can be interpolated into
// the string templates and copied verbatim into the preview tool's lib/
// directory (see icons.js for the same convention).

import { FLASH_16MB } from '../utils/flash.js';

/**
 * Render a single flash-size chip: `16MB` (requires a 16MB program card) or
 * `2MB` (runs on any program card).
 */
export function renderFlashChip(size) {
  const is16mb = size === FLASH_16MB;
  return `<span class="flash-chip flash-chip--${is16mb ? '16mb' : '2mb'}">${is16mb ? '16MB' : '2MB'}</span>`;
}
