#!/usr/bin/env bash
# Download a pinned rclone release and install it into an AppDir, so the
# AppImage is self-contained (sandbox::rclone_program() prefers it at runtime).
#
# Usage: packaging/appimage/fetch-rclone.sh <AppDir>
#
# To bump: set RCLONE_VERSION and take the linux-amd64.zip line from
# https://downloads.rclone.org/v<version>/SHA256SUMS.

set -euo pipefail

RCLONE_VERSION="1.75.1"
RCLONE_SHA256="982b5aa772841168f8e380f139e9e787b2a105403e32b94da8676a0e1c0a13ab"

APPDIR="${1:?usage: $0 <AppDir>}"

[[ "$(uname -m)" == "x86_64" ]] \
    || { echo "fetch-rclone.sh: only x86_64 is pinned (got $(uname -m))" >&2; exit 1; }

name="rclone-v${RCLONE_VERSION}-linux-amd64"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fsSL -o "$tmp/$name.zip" "https://downloads.rclone.org/v${RCLONE_VERSION}/${name}.zip"
echo "${RCLONE_SHA256}  $tmp/$name.zip" | sha256sum -c --quiet -
unzip -q "$tmp/$name.zip" -d "$tmp"

install -Dm755 "$tmp/$name/rclone" "$APPDIR/usr/bin/rclone"
curl -fsSL -o "$tmp/COPYING" "https://raw.githubusercontent.com/rclone/rclone/v${RCLONE_VERSION}/COPYING"
install -Dm644 "$tmp/COPYING"      "$APPDIR/usr/share/doc/rclone/COPYING"

echo "Bundled $("$APPDIR/usr/bin/rclone" version | head -1) into $APPDIR"
