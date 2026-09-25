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

#include "rclone_types.hpp"
#include <giomm.h>
#include <string>
#include <vector>

namespace mtsync::rclone {

class RcloneCli {
public:
    explicit RcloneCli(std::string rclone_path = "rclone");

    // Config operations (async, results dispatched on GTK main thread)
    void list_remotes(AsyncCallback<std::vector<RemoteInfo>> callback);
    void get_providers(AsyncCallback<std::vector<ProviderInfo>> callback);
    void config_create(const std::string& name, const std::string& type,
                       const std::vector<std::pair<std::string, std::string>>& params,
                       AsyncCallback<std::monostate> callback);
    void config_update(const std::string& name,
                       const std::vector<std::pair<std::string, std::string>>& params,
                       AsyncCallback<std::monostate> callback);
    void config_delete(const std::string& name, AsyncCallback<std::monostate> callback);

    // OAuth authorization — opens browser, returns token JSON string
    void authorize(const std::string& backend,
                   const std::string& client_id,
                   const std::string& client_secret,
                   AsyncCallback<std::string> callback);

    // rclone binary version string (e.g. "v1.60.1-DEV")
    void get_version(AsyncCallback<std::string> callback);

    // File listing
    void lsjson(const std::string& remote_path,
                AsyncCallback<std::vector<FileEntry>> callback);

    // File operations
    void delete_files(const std::string& dir,
                      const std::vector<std::string>& includes,
                      AsyncCallback<std::monostate> callback);

    // Copy specific files from src to dst; includes are absolute rclone filter patterns (e.g. "/sub/file.txt")
    void copy_files(const std::string& src, const std::string& dst,
                    const std::vector<std::string>& includes,
                    AsyncCallback<std::monostate> callback);

    void mkdir(const std::string& path, AsyncCallback<std::monostate> callback);

    // Recursive lsjson — same as lsjson() but passes -R, returns all files recursively
    // Returns the Gio::Subprocess handle so callers can cancel if needed.
    Glib::RefPtr<Gio::Subprocess> lsjson_r(const std::string& remote_path,
                                            AsyncCallback<std::vector<FileEntry>> callback);

    // rclone check SRC DST --combined - ; non-zero exit = diffs found, not an error
    // Returns the Gio::Subprocess handle so callers can cancel if needed.
    Glib::RefPtr<Gio::Subprocess> check(const std::string& src, const std::string& dst,
                                         AsyncCallback<std::vector<CheckEntry>> callback);

    // rclone sync|copy|move SRC DST --dry-run for `job`, reporting each action it
    // would have taken. Flags mirror MtSyncDaemon::on_run_job; `global_flags` and
    // the job's extra_flags are raw CLI tokens. Errors only when rclone failed
    // without reporting a single action.
    // Returns the Gio::Subprocess handle so callers can cancel if needed.
    Glib::RefPtr<Gio::Subprocess> dry_run(const Job& job, int default_transfers,
                                           const std::string& global_flags,
                                           AsyncCallback<std::vector<DryRunAction>> callback);

    // Parse the stderr of a `--dry-run --use-json-log` run into its actions.
    // Lines that are not JSON, or carry no "skipped"/error object, are ignored.
    static std::vector<DryRunAction> parse_dry_run_log(const std::string& log);

private:
    std::string m_rclone_path;
    std::string m_config_path;

    using CompletionHandler = std::function<void(const std::string& stdout_data,
                                                  const std::string& stderr_data,
                                                  int exit_code)>;
    Glib::RefPtr<Gio::Subprocess> run_command(std::vector<std::string> args,
                                               CompletionHandler on_complete);
};

} // namespace mtsync::rclone
