// Shared inline SVG icons.
//
// Kept as plain markup strings so they can be interpolated into the string
// templates and copied verbatim into the preview tool's lib/ directory.

/**
 * Small north-east arrow used to mark links that leave the site.
 * Decorative: callers pair it with an `.sr-only` "(opens in a new tab)" label
 * where the destination is not otherwise obvious.
 *
 * Append it directly after the link text with no separating space - the gap
 * comes from `.external-link-arrow`'s margin, so the arrow can never wrap onto
 * a line of its own.
 */
export function externalLinkArrow() {
  return '<svg class="external-link-arrow" viewBox="0 0 10 10" width="10" height="10" aria-hidden="true" focusable="false"><path d="M1.2 8.8 7.4 2.6M3.4 2.3h4.3v4.3" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="butt" stroke-linejoin="miter"/></svg>';
}
