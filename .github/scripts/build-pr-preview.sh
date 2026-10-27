#!/usr/bin/env bash
# Build the checked-out pull request as its site preview, served at
# <base-url>pr/<number>/, into ./site. Runs in the pull request's own workflow
# run (pr-preview.yml), never on main, so PR code cannot reach main's Actions
# caches or the Pages deployment.
#
# Usage: build-pr-preview.sh <number> <head-sha> <base-url>
# PREVIEW_MAX_MB caps the preview's size (default 100).

set -euo pipefail

number=$1
sha=$2
base_url=${3%/}/
max_mb=${PREVIEW_MAX_MB:-100}

# The generator needs no install scripts, so never run any from a PR's
# dependency changes.
npm ci --prefix tools/sitegen --ignore-scripts --no-audit --no-fund
npm run sync-curation
SITE_BASE_URL="${base_url}pr/$number/" SITE_REF="$sha" timeout --kill-after=30s 10m npm run build

# Keep previews out of search results. Pages that omit the optional <head>
# tag get the meta tag after <html> instead.
find site -name '*.html' -exec \
  perl -0pi -e 's/(<head\b[^>]*>)/$1\n  <meta name="robots" content="noindex">/i or s/(<html\b[^>]*>)/$1<meta name="robots" content="noindex">/i' {} +

# Every preview ships inside the production site, which Pages caps at 1 GB.
size_mb=$(du -sm site | cut -f1)
if (( size_mb > max_mb )); then
  echo "::error::This preview is ${size_mb} MB; previews are limited to ${max_mb} MB."
  exit 1
fi
echo "Preview for PR #$number is ${size_mb} MB."
