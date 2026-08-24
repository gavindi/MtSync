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

#include <glib.h>
#include <gio/gio.h>
#include <cairo/cairo.h>
#include <sigc++/sigc++.h>
#include <string>
#include <array>
#include <memory>
#include <vector>

namespace mtsync {

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    void set_tooltip(const std::string& text);
    void set_attention(bool attention);

    void start_animation();
    void stop_animation();
    bool is_animating() const { return m_animating; }
    GVariant* frame_pixmap() const;
    
    // Idle icon accessors
    bool has_idle_icon() const { return !m_idle_icon.empty(); }
    GVariant* get_idle_icon_pixmap() const { return idle_icon_pixmap(); }

    sigc::signal<void()>& signal_show_window() { return m_signal_show_window; }
    sigc::signal<void()>& signal_quit() { return m_signal_quit; }

private:
    static constexpr int   ANIM_FRAMES      = 16;
    static constexpr int   ICON_SIZE        = 47;
    static constexpr guint ANIM_INTERVAL_MS = 100;

    void build_frames();
    void load_idle_icon();
    GVariant* idle_icon_pixmap() const;

    guint m_owner_id = 0;

    sigc::signal<void()> m_signal_show_window;
    sigc::signal<void()> m_signal_quit;

    int              m_anim_frame = 0;
    bool             m_animating  = false;
    sigc::connection m_anim_timer;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::array<std::vector<uint8_t>, ANIM_FRAMES> m_frames;
    std::vector<uint8_t> m_idle_icon;
    cairo_surface_t* m_idle_surface = nullptr;

public:
    GDBusConnection* m_connection = nullptr;
    guint m_sni_reg_id = 0;
    guint m_menu_reg_id = 0;
};

} // namespace mtsync
