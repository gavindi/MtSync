#!/usr/bin/env bash
#
# Build Mt. Sync and, optionally, package it.
#
# Usage:
#   ./build.sh                     Configure + compile only
#   ./build.sh --deb               Compile, then produce a .deb
#   ./build.sh --deb --appimage    Compile, then produce multiple package types
#   ./build.sh --all               Compile, then attempt every package type
#   ./build.sh --clean --deb       Wipe the build dir first
#
# Package flags: --deb --rpm --appimage --flatpak --snap --all
# Other flags:   --clean  --debug  --jobs N  --help
#
# Mirrors the per-format steps in .github/workflows/build.yaml, but runs
# against whatever toolchain is actually installed on this host instead of
# the matrixed CI containers — so on a non-matching distro, a given package
# type may simply be unavailable (the script says so and skips it).

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build"
BUILD_TYPE="Release"
JOBS="$(nproc)"
DO_CLEAN=0
WANT_DEB=0
WANT_RPM=0
WANT_APPIMAGE=0
WANT_FLATPAK=0
WANT_SNAP=0

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deb)       WANT_DEB=1 ;;
        --rpm)       WANT_RPM=1 ;;
        --appimage)  WANT_APPIMAGE=1 ;;
        --flatpak)   WANT_FLATPAK=1 ;;
        --snap)      WANT_SNAP=1 ;;
        --all)       WANT_DEB=1; WANT_RPM=1; WANT_APPIMAGE=1; WANT_FLATPAK=1; WANT_SNAP=1 ;;
        --clean)     DO_CLEAN=1 ;;
        --debug)     BUILD_TYPE="Debug" ;;
        --jobs|-j)   JOBS="$2"; shift ;;
        --help|-h)   usage 0 ;;
        *) echo "Unknown option: $1" >&2; usage 1 ;;
    esac
    shift
done

VERSION="$(grep -oP '(?<=VERSION )\d+\.\d+\.\d+' CMakeLists.txt | head -1)"
ARCH="$(uname -m)"
OS_ID=""
OS_VERSION_ID=""
if [[ -r /etc/os-release ]]; then
    # Sourced in a subshell — /etc/os-release also defines VERSION, which
    # would otherwise clobber this script's own $VERSION (the app version).
    OS_ID="$(. /etc/os-release && echo "${ID:-}")"
    OS_VERSION_ID="$(. /etc/os-release && echo "${VERSION_ID:-}")"
fi

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

require() {
    command -v "$1" &>/dev/null || die "'$1' is required for this package type but is not installed."
}

# ── Configure + compile ─────────────────────────────────────────────────────

if [[ "$DO_CLEAN" -eq 1 ]]; then
    log "Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

log "Configuring ($BUILD_TYPE)"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX=/usr

log "Building (-j$JOBS)"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

# ── DEB ──────────────────────────────────────────────────────────────────────

build_deb() {
    log "Packaging DEB"
    require dpkg
    require cpack
    ( cd "$BUILD_DIR" && cpack -G DEB )
    local src dst
    src="$(ls "$BUILD_DIR"/*.deb | head -1)"
    dst="$BUILD_DIR/mtsync_${VERSION}_${OS_ID}${OS_VERSION_ID}_${ARCH}.deb"
    mv "$src" "$dst"
    echo "Built: $dst"
}

# ── RPM ──────────────────────────────────────────────────────────────────────

build_rpm() {
    log "Packaging RPM"
    require rpmbuild
    require cpack
    ( cd "$BUILD_DIR" && cpack -G RPM )
    local src dst
    src="$(ls "$BUILD_DIR"/*.rpm | head -1)"
    dst="$BUILD_DIR/mtsync_${VERSION}_${OS_ID}${OS_VERSION_ID}_${ARCH}.rpm"
    mv "$src" "$dst"
    echo "Built: $dst"
}

# ── AppImage ─────────────────────────────────────────────────────────────────

build_appimage() {
    log "Packaging AppImage"
    require wget
    ldconfig -p 2>/dev/null | grep -q libfuse.so.2 \
        || die "libfuse2 is required to run linuxdeploy's AppImage. Install it first."

    local appdir="$BUILD_DIR/AppDir"
    log "Installing to AppDir"
    rm -rf "$appdir"
    DESTDIR="$SCRIPT_DIR/$appdir" cmake --install "$BUILD_DIR"

    local linuxdeploy="$BUILD_DIR/linuxdeploy"
    local gtk_plugin="$BUILD_DIR/linuxdeploy-plugin-gtk.sh"
    if [[ ! -x "$linuxdeploy" ]]; then
        log "Downloading linuxdeploy"
        wget -q -O "$linuxdeploy" \
            https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
        chmod +x "$linuxdeploy"
    fi
    if [[ ! -f "$gtk_plugin" ]]; then
        wget -q -O "$gtk_plugin" \
            https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh
        chmod +x "$gtk_plugin"
    fi

    ( cd "$BUILD_DIR" && \
        DEPLOY_GTK_VERSION=4 VERSION="$VERSION" PATH="$SCRIPT_DIR/$BUILD_DIR:$PATH" \
        ./linuxdeploy --appdir AppDir --plugin gtk --output appimage )

    local src dst
    src="$(ls "$BUILD_DIR"/*.AppImage | head -1)"
    dst="$BUILD_DIR/mtsync_${VERSION}_${ARCH}.AppImage"
    mv "$src" "$dst"
    echo "Built: $dst"
}

# ── Flatpak ──────────────────────────────────────────────────────────────────

build_flatpak() {
    log "Packaging Flatpak"
    require flatpak-builder
    local bundle="$BUILD_DIR/mtsync_${VERSION}_${ARCH}.flatpak"
    flatpak-builder --force-clean --repo="$BUILD_DIR/flatpak-repo" \
        "$BUILD_DIR/flatpak-build" packaging/com.mtsync.MtSync.yml
    flatpak build-bundle "$BUILD_DIR/flatpak-repo" "$bundle" com.mtsync.MtSync
    echo "Built: $bundle"
}

# ── Snap ─────────────────────────────────────────────────────────────────────

build_snap() {
    log "Packaging Snap"
    require snapcraft
    snapcraft
    local src dst
    src="$(ls ./*.snap | head -1)"
    dst="$BUILD_DIR/$(basename "$src")"
    mv "$src" "$dst"
    echo "Built: $dst"
}

# ── Run requested packagers ─────────────────────────────────────────────────

[[ "$WANT_DEB"      -eq 1 ]] && build_deb
[[ "$WANT_RPM"      -eq 1 ]] && build_rpm
[[ "$WANT_APPIMAGE" -eq 1 ]] && build_appimage
[[ "$WANT_FLATPAK"  -eq 1 ]] && build_flatpak
[[ "$WANT_SNAP"     -eq 1 ]] && build_snap

log "Done"
