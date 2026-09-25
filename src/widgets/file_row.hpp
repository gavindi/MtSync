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

#include "rclone/rclone_types.hpp"
#include <glibmm.h>

namespace mtsync {

// GObject wrapper around RemoteInfo for use with Gio::ListStore in the sidebar
class RemoteObject : public Glib::Object {
public:
    static Glib::RefPtr<RemoteObject> create(const rclone::RemoteInfo& info);

    Glib::Property<Glib::ustring> property_name{*this, "name"};
    Glib::Property<Glib::ustring> property_type{*this, "type"};

protected:
    RemoteObject();
};

// GObject wrapper around FileEntry for use with Gio::ListStore
class FileObject : public Glib::Object {
public:
    static Glib::RefPtr<FileObject> create(const rclone::FileEntry& entry);

    Glib::Property<Glib::ustring> property_name{*this, "name"};
    Glib::Property<Glib::ustring> property_path{*this, "path"};
    Glib::Property<gint64> property_size{*this, "size"};
    Glib::Property<Glib::ustring> property_mod_time{*this, "mod-time"};
    Glib::Property<Glib::ustring> property_mime_type{*this, "mime-type"};
    Glib::Property<bool> property_is_dir{*this, "is-dir"};

protected:
    FileObject();
};

// GObject wrapper for a parsed activity log line
class LogEntry : public Glib::Object {
public:
    static Glib::RefPtr<LogEntry> create(
        const std::string& time,
        const std::string& state,
        const std::string& job_id,
        const std::string& job_type,
        const std::string& contents,
        const std::string& log_path = "");

    Glib::Property<Glib::ustring> property_time    {*this, "time"};
    Glib::Property<Glib::ustring> property_state   {*this, "state"};
    Glib::Property<Glib::ustring> property_job_id  {*this, "job-id"};
    Glib::Property<Glib::ustring> property_job_type{*this, "job-type"};
    Glib::Property<Glib::ustring> property_contents{*this, "contents"};
    Glib::Property<Glib::ustring> property_log_path{*this, "log-path"};

protected:
    LogEntry();
};

// GObject wrapper for a single row in the Compare dialog's ColumnView
class CompareRowObject : public Glib::Object {
public:
    static Glib::RefPtr<CompareRowObject> create(
        char status,
        const std::string& src_name, int64_t src_size, const std::string& src_mod,
        const std::string& dst_name, int64_t dst_size, const std::string& dst_mod,
        const std::string& path = "",
        const std::string& action = "");

    Glib::Property<Glib::ustring> property_status  {*this, "status"};   // single-char string
    Glib::Property<Glib::ustring> property_src_name{*this, "src-name"};
    Glib::Property<gint64>        property_src_size {*this, "src-size"}; // -1 = not on this side
    Glib::Property<Glib::ustring> property_src_mod  {*this, "src-mod"};
    Glib::Property<Glib::ustring> property_dst_name {*this, "dst-name"};
    Glib::Property<gint64>        property_dst_size {*this, "dst-size"};
    Glib::Property<Glib::ustring> property_dst_mod  {*this, "dst-mod"};
    Glib::Property<Glib::ustring> property_path     {*this, "path"};    // relative path within remote
    Glib::Property<Glib::ustring> property_action   {*this, "action"};  // dry-run preview: "Copy", "Delete", ...

protected:
    CompareRowObject();
};

} // namespace mtsync
