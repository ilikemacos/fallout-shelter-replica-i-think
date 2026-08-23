#!/bin/sh
#
# Haven — one-file installer
#
# The entire game is embedded in this script. It finds a Python, unpacks the
# source into your user data directory, creates an isolated virtualenv, and
# writes a `haven` launcher. Nothing is installed system-wide and no
# administrator password is required.
#
#   chmod +x shelter.sh
#   ./shelter.sh --run
#
# Flags:
#   (none)              install
#   --run               install, then launch
#   --app               ALSO build a native Haven.app / Haven.exe bundle
#                       with PyInstaller (slower; needs pyinstaller + pillow)
#   --source-only       unpack the source only, skip the environment
#   --uninstall         remove Haven
#   --uninstall --keep-saves   ... but keep save files
#   --help              show this text
#
# Works on macOS 13+ and on Linux. Requires Python 3.10 or newer.

set -eu

APP_NAME="Haven"

RUN_AFTER=0
SOURCE_ONLY=0
UNINSTALL=0
KEEP_SAVES=0
BUILD_APP=0
SHOW_HELP=0

for arg in "$@"; do
    case "$arg" in
        --run)          RUN_AFTER=1 ;;
        --app)          BUILD_APP=1 ;;
        --source-only)  SOURCE_ONLY=1 ;;
        --uninstall)    UNINSTALL=1 ;;
        --keep-saves)   KEEP_SAVES=1 ;;
        --help|-h)      SHOW_HELP=1 ;;
        *) printf 'Unknown option: %s (try --help)\n' "$arg" >&2; exit 2 ;;
    esac
done

# ----- pretty output -----
if [ -t 1 ]; then
    B=$(printf '\033[1m'); RST=$(printf '\033[0m')
    GRN=$(printf '\033[32m'); YEL=$(printf '\033[33m'); RED=$(printf '\033[31m')
else
    B=""; RST=""; GRN=""; YEL=""; RED=""
fi
say()  { printf "%s==>%s %s\n" "$GRN$B" "$RST" "$*"; }
warn() { printf "%s  ! %s%s\n" "$YEL" "$*" "$RST"; }
die()  { printf "%serror:%s %s\n" "$RED$B" "$RST" "$*" >&2; exit 1; }

if [ "$SHOW_HELP" -eq 1 ]; then
    # Print the leading comment block, stopping at the first line that is not
    # a comment, so the help text cannot drift out of sync with line numbers.
    awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 0
fi

# ----- platform -----
OS=$(uname -s)
case "$OS" in
    Darwin)
        PLATFORM="macos"
        INSTALL_ROOT="${HOME}/Library/Application Support/${APP_NAME}"
        SAVE_DIR="$INSTALL_ROOT"
        LAUNCHER_EXTRA="${HOME}/Applications/${APP_NAME}.command"
        ;;
    Linux)
        PLATFORM="linux"
        INSTALL_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/${APP_NAME}"
        SAVE_DIR="$INSTALL_ROOT"
        LAUNCHER_EXTRA="${XDG_DATA_HOME:-$HOME/.local/share}/applications/haven.desktop"
        ;;
    *)
        die "Unsupported system '$OS'. This installer supports macOS and Linux."
        ;;
esac

SRC_DIR="${INSTALL_ROOT}/src"
VENV_DIR="${INSTALL_ROOT}/venv"
BIN_DIR="${HOME}/.local/bin"
LAUNCHER="${BIN_DIR}/haven"

# ----- uninstall -----
if [ "$UNINSTALL" -eq 1 ]; then
    say "Uninstalling ${APP_NAME}"
    for p in "$SRC_DIR" "$VENV_DIR" "$LAUNCHER" "$LAUNCHER_EXTRA" \
             "${HOME}/Applications/${APP_NAME}.app"; do
        if [ -e "$p" ]; then rm -rf "$p"; printf '    removed %s\n' "$p"; fi
    done
    if [ "$KEEP_SAVES" -eq 1 ]; then
        printf '    kept save files in %s\n' "$SAVE_DIR"
    else
        for f in "${SAVE_DIR}/saves" "${SAVE_DIR}/backups" "${SAVE_DIR}/settings.json"; do
            if [ -e "$f" ]; then rm -rf "$f"; printf '    removed %s\n' "$f"; fi
        done
    fi
    rmdir "$INSTALL_ROOT" 2>/dev/null || true
    say "Done."
    exit 0
fi

if [ "$PLATFORM" = "macos" ]; then
    OS_MAJOR=$(sw_vers -productVersion 2>/dev/null | cut -d. -f1 || echo 0)
    if [ "${OS_MAJOR:-0}" -lt 13 ] 2>/dev/null; then
        warn "macOS 13 or newer is recommended (found $(sw_vers -productVersion 2>/dev/null))"
    fi
    say "${APP_NAME} installer — macOS $(sw_vers -productVersion 2>/dev/null) $(uname -m)"
else
    say "${APP_NAME} installer — Linux $(uname -m)"
fi

# ----- find a usable python -----
PYTHON=""
for cand in python3.13 python3.12 python3.11 python3.10 python3 python; do
    if command -v "$cand" >/dev/null 2>&1; then
        if "$cand" -c 'import sys; sys.exit(0 if sys.version_info[:2] >= (3,10) else 1)' \
           >/dev/null 2>&1; then
            PYTHON=$(command -v "$cand")
            break
        fi
    fi
done
if [ -z "$PYTHON" ]; then
    printf '\n'
    if [ "$PLATFORM" = "macos" ]; then
        die "No Python 3.10+ found.

Install one, then run this installer again:
  * https://www.python.org/downloads/macos/
  * or with Homebrew:  brew install python@3.12"
    else
        die "No Python 3.10+ found.

Install one, then run this installer again, for example:
  * Debian/Ubuntu:  sudo apt install python3 python3-venv
  * Fedora:         sudo dnf install python3"
    fi
fi
say "Using ${PYTHON} ($("$PYTHON" -V 2>&1))"

# ----- unpack the embedded payload -----
say "Unpacking the game into ${SRC_DIR}"
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"
PAYLOAD_LINE=$(awk '/^__HAVEN_PAYLOAD_BELOW__$/ { print NR + 1; exit 0 }' "$0")
[ -n "${PAYLOAD_LINE:-}" ] || die "This script is missing its embedded payload."
if ! tail -n "+${PAYLOAD_LINE}" "$0" | base64 -d 2>/dev/null | tar -xzf - -C "$SRC_DIR"; then
    # BSD base64 spells the decode flag -D
    tail -n "+${PAYLOAD_LINE}" "$0" | base64 -D | tar -xzf - -C "$SRC_DIR" \
        || die "Could not extract the embedded payload."
fi
printf '    unpacked %s files\n' "$(find "$SRC_DIR" -type f | wc -l | tr -d ' ')"

if [ "$SOURCE_ONLY" -eq 1 ]; then
    say "Source unpacked. To run it yourself:"
    printf '    cd "%s" && python3 -m pip install pygame PyOpenGL && python3 run.py\n' "$SRC_DIR"
    exit 0
fi

# ----- isolated environment -----
say "Creating an isolated Python environment"
rm -rf "$VENV_DIR"
"$PYTHON" -m venv "$VENV_DIR" 2>/dev/null || {
    if [ "$PLATFORM" = "linux" ]; then
        die "Could not create a virtualenv. On Debian/Ubuntu you may need:
  sudo apt install python3-venv"
    fi
    die "Could not create a virtualenv."
}
VPY="${VENV_DIR}/bin/python"
[ -x "$VPY" ] || die "The virtualenv looks incomplete."

say "Installing dependencies (pygame, PyOpenGL)"
"$VPY" -m pip install --upgrade pip >/dev/null 2>&1 || true
"$VPY" -m pip install --upgrade "pygame>=2.5,<3" PyOpenGL \
    || die "Could not install pygame / PyOpenGL."
# Optional compiled speedup for the GL renderer; never fatal.
"$VPY" -m pip install PyOpenGL-accelerate >/dev/null 2>&1 \
    || warn "PyOpenGL-accelerate unavailable (optional, ignoring)"

# ----- verify it actually runs before claiming success -----
say "Verifying the install"
# Point the check at a scratch data directory so it cannot touch real saves.
CHECK_DIR="${INSTALL_ROOT}/.check"
rm -rf "$CHECK_DIR"
if HAVEN_SELFTEST=1 HAVEN_DATA_DIR="$CHECK_DIR" \
   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
   "$VPY" "${SRC_DIR}/run.py" >/dev/null 2>&1; then
    printf '    selftest passed\n'
else
    warn "The headless selftest did not pass; the game may still run normally."
fi
rm -rf "$CHECK_DIR"

# ----- launcher -----
mkdir -p "$BIN_DIR"
cat > "$LAUNCHER" <<LAUNCH
#!/bin/sh
# Haven launcher — generated by the installer.
exec "${VPY}" "${SRC_DIR}/run.py" "\$@"
LAUNCH
chmod +x "$LAUNCHER"
say "Installed launcher: ${LAUNCHER}"

case ":${PATH}:" in
    *":${BIN_DIR}:"*) ;;
    *) warn "${BIN_DIR} is not on your PATH. Either run it by full path, or add:
      export PATH=\"\$HOME/.local/bin:\$PATH\"" ;;
esac

# A double-clickable entry so it can be launched without a terminal.
if [ "$PLATFORM" = "macos" ]; then
    mkdir -p "${HOME}/Applications"
    cat > "$LAUNCHER_EXTRA" <<CMD
#!/bin/sh
exec "${VPY}" "${SRC_DIR}/run.py"
CMD
    chmod +x "$LAUNCHER_EXTRA"
    xattr -d com.apple.quarantine "$LAUNCHER_EXTRA" 2>/dev/null || true
    printf '    double-click to play: %s\n' "$LAUNCHER_EXTRA"
else
    mkdir -p "$(dirname "$LAUNCHER_EXTRA")"
    cat > "$LAUNCHER_EXTRA" <<DESK
[Desktop Entry]
Type=Application
Name=Haven
Comment=Underground shelter management
Exec=${LAUNCHER}
Terminal=false
Categories=Game;Simulation;
DESK
    printf '    menu entry: %s\n' "$LAUNCHER_EXTRA"
fi

# ----- optional native bundle -----
if [ "$BUILD_APP" -eq 1 ]; then
    say "Building a native bundle with PyInstaller (this takes a few minutes)"
    "$VPY" -m pip install --upgrade pyinstaller pillow \
        || die "Could not install PyInstaller."
    if [ "$PLATFORM" = "macos" ]; then
        ( cd "$SRC_DIR" && "$VPY" scripts/build_macos.py ) \
            && BUILT="${SRC_DIR}/dist/macOS/Haven.app" || BUILT=""
        if [ -n "$BUILT" ] && [ -d "$BUILT" ]; then
            rm -rf "${HOME}/Applications/${APP_NAME}.app"
            cp -R "$BUILT" "${HOME}/Applications/"
            xattr -dr com.apple.quarantine "${HOME}/Applications/${APP_NAME}.app" 2>/dev/null || true
            say "Installed ${HOME}/Applications/${APP_NAME}.app"
        else
            warn "The bundle build did not finish; the launcher above still works."
        fi
    else
        warn "--app builds a bundle on macOS and Windows only; skipping on Linux."
    fi
fi

printf '\n'
say "${APP_NAME} is installed."
printf '    Play:    %s\n' "$LAUNCHER"
if [ "$PLATFORM" = "macos" ]; then
    printf '    Or double-click: %s\n' "$LAUNCHER_EXTRA"
fi
printf '    Saves:   %s\n' "${SAVE_DIR}/saves"
printf '    Remove:  ./shelter.sh --uninstall\n'
printf '\n'

if [ "$RUN_AFTER" -eq 1 ]; then
    say "Launching ${APP_NAME}"
    exec "$LAUNCHER"
fi
exit 0

__HAVEN_PAYLOAD_BELOW__
