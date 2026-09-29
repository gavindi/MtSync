#!/usr/bin/env bash
# Install the build toolchain and the distro-provided libraries needed to
# build the AppImage on Ubuntu 22.04 (the AppImage's baseline). Run as root
# in a clean ubuntu:22.04 container.
#
# 22.04's GTK stack is too old for Mt. Sync (it needs libadwaita >= 1.5), so
# build-gtk-stack.sh builds glib, wayland, GTK, libadwaita and the C++
# bindings from source on top of what this installs.

set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq --no-install-recommends software-properties-common gpg-agent >/dev/null
# std::format needs GCC 13; jammy's own libstdc++ stays in use at runtime
# because the AppImage links libstdc++ statically (see build-appimage.sh).
add-apt-repository -y ppa:ubuntu-toolchain-r/test >/dev/null
apt-get update -qq

apt-get install -y -qq --no-install-recommends \
    build-essential gcc-13 g++-13 \
    git curl wget ca-certificates file unzip xz-utils patchelf \
    pkg-config gettext python3 python3-pip python3-packaging \
    libffi-dev libpcre2-dev zlib1g-dev libmount-dev libselinux1-dev \
    libexpat1-dev libxml2-dev \
    libpango1.0-dev libcairo2-dev libharfbuzz-dev libfribidi-dev \
    libfontconfig-dev libfreetype-dev \
    libgdk-pixbuf-2.0-dev librsvg2-common libgraphene-1.0-dev libepoxy-dev \
    libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxi-dev \
    libxcursor-dev libxdamage-dev libxfixes-dev libxinerama-dev \
    libpng-dev libjpeg-dev libtiff-dev libdrm-dev libegl-dev libgles-dev \
    libsoup-3.0-dev libappstream-dev sassc \
    nlohmann-json3-dev \
    >/dev/null

# jammy ships meson 0.61 and CMake 3.22; glib 2.80 needs meson >= 1.2 and
# Mt. Sync needs CMake >= 3.25.
pip3 install -q 'meson==1.5.2' 'ninja==1.11.1.1' 'cmake==3.30.5'

echo "Build dependencies installed: $(gcc-13 --version | head -1), meson $(meson --version), $(cmake --version | head -1)"
