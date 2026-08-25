#!/usr/bin/env bash
# =============================================================================
#  Haven: Deep Shelter — installer
#
#  This is the ONLY installer you need. It embeds the game's full source
#  (as a compressed archive further down this file) and builds it on your
#  own Apple Silicon Mac, so the result is a real, native, optimized build —
#  not a generic download. Just run:
#
#      ~/Downloads/6767.sh
#
#  It will detect your Mac, fetch a build toolchain if you don't already
#  have one, compile Haven in Release mode, verify the build by actually
#  running its test suite, install Haven.app, and tell you where to launch
#  it from. Running it again later rebuilds/updates your install safely —
#  your saves live outside the app bundle and are never touched.
#
#  Version __HAVEN_VERSION__ · embedded source payload: __HAVEN_PAYLOAD_SIZE__ bytes
# =============================================================================
set -euo pipefail

# ---- appearance -------------------------------------------------------------
if [ -t 1 ]; then
  C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
  C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_BLUE=$'\033[36m'
else
  C_RESET=""; C_BOLD=""; C_DIM=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_BLUE=""
fi
step()  { printf '%s\n' "${C_BOLD}${C_BLUE}==>${C_RESET} ${C_BOLD}$*${C_RESET}"; }
info()  { printf '%s\n' "    $*"; }
ok()    { printf '%s\n' "    ${C_GREEN}✓${C_RESET} $*"; }
warn()  { printf '%s\n' "    ${C_YELLOW}!${C_RESET} $*" >&2; }
fail()  { printf '%s\n' "${C_RED}${C_BOLD}error:${C_RESET} $*" >&2; exit 1; }

HAVEN_APP_NAME="Haven"
HAVEN_BUNDLE_ID="org.haven.deepshelter"
HAVEN_SUPPORT_DIR="$HOME/Library/Application Support/Haven"
HAVEN_TOOLS_DIR="$HAVEN_SUPPORT_DIR/tools"
HAVEN_SRC_DIR="$HAVEN_SUPPORT_DIR/build-source"
HAVEN_BUILD_DIR="$HAVEN_SUPPORT_DIR/build-output"
HAVEN_LOG_DIR="$HAVEN_SUPPORT_DIR/install-logs"
CMAKE_VERSION="3.28.3"
MIN_MACOS_MAJOR=12

have_cmd() { command -v "$1" >/dev/null 2>&1; }

# -----------------------------------------------------------------------------
#  1. Detect Apple Silicon
# -----------------------------------------------------------------------------
detect_platform() {
  step "Checking your Mac"
  if [ "$(uname -s)" != "Darwin" ]; then
    fail "Haven only installs on macOS. This looks like $(uname -s)."
  fi
  local arch
  arch="$(uname -m)"
  if [ "$arch" != "arm64" ]; then
    fail "Haven requires Apple Silicon (M1 or newer). This Mac reports '$arch' (Intel), which is not supported. There is no Intel build — Haven is built specifically for Apple Silicon's unified memory architecture."
  fi
  local brand
  brand="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "Apple Silicon")"
  ok "Apple Silicon detected: $brand"
}

# -----------------------------------------------------------------------------
#  2. Detect macOS version
# -----------------------------------------------------------------------------
detect_macos_version() {
  step "Checking macOS version"
  local ver major
  ver="$(sw_vers -productVersion)"
  major="${ver%%.*}"
  if [ "$major" -lt "$MIN_MACOS_MAJOR" ]; then
    fail "Haven needs macOS $MIN_MACOS_MAJOR (Monterey) or newer. This Mac is running macOS $ver."
  fi
  ok "macOS $ver"
}

# -----------------------------------------------------------------------------
#  3. Ensure a compiler toolchain (Xcode Command Line Tools) is present,
#     installing it automatically when possible.
# -----------------------------------------------------------------------------
ensure_compiler() {
  step "Checking for a C++ compiler"
  if xcode-select -p >/dev/null 2>&1 && have_cmd clang++; then
    ok "Compiler toolchain already present ($(xcode-select -p))"
    return
  fi

  info "No Xcode Command Line Tools found — attempting an automatic, silent install."
  info "(This is Apple's own compiler toolchain; Haven needs it to build native code."
  info " No full Xcode.app or App Store account is required, only the small CLT package.)"

  # The well-known headless trick: dropping this marker file makes
  # `softwareupdate` list the Command Line Tools package so it can be
  # installed non-interactively, without popping the GUI installer.
  local marker="/tmp/.com.apple.dt.CommandLineTools.installondemand.in-progress"
  touch "$marker" 2>/dev/null || true
  local clt_label
  clt_label="$(softwareupdate -l 2>/dev/null | awk -F'\* ' '/Command Line Tools/ { print $2 }' | tail -1 | sed 's/^ *//;s/ *$//')"
  if [ -n "$clt_label" ]; then
    info "Installing: $clt_label (this can take a few minutes)"
    if softwareupdate -i "$clt_label" >/tmp/haven_clt_install.log 2>&1; then
      rm -f "$marker"
      if have_cmd clang++; then
        ok "Command Line Tools installed automatically."
        return
      fi
    fi
  fi
  rm -f "$marker" 2>/dev/null || true

  # Automatic path failed (offline, no admin rights, Apple changed the
  # catalog, etc). Fall back to the interactive installer and wait for it —
  # this is the one step Haven genuinely cannot force to be silent, since
  # Apple's own installer requires a person to click "Install".
  warn "Automatic install did not complete — opening Apple's installer."
  info "A window titled 'Install Command Line Tools' should appear."
  info "Click Install there, accept the license, and let it finish, then re-run this script."
  xcode-select --install 2>/dev/null || true
  fail "Waiting on Xcode Command Line Tools. Run ~/Downloads/6767.sh again once that install finishes."
}

# -----------------------------------------------------------------------------
#  4. Ensure CMake. If missing, fetch Kitware's official prebuilt binary —
#     no Homebrew, no package manager, no sudo required.
# -----------------------------------------------------------------------------
ensure_cmake() {
  step "Checking for CMake"
  if have_cmd cmake; then
    CMAKE_BIN="$(command -v cmake)"
    ok "Found system CMake: $(cmake --version | head -1)"
    return
  fi
  local cached="$HAVEN_TOOLS_DIR/cmake-$CMAKE_VERSION/CMake.app/Contents/bin/cmake"
  if [ -x "$cached" ]; then
    CMAKE_BIN="$cached"
    ok "Using previously downloaded CMake $CMAKE_VERSION"
    return
  fi

  info "CMake not found — downloading Kitware's official build (no Homebrew needed)."
  mkdir -p "$HAVEN_TOOLS_DIR"
  local url="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-macos-universal.tar.gz"
  local dest="$HAVEN_TOOLS_DIR/cmake-${CMAKE_VERSION}.tar.gz"
  if ! curl -fL --progress-bar "$url" -o "$dest"; then
    fail "Could not download CMake from $url. Check your internet connection, or install CMake yourself (e.g. 'brew install cmake') and run this installer again."
  fi
  mkdir -p "$HAVEN_TOOLS_DIR/cmake-$CMAKE_VERSION"
  tar -xzf "$dest" -C "$HAVEN_TOOLS_DIR/cmake-$CMAKE_VERSION" --strip-components=1
  rm -f "$dest"
  CMAKE_BIN="$HAVEN_TOOLS_DIR/cmake-$CMAKE_VERSION/CMake.app/Contents/bin/cmake"
  [ -x "$CMAKE_BIN" ] || fail "CMake download did not produce a usable binary at $CMAKE_BIN"
  ok "Downloaded CMake $CMAKE_VERSION"
}

# -----------------------------------------------------------------------------
#  5. Unpack the embedded source archive.
# -----------------------------------------------------------------------------
unpack_source() {
  step "Unpacking Haven's source"
  # Fresh checkout every run so a re-install always builds exactly what
  # this installer script contains, with nothing stale left behind — a
  # source file removed or renamed between versions must not linger and
  # get silently picked up by CMake's directory glob.
  rm -rf "${HAVEN_SRC_DIR:?}"
  mkdir -p "$HAVEN_SRC_DIR"
  local tarpath="$HAVEN_SUPPORT_DIR/.payload.tar.gz"
  mkdir -p "$HAVEN_SUPPORT_DIR"
  printf '%s' "$HAVEN_PAYLOAD_B64" | base64 --decode > "$tarpath"
  tar -xzf "$tarpath" -C "$HAVEN_SRC_DIR"
  rm -f "$tarpath"
  [ -f "$HAVEN_SRC_DIR/CMakeLists.txt" ] || fail "Embedded source did not unpack correctly."
  ok "Source unpacked to $HAVEN_SRC_DIR"
}

# -----------------------------------------------------------------------------
#  6-7. Configure and build in Release mode.
# -----------------------------------------------------------------------------
configure_and_build() {
  step "Configuring the build (Release, arm64)"
  mkdir -p "$HAVEN_BUILD_DIR" "$HAVEN_LOG_DIR"
  local cfg_log="$HAVEN_LOG_DIR/configure.log"
  if ! "$CMAKE_BIN" -S "$HAVEN_SRC_DIR" -B "$HAVEN_BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DHAVEN_BUILD_APP=ON \
        -DHAVEN_BUILD_TESTS=ON \
        > "$cfg_log" 2>&1; then
    warn "Configure failed — last 40 lines of $cfg_log:"
    tail -40 "$cfg_log" >&2
    fail "CMake configuration failed. Full log: $cfg_log"
  fi
  ok "Configured"

  step "Building Haven (this compiles the whole engine — a few minutes on first run)"
  local build_log="$HAVEN_LOG_DIR/build.log"
  local jobs
  jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  if ! "$CMAKE_BIN" --build "$HAVEN_BUILD_DIR" --config Release -j "$jobs" > "$build_log" 2>&1; then
    warn "Build failed — last 60 lines of $build_log:"
    tail -60 "$build_log" >&2
    fail "Build failed. Full log: $build_log"
  fi
  ok "Build complete"
}

# -----------------------------------------------------------------------------
#  8. Verify the build by actually running its test suite on this machine —
#     a real correctness check, not a rubber stamp.
# -----------------------------------------------------------------------------
verify_build() {
  step "Verifying the build (running Haven's test suite)"
  local tests_bin="$HAVEN_BUILD_DIR/haven_tests"
  [ -x "$tests_bin" ] || fail "Test binary was not produced at $tests_bin — build did not complete correctly."
  local test_log="$HAVEN_LOG_DIR/tests.log"
  if ! TMPDIR="${TMPDIR:-/tmp}" "$tests_bin" > "$test_log" 2>&1; then
    warn "Test suite failed — output:"
    cat "$test_log" >&2
    fail "The build did not pass its own test suite. Full log: $test_log"
  fi
  ok "$(tail -1 "$test_log")"

  local app_binary="$HAVEN_BUILD_DIR/${HAVEN_APP_NAME}.app/Contents/MacOS/${HAVEN_APP_NAME}"
  [ -x "$app_binary" ] || fail "Haven.app binary was not produced at $app_binary"
  if have_cmd file; then
    local filetype
    filetype="$(file -b "$app_binary")"
    case "$filetype" in
      *arm64*) ok "Haven binary is native arm64 ($filetype)" ;;
      *) fail "Haven binary is not arm64 ($filetype) — refusing to install a non-native build." ;;
    esac
  fi
}

# -----------------------------------------------------------------------------
#  9. Install Haven.app.
# -----------------------------------------------------------------------------
install_app() {
  step "Installing ${HAVEN_APP_NAME}.app"
  local built="$HAVEN_BUILD_DIR/${HAVEN_APP_NAME}.app"
  local target_dir="/Applications"
  if [ ! -w "$target_dir" ]; then
    target_dir="$HOME/Applications"
    mkdir -p "$target_dir"
  fi
  local dest="$target_dir/${HAVEN_APP_NAME}.app"

  rm -rf "$dest"
  cp -R "$built" "$dest"
  # Ad-hoc sign so Gatekeeper treats it as a locally built app rather than
  # an unsigned stranger — no paid developer account needed for this.
  if have_cmd codesign; then
    codesign --force --deep --sign - "$dest" >/dev/null 2>&1 || \
      warn "Ad-hoc codesign failed (non-fatal) — macOS may show an extra confirmation on first launch."
  fi
  xattr -dr com.apple.quarantine "$dest" 2>/dev/null || true

  HAVEN_INSTALLED_PATH="$dest"
  ok "Installed to $dest"
}

# -----------------------------------------------------------------------------
#  10. A convenient launcher: a `haven` command on the PATH, alongside the
#      .app itself (which is already double-click/Spotlight launchable).
# -----------------------------------------------------------------------------
create_launcher() {
  step "Creating a command-line launcher"
  local bin_dir="$HOME/.local/bin"
  mkdir -p "$bin_dir"
  local launcher="$bin_dir/haven"
  cat > "$launcher" <<LAUNCH
#!/usr/bin/env bash
exec open "$HAVEN_INSTALLED_PATH"
LAUNCH
  chmod +x "$launcher"
  ok "Launcher: $launcher  (also just open $HAVEN_APP_NAME from Spotlight/Launchpad)"

  case ":$PATH:" in
    *":$bin_dir:"*) ;;
    *) warn "$bin_dir isn't on your PATH yet — add it to your shell profile to use the 'haven' command, e.g.:"
       info "  echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.zshrc" ;;
  esac
}

# -----------------------------------------------------------------------------
#  11. Final verification of the installed copy.
# -----------------------------------------------------------------------------
verify_install() {
  step "Verifying the installed app"
  [ -d "$HAVEN_INSTALLED_PATH" ] || fail "Installed app is missing at $HAVEN_INSTALLED_PATH"
  [ -f "$HAVEN_INSTALLED_PATH/Contents/Info.plist" ] || fail "Installed app bundle looks incomplete (no Info.plist)"
  [ -x "$HAVEN_INSTALLED_PATH/Contents/MacOS/${HAVEN_APP_NAME}" ] || fail "Installed app has no executable"
  ok "Installed app looks correct"
}

# -----------------------------------------------------------------------------
main() {
  printf '\n%s\n' "${C_BOLD}Haven: Deep Shelter — installer (v__HAVEN_VERSION__)${C_RESET}"
  printf '%s\n\n' "${C_DIM}A 3D underground shelter-management game for Apple Silicon Macs.${C_RESET}"

  detect_platform
  detect_macos_version
  ensure_compiler
  ensure_cmake
  unpack_source
  configure_and_build
  verify_build
  install_app
  create_launcher
  verify_install

  printf '\n%s\n' "${C_GREEN}${C_BOLD}Haven is installed.${C_RESET}"
  info "Launch it from: ${C_BOLD}$HAVEN_INSTALLED_PATH${C_RESET}"
  info "Or run:          ${C_BOLD}open \"$HAVEN_INSTALLED_PATH\"${C_RESET}"
  info "Or, once ~/.local/bin is on your PATH: ${C_BOLD}haven${C_RESET}"
  info "Save files live under: $HOME/Library/Application Support/Haven"
  info "Re-run this installer any time to rebuild/update — your saves are untouched."
  printf '\n'

  read -r -p "Launch Haven now? [Y/n] " reply || true
  case "${reply:-Y}" in
    [Yy]*|"") open "$HAVEN_INSTALLED_PATH" ;;
    *) ;;
  esac
}

# The embedded source payload is appended below by scripts/make_installer.sh
# as: HAVEN_PAYLOAD_B64='...base64...'
# followed by: main "$@"
