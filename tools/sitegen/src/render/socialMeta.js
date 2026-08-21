export const SITE_NAME = 'Workshop Computer';
export const DEFAULT_OG_DESCRIPTION = 'Program cards for the Music Thing Modular Workshop Computer.';
export const DEFAULT_OG_IMAGE_PATH = 'assets/og/default.png';
export const OG_IMAGE_WIDTH = 1200;
export const OG_IMAGE_HEIGHT = 630;
export const THEME_COLOR = '#27743a';

/** Join a site base (trailing slash) with a relative path into an absolute URL. */
export function absoluteSiteUrl(siteBase, relativePath = '') {
  return new URL(String(relativePath || '').replace(/^\/+/, ''), siteBase).href;
}

export function escapeHtmlAttr(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/**
 * Open Graph + Twitter Card tags used by Discord, Slack, iMessage, and similar
 * unfurl crawlers. Omitted when `url` or `image` is missing so unit tests can
 * render a layout without a public site base.
 */
export function renderSocialMeta({
  title,
  description = DEFAULT_OG_DESCRIPTION,
  url,
  image,
  imageAlt,
} = {}) {
  if (!url || !image) return '';
  const pageTitle = String(title || SITE_NAME);
  const pageDescription = String(description || DEFAULT_OG_DESCRIPTION);
  const alt = String(imageAlt || pageTitle);
  const attr = escapeHtmlAttr;
  return `
  <meta name="description" content="${attr(pageDescription)}" />
  <meta name="theme-color" content="${THEME_COLOR}" />
  <link rel="canonical" href="${attr(url)}" />
  <meta property="og:type" content="website" />
  <meta property="og:site_name" content="${attr(SITE_NAME)}" />
  <meta property="og:title" content="${attr(pageTitle)}" />
  <meta property="og:description" content="${attr(pageDescription)}" />
  <meta property="og:url" content="${attr(url)}" />
  <meta property="og:image" content="${attr(image)}" />
  <meta property="og:image:width" content="${OG_IMAGE_WIDTH}" />
  <meta property="og:image:height" content="${OG_IMAGE_HEIGHT}" />
  <meta property="og:image:type" content="image/png" />
  <meta property="og:image:alt" content="${attr(alt)}" />
  <meta name="twitter:card" content="summary_large_image" />
  <meta name="twitter:title" content="${attr(pageTitle)}" />
  <meta name="twitter:description" content="${attr(pageDescription)}" />
  <meta name="twitter:image" content="${attr(image)}" />
  <meta name="twitter:image:alt" content="${attr(alt)}" />`;
}
