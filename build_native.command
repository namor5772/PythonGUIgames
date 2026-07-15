#!/bin/bash
# Builds a native app (MyTetris.cpp / MyPocketTanks.cpp / Sun2Set.cpp) on
# macOS into build/<App> and wraps it in a double-clickable build/<App>.app
# with the proper icon (when sips/iconutil are available). Requires the
# Xcode Command Line Tools (clang++).
#
#   ./build_native.command                  # default: MyTetris
#   ./build_native.command MyPocketTanks
#
# Self-test (same headless checks as the .py):  ./build/<App> --selftest
set -euo pipefail

APP="${1:-MyTetris}"
case "$APP" in
    MyTetris|MyPocketTanks|Sun2Set) ;;
    *) echo "unknown app: $APP (choose MyTetris, MyPocketTanks or Sun2Set)"
       exit 1 ;;
esac
LOWER="$(echo "$APP" | tr '[:upper:]' '[:lower:]')"

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if ! command -v clang++ >/dev/null 2>&1; then
    echo "clang++ was not found - install the Xcode Command Line Tools first:"
    echo "    xcode-select --install"
    exit 1
fi

mkdir -p build
# The one file is C++ everywhere and Objective-C++ here (the Cocoa backend),
# hence -x objective-c++.
clang++ -x objective-c++ -std=c++17 -O2 -Wall "$APP.cpp" \
        -framework Cocoa -o "build/$APP"
echo "Built build/$APP"

# ---- Optional: wrap the binary into a clickable .app with the icon. -------
if [ -f "$LOWER.png" ] && command -v sips >/dev/null 2>&1 \
                       && command -v iconutil >/dev/null 2>&1; then
    TMP="$(mktemp -d)"
    ICONSET="$TMP/icon.iconset"
    mkdir -p "$ICONSET"
    for spec in "icon_16x16:16"     "icon_16x16@2x:32" \
                "icon_32x32:32"     "icon_32x32@2x:64" \
                "icon_128x128:128"  "icon_128x128@2x:256" \
                "icon_256x256:256"  "icon_256x256@2x:512" \
                "icon_512x512:512"  "icon_512x512@2x:1024"; do
        out="${spec%%:*}"; sz="${spec##*:}"
        sips -z "$sz" "$sz" "$LOWER.png" --out "$ICONSET/${out}.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$TMP/game.icns"

    BUNDLE="build/$APP.app"
    rm -rf "$BUNDLE"
    mkdir -p "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"
    cp "build/$APP" "$BUNDLE/Contents/MacOS/$APP"
    cp "$TMP/game.icns" "$BUNDLE/Contents/Resources/game.icns"
    rm -rf "$TMP"

    cat > "$BUNDLE/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>             <string>$APP</string>
    <key>CFBundleDisplayName</key>      <string>$APP</string>
    <key>CFBundleIdentifier</key>       <string>com.pythonguigames.${APP}Native</string>
    <key>CFBundleVersion</key>          <string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>CFBundleExecutable</key>       <string>$APP</string>
    <key>CFBundleIconFile</key>         <string>game</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSMinimumSystemVersion</key>   <string>10.15</string>
</dict>
</plist>
PLIST

    touch "$BUNDLE"
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
        -f "$BUNDLE" >/dev/null 2>&1 || true
    echo "Built $BUNDLE (self-contained - copy it wherever you like)"
else
    echo "(skipped the .app wrap: $LOWER.png or sips/iconutil missing)"
fi
