#!/usr/bin/env bash
# Build the Mt. Sync AppImage against the GTK stack from build-gtk-stack.sh.
# Run on Ubuntu 22.04 (the AppImage's baseline) after
# install-build-deps-jammy.sh and build-gtk-stack.sh, from the repo root.
#
# Usage: packaging/appimage/build-appimage.sh [OUTPUT_DIR]   (default: build)
#
# Env: MTSYNC_DEPS  prefix of the GTK stack (default /opt/mtsync-deps)

set -euo pipefail

OUT="$(mkdir -p "${1:-build}" && cd "${1:-build}" && pwd)"
PREFIX="${MTSYNC_DEPS:-/opt/mtsync-deps}"
BUILD="build/appimage-jammy"
APPDIR="$PWD/$BUILD/AppDir"
VERSION="$(grep -oP '(?<=VERSION )\d+\.\d+\.\d+' CMakeLists.txt | head -1)"
ARCH="$(uname -m)"

export CC=gcc-13 CXX=g++-13
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$PREFIX/bin:$PATH"

# libstdc++ is linked statically: GCC 13's is newer than 22.04's, and the C++
# bindings (gtkmm & co.) are static archives, so this is the only copy.
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build "$BUILD" --parallel "$(nproc)"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"
packaging/appimage/fetch-rclone.sh "$APPDIR"

( cd "$BUILD"
  [[ -x linuxdeploy ]] || { curl -fsSL -o linuxdeploy \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
      chmod +x linuxdeploy; }
  [[ -f linuxdeploy-plugin-gtk.sh ]] || { curl -fsSL -o linuxdeploy-plugin-gtk.sh \
      https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
      chmod +x linuxdeploy-plugin-gtk.sh; }
  rm -f ./*.AppImage
  # No FUSE in containers: linuxdeploy (itself an AppImage) extracts itself
  APPIMAGE_EXTRACT_AND_RUN=1 DEPLOY_GTK_VERSION=4 VERSION="$VERSION" PATH="$PWD:$PATH" \
      ./linuxdeploy --appdir AppDir --plugin gtk --output appimage )

dst="$OUT/mtsync_${VERSION}_${ARCH}.AppImage"
mv "$BUILD"/*.AppImage "$dst"
echo "Built: $dst"

packaging/appimage/check-appimage.sh "$dst"
