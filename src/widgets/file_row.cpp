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

#include "widgets/file_row.hpp"

namespace mtsync {

RemoteObject::RemoteObject() : Glib::ObjectBase("MtSyncRemoteObject") {}

Glib::RefPtr<RemoteObject> RemoteObject::create(const rclone::RemoteInfo& info) {
    auto obj = Glib::make_refptr_for_instance(new RemoteObject());
    obj->property_name.set_value(info.name);
    obj->property_type.set_value(info.type);
    return obj;
}

FileObject::FileObject() : Glib::ObjectBase("MtSyncFileObject") {}

Glib::RefPtr<FileObject> FileObject::create(const rclone::FileEntry& entry) {
    auto obj = Glib::make_refptr_for_instance(new FileObject());
    obj->property_name.set_value(entry.name);
    obj->property_path.set_value(entry.path);
    obj->property_size.set_value(entry.size);
    obj->property_mod_time.set_value(entry.mod_time);
    obj->property_mime_type.set_value(entry.mime_type);
    obj->property_is_dir.set_value(entry.is_dir);
    return obj;
}

LogEntry::LogEntry() : Glib::ObjectBase("MtSyncLogEntry") {}

Glib::RefPtr<LogEntry> LogEntry::create(
    const std::string& time, const std::string& state,
    const std::string& job_id, const std::string& job_type,
    const std::string& contents, const std::string& log_path)
{
    auto obj = Glib::make_refptr_for_instance(new LogEntry());
    obj->property_time.set_value(time);
    obj->property_state.set_value(state);
    obj->property_job_id.set_value(job_id);
    obj->property_job_type.set_value(job_type);
    obj->property_contents.set_value(contents);
    obj->property_log_path.set_value(log_path);
    return obj;
}

CompareRowObject::CompareRowObject() : Glib::ObjectBase("MtSyncCompareRowObject") {}

Glib::RefPtr<CompareRowObject> CompareRowObject::create(
    char status,
    const std::string& src_name, int64_t src_size, const std::string& src_mod,
    const std::string& dst_name, int64_t dst_size, const std::string& dst_mod,
    const std::string& path,
    const std::string& action)
{
    auto obj = Glib::make_refptr_for_instance(new CompareRowObject());
    obj->property_status  .set_value(Glib::ustring(1, static_cast<gunichar>(status)));
    obj->property_src_name.set_value(src_name);
    obj->property_src_size.set_value(src_size);
    obj->property_src_mod .set_value(src_mod);
    obj->property_dst_name.set_value(dst_name);
    obj->property_dst_size.set_value(dst_size);
    obj->property_dst_mod .set_value(dst_mod);
    obj->property_path    .set_value(path);
    obj->property_action  .set_value(action);
    return obj;
}

} // namespace mtsync
