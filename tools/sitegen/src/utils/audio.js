// Audio sample classification for the `audio-sample` field.
//
// Pure and offline (no network): given an author value (a repo-relative file
// path or an http(s) URL), it produces a display item describing how to render
// the sample. Shared by the site build and the browser preview so both match.

const AUDIO_FILE_RE = /\.(mp3|ogg|oga|wav|flac|m4a|aac|opus)(\?|#|$)/i;

/**
 * Pull the player URL (and pixel height, if given) out of a pasted embed
 * `<iframe …>` snippet, so authors can paste the whole Bandcamp/SoundCloud
 * "Embed" code instead of hand-extracting the src. Returns null when there's
 * no iframe src.
 */
export function extractIframeSrc(html) {
  const s = String(html);
  const src = s.match(/<iframe[^>]*\ssrc\s*=\s*["']([^"']+)["']/i);
  if (!src) return null;
  const h = s.match(/\sheight\s*=\s*["']?(\d+)/i) || s.match(/height:\s*(\d+)px/i);
  return { src: src[1], height: h ? parseInt(h[1], 10) : 0 };
}

/** Build the SoundCloud player iframe URL from a track/set page URL. */
export function soundcloudEmbedUrl(trackUrl) {
  const params = new URLSearchParams({
    url: trackUrl,
    color: '#ff5500',
    auto_play: 'false',
    hide_related: 'true',
    show_comments: 'false',
    show_user: 'true',
    show_reposts: 'false',
    visual: 'false',
  });
  return `https://w.soundcloud.com/player/?${params.toString()}`;
}

/**
 * Classify an already-absolute audio URL into a render item:
 *   { kind: 'file'|'soundcloud'|'bandcamp'|'link', url, embedUrl?, host? }
 * - soundcloud: player iframe derived from a track URL (no API call); a URL that
 *   is already a w.soundcloud.com/player link is used as-is.
 * - bandcamp: only embeddable when given an EmbeddedPlayer URL (from Bandcamp's
 *   Share -> Embed dialog); a plain album/track URL can't be embedded without a
 *   network fetch, so it degrades to a link.
 * - file: a direct audio file -> <audio> element.
 * - link: anything else.
 */
export function classifyAudioUrl(url) {
  let host = '';
  try { host = new URL(url).hostname.replace(/^www\./i, ''); } catch { /* not a URL */ }

  if (host === 'soundcloud.com' || host === 'snd.sc' || host.endsWith('.soundcloud.com')) {
    const isPlayer = host === 'w.soundcloud.com' || /\/player\//i.test(url);
    return { kind: 'soundcloud', url, embedUrl: isPlayer ? url : soundcloudEmbedUrl(url), host };
  }
  if (host === 'bandcamp.com' || host.endsWith('.bandcamp.com')) {
    if (/\/EmbeddedPlayer/i.test(url)) return { kind: 'bandcamp', url, embedUrl: url, host };
    return { kind: 'link', url, host }; // plain page: link only (no fetch)
  }
  if (AUDIO_FILE_RE.test(url)) return { kind: 'file', url, host };
  return { kind: 'link', url, host };
}

/**
 * Resolve an `audio-sample` field (a string, a list, `{ url, title }` objects,
 * or a pasted embed `<iframe>` snippet) into a list of classified items.
 * `resolveRel(relPath)` turns a repo-relative path into an absolute URL
 * (returns '' when it can't).
 */
export function resolveAudioSamples(value, resolveRel) {
  const list = Array.isArray(value) ? value : (value == null ? [] : [value]);
  const out = [];
  for (const raw of list) {
    let s = '';
    let title = '';
    if (typeof raw === 'string') {
      s = raw.trim();
    } else if (raw && typeof raw === 'object') {
      s = typeof raw.url === 'string' ? raw.url.trim() : '';
      if (typeof raw.title === 'string') title = raw.title.trim();
    }
    if (!s) continue;
    // Accept a pasted embed <iframe …> and pull out its player URL (+ height).
    let height = 0;
    if (/<iframe/i.test(s)) {
      const ex = extractIframeSrc(s);
      if (ex && ex.src) { s = ex.src; height = ex.height; }
    }
    const abs = /^https?:\/\//i.test(s) ? s : (resolveRel ? resolveRel(s) : '');
    if (!abs) continue;
    const item = classifyAudioUrl(abs);
    if (title) item.title = title;
    if (height) item.height = height;
    out.push(item);
  }
  return out;
}

/** Read the `audio-sample`/`audio-samples` field from a parsed object (loose key). */
export function getAudioField(obj) {
  if (!obj || typeof obj !== 'object') return undefined;
  for (const k of Object.keys(obj)) {
    const nk = k.toLowerCase().replace(/[^a-z0-9]/g, '');
    if (nk === 'audiosample' || nk === 'audiosamples') return obj[k];
  }
  return undefined;
}
