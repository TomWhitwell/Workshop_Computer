#!/usr/bin/env bash
# Build a site preview for every open pull request whose branch lives in this
# repository (fork PRs are skipped) into <out-dir>/<number>/, served at
# <base-url>pr/<number>/. Each PR is built from its own branch with its own
# generator, in a worktree of the current checkout.
#
# Never fails the job: a PR that does not build only loses its own preview.
# Writes a JSON array of {number, sha, status} to the `results` step output.
#
# Usage: build-pr-previews.sh <out-dir> <base-url>
# Needs GH_TOKEN, GITHUB_REPOSITORY and RUNNER_TEMP (set by GitHub Actions).

set -uo pipefail

out_dir=$(realpath -m "$1")
base_url=${2%/}/
mkdir -p "$out_dir"

results='[]'
if ! prs=$(gh api --paginate "repos/$GITHUB_REPOSITORY/pulls?state=open&per_page=100" \
  --jq ".[] | select(.head.repo.full_name == \"$GITHUB_REPOSITORY\") | \"\(.number) \(.head.sha)\""); then
  echo "::warning::Could not list open pull requests; deploying without PR previews."
  echo "results=$results" >> "${GITHUB_OUTPUT:-/dev/null}"
  exit 0
fi

# Read the PR list on fd 3 so git/npm inside the loop cannot consume it.
while read -r number sha <&3; do
  [ -n "$number" ] || continue
  worktree="$RUNNER_TEMP/pr-$number"
  echo "::group::Preview for PR #$number ($sha)"
  status=failure
  if git fetch --no-tags --quiet origin "$sha" \
    && git worktree add --detach --force "$worktree" "$sha" \
    && (
      cd "$worktree" \
        && npm ci --prefix tools/sitegen --no-audit --no-fund \
        && npm run sync-curation \
        && SITE_BASE_URL="${base_url}pr/$number/" SITE_REF="$sha" npm run build
    ); then
    # Keep previews out of search results. Injected here rather than by the
    # generator so it also applies to PRs branched before this script existed.
    # Pages that omit the optional <head> tag get it after <html> instead.
    find "$worktree/site" -name '*.html' -exec \
      perl -0pi -e 's/(<head\b[^>]*>)/$1\n  <meta name="robots" content="noindex">/i or s/(<html\b[^>]*>)/$1<meta name="robots" content="noindex">/i' {} +
    rm -rf "${out_dir:?}/$number"
    mv "$worktree/site" "$out_dir/$number"
    status=success
  else
    echo "::warning::Could not build a preview for PR #$number."
  fi
  git worktree remove --force "$worktree" 2>/dev/null || rm -rf "$worktree"
  echo "::endgroup::"
  results=$(jq -c --argjson n "$number" --arg sha "$sha" --arg status "$status" \
    '. + [{number: $n, sha: $sha, status: $status}]' <<< "$results")
done 3<<< "$prs"

echo "Preview results: $results"
echo "results=$results" >> "${GITHUB_OUTPUT:-/dev/null}"
