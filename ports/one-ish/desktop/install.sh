#!/usr/bin/env bash
# Copy the desktop build (desktop/build.sh) into a VST2 folder a host scans: default %USERPROFILE%\VST2.
# Run in Git Bash on Windows:  ports/one-ish/desktop/install.sh [folder]
# A DLL a host still has loaded can't be overwritten, but it can be renamed: the old copy is moved aside, the host
# keeps using it until it reloads the plugin, and the leftover is deleted on the next install.
# The plugin is called "Moo Won" (2026-10-30, the owner's name for it): it installs as "Moo Won.dll". A copy from before
# the rename ("ONE-ish.dll", the same plugin ID) is moved aside too, so a host doesn't list the plugin twice.
set -euo pipefail
SRC="$(dirname "$0")/../build/desktop/ONE-ish.dll"
DEST="${1:-$USERPROFILE/VST2}"
NAME="Moo Won.dll"
mkdir -p "$DEST"
rm -f "$DEST"/*.dll.old-* 2>/dev/null || true
for old in "$DEST/$NAME" "$DEST/ONE-ish.dll"; do
    if [ -f "$old" ]; then mv "$old" "$old.old-$(date +%s)"; fi
done
cp "$SRC" "$DEST/$NAME"
echo "installed: $(cygpath -w "$DEST/$NAME")"
# (2026-10-31) the eight-output build beside it, a plugin of its own ("Moo Won Multi", its own ID)
MSRC="$(dirname "$0")/../build/desktop/ONE-ish-Multi.dll"
MNAME="Moo Won Multi.dll"
if [ -f "$MSRC" ]; then
    if [ -f "$DEST/$MNAME" ]; then mv "$DEST/$MNAME" "$DEST/$MNAME.old-$(date +%s)"; fi
    cp "$MSRC" "$DEST/$MNAME"
    echo "installed: $(cygpath -w "$DEST/$MNAME")"
fi
# (2026-11-01) the bundled libraries (libraries/<BANK>/*.oneish: Steveo's sounds, libraries/make_bundled.py) into the
# user's library folder, each bank only when it isn't there yet: once copied, it is the user's (edits, deletions and
# renames stay); delete a bank's folder to get it fresh again. ONEISH_LIB_DIR moves the library folder, as in parts.c.
LIBS="${ONEISH_LIB_DIR:-$USERPROFILE/Documents/ONE-ish/Libraries}"
for bank in "$(dirname "$0")"/../libraries/*/; do
    name="$(basename "$bank")"
    if [ ! -d "$LIBS/$name" ]; then
        mkdir -p "$LIBS/$name"
        cp "$bank"*.oneish "$LIBS/$name/"
        echo "library added: $(cygpath -w "$LIBS/$name") ($(ls "$bank"*.oneish | wc -l) patches)"
    fi
done
