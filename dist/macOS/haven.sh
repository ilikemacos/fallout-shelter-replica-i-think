#!/bin/bash
#
# Haven — macOS installer
#
# A self-contained installer. Everything needed is embedded in this one file.
# It sets up an isolated Python environment, installs pygame, builds
# Haven.app with PyInstaller, and installs it into ~/Applications.
#
# Usage:
#     chmod +x haven.sh
#     ./haven.sh                 # install (builds Haven.app)
#     ./haven.sh --run           # install then launch immediately
#     ./haven.sh --source-only   # just unpack the source, no app build
#     ./haven.sh --uninstall     # remove Haven.app and support files
#     ./haven.sh --keep-saves    # (with --uninstall) keep save files
#
# No administrator privileges are required. Nothing is installed system-wide.
#
set -euo pipefail

APP_NAME="Haven"
INSTALL_ROOT="${HOME}/Library/Application Support/Haven"
SRC_DIR="${INSTALL_ROOT}/src"
VENV_DIR="${INSTALL_ROOT}/venv"
APPS_DIR="${HOME}/Applications"
APP_PATH="${APPS_DIR}/${APP_NAME}.app"

RUN_AFTER=0
SOURCE_ONLY=0
UNINSTALL=0
KEEP_SAVES=0

for arg in "$@"; do
    case "$arg" in
        --run)          RUN_AFTER=1 ;;
        --source-only)  SOURCE_ONLY=1 ;;
        --uninstall)    UNINSTALL=1 ;;
        --keep-saves)   KEEP_SAVES=1 ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "Unknown option: $arg  (try --help)" >&2
            exit 1 ;;
    esac
done

# ----- pretty output -----
if [ -t 1 ]; then
    B=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'
    RED=$'\033[31m'; RST=$'\033[0m'
else
    B=""; DIM=""; GRN=""; YEL=""; RED=""; RST=""
fi
say()  { printf "%s==>%s %s\n" "$GRN$B" "$RST" "$*"; }
warn() { printf "%s==>%s %s\n" "$YEL$B" "$RST" "$*"; }
die()  { printf "%serror:%s %s\n" "$RED$B" "$RST" "$*" >&2; exit 1; }

# ----- uninstall path -----
if [ "$UNINSTALL" -eq 1 ]; then
    say "Uninstalling ${APP_NAME}"
    for target in "$APP_PATH" "$SRC_DIR" "$VENV_DIR" \
                  "${INSTALL_ROOT}/buildout" "${INSTALL_ROOT}/buildwork"; do
        if [ -d "$target" ]; then
            rm -rf "$target"
            echo "    removed ${target}"
        fi
    done
    if [ "$KEEP_SAVES" -eq 1 ]; then
        echo "    kept save files in ${INSTALL_ROOT}/saves"
    else
        rm -rf "${INSTALL_ROOT}/saves" "${INSTALL_ROOT}/backups" \
               "${INSTALL_ROOT}/settings.json" 2>/dev/null || true
        echo "    removed save files"
    fi
    rmdir "$INSTALL_ROOT" 2>/dev/null || true
    say "Done."
    exit 0
fi

# ----- preflight -----
[ "$(uname -s)" = "Darwin" ] || die "This installer is for macOS. On Windows use scripts\\build_windows.bat."

OS_MAJOR=$(sw_vers -productVersion | cut -d. -f1)
if [ "$OS_MAJOR" -lt 13 ]; then
    warn "macOS 13 or newer is recommended (you have $(sw_vers -productVersion)). Continuing anyway."
fi

ARCH=$(uname -m)
say "${APP_NAME} installer — macOS $(sw_vers -productVersion) ${ARCH}"

# Find a usable Python 3.10+
PYTHON=""
for cand in python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$cand" >/dev/null 2>&1; then
        ver=$("$cand" -c 'import sys; print("%d%02d" % sys.version_info[:2])' 2>/dev/null || echo 0)
        if [ "$ver" -ge 310 ]; then PYTHON=$(command -v "$cand"); break; fi
    fi
done

if [ -z "$PYTHON" ]; then
    cat >&2 <<'EOF'

Python 3.10 or newer was not found on this Mac.

The macOS system Python is not sufficient. Install a real Python first:

  * Download the macOS installer from  https://www.python.org/downloads/
    (pick the latest 3.x "macOS 64-bit universal2 installer"), or
  * If you use Homebrew:   brew install python@3.12

Then re-run this installer.

EOF
    exit 1
fi
say "Using Python: ${PYTHON} ($("$PYTHON" -V 2>&1))"

# Xcode command line tools are needed for iconutil (icon) — optional
HAVE_ICONUTIL=0
if command -v iconutil >/dev/null 2>&1; then HAVE_ICONUTIL=1; fi

# ----- unpack embedded payload -----
say "Unpacking game source to ${SRC_DIR}"
mkdir -p "$SRC_DIR"
rm -rf "${SRC_DIR:?}/haven" "${SRC_DIR:?}/scripts"

PAYLOAD_LINE=$(awk '/^__HAVEN_PAYLOAD_BELOW__$/ {print NR + 1; exit 0; }' "$0")
[ -n "$PAYLOAD_LINE" ] || die "installer is corrupt (no payload marker)"
tail -n "+${PAYLOAD_LINE}" "$0" | base64 --decode | tar -xzf - -C "$SRC_DIR" \
    || die "failed to unpack the embedded payload"
[ -f "${SRC_DIR}/run.py" ] || die "payload unpacked but run.py is missing"

if [ "$SOURCE_ONLY" -eq 1 ]; then
    say "Source unpacked. Run it with:"
    echo "    cd \"${SRC_DIR}\" && python3 -m pip install pygame && python3 run.py"
    exit 0
fi

# ----- python environment -----
say "Creating isolated Python environment"
if [ ! -x "${VENV_DIR}/bin/python" ]; then
    "$PYTHON" -m venv "$VENV_DIR" || die "could not create a virtualenv"
fi
VPY="${VENV_DIR}/bin/python"

say "Installing dependencies (pygame, pyinstaller, pillow)"
"$VPY" -m pip install --upgrade pip >/dev/null 2>&1 || true
if ! "$VPY" -m pip install --upgrade "pygame>=2.5,<3" pyinstaller pillow; then
    die "dependency installation failed — check your network connection"
fi

# ----- build the .app -----
say "Building ${APP_NAME}.app (this takes a minute)"
cd "$SRC_DIR"
BUILD_OUT="${INSTALL_ROOT}/buildout"
rm -rf "$BUILD_OUT"
mkdir -p "$BUILD_OUT"

ICON_ARGS=()
if [ "$HAVE_ICONUTIL" -eq 1 ]; then
    if "$VPY" - <<'PYICON' >/dev/null 2>&1
import os, subprocess, sys
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
import pygame
from PIL import Image
pygame.init(); pygame.display.set_mode((1, 1))
sys.path.insert(0, os.getcwd())
from haven import assets as G
iconset = os.path.join(os.getcwd(), "Haven.iconset")
os.makedirs(iconset, exist_ok=True)
for size in (16, 32, 64, 128, 256, 512, 1024):
    surf = G.app_icon(size)
    im = Image.frombytes("RGBA", surf.get_size(), pygame.image.tobytes(surf, "RGBA"))
    im.save(os.path.join(iconset, "icon_%dx%d.png" % (size, size)))
    if size <= 512:
        im.save(os.path.join(iconset, "icon_%dx%d@2x.png" % (size, size)))
subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o",
                       os.path.join(os.getcwd(), "Haven.icns")])
PYICON
    then
        ICON_ARGS=(--icon "${SRC_DIR}/Haven.icns")
    else
        warn "icon generation failed; building with the default icon"
    fi
fi

if ! "$VPY" -m PyInstaller \
        --noconfirm --windowed --clean \
        --name "$APP_NAME" \
        --osx-bundle-identifier app.haven.game \
        --distpath "$BUILD_OUT" \
        --workpath "${INSTALL_ROOT}/buildwork" \
        --specpath "${INSTALL_ROOT}/buildwork" \
        ${ICON_ARGS[@]+"${ICON_ARGS[@]}"} \
        run.py; then
    die "PyInstaller build failed"
fi

[ -d "${BUILD_OUT}/${APP_NAME}.app" ] || die "build finished but ${APP_NAME}.app is missing"

# ----- install -----
say "Installing to ${APP_PATH}"
mkdir -p "$APPS_DIR"
rm -rf "$APP_PATH"
cp -R "${BUILD_OUT}/${APP_NAME}.app" "$APP_PATH"

# Strip the quarantine flag so Gatekeeper does not block our own local build
xattr -dr com.apple.quarantine "$APP_PATH" 2>/dev/null || true

# Clean intermediates, keep the source for rebuilds
rm -rf "$BUILD_OUT" "${INSTALL_ROOT}/buildwork" \
       "${SRC_DIR}/Haven.iconset" "${SRC_DIR}/build" 2>/dev/null || true

# ----- also emit a portable zip next to the installer -----
ZIP_DEST="$(cd "$(dirname "$0")" && pwd)/Haven-macOS.zip"
if command -v ditto >/dev/null 2>&1; then
    if ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$ZIP_DEST" 2>/dev/null; then
        say "Portable zip written to ${ZIP_DEST}"
    else
        warn "could not write the portable zip (continuing)"
    fi
fi

cat <<EOF

${GRN}${B}${APP_NAME} is installed.${RST}

  App:    ${APP_PATH}
  Saves:  ${INSTALL_ROOT}/saves
  Source: ${SRC_DIR}

Launch it from Finder (Go → Applications in your home folder), from
Spotlight, or from this terminal:

    open "${APP_PATH}"

To remove everything later:

    ./haven.sh --uninstall

EOF

if [ "$RUN_AFTER" -eq 1 ]; then
    say "Launching ${APP_NAME}"
    open "$APP_PATH"
fi

exit 0
__HAVEN_PAYLOAD_BELOW__
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
HSeSLxDNIy6RRL3C2/1Txfe71rkq9b6ovpSq8+9zVNGvfv8/4/6vvrKRsv9d2Xj+7MH+91f5AIEi
bNrVOmSr2CkRP1jfYBZB9NxEtC+J7SkUjmGDGIaKA4ZE0b4lRs8BfwxjfkJFv0eyD/TmZRYVYVHL
CnLiZMfqvHE453WBbXxhHawCa4qaOmROE8dV1+9MmG8QRzKWU4roVBSKE9ENxlB9Ov2CJzyNfhVF
pQ/AWHmK8yJmoT9JBK3vewNPtcAsEaYgLMDTPHTL3E+2RPR6mjUjqjNp973wshwrbFx2Ou3LXDK3
UoOXJO1r6ObBJKrkHrp3KumGj7mB14xMUcji2aXKaRKNxAsLPWIQxL8KwyXJzucWcTBr75qe35cr
FBAvzZqIvOq04SAR3RJAXeR11EGABRjFq6pekexFNKntqgmTIH6OMZwAzTOJ9ED2iY9mgT01TETT
eNOwmkevTt/tnDSs/aZ1fHIEp9s9y95p0m+7bL3bP30Dy2MqcbJzePqTdfTK2jn8yfrT/uEesY8/
Hp80mk3r6KSw//b4YL9Bz8S+c//wtfWS6h0eERrvEzIT0NMjCw0qUPuNJoC9bZzsvqGfOy/3D/ZP
fyoXXu2fHgLmq6MTa4eOpZPT/d2zg50T6/js5Pio2aDm9wjs4f7hqxNqpfG2cXhapVbpmdX4gX5Y
zTc7BwdoqrBzRr0/Qf+s3aPjn072X785td5wlNqm9bJBPdt5edCQpmhQuwc7+2/L1t7O253XDa51
RFBOCigmvbPevWngEdrbof92T/fp9KJh7B4dnp7QzzKN8uQ0qvpuv9koWzsn+01MyKuTo7dlNuSm
GkcMhOodNgQKptpKrAgVwe+zZiMCaO01dg4IVhOVMURduPpgIvDwefg8fB4+D5+Hz8Pn4fPwefg8
fB4+D5+Hz8Pn4fPwefg8fB4+D5+Hz8Pn4fPwefg8fB4+v8nP/wevNoSQACADAA==
