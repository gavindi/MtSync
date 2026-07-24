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

#include "application.hpp"
#include "window.hpp"
#include "views/welcome_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <glib.h>
#include <glibmm.h>
#include <nlohmann/json.hpp>

extern "C" GResource* mtsync_get_resource();

namespace mtsync {

MtSyncApplication::MtSyncApplication()
    : Gtk::Application("com.mtsync.MtSync") {
    mtsync_get_resource(); // register embedded GLib resources
    adw::init();
    m_settings = load_settings();
}

Glib::RefPtr<MtSyncApplication> MtSyncApplication::create() {
    return Glib::make_refptr_for_instance(new MtSyncApplication());
}

void MtSyncApplication::ensure_daemon_running() {
    m_daemon_proxy = std::make_unique<DaemonProxy>();

    if (!m_daemon_proxy->connect()) {
        g_message("Daemon not running, starting it...");
        
        // Prefer our own binary so a dev build doesn't launch the installed one
        std::string exe_path = "/proc/self/exe";
        if (!Glib::file_test(exe_path, Glib::FileTest::EXISTS)) {
            exe_path = Glib::find_program_in_path("mtsync");
        }

        try {
            Glib::spawn_async(
                {},
                {exe_path, "--daemon"}
            );
            g_message("Started daemon process");
        } catch (const Glib::Error& e) {
            g_warning("Failed to spawn daemon: %s", e.what());
        }

        for (int i = 0; i < 10; ++i) {
            g_usleep(100000);
            if (m_daemon_proxy->connect()) {
                g_message("Connected to daemon");
                return;
            }
        }

        g_warning("Could not connect to daemon after spawning");
        m_daemon_proxy.reset();
    } else {
        g_message("Connected to existing daemon");
    }
}

void MtSyncApplication::on_activate() {
    bool first_create = !m_window;
    if (!m_window) {
        ensure_daemon_running();

        m_window = Gtk::make_managed<MtSyncWindow>(m_rclone_manager, m_daemon_proxy.get(),
                                                    m_settings);
        add_window(*m_window);

        if (m_daemon_proxy) {
            m_daemon_proxy->signal_message().connect(
                [this](const nlohmann::json& msg) {
                    if (msg.value("type", "") == "show_window" && m_window)
                        m_window->present();
                });
        }
    }
    if (m_force_show || !first_create || !m_settings.start_minimized)
        m_window->present();

    if (first_create && m_settings.show_welcome_on_startup) {
        auto* welcome = Gtk::make_managed<WelcomeDialog>(m_settings);
        welcome->set_transient_for(*m_window);
        welcome->present();
    }
}

} // namespace mtsync
