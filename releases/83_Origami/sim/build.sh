#!/usr/bin/env bash
# Build and run the offline Origami renderer. Needs only g++ (C++17).
# Works on macOS, Linux, and Raspberry Pi OS — no Pico SDK required.
set -e
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -Wall render.cpp -o render
./render .
echo
echo "WAVs written to: $(pwd)"
