#!/usr/bin/env bash
# Builds dist/6767.sh by embedding the current source tree (as a
# gzip+base64 tarball) into the installer template below. Run this whenever
# src/, CMakeLists.txt, cmake/ or shaders/ change, and commit the result —
# dist/6767.sh is the actual shipped deliverable; this script is
# maintainer tooling, not something an end user ever runs.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

OUT="dist/6767.sh"
PAYLOAD_TAR="$(mktemp -t haven_payload.XXXXXX.tar.gz)"
trap 'rm -f "$PAYLOAD_TAR"' EXIT

tar --exclude='.DS_Store' -czf "$PAYLOAD_TAR" \
  CMakeLists.txt cmake shaders src tests LICENSE

PAYLOAD_B64="$(base64 < "$PAYLOAD_TAR" | tr -d '\n')"
PAYLOAD_SIZE=$(wc -c < "$PAYLOAD_TAR" | tr -d ' ')
VERSION="$(grep -m1 'project(Haven VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"

mkdir -p dist
{
  sed "s/__HAVEN_VERSION__/${VERSION}/g; s/__HAVEN_PAYLOAD_SIZE__/${PAYLOAD_SIZE}/g" scripts/installer_template.sh
  echo "HAVEN_PAYLOAD_B64='${PAYLOAD_B64}'"
  echo "main \"\$@\""
} > "$OUT"
chmod +x "$OUT"

echo "Wrote $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes, payload $PAYLOAD_SIZE bytes)"
