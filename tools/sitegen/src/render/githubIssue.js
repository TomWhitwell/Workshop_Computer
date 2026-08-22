export const UPSTREAM_REPO = 'TomWhitwell/Workshop_Computer';
export const UPSTREAM_ISSUES = `https://github.com/${UPSTREAM_REPO}`;
export const GITHUB_LABEL_MAX = 50;
export const NEW_CARD_LABEL_COLOR = 'c5def5';
export const SKIP_CARD_LABEL_IDS = new Set(['02_comingsoon', '77_Placeholder']);

export function cardFolderNumber(id) {
  return String(id || '').split('_')[0].trim();
}

export function cardIssueLabel({ id, title } = {}) {
  const number = cardFolderNumber(id);
  const name = String(title || '').replace(/\s+/g, ' ').trim();
  let label = !number ? name
    : !name ? number
    : (name === number || name.startsWith(`${number} `)) ? name
    : `${number} ${name}`;
  label = label.replace(/[\u0000-\u001f]/g, '').trim();
  if (label.length > GITHUB_LABEL_MAX) label = label.slice(0, GITHUB_LABEL_MAX).trimEnd();
  return label;
}

export function cardIssueLabelsFromCards(cards) {
  const labels = [];
  const seen = new Set();
  for (const card of Array.isArray(cards) ? cards : []) {
    if (!card || SKIP_CARD_LABEL_IDS.has(card.id)) continue;
    const label = cardIssueLabel(card);
    if (!label || seen.has(label)) continue;
    seen.add(label);
    labels.push(label);
  }
  return labels;
}

export function parseForgeRepository(raw) {
  const text = String(raw || '').trim();
  if (!text) return null;
  let url;
  try {
    url = new URL(text);
  } catch {
    return null;
  }
  if (url.protocol !== 'http:' && url.protocol !== 'https:') return null;
  const host = url.hostname.replace(/^www\./i, '').toLowerCase();
  const parts = url.pathname.replace(/\/+$/, '').split('/').filter(Boolean);
  if (parts.length < 2) return null;
  const owner = parts[0];
  const repo = parts[1].replace(/\.git$/i, '');
  if (!owner || !repo) return null;
  if (/^(orgs|settings|login|marketplace|topics|features)$/i.test(owner)) return null;
  const kind = host === 'github.com' ? 'github'
    : host === 'gitlab.com' ? 'gitlab'
    : 'forgejo';
  return { host, owner, repo, origin: `${url.protocol}//${url.host}`, kind };
}

export function isCanonicalWorkshopRepo(parsed) {
  return parsed?.kind === 'github'
    && String(parsed.owner).toLowerCase() === 'tomwhitwell'
    && String(parsed.repo).toLowerCase() === 'workshop_computer';
}

export function cardFeedbackHostLabel(card) {
  const parsed = parseForgeRepository(card?.metadata?.repository);
  if (parsed && !isCanonicalWorkshopRepo(parsed)) {
    if (parsed.host === 'github.com') return 'GitHub';
    if (parsed.host === 'codeberg.org') return 'Codeberg';
    if (parsed.host === 'gitlab.com') return 'GitLab';
    return parsed.host;
  }
  return 'GitHub';
}

export function websiteFeedbackUrl() {
  return `${UPSTREAM_ISSUES}/issues/new?template=website_feedback.yml`;
}

function catalogueFeedbackUrl(card) {
  const label = cardIssueLabel(card);
  const params = new URLSearchParams({
    template: 'card_feedback.yml',
    labels: label,
    card: label,
  });
  return `${UPSTREAM_ISSUES}/issues/new?${params}`;
}

function externalIssuesUrl(parsed, card) {
  const title = cardIssueLabel(card);
  if (parsed.kind === 'gitlab') {
    const params = new URLSearchParams({ 'issue[title]': title ? `${title}: ` : '' });
    return `${parsed.origin}/${parsed.owner}/${parsed.repo}/-/issues/new?${params}`;
  }
  const base = `${parsed.origin}/${parsed.owner}/${parsed.repo}/issues/new`;
  if (!title) return base;
  return `${base}?${new URLSearchParams({ title: `${title}: ` })}`;
}

export function cardFeedbackUrl(card) {
  const parsed = parseForgeRepository(card?.metadata?.repository);
  if (parsed && !isCanonicalWorkshopRepo(parsed)) return externalIssuesUrl(parsed, card);
  return catalogueFeedbackUrl(card);
}
