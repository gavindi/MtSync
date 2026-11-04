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

#include <filesystem>
#include <string>
#include <vector>

namespace mtsync::activity_log {

struct EntryToDelete {
    std::string raw_line;        // exact line as read from mtsync.log
    std::string error_log_path;  // optional referenced file under error_log_dir()
};

std::filesystem::path log_file_path();
std::filesystem::path error_log_dir();

// Append a timestamped line to the activity log. Safe to call from the daemon
// and the GUI at the same time.
void append_line(const std::string& message);

// Delete activity log lines and error log files older than retention_days.
// A value <= 0 keeps everything.
void prune(int retention_days);

// Remove the given lines from the activity log (one occurrence per entry) and
// delete their referenced error log files. Returns false if the log could not
// be rewritten.
bool delete_entries(const std::vector<EntryToDelete>& entries);

} // namespace mtsync::activity_log
