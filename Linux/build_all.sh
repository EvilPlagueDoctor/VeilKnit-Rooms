#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/out/linux-release"
DIST="$ROOT/dist/linux"
command -v cmake >/dev/null || { echo "Install cmake."; exit 1; }
command -v g++ >/dev/null || { echo "Install build-essential."; exit 1; }
pkg-config --exists openssl || { echo "Install libssl-dev and pkg-config."; exit 1; }
cmake -S "$ROOT/Desktop" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DVKROOMS_BUILD_TESTS=ON -DVKROOMS_BUILD_LINUX_GUI=ON -DVKROOMS_PRIVACY_RELEASE=ON
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
mkdir -p "$DIST"
install -m 755 "$BUILD/veilknit-rooms-console" "$DIST/veilknit-rooms-console"
if [[ -f "$BUILD/veilknit-rooms-gui" ]]; then install -m 755 "$BUILD/veilknit-rooms-gui" "$DIST/veilknit-rooms-gui"; fi
strip --strip-all "$DIST"/* 2>/dev/null || true
echo "Built Linux Rooms applications in $DIST"
