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

#include "rclone_rc.hpp"
#include <glibmm.h>
#include <format>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

namespace mtsync::rclone {

using json = nlohmann::json;

// Timeout (ms) for stop_daemon before escalating from SIGTERM to SIGKILL
static constexpr int STOP_TIMEOUT_MS = 5000;

RcloneRc::RcloneRc(std::string addr, int port)
    : m_base_url(std::format("http://{}:{}", addr, port)) {
    m_session = soup_session_new();
    // rcd can be slow to answer polling calls (job/status, core/stats) while it's
    // busy doing CPU/IO-heavy work for another job, e.g. checksum hashing during a
    // bisync — 5s was tight enough to spuriously trip during that.
    g_object_set(m_session, "timeout", 20u, nullptr);

    // Mirror RcloneCli's config path logic so rclone rcd also bypasses Flatpak's
    // $XDG_CONFIG_HOME redirect and reads the real user config.
    const char* env_cfg = g_getenv("RCLONE_CONFIG");
    m_config_path = (env_cfg && *env_cfg) ? env_cfg
                  : std::string(g_get_home_dir()) + "/.config/rclone/rclone.conf";
}

RcloneRc::~RcloneRc() {
    stop_daemon();
    if (m_session) {
        g_object_unref(m_session);
        m_session = nullptr;
    }
}

void RcloneRc::rc_post(const std::string& endpoint,
                        const json& body,
                        AsyncCallback<json> callback) {
    auto url = m_base_url + "/" + endpoint;
    auto body_str = body.dump();

    auto* msg = soup_message_new("POST", url.c_str());
    if (!msg) {
        callback(std::unexpected("Failed to create HTTP message for " + url));
        return;
    }

    auto* bytes = g_bytes_new(body_str.c_str(), body_str.size());
    soup_message_set_request_body_from_bytes(msg, "application/json", bytes);
    g_bytes_unref(bytes);

    struct Pack {
        std::shared_ptr<AsyncCallback<json>> cb;
        SoupMessage* msg;
    };

    // Retain a reference so we can read the HTTP status code in the callback.
    // The session holds its own ref during the async op, but we need ours to
    // survive past g_object_unref(msg) below.
    g_object_ref(msg);
    auto* pack = new Pack{std::make_shared<AsyncCallback<json>>(std::move(callback)), msg};

    soup_session_send_and_read_async(m_session, msg, G_PRIORITY_DEFAULT, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* pack = static_cast<Pack*>(user_data);
            GError* error = nullptr;
            auto* bytes = soup_session_send_and_read_finish(
                SOUP_SESSION(source), result, &error);

            if (error) {
                (*pack->cb)(std::unexpected(std::string("HTTP error: ") + error->message));
                g_error_free(error);
            } else {
                auto http_status = soup_message_get_status(pack->msg);
                gsize size = 0;
                auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
                try {
                    auto j = json::parse(std::string_view(data, size));
                    // rclone RC error responses use non-200 HTTP status and include
                    // an "error" string field in the body. Surface that as an error
                    // rather than passing the raw JSON through (which would produce
                    // confusing "No jobid in response" errors at the call sites).
                    if (http_status != 200
                            && j.is_object()
                            && j.contains("error")
                            && j["error"].is_string()) {
                        (*pack->cb)(std::unexpected(j["error"].get<std::string>()));
                    } else {
                        (*pack->cb)(std::move(j));
                    }
                } catch (const json::exception& e) {
                    (*pack->cb)(std::unexpected(std::string("JSON parse error: ") + e.what()));
                }
                g_bytes_unref(bytes);
            }
            g_object_unref(pack->msg);
            delete pack;
        },
        pack);

    g_object_unref(msg);
}

void RcloneRc::ensure_daemon(AsyncCallback<std::monostate> callback) {
    if (m_daemon_pid > 0) {
        // Daemon is alive (child watch will fire if it dies)
        callback(std::monostate{});
        return;
    }

    // m_daemon_pid == 0: probe first — a previous session's rclone rcd may still
    // be listening (e.g. app crashed, or stop_daemon() raced with restart).
    // Adopt it rather than spawning a new one that would fail to bind the port.
    // Sentinel prevents the callback from touching `this` after destruction.
    // stop_daemon() sets *m_ensure_cancelled = true before the session is released.
    m_ensure_cancelled = std::make_shared<bool>(false);
    rc_post("core/version", json::object(),
            [this, cancelled = m_ensure_cancelled, callback = std::move(callback)](auto result) mutable {
        if (*cancelled) return;
        if (result.has_value()) {
            // An rclone rcd is already running — adopt it by setting up a child
            // watch so we get notified when it dies.  We don't know the real PID,
            // so we can't use g_child_watch_add; instead we'll detect death via
            // HTTP ping failure on the next ensure_daemon call.  Mark with -1 to
            // distinguish from a PID we spawned ourselves.
            m_daemon_pid = -1;
            callback(std::monostate{});
            return;
        }
        // Nothing listening — spawn a fresh one
        spawn_daemon(std::move(callback));
    });
}

void RcloneRc::spawn_daemon(AsyncCallback<std::monostate> callback) {
    auto rclone_path = Glib::find_program_in_path("rclone");
    if (rclone_path.empty()) {
        callback(std::unexpected("rclone not found in PATH"));
        return;
    }
    gchar* argv[] = {
        const_cast<gchar*>(rclone_path.c_str()),
        const_cast<gchar*>("rcd"),
        const_cast<gchar*>("--config"),
        const_cast<gchar*>(m_config_path.c_str()),
        const_cast<gchar*>("--rc-no-auth"),
        const_cast<gchar*>("--rc-addr"),
        const_cast<gchar*>("localhost:5571"),
        nullptr
    };

    // Redirect rclone's stdout/stderr to /dev/null so it doesn't pollute the console
    gint dev_null = open("/dev/null", O_RDWR);

    GSpawnChildSetupFunc setup_func = dev_null >= 0 ? +[](gpointer data) {
        int fd = GPOINTER_TO_INT(data);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    } : nullptr;

    GError* error = nullptr;
    gboolean ok = g_spawn_async(nullptr, argv, nullptr,
        G_SPAWN_DO_NOT_REAP_CHILD,
        setup_func,
        dev_null >= 0 ? GINT_TO_POINTER(dev_null) : nullptr,
        &m_daemon_pid, &error);

    if (dev_null >= 0) close(dev_null);

    if (!ok) {
        m_daemon_pid = 0;
        std::string err = error ? error->message : "unknown error";
        if (error) g_error_free(error);
        callback(std::unexpected("Failed to spawn rclone rcd: " + err));
        return;
    }

    // Register a child watch so we get async notification when the daemon dies.
    // Source ID is stored so stop_daemon() can remove it before killing the process,
    // preventing a dangling-this callback after RcloneRc is destroyed.
    struct WatchPack {
        RcloneRc* self;
        GPid pid;
    };
    auto* pack = new WatchPack{this, m_daemon_pid};
    m_child_watch_id = g_child_watch_add(pack->pid, [](GPid pid, gint status, gpointer user_data) {
        auto* p = static_cast<WatchPack*>(user_data);
        g_spawn_close_pid(pid);
        p->self->m_child_watch_id = 0;
        p->self->m_daemon_pid = 0;
        delete p;
        g_warning("rclone rcd (pid %d) exited with status %d", (int)pid, status);
    }, pack);

    // Give rclone rcd time to start, then verify it's up.
    // The sentinel (m_verify_cancelled) lets stop_daemon() abort the in-flight
    // HTTP call without a dangling-this dereference.
    struct VerifyPack {
        RcloneRc* self;
        std::shared_ptr<AsyncCallback<std::monostate>> cb;
        std::shared_ptr<bool> cancelled;
    };
    m_verify_cancelled = std::make_shared<bool>(false);
    auto* verify_pack = new VerifyPack{
        this,
        std::make_shared<AsyncCallback<std::monostate>>(std::move(callback)),
        m_verify_cancelled
    };
    // g_timeout_add_full: GDestroyNotify deletes verify_pack whether the timer
    // fires naturally or is cancelled early via g_source_remove().
    m_verify_timer_id = g_timeout_add_full(G_PRIORITY_DEFAULT, 1500,
        [](gpointer data) -> gboolean {
            auto* p = static_cast<VerifyPack*>(data);
            if (!*p->cancelled) {
                p->self->rc_post("core/version", json::object(),
                    [self = p->self, cb = p->cb, cancelled = p->cancelled](auto result) {
                        if (*cancelled) return;
                        if (result.has_value()) {
                            (*cb)(std::monostate{});
                        } else {
                            g_warning("rclone rcd failed startup verification, terminating");
                            self->stop_daemon();
                            (*cb)(std::unexpected("rclone rcd failed to start"));
                        }
                    });
            }
            return G_SOURCE_REMOVE;
        },
        verify_pack,
        [](gpointer data) { delete static_cast<VerifyPack*>(data); }
    );
}

void RcloneRc::stop_daemon() {
    // Cancel the startup verification timer and mark its HTTP callback as stale
    // before touching m_daemon_pid, so the callback can't race against destruction.
    if (m_verify_timer_id) {
        g_source_remove(m_verify_timer_id);
        m_verify_timer_id = 0;
    }
    if (m_verify_cancelled) {
        *m_verify_cancelled = true;
        m_verify_cancelled.reset();
    }
    if (m_ensure_cancelled) {
        *m_ensure_cancelled = true;
        m_ensure_cancelled.reset();
    }

    // Remove the child watch before killing so the callback doesn't fire with
    // a dangling this pointer after RcloneRc is destroyed.
    if (m_child_watch_id) {
        g_source_remove(m_child_watch_id);
        m_child_watch_id = 0;
    }

    if (m_daemon_pid > 0) {
        GPid pid = m_daemon_pid;
        m_daemon_pid = 0;

        kill(pid, SIGTERM);

        // Poll up to 1 s at 5 ms intervals; exits promptly when process dies quickly.
        for (int i = 0; i < 200; ++i) {
            if (waitpid(pid, nullptr, WNOHANG) != 0) {
                g_spawn_close_pid(pid);
                return;
            }
            g_usleep(5000);
        }

        g_warning("rclone rcd (pid %d) did not exit after 1s, sending SIGKILL", (int)pid);
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        g_spawn_close_pid(pid);
    } else if (m_daemon_pid == -1) {
        rc_post("core/quit", json::object(), [](auto) {});
        m_daemon_pid = 0;
    }
}

void RcloneRc::list_mounts(AsyncCallback<std::vector<std::string>> callback) {
    rc_post("mount/listmounts", {}, [callback = std::move(callback)](auto result) {
        if (result.has_value()) {
            std::vector<std::string> mount_points;
            try {
                auto& j = result.value();
                if (j.contains("mountPoints")) {
                    for (auto& mp : j["mountPoints"]) {
                        mount_points.push_back(mp.template get<std::string>());
                    }
                }
            } catch (const std::exception& e) {
                callback(std::unexpected(std::string("Failed to parse mount list: ") + e.what()));
                return;
            }
            callback(std::move(mount_points));
        } else {
            callback(std::unexpected(result.error()));
        }
    });
}

void RcloneRc::mount_async(const std::string& src, const std::string& mountpoint,
                            AsyncCallback<std::monostate> callback) {
    mount_async(src, mountpoint, "", std::move(callback));
}

void RcloneRc::mount_async(const std::string& src, const std::string& mountpoint,
                            const std::string& vfs_cache_mode,
                            AsyncCallback<std::monostate> callback) {
    json body = {{"fs", src}, {"mountPoint", mountpoint}};
    if (!vfs_cache_mode.empty()) {
        // rclone RC mount/mount accepts vfsOpt.CacheMode as integer:
        // 0=off, 1=minimal, 2=writes, 3=full
        int cache_mode = 0;
        if (vfs_cache_mode == "minimal") cache_mode = 1;
        else if (vfs_cache_mode == "writes") cache_mode = 2;
        else if (vfs_cache_mode == "full") cache_mode = 3;
        body["vfsOpt"]["CacheMode"] = cache_mode;
    }
    rc_post("mount/mount", body,
        [callback = std::move(callback)](auto result) {
            if (result.has_value()) callback(std::monostate{});
            else                    callback(std::unexpected(result.error()));
        });
}

void RcloneRc::unmount_async(const std::string& mountpoint,
                              AsyncCallback<std::monostate> callback) {
    rc_post("mount/unmount", {{"mountPoint", mountpoint}},
        [callback = std::move(callback)](auto result) {
            if (result.has_value()) callback(std::monostate{});
            else                    callback(std::unexpected(result.error()));
        });
}

void RcloneRc::sync_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/sync", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::copy_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/copy", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::move_async(const std::string& src_fs, const std::string& dst_fs,
                            const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"srcFs", src_fs},
        {"dstFs", dst_fs},
        {"_async", true}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/move", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::bisync_async(const std::string& path1, const std::string& path2,
                              const json& opts,
                            AsyncCallback<int64_t> callback) {
    json body = {
        {"path1", path1},
        {"path2", path2},
        {"_async", true},
        // rclone's sync/bisync RC handler doesn't apply the --check-sync CLI
        // default when the param is omitted from the RC body — it decodes to
        // "" and rejects it with "unknown check-sync mode for bisync".
        {"checkSync", "true"}
    };
    if (!opts.empty()) body.update(opts);

    rc_post("sync/bisync", body, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        if (j.contains("jobid")) {
            callback(j["jobid"].template get<int64_t>());
        } else {
            callback(std::unexpected("No jobid in response"));
        }
    });
}

void RcloneRc::get_stats(AsyncCallback<SyncStats> callback) {
    rc_post("core/stats", json::object(), [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        SyncStats stats;
        stats.bytes = j.value("bytes", int64_t{0});
        stats.total_bytes = j.value("totalBytes", int64_t{0});
        stats.transfers = j.value("transfers", 0);
        stats.total_transfers = j.value("totalTransfers", 0);
        stats.errors = j.value("errors", 0);
        stats.speed = j.value("speed", 0.0);
        stats.elapsed_time = j.value("elapsedTime", 0.0);
        if (j.contains("eta") && !j["eta"].is_null())
            stats.eta = j["eta"].template get<double>();
        stats.fatal_error = j.value("fatalError", false);
        callback(std::move(stats));
    });
}

void RcloneRc::get_stats(int64_t jobid, AsyncCallback<SyncStats> callback) {
    rc_post("core/stats", {{"group", "job/" + std::to_string(jobid)}},
        [callback = std::move(callback)](auto result) {
            if (!result.has_value()) {
                callback(std::unexpected(result.error()));
                return;
            }
            auto& j = result.value();
            SyncStats stats;
            stats.bytes = j.value("bytes", int64_t{0});
            stats.total_bytes = j.value("totalBytes", int64_t{0});
            stats.transfers = j.value("transfers", 0);
            stats.total_transfers = j.value("totalTransfers", 0);
            stats.errors = j.value("errors", 0);
            stats.speed = j.value("speed", 0.0);
            stats.elapsed_time = j.value("elapsedTime", 0.0);
            if (j.contains("eta") && !j["eta"].is_null())
                stats.eta = j["eta"].template get<double>();
            stats.fatal_error = j.value("fatalError", false);
            callback(std::move(stats));
        });
}

void RcloneRc::get_about(const std::string& remote, AsyncCallback<AboutInfo> callback) {
    rc_post("operations/about", {{"fs", remote}}, [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        AboutInfo info;
        if (j.contains("total") && !j["total"].is_null())
            info.total = j["total"].template get<int64_t>();
        if (j.contains("used") && !j["used"].is_null())
            info.used = j["used"].template get<int64_t>();
        if (j.contains("free") && !j["free"].is_null())
            info.free = j["free"].template get<int64_t>();
        callback(std::move(info));
    });
}

void RcloneRc::job_status(int64_t jobid, AsyncCallback<JobStatus> callback) {
    rc_post("job/status", {{"jobid", jobid}},
        [callback = std::move(callback)](auto result) {
        if (!result.has_value()) {
            callback(std::unexpected(result.error()));
            return;
        }
        auto& j = result.value();
        // rclone wraps RC-level errors as {"error":"...","status":N,"path":"..."}
        // Return as unexpected so callers can distinguish "job not found" from "still running"
        if (j.contains("status") && j.contains("error")) {
            callback(std::unexpected(j["error"].template get<std::string>()));
            return;
        }
        JobStatus status;
        status.id = j.value("id", int64_t{0});
        status.finished = j.value("finished", false);
        status.success = j.value("success", false);
        status.error = j.value("error", std::string{});
        if (j.contains("output") && j["output"].is_object())
            status.output_log = j["output"].value("output", std::string{});
        callback(std::move(status));
    });
}

void RcloneRc::job_stop(int64_t jobid, AsyncCallback<std::monostate> callback) {
    rc_post("job/stop", {{"jobid", jobid}},
        [callback = std::move(callback)](auto result) {
        if (result.has_value())
            callback(std::monostate{});
        else
            callback(std::unexpected(result.error()));
    });
}

} // namespace mtsync::rclone
