/*
 * Mt. Sync — GTK4 frontend to rclone
 * Copyright (C) 2026  Mt. Sync contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace mtsync::sandbox {

inline bool in_flatpak() {
    if (std::getenv("FLATPAK_ID")) return true;
    std::error_code ec;
    return std::filesystem::exists("/.flatpak-info", ec);
}

inline bool in_snap() {
    return std::getenv("SNAP") != nullptr;
}

// The AppImage runtime sets $APPIMAGE (path to the .AppImage file) and
// $APPDIR (its temporary FUSE mount point) before exec'ing AppRun.
inline bool in_appimage() {
    return std::getenv("APPIMAGE") != nullptr && std::getenv("APPDIR") != nullptr;
}

// Executable to re-launch ourselves with (daemon, GUI). Inside an AppImage,
// /proc/self/exe points into this process's FUSE mount, which the runtime
// unmounts as soon as the launching process exits — so a spawned daemon must
// go through $APPIMAGE to get a mount of its own. Returns "" if not found.
inline std::string self_exe() {
    if (in_appimage()) return std::getenv("APPIMAGE");
    std::error_code ec;
    // Prefer our own binary so a dev build doesn't launch the installed one
    if (std::filesystem::exists("/proc/self/exe", ec)) return "/proc/self/exe";
    gchar* found = g_find_program_in_path("mtsync");
    std::string exe = found ? found : "";
    g_free(found);
    return exe;
}

// rclone binary to run. The AppImage ships its own rclone in usr/bin and
// always uses it; everywhere else rclone comes from PATH.
inline std::string rclone_program() {
    if (in_appimage()) {
        auto bundled = std::filesystem::path(std::getenv("APPDIR")) / "usr/bin/rclone";
        std::error_code ec;
        if (std::filesystem::exists(bundled, ec)) return bundled.string();
    }
    return "rclone";
}

// Path to the real user home directory, bypassing sandbox redirection.
// Inside a Snap, $HOME is redirected to $SNAP_USER_DATA — use $SNAP_REAL_HOME.
// Inside Flatpak, the host home is already mounted at /home/$USER when
// --filesystem=home is granted, so g_get_home_dir() is correct.
inline std::string real_home() {
    if (const char* real = std::getenv("SNAP_REAL_HOME")) return real;
    return g_get_home_dir();
}

// Path to the real user XDG config dir on the host, bypassing sandbox
// redirection. Inside Flatpak, g_get_user_config_dir() returns the
// app-private ~/.var/app/<id>/config. Inside Snap, it returns
// $SNAP_USER_DATA/.config. Host-visible autostart entries must be written
// to the real ~/.config/ instead.
inline std::string real_config_dir() {
    if (in_flatpak() || in_snap()) return real_home() + "/.config";
    return g_get_user_config_dir();
}

// Exec= line for the autostart .desktop file, rewritten so it launches
// the sandboxed binary from the host session.
inline std::string autostart_exec() {
    if (in_flatpak()) {
        const char* id = std::getenv("FLATPAK_ID");
        return std::string("flatpak run --command=mtsync ")
             + (id ? id : "com.mtsync.MtSync") + " --daemon";
    }
    if (in_snap()) return "/snap/bin/mtsync --daemon";
    if (in_appimage()) {
        // Desktop Entry spec: quote the path and backslash-escape " ` $ \ inside
        // the quotes; the string-value escape rule then doubles each backslash.
        std::string quoted = "\"";
        for (char c : std::string(std::getenv("APPIMAGE"))) {
            if (c == '\\')                           quoted += "\\\\\\\\";
            else if (c == '"' || c == '`' || c == '$') { quoted += "\\\\"; quoted += c; }
            else                                     quoted += c;
        }
        return quoted + "\" --daemon";
    }
    return "mtsync --daemon";
}

} // namespace mtsync::sandbox
