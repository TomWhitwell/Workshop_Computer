#!/usr/bin/env bash
# Gather the latest built preview of every open pull request whose branch
# lives in this repository (fork PRs are skipped) into <out-dir>/<number>/.
#
# Previews are built by pr-preview.yml in each pull request's own workflow run
# and uploaded as `pr-preview-<number>-<head-sha>` artifacts. This script only
# downloads them; it never runs pull request code. An artifact is used only if
# it was built for the PR's current head commit from a branch of this
# repository.
#
# Never fails the job: production must deploy even if previews cannot.
# Writes a JSON array of {number, sha, status, run_url} to the `results` step
# output, where status is success, failure, pending, missing or too-large.
#
# Usage: collect-pr-previews.sh <out-dir>
# Needs GH_TOKEN, GITHUB_REPOSITORY and RUNNER_TEMP (set by GitHub Actions).
# PREVIEW_MAX_MB caps each preview (default 100); PREVIEW_BUDGET_MB caps all
# previews together (default 600), keeping the site under Pages' 1 GB limit.

set -uo pipefail

out_dir=$(realpath -m "$1")
max_mb=${PREVIEW_MAX_MB:-100}
budget_mb=${PREVIEW_BUDGET_MB:-600}
mkdir -p "$out_dir"

results='[]'
finish() {
  echo "Preview results: $results"
  echo "results=$results" >> "${GITHUB_OUTPUT:-/dev/null}"
  exit 0
}

repo_id=$(gh api "repos/$GITHUB_REPOSITORY" --jq .id) || {
  echo "::warning::Could not read repository details; deploying without PR previews."
  finish
}
prs=$(gh api --paginate "repos/$GITHUB_REPOSITORY/pulls?state=open&per_page=100" \
  --jq ".[] | select(.head.repo.full_name == \"$GITHUB_REPOSITORY\") | \"\(.number) \(.head.sha)\"") || {
  echo "::warning::Could not list open pull requests; deploying without PR previews."
  finish
}

used_mb=0
# Read the PR list on fd 3 so commands inside the loop cannot consume it.
while read -r number sha <&3; do
  [[ $number =~ ^[0-9]+$ && $sha =~ ^[0-9a-f]{40}$ ]] || continue
  status=missing
  run_url=''

  artifact=$(gh api "repos/$GITHUB_REPOSITORY/actions/artifacts?name=pr-preview-$number-$sha&per_page=20" \
    --jq "[.artifacts[] | select((.expired | not) and .workflow_run.head_sha == \"$sha\" and .workflow_run.head_repository_id == $repo_id)] | max_by(.created_at) // empty | \"\(.id) \(.workflow_run.id)\"") || artifact=''

  if [ -n "$artifact" ]; then
    read -r artifact_id run_id <<< "$artifact"
    run_url="https://github.com/$GITHUB_REPOSITORY/actions/runs/$run_id"
    zip="$RUNNER_TEMP/pr-preview-$number.zip"
    dest="$out_dir/$number"
    if gh api "repos/$GITHUB_REPOSITORY/actions/artifacts/$artifact_id/zip" > "$zip" \
      && rm -rf "$dest" && mkdir -p "$dest" && unzip -q "$zip" -d "$dest"; then
      size_mb=$(du -sm "$dest" | cut -f1)
      if (( size_mb > max_mb || used_mb + size_mb > budget_mb )); then
        echo "::warning::Skipping the ${size_mb} MB preview for PR #$number (limit ${max_mb} MB each, ${budget_mb} MB in total)."
        rm -rf "$dest"
        status=too-large
      else
        used_mb=$((used_mb + size_mb))
        status=success
      fi
    else
      echo "::warning::Could not download the preview for PR #$number."
      rm -rf "$dest"
      status=failure
    fi
    rm -f "$zip"
  else
    # No usable artifact: report the latest preview run for this commit.
    run=$(gh api "repos/$GITHUB_REPOSITORY/actions/workflows/pr-preview.yml/runs?head_sha=$sha&event=pull_request&per_page=20" \
      --jq "[.workflow_runs[] | select(.head_repository.id == $repo_id)] | max_by(.created_at) // empty | \"\(.status) \(.conclusion) \(.html_url)\"") || run=''
    if [ -n "$run" ]; then
      read -r run_status conclusion run_url <<< "$run"
      if [ "$run_status" != completed ]; then
        status=pending
      elif [ "$conclusion" != success ]; then
        status=failure
      fi
    fi
  fi

  results=$(jq -c --argjson n "$number" --arg sha "$sha" --arg status "$status" --arg run_url "$run_url" \
    '. + [{number: $n, sha: $sha, status: $status, run_url: $run_url}]' <<< "$results")
done 3<<< "$prs"

finish
