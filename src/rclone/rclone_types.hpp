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

#include <nlohmann/json.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mtsync::rclone {

struct ProviderOption {
    std::string name;
    std::string help;
    std::string default_value;
    std::string type; // "string", "bool", "int", etc.
    bool required = false;
    bool is_password = false;
    bool advanced = false;
    bool exclusive = false;
    struct Example {
        std::string value;
        std::string help;
    };
    std::vector<Example> examples;
};

struct ProviderInfo {
    std::string name;
    std::string description;
    std::string prefix;
    std::vector<ProviderOption> options;

    bool needs_oauth() const {
        for (auto& o : options)
            if (o.name == "token") return true;
        return false;
    }
};

struct RemoteInfo {
    std::string name;
    std::string type;
    nlohmann::json params;
};

struct FileEntry {
    std::string name;
    std::string path;
    int64_t size = 0;
    std::string mod_time;
    std::string mime_type;
    bool is_dir = false;
};

// One line of output from `rclone check --combined -`
// status: '=' same | '-' missing from dest | '+' extra in dest | '*' different | '!' error
struct CheckEntry {
    char        status = '=';
    std::string path;   // relative path (matches FileEntry::path from lsjson -R)
};

struct SyncStats {
    int64_t bytes = 0;
    int64_t total_bytes = 0;
    int transfers = 0;
    int total_transfers = 0;
    int errors = 0;
    double speed = 0.0;
    double elapsed_time = 0.0;
    std::optional<double> eta;
    bool fatal_error = false;
};

struct AboutInfo {
    std::optional<int64_t> total;
    std::optional<int64_t> used;
    std::optional<int64_t> free;
};

struct JobStatus {
    int64_t id;
    bool finished = false;
    bool success = false;
    std::string error;
    std::string output_log; // raw captured log text, e.g. bisync's detailed abort reason
};

enum class JobType { Sync, Copy, Move, Mount };

NLOHMANN_JSON_SERIALIZE_ENUM(JobType, {
    {JobType::Sync,  "sync"},
    {JobType::Copy,  "copy"},
    {JobType::Move,  "move"},
    {JobType::Mount, "mount"},
})

struct Job {
    std::string id;
    JobType     type             = JobType::Sync;
    std::string source;
    std::string destination;
    bool        dry_run          = false;
    bool        bisync           = false;
    bool        bisync_force_deletes = false; // bypass bisync's delete-safety abort (bisync jobs only)
    bool        ignore_checksum  = true;
    std::string bandwidth;
    bool        schedule_enabled  = false;
    bool        mount_at_startup  = false;
    bool        active            = false;
    bool        running           = false;
    std::string vfs_cache_mode;   // off|minimal|writes|full (mount jobs only)
    std::string cron_minute      = "*";
    std::string cron_hour        = "*";
    std::string cron_day         = "*";
    std::string cron_month       = "*";
    std::string cron_weekday     = "*";
    std::string last_start;
    std::string last_run;
    std::string last_status;
    std::vector<std::string> includes;  // Files to include; empty = entire directory
    int         parallel_transfers = -1; // -1 = use global settings default
    int         retries            = -1; // -1 = use global settings default
    std::string extra_flags;             // arbitrary rclone flags appended at run time
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Job,
    id, type, source, destination, dry_run, bisync, bisync_force_deletes, ignore_checksum, bandwidth,
    schedule_enabled, mount_at_startup, active, running, vfs_cache_mode,
    cron_minute, cron_hour, cron_day, cron_month, cron_weekday,
    last_start, last_run, last_status, includes, parallel_transfers, retries,
    extra_flags)

// Async callback type used throughout
template <typename T>
using AsyncCallback = std::function<void(std::expected<T, std::string>)>;

} // namespace mtsync::rclone
