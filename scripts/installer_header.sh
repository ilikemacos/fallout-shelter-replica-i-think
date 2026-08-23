#!/bin/bash
#
# Haven — macOS installer
#
# A self-contained installer. Everything needed is embedded in this one file.
# It sets up an isolated Python environment, installs pygame + PyOpenGL, builds
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
    echo "    cd \"${SRC_DIR}\" && python3 -m pip install pygame PyOpenGL && python3 run.py"
    exit 0
fi

# ----- python environment -----
say "Creating isolated Python environment"
if [ ! -x "${VENV_DIR}/bin/python" ]; then
    "$PYTHON" -m venv "$VENV_DIR" || die "could not create a virtualenv"
fi
VPY="${VENV_DIR}/bin/python"

say "Installing dependencies (pygame, PyOpenGL, pyinstaller, pillow)"
"$VPY" -m pip install --upgrade pip >/dev/null 2>&1 || true
if ! "$VPY" -m pip install --upgrade "pygame>=2.5,<3" PyOpenGL PyOpenGL-accelerate \
        pyinstaller pillow; then
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
