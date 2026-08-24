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
#include <adwaita.h>
#include <gtkmm.h>
#include <vector>

namespace mtsync {

// First-run welcome screen: lets the user take a short guided tour of the
// core workflow (remote → source/destination → job type → job options →
// run → schedule) or dismiss it, optionally for good.
class WelcomeDialog : public Gtk::Window {
public:
    explicit WelcomeDialog(Settings& settings);

private:
    Settings& m_settings;

    Gtk::Stack*        m_top_stack      = nullptr;
    AdwCarousel*        m_carousel      = nullptr;
    Gtk::CheckButton*  m_dont_show_check = nullptr;
    Gtk::Button*       m_back_btn       = nullptr;
    Gtk::Button*       m_next_btn       = nullptr;

    std::vector<Gtk::Widget*> m_tour_pages;
    unsigned m_current_step = 0;

    void setup_ui();
    void build_tour_pages();
    void start_tour();
    void go_next();
    void go_back();
    void finish_tour();
    void on_skip_clicked();
    void on_page_changed(unsigned index);
    void update_nav_buttons(unsigned index);
};

} // namespace mtsync
