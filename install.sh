#!/bin/bash

set -e

# Ensure script is run as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run this script with sudo:"
  echo "  sudo ./install.sh"
  exit 1
fi

echo "Installing Jack Graph system-wide..."

# Install binary
cp jack-graph /usr/local/bin/
chmod +x /usr/local/bin/jack-graph

# Install desktop entry
cp resources/jack-graph.desktop /usr/share/applications/


echo "Installation complete!"
echo "You can now run 'jack-graph' from the command line"
echo "Or find it in your desktop environment's application menu"
echo ""
echo "Note: Make sure JACK is installed and running on your system"
echo "sudo apt install jackd2 on Debian/Devuan installs qjackctl"
echo "if you do not want qjackctl sudo apt-mark hold qjackctl before"
echo "you install jackd2" 
