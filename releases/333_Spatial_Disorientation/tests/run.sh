#!/bin/sh
# Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
spatial_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/spatial333-tests.XXXXXX")
trap 'rm -rf "$spatial_test_dir"' EXIT HUP INT TERM
for spatial_test_file in tests/*_test.cpp; do
    spatial_test_name=$(basename "$spatial_test_file" .cpp)
    if [ "$spatial_test_name" = save_test ]; then
        "${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -fsanitize=address,undefined -DSPATIAL_HRTF_TAPS=64 -DSPATIAL_ELEVATION_SOLO=0 -Itests/mocks -Isrc "$spatial_test_file" -o "$spatial_test_dir/$spatial_test_name"
    else
        "${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -fsanitize=address,undefined -DSPATIAL_HRTF_TAPS=64 -Isrc "$spatial_test_file" -o "$spatial_test_dir/$spatial_test_name"
    fi
    "$spatial_test_dir/$spatial_test_name"
done
node tests/editor_test.cjs
