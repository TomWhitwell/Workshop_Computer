#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage:
  build.sh <directory-with-CMakeLists.txt> [options]

Options:
  --target <cmake-target>   Build a single CMake target
  --configure-only          Configure only; do not build
  --clean                   Remove the build directory and exit

Environment:
  PICO_SDK_PATH             Pico SDK location (optional)
  COMPUTERCARD_DEPS_DIR     Shared dependency cache root (default: ~/.cache/computercard)
  COMPUTERCARD_BUILD_DIR    Build directory override (default: <dir>/build)
  COMPUTERCARD_GENERATOR    CMake generator (default: Ninja)
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

TARGET_DIR="$1"; shift

TARGET=""
CONFIGURE_ONLY=0
CLEAN=0

GENERATOR="${COMPUTERCARD_GENERATOR:-Ninja}"
DEPS_DIR="${COMPUTERCARD_DEPS_DIR:-${HOME}/.cache/computercard}"
BUILD_DIR="${COMPUTERCARD_BUILD_DIR:-$TARGET_DIR/build}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      if [[ $# -lt 2 ]]; then
        echo "Error: --target requires a value" >&2
        usage
        exit 2
      fi
      TARGET="$2"
      shift 2
      ;;
    --configure-only)
      CONFIGURE_ONLY=1
      shift
      ;;
    --clean)
      CLEAN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ ! -d "$TARGET_DIR" ]]; then
  echo "Error: directory not found: $TARGET_DIR" >&2
  exit 1
fi

if [[ ! -f "$TARGET_DIR/CMakeLists.txt" ]]; then
  echo "Error: expected a directory containing CMakeLists.txt: $TARGET_DIR" >&2
  exit 1
fi

if [[ $CLEAN -eq 1 ]]; then
  rm -rf "$BUILD_DIR"
  exit 0
fi

# Always clean before (re)configure/build for reproducible builds.
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Cache directory for CMake FetchContent downloads (shared across builds).
mkdir -p "$DEPS_DIR/fetchcontent"

if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  EXISTING_GENERATOR="$(grep -E '^CMAKE_GENERATOR:INTERNAL=' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2- || true)"
  if [[ -n "$EXISTING_GENERATOR" && "$EXISTING_GENERATOR" != "$GENERATOR" ]]; then
    echo "Build dir generator mismatch ($EXISTING_GENERATOR vs $GENERATOR); recreating $BUILD_DIR" >&2
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
  fi
fi

CMAKE_ARGS=("-S" "$TARGET_DIR" "-B" "$BUILD_DIR" "-G" "$GENERATOR")
if [[ -n "${PICO_SDK_PATH:-}" ]]; then
  CMAKE_ARGS+=("-DPICO_SDK_PATH=${PICO_SDK_PATH}")
fi

if [[ -n "${PICO_TOOLCHAIN_PATH:-}" ]]; then
  CMAKE_ARGS+=("-DPICO_TOOLCHAIN_PATH=${PICO_TOOLCHAIN_PATH}")
else
  TOOLCHAIN_BIN="$(command -v arm-none-eabi-gcc || true)"
  if [[ -n "$TOOLCHAIN_BIN" ]]; then
    TOOLCHAIN_ROOT="$(cd "$(dirname "$TOOLCHAIN_BIN")/.." && pwd)"
    CMAKE_ARGS+=("-DPICO_TOOLCHAIN_PATH=${TOOLCHAIN_ROOT}")
  fi
fi

TOOLCHAIN_C="$(command -v arm-none-eabi-gcc || true)"
TOOLCHAIN_CXX="$(command -v arm-none-eabi-g++ || true)"
if [[ -n "$TOOLCHAIN_C" ]]; then
  CMAKE_ARGS+=("-DCMAKE_C_COMPILER=${TOOLCHAIN_C}")
fi
if [[ -n "$TOOLCHAIN_CXX" ]]; then
  CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=${TOOLCHAIN_CXX}")
fi

# Reuse downloaded dependencies across builds.
CMAKE_ARGS+=("-DFETCHCONTENT_BASE_DIR=${DEPS_DIR}/fetchcontent")
CMAKE_ARGS+=("-DFETCHCONTENT_UPDATES_DISCONNECTED=ON")

cmake "${CMAKE_ARGS[@]}"

if [[ $CONFIGURE_ONLY -eq 1 ]]; then
  exit 0
fi

if [[ -n "$TARGET" ]]; then
  cmake --build "$BUILD_DIR" --target "$TARGET"
else
  cmake --build "$BUILD_DIR"
fi
