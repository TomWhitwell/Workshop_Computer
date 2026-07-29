import crypto from 'node:crypto';

const PLACEHOLDER = '__WORKSHOP_CSP__';

/** Add deterministic SHA-256 allowances for every inline script in generated HTML. */
export function applyContentSecurityPolicy(html, { preview = false } = {}) {
  const hashes = [];
  for (const match of String(html).matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/gi)) {
    const body = match[1];
    if (!body) continue;
    const digest = crypto.createHash('sha256').update(body).digest('base64');
    hashes.push(`'sha256-${digest}'`);
  }
  const scriptSources = ["'self'", ...(preview ? ["'unsafe-eval'"] : []), ...new Set(hashes)];
  const directives = [
    "default-src 'self'",
    `script-src ${scriptSources.join(' ')}`,
    "style-src 'self' 'unsafe-inline'",
    "font-src 'self' data:",
    "img-src 'self' data: https:",
    "media-src 'self' https:",
    "frame-src https://www.youtube.com https://www.youtube-nocookie.com",
    "object-src 'self'",
    "connect-src 'self' https:",
    "base-uri 'self'",
    "form-action 'self'",
    ...(preview ? ["worker-src 'self' blob:"] : []),
  ];
  return String(html).replace(PLACEHOLDER, directives.join('; '));
}

export const CSP_PLACEHOLDER = PLACEHOLDER;
