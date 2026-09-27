#!/usr/bin/env bash
#
# Build a Debian package (.deb) for jack-graph.
#
# Produces jack-graph_<version>_<arch>.deb that installs the binary to /usr/bin, the bundled fonts
# to /usr/share/jack-graph/fonts and the desktop entry to /usr/share/applications. It is the
# .deb twin of packaging/install.sh, minus the two things a package must not do behind apt's
# back: it does not add anyone to the audio group, and it does not hold qjackctl.
#
# <arch> is dpkg's name for the build machine (amd64, arm64). .github/workflows/linux-aarch64.yml
# builds the arm64 one in a debian:12 container; the amd64 one is built by hand on Debian 12.
#
# Usage: packaging/build-deb.sh [version]
#   With no argument the version is read from the VERSION file. The build dependencies (g++,
#   make, pkg-config, and the jack/alsa/cairo/freetype/x11 dev packages) and dpkg-deb must
#   already be installed.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
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

command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb not found (install the 'dpkg' package)."
command -v make >/dev/null 2>&1 || die "make not found (install build dependencies first)."

ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
OUT="$ROOT/jack-graph_${VERSION}_${ARCH}.deb"

# Built for /usr, so the compiled-in font fallback points at /usr/share/jack-graph. It is only
# the fallback -- respath.cpp looks beside the binary first -- but it should still be right.
step "Building jack-graph ($VERSION, $ARCH)"
make clean >/dev/null
make -j"$(nproc 2>/dev/null || echo 2)" PREFIX=/usr VERSION="$VERSION" \
  ${WERROR:+WERROR=$WERROR} jack-graph
[ -f "$ROOT/jack-graph" ] || die "missing build output: jack-graph"

STAGE="$(mktemp -d)"
trap 'rm -rf -- "$STAGE"' EXIT
chmod 0755 -- "$STAGE" # the archive root (./) must be world-readable, not mktemp's 0700

step "Staging package tree"
make install PREFIX=/usr DESTDIR="$STAGE" VERSION="$VERSION" >/dev/null
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$STAGE/usr/bin/jack-graph" 2>/dev/null || true
fi
install -d -m 0755 -- "$STAGE/usr/share/doc/jack-graph"
install -m 0644 -- "$ROOT/README.md" "$STAGE/usr/share/doc/jack-graph/README.md"
install -m 0644 -- "$ROOT/LICENSE" "$STAGE/usr/share/doc/jack-graph/copyright"
info "staged usr/bin/jack-graph, usr/share/jack-graph/fonts, the desktop entry and docs"

# A clean build leaves the tree as it was for the next `make`.
make clean >/dev/null

INSTALLED_SIZE="$(du -k -s "$STAGE/usr" | awk '{print $1}')"

step "Writing control metadata"
mkdir -p -- "$STAGE/DEBIAN"

# Depends lists what the binary links (objdump -p NEEDED; scripts/check-deps.sh keeps that list
# short) plus jackd itself, which Settings -> Start runs. jackd2 Recommends qjackctl, which apt
# installs by default; `apt install --no-install-recommends` avoids it, and the README says so.
cat >"$STAGE/DEBIAN/control" <<CONTROL
Package: jack-graph
Version: $VERSION
Architecture: $ARCH
Maintainer: rations <ehqcar@proton.me>
Installed-Size: $INSTALLED_SIZE
Depends: libc6, libstdc++6, libasound2, libcairo2, libfreetype6, libx11-6, libjack-jackd2-0 | libjack0, jackd2 | jackd
Section: sound
Priority: optional
Homepage: https://github.com/rations/jack-graph
Description: JACK and ALSA MIDI connection graph
 Jack Graph shows the JACK audio and MIDI ports of running applications and
 hardware as a graph, and connects and disconnects them by dragging cables.
 It starts and stops the JACK server itself, and shows and connects ALSA
 sequencer MIDI ports while JACK is not running.
 .
 The interface is drawn directly with Cairo and FreeType on a plain X11
 window: there is no widget toolkit, no Qt, no GTK and no GLib.
CONTROL

cat >"$STAGE/DEBIAN/postinst" <<'EOF_POSTINST'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications 2>/dev/null || true
fi
exit 0
EOF_POSTINST

cat >"$STAGE/DEBIAN/postrm" <<'EOF_POSTRM'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications 2>/dev/null || true
fi
exit 0
EOF_POSTRM
chmod 0755 -- "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

step "Building package"
# Root-owned files inside the archive; fakeroot is not needed with --root-owner-group.
dpkg-deb --root-owner-group --build "$STAGE" "$OUT"

step "Done"
info "$OUT"
info "$(du -h "$OUT" | cut -f1)"
info "Install with:  sudo apt install --no-install-recommends ./$(basename -- "$OUT")"
