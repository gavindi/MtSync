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

#include "rclone/rclone_manager.hpp"
#include "tray.hpp"
#include "notification.hpp"
#include "ipc/server.hpp"
#include "ipc/protocol.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sigc++/sigc++.h>

namespace fs = std::filesystem;
namespace rclone = mtsync::rclone;

namespace mtsync {

class MtSyncDaemon {
public:
    MtSyncDaemon();
    ~MtSyncDaemon();

    void run();
    void stop();

private:
    void load_jobs();
    void save_jobs();
    void schedule_all_jobs();
    void schedule_job(size_t index);
    void on_run_job(size_t index);
    void on_job_completed(size_t index, bool success, const std::string& error_msg = "",
                           const std::string& output_log = "");

    struct JobState {
        sigc::connection  poll_timer;
        sigc::connection  sched_timer;
        sigc::connection  retry_timer;
        int64_t           job_id         = -1;
        uint8_t           submitting     = 0; // on_run_job called but RC hasn't returned ID yet
        uint8_t           poll_in_flight = 0; // poll HTTP request pending, skip next tick
        int               poll_failures  = 0; // consecutive failed status polls; job may still be alive on rcd
        int               retry_count    = 0;
        bool              bisync_force_resync   = false; // consumed once by on_run_job to add resync:true
        bool              bisync_resync_retried = false; // single-shot guard against retry loops
        rclone::SyncStats last_stats;
    };

    rclone::RcloneManager m_manager;
    std::vector<rclone::Job>   m_jobs;
    std::vector<JobState>      m_job_state;
    std::string m_config_path;

    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<ipc::IpcServer> m_ipc_server;

    int m_running_job_count = 0;

    sigc::connection m_mount_health_timer;

    void update_tray_animation();

    bool m_running      = true;
    bool m_quit_pending = false;
};

} // namespace mtsync
