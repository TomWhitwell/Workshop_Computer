// Ask the GitHub REST API whether an account has committed to a card.
// Squash merges credit the PR author, and GitHub only links a commit to an
// account whose verified email it carries, so a match is reliable.

/**
 * Whether `author` has a commit under releases/<card>/ reachable from `ref`.
 * `author` must be a login: the API also accepts an email address here, which
 * would match the commit's self-reported email and could be spoofed.
 * Returns null on any API failure so callers fail closed.
 */
export async function hasCommittedToCard({ repo, card, ref, author, token, fetchImpl = fetch }) {
  if (!author || author.includes('@')) return null;
  const headers = { accept: 'application/vnd.github+json', 'x-github-api-version': '2022-11-28' };
  if (token) headers.authorization = `Bearer ${token}`;
  const url = `https://api.github.com/repos/${repo}/commits?${new URLSearchParams({
    sha: ref, path: `releases/${card}`, author, per_page: '1',
  })}`;
  try {
    const response = await fetchImpl(url, { headers });
    if (!response.ok) return null;
    const commits = await response.json();
    return Array.isArray(commits) ? commits.length > 0 : null;
  } catch {
    return null;
  }
}
