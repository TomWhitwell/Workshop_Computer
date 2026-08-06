#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
PORT="${1:-8080}"
echo "Starting server at http://localhost:$PORT"
echo "Open the Degenerator Manager in your browser."
echo ""

if command -v python3 &>/dev/null; then
    python3 -m http.server "$PORT"
elif command -v python &>/dev/null; then
    python -m http.server "$PORT"
else
    echo "Error: Python 3 is required but not found."
    exit 1
fi
