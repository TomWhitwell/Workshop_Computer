/** Extract YouTube video ID from common URL forms. */
export function parseYoutubeId(url) {
  const u = String(url || '').trim();
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

export function youtubeEmbedHtml(urlOrId) {
  const id = parseYoutubeId(urlOrId) || (
    /^[A-Za-z0-9_-]{6,}$/.test(String(urlOrId || '').trim()) ? String(urlOrId).trim() : null
  );
  if (!id) return '';
  const embedUrl = `https://www.youtube-nocookie.com/embed/${id}?rel=0`;
  return `<div class="video-embed"><iframe src="${embedUrl}" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" allowfullscreen title="YouTube video"></iframe></div>`;
}
