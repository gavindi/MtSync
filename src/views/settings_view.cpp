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

#include "views/settings_view.hpp"
#include "sandbox.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <glibmm/i18n.h>

namespace mtsync {

namespace fs = std::filesystem;

SettingsView::SettingsView(Settings& settings)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_settings(settings) {
    setup_ui();
}

SettingsView::~SettingsView() {
    g_signal_handler_disconnect(m_autostart_row->gobj(),        m_sig_autostart);
    g_signal_handler_disconnect(m_minimized_row->gobj(),        m_sig_minimized);
    g_signal_handler_disconnect(m_tray_row->gobj(),             m_sig_tray);
    g_signal_handler_disconnect(m_bandwidth_row->gobj(),        m_sig_bandwidth);
    g_signal_handler_disconnect(m_checksums_row->gobj(),        m_sig_checksums);
    g_signal_handler_disconnect(m_transfers_row->gobj(),        m_sig_transfers);
    g_signal_handler_disconnect(m_retries_row->gobj(),          m_sig_retries);
    g_signal_handler_disconnect(m_rclone_path_row->gobj(),      m_sig_rclone_path);
    g_signal_handler_disconnect(m_global_flags_row->gobj(),     m_sig_global_flags);
    g_signal_handler_disconnect(m_notify_start_row->gobj(),     m_sig_notify_start);
    g_signal_handler_disconnect(m_notify_completion_row->gobj(),m_sig_notify_completion);
    g_signal_handler_disconnect(m_notify_errors_row->gobj(),    m_sig_notify_errors);
}

void SettingsView::save() {
    save_settings(m_settings);
}

void SettingsView::write_autostart(bool enable) {
    auto autostart_dir = fs::path(sandbox::real_config_dir()) / "autostart";
    auto desktop_file  = autostart_dir / "mtsync-daemon.desktop";

    if (enable) {
        std::error_code ec;
        fs::create_directories(autostart_dir, ec);
        std::ofstream f(desktop_file);
        f << "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=Mt. Sync Daemon\n"
             "Exec=" << sandbox::autostart_exec() << "\n"
             "X-GNOME-Autostart-enabled=true\n";
    } else {
        std::error_code ec;
        fs::remove(desktop_file, ec);
    }
}

void SettingsView::setup_ui() {
    set_vexpand(true);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    append(*scroll);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    scroll->set_child(*clamp);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // ── Notifications ─────────────────────────────────────────────────────────
    auto* notif_group = adw::preferences_group();
    adw::preferences_group_set_title(notif_group, _("Notifications"));
    vbox->append(*notif_group);

    m_notify_start_row = adw::switch_row();
    adw::preferences_row_set_title(m_notify_start_row, _("On Job Start"));
    m_notify_start_row->set_tooltip_text(_("Show a desktop notification whenever a sync, copy, move, or mount job begins running"));
    adw::switch_row_set_active(m_notify_start_row, m_settings.notify_on_start);
    adw::preferences_group_add(notif_group, m_notify_start_row);

    m_notify_completion_row = adw::switch_row();
    adw::preferences_row_set_title(m_notify_completion_row, _("On Completion"));
    m_notify_completion_row->set_tooltip_text(_("Show a desktop notification when a job finishes successfully"));
    adw::switch_row_set_active(m_notify_completion_row, m_settings.notify_on_completion);
    adw::preferences_group_add(notif_group, m_notify_completion_row);

    m_notify_errors_row = adw::switch_row();
    adw::preferences_row_set_title(m_notify_errors_row, _("On Completion with Errors/Warnings"));
    m_notify_errors_row->set_tooltip_text(_("Show a desktop notification when a job finishes but encountered errors or warnings during the transfer"));
    adw::switch_row_set_active(m_notify_errors_row, m_settings.notify_on_errors);
    adw::preferences_group_add(notif_group, m_notify_errors_row);

    // ── Start Up & Shut Down ──────────────────────────────────────────────────
    auto* general_group = adw::preferences_group();
    adw::preferences_group_set_title(general_group, _("Start Up & Shut Down"));
    vbox->append(*general_group);

    m_autostart_row = adw::switch_row();
    adw::preferences_row_set_title(m_autostart_row, _("Start daemon on login"));
    m_autostart_row->set_tooltip_text(_("Automatically start the Mt. Sync background daemon when you log in, so scheduled jobs run even when the app window is closed"));
    adw::switch_row_set_active(m_autostart_row, m_settings.start_daemon_on_login);
    adw::preferences_group_add(general_group, m_autostart_row);

    m_minimized_row = adw::switch_row();
    adw::preferences_row_set_title(m_minimized_row, _("Start minimized to tray"));
    m_minimized_row->set_tooltip_text(_("Launch Mt. Sync with the window hidden; the app appears only as a system tray icon until you open it"));
    adw::switch_row_set_active(m_minimized_row, m_settings.start_minimized);
    adw::preferences_group_add(general_group, m_minimized_row);

    m_tray_row = adw::switch_row();
    adw::preferences_row_set_title(m_tray_row, _("Shutdown daemon when closing application"));
    m_tray_row->set_tooltip_text(_("Stop the background daemon and all running jobs when the app window is closed; when off, the daemon keeps running and scheduled jobs continue in the background"));
    adw::switch_row_set_active(m_tray_row, m_settings.shutdown_daemon_on_close);
    adw::preferences_group_add(general_group, m_tray_row);

    // ── Transfers ─────────────────────────────────────────────────────────────
    auto* transfers_group = adw::preferences_group();
    adw::preferences_group_set_title(transfers_group, _("Transfers"));
    vbox->append(*transfers_group);

    m_bandwidth_row = adw::entry_row();
    adw::preferences_row_set_title(m_bandwidth_row, _("Default bandwidth limit (e.g. 10M)"));
    if (!m_settings.default_bandwidth.empty())
        adw::entry_row_set_text(m_bandwidth_row, m_settings.default_bandwidth.c_str());
    adw::preferences_group_add(transfers_group, m_bandwidth_row);

    m_checksums_row = adw::switch_row();
    adw::preferences_row_set_title(m_checksums_row, _("Verify checksums"));
    m_checksums_row->set_tooltip_text(_("Verify file integrity using checksums during transfers — slower but guarantees bit-perfect copies"));
    adw::switch_row_set_active(m_checksums_row, m_settings.verify_checksums);
    adw::preferences_group_add(transfers_group, m_checksums_row);

    m_transfers_row = adw::entry_row();
    adw::preferences_row_set_title(m_transfers_row, _("Parallel transfers"));
    adw::entry_row_set_text(m_transfers_row,
        std::format("{}", m_settings.parallel_transfers).c_str());
    adw::preferences_group_add(transfers_group, m_transfers_row);

    m_retries_row = adw::entry_row();
    adw::preferences_row_set_title(m_retries_row, _("Retries on failure"));
    adw::entry_row_set_text(m_retries_row,
        std::format("{}", m_settings.retries).c_str());
    adw::preferences_group_add(transfers_group, m_retries_row);

    // ── rclone ────────────────────────────────────────────────────────────────
    auto* rclone_group = adw::preferences_group();
    adw::preferences_group_set_title(rclone_group, "rclone");
    vbox->append(*rclone_group);

    m_rclone_path_row = adw::entry_row();
    adw::preferences_row_set_title(m_rclone_path_row, _("rclone binary path"));
    if (!m_settings.rclone_path.empty())
        adw::entry_row_set_text(m_rclone_path_row, m_settings.rclone_path.c_str());
    adw::preferences_group_add(rclone_group, m_rclone_path_row);

    auto* rclone_hint = Gtk::make_managed<Gtk::Label>(
        _("Leave empty to use PATH lookup. Restart required."));
    rclone_hint->add_css_class("dim-label");
    rclone_hint->set_xalign(0.0f);
    rclone_hint->set_margin_start(12);
    rclone_hint->set_margin_top(4);
    vbox->append(*rclone_hint);

    m_global_flags_row = adw::entry_row();
    adw::preferences_row_set_title(m_global_flags_row, _("Global rclone flags"));
    if (!m_settings.global_rclone_flags.empty())
        adw::entry_row_set_text(m_global_flags_row, m_settings.global_rclone_flags.c_str());
    adw::preferences_group_add(rclone_group, m_global_flags_row);

    auto* flags_hint = Gtk::make_managed<Gtk::Label>(
        _("Added to every job at execution time (e.g. --log-level DEBUG --checkers 8)."));
    flags_hint->add_css_class("dim-label");
    flags_hint->set_xalign(0.0f);
    flags_hint->set_margin_start(12);
    flags_hint->set_margin_top(4);
    vbox->append(*flags_hint);

    // ── Signal wiring (after all member pointers are stored) ──────────────────

    m_sig_autostart = g_signal_connect(m_autostart_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.start_daemon_on_login =
                adw::switch_row_get_active(self->m_autostart_row);
            write_autostart(self->m_settings.start_daemon_on_login);
            self->save();
        }), this);

    m_sig_minimized = g_signal_connect(m_minimized_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.start_minimized =
                adw::switch_row_get_active(self->m_minimized_row);
            self->save();
        }), this);

    m_sig_tray = g_signal_connect(m_tray_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.shutdown_daemon_on_close =
                adw::switch_row_get_active(self->m_tray_row);
            self->save();
        }), this);

    m_sig_bandwidth = g_signal_connect(m_bandwidth_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.default_bandwidth =
                adw::entry_row_get_text(self->m_bandwidth_row);
            self->save();
        }), this);

    m_sig_checksums = g_signal_connect(m_checksums_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.verify_checksums =
                adw::switch_row_get_active(self->m_checksums_row);
            self->save();
        }), this);

    m_sig_transfers = g_signal_connect(m_transfers_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            try {
                int v = std::stoi(adw::entry_row_get_text(self->m_transfers_row));
                if (v > 0) self->m_settings.parallel_transfers = v;
            } catch (...) {}
            self->save();
        }), this);

    m_sig_retries = g_signal_connect(m_retries_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            try {
                int v = std::stoi(adw::entry_row_get_text(self->m_retries_row));
                if (v >= 0) self->m_settings.retries = v;
            } catch (...) {}
            self->save();
        }), this);

    m_sig_rclone_path = g_signal_connect(m_rclone_path_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.rclone_path =
                adw::entry_row_get_text(self->m_rclone_path_row);
            self->save();
        }), this);

    m_sig_global_flags = g_signal_connect(m_global_flags_row->gobj(), "changed",
        G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.global_rclone_flags =
                adw::entry_row_get_text(self->m_global_flags_row);
            self->save();
        }), this);

    m_sig_notify_start = g_signal_connect(m_notify_start_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.notify_on_start =
                adw::switch_row_get_active(self->m_notify_start_row);
            self->save();
        }), this);

    m_sig_notify_completion = g_signal_connect(m_notify_completion_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.notify_on_completion =
                adw::switch_row_get_active(self->m_notify_completion_row);
            self->save();
        }), this);

    m_sig_notify_errors = g_signal_connect(m_notify_errors_row->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<SettingsView*>(data);
            self->m_settings.notify_on_errors =
                adw::switch_row_get_active(self->m_notify_errors_row);
            self->save();
        }), this);
}

} // namespace mtsync
