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

#include "window.hpp"
#include "widgets/adw_wrapper.hpp"

namespace mtsync {

MtSyncWindow::MtSyncWindow(rclone::RcloneManager& manager, DaemonProxy* daemon_proxy,
                           Settings& settings)
    : m_settings(settings)
    , m_daemon_proxy(daemon_proxy)
    , m_remotes_view(manager)
    , m_job_view(daemon_proxy)
    , m_browser_view(manager)
    , m_settings_view(m_settings)
    , m_about_view(manager, daemon_proxy, m_settings) {
    set_title("Mt. Sync");
    set_default_size(1250, 900);
    set_icon_name("com.mtsync.MtSync");

    // CSS for view switcher tab styling
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        "viewswitcher button:checked {\n"
        "    background-color: #1a5276;\n"
        "}\n"
        "viewswitcher button:checked:hover {\n"
        "    background-color: #216694;\n"
        "}\n"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    m_view_stack = adw::view_stack_new();

    auto* page1 = adw::view_stack_add_titled(
        m_view_stack, &m_browser_view, "browser", "Browse");
    adw_view_stack_page_set_icon_name(page1, "folder-symbolic");

    auto* page2 = adw::view_stack_add_titled(
        m_view_stack, &m_job_view, "jobs", "Jobs");
    adw_view_stack_page_set_icon_name(page2, "emblem-synchronizing-symbolic");

    auto* page3 = adw::view_stack_add_titled(
        m_view_stack, &m_remotes_view, "backends", "Remotes");
    adw_view_stack_page_set_icon_name(page3, "network-server-symbolic");

    auto* page4 = adw::view_stack_add_titled(
        m_view_stack, &m_settings_view, "settings", "Settings");
    adw_view_stack_page_set_icon_name(page4, "preferences-other-symbolic");

    auto* page5 = adw::view_stack_add_titled(
        m_view_stack, &m_about_view, "about", "About");
    adw_view_stack_page_set_icon_name(page5, "help-about-symbolic");

    auto* switcher = adw::view_switcher(m_view_stack);
    auto* header = adw::header_bar();
    adw::header_bar_set_show_start_title_buttons(header, false);
    adw::header_bar_set_show_end_title_buttons(header, false);
    adw::header_bar_set_title_widget(header, switcher);

    // Toast overlay wraps the view stack
    m_toast_overlay = adw::toast_overlay();
    adw::toast_overlay_set_child(m_toast_overlay, adw::view_stack_widget(m_view_stack));

    auto* toolbar = adw::toolbar_view();
    adw::toolbar_view_add_top_bar(toolbar, header);
    adw::toolbar_view_set_content(toolbar, m_toast_overlay);

    set_child(*toolbar);

    m_browser_view.signal_job_created.connect(
        sigc::mem_fun(m_job_view, &JobView::add_job));
    m_browser_view.signal_job_saved.connect(
        sigc::mem_fun(m_job_view, &JobView::add_job_no_run));

    signal_close_request().connect([this]() -> bool {
        if (m_settings.shutdown_daemon_on_close && m_daemon_proxy)
            m_daemon_proxy->quit();
        return false;
    }, false);
}

} // namespace mtsync
