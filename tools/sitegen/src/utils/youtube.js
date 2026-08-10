/** Parse a YouTube time offset (`1772`, `1331s`, `1h2m3s`) into seconds. */
function parseTimeOffset(raw) {
  const s = String(raw || '').trim();
  if (!s) return null;
  if (/^\d+$/.test(s)) return Number(s);

  const hms = s.match(/^(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$/i);
  if (hms && (hms[1] || hms[2] || hms[3])) {
    return (Number(hms[1] || 0) * 3600) + (Number(hms[2] || 0) * 60) + Number(hms[3] || 0);
  }
  return null;
}

/** Normalize hrefs pulled from sanitized HTML (`&amp;` → `&`). */
function urlOrRaw(url) {
  return String(url || '').replace(/&amp;/gi, '&');
}

/** Extract YouTube video ID from common URL forms. */
export function parseYoutubeId(url) {
  const u = urlOrRaw(url).trim();
  if (!u) return null;

  let m = u.match(/(?:^|\b)youtu\.be\/([A-Za-z0-9_-]{6,})/i);
  if (m) return m[1];

  m = u.match(/[?&]v=([A-Za-z0-9_-]{6,})/i);
  if (m && /youtube\.com\//i.test(u)) return m[1];

  m = u.match(/youtube\.com\/shorts\/([A-Za-z0-9_-]{6,})/i);
  if (m) return m[1];

  m = u.match(/youtube\.com\/embed\/([A-Za-z0-9_-]{6,})/i);
  if (m) return m[1];

  return null;
}

/**
 * Extract a start offset in seconds from `t=` / `start=` query (or `#t=`) params.
 * Returns null when absent or unparseable.
 */
export function parseYoutubeStartSeconds(url) {
  const u = urlOrRaw(url).trim();
  if (!u) return null;

  let raw = null;
  try {
    const parsed = new URL(u);
    raw = parsed.searchParams.get('t') || parsed.searchParams.get('start');
    if (!raw && parsed.hash) {
      const hm = parsed.hash.match(/[#&?]t=([^&]+)/i);
      if (hm) raw = decodeURIComponent(hm[1]);
    }
  } catch {
    const m = u.match(/[?&#](?:t|start)=([^&#]+)/i);
    if (m) {
      try { raw = decodeURIComponent(m[1]); }
      catch { raw = m[1]; }
    }
  }

  const seconds = parseTimeOffset(raw);
  if (seconds == null || !Number.isFinite(seconds) || seconds < 0) return null;
  return Math.floor(seconds);
}

export function youtubeEmbedHtml(urlOrId) {
  const id = parseYoutubeId(urlOrId) || (
    /^[A-Za-z0-9_-]{6,}$/.test(String(urlOrId || '').trim()) ? String(urlOrId).trim() : null
  );
  if (!id) return '';
  const start = parseYoutubeStartSeconds(urlOrId);
  const startParam = start != null && start > 0 ? `&start=${start}` : '';
  const embedUrl = `https://www.youtube.com/embed/${id}?rel=0${startParam}`;
  return `<div class="video-embed"><iframe src="${embedUrl}" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe></div>`;
}
