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

#include "log/activity_log.hpp"

#include <glib.h>
#include <glibmm.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <unordered_map>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace mtsync::activity_log {

namespace {

// Keeps appends from the daemon and rewrites from the GUI from racing each
// other. The lock is a sidecar file because rewrites replace the log inode.
class FileLock {
public:
    explicit FileLock(const fs::path& path) {
        m_fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (m_fd < 0) return;
        while (::flock(m_fd, LOCK_EX) != 0 && errno == EINTR) {}
    }

    ~FileLock() {
        if (m_fd >= 0) {
            ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
        }
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    int m_fd = -1;
};

fs::path lock_file_path() {
    return log_file_path().parent_path() / "mtsync.log.lock";
}

// Extracts the leading "[YYYY-MM-DD HH:MM:SS]" timestamp, if present.
bool leading_timestamp(const std::string& line, std::string& out) {
    if (line.size() < 21 || line[0] != '[' || line[20] != ']')
        return false;
    for (size_t i = 1; i < 20; ++i) {
        if (i == 4 || i == 7) {
            if (line[i] != '-') return false;
        } else if (i == 10) {
            if (line[i] != ' ') return false;
        } else if (i == 13 || i == 16) {
            if (line[i] != ':') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
            return false;
        }
    }
    out.assign(line, 1, 19);
    return true;
}

bool rewrite_atomic(const fs::path& path, const std::vector<std::string>& lines) {
    auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        for (const auto& line : lines)
            out << line << '\n';
        if (!out.good()) {
            out.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

void delete_error_log(const std::string& path) {
    if (path.empty()) return;

    std::error_code ec;
    auto base = fs::weakly_canonical(error_log_dir(), ec);
    if (ec) return;

    auto file = fs::weakly_canonical(fs::path(path), ec);
    if (ec || file.parent_path() != base) return;

    if (!fs::is_regular_file(file, ec) || ec) return;
    fs::remove(file, ec);
}

void prune_error_logs(int retention_days) {
    std::error_code ec;
    auto dir = error_log_dir();
    if (!fs::is_directory(dir, ec) || ec) return;

    auto cutoff = fs::file_time_type::clock::now()
                - std::chrono::hours(24) * retention_days;

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || file_ec) continue;
        auto mtime = fs::last_write_time(it->path(), file_ec);
        if (file_ec) continue;
        if (mtime < cutoff)
            fs::remove(it->path(), file_ec);
    }
}

} // namespace

fs::path log_file_path() {
    return fs::path(g_get_user_state_dir()) / "mtsync" / "mtsync.log";
}

fs::path error_log_dir() {
    return fs::path(g_get_user_state_dir()) / "mtsync" / "errors";
}

void append_line(const std::string& message) {
    auto path = log_file_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    FileLock lock(lock_file_path());

    std::ofstream f(path, std::ios::app);
    if (!f) return;
    auto ts = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    f << "[" << ts.raw() << "] " << message << "\n";
}

void prune(int retention_days) {
    if (retention_days <= 0) return;

    const int days = std::clamp(retention_days, 0, 36500);
    auto path = log_file_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    {
        FileLock lock(lock_file_path());

        std::ifstream in(path);
        if (in) {
            auto cutoff = Glib::DateTime::create_now_local()
                              .add_days(-days)
                              .format("%Y-%m-%d %H:%M:%S");

            std::vector<std::string> kept;
            std::string line;
            int removed = 0;
            while (std::getline(in, line)) {
                std::string ts;
                if (leading_timestamp(line, ts) && ts < cutoff.raw()) {
                    ++removed;
                    continue;
                }
                kept.push_back(std::move(line));
            }
            in.close();

            if (removed > 0 && !rewrite_atomic(path, kept))
                g_warning("activity_log: failed to rewrite %s", path.c_str());
        }
    }

    prune_error_logs(days);
}

bool delete_entries(const std::vector<EntryToDelete>& entries) {
    if (entries.empty()) return true;

    auto path = log_file_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::unordered_map<std::string, int> remaining;
    for (const auto& entry : entries)
        ++remaining[entry.raw_line];

    bool ok = true;
    {
        FileLock lock(lock_file_path());

        std::ifstream in(path);
        if (in) {
            std::vector<std::string> kept;
            std::string line;
            int removed = 0;
            while (std::getline(in, line)) {
                auto it = remaining.find(line);
                if (it != remaining.end() && it->second > 0) {
                    --it->second;
                    ++removed;
                    continue;
                }
                kept.push_back(std::move(line));
            }
            in.close();

            if (removed > 0 && !rewrite_atomic(path, kept)) {
                g_warning("activity_log: failed to rewrite %s", path.c_str());
                ok = false;
            }
        }
    }

    if (ok) {
        for (const auto& entry : entries)
            delete_error_log(entry.error_log_path);
    }

    return ok;
}

} // namespace mtsync::activity_log
