#!/usr/bin/env bash
#
# Build jack-graph and package a release tarball.
#
# Produces jack-graph-<version>-linux-<arch>.tar.gz that unpacks to a single jack-graph/
# directory holding the prebuilt binary, the bundled fonts, install.sh / uninstall.sh and the
# full source for the installer's build-from-source fallback. <arch> is read from the built
# binary itself (x86_64, aarch64), so the name says what it runs on, not where it was packed.
#
# The x86_64 release is built by hand on a Debian 12 machine, for its glibc 2.36 floor; the
# aarch64 one by .github/workflows/linux-aarch64.yml in a debian:12 container. Both read the
# same VERSION file, so every architecture's tarball carries one number.
#
# Usage: ./release-tarball.sh [version]
#   With no argument the version is read from the VERSION file (bump that when you cut a
#   release). An explicit argument overrides it for a one-off build. Extra make variables pass
#   through the environment, e.g. WERROR=1 ./release-tarball.sh, which is what CI runs.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$ROOT"

if [ "$#" -ge 1 ] && [ -n "$1" ]; then
  VERSION="$1"
elif [ -f "$ROOT/VERSION" ]; then
  VERSION="$(tr -d '[:space:]' <"$ROOT/VERSION")"
else
  VERSION="$(git describe --tags --always --dirty 2>/dev/null || date +%Y%m%d)"
fi
[ -n "$VERSION" ] || { echo "ERROR: could not determine a version (empty VERSION file?)" >&2; exit 1; }

info() { printf '  %s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
die()
{
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

# ---- build ------------------------------------------------------------------

# A clean build, for /usr/local -- the prefix install.sh installs into. The prefix is only a
# fallback for finding the fonts (respath.cpp looks beside the installed binary first), so the
# same binary also works from any other prefix.
step "Building jack-graph ($VERSION)"
make clean >/dev/null
# VERSION is passed explicitly, so a one-off version argument reaches --version and the About card.
make -j"$(nproc 2>/dev/null || echo 2)" PREFIX=/usr/local VERSION="$VERSION" \
  ${WERROR:+WERROR=$WERROR} all

[ -f "$ROOT/jack-graph" ] || die "missing build output: jack-graph (did the build succeed?)"

# A binary tarball only runs on one architecture, so it says which in its name. Taken from the
# ELF header rather than uname -m, so the name is what the binary IS; uname is only the fallback
# when readelf is missing.
ARCH=""
if command -v readelf >/dev/null 2>&1; then
  case "$(readelf -h "$ROOT/jack-graph" 2>/dev/null)" in
    *X86-64*) ARCH=x86_64 ;;
    *AArch64*) ARCH=aarch64 ;;
  esac
fi
[ -n "$ARCH" ] || ARCH="$(uname -m)"
OUT="$ROOT/jack-graph-$VERSION-linux-$ARCH.tar.gz"

# ---- stage ------------------------------------------------------------------

STAGE="$(mktemp -d)"
trap 'rm -rf -- "$STAGE"' EXIT
PKG="$STAGE/jack-graph"
mkdir -p -- "$PKG/bin" "$PKG/resources"

step "Staging release tree"

install -m 0755 -- "$ROOT/jack-graph" "$PKG/bin/jack-graph"
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$PKG/bin/jack-graph" 2>/dev/null || true
fi
info "staged bin/jack-graph"

# The fonts at the TOP of the archive, because install.sh installs them directly for the
# prebuilt-binary path and must not have to reach into source/ to do it.
cp -r -- "$ROOT/resources/fonts" "$PKG/resources/"
install -m 0644 -- "$ROOT/resources/jack-graph.desktop" "$PKG/resources/jack-graph.desktop"
info "staged resources/fonts and the desktop entry"

# Source tree for install.sh's build-from-source fallback: everything `make` needs.
mkdir -p -- "$PKG/source"
cp -- "$ROOT/Makefile" "$PKG/source/"
printf '%s\n' "$VERSION" >"$PKG/source/VERSION"
cp -r -- "$ROOT/src" "$ROOT/tools" "$ROOT/resources" "$PKG/source/"
find "$PKG/source" \( -name '*.o' -o -name '*.d' \) -delete
rm -f -- "$PKG/source/tools/uirender"
info "staged source/ (build-from-source fallback)"

install -m 0755 -- "$ROOT/packaging/install.sh" "$PKG/install.sh"
install -m 0755 -- "$ROOT/packaging/uninstall.sh" "$PKG/uninstall.sh"
install -m 0644 -- "$ROOT/README.md" "$PKG/README.md"
install -m 0644 -- "$ROOT/LICENSE" "$PKG/LICENSE"
printf '%s\n' "$VERSION" >"$PKG/VERSION"
info "staged install.sh, uninstall.sh, README, LICENSE, VERSION"

# ---- archive ----------------------------------------------------------------

step "Creating tarball"
# Clean, reproducible ownership; unpacks to jack-graph/.
tar -czf "$OUT" \
  --owner=0 --group=0 --numeric-owner \
  -C "$STAGE" jack-graph

step "Done"
info "$OUT"
info "$(du -h "$OUT" | cut -f1) -- unpacks to jack-graph/"
