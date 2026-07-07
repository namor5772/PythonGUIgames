#!/bin/bash
# =====================================================================
#  MyPocketTanks - macOS installer.
#  Finds Python 3 (helping you install it if it's missing) and builds a
#  double-clickable "MyPocketTanks" app on your Desktop.
#  Plain text on purpose - feel free to read it.
#
#  (This file doubles as the template for the other apps' bundles:
#  package_games.py rewrites the MyPocketTanks/mypockettanks tokens, so
#  keep app-specific wording inside them - or use $NAME.)
#
#  If macOS refuses to open this file ("unidentified developer"):
#  right-click it and choose Open - or run it from Terminal with:
#      bash Install-macOS.command
# =====================================================================
set -uo pipefail

NAME="MyPocketTanks"
SCRIPT="MyPocketTanks.py"
PNG="mypockettanks.png"

# The folder this installer (and the app it installs) lives in.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

pause_exit() {                          # keep the Terminal window readable
    echo
    read -r -p "Press Return to close... " _ 2>/dev/null || true
    exit "$1"
}

echo
echo "  === $NAME installer ==="
echo

if [ ! -f "$DIR/$SCRIPT" ]; then
    echo "  $SCRIPT was not found next to this installer."
    echo "  Please unpack the whole zip and run the installer from the"
    echo "  extracted $NAME folder."
    pause_exit 1
fi

# ---- 1) Find a Python 3 that has Tkinter (all this app needs). ---------
# /usr/bin/python3 goes last: on a bare Mac it is only a stub that offers
# to install Apple's developer tools, which may still lack Tkinter.
PYTHON=""
for cand in "$(command -v python3 2>/dev/null || true)" \
            /usr/local/bin/python3 \
            /opt/homebrew/bin/python3 \
            /Library/Frameworks/Python.framework/Versions/Current/bin/python3 \
            /usr/bin/python3; do
    [ -n "$cand" ] && [ -x "$cand" ] || continue
    if "$cand" -c 'import sys, tkinter; sys.exit(0 if sys.version_info >= (3, 8) else 1)' >/dev/null 2>&1; then
        PYTHON="$cand"
        break
    fi
done

if [ -z "$PYTHON" ]; then
    echo "  Python 3 (with its Tkinter GUI library) was not found."
    echo
    echo "    1. Your browser is opening  https://www.python.org/downloads/"
    echo "    2. Download and run the standard installer there (it includes"
    echo "       everything $NAME needs)."
    echo "    3. Then double-click this installer again."
    echo
    echo "  (Homebrew users: 'brew install python-tk' also works.)"
    open "https://www.python.org/downloads/" 2>/dev/null || true
    pause_exit 1
fi
echo "  Using Python: $PYTHON"

# ---- 2) Build the multi-resolution .icns from the shipped PNG. ---------
# Purely cosmetic - if anything here fails the app just gets a plain icon.
ICNS=""
TMP=""
if [ -f "$DIR/$PNG" ] && command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1; then
    TMP="$(mktemp -d)"
    ICONSET="$TMP/icon.iconset"
    mkdir -p "$ICONSET"
    for spec in "icon_16x16:16"     "icon_16x16@2x:32" \
                "icon_32x32:32"     "icon_32x32@2x:64" \
                "icon_128x128:128"  "icon_128x128@2x:256" \
                "icon_256x256:256"  "icon_256x256@2x:512" \
                "icon_512x512:512"  "icon_512x512@2x:1024"; do
        out="${spec%%:*}"; sz="${spec##*:}"
        sips -z "$sz" "$sz" "$DIR/$PNG" --out "$ICONSET/${out}.png" >/dev/null 2>&1
    done
    if iconutil -c icns "$ICONSET" -o "$TMP/game.icns" 2>/dev/null; then
        ICNS="$TMP/game.icns"
    fi
fi

# ---- 3) Assemble the .app bundle on the Desktop. -----------------------
APP="$HOME/Desktop/$NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
[ -n "$ICNS" ] && cp "$ICNS" "$APP/Contents/Resources/game.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>             <string>$NAME</string>
    <key>CFBundleDisplayName</key>      <string>$NAME</string>
    <key>CFBundleIdentifier</key>       <string>com.pythonguigames.$NAME</string>
    <key>CFBundleVersion</key>          <string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>CFBundleExecutable</key>       <string>$NAME</string>
    <key>CFBundleIconFile</key>         <string>game</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSMinimumSystemVersion</key>   <string>10.13</string>
</dict>
</plist>
PLIST

# The launcher: a tiny stub that runs the app with the Python we just
# found (baked in as an absolute path - Finder gives apps a minimal PATH).
cat > "$APP/Contents/MacOS/$NAME" <<LAUNCH
#!/bin/bash
PYTHON="$PYTHON"
[ -x "\$PYTHON" ] || PYTHON="\$(/usr/bin/command -v python3 || echo /usr/bin/python3)"
cd "$DIR"
exec "\$PYTHON" "$DIR/$SCRIPT" "\$@"
LAUNCH
chmod +x "$APP/Contents/MacOS/$NAME"

# Nudge LaunchServices/Finder to pick up the new bundle + icon right away.
touch "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$APP" >/dev/null 2>&1 || true
[ -n "$TMP" ] && rm -rf "$TMP"

echo
echo "  Done! \"$NAME\" is now on your Desktop."
echo "  (If you move this folder later, run this installer again.)"
echo
read -r -p "  Start $NAME now [y/N]? " yn 2>/dev/null || yn=""
case "$yn" in
    [Yy]*) open "$APP" ;;
esac
exit 0
