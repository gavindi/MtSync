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

#include "daemon.hpp"
#include "rclone/cron_utils.hpp"
#include "ipc/protocol.hpp"
#include "settings.hpp"
#include <format>
#include <iostream>
#include <unordered_set>
#include <sstream>
#include <glibmm.h>
#include <glib-unix.h>

using json = nlohmann::json;

namespace {

json make_response(mtsync::ipc::ResponseType type, const json& payload,
                   const json& request = json{}) {
    json resp = {
        {"type", type},
        {"payload", payload}
    };
    if (request.contains("request_id")) {
        resp["request_id"] = request["request_id"];
    }
    return resp;
}

const char* type_str(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return "SYNC";
        case rclone::JobType::Copy:  return "COPY";
        case rclone::JobType::Move:  return "MOVE";
        case rclone::JobType::Mount: return "MOUNT";
    }
    return "?";
}

std::string format_bytes(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string format_speed(double bps) {
    if (bps < 1024) return std::format("{:.0f} B/s", bps);
    if (bps < 1024 * 1024) return std::format("{:.1f} KB/s", bps / 1024);
    if (bps < 1024 * 1024 * 1024) return std::format("{:.1f} MB/s", bps / (1024.0 * 1024));
    return std::format("{:.1f} GB/s", bps / (1024.0 * 1024 * 1024));
}

std::string format_duration(double seconds) {
    auto total_secs = static_cast<int64_t>(seconds);
    if (total_secs < 60) return std::format("{}s", total_secs);
    auto mins = total_secs / 60;
    auto secs = total_secs % 60;
    if (mins < 60) return std::format("{}m {}s", mins, secs);
    auto hrs = mins / 60;
    auto rem_mins = mins % 60;
    return std::format("{}h {}m {}s", hrs, rem_mins, secs);
}

void append_log(const std::string& line) {
    auto dir = fs::path(g_get_user_state_dir()) / "mtsync";
    fs::create_directories(dir);
    std::ofstream f(dir / "mtsync.log", std::ios::app);
    if (!f) return;
    auto ts = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    f << "[" << ts.raw() << "] " << line << "\n";
}

static std::string sanitize_filename(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        out += (std::isalnum(c) || c == '-' || c == '_' || c == '.') ? (char)c : '_';
    return out.empty() ? "job" : out;
}

static std::string write_error_log(const rclone::Job& job, const std::string& error_msg) {
    auto dir = fs::path(g_get_user_state_dir()) / "mtsync" / "errors";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return "";

    auto ts  = Glib::DateTime::create_now_local().format("%Y-%m-%d-%H-%M-%S");
    auto pos = job.source.rfind('/');
    auto src = (pos != std::string::npos) ? job.source.substr(pos + 1) : job.source;
    auto fname = sanitize_filename(src) + "-" + std::string(ts.raw()) + ".log";
    auto path  = dir / fname;

    std::ofstream f(path);
    if (!f) return "";

    auto human_ts = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    f << "MtSync Error Log\n================\n"
      << "Timestamp:   " << human_ts.raw()     << "\n"
      << "Job ID:      " << job.id             << "\n"
      << "Type:        " << type_str(job.type) << "\n"
      << "Source:      " << job.source         << "\n"
      << "Destination: " << job.destination    << "\n"
      << "\nError:\n"    << error_msg          << "\n";

    return f.good() ? path.string() : "";
}

} // namespace

namespace mtsync {

MtSyncDaemon::MtSyncDaemon() {
    auto config_dir = fs::path(g_get_user_config_dir()) / "mtsync";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "jobs.json").string();

    load_jobs();

    m_tray = std::make_unique<TrayIcon>();
    m_tray->set_tooltip("Mt. Sync - rclone GUI");
    m_tray->signal_show_window().connect([this]() {
        if (m_ipc_server->client_count() > 0) {
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::ShowWindow, {}));
        } else {
            // No GUI connected — launch one; --show overrides start_minimized.
            // Prefer our own binary so a dev build doesn't launch the installed one.
            std::string exe = "/proc/self/exe";
            if (!fs::exists(exe)) {
                exe = Glib::find_program_in_path("mtsync");
                if (exe.empty()) return;
            }
            try {
                Glib::spawn_async({}, {exe, "--show"});
            } catch (const Glib::Error& e) {
                g_warning("Failed to launch GUI: %s", e.what());
            }
        }
    });
    m_tray->signal_quit().connect([this]() {
        stop();
    });

    m_ipc_server = std::make_unique<ipc::IpcServer>();
    m_ipc_server->signal_message().connect([this](const json& msg) {
        auto msg_type = msg.value("type", "");
        auto payload = msg.value("payload", json{});

        try {
            if (msg_type == "get_jobs") {
                json response_payload = {{"jobs", m_jobs}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobsList, response_payload, msg));

            } else if (msg_type == "add_job") {
                m_jobs.push_back(payload.get<rclone::Job>());
                m_job_state.resize(m_jobs.size());
                save_jobs();
                schedule_all_jobs();
                json response_payload = {{"index", m_jobs.size() - 1}, {"job", m_jobs.back()}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobAdded, response_payload, msg));

            } else if (msg_type == "update_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    m_jobs[index] = payload.value("job", rclone::Job{});
                    save_jobs();
                    schedule_all_jobs();
                    json response_payload = {{"index", index}, {"job", m_jobs[index]}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobUpdated, response_payload, msg));
                }

            } else if (msg_type == "delete_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    // If a non-mount job is actively running, its in-flight HTTP callbacks
                    // will be ignored (UUID mismatch after erasure), so adjust the counter now.
                    if (m_jobs[index].type != rclone::JobType::Mount
                        && index < m_job_state.size() && m_job_state[index].job_id >= 0) {
                        m_running_job_count--;
                        if (m_running_job_count < 0) m_running_job_count = 0;
                        update_tray_animation();
                    }
                    // Disconnect timers, then erase both vectors in one step each.
                    if (index < m_job_state.size()) {
                        m_job_state[index].sched_timer.disconnect();
                        m_job_state[index].poll_timer.disconnect();
                        m_job_state[index].retry_timer.disconnect();
                        m_job_state.erase(m_job_state.begin() + index);
                    }
                    m_jobs.erase(m_jobs.begin() + index);
                    save_jobs();
                    json response_payload = {{"index", index}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobDeleted, response_payload, msg));
                }

            } else if (msg_type == "run_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size()) {
                    on_run_job(index);
                    json response_payload = {{"index", index}};
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload, msg));
                }

            } else if (msg_type == "stop_job") {
                auto index = payload.value("index", static_cast<size_t>(-1));
                if (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount) {
                    std::string job_uuid = m_jobs[index].id;
                    m_manager.rc().unmount_async(m_jobs[index].destination, [this, index, job_uuid](auto) {
                        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                        m_jobs[index].active = false;
                        append_log(std::format("STOPPED   {} [{}] user stopped",
                            m_jobs[index].id, type_str(m_jobs[index].type)));
                        json response_payload = {{"index", index}, {"success", true}};
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    });
                } else if (index < m_job_state.size() && m_job_state[index].job_id >= 0) {
                    m_job_state[index].retry_timer.disconnect();
                    std::string job_uuid = m_jobs[index].id;
                    m_manager.rc().job_stop(m_job_state[index].job_id, [this, index, msg, job_uuid](auto) {
                        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                        if (index >= m_job_state.size() || m_job_state[index].job_id < 0) return;
                        m_job_state[index].poll_timer.disconnect();
                        m_job_state[index].job_id = -1;
                        m_jobs[index].running = false;
                        save_jobs();
                        append_log(std::format("STOPPED   {} [{}] user stopped",
                            m_jobs[index].id, type_str(m_jobs[index].type)));
                        m_running_job_count--;
                        if (m_running_job_count < 0) m_running_job_count = 0;
                        update_tray_animation();
                        json response_payload = {{"index", index}};
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStopped, response_payload, msg));
                    });
                }

            } else if (msg_type == "get_remotes") {
                m_manager.cli().list_remotes([this, msg](std::expected<std::vector<rclone::RemoteInfo>, std::string> result) {
                    json response_payload;
                    if (result.has_value()) {
                        response_payload = {{"remotes", json::array()}};
                        for (auto& remote : result.value()) {
                            response_payload["remotes"].push_back({
                                {"name", remote.name},
                                {"type", remote.type}
                            });
                        }
                    } else {
                        response_payload = {{"error", result.error()}};
                    }
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::RemotesList, response_payload, msg));
                });

            } else if (msg_type == "quit") {
                if (!m_quit_pending) {
                    m_quit_pending = true;
                    // Defer stop() to the next event-loop tick so we don't destroy
                    // the IpcServer (and its m_clients map) while still inside
                    // IpcServer::read_from_client() — which would be use-after-free.
                    Glib::signal_idle().connect_once([this]() { stop(); });
                }

            } else {
                json response_payload = {{"error", "Unknown request type: " + msg_type}};
                m_ipc_server->send_to_all(make_response(ipc::ResponseType::Error, response_payload, msg));
            }
        } catch (const std::exception& e) {
            json response_payload = {{"error", std::string(e.what())}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::Error, response_payload, msg));
        }
    });

    m_manager.rc().ensure_daemon([this](auto result) {
        if (!m_running) return;
        if (!result.has_value()) return;

        // First, check which mount points are already active (e.g. from a previous session)
        m_manager.rc().list_mounts([this](auto result) {
            if (!m_running) return;
            std::set<std::string> active_mounts;
            if (result.has_value()) {
                active_mounts = std::set<std::string>(result.value().begin(), result.value().end());
            }

            // For each mount job, verify if it's actually mounted or needs auto-start
            for (size_t i = 0; i < m_jobs.size(); ++i) {
                if (m_jobs[i].type != rclone::JobType::Mount) continue;

                // Extract the mount point (destination) from the job
                std::string mount_point = m_jobs[i].destination;

                if (active_mounts.count(mount_point)) {
                    // Mount already exists — mark as running/active
                    m_jobs[i].running = true;
                    m_jobs[i].active = true;
                } else if (m_jobs[i].mount_at_startup) {
                    // Not mounted but flagged for auto-mount — start it
                    on_run_job(i);
                }
            }

            save_jobs();

            // Broadcast updated job list so any connected GUI sees the correct state
            json response_payload = {{"jobs", m_jobs}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobsList, response_payload));
        });
    });

    schedule_all_jobs();

    // Periodically verify mount liveness via rclone RC (every 60s).
    // Detects mounts that silently died (network loss, rclone crash)
    // so users/file managers don't access dead FUSE mount points.
    m_mount_health_timer = Glib::signal_timeout().connect([this]() -> bool {
        if (!m_running) return false;
        bool has_active_mount = false;
        for (auto& j : m_jobs)
            if (j.type == rclone::JobType::Mount && j.active) { has_active_mount = true; break; }
        if (!has_active_mount) return true;
        m_manager.rc().list_mounts([this](auto result) {
            if (!m_running) return;
            std::unordered_set<std::string> live_mounts;
            if (result.has_value()) {
                live_mounts = std::unordered_set<std::string>(
                    result.value().begin(), result.value().end());
            }
            bool changed = false;
            for (size_t i = 0; i < m_jobs.size(); ++i) {
                if (m_jobs[i].type != rclone::JobType::Mount) continue;
                if (!m_jobs[i].active) continue;
                if (!live_mounts.count(m_jobs[i].destination)) {
                    m_jobs[i].active = false;
                    m_jobs[i].running = false;
                    changed = true;
                    append_log(std::format("STALE     {} [MOUNT] {} no longer mounted",
                        m_jobs[i].id, m_jobs[i].destination));
                    json rp = {{"index", i}, {"success", false}};
                    m_ipc_server->send_to_all(
                        make_response(ipc::ResponseType::JobCompleted, rp));
                }
            }
            if (changed) save_jobs();
        });
        return true;
    }, 60000);
}

MtSyncDaemon::~MtSyncDaemon() {
    stop();
}

void MtSyncDaemon::stop() {
    m_running = false;

    for (auto& s : m_job_state) {
        s.poll_timer.disconnect();
        s.sched_timer.disconnect();
        s.retry_timer.disconnect();
    }
    m_mount_health_timer.disconnect();
    m_job_state.clear();

    m_ipc_server.reset();
    m_tray.reset();

    m_manager.rc().stop_daemon();
}

void MtSyncDaemon::run() {
    if (!m_ipc_server->start()) {
        g_error("Failed to start IPC server");
        return;
    }

    // Handle SIGTERM/SIGINT via GLib's signal sources so the callback is delivered
    // on the main loop rather than interrupting a syscall. std::signal() + SA_RESTART
    // (the Linux default) would silently restart g_main_context_iteration, making the
    // daemon unresponsive to kill signals.
    guint sigterm_src = g_unix_signal_add(SIGTERM, [](gpointer data) -> gboolean {
        static_cast<MtSyncDaemon*>(data)->stop();
        return G_SOURCE_REMOVE;
    }, this);
    guint sigint_src = g_unix_signal_add(SIGINT, [](gpointer data) -> gboolean {
        static_cast<MtSyncDaemon*>(data)->stop();
        return G_SOURCE_REMOVE;
    }, this);

    g_message("Mt. Sync daemon started");

    while (m_running) {
        g_main_context_iteration(nullptr, true);
    }

    g_message("Mt. Sync daemon stopped");

    g_source_remove(sigterm_src);
    g_source_remove(sigint_src);
}

void MtSyncDaemon::load_jobs() {
    try {
        std::ifstream f(m_config_path);
        if (f) {
            auto j = json::parse(f);
            if (j.contains("jobs")) {
                for (auto& p : j["jobs"])
                    m_jobs.push_back(p.get<rclone::Job>());
            }
        }
    } catch (const std::exception& e) {
        g_warning("Failed to load jobs: %s", e.what());
    }
    // In-flight state is not valid across daemon restarts — reset unconditionally.
    for (auto& job : m_jobs) {
        if (job.type == rclone::JobType::Mount)
            job.active = false;
        job.running = false;
    }
    m_job_state.resize(m_jobs.size());
}

void MtSyncDaemon::save_jobs() {
    json j;
    j["jobs"] = json::array();
    for (auto& job : m_jobs)
        j["jobs"].push_back(job);

    auto target = fs::path(m_config_path);
    fs::create_directories(target.parent_path());
    auto tmp = target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream f(tmp);
        if (!f) { g_warning("save_jobs: cannot write to %s", tmp.c_str()); return; }
        f << j.dump(2);
        if (!f.good()) { g_warning("save_jobs: write error on %s", tmp.c_str()); fs::remove(tmp); return; }
    }
    try { fs::rename(tmp, target); }
    catch (const fs::filesystem_error& e) {
        g_warning("save_jobs: rename failed: %s", e.what());
        fs::remove(tmp);
    }
}

void MtSyncDaemon::schedule_all_jobs() {
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].schedule_enabled) {
            schedule_job(i);
        }
    }
}

void MtSyncDaemon::schedule_job(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];
    if (!job.schedule_enabled) return;

    GDateTime* now = g_date_time_new_now_local();
    GDateTime* next = cron::next_occurrence(job, now);
    g_date_time_unref(now);

    if (!next) return;

    GDateTime* now2 = g_date_time_new_now_local();
    gint64 diff_us = g_date_time_difference(next, now2);
    g_date_time_unref(now2);
    g_date_time_unref(next);

    if (diff_us <= 0) {
        on_run_job(index);
        return;
    }

    constexpr gint64 MAX_DELAY_MS = 24LL * 3600 * 1000;
    auto delay_ms = static_cast<unsigned int>(std::min(diff_us / 1000, MAX_DELAY_MS));

    if (index >= m_job_state.size()) m_job_state.resize(index + 1);

    m_job_state[index].sched_timer.disconnect();
    std::string sched_uuid = job.id;
    m_job_state[index].sched_timer = Glib::signal_timeout().connect(
        [this, index, sched_uuid]() -> bool {
            if (index >= m_jobs.size() || m_jobs[index].id != sched_uuid)
                return false;
            on_run_job(index);
            return false;
        }, delay_ms);
}

void MtSyncDaemon::on_run_job(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];

    // Skip if a previous instance is still running or we're still waiting
    // for rclone to acknowledge a start request (prevents duplicate increments
    // to m_running_job_count from rapid scheduler / IPC calls)
    bool already_running = job.type == rclone::JobType::Mount
        ? (job.active || (index < m_job_state.size() && m_job_state[index].submitting))
        : ((index < m_job_state.size() && m_job_state[index].job_id >= 0)
           || (index < m_job_state.size() && m_job_state[index].submitting));
    if (already_running) {
        append_log(std::format("SKIPPED   {} [{}] previous instance still running",
            job.id, type_str(job.type)));
        return;
    }

    if (index >= m_job_state.size()) m_job_state.resize(index + 1);
    m_job_state[index].submitting = true;
    // Capture the job's stable ID so async callbacks can detect if the job was
    // deleted and a different job shifted into this index slot.
    std::string job_uuid = job.id;

    auto start_time = Glib::DateTime::create_now_local().format("%Y-%m-%d %H:%M:%S");
    if (index < m_jobs.size()) {
        m_jobs[index].last_start = start_time;
        m_jobs[index].running = true;
    }
    save_jobs();

    append_log(std::format("STARTED   {} [{}] {} -> {}",
        job.id, type_str(job.type), job.source, job.destination));

    auto settings = load_settings();
    if (settings.notify_on_start)
        send_notification("Job Started", job.source + " → " + job.destination);

    m_tray->set_attention(false);

    if (job.type == rclone::JobType::Mount) {
        // Mount jobs are persistent state, not active transfers — don't count them
        m_jobs[index].active = true;
        json response_payload = {{"index", index}};
        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload));
        m_manager.rc().mount_async(job.source, job.destination, job.vfs_cache_mode,
            [this, index, job_uuid](auto result) {
                if (index < m_job_state.size()) m_job_state[index].submitting = false;
                if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                json response_payload = {{"index", index}};
                if (result.has_value()) {
                    response_payload["success"] = true;
                    m_jobs[index].active = true;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    append_log(std::format("COMPLETED {} [MOUNT] SUCCESS", m_jobs[index].id));
                } else {
                    response_payload["success"] = false;
                    m_jobs[index].active = false;
                    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
                    append_log(std::format("FAILED    {} [MOUNT] {}",
                        m_jobs[index].id, result.error()));
                    g_warning("Mount %zu failed: %s", index, result.error().c_str());
                }
            });
        return;
    }

    // Sync/Copy/Move jobs are active transfers — track them for tray animation
    m_running_job_count++;
    update_tray_animation();

    json response_payload = {{"index", index}};
    m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobStarted, response_payload));

    nlohmann::json opts;
    if (job.dry_run) opts["_config"] = {{"DryRun", true}};
    if (!job.bandwidth.empty()) opts["_config"]["BwLimit"] = job.bandwidth;
    if (job.ignore_checksum) opts["_config"]["IgnoreChecksum"] = true;
    {
        int pt = job.parallel_transfers > 0
                 ? job.parallel_transfers
                 : settings.parallel_transfers;
        if (pt > 0) opts["_config"]["Transfers"] = pt;
    }
    if (job.type == rclone::JobType::Sync) {
        opts["createEmptySrcDirs"] = true;
        // bisync refuses to run without an established baseline ("path1 and path2
        // are out of sync, run --resync to recover"). Rather than guessing whether
        // one exists, let a normal run fail first and reactively retry once with
        // resync — see on_job_completed. Forcing resync proactively on every run
        // (the previous approach) re-establishes the baseline from scratch each
        // time, which can undo since-propagated deletes and misfire the bisync
        // "too many deletes" safety check.
        if (index < m_job_state.size() && m_job_state[index].bisync_force_resync) {
            opts["resync"] = true;
            m_job_state[index].bisync_force_resync = false;
        }
        // rclone's sync/bisync RC handler doesn't apply the CLI --max-delete
        // default (50%) when the param is omitted — it behaves as 0%, so any
        // deletion at all trips the safety abort. maxDelete itself is also
        // ignored over RC regardless of value, so the only way to allow
        // deletions through is the job's explicit opt-in "force" flag.
        if (job.bisync && job.bisync_force_deletes) opts["force"] = true;
    }
    if (!job.includes.empty()) {
        json filter = json::array();
        for (auto& inc : job.includes) {
            filter.push_back({{"IncludeRule", {{"Pattern", inc}}}});
        }
        opts["_config"]["Filter"] = filter;
    }

    auto to_pascal = [](const std::string& s) {
        std::string result;
        bool cap_next = true;
        for (char c : s) {
            if (c == '-') { cap_next = true; continue; }
            result += cap_next ? (char)std::toupper((unsigned char)c) : c;
            cap_next = false;
        }
        return result;
    };

    // Coerce a string flag value to the appropriate JSON type for rclone's _config.
    // - Pure integers → JSON number (rclone int/bool fields)
    // - Go duration strings (5m, 1h30s, 300ms) → nanoseconds as JSON int64
    //   (required for fields typed time.Duration, which rejects string input)
    // - Everything else (e.g. "1M" bandwidth) → kept as JSON string
    auto coerce_value = [](const std::string& val) -> json {
        if (val.empty()) return val;
        // Pure integer
        if (val.find_first_not_of("0123456789") == std::string::npos) {
            try { return std::stoll(val); } catch (...) {}
        }
        // Go duration: digits followed by a unit (ns/us/ms/s/m/h), possibly repeated
        int64_t total_ns = 0;
        size_t i = 0;
        while (i < val.size()) {
            if (!std::isdigit(static_cast<unsigned char>(val[i]))) goto not_duration;
            int64_t num = 0;
            while (i < val.size() && std::isdigit(static_cast<unsigned char>(val[i])))
                num = num * 10 + (val[i++] - '0');
            if (i + 1 < val.size() && val[i] == 'n' && val[i+1] == 's') { total_ns += num;                           i += 2; }
            else if (i + 1 < val.size() && val[i] == 'u' && val[i+1] == 's') { total_ns += num * 1'000LL;           i += 2; }
            else if (i + 1 < val.size() && val[i] == 'm' && val[i+1] == 's') { total_ns += num * 1'000'000LL;       i += 2; }
            else if (i < val.size() && val[i] == 's') { total_ns += num * 1'000'000'000LL;                          i += 1; }
            else if (i < val.size() && val[i] == 'm') { total_ns += num * 60LL  * 1'000'000'000LL;                  i += 1; }
            else if (i < val.size() && val[i] == 'h') { total_ns += num * 3600LL * 1'000'000'000LL;                 i += 1; }
            else goto not_duration;
        }
        if (i == val.size() && i > 0) return total_ns;
        not_duration:
        return val;
    };

    auto inject_flags = [&](const std::string& flags) {
        std::vector<std::string> tokens;
        std::istringstream iss(flags);
        std::string tok;
        while (iss >> tok) tokens.push_back(std::move(tok));
        for (size_t i = 0; i < tokens.size(); ) {
            const auto& t = tokens[i];
            if (t.size() < 3 || t[0] != '-' || t[1] != '-') { ++i; continue; }
            std::string key_raw = t.substr(2);
            if (auto eq = key_raw.find('='); eq != std::string::npos) {
                std::string key = to_pascal(key_raw.substr(0, eq));
                std::string val = key_raw.substr(eq + 1);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = coerce_value(val);
                ++i;
            } else if (i + 1 < tokens.size() && tokens[i + 1][0] != '-') {
                std::string key = to_pascal(key_raw);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = coerce_value(tokens[i + 1]);
                i += 2;
            } else {
                std::string key = to_pascal(key_raw);
                if (!opts["_config"].contains(key))
                    opts["_config"][key] = true;
                ++i;
            }
        }
    };

    // Inject per-job extra flags (higher priority than global flags)
    if (!job.extra_flags.empty()) inject_flags(job.extra_flags);

    // Inject global rclone flags (lower priority — do not overwrite per-job opts)
    if (!settings.global_rclone_flags.empty())
        inject_flags(settings.global_rclone_flags);

    auto done_cb = [this, index, job_uuid](auto result) {
        if (!m_running) return;
        if (index < m_job_state.size()) m_job_state[index].submitting = false;
        if (!result.has_value()) {
            // Job never received an RC job ID — on_job_completed() would return early
            // on its m_job_state guard, leaking job.running and m_running_job_count.
            // Clean up directly instead.
            g_warning("Job %zu failed to submit: %s", index, result.error().c_str());
            std::string log_path;
            if (index < m_jobs.size() && m_jobs[index].id == job_uuid) {
                log_path = write_error_log(m_jobs[index], result.error());
                m_jobs[index].running = false;
                m_jobs[index].last_status = "error";
                save_jobs();
                std::string log_suffix = log_path.empty() ? "" : " | log:" + log_path;
                append_log(std::format("FAILED    {} [{}] {}{}",
                    m_jobs[index].id, type_str(m_jobs[index].type),
                    result.error(), log_suffix));
            }
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            json response_payload = {{"index", index}, {"success", false}, {"log_path", log_path}};
            m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));
            return;
        }

        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) {
            // Job was deleted while the submission was in-flight.
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            return;
        }

        if (index >= m_job_state.size()) m_job_state.resize(index + 1);
        m_job_state[index].job_id = result.value();

        m_job_state[index].poll_timer = Glib::signal_timeout().connect(
            [this, index, job_uuid]() -> bool {
                if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return false;
                if (index >= m_job_state.size() || m_job_state[index].job_id < 0) return false;

                if (m_job_state[index].poll_in_flight) return true; // previous request still pending
                m_job_state[index].poll_in_flight = true;

                int64_t current_job_id = m_job_state[index].job_id;
                // Check job status; if still running, fetch stats in the same callback
                m_manager.rc().job_status(current_job_id, [this, index, job_uuid, current_job_id](auto status) {
                    // Guard against stale index (job deleted) or duplicate completion before
                    // clearing the in-flight flag — avoids clearing the flag for a different
                    // job that shifted into this index after a deletion.
                    if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                    if (index >= m_job_state.size() || m_job_state[index].job_id < 0) return;
                    m_job_state[index].poll_in_flight = false;
                    if (!status.has_value()) {
                        // A single missed poll doesn't mean the job died — rcd can be slow to
                        // answer while it's CPU/IO-busy (e.g. hashing files for a checksummed
                        // bisync). Only give up after several consecutive failures; otherwise
                        // the daemon resubmits a brand-new job while the original is still
                        // running, which for bisync collides on its workdir lock.
                        if (++m_job_state[index].poll_failures < 6) {
                            g_warning("Job %zu status poll failed (%d/6): %s",
                                index, m_job_state[index].poll_failures, status.error().c_str());
                            return;
                        }
                        on_job_completed(index, false, "rclone rcd unreachable");
                        return;
                    }
                    m_job_state[index].poll_failures = 0;
                    if (status->finished) {
                        on_job_completed(index, status->success,
                            status->success ? std::string{} : status->error,
                            status->success ? std::string{} : status->output_log);
                        return;
                    }
                    // Job still running — fetch stats for progress display
                    m_manager.rc().get_stats(current_job_id, [this, index, job_uuid](auto result) {
                        if (!result.has_value()) return;
                        if (index >= m_jobs.size() || m_jobs[index].id != job_uuid) return;
                        auto& stats = result.value();
                        if (index < m_job_state.size()) m_job_state[index].last_stats = stats;
                        if (stats.bytes == 0 && stats.total_bytes == 0 && stats.transfers == 0) return;
                        json response_payload = {
                            {"index", index},
                            {"bytes", stats.bytes},
                            {"total_bytes", stats.total_bytes},
                            {"transfers", stats.transfers},
                            {"total_transfers", stats.total_transfers},
                            {"speed", stats.speed}
                        };
                        if (stats.eta) response_payload["eta"] = *stats.eta;
                        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobProgress, response_payload));
                    });
                });
                return true;
            }, 500);
    };

    switch (job.type) {
        case rclone::JobType::Sync:
            if (job.bisync)
                m_manager.rc().bisync_async(job.source, job.destination, opts, done_cb);
            else
                m_manager.rc().sync_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Copy:
            m_manager.rc().copy_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Move:
            m_manager.rc().move_async(job.source, job.destination, opts, done_cb);
            break;
        case rclone::JobType::Mount:
            break; // handled by early-return above
    }
}

void MtSyncDaemon::on_job_completed(size_t index, bool success, const std::string& error_msg,
                                     const std::string& output_log) {
    // Atomically claim ownership: if the job_id is already cleared, another
    // completion path beat us here (duplicate from overlapping poll cycles).
    if (index >= m_job_state.size() || m_job_state[index].job_id < 0) return;
    if (index >= m_jobs.size()) return;
    std::string job_uuid = m_jobs[index].id;
    // Clear the job_id FIRST so subsequent callbacks see it as done
    m_job_state[index].job_id = -1;
    m_job_state[index].poll_in_flight = false;
    m_job_state[index].poll_timer.disconnect();

    auto settings = load_settings();
    // bisync's top-level error is a generic "bisync aborted" shared by several
    // distinct causes (missing baseline, empty-directory abort, too-many-deletes
    // safety abort), so the specific reason has to come from the captured log
    // text instead. Only the "no prior listings" phrasing is safe to
    // auto-recover from — other aborts (too many deletes, empty directory) are
    // deliberate safety checks that resyncing could silently defeat (e.g. by
    // resurrecting files the user just deleted), so they're surfaced as-is.
    if (!success && index < m_jobs.size() && m_jobs[index].bisync
        && !m_job_state[index].bisync_resync_retried
        && output_log.find("cannot find prior Path1 or Path2 listings") != std::string::npos) {
        // bisync has no established baseline for this pair yet (fresh pair, or an
        // interrupted prior run). Retry once immediately with resync — outside the
        // normal retry/backoff path so it doesn't consume the job's configured
        // retry budget and isn't delayed by exponential backoff.
        m_job_state[index].bisync_resync_retried = true;
        m_job_state[index].bisync_force_resync = true;
        append_log(std::format("RETRYING  {} [{}] with --resync (no established bisync baseline)",
            m_jobs[index].id, type_str(m_jobs[index].type)));
        std::string retry_uuid = m_jobs[index].id;
        Glib::signal_idle().connect_once([this, index, retry_uuid]() {
            if (index >= m_jobs.size() || m_jobs[index].id != retry_uuid) return;
            on_run_job(index);
        });
        // Balance the increment from on_run_job; the retry re-increments when it starts.
        m_running_job_count--;
        if (m_running_job_count < 0) m_running_job_count = 0;
        update_tray_animation();
        return;
    }
    if (!success && index < m_jobs.size()) {
        int max_retries = (m_jobs[index].retries >= 0)
            ? m_jobs[index].retries
            : settings.retries;
        if (m_job_state[index].retry_count < max_retries) {
            m_job_state[index].retry_count++;
            int shift = std::min(m_job_state[index].retry_count - 1, 4);
            unsigned int delay_ms = std::min(2000u << shift, 60000u);
            append_log(std::format("RETRYING  {} [{}] attempt {}/{} in {}s",
                m_jobs[index].id, type_str(m_jobs[index].type),
                m_job_state[index].retry_count, max_retries, delay_ms / 1000));
            std::string retry_uuid = m_jobs[index].id;
            m_job_state[index].retry_timer.disconnect();
            m_job_state[index].retry_timer = Glib::signal_timeout().connect(
                [this, index, retry_uuid]() -> bool {
                    if (index >= m_jobs.size() || m_jobs[index].id != retry_uuid)
                        return false;
                    on_run_job(index);
                    return false;
                }, delay_ms);
            // Balance the increment from on_run_job; the retry re-increments when it starts.
            m_running_job_count--;
            if (m_running_job_count < 0) m_running_job_count = 0;
            update_tray_animation();
            return;
        }
        m_job_state[index].retry_count = 0;
    }

    // Use cached stats from the last poll cycle; avoids an extra HTTP round-trip.
    // Delta from true final stats is at most one poll interval (500 ms).
    auto now = Glib::DateTime::create_now_local().format_iso8601();
    if (index < m_jobs.size()) {
        auto& job = m_jobs[index];
        m_job_state[index].retry_count = 0;
        job.active = false;
        job.running = false;
        job.last_status = success ? "success" : "error";
        job.last_run = now;
        save_jobs();

        std::string log_path;
        if (!success && !error_msg.empty())
            log_path = write_error_log(job, error_msg);
        std::string log_suffix = log_path.empty() ? "" : " | log:" + log_path;

        json response_payload = {{"index", index}, {"success", success}};
        if (!log_path.empty()) response_payload["log_path"] = log_path;

        // Include stats text for GUI display
        {
            auto& s = m_job_state[index].last_stats;
            if (success) {
                response_payload["stats_text"] = std::format("{} files, {}, {}",
                    s.transfers, format_bytes(s.bytes), format_speed(s.speed));
            }
        }

        m_ipc_server->send_to_all(make_response(ipc::ResponseType::JobCompleted, response_payload));

        // Always write a log entry regardless of stats availability
        std::string state  = success ? "COMPLETED" : "FAILED   ";
        auto& s = m_job_state[index].last_stats;
        std::string detail = success
            ? std::format("SUCCESS -- {} files, {}, {}, ran for {}",
                s.transfers, format_bytes(s.bytes), format_speed(s.speed),
                format_duration(s.elapsed_time))
            : std::format("{}{}", error_msg.empty() ? "see log" : error_msg, log_suffix);
        append_log(std::format("{} {} [{}] {}", state, job.id, type_str(job.type), detail));

        if (success) {
            if (settings.notify_on_completion)
                send_notification("Sync Complete", job.source + " → " + job.destination);
        } else {
            if (settings.notify_on_errors)
                send_notification("Sync Failed", job.source + " → " + job.destination);
        }

        if (job.schedule_enabled) {
            schedule_job(index);
        }
    }

    m_running_job_count--;
    if (m_running_job_count < 0) m_running_job_count = 0;
    update_tray_animation();

    // Only update attention state when all jobs are truly done.
    // Calling set_attention() mid-run emits NewStatus which can cause
    // some desktop environments (GNOME AppIndicator extension) to reset
    // and re-cache the icon, breaking the spinner display.
    if (m_running_job_count == 0)
        m_tray->set_attention(success);
}

void MtSyncDaemon::update_tray_animation() {
    if (m_running_job_count > 0)
        m_tray->start_animation();
    else
        m_tray->stop_animation();
}

} // namespace mtsync
