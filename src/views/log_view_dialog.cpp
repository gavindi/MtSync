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

#include "views/log_view_dialog.hpp"
#include <glibmm/i18n.h>
#include <fstream>
#include <sstream>

namespace mtsync {

namespace {

// Guard against pathological files — error logs are tiny, but be safe
constexpr size_t MAX_FILE_BYTES = 1024 * 1024;

} // namespace

LogViewDialog::LogViewDialog(const Glib::RefPtr<LogEntry>& entry) {
    set_title(_("Log Entry"));
    set_default_size(720, 480);
    set_modal(true);
    set_destroy_with_parent(true);
    setup_ui(entry);
}

void LogViewDialog::setup_ui(const Glib::RefPtr<LogEntry>& entry) {
    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_child(*root);

    // Entry summary (Time / State / Job ID / Type)
    auto* info_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    info_box->set_margin_top(10);
    info_box->set_margin_bottom(6);
    info_box->set_margin_start(12);
    info_box->set_margin_end(12);

    auto add_row = [info_box](const Glib::ustring& name, const Glib::ustring& value) {
        auto* lbl = Gtk::make_managed<Gtk::Label>(name + ":  " + value);
        lbl->set_xalign(0.0f);
        lbl->add_css_class("caption");
        lbl->add_css_class("dim-label");
        info_box->append(*lbl);
    };
    add_row(_("Time"),   entry->property_time.get_value());
    add_row(_("State"),  entry->property_state.get_value());
    add_row(_("Job ID"), entry->property_job_id.get_value());
    add_row(_("Type"),   entry->property_job_type.get_value());

    root->append(*info_box);

    m_log_path = std::string(entry->property_log_path.get_value());
    if (!m_log_path.empty()) {
        auto* path_lbl = Gtk::make_managed<Gtk::Label>(m_log_path);
        path_lbl->set_xalign(0.0f);
        path_lbl->add_css_class("caption");
        path_lbl->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        path_lbl->set_tooltip_text(m_log_path);
        path_lbl->set_margin_start(12);
        path_lbl->set_margin_end(12);
        path_lbl->set_margin_bottom(6);
        root->append(*path_lbl);
    }

    // Body: file contents (or the entry's own text when no file is referenced)
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);

    m_text_view = Gtk::make_managed<Gtk::TextView>();
    m_text_view->set_editable(false);
    m_text_view->set_cursor_visible(false);
    m_text_view->set_wrap_mode(Gtk::WrapMode::NONE);
    m_text_view->set_monospace(true);
    m_text_view->set_left_margin(12);
    m_text_view->set_right_margin(12);
    m_text_view->set_top_margin(8);
    m_text_view->set_bottom_margin(8);
    scroll->set_child(*m_text_view);
    root->append(*scroll);

    std::string contents;
    if (m_log_path.empty()) {
        contents = std::string(entry->property_contents.get_value());
    } else {
        std::ifstream f(m_log_path, std::ios::binary);
        if (!f) {
            contents = std::string(_("Could not read log file: ")) + m_log_path;
        } else {
            std::ostringstream ss;
            ss << f.rdbuf();
            contents = ss.str();
            if (contents.size() > MAX_FILE_BYTES)
                contents.resize(MAX_FILE_BYTES);
        }
    }
    m_text_view->get_buffer()->set_text(contents);

    // Footer: open in default app (only when a file is referenced) + Close
    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    footer->set_margin_top(6);
    footer->set_margin_bottom(10);
    footer->set_margin_start(12);
    footer->set_margin_end(12);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    footer->append(*spacer);

    if (!m_log_path.empty()) {
        auto* open_btn = Gtk::make_managed<Gtk::Button>(_("Open in default app"));
        open_btn->set_tooltip_text(
            _("Open the activity log file for this job in the default application"));
        open_btn->signal_clicked().connect([this]() {
            if (!m_log_path.empty())
                Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(m_log_path));
        });
        footer->append(*open_btn);
    }

    auto* close_btn = Gtk::make_managed<Gtk::Button>(_("Close"));
    close_btn->add_css_class("suggested-action");
    close_btn->signal_clicked().connect([this]() { close(); });
    footer->append(*close_btn);

    root->append(*footer);
}

} // namespace mtsync