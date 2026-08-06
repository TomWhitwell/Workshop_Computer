#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "=========================================================="
echo " Starting local web server for Clockwork Web UI..."
echo " Open your browser and navigate to: http://localhost:8000"
echo "=========================================================="
echo " NOTE: Web Serial requires Google Chrome or Microsoft Edge."
echo "=========================================================="
python3 -m http.server 8000 --directory "$DIR/web"
