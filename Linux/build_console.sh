#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/out/linux-console-release"
DIST="$ROOT/dist/linux"
cmake -S "$ROOT/Desktop" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DVKROOMS_BUILD_TESTS=ON -DVKROOMS_BUILD_LINUX_GUI=OFF -DVKROOMS_PRIVACY_RELEASE=ON
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
mkdir -p "$DIST"
install -m 755 "$BUILD/veilknit-rooms-console" "$DIST/veilknit-rooms-console"
strip --strip-all "$DIST/veilknit-rooms-console" 2>/dev/null || true
