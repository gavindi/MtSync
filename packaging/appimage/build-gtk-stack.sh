#!/usr/bin/env bash
# Build the parts of Mt. Sync's GTK stack that Ubuntu 22.04 ships too old,
# into PREFIX (default /opt/mtsync-deps). Run after install-build-deps-jammy.sh.
#
#   glib 2.80, wayland 1.22 + wayland-protocols  (GTK 4.14 needs newer than jammy's)
#   GTK 4.14, libadwaita 1.5                     (Mt. Sync needs libadwaita >= 1.5)
#   libsigc++, glibmm, cairomm, pangomm, gtkmm   (C++ bindings, built as *static*
#                                                  libraries so the AppImage has a
#                                                  single, statically linked libstdc++)
#   nlohmann-json 3.11                           (jammy's 3.10 lacks a macro Mt. Sync uses)
#
# Everything else (pango, cairo, harfbuzz, gdk-pixbuf, libsoup, ...) comes from
# jammy's own packages. Each component leaves a stamp file in PREFIX, so a
# rerun (e.g. after a CI cache restore) only builds what is missing.
#
# Usage: packaging/appimage/build-gtk-stack.sh [PREFIX]

set -euo pipefail

PREFIX="${1:-/opt/mtsync-deps}"
WORK="${WORK:-/tmp/mtsync-deps-build}"

# name  version  sha256  url
SOURCES=(
  "glib 2.80.5 9f23a9de803c695bbfde7e37d6626b18b9a83869689dd79019bf3ae66c3e6771 https://download.gnome.org/sources/glib/2.80/glib-2.80.5.tar.xz"
  "wayland 1.22.0 1540af1ea698a471c2d8e9d288332c7e0fd360c8f1d12936ebb7e7cbc2425842 https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.22.0/downloads/wayland-1.22.0.tar.xz"
  "wayland-protocols 1.36 71fd4de05e79f9a1ca559fac30c1f8365fa10346422f9fe795f74d77b9ef7e92 https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.36/downloads/wayland-protocols-1.36.tar.xz"
  "gtk 4.14.5 5547f2b9f006b133993e070b87c17804e051efda3913feaca1108fa2be41e24d https://download.gnome.org/sources/gtk/4.14/gtk-4.14.5.tar.xz"
  "libadwaita 1.5.5 2322c49333b22a3bb2e0c8edc4b2d214a11abd3bfad2234f0685553bf9d2c427 https://download.gnome.org/sources/libadwaita/1.5/libadwaita-1.5.5.tar.xz"
  "libsigc++ 3.6.0 c3d23b37dfd6e39f2e09f091b77b1541fbfa17c4f0b6bf5c89baef7229080e17 https://download.gnome.org/sources/libsigc++/3.6/libsigc++-3.6.0.tar.xz"
  "glibmm 2.80.1 f1a0c0ec514e3774bf993396f17f72106b40912c7d7cc9d10da31ba15517e3f5 https://download.gnome.org/sources/glibmm/2.80/glibmm-2.80.1.tar.xz"
  "cairomm 1.18.0 b81255394e3ea8e8aa887276d22afa8985fc8daef60692eb2407d23049f03cfb https://www.cairographics.org/releases/cairomm-1.18.0.tar.xz"
  "pangomm 2.50.2 1bc5ab4ea3280442580d68318226dab36ceedfc3288f9d83711cf7cfab50a9fb https://download.gnome.org/sources/pangomm/2.50/pangomm-2.50.2.tar.xz"
  "gtkmm 4.14.0 9350a0444b744ca3dc69586ebd1b6707520922b6d9f4f232103ce603a271ecda https://download.gnome.org/sources/gtkmm/4.14/gtkmm-4.14.0.tar.xz"
)

# Per-component meson options (the C++ bindings name theirs differently)
MM="-Ddefault_library=static -Dbuild-documentation=false"
declare -A OPTS=(
  [glib]="-Dintrospection=disabled -Dtests=false -Dman-pages=disabled -Ddocumentation=false -Dsysprof=disabled"
  [wayland]="-Ddocumentation=false -Dtests=false -Ddtd_validation=false"
  [wayland-protocols]="-Dtests=false"
  [gtk]="-Dintrospection=disabled -Dmedia-gstreamer=disabled -Dprint-cups=disabled -Dvulkan=disabled -Dcloudproviders=disabled -Dsysprof=disabled -Dtracker=disabled -Dcolord=disabled -Dbuild-demos=false -Dbuild-examples=false -Dbuild-tests=false -Dbuild-testsuite=false -Ddocumentation=false -Dman-pages=false"
  [libadwaita]="-Dintrospection=disabled -Dvapi=false -Dtests=false -Dexamples=false -Dgtk_doc=false"
  [libsigc++]="$MM -Dbuild-examples=false -Dbuild-tests=false -Dvalidation=false"
  [glibmm]="$MM -Dbuild-examples=false"
  [cairomm]="$MM -Dbuild-examples=false -Dbuild-tests=false"
  [pangomm]="$MM"
  [gtkmm]="$MM -Dbuild-demos=false -Dbuild-tests=false"
)

export CC=gcc-13 CXX=g++-13
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# pangomm/gtkmm locate glibmm's static helper library with find_library(),
# which only searches the compiler's own library path
export LIBRARY_PATH="$PREFIX/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PATH="$PREFIX/bin:$PATH"

mkdir -p "$PREFIX" "$WORK"
for entry in "${SOURCES[@]}"; do
    read -r name version sha256 url <<<"$entry"
    stamp="$PREFIX/.built-$name-$version"
    if [[ -f "$stamp" ]]; then
        echo "== $name $version: already built"
        continue
    fi
    echo "== $name $version"
    tarball="$WORK/$(basename "$url")"
    [[ -f "$tarball" ]] || curl -fsSL -o "$tarball" "$url"
    echo "$sha256  $tarball" | sha256sum -c --quiet -
    rm -rf "$WORK/$name-$version"
    tar xJf "$tarball" -C "$WORK"
    ( cd "$WORK/$name-$version"
      # shellcheck disable=SC2086  # OPTS values are word lists
      meson setup _build --prefix="$PREFIX" --libdir=lib --buildtype=release \
          --wrap-mode=nodownload ${OPTS[$name]}
      meson compile -C _build
      meson install -C _build --quiet )
    rm -rf "$WORK/$name-$version"
    touch "$stamp"
done

# nlohmann-json >= 3.11 (NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT);
# jammy has 3.10.5. Header-only, installed with CMake.
JSON_VERSION=3.11.3
JSON_SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
if [[ ! -f "$PREFIX/.built-nlohmann-json-$JSON_VERSION" ]]; then
    echo "== nlohmann-json $JSON_VERSION"
    tarball="$WORK/json-$JSON_VERSION.tar.xz"
    [[ -f "$tarball" ]] || curl -fsSL -o "$tarball" \
        "https://github.com/nlohmann/json/releases/download/v$JSON_VERSION/json.tar.xz"
    echo "$JSON_SHA256  $tarball" | sha256sum -c --quiet -
    rm -rf "$WORK/json" && tar xJf "$tarball" -C "$WORK"
    cmake -S "$WORK/json" -B "$WORK/json/_build" -DJSON_BuildTests=OFF \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib >/dev/null
    cmake --install "$WORK/json/_build" >/dev/null
    rm -rf "$WORK/json"
    touch "$PREFIX/.built-nlohmann-json-$JSON_VERSION"
fi

echo "GTK stack ready in $PREFIX: gtk4 $(pkg-config --modversion gtk4), libadwaita $(pkg-config --modversion libadwaita-1), gtkmm $(pkg-config --modversion gtkmm-4.0)"
