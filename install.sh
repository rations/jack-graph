#!/bin/bash

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo:"
    echo "  sudo ./install.sh"
    exit 1
fi

# Get the real user who invoked sudo (needed for group changes)
REAL_USER="${SUDO_USER:-$USER}"

echo "Installing Jack Graph..."
echo ""

# --- Detect package manager ---
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

echo "Detected package manager: $PKG_MGR"
echo ""

# --- Install runtime dependencies ---
install_dependencies() {
    case "$PKG_MGR" in
        apt)
            apt-get update -qq
            # Hold qjackctl before installing jackd2 so apt does not pull it
            # in as a recommendation. jack-graph replaces it.
            apt-mark hold qjackctl 2>/dev/null || true
            apt-get install -y --no-install-recommends \
                jackd2 \
                libgtkmm-3.0-1v5 \
                libasound2 \
                alsa-utils
            ;;
        pacman)
            # On Arch/Artix, jack2 and qjackctl are fully independent packages
            pacman -Sy --noconfirm --needed \
                jack2 \
                gtkmm3 \
                alsa-lib \
                alsa-utils
            ;;
        xbps)
            xbps-install -Sy \
                jack \
                gtkmm3 \
                alsa-lib \
                alsa-utils
            ;;
        dnf)
            dnf install -y \
                jack-audio-connection-kit \
                gtkmm30 \
                alsa-lib \
                alsa-utils
            ;;
        *)
            echo "WARNING: Could not detect a supported package manager."
            echo "Please install the following packages manually before running jack-graph:"
            echo "  jackd2 / jack2          (JACK audio server daemon)"
            echo "  libgtkmm-3.0 / gtkmm3   (GTK3 C++ bindings)"
            echo "  libasound2 / alsa-lib    (ALSA sound library)"
            echo "  alsa-utils               (provides aplay for device listing)"
            echo ""
            ;;
    esac
}

echo "Installing dependencies..."
install_dependencies
echo ""

# --- Audio group (needed for JACK realtime priority) ---
NEED_LOGOUT=false
if id "$REAL_USER" &>/dev/null; then
    if ! id -nG "$REAL_USER" | grep -qw audio; then
        echo "Adding $REAL_USER to the audio group (required for JACK realtime priority)..."
        usermod -aG audio "$REAL_USER"
        NEED_LOGOUT=true
    else
        echo "$REAL_USER is already in the audio group."
    fi
fi

# --- Binary ---
echo "Installing jack-graph binary..."
install -Dm755 jack-graph /usr/local/bin/jack-graph

# --- Desktop entry ---
echo "Installing desktop entry..."
install -Dm644 resources/jack-graph.desktop /usr/share/applications/jack-graph.desktop

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database /usr/share/applications/
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
