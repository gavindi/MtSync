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

#include "widgets/file_row.hpp"
#include <adwaita.h>
#include <gtkmm.h>
#include <memory>

namespace mtsync {

// Details dialog for a single activity-log entry, shown on double-click.
// If the entry references an on-disk error log (log-path), the file's
// contents are displayed; otherwise the entry's own text is shown.
class LogViewDialog : public Gtk::Window {
public:
    explicit LogViewDialog(const Glib::RefPtr<LogEntry>& entry);

private:
    void setup_ui(const Glib::RefPtr<LogEntry>& entry);

    Gtk::TextView* m_text_view = nullptr;
    std::string    m_log_path;
};

} // namespace mtsync