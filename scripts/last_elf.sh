#!/usr/bin/env bash
# Create/refresh a stable "build/.last.elf" symlink pointing at the most
# recently built firmware ELF.
#
# The VS Code debug configuration (.vscode/launch.json) attaches to
# "<card>/build/.last.elf". Not every build path creates that symlink:
# pico_auto_make.mk does, but cards with their own Makefile/GNUmakefile and
# scripts/build.sh do not. Running this after `make build` guarantees the
# symlink exists for every card.
#
# Usage: last_elf.sh [BUILD_DIR]   (BUILD_DIR defaults to ./build)
set -uo pipefail

build_dir="${1:-build}"

if [[ ! -d "$build_dir" ]]; then
  echo "last_elf: build directory not found: $build_dir" >&2
  exit 0
fi

build_abs="$(cd "$build_dir" && pwd)"

# Newest ELF at depth <= 4 (excludes the Pico SDK boot stage2 ELF, which lives
# deeper) and excludes the symlink itself.
last_elf="$(find "$build_abs" -maxdepth 4 -type f -name '*.elf' ! -name '.last.elf' \
  -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"

if [[ -z "$last_elf" ]]; then
  echo "last_elf: no ELF found under $build_dir" >&2
  exit 0
fi

ln -sf "$last_elf" "$build_dir/.last.elf"
echo "last_elf: $build_dir/.last.elf -> $last_elf"
