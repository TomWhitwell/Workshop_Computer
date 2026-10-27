#!/usr/bin/env bash
# Create or update this repository's single site-preview comment on a pull
# request. The comment is found by a hidden marker, and is left untouched when
# its text would not change.
#
# Usage: pr-preview-comment.sh <pr-number> <body-markdown>
# Needs GH_TOKEN and GITHUB_REPOSITORY (set by GitHub Actions).

set -euo pipefail

number=$1
marker='<!-- site-pr-preview -->'
body="$marker
$2"

existing=$(gh api --paginate "repos/$GITHUB_REPOSITORY/issues/$number/comments" \
  --jq ".[] | select(.body | startswith(\"$marker\")) | {id, body} | @base64" | head -n 1)

if [ -z "$existing" ]; then
  gh api --silent "repos/$GITHUB_REPOSITORY/issues/$number/comments" -f body="$body"
  exit 0
fi

id=$(base64 -d <<< "$existing" | jq -r .id)
if [ "$(base64 -d <<< "$existing" | jq -r .body)" != "$body" ]; then
  gh api --silent -X PATCH "repos/$GITHUB_REPOSITORY/issues/comments/$id" -f body="$body"
fi
