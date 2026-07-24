// Discovery / archive renderers for the program-card index.
//
// Ported from the MTM discovery UI (renderShelf / renderCardTile / renderTags in
// assets/program_cards/preview-tools.js and _layouts/program_cards_*.html),
// adapted to the canonical card model and the local curation layer.

import { curation, resolveFlair } from '../curation/index.js';

const CARD_ARTWORK = {
  '00_Simple_MIDI': '00-simple-midi.svg',
  '03_Turing_Machine': '03-turing-machine.svg',
  '20_reverb': '20-reverb.svg',
  '88_Blank': '88-blank.svg',
};

const FEATURED_COPY = {
  '88_Blank': {
    text: 'A blank card is a blank canvas. Install any card firmware you want from this site.',
    linkText: 'Learn how',
    link: 'https://www.musicthing.co.uk/workshopsystem/program-cards/install/',
  },
};

function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function escapeAttr(value) {
  return String(value ?? '').replaceAll('"', '&quot;');
}

function stripTags(value) {
  return String(value == null ? '' : value).replace(/<[^>]*>/g, '');
}

function truncate(value, length) {
  const text = stripTags(value).trim();
  if (text.length <= length) return text;
  return text.slice(0, Math.max(0, length - 1)).trimEnd() + '\u2026';
}

function cardNumber(card) {
  const raw = String(card.release || card.id || '').split('/')[0].split('_')[0].trim();
  const number = Number.parseInt(raw, 10);
  return Number.isNaN(number) ? raw : String(number);
}

function renderTagBadges(flair, hideTags = [], root = '.') {
  const hidden = new Set((hideTags || []).map(t => curation.slugify(t)));
  const badges = (flair || [])
    .filter(tag => !hidden.has(tag.id) && !hidden.has(curation.slugify(tag.label)))
    .map(tag => {
      const style = [
        tag.color ? `--program-card-tag-bg: ${tag.color}; --program-card-tag-border: ${tag.color};` : '',
        tag.textColor ? ` --program-card-tag-ink: ${tag.textColor};` : '',
      ].join('').trim();
      return `<a class="program-card-tag program-card-tag--${esc(tag.id)}" href="${root}/?tag=${encodeURIComponent(tag.id)}"${style ? ` style="${esc(style)}"` : ''}>${esc(tag.label)}</a>`;
    });
  return badges.length ? `<span class="program-card-tags">${badges.join('')}</span>` : '';
}

function renderAllTagBadges(card, flair, root = '.') {
  const curatedIds = new Set(flair.map(tag => tag.id));
  const authorBadges = (Array.isArray(card.tags) ? card.tags : [])
    .map(tag => ({ id: curation.slugify(tag), label: String(tag) }))
    .filter(tag => tag.id && !curatedIds.has(tag.id))
    .map(tag => `<a class="program-card-tag program-card-tag--author" href="${root}/?tag=${encodeURIComponent(tag.id)}">${esc(tag.label)}</a>`);
  const curated = renderTagBadges(flair, [], root);
  if (!authorBadges.length) return curated;
  const curatedBadges = curated.replace(/^<span class="program-card-tags">|<\/span>$/g, '');
  return `<span class="program-card-tags">${curatedBadges}${authorBadges.join('')}</span>`;
}

export function renderTile(card, opts = {}) {
  const { showVideo = false, showArtwork = false, showAllTags = false, showCreator = false, hideTags = [], root = '.' } = opts;
  const flair = resolveFlair(card.id);
  const number = cardNumber(card);
  const summary = card.short_description || '';
  const metadata = card.metadata || {};
  const firstVideo = Array.isArray(card.videos) && card.videos[0];
  const featuredCopy = showArtwork ? FEATURED_COPY[card.id] : null;

  const media = showVideo && firstVideo
    ? `<span class="program-card-tile__media" data-youtube-id="${esc(firstVideo.id)}" aria-hidden="true"><img src="https://img.youtube.com/vi/${esc(firstVideo.id)}/hqdefault.jpg" alt="" loading="lazy"></span>`
    : '';
  const artworkFile = showArtwork ? CARD_ARTWORK[card.id] : '';
  const artwork = card.id === '88_Blank' && showArtwork
    ? `<span class="program-card-tile__artwork program-card-tile__artwork--blank" aria-hidden="true" data-random-blank-card><svg viewBox="0 0 306 178"><g transform="translate(0 178) scale(1 -1)"><path fill="currentColor" d="M16 0h132l11 12h11l11-12h20l28 22h64c7 0 13 6 13 13v130c0 7-6 13-13 13H40c-22 0-40-18-40-40V16C0 7 7 0 16 0Z"/><circle cx="39" cy="138" r="27" fill="#fdfdfd"/></g></svg></span>`
    : artworkFile
      ? `<span class="program-card-tile__artwork" aria-hidden="true"><img src="${root}/assets/program_cards/${artworkFile}" alt="" loading="lazy"></span>`
      : '';

  const searchText = [
    number, card.title, summary, metadata.creator, metadata.language,
    ...(Array.isArray(card.tags) ? card.tags : []),
    ...flair.map(f => f.label),
  ].filter(Boolean).join(' ').toLowerCase();

  const curatedTagFilter = flair.map(f => f.id);
  const authorTagFilter = (Array.isArray(card.tags) ? card.tags : []).map(tag => curation.slugify(tag)).filter(Boolean);
  const tagFilter = [...new Set([...curatedTagFilter, ...authorTagFilter])].join(' ');

  return `<article class="program-card-tile${media ? ' program-card-tile--video' : ''}${artwork ? ' program-card-tile--artwork' : ''}"` +
    ` data-creator="${escapeAttr(metadata.creator || '')}" data-language="${escapeAttr(metadata.language || '')}"` +
    ` data-type="${escapeAttr(metadata.status || '')}" data-date="${escapeAttr(metadata.updated || '')}"` +
    ` data-name="${escapeAttr(String(card.title || card.id || '').toLowerCase())}" data-num="${escapeAttr(String(parseInt(number, 10) || 0))}"` +
    ` data-tags="${escapeAttr(tagFilter)}" data-search="${escapeAttr(searchText)}">
    <a class="program-card-tile__link" href="${card.id === '88_Blank' && showArtwork ? `${root}/random/` : `${root}/programs/${card.slug}/`}">
      ${media}
      <span class="program-card-tile__head"><span class="program-card-tile__title">${artwork || `<span class="program-card-tile__number">${esc(number)}</span>`}<span class="program-card-tile__name">${esc(truncate(card.title || card.id || 'Untitled card', 48))}</span>${showCreator && metadata.creator ? `<span class="program-card-tile__byline">by ${esc(metadata.creator)}</span>` : ''}</span></span>
      ${!featuredCopy && summary ? `<span class="program-card-tile__summary">${esc(truncate(summary, 190))}</span>` : ''}
    </a>
    ${featuredCopy ? `<span class="program-card-tile__summary">${esc(featuredCopy.text)}<a class="program-card-tile__inline-link" href="${esc(featuredCopy.link)}" target="_blank" rel="noopener noreferrer">${esc(featuredCopy.linkText)} <span aria-hidden="true">↗</span><span class="sr-only"> (opens in a new tab)</span></a></span>` : ''}
    ${showAllTags ? renderAllTagBadges(card, flair, root) : renderTagBadges(flair, hideTags, root)}
  </article>`;
}

function shelfCards(shelf, cardsById) {
  if (Array.isArray(shelf.cards)) {
    return shelf.cards.map(id => cardsById.get(id)).filter(Boolean);
  }
  if (Array.isArray(shelf.cards_from_tags)) {
    const wanted = shelf.cards_from_tags.map(t => curation.slugify(t));
    const limit = shelf.limit || 999;
    const list = [];
    for (const card of cardsById.values()) {
      const flairIds = resolveFlair(card.id).map(f => f.id);
      if (flairIds.some(id => wanted.includes(id))) list.push(card);
      if (list.length >= limit) break;
    }
    return list;
  }
  return [];
}

export function renderShelf(shelf, cardsById, opts = {}) {
  const { featured = false, root = '.' } = opts;
  const layout = shelf.layout || '';
  const showVideo = layout.includes('video');
  const list = shelfCards(shelf, cardsById);
  if (!list.length) return '';

  const layoutSlug = layout ? curation.slugify(layout) : '';
  const classes = `program-card-shelf${featured ? ' program-card-shelf--featured' : ''}${layoutSlug ? ` program-card-shelf--${layoutSlug}` : ''}`;
  const gridClasses = `program-card-grid${featured ? ' program-card-grid--featured' : ''}${layoutSlug ? ` program-card-grid--${layoutSlug}` : ''}`;

  return `<section class="${classes}">
    <header class="program-card-shelf__header"><h2>${esc(shelf.title || 'Shelf')}</h2>${shelf.intro ? `<p>${esc(shelf.intro)}</p>` : ''}</header>
    <div class="${gridClasses}">${list.map(card => renderTile(card, { showVideo, showArtwork: featured, hideTags: shelf.hide_tags, root })).join('')}</div>
  </section>`;
}

export function renderDiscovery(cards, root = '.') {
  const cardsById = new Map(cards.map(card => [card.id, card]));
  const cfg = curation.discovery || {};
  const hero = cfg.hero || {};
  const heroShelf = Array.isArray(hero.featured) && hero.featured.length
    ? renderShelf({ title: hero.title || 'Included cards', intro: hero.text, cards: hero.featured, layout: hero.layout || 'grid' }, cardsById, { featured: true, root })
    : '';
  const shelves = (cfg.shelves || []).map(shelf => renderShelf(shelf, cardsById, { root })).join('');
  return `<div id="discovery">${heroShelf}${shelves}</div>`;
}

function renderArchiveRow(card, root) {
  const flair = resolveFlair(card.id);
  const number = cardNumber(card);
  const summary = card.short_description || '';
  const searchText = [number, card.title, summary, card.metadata?.creator, ...(Array.isArray(card.tags) ? card.tags : []), ...flair.map(f => f.label)]
    .filter(Boolean).join(' ').toLowerCase();
  const date = card.metadata?.updated || '';
  const curatedTagFilter = flair.map(f => f.id);
  const authorTagFilter = (Array.isArray(card.tags) ? card.tags : []).map(tag => curation.slugify(tag)).filter(Boolean);
  const tagFilter = [...new Set([...curatedTagFilter, ...authorTagFilter])].join(' ');
  return `<article class="program-card-archive-row" data-date="${escapeAttr(date)}" data-name="${escapeAttr(String(card.title || '').toLowerCase())}" data-num="${escapeAttr(String(parseInt(number, 10) || 0))}" data-tags="${escapeAttr(tagFilter)}" data-search="${escapeAttr(searchText)}">
    <a class="program-card-archive-row__link" href="${root}/programs/${card.slug}/">
      <span class="program-card-archive-row__number">${esc(number)}</span>
      <span class="program-card-archive-row__main"><span class="program-card-archive-row__heading"><span class="program-card-archive-row__title">${esc(card.title)}</span>${card.metadata?.creator ? `<span class="program-card-archive-row__byline">by ${esc(card.metadata.creator)}</span>` : ''}</span>${summary ? `<span class="program-card-archive-row__summary">${esc(truncate(summary, 120))}</span>` : ''}</span>
    </a>
    <span class="program-card-archive-row__flags">${renderAllTagBadges(card, flair, root)}</span>
  </article>`;
}

export function renderArchive(cards, root = '..') {
  const rows = cards.map(card => renderArchiveRow(card, root)).join('');
  return `<section class="program-card-archive">
    <header class="program-card-shelf__header"><h2>Complete index</h2></header>
    <div class="program-card-archive-list">${rows}</div>
  </section>`;
}
