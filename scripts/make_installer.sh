#!/usr/bin/env bash
# Builds a randomly-named installer in dist/ by embedding the current source tree (as a
# gzip+base64 tarball) into the installer template below. Run this whenever
# src/, CMakeLists.txt, cmake/ or shaders/ change, and commit the result —
# the generated dist/*.sh is the actual shipped deliverable; this script is
# maintainer tooling, not something an end user ever runs.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# The installer filename is randomised on every build, by request. Any
# previously generated installer in dist/ is removed so only the current one
# is ever present and there is no ambiguity about which to run.
# NB: no pipeline here — `tr </dev/urandom | head -c N` makes head close the
# pipe early, tr dies of SIGPIPE, and `set -o pipefail` then aborts the script.
random_name() {
  local chars=abcdefghijklmnopqrstuvwxyz
  local out="" i
  for i in 1 2 3 4 5 6; do out+="${chars:$((RANDOM % 26)):1}"; done
  printf '%s.sh' "$out"
}
INSTALLER_NAME="${HAVEN_INSTALLER_NAME:-$(random_name)}"
OUT="dist/${INSTALLER_NAME}"
PAYLOAD_TAR="$(mktemp -t haven_payload.XXXXXX.tar.gz)"
trap 'rm -f "$PAYLOAD_TAR"' EXIT

tar --exclude='.DS_Store' -czf "$PAYLOAD_TAR" \
  CMakeLists.txt cmake shaders src tests tools LICENSE

PAYLOAD_B64="$(base64 < "$PAYLOAD_TAR" | tr -d '\n')"
PAYLOAD_SIZE=$(wc -c < "$PAYLOAD_TAR" | tr -d ' ')
VERSION="$(grep -m1 'project(Haven VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"

mkdir -p dist
find dist -maxdepth 1 -name '*.sh' -delete
{
  sed "s/__HAVEN_VERSION__/${VERSION}/g; s/__HAVEN_PAYLOAD_SIZE__/${PAYLOAD_SIZE}/g; s/__HAVEN_INSTALLER_NAME__/${INSTALLER_NAME}/g" scripts/installer_template.sh
  echo "HAVEN_PAYLOAD_B64='${PAYLOAD_B64}'"
  echo "main \"\$@\""
} > "$OUT"
chmod +x "$OUT"

echo "Wrote $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes, payload $PAYLOAD_SIZE bytes)"
