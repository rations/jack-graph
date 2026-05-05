#!/bin/bash

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with sudo:"
    echo "  sudo ./uninstall.sh"
    exit 1
fi

echo "Uninstalling Jack Graph..."
echo ""

# --- Remove binary ---
if [ -f /usr/local/bin/jack-graph ]; then
    rm -f /usr/local/bin/jack-graph
    echo "Removed /usr/local/bin/jack-graph"
fi

# --- Remove desktop entry ---
if [ -f /usr/share/applications/jack-graph.desktop ]; then
    rm -f /usr/share/applications/jack-graph.desktop
    echo "Removed /usr/share/applications/jack-graph.desktop"
fi

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database /usr/share/applications/
fi

# --- Remove apt-mark hold on qjackctl (apt distros only) ---
if command -v apt-mark &>/dev/null; then
    if apt-mark showhold 2>/dev/null | grep -q '^qjackctl$'; then
        echo "Removing hold on qjackctl..."
        apt-mark unhold qjackctl
    fi
fi

echo ""
echo "Jack Graph has been removed."
echo ""
echo "Runtime dependencies (jackd2, etc.) have been left in place."
echo "To remove jackd2 run:  sudo apt remove jackd2   (Debian/Devuan)"
echo "                       sudo pacman -R jack2      (Arch/Artix)"
echo "                       sudo xbps-remove jack     (Void)"
echo "                       sudo dnf remove jack-audio-connection-kit  (Fedora)"
