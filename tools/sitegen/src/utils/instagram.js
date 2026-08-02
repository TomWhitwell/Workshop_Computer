/** Extract Instagram media shortcode + kind from common URL forms. */
export function parseInstagram(url) {
  const u = String(url || '').trim();
  if (!u || !/instagram\.com\//i.test(u)) return null;

  // /reel|p|tv/SHORTCODE[/embed[/captioned]]
  let m = u.match(/instagram\.com\/(?:[^/?#]+\/)?(reel|p|tv)\/([A-Za-z0-9_-]{5,})/i);
  if (m) return { kind: m[1].toLowerCase(), shortcode: m[2] };

  return null;
}

/** Canonical permalink used by Instagram's official embed script. */
export function instagramPermalink(url) {
  const parsed = parseInstagram(url);
  if (!parsed) return '';
  return `https://www.instagram.com/${parsed.kind}/${parsed.shortcode}/`;
}

/**
 * Build an Instagram embed using the official blockquote + embed.js pattern.
 * The script (loaded by the page) measures content and sizes the iframe.
 */
export function instagramEmbedHtml(url) {
  const parsed = parseInstagram(url);
  if (!parsed) return '';
  const permalink = instagramPermalink(url);
  return `<div class="instagram-embed"><blockquote class="instagram-media" data-instgrm-permalink="${permalink}" data-instgrm-version="14"><a href="${permalink}" target="_blank" rel="noopener noreferrer">View this ${parsed.kind === 'reel' ? 'reel' : 'post'} on Instagram</a></blockquote></div>`;
}
