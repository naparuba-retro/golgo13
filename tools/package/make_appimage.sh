#!/bin/sh
# Builds golgo13-x86_64.AppImage from an existing CMake build (build/golgo13) and a local SDL3 install (local/lib).
# Run from the project root on the Linux build machine:  sh tools/package/make_appimage.sh
# The AppImage contains NO game data: the user supplies their own ROM set at first launch.
set -e
ROOT=$(pwd)
APPDIR="$ROOT/build/AppDir"
rm -rf "$APPDIR"; mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
cp build/golgo13 "$APPDIR/usr/bin/golgo13"
cp -a local/lib/libSDL3.so* "$APPDIR/usr/lib/"
strip "$APPDIR/usr/bin/golgo13" "$APPDIR/usr/lib/"libSDL3.so.*.* 2>/dev/null || true
python3 tools/package/make_icon.py "$APPDIR/golgo13.png"
cat > "$APPDIR/golgo13.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Golgo 13 (native port)
Comment=Native port of the Namco System 12 arcade game (bring your own ROM)
Exec=golgo13
Icon=golgo13
Categories=Game;
Terminal=false
EOF
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/golgo13" "$@"
EOF
chmod +x "$APPDIR/AppRun"
TOOL="$ROOT/dl/appimagetool-x86_64.AppImage"
if [ ! -x "$TOOL" ]; then
  mkdir -p "$ROOT/dl"
  curl -sL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$ROOT/build/golgo13-x86_64.AppImage"
ls -l "$ROOT/build/golgo13-x86_64.AppImage"
