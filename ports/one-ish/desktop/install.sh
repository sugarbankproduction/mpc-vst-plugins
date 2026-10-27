#!/usr/bin/env bash
# Copy the desktop build (desktop/build.sh) into a VST2 folder a host scans: default %USERPROFILE%\VST2.
# Run in Git Bash on Windows:  ports/one-ish/desktop/install.sh [folder]
# A DLL a host still has loaded can't be overwritten, but it can be renamed: the old copy is moved aside, the host
# keeps using it until it reloads the plugin, and the leftover is deleted on the next install.
set -euo pipefail
SRC="$(dirname "$0")/../build/desktop/ONE-ish.dll"
DEST="${1:-$USERPROFILE/VST2}"
mkdir -p "$DEST"
rm -f "$DEST"/ONE-ish.dll.old-* 2>/dev/null || true
if [ -f "$DEST/ONE-ish.dll" ]; then mv "$DEST/ONE-ish.dll" "$DEST/ONE-ish.dll.old-$(date +%s)"; fi
cp "$SRC" "$DEST/ONE-ish.dll"
echo "installed: $(cygpath -w "$DEST/ONE-ish.dll")"
