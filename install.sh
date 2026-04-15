#!/bin/bash

# Jack Graph User Installation Script
# Installs jack-graph binary and desktop entry for current user

set -e

echo "Installing Jack Graph for user: $USER"

# Create directories if they don't exist
mkdir -p ~/.local/bin
mkdir -p ~/.local/share/applications

# Copy binary to user bin directory
cp jack-graph ~/.local/bin/
chmod +x ~/.local/bin/jack-graph

# Copy desktop entry to user applications directory
cp resources/jack-graph.desktop ~/.local/share/applications/

echo "Installation complete!"
echo "You can now run 'jack-graph' from the command line"
echo "Or find it in your desktop environment's application menu"
echo ""
echo "Note: Make sure JACK is installed and running on your system"
echo "sudo apt install jackd2 on Debian/Devuan installs qjackctl"
echo "if you do not want qjackctl sudo apt-mark hold qjackctl before"
echo "you install jackd2" 