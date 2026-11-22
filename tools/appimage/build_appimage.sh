#!/bin/bash
# Builds golgo13-<version>-x86_64.AppImage in CI. Needs: the Linux bundle (program + project files) in $BUNDLE, the SDL3 build packages.
# Everything the user needs to build the games from their ROMs is put inside: Python, Pillow/NumPy, CMake, Ninja, zig cc, static SDL3.
set -euo pipefail
VERSION="${GOLGO13_VERSION:-dev}"; BUNDLE="${BUNDLE:?}"; OUT="${OUT:-$PWD}"
PBS="https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.11.18%2B20261110-x86_64-unknown-linux-gnu-install_only.tar.gz"
APP="$PWD/AppDir"; rm -rf "$APP"; mkdir -p "$APP/usr/bin" "$APP/opt"

echo "== private Python"
curl -fsSL "$PBS" | tar -xz -C "$APP/usr"; mv "$APP/usr/python" "$APP/usr/python" 2>/dev/null || true
"$APP/usr/python/bin/python3" -m pip install --quiet --disable-pip-version-check pillow numpy cmake ninja ziglang
for t in cmake ninja; do ln -sf "../python/bin/$t" "$APP/usr/bin/$t"; done
cat > "$APP/usr/bin/zigcc" <<'Z'
#!/bin/sh
exec "$(dirname "$(readlink -f "$0")")/../python/bin/python3" -m ziglang cc "$@"
Z
chmod +x "$APP/usr/bin/zigcc"

echo "== static SDL3 (built once here, so the user's machine needs no X11/Wayland headers)"
git clone --depth 1 --branch release-3.4.20 https://github.com/libsdl-org/SDL.git sdl-src
cmake -S sdl-src -B sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
      -DSDL_X11_XTEST=OFF -DSDL_VULKAN=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$APP/usr/sdl3"
cmake --build sdl-build && cmake --install sdl-build

echo "== project files + menu-only program"
mkdir -p "$APP/opt/golgo13"; cp -a "$BUNDLE/." "$APP/opt/golgo13/"
rm -rf "$APP/opt/golgo13/.venv" "$APP/opt/golgo13/.git"
echo "$VERSION" > "$APP/opt/golgo13/VERSION"
cp tools/appimage/AppRun "$APP/AppRun"; chmod +x "$APP/AppRun"
cat > "$APP/golgo13.desktop" <<D
[Desktop Entry]
Type=Application
Name=Golgo 13
Comment=Native port of the Golgo 13 arcade games (bring your own ROMs)
Exec=AppRun
Icon=golgo13
Categories=Game;
D
"$APP/usr/python/bin/python3" - <<'P'
from PIL import Image
im = Image.open('docs/img/logo.png').convert('RGB'); im = im.resize((256, int(256 * im.height / im.width))); c = Image.new('RGB', (256, 256), (14, 14, 22)); c.paste(im, (0, (256 - im.height) // 2)); c.save('AppDir/golgo13.png')
P

echo "== AppImage"
curl -fsSL -o appimagetool https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage; chmod +x appimagetool
ARCH=x86_64 ./appimagetool --appimage-extract-and-run "$APP" "$OUT/golgo13-$VERSION-x86_64.AppImage"
ls -la "$OUT"/*.AppImage
