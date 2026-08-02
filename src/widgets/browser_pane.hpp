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
#include "widgets/adw_wrapper.hpp"
#include "widgets/file_row.hpp"
#include <gtkmm.h>
#include <string>
#include <vector>

namespace mtsync {

class BrowserPane : public Gtk::Box {
public:
    explicit BrowserPane(rclone::RcloneManager& manager);

    // Called by parent to populate the remote dropdown on first map
    void load_remotes();

    // Returns the rclone path for the current location (e.g. "remote:dir" or "/local/path")
    std::string get_current_rclone_path() const;

    // Returns selected FileObjects from the column view
    std::vector<Glib::RefPtr<FileObject>> get_selected_files() const;

    // Re-navigate to the current path
    void refresh();

    // Highlight this pane as the active one
    void set_active(bool active);

    // Physically swap the remote, path, and navigation history with another pane
    void swap_location_with(BrowserPane& other);

    // Emitted when the user interacts with this pane (for active-pane tracking)
    sigc::signal<void()> signal_focused;

private:
    rclone::RcloneManager& m_manager;

    // Remote selector
    Glib::RefPtr<Gtk::StringList> m_remote_string_list;
    Gtk::DropDown*                m_remote_dropdown = nullptr;
    sigc::connection              m_dropdown_conn;
    gulong                        m_dark_signal_id  = 0;

    // Navigation buttons & breadcrumbs
    Gtk::ScrolledWindow* m_breadcrumb_scroll = nullptr;
    Gtk::Box*            m_breadcrumb_box    = nullptr;
    Gtk::Button  m_back_btn;
    Gtk::Button  m_up_btn;
    Gtk::Button  m_refresh_btn;

    // Content stack + state pages
    Gtk::Stack*   m_content_stack    = nullptr;
    Gtk::Widget*  m_no_remote_status = nullptr;
    Gtk::Widget*  m_empty_status     = nullptr;
    Gtk::Widget*  m_error_status     = nullptr;

    // File list model
    Glib::RefPtr<Gio::ListStore<FileObject>> m_list_store;
    Glib::RefPtr<Gtk::SortListModel>         m_sort_model;
    Glib::RefPtr<Gtk::MultiSelection>        m_file_selection;
    Gtk::ColumnView*                         m_column_view = nullptr;

    bool              m_show_hidden       = false;
    Gtk::CheckButton* m_show_hidden_check = nullptr;
    Gtk::Label*       m_status_label      = nullptr;

    // Navigation state
    std::string                    m_current_remote;
    std::string                    m_current_path;
    bool                           m_is_local = false;
    std::vector<std::string>       m_path_history;
    std::vector<rclone::RemoteInfo> m_remotes;
    uint64_t                       m_load_generation = 0;
    bool                           m_initialized     = false;

    void setup_header();
    void build_column_view();
    void on_remote_selection_changed();
    void navigate(const std::string& path);
    void rebuild_breadcrumbs();
    void on_item_activated(guint position);
    void go_back();
    void go_up();
    void show_content_state(const std::string& name);

    static std::string mime_to_icon(const std::string& mime, bool is_dir);
    static const char* mime_to_css_class(const std::string& mime, bool is_dir);
    static std::string format_size(int64_t bytes);
};

} // namespace mtsync
