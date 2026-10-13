#!/bin/sh
# Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
TEST_EXE=$(mktemp "${TMPDIR:-/tmp}/spin-test.XXXXXX")
trap 'rm -f "$TEST_EXE"' EXIT HUP INT TERM
"${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    -fsanitize=undefined,address -fno-sanitize-recover=all -Isrc \
    tests/test_rotary.cpp -o "$TEST_EXE"
"$TEST_EXE"
