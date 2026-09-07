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

#include "views/browser_view.hpp"
#include "views/compare_dialog.hpp"
#include "views/job_edit_dialog.hpp"
#include <glibmm/i18n.h>

namespace mtsync {

BrowserView::~BrowserView() = default;

BrowserView::BrowserView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {
    set_vexpand(true);
    set_hexpand(true);

    // CSS for active-pane indicator (top accent stripe)
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".browser-pane-active { box-shadow: inset 0 3px 0 @accent_color; }\n"
        ".action-green { background-color: #528a6f; color: white; }\n"
        ".action-green:hover { background-color: #5e9a7d; }\n"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Two panes separated by a draggable handle
    auto* paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    paned->set_vexpand(true);
    paned->set_wide_handle(true);

    m_left_pane  = Gtk::make_managed<BrowserPane>(manager);
    m_right_pane = Gtk::make_managed<BrowserPane>(manager);

    paned->set_start_child(*m_left_pane);
    paned->set_end_child(*m_right_pane);
    paned->set_shrink_start_child(false);
    paned->set_shrink_end_child(false);

    append(*paned);

    // ── Transfer action bar ──────────────────────────────────────────────
    auto* action_bar = Gtk::make_managed<Gtk::ActionBar>();

    auto make_icon_btn = [](const char* icon_name, const char* label_text) {
        auto* btn = Gtk::make_managed<Gtk::Button>();
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name(icon_name);
        auto* lbl = Gtk::make_managed<Gtk::Label>(label_text);
        box->append(*img);
        box->append(*lbl);
        btn->set_child(*box);
        return btn;
    };

    // Copy button
    auto* copy_btn = make_icon_btn("edit-copy-symbolic", _("Copy"));
    copy_btn->add_css_class("action-green");
    copy_btn->set_tooltip_text(_("Copy files from the source pane to the destination pane, creating a new rclone Copy job"));
    copy_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Copy);
    });

    // Move button
    auto* move_btn = make_icon_btn("document-send-symbolic", _("Move"));
    move_btn->add_css_class("action-green");
    move_btn->set_tooltip_text(_("Move files from the source pane to the destination and delete them from the source, creating a new rclone Move job"));
    move_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Move);
    });

    // Sync button
    auto* sync_btn = make_icon_btn("emblem-synchronizing-symbolic", _("Sync"));
    sync_btn->add_css_class("action-green");
    sync_btn->set_tooltip_text(_("Synchronise the source directory with the destination, making it an exact mirror; files deleted from the source are also removed from the destination"));
    sync_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Sync);
    });

    // Mount button
    auto* mount_btn = make_icon_btn("drive-harddisk-symbolic", _("Mount"));
    mount_btn->add_css_class("action-green");
    mount_btn->set_tooltip_text(_("Mount the source remote as a local filesystem at the destination path using rclone mount"));
    mount_btn->signal_clicked().connect([this]() {
        show_job_dialog(rclone::JobType::Mount);
    });

    // Swap button - reverses source/destination
    auto* swap_btn = Gtk::make_managed<Gtk::Button>(_("Swap ↔"));
    swap_btn->set_tooltip_text(_("Swap the left (source) and right (destination) panes so they exchange roles"));
    swap_btn->signal_clicked().connect([this]() {
        swap_source_destination();
    });

    // Left box for Copy/Move/Sync/Mount buttons
    auto* left_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    left_box->append(*copy_btn);
    left_box->append(*move_btn);
    left_box->append(*sync_btn);
    left_box->append(*mount_btn);

    // Center box for Swap button
    auto* swap_center = Gtk::make_managed<Gtk::CenterBox>();
    swap_center->set_center_widget(*swap_btn);

    // Delete button
    auto* delete_btn = Gtk::make_managed<Gtk::Button>();
    auto* delete_icon = Gtk::make_managed<Gtk::Image>();
    delete_icon->set_from_icon_name("user-trash-symbolic");
    delete_icon->set_icon_size(Gtk::IconSize::NORMAL);
    auto* delete_label = Gtk::make_managed<Gtk::Label>(_("Delete"));
    auto* delete_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    delete_box->append(*delete_icon);
    delete_box->append(*delete_label);
    delete_btn->set_child(*delete_box);
    delete_btn->add_css_class("destructive-action");
    delete_btn->set_tooltip_text(_("Permanently delete the selected files or folders in the active pane — this action cannot be undone"));
    delete_btn->signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::on_delete_confirm));

    // Compare button
    auto* compare_btn = make_icon_btn("folder-visiting-symbolic", _("Compare"));
    compare_btn->set_tooltip_text(_("Open a side-by-side comparison of the source and destination directories, highlighting files that are unique to one side, identical, or differ in content"));
    compare_btn->signal_clicked().connect(sigc::mem_fun(*this, &BrowserView::on_compare));

    // New Folder — MenuButton with a Popover containing a text entry
    auto* mkdir_btn    = Gtk::make_managed<Gtk::MenuButton>();
    auto* mkdir_popover = Gtk::make_managed<Gtk::Popover>();
    auto* pop_box      = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    auto* folder_entry = Gtk::make_managed<Gtk::Entry>();
    auto* create_btn   = Gtk::make_managed<Gtk::Button>(_("Create"));

    auto* mkdir_icon = Gtk::make_managed<Gtk::Image>();
    mkdir_icon->set_from_icon_name("folder-new-symbolic");
    mkdir_icon->set_icon_size(Gtk::IconSize::NORMAL);
    auto* mkdir_label = Gtk::make_managed<Gtk::Label>(_("New Folder"));
    auto* mkdir_label_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    mkdir_label_box->append(*mkdir_icon);
    mkdir_label_box->append(*mkdir_label);
    mkdir_btn->set_child(*mkdir_label_box);
    mkdir_btn->set_tooltip_text(_("Create a new folder at the current location in the active pane"));

    pop_box->set_margin(12);
    folder_entry->set_placeholder_text(_("Folder name"));
    folder_entry->set_width_chars(22);
    create_btn->add_css_class("suggested-action");
    pop_box->append(*folder_entry);
    pop_box->append(*create_btn);
    mkdir_popover->set_child(*pop_box);
    mkdir_btn->set_popover(*mkdir_popover);

    create_btn->signal_clicked().connect([this, folder_entry, mkdir_popover]() {
        auto name = std::string(folder_entry->get_text());
        if (name.empty() || !m_active_pane) return;
        if (name.find('/') != std::string::npos || name.find("..") != std::string::npos) return;
        folder_entry->set_text("");
        mkdir_popover->popdown();
        auto dest = m_active_pane->get_current_rclone_path();
        if (dest.empty()) return;
        m_manager.cli().mkdir(dest + "/" + name, [this](auto) {
            if (m_active_pane) m_active_pane->refresh();
        });
    });

    // Also trigger create on Enter key in the entry
    folder_entry->signal_activate().connect([create_btn]() {
        create_btn->activate();
    });

    action_bar->pack_start(*left_box);
    action_bar->set_center_widget(*swap_center);
    action_bar->pack_end(*mkdir_btn);
    action_bar->pack_end(*delete_btn);
    action_bar->pack_end(*compare_btn);

    append(*action_bar);

    // Active-pane tracking — left pane starts active
    m_active_pane = m_left_pane;
    m_left_pane->set_active(true);

    m_left_pane->signal_focused.connect([this]() { set_active_pane(m_left_pane); });
    m_right_pane->signal_focused.connect([this]() { set_active_pane(m_right_pane); });
}

void BrowserView::set_active_pane(BrowserPane* pane) {
    if (m_active_pane == pane) return;
    if (m_active_pane) m_active_pane->set_active(false);
    m_active_pane = pane;
    if (m_active_pane) m_active_pane->set_active(true);
}

void BrowserView::swap_source_destination() {
    m_left_pane->swap_location_with(*m_right_pane);
}

void BrowserView::show_job_dialog(rclone::JobType type) {
    auto* src_pane = m_left_pane;
    auto* dst_pane = m_right_pane;

    auto src = src_pane->get_current_rclone_path();
    auto dst = dst_pane->get_current_rclone_path();

    std::vector<std::string> includes;
    auto selected = src_pane->get_selected_files();
    if (!selected.empty()) {
        for (auto& f : selected) {
            auto name = std::string(f->property_name.get_value());
            includes.push_back(f->property_is_dir.get_value() ? (name + "/**") : name);
        }
    }

    m_job_dialog = std::make_unique<JobEditDialog>(type, src, dst, includes,
        [this](rclone::Job job) { signal_job_created.emit(job); });
    m_job_dialog->set_save_callback([this](rclone::Job job) {
        signal_job_saved.emit(job);
    });
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root()))
        m_job_dialog->set_transient_for(*win);
    m_job_dialog->present();
}

void BrowserView::on_compare() {
    auto src = m_left_pane->get_current_rclone_path();
    auto dst = m_right_pane->get_current_rclone_path();
    if (src.empty() || dst.empty()) return;
    m_compare_dialog = std::make_unique<CompareDialog>(src, dst, m_manager);
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root()))
        m_compare_dialog->set_transient_for(*win);
    m_compare_dialog->present();
}

void BrowserView::on_delete_confirm() {
    if (!m_active_pane) return;
    if (m_active_pane->get_selected_files().empty()) return;

    auto* dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Delete Files?"),
        _("The selected files will be permanently deleted.")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "delete", _("Delete"));
    adw_alert_dialog_set_response_appearance(dlg, "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");

    g_signal_connect(dlg, "response",
        G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer data) {
            if (std::string_view(response) == "delete")
                static_cast<BrowserView*>(data)->on_delete();
        }), this);

    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(get_root()->gobj()));
}

void BrowserView::on_delete() {
    if (!m_active_pane) return;
    auto files = m_active_pane->get_selected_files();
    if (files.empty()) return;
    auto dir_path = m_active_pane->get_current_rclone_path();
    if (dir_path.empty()) return;

    std::vector<std::string> includes;
    includes.reserve(files.size());
    for (auto& f : files) {
        auto name = std::string(f->property_name.get_value());
        includes.push_back(f->property_is_dir.get_value() ? name + "/**" : name);
    }

    m_manager.cli().delete_files(dir_path, includes, [this](auto) {
        if (m_active_pane) m_active_pane->refresh();
    });
}

} // namespace mtsync
