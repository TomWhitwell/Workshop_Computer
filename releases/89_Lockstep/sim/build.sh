#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -I../src test_engine.cpp -o test_engine
./test_engine
