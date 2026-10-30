#!/bin/bash
# Catch the macOS 26 startup regression from Swift Collections 1.7 (#11).
set -euo pipefail
APP="${1:?Usage: check-runtime-compatibility.sh /path/to/TypeTide.app}"
BINARY="$APP/Contents/MacOS/TypeTide"
[ -f "$BINARY" ] || { echo "Missing TypeTide executable: $BINARY" >&2; exit 1; }
IMPORTS="$(xcrun nm -u -m "$BINARY")"
if printf '%s\n' "$IMPORTS" | grep -E '[[:space:]]_swift_initBorrow([[:space:]]|$)'; then
    echo "Unsupported Swift 6.4 runtime import: swift_initBorrow (macOS 26 launch regression)" >&2
    exit 1
fi
echo "macOS 26 runtime import check passed"
