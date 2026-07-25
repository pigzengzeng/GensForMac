#!/bin/sh
# ============================================================================
#  make_app.sh - package ./gens_mac into a self-contained Gens.app bundle
#  (macOS, Intel x86_64). Bundles the Homebrew SDL2 dylib so the app runs
#  without a separate SDL2 install.
# ============================================================================
set -e

APP="Gens.app"
BIN="gens_mac"
EXEC_NAME="GensForMac"

if [ ! -f "$BIN" ]; then
  echo "error: ./$BIN not found - run 'make' first." >&2
  exit 1
fi

echo "==> creating $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Frameworks"
mkdir -p "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/$EXEC_NAME"

# ---- game controller mapping DB (community + BETOP C3 override) -----------
if [ -f "src/gamecontrollerdb.txt" ]; then
  cp "src/gamecontrollerdb.txt" "$APP/Contents/Resources/gamecontrollerdb.txt"
  echo "==> bundled gamecontrollerdb.txt"
fi

# ---- Info.plist -----------------------------------------------------------
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>Gens for Mac</string>
  <key>CFBundleDisplayName</key>     <string>Gens for Mac</string>
  <key>CFBundleIdentifier</key>      <string>com.gens.mac</string>
  <key>CFBundleVersion</key>         <string>2.14</string>
  <key>CFBundleShortVersionString</key> <string>2.14</string>
  <key>CFBundleExecutable</key>      <string>$EXEC_NAME</string>
  <key>CFBundlePackageType</key>     <string>APPL</string>
  <key>LSMinimumSystemVersion</key>  <string>10.13</string>
  <key>NSHighResolutionCapable</key> <true/>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>Mega Drive ROM</string>
      <key>CFBundleTypeRole</key><string>Viewer</string>
      <key>LSItemContentTypes</key>
      <array><string>public.data</string></array>
      <key>CFBundleTypeExtensions</key>
      <array>
        <string>bin</string><string>md</string><string>gen</string>
        <string>smd</string><string>sms</string><string>gg</string>
        <string>zip</string>
      </array>
    </dict>
  </array>
</dict>
</plist>
PLIST

# ---- SDL2 handling --------------------------------------------------------
# Preferred path: SDL2 is STATICALLY linked (vendor/install, built by
# ./build_sdl2.sh) -> nothing to bundle, the app is fully self-contained.
#
# Fallback path (dynamic system SDL2): bundle the dylib unless it is
# sdl2-compat, whose load-time constructor aborts when relocated into a
# bundle's Frameworks/ folder.
SDL_LIB=$(otool -L "$APP/Contents/MacOS/$EXEC_NAME" | awk '/libSDL2/{print $1; exit}')

if [ -z "$SDL_LIB" ]; then
  echo "==> SDL2 statically linked - app is fully self-contained."
else
  IS_COMPAT=0
  case "$SDL_LIB" in
    *sdl2-compat*) IS_COMPAT=1 ;;
  esac
  if [ "$IS_COMPAT" -eq 0 ] && [ -f "$SDL_LIB" ]; then
    BASE=$(basename "$SDL_LIB")
    cp "$SDL_LIB" "$APP/Contents/Frameworks/$BASE"
    chmod u+w "$APP/Contents/Frameworks/$BASE"
    install_name_tool -change "$SDL_LIB" "@executable_path/../Frameworks/$BASE" \
        "$APP/Contents/MacOS/$EXEC_NAME"
    echo "==> bundled $BASE (self-contained)"
  else
    echo "==> NOTE: sdl2-compat detected - not bundled (dllinit abort on relocate)."
    echo "==>       App links against the system SDL2; ensure 'brew install sdl2'."
    echo "==>       For a fully self-contained app run ./build_sdl2.sh first."
  fi
fi

# ad-hoc code signature on the main executable so Gatekeeper allows local launch
codesign --force --sign - "$APP/Contents/MacOS/$EXEC_NAME" 2>/dev/null || true

echo "==> done: $APP"
