#!/bin/bash

# Jack Graph Release Tarball Creation Script
# Creates a release tarball with all necessary files

set -e

TARBALL_NAME="jack-graph.tar.gz"

echo "Creating release tarball: ${TARBALL_NAME}"

# Create tarball with files in jack-graph/ directory structure
tar -czf "${TARBALL_NAME}" \
    --transform "s,^,jack-graph/," \
    --exclude='*.o' \
    --exclude='.git*' \
    --exclude="${TARBALL_NAME}" \
    README.md \
    install.sh \
    jack-graph \
    resources/jack-graph.desktop \
    LICENSE

echo "Tarball created: ${TARBALL_NAME}"
echo "Size: $(du -h "${TARBALL_NAME}" | cut -f1)"
echo ""
echo "To test extraction:"
echo "  mkdir test && cd test && tar -xf ../${TARBALL_NAME} && ls jack-graph/"