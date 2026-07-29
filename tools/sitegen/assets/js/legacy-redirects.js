const assetUrl = new URL(import.meta.url);
const assetSuffix = '/assets/js/legacy-redirects.js';
const siteBasePath = assetUrl.pathname.endsWith(assetSuffix)
  ? assetUrl.pathname.slice(0, -assetSuffix.length) || '/'
  : '/';

function normalizedBasePath(value) {
  const path = `/${String(value || '').replace(/^\/+|\/+$/g, '')}`;
  return path === '/' ? path : `${path}/`;
}

/** Resolve an old public URL to its current equivalent, or return null. */
export function resolveLegacyTarget(currentUrl, basePath, knownSlugs) {
  const current = new URL(currentUrl);
  const base = normalizedBasePath(basePath);
  const baseWithoutSlash = base === '/' ? '' : base.slice(0, -1);
  const homePaths = new Set([base, `${base}index.html`]);

  // The original Music Thing Modular card index addressed cards with hashes.
  // Accept that form at the new catalogue root without treating arbitrary
  // fragments as paths.
  if (current.hash && homePaths.has(current.pathname)) {
    let slug;
    try {
      slug = decodeURIComponent(current.hash.slice(1)).trim().toLowerCase();
    } catch {
      return null;
    }
    if (knownSlugs.has(slug)) {
      const target = new URL(`${base}programs/${encodeURIComponent(slug)}/`, current.origin);
      target.search = current.search;
      return target.href;
    }
  }

  // GitHub's default project URL contains /Workshop_Computer/. If that prefix
  // survives an external redirect to the root custom domain, remove it while
  // retaining card web-editor subpaths, queries, and fragments. Never do this
  // when the site itself is legitimately hosted at a project subpath.
  if (base !== '/') return null;
  const legacyPrefix = '/Workshop_Computer';
  if (current.pathname === legacyPrefix || current.pathname === `${legacyPrefix}/` || current.pathname === `${legacyPrefix}/index.html`) {
    return new URL(`/${current.search}${current.hash}`, current.origin).href;
  }

  const programMatch = current.pathname.match(/^\/Workshop_Computer\/programs\/([^/]+)(\/.*)?$/i);
  if (!programMatch) return null;
  const slug = decodeURIComponent(programMatch[1]).toLowerCase();
  if (!knownSlugs.has(slug)) return null;

  const suffix = programMatch[2] || '/';
  const target = new URL(`${baseWithoutSlash}/programs/${encodeURIComponent(slug)}${suffix}`, current.origin);
  target.search = current.search;
  target.hash = current.hash;
  return target.href;
}

async function redirectLegacyUrl() {
  try {
    const catalogueUrl = new URL(`${normalizedBasePath(siteBasePath)}cards.json`, assetUrl.origin);
    const response = await fetch(catalogueUrl);
    if (!response.ok) return;
    const catalogue = await response.json();
    const knownSlugs = new Set((catalogue.cards || []).map(card => String(card.slug || '').toLowerCase()).filter(Boolean));
    const target = resolveLegacyTarget(location.href, siteBasePath, knownSlugs);
    if (target && target !== location.href) location.replace(target);
  } catch {
    // Compatibility redirects must never prevent the requested page loading.
  }
}

if (typeof window !== 'undefined') redirectLegacyUrl();