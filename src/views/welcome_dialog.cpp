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

#include "views/welcome_dialog.hpp"
#include "widgets/adw_wrapper.hpp"

namespace mtsync {

WelcomeDialog::WelcomeDialog(Settings& settings)
    : m_settings(settings) {
    set_title("Welcome");
    set_default_size(480, 440);
    set_modal(true);
    set_destroy_with_parent(true);
    setup_ui();
}

void WelcomeDialog::setup_ui() {
    m_top_stack = Gtk::make_managed<Gtk::Stack>();
    m_top_stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    set_child(*m_top_stack);

    // ── "welcome" page ──────────────────────────────────────────────────
    auto* welcome_status = adw::status_page();
    adw::status_page_set_icon_name(welcome_status, "help-about-symbolic");
    adw::status_page_set_title(welcome_status, "Welcome to Mt. Sync");
    adw::status_page_set_description(welcome_status,
        "Mount or sync network storage anywhere. Take a quick tour to learn "
        "the basics, or jump right in.");

    auto* welcome_extra = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    welcome_extra->set_halign(Gtk::Align::CENTER);

    auto* button_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    button_box->set_halign(Gtk::Align::CENTER);

    auto* skip_btn = Gtk::make_managed<Gtk::Button>("Skip");
    skip_btn->add_css_class("flat");
    skip_btn->signal_clicked().connect([this]() { on_skip_clicked(); });

    auto* tour_btn = Gtk::make_managed<Gtk::Button>("Take the Tour");
    tour_btn->add_css_class("suggested-action");
    tour_btn->signal_clicked().connect([this]() { start_tour(); });

    button_box->append(*skip_btn);
    button_box->append(*tour_btn);
    welcome_extra->append(*button_box);

    m_dont_show_check = Gtk::make_managed<Gtk::CheckButton>("Don't show this again");
    m_dont_show_check->set_active(true);
    m_dont_show_check->set_halign(Gtk::Align::CENTER);
    welcome_extra->append(*m_dont_show_check);

    adw::status_page_set_child(welcome_status, welcome_extra);
    m_top_stack->add(*welcome_status, "welcome");

    // ── "tour" page ─────────────────────────────────────────────────────
    auto* tour_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    tour_box->set_vexpand(true);

    m_carousel = adw::carousel_new();
    auto* carousel_widget = adw::carousel_widget(m_carousel);
    carousel_widget->set_vexpand(true);
    tour_box->append(*carousel_widget);

    build_tour_pages();

    auto* dots = adw::carousel_indicator_dots(m_carousel);
    dots->set_halign(Gtk::Align::CENTER);
    dots->set_margin_top(6);
    dots->set_margin_bottom(6);
    tour_box->append(*dots);

    auto* nav_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    nav_box->set_margin_start(16);
    nav_box->set_margin_end(16);
    nav_box->set_margin_bottom(16);

    m_back_btn = Gtk::make_managed<Gtk::Button>("Back");
    m_back_btn->set_sensitive(false);
    m_back_btn->signal_clicked().connect([this]() { go_back(); });

    auto* nav_spacer = Gtk::make_managed<Gtk::Box>();
    nav_spacer->set_hexpand(true);

    m_next_btn = Gtk::make_managed<Gtk::Button>("Next");
    m_next_btn->add_css_class("suggested-action");
    m_next_btn->signal_clicked().connect([this]() { go_next(); });

    nav_box->append(*m_back_btn);
    nav_box->append(*nav_spacer);
    nav_box->append(*m_next_btn);
    tour_box->append(*nav_box);

    m_top_stack->add(*tour_box, "tour");
    m_top_stack->set_visible_child("welcome");

    g_signal_connect(m_carousel, "page-changed",
        G_CALLBACK(+[](AdwCarousel*, guint index, gpointer data) {
            static_cast<WelcomeDialog*>(data)->on_page_changed(index);
        }), this);
}

void WelcomeDialog::build_tour_pages() {
    struct Step {
        const char* icon;
        const char* title;
        const char* desc;
    };
    static const Step steps[] = {
        { "network-server-symbolic", "Add a Remote",
          "Head to the Remotes tab and click + to connect a cloud provider "
          "or network share." },
        { "folder-open-symbolic", "Choose Source & Destination",
          "In the Browse tab, pick a source in the left pane and a "
          "destination in the right — swap them anytime with the swap "
          "button." },
        { "view-list-symbolic", "Pick a Job Type",
          "Sync, Copy, Move, or Mount — choose what kind of job to run "
          "from the Browse tab's action buttons or the job editor's Type "
          "field." },
        { "preferences-system-symbolic", "Set Job Options",
          "Fine-tune the job — dry run, bi-directional sync, checksums, "
          "cache mode — in the job editor's Job tab." },
        { "media-playback-start-symbolic", "Run It",
          "Hit Run Now, or save it and start it later from the Jobs tab." },
        { "alarm-symbolic", "Advanced: Scheduling (optional)",
          "Want it to run automatically? Enable a schedule in the job "
          "editor's Schedule tab and set a cron-style time." },
    };

    for (const auto& step : steps) {
        auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        page->set_valign(Gtk::Align::CENTER);
        page->set_halign(Gtk::Align::CENTER);
        page->set_margin_start(32);
        page->set_margin_end(32);
        page->set_margin_top(24);
        page->set_margin_bottom(24);

        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(step.icon);
        icon->set_pixel_size(64);
        page->append(*icon);

        auto* title = Gtk::make_managed<Gtk::Label>(step.title);
        title->add_css_class("title-2");
        title->set_wrap(true);
        title->set_justify(Gtk::Justification::CENTER);
        page->append(*title);

        auto* desc = Gtk::make_managed<Gtk::Label>(step.desc);
        desc->add_css_class("dim-label");
        desc->set_wrap(true);
        desc->set_justify(Gtk::Justification::CENTER);
        desc->set_max_width_chars(40);
        page->append(*desc);

        adw::carousel_append(m_carousel, page);
        m_tour_pages.push_back(page);
    }
}

void WelcomeDialog::start_tour() {
    m_top_stack->set_visible_child("tour");
    if (!m_tour_pages.empty())
        adw::carousel_scroll_to(m_carousel, m_tour_pages.front(), false);
    update_nav_buttons(0);
}

void WelcomeDialog::go_next() {
    if (m_current_step + 1 < m_tour_pages.size())
        adw::carousel_scroll_to(m_carousel, m_tour_pages[m_current_step + 1], true);
    else
        finish_tour();
}

void WelcomeDialog::go_back() {
    if (m_current_step > 0)
        adw::carousel_scroll_to(m_carousel, m_tour_pages[m_current_step - 1], true);
}

void WelcomeDialog::finish_tour() {
    m_settings.show_welcome_on_startup = false;
    save_settings(m_settings);
    close();
}

void WelcomeDialog::on_skip_clicked() {
    if (m_dont_show_check->get_active()) {
        m_settings.show_welcome_on_startup = false;
        save_settings(m_settings);
    }
    close();
}

void WelcomeDialog::on_page_changed(unsigned index) {
    m_current_step = index;
    update_nav_buttons(index);
}

void WelcomeDialog::update_nav_buttons(unsigned index) {
    m_back_btn->set_sensitive(index > 0);
    bool last = (index + 1 >= m_tour_pages.size());
    m_next_btn->set_label(last ? "Done" : "Next");
}

} // namespace mtsync
