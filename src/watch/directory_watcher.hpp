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

#include <giomm.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mtsync::watch {

// True if `p` is a local filesystem path rather than an rclone remote spec
// ("remote:path"). rclone remote names never contain '/', so a colon that
// appears before the first slash is a remote prefix; anything else (absolute
// path, ~, ./, ../, or a bare relative path with a colon later in it) is local.
inline bool is_local_path(const std::string& p) {
    if (p.empty()) return true;
    if (p.front() == '/' || p.front() == '~' ||
        p.rfind("./", 0) == 0 || p.rfind("../", 0) == 0)
        return true;
    auto colon = p.find(':');
    auto slash = p.find('/');
    return !(colon != std::string::npos && (slash == std::string::npos || colon < slash));
}

// Recursively watches a local directory tree for filesystem changes using
// Gio::FileMonitor (inotify on Linux). Gio::FileMonitor, like raw inotify,
// only watches one directory's direct children — recursion is achieved by
// maintaining one monitor per directory and adding/removing monitors as
// subdirectories are created/deleted.
//
// This class does NOT debounce — on_change fires on every raw filesystem
// event. Debouncing/coalescing lives in the caller (the daemon), since it
// needs to share state with job scheduling/execution.
class DirectoryWatcher {
public:
    // on_change: invoked (possibly many times) for any change anywhere in the tree.
    // on_error: invoked at most once, if a directory monitor fails to set up
    //           (e.g. fs.inotify.max_user_watches exhausted). Watching continues
    //           with whatever monitors were already created successfully.
    DirectoryWatcher(std::string root,
                      std::function<void()> on_change,
                      std::function<void(const std::string&)> on_error);
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

private:
    struct WatchedDir {
        Glib::RefPtr<Gio::FileMonitor> monitor;
        sigc::connection               changed_conn;
        int                            depth = 0;
    };

    void add_watch_recursive(const std::string& dir_path, int depth);
    void remove_watch(const std::string& dir_path);
    void on_monitor_event(Gio::FileMonitor::Event event_type,
                          const Glib::RefPtr<Gio::File>& file,
                          const Glib::RefPtr<Gio::File>& other_file,
                          const std::string& watched_dir_path);

    std::string m_root;
    std::unordered_map<std::string, WatchedDir> m_watched;
    std::unordered_set<std::string>             m_visited_real_paths;
    std::function<void()>                       m_on_change;
    std::function<void(const std::string&)>     m_on_error;
    bool                                         m_error_reported = false;

    static constexpr int MAX_RECURSION_DEPTH = 32;
};

} // namespace mtsync::watch
