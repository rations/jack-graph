#!/bin/bash
#
# Jack Graph installer (system-wide, /usr/local).
#
# Installs the runtime dependencies with the system package manager, adds the invoking user to
# the audio group (JACK realtime priority needs it), and installs the binary, its bundled fonts
# and the desktop entry.
#
# By default it uses the bundled prebuilt binary when it is for this machine's architecture and
# resolves its libraries here; otherwise (or with --from-source) it builds from the bundled
# source tree.
#
# Usage: sudo ./install.sh [--from-source] [--no-deps] [--help]

set -euo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

PREFIX="/usr/local"
BINDIR="$PREFIX/bin"
# respath.cpp finds the fonts at bin/../share/jack-graph, relative to the installed binary.
RESDIR="$PREFIX/share/jack-graph"
APPDIR="/usr/share/applications"

DO_DEPS=1
FROM_SOURCE=0
BUILD_TMP=""
SRC_BIN=""

usage()
{
  cat <<'EOF'
Jack Graph installer

Usage: sudo ./install.sh [options]

Options:
  --from-source   Build from the bundled source instead of using the prebuilt binary (also
                  done automatically if the prebuilt one cannot run on this system).
  --no-deps       Do not install system packages (install the dependencies yourself).
  --help          Show this help.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --from-source) FROM_SOURCE=1 ;;
    --no-deps) DO_DEPS=0 ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "install.sh: unknown option '$arg'" >&2
      usage >&2
      exit 2
      ;;
  esac
done

info() { printf '  %s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die()
{
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

cleanup() { [ -n "$BUILD_TMP" ] && rm -rf -- "$BUILD_TMP"; return 0; }
trap cleanup EXIT

if [ "$EUID" -ne 0 ]; then
  echo "Please run this script with sudo:"
  echo "  sudo ./install.sh"
  exit 1
fi

# The real user who invoked sudo, for the audio group.
REAL_USER="${SUDO_USER:-$USER}"

echo "Installing Jack Graph $(cat "$SELF_DIR/VERSION" 2>/dev/null || true)"

# ---- package manager --------------------------------------------------------

if command -v apt-get &>/dev/null; then
  PKG_MGR="apt"
elif command -v pacman &>/dev/null; then
  PKG_MGR="pacman"
elif command -v xbps-install &>/dev/null; then
  PKG_MGR="xbps"
elif command -v dnf &>/dev/null; then
  PKG_MGR="dnf"
else
  PKG_MGR="unknown"
fi
info "detected package manager: $PKG_MGR"

# No GTK any more: the window is X11 + Cairo + FreeType. alsa-utils is no longer needed either --
# the interface list comes from the ALSA control API, not from `aplay -l`.
install_runtime_deps()
{
  case "$PKG_MGR" in
    apt)
      apt-get update -qq
      # Hold qjackctl before installing jackd2 so apt does not pull it in as a recommendation.
      # jack-graph replaces it.
      apt-mark hold qjackctl 2>/dev/null || true
      apt-get install -y --no-install-recommends \
        jackd2 libjack-jackd2-0 libasound2 libcairo2 libfreetype6 libx11-6 \
        || warn "apt-get could not install all packages; install them manually."
      ;;
    pacman)
      # On Arch/Artix, jack2 and qjackctl are fully independent packages.
      pacman -Sy --noconfirm --needed jack2 alsa-lib cairo freetype2 libx11 \
        || warn "pacman could not install all packages; install them manually."
      ;;
    xbps)
      xbps-install -Sy jack alsa-lib cairo freetype libX11 \
        || warn "xbps could not install all packages; install them manually."
      ;;
    dnf)
      dnf install -y jack-audio-connection-kit alsa-lib cairo freetype libX11 \
        || warn "dnf could not install all packages; install them manually."
      ;;
    *)
      warn "could not detect a supported package manager."
      warn "Install these before running jack-graph:"
      warn "  jackd2 / jack2   (the JACK server, and libjack)"
      warn "  alsa-lib         (libasound)"
      warn "  cairo, freetype, libX11"
      ;;
  esac
}

install_build_deps()
{
  case "$PKG_MGR" in
    apt)
      apt-get install -y --no-install-recommends \
        g++ make pkg-config libjack-jackd2-dev libasound2-dev libcairo2-dev libfreetype-dev \
        libx11-dev || warn "apt-get could not install all build packages; install them manually."
      ;;
    pacman)
      pacman -S --noconfirm --needed base-devel pkgconf jack2 alsa-lib cairo freetype2 libx11 \
        || warn "pacman could not install all build packages; install them manually."
      ;;
    xbps)
      xbps-install -Sy gcc make pkg-config jack-devel alsa-lib-devel cairo-devel \
        freetype-devel libX11-devel \
        || warn "xbps could not install all build packages; install them manually."
      ;;
    dnf)
      dnf install -y gcc-c++ make pkgconf jack-audio-connection-kit-devel alsa-lib-devel \
        cairo-devel freetype-devel libX11-devel \
        || warn "dnf could not install all build packages; install them manually."
      ;;
    *)
      warn "install a C++17 compiler, make, pkg-config, and the development packages for"
      warn "  jack, alsa, cairo, freetype2 and x11."
      ;;
  esac
}

# ---- prebuilt or source -----------------------------------------------------

# True if the ELF binary $1 was built for this machine's architecture.
#
# The ldd check below cannot answer this: ldd on a foreign binary says "not a dynamic
# executable", never "not found", so an x86_64 tarball unpacked on a Pi would pass it. e_machine
# is the 2-byte field at offset 18 of the ELF header, read with od (coreutils) because readelf is
# not on a stock Pi.
prebuilt_arch_matches()
{
  local machine
  machine="$(od -An -t u2 -j 18 -N 2 -- "$1" 2>/dev/null | tr -d ' ')"
  case "$(uname -m):$machine" in
    x86_64:62 | amd64:62) return 0 ;;
    aarch64:183 | arm64:183) return 0 ;;
    *) return 1 ;;
  esac
}

# True if the prebuilt jack-graph exists, is for this architecture, and resolves all its
# libraries here.
prebuilt_runnable()
{
  local b="$SELF_DIR/bin/jack-graph"
  [ -f "$b" ] || return 1
  prebuilt_arch_matches "$b" || return 1
  command -v ldd >/dev/null 2>&1 || return 0 # can't check; assume usable
  ! ldd "$b" 2>/dev/null | grep -q 'not found'
}

build_from_source()
{
  local srcdir="$SELF_DIR/source"
  [ -f "$srcdir/Makefile" ] || die "no bundled source tree; cannot build from source."
  command -v make >/dev/null 2>&1 || die "make not found. Install build dependencies (drop --no-deps) and retry."

  step "Building from source"
  BUILD_TMP="$(mktemp -d)"
  cp -r -- "$srcdir/." "$BUILD_TMP/"
  # A private copy, so the unpacked archive is left as it was shipped. PREFIX matches where the
  # binary is about to go.
  make -C "$BUILD_TMP" -j"$(nproc 2>/dev/null || echo 2)" PREFIX="$PREFIX" jack-graph
  SRC_BIN="$BUILD_TMP/jack-graph"
}

if [ "$DO_DEPS" -eq 1 ]; then
  step "Installing runtime dependencies"
  install_runtime_deps
else
  step "Skipping dependency install (--no-deps)"
fi

# Decided AFTER the runtime dependencies are in place, so the ldd check sees the libraries that
# are actually there.
if [ "$FROM_SOURCE" -eq 0 ]; then
  if prebuilt_runnable; then
    SRC_BIN="$SELF_DIR/bin/jack-graph"
    info "using the prebuilt binary"
  elif [ -f "$SELF_DIR/source/Makefile" ]; then
    warn "the prebuilt binary is missing, for another architecture, or cannot find its"
    warn "libraries here -- building from source instead."
    FROM_SOURCE=1
  else
    die "the prebuilt binary cannot run here and no source tree is bundled."
  fi
fi

if [ "$FROM_SOURCE" -eq 1 ]; then
  [ "$DO_DEPS" -eq 1 ] && { step "Installing build dependencies"; install_build_deps; }
  build_from_source
fi

# ---- audio group ------------------------------------------------------------

NEED_LOGOUT=false
if id "$REAL_USER" &>/dev/null; then
  if ! id -nG "$REAL_USER" | grep -qw audio; then
    step "Adding $REAL_USER to the audio group (required for JACK realtime priority)"
    usermod -aG audio "$REAL_USER"
    NEED_LOGOUT=true
  else
    info "$REAL_USER is already in the audio group."
  fi
fi

# ---- files ------------------------------------------------------------------

step "Installing jack-graph to $BINDIR"
install -Dm755 -- "$SRC_BIN" "$BINDIR/jack-graph"

# THE FONTS ARE NOT OPTIONAL. There is no toolkit to fall back on: the window draws its own text
# through FreeType, and without these it uses a cairo toy face whose metrics are not the ones the
# layout was audited against. The licences travel with them (Roboto Apache-2.0, Michroma OFL-1.1).
step "Installing fonts to $RESDIR/fonts"
if [ -d "$SELF_DIR/resources/fonts" ]; then
  FONT_SRC="$SELF_DIR/resources/fonts"
else
  FONT_SRC="$SELF_DIR/source/resources/fonts"
fi
[ -d "$FONT_SRC" ] || die "the bundled fonts are missing from this archive."
install -d -m 0755 -- "$RESDIR/fonts"
for f in "$FONT_SRC"/*; do
  [ -f "$f" ] && install -m 0644 -- "$f" "$RESDIR/fonts/$(basename -- "$f")"
done

step "Installing the desktop entry"
if [ -f "$SELF_DIR/resources/jack-graph.desktop" ]; then
  DESKTOP_SRC="$SELF_DIR/resources/jack-graph.desktop"
else
  DESKTOP_SRC="$SELF_DIR/source/resources/jack-graph.desktop"
fi
install -Dm644 -- "$DESKTOP_SRC" "$APPDIR/jack-graph.desktop"
if command -v update-desktop-database &>/dev/null; then
  update-desktop-database "$APPDIR" || true
fi

if command -v ldd >/dev/null 2>&1 && ldd "$BINDIR/jack-graph" 2>/dev/null | grep -q 'not found'; then
  warn "jack-graph has unresolved libraries:"
  ldd "$BINDIR/jack-graph" 2>/dev/null | grep 'not found' >&2 || true
fi

echo ""
echo "Jack Graph installed successfully!"
echo ""
echo "Run it from a terminal with:  jack-graph"
echo "Or find it in your application menu under Multimedia/Audio/Video."
echo ""
if [ "$NEED_LOGOUT" = "true" ]; then
  echo "IMPORTANT: Log out and back in for the audio group membership to take effect."
  echo "Without this JACK realtime priority will not work correctly."
fi
