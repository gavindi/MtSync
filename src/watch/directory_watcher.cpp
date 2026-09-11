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

#include "watch/directory_watcher.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace mtsync::watch {

DirectoryWatcher::DirectoryWatcher(std::string root,
                                    std::function<void()> on_change,
                                    std::function<void(const std::string&)> on_error)
    : m_root(std::move(root))
    , m_on_change(std::move(on_change))
    , m_on_error(std::move(on_error)) {
    add_watch_recursive(m_root, 0);
}

DirectoryWatcher::~DirectoryWatcher() {
    for (auto& [path, wd] : m_watched)
        wd.changed_conn.disconnect();
    m_watched.clear();
}

void DirectoryWatcher::add_watch_recursive(const std::string& dir_path, int depth) {
    std::error_code ec;
    auto canonical = fs::canonical(dir_path, ec);
    if (ec) return; // gone, or a permission error resolving it — skip this subtree

    std::string canon_str = canonical.string();
    if (m_visited_real_paths.contains(canon_str)) return; // symlink loop / already-watched target
    if (depth > MAX_RECURSION_DEPTH) {
        if (!m_error_reported) {
            m_error_reported = true;
            if (m_on_error)
                m_on_error("directory tree exceeds maximum watch depth (" +
                    std::to_string(MAX_RECURSION_DEPTH) + "); some subdirectories are not watched");
        }
        return;
    }

    Glib::RefPtr<Gio::FileMonitor> monitor;
    try {
        monitor = Gio::File::create_for_path(dir_path)->monitor_directory();
    } catch (const Glib::Error& e) {
        if (!m_error_reported) {
            m_error_reported = true;
            if (m_on_error) m_on_error(e.what());
        }
        return; // stop recursing further, but keep monitors already created
    }

    m_visited_real_paths.insert(canon_str);

    WatchedDir wd;
    wd.depth = depth;
    wd.changed_conn = monitor->signal_changed().connect(
        [this, dir_path](const Glib::RefPtr<Gio::File>& file,
                          const Glib::RefPtr<Gio::File>& other_file,
                          Gio::FileMonitor::Event event_type) {
            on_monitor_event(event_type, file, other_file, dir_path);
        });
    wd.monitor = monitor;
    m_watched[dir_path] = std::move(wd);

    std::error_code it_ec;
    fs::directory_iterator it(dir_path, fs::directory_options::skip_permission_denied, it_ec);
    if (it_ec) return; // unreadable — parent's monitor still reports if this dir is deleted/renamed

    for (const auto& entry : it) {
        std::error_code type_ec;
        if (entry.is_directory(type_ec) && !type_ec)
            add_watch_recursive(entry.path().string(), depth + 1);
    }
}

void DirectoryWatcher::remove_watch(const std::string& dir_path) {
    auto prefix = dir_path + "/";
    for (auto it = m_watched.begin(); it != m_watched.end(); ) {
        if (it->first == dir_path || it->first.starts_with(prefix)) {
            it->second.changed_conn.disconnect();
            it = m_watched.erase(it);
        } else {
            ++it;
        }
    }
}

void DirectoryWatcher::on_monitor_event(Gio::FileMonitor::Event event_type,
                                        const Glib::RefPtr<Gio::File>& file,
                                        const Glib::RefPtr<Gio::File>& /*other_file*/,
                                        const std::string& watched_dir_path) {
    if (m_on_change) m_on_change();

    // Monitors are created without WATCH_MOVES, so renames arrive as a plain
    // DELETED (old path) + CREATED (new path) pair rather than RENAMED/
    // MOVED_IN/MOVED_OUT — simpler to reason about and sufficient here, since
    // all that matters is keeping the per-directory monitor set in sync.
    switch (event_type) {
        case Gio::FileMonitor::Event::CREATED: {
            if (!file) break;
            std::error_code ec;
            if (fs::is_directory(file->get_path(), ec) && !ec) {
                auto parent_it = m_watched.find(watched_dir_path);
                int parent_depth = (parent_it != m_watched.end()) ? parent_it->second.depth : 0;
                add_watch_recursive(file->get_path(), parent_depth + 1);
            }
            break;
        }
        case Gio::FileMonitor::Event::DELETED: {
            if (!file) break;
            std::string path = file->get_path();
            if (m_watched.contains(path)) remove_watch(path);
            break;
        }
        default:
            break;
    }
}

} // namespace mtsync::watch
