#!/usr/bin/env bash
# Runs web/index.html's functional tests in headless Chromium (SwiftShader, so
# no GPU needed). Exits non-zero if any check fails.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
  for c in /opt/pw-browsers/chromium-*/chrome-linux/chrome \
           "$(command -v chromium || true)" "$(command -v google-chrome || true)" \
           "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"; do
    [ -x "$c" ] && CHROME="$c" && break
  done
fi
[ -n "$CHROME" ] || { echo "no chromium found; set CHROME=/path/to/chrome" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
python3 - "$TMP/test.html" <<'PY'
import sys
page = open('web/index.html').read()
harness = open('tools/web_test_harness.js').read()
anchor = "addEventListener('beforeunload',()=>{ if(running) save(); });"
assert anchor in page, "boot anchor not found in web/index.html"
page = page.replace(anchor, anchor + "\n" + harness)
open(sys.argv[1], 'w').write(page)
PY

OUT="$("$CHROME" --headless --no-sandbox --enable-unsafe-swiftshader \
  --use-gl=angle --use-angle=swiftshader --window-size=1280,760 \
  --virtual-time-budget=12000 --dump-dom "file://$TMP/test.html" 2>/dev/null \
  | grep -oE '<title>[^<]*</title>' | head -1 | sed 's/<[^>]*>//g')"

echo "$OUT" | sed 's/ ;; /\n/g'
# NB: `grep -o` exits 1 when it finds nothing, which under `set -o pipefail`
# would abort this script precisely when every test passed. Hence `|| true`.
FAILS=$( (echo "$OUT" | grep -o 'FAIL' || true) | wc -l | tr -d ' ')
PASSES=$( (echo "$OUT" | grep -o 'PASS' || true) | wc -l | tr -d ' ')
echo "---"
echo "$PASSES passed, $FAILS failed"
[ "$FAILS" = "0" ] && [ "$PASSES" -gt 0 ]
