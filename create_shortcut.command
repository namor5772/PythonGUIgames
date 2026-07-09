#!/bin/bash
# Builds a clickable Desktop shortcut for this project's games on macOS, in one
# of two flavors — the macOS counterpart of create_shortcut.ps1:
#
#   * Python (default) — a <Name>.app whose launcher runs the .py with python3.
#   * Native (--native) — a "<App> (Native).app" whose launcher runs the
#                         compiled build/<App> binary directly (no Python, no
#                         venv, fully self-contained). Build it first with
#                         build_native.command.
#
# Either way it turns a master PNG (see make_*_icon_mac.py) into a proper .icns
# and assembles a double-clickable .app bundle on the Desktop.
#
# Python flavor:  ./create_shortcut.command [Name [Script.py [icon.png]]]
#   ./create_shortcut.command                             # MyTetris / MyTetris.py
#   ./create_shortcut.command MyPocketTanks MyPocketTanks.py mypockettanks.png
#   ./create_shortcut.command "My App" App.py myapp.png
#
# Native flavor:  ./create_shortcut.command --native [App [DisplayName]]
#   ./create_shortcut.command --native MyTetris           # -> "MyTetris (Native).app"
#   ./create_shortcut.command --native Sun2Set
#   App is one of MyTetris / MyPocketTanks / Sun2Set; the binary is build/<App>,
#   the icon <lowercase App>.png, and — like create_shortcut.ps1 -Native — the
#   shortcut is named "<App> (Native)" so it sits beside the Python one instead
#   of overwriting it (pass DisplayName to override).
#
# Pure system tools only (bash, sips, iconutil) — no third-party packages.

set -euo pipefail

# ---- Parse a leading --native flag; everything else stays positional. ------
NATIVE=0
POS=()
for arg in "$@"; do
    case "$arg" in
        --native|-Native|-n) NATIVE=1 ;;
        *) POS+=("$arg") ;;
    esac
done

# Resolve the project directory (where this script lives), even via symlink.
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
PROJECT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
cd "$PROJECT_DIR"

if [ "$NATIVE" -eq 1 ]; then
    # ---- Native flavor: point at the standalone build/<App> binary. --------
    APP="${POS[0]:-MyTetris}"
    case "$APP" in
        MyTetris|MyPocketTanks|Sun2Set) ;;
        *) echo "unknown app: $APP (choose MyTetris, MyPocketTanks or Sun2Set)"
           exit 1 ;;
    esac
    NAME="${POS[1]:-$APP (Native)}"
    PNG="$(echo "$APP" | tr '[:upper:]' '[:lower:]').png"
    BIN="$PROJECT_DIR/build/$APP"
    if [ ! -x "$BIN" ]; then
        echo "Native build not found: $BIN"
        echo "    build it first:  ./build_native.command $APP"
        exit 1
    fi
else
    # ---- Python flavor: launch the .py with python3 (unchanged). -----------
    NAME="${POS[0]:-MyTetris}"
    SCRIPT="${POS[1]:-MyTetris.py}"
    PNG="${POS[2]:-mytetris.png}"

    # Bake in the python3 we can actually find now (Finder gives apps a minimal
    # PATH, so we record the full path rather than relying on PATH at launch).
    PYTHON="$(command -v python3 || true)"
    [ -z "$PYTHON" ] && PYTHON="/usr/bin/python3"
fi

BASE="${PNG%.png}"          # mytetris.png -> mytetris(.icns)

# 1) Make sure the master PNG exists (running its generator if we know it),
#    then build a multi-resolution .icns.
if [ ! -f "$PNG" ]; then
    PYGEN="${PYTHON:-$(command -v python3 || echo /usr/bin/python3)}"
    case "$PNG" in
        mytetris.png)      "$PYGEN" make_tetris_icon_mac.py ;;
        mypockettanks.png) "$PYGEN" make_pockettanks_icon_mac.py ;;
        sun2set.png)       "$PYGEN" make_sun2set_icon_mac.py ;;
        *) echo "Icon $PNG not found (and no known generator for it)"; exit 1 ;;
    esac
fi

ICONSET="$(mktemp -d)/$BASE.iconset"
mkdir -p "$ICONSET"
# (target filename : pixel size) — the set Apple's iconutil expects.
for spec in \
    "icon_16x16:16"        "icon_16x16@2x:32" \
    "icon_32x32:32"        "icon_32x32@2x:64" \
    "icon_128x128:128"     "icon_128x128@2x:256" \
    "icon_256x256:256"     "icon_256x256@2x:512" \
    "icon_512x512:512"     "icon_512x512@2x:1024"; do
    out="${spec%%:*}"; sz="${spec##*:}"
    sips -z "$sz" "$sz" "$PNG" --out "$ICONSET/${out}.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$BASE.icns"
rm -rf "$(dirname "$ICONSET")"
echo "Built $BASE.icns"

# 2) Assemble the .app bundle on the Desktop.
APP_BUNDLE="$HOME/Desktop/$NAME.app"
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS" "$APP_BUNDLE/Contents/Resources"
cp "$BASE.icns" "$APP_BUNDLE/Contents/Resources/$BASE.icns"

cat > "$APP_BUNDLE/Contents/Info.plist" <<PLIST
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
    <key>CFBundleIconFile</key>         <string>$BASE</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>LSMinimumSystemVersion</key>   <string>10.13</string>
</dict>
</plist>
PLIST

# The launcher: a tiny shell stub that starts the game.
if [ "$NATIVE" -eq 1 ]; then
    # Run the standalone native binary directly — no interpreter needed.
    cat > "$APP_BUNDLE/Contents/MacOS/$NAME" <<LAUNCH
#!/bin/bash
BIN="$BIN"
cd "$PROJECT_DIR"
exec "\$BIN" "\$@"
LAUNCH
else
    # Run the game with the baked python3.
    cat > "$APP_BUNDLE/Contents/MacOS/$NAME" <<LAUNCH
#!/bin/bash
PYTHON="$PYTHON"
SCRIPT="$PROJECT_DIR/$SCRIPT"
[ -x "\$PYTHON" ] || PYTHON="\$(/usr/bin/command -v python3 || echo /usr/bin/python3)"
cd "$PROJECT_DIR"
exec "\$PYTHON" "\$SCRIPT" "\$@"
LAUNCH
fi
chmod +x "$APP_BUNDLE/Contents/MacOS/$NAME"

# Nudge LaunchServices/Finder to pick up the new bundle + icon right away.
touch "$APP_BUNDLE"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$APP_BUNDLE" >/dev/null 2>&1 || true

echo "Created shortcut: $APP_BUNDLE"
if [ "$NATIVE" -eq 1 ]; then
    echo "Launches: $BIN"
else
    echo "Launches: $PYTHON $PROJECT_DIR/$SCRIPT"
fi
