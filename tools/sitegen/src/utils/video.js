import { parseYoutubeId, parseYoutubeStartSeconds, youtubeEmbedHtml } from './youtube.js';
import { parseInstagram, instagramEmbedHtml } from './instagram.js';

/**
 * Classify a demo / media URL into a renderable video descriptor.
 * Returns null when the URL is not a supported YouTube or Instagram link.
 */
export function classifyDemoVideo(url) {
  const u = String(url || '').trim();
  if (!u) return null;

  const youtubeId = parseYoutubeId(u);
  if (youtubeId) {
    const video = { provider: 'youtube', id: youtubeId, url: u, aspect: 'landscape' };
    const start = parseYoutubeStartSeconds(u);
    if (start != null && start > 0) video.start = start;
    return video;
  }

  const ig = parseInstagram(u);
  if (ig) {
    return {
      provider: 'instagram',
      id: ig.shortcode,
      kind: ig.kind,
      url: u,
      aspect: 'portrait',
    };
  }

  return null;
}

/** Embed HTML for a YouTube or Instagram URL (empty string if unsupported). */
export function videoEmbedHtml(url) {
  if (parseYoutubeId(url)) return youtubeEmbedHtml(url);
  if (parseInstagram(url)) return instagramEmbedHtml(url);
  return '';
}
