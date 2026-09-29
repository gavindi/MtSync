#!/usr/bin/env bash
# Verify an AppImage is self-contained against the Ubuntu 24.04 baseline:
#   - every shared library an ELF in the bundle needs is either bundled or on
#     the host allowlist (libraries every desktop distro provides and which
#     must come from the host: glibc, the GPU/display stack, fonts),
#   - nothing needs a glibc / libstdc++ symbol version newer than 24.04 ships,
#   - the bundled rclone is present and runs.
#
# Usage: packaging/appimage/check-appimage.sh <file.AppImage>

set -euo pipefail

MAX_GLIBC="2.39"        # Ubuntu 24.04 glibc
MAX_GLIBCXX="3.4.33"    # Ubuntu 24.04 libstdc++ (GCC 14)

HOST_ALLOWLIST='^(ld-linux-x86-64\.so\.2|libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|libresolv\.so\.2|libutil\.so\.1|libstdc\+\+\.so\.6|libgcc_s\.so\.1|libGL\.so\.1|libGLX\.so\.0|libOpenGL\.so\.0|libEGL\.so\.1|libGLESv2\.so\.2|libgbm\.so\.1|libdrm\.so\.2|libwayland-client\.so\.0|libX11\.so\.6|libX11-xcb\.so\.1|libxcb\.so\.1|libfontconfig\.so\.1|libfreetype\.so\.6|libharfbuzz\.so\.0|libfribidi\.so\.0|libz\.so\.1|libexpat\.so\.1|libgmp\.so\.10|libgpg-error\.so\.0|libcom_err\.so\.2)$'

appimage="$(readlink -f "${1:?usage: $0 <file.AppImage>}")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
( cd "$work" && "$appimage" --appimage-extract >/dev/null )
root="$work/squashfs-root"

fail=0
elfs=()
while IFS= read -r -d '' f; do
    [[ "$(head -c4 "$f" 2>/dev/null)" == $'\x7fELF' ]] && elfs+=("$f")
done < <(find "$root" -type f -print0)

# ── Unresolved libraries ────────────────────────────────────────────────────
bundled="$(find "$root" \( -type f -o -type l \) -name '*.so*' -printf '%f\n' | sort -u)"
needed="$(for f in "${elfs[@]}"; do readelf -d "$f" 2>/dev/null; done \
          | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u)"
while IFS= read -r lib; do
    [[ -z "$lib" ]] && continue
    grep -qxF "$lib" <<<"$bundled" && continue
    [[ "$lib" =~ $HOST_ALLOWLIST ]] && continue
    echo "FAIL: $lib is neither bundled nor on the host allowlist" >&2
    fail=1
done <<<"$needed"

# ── Symbol-version ceiling ──────────────────────────────────────────────────
max_version() {  # $1 = GLIBC | GLIBCXX
    for f in "${elfs[@]}"; do objdump -T "$f" 2>/dev/null; done \
        | grep -oE "\b$1_[0-9]+(\.[0-9]+)+\b" | sed "s/^$1_//" | sort -Vu | tail -1
}
check_ceiling() {  # $1 = name, $2 = found, $3 = ceiling
    if [[ -n "$2" && "$(printf '%s\n%s\n' "$2" "$3" | sort -V | tail -1)" != "$3" ]]; then
        echo "FAIL: requires $1_$2, newer than the Ubuntu 24.04 baseline ($1_$3)" >&2
        fail=1
    else
        echo "ok: max $1_${2:-none} (ceiling $3)"
    fi
}
check_ceiling GLIBC   "$(max_version GLIBC)"   "$MAX_GLIBC"
check_ceiling GLIBCXX "$(max_version GLIBCXX)" "$MAX_GLIBCXX"

# ── Bundled rclone ──────────────────────────────────────────────────────────
if [[ -x "$root/usr/bin/rclone" ]] && "$root/usr/bin/rclone" version >/dev/null 2>&1; then
    echo "ok: bundled $("$root/usr/bin/rclone" version | head -1)"
else
    echo "FAIL: usr/bin/rclone missing or not runnable" >&2
    fail=1
fi

[[ "$fail" -eq 0 ]] && echo "AppImage check passed: $(basename "$appimage")"
exit "$fail"
