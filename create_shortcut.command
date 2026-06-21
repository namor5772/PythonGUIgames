#!/bin/bash
# Builds a clickable Desktop shortcut for MyTetris on macOS.
#
# It turns mytetris.png (see make_tetris_icon_mac.py) into a proper mytetris.icns
# and assembles a double-clickable <Name>.app bundle on the Desktop whose
# launcher runs the project's script with python3 — the macOS counterpart of
# create_shortcut.ps1.
#
#   ./create_shortcut.command                 # default "MyTetris" / MyTetris.py
#   ./create_shortcut.command "My App" App.py # custom name / target script
#
# Pure system tools only (bash, sips, iconutil) — no third-party packages.

set -euo pipefail

NAME="${1:-MyTetris}"
SCRIPT="${2:-MyTetris.py}"

# Resolve the project directory (where this script lives), even via symlink.
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
PROJECT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"

# Bake in the python3 we can actually find now (Finder gives apps a minimal
# PATH, so we record the full path rather than relying on PATH at launch).
PYTHON="$(command -v python3 || true)"
[ -z "$PYTHON" ] && PYTHON="/usr/bin/python3"

cd "$PROJECT_DIR"

# 1) Make sure the master PNG exists, then build a multi-resolution .icns.
[ -f mytetris.png ] || "$PYTHON" make_tetris_icon_mac.py

ICONSET="$(mktemp -d)/mytetris.iconset"
mkdir -p "$ICONSET"
# (target filename : pixel size) — the set Apple's iconutil expects.
for spec in \
    "icon_16x16:16"        "icon_16x16@2x:32" \
    "icon_32x32:32"        "icon_32x32@2x:64" \
    "icon_128x128:128"     "icon_128x128@2x:256" \
    "icon_256x256:256"     "icon_256x256@2x:512" \
    "icon_512x512:512"     "icon_512x512@2x:1024"; do
    out="${spec%%:*}"; sz="${spec##*:}"
    sips -z "$sz" "$sz" mytetris.png --out "$ICONSET/${out}.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o mytetris.icns
rm -rf "$(dirname "$ICONSET")"
echo "Built mytetris.icns"

# 2) Assemble the .app bundle on the Desktop.
APP="$HOME/Desktop/$NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp mytetris.icns "$APP/Contents/Resources/mytetris.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>             <string>$NAME</string>
    <key>CFBundleDisplayName</key>      <string>$NAME</string>
    <key>CFBundleIdentifier</key>       <string>com.pythonguigames.${NAME// /}</string>
    <key>CFBundleVersion</key>          <string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>CFBundleExecutable</key>       <string>$NAME</string>
    <key>CFBundleIconFile</key>         <string>mytetris</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSMinimumSystemVersion</key>   <string>10.13</string>
</dict>
</plist>
PLIST

# The launcher: a tiny shell stub that runs the game with the baked python3.
cat > "$APP/Contents/MacOS/$NAME" <<LAUNCH
#!/bin/bash
PYTHON="$PYTHON"
SCRIPT="$PROJECT_DIR/$SCRIPT"
[ -x "\$PYTHON" ] || PYTHON="\$(/usr/bin/command -v python3 || echo /usr/bin/python3)"
cd "$PROJECT_DIR"
exec "\$PYTHON" "\$SCRIPT" "\$@"
LAUNCH
chmod +x "$APP/Contents/MacOS/$NAME"

# Nudge LaunchServices/Finder to pick up the new bundle + icon right away.
touch "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$APP" >/dev/null 2>&1 || true

echo "Created shortcut: $APP"
echo "Launches: $PYTHON $PROJECT_DIR/$SCRIPT"
