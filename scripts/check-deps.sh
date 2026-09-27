#!/bin/sh
# Fail if jack-graph links anything that is not on the allowlist.
#
# An allowlist, for the same reason a manifest is one: a new NEEDED entry is a new thing every
# user has to already have, and it should have to be DECIDED rather than discovered. The whole
# point of removing gtkmm was to shorten this list -- libgtk, libglib, libgobject, libpango,
# libgdk, libatk, libsigc++ and the rest -- and nothing should lengthen it by accident.
#
# libfontconfig is deliberately absent: FontStack loads the two bundled faces by path through
# FreeType and never asks fontconfig anything.
#
# Borrowed from Audio-Gui's scripts/check-deps.sh.
set -eu

bin=${1:-jack-graph}
[ -f "$bin" ] || { echo "$0: no binary at $bin" >&2; exit 1; }

allowed='libcairo.so.2 libfreetype.so.6 libX11.so.6 libasound.so.2 libjack.so.0
         libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6
         ld-linux-x86-64.so.2 ld-linux-aarch64.so.1'

status=0
for lib in $(objdump -p "$bin" | awk '/NEEDED/{print $2}'); do
    case " $(echo $allowed) " in
        *" $lib "*) ;;
        *)
            echo "$0: $bin links $lib, which is not on the allowlist." >&2
            echo "  Either it belongs in the dependency list -- add it here, to" >&2
            echo "  packaging/build-deb.sh, packaging/install.sh and the README -- or it was" >&2
            echo "  linked by accident." >&2
            status=1
            ;;
    esac
done

[ "$status" -eq 0 ] && echo "$0: $bin links only allowlisted libraries"
exit $status
