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

#include "settings.hpp"
#include <gtkmm.h>

namespace mtsync {

class SettingsView : public Gtk::Box {
public:
    explicit SettingsView(Settings& settings);
    ~SettingsView() override;

private:
    Settings&    m_settings;

    // General
    Gtk::Widget* m_autostart_row   = nullptr;
    Gtk::Widget* m_minimized_row   = nullptr;
    Gtk::Widget* m_tray_row        = nullptr;
    // Transfers
    Gtk::Widget* m_bandwidth_row   = nullptr;
    Gtk::Widget* m_checksums_row   = nullptr;
    Gtk::Widget* m_transfers_row   = nullptr;
    Gtk::Widget* m_retries_row     = nullptr;
    Gtk::Widget* m_watch_debounce_row = nullptr;
    Gtk::Widget* m_watch_max_wait_row = nullptr;
    // rclone
    Gtk::Widget* m_rclone_path_row       = nullptr;
    Gtk::Widget* m_global_flags_row      = nullptr;
    // Notifications
    Gtk::Widget* m_notify_start_row      = nullptr;
    Gtk::Widget* m_notify_completion_row = nullptr;
    Gtk::Widget* m_notify_errors_row     = nullptr;

    // Signal handler IDs for clean disconnect in destructor
    gulong m_sig_autostart       = 0;
    gulong m_sig_minimized        = 0;
    gulong m_sig_tray             = 0;
    gulong m_sig_bandwidth        = 0;
    gulong m_sig_checksums        = 0;
    gulong m_sig_transfers        = 0;
    gulong m_sig_retries          = 0;
    gulong m_sig_watch_debounce   = 0;
    gulong m_sig_watch_max_wait   = 0;
    gulong m_sig_rclone_path      = 0;
    gulong m_sig_global_flags     = 0;
    gulong m_sig_notify_start     = 0;
    gulong m_sig_notify_completion= 0;
    gulong m_sig_notify_errors    = 0;

    void setup_ui();
    void save();
    static void write_autostart(bool enable);
};

} // namespace mtsync
