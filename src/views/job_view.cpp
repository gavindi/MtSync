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

#include "views/job_view.hpp"
#include "views/job_edit_dialog.hpp"
#include "rclone/cron_utils.hpp"
#include "widgets/adw_wrapper.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <deque>
#include <format>
#include <regex>

namespace mtsync {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string last_path_component(const std::string& rclone_path) {
    auto colon = rclone_path.find(':');
    std::string path_part = (colon != std::string::npos)
        ? rclone_path.substr(colon + 1)
        : rclone_path;

    // Strip trailing slashes
    while (path_part.size() > 1 && path_part.back() == '/')
        path_part.pop_back();

    if (path_part.empty())
        return (colon != std::string::npos) ? rclone_path.substr(0, colon) : "/";

    return fs::path(path_part).filename().string();
}

std::string job_display_name(const rclone::Job& job) {
    return last_path_component(job.source) + " → " + last_path_component(job.destination);
}

const char* type_icon(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return "emblem-synchronizing-symbolic";
        case rclone::JobType::Copy:  return "edit-copy-symbolic";
        case rclone::JobType::Move:  return "document-send-symbolic";
        case rclone::JobType::Mount: return "drive-harddisk-symbolic";
    }
    return "dialog-question-symbolic";
}

} // namespace

JobView::JobView(DaemonProxy* daemon_proxy)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_daemon_proxy(daemon_proxy)
    , m_paned(Gtk::Orientation::VERTICAL) {

    auto config_dir = fs::path(g_get_user_config_dir()) / "mtsync";
    fs::create_directories(config_dir);
    m_config_path = (config_dir / "jobs.json").string();

    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_size_request(-1, 150);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    m_scroll.set_child(*clamp);

    auto* groups_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);

    auto* header_group = adw::preferences_group();
    adw::preferences_group_set_title(header_group, "Jobs");
    auto* add_btn = Gtk::make_managed<Gtk::Button>();
    add_btn->set_icon_name("list-add-symbolic");
    add_btn->add_css_class("flat");
    add_btn->add_css_class("circular");
    add_btn->set_tooltip_text("Add a new sync, copy, move, or mount job");
    add_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobView::show_add_dialog));
    adw::preferences_group_set_header_suffix(header_group, add_btn);
    groups_box->append(*header_group);

    m_group_sync  = adw::preferences_group();
    adw::preferences_group_set_title(m_group_sync,  "Sync");
    m_group_copy  = adw::preferences_group();
    adw::preferences_group_set_title(m_group_copy,  "Copy");
    m_group_move  = adw::preferences_group();
    adw::preferences_group_set_title(m_group_move,  "Move");
    m_group_mount = adw::preferences_group();
    adw::preferences_group_set_title(m_group_mount, "Mount");
    groups_box->append(*m_group_sync);
    groups_box->append(*m_group_copy);
    groups_box->append(*m_group_move);
    groups_box->append(*m_group_mount);

    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(groups_box->gobj()));

    if (m_daemon_proxy) {
        m_daemon_proxy->signal_message().connect(
            sigc::mem_fun(*this, &JobView::on_daemon_message));
        // Request initial job list from daemon; subsequent updates arrive via broadcast messages
        m_daemon_proxy->get_jobs([this](auto) {});
    }

    // Log section
    auto* log_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    auto* log_header = Gtk::make_managed<Gtk::Label>("Activity Log");
    log_header->set_halign(Gtk::Align::START);
    log_header->add_css_class("caption");
    log_header->add_css_class("dim-label");
    log_header->set_margin_start(12);
    log_header->set_margin_top(6);
    log_header->set_margin_bottom(4);
    log_box->append(*log_header);

    m_log_store = Gio::ListStore<LogEntry>::create();
    auto no_sel = Gtk::NoSelection::create(m_log_store);

    m_log_column_view = Gtk::make_managed<Gtk::ColumnView>(no_sel);
    m_log_column_view->add_css_class("data-table");
    m_log_column_view->add_css_class("job-log");
    m_log_column_view->set_show_row_separators(true);

    // --- Time column ---
    auto time_factory = Gtk::SignalListItemFactory::create();
    time_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        item->set_child(*lbl);
    });
    time_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<LogEntry>(item->get_item());
        if (!obj) return;
        dynamic_cast<Gtk::Label*>(item->get_child())->set_text(obj->property_time.get_value());
    });
    auto time_col = Gtk::ColumnViewColumn::create("Time", time_factory);
    time_col->set_fixed_width(175);
    m_log_column_view->append_column(time_col);

    // --- State column ---
    auto state_factory = Gtk::SignalListItemFactory::create();
    state_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        item->set_child(*lbl);
    });
    state_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<LogEntry>(item->get_item());
        if (!obj) return;
        auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
        auto state = std::string(obj->property_state.get_value());
        lbl->set_text(state);
        for (auto cls : {"log-started","log-completed","log-failed","log-skipped","log-retrying","log-stopped"})
            lbl->remove_css_class(cls);
        if      (state == "STARTED")   lbl->add_css_class("log-started");
        else if (state == "COMPLETED") lbl->add_css_class("log-completed");
        else if (state == "FAILED")    lbl->add_css_class("log-failed");
        else if (state == "SKIPPED")   lbl->add_css_class("log-skipped");
        else if (state == "RETRYING")  lbl->add_css_class("log-retrying");
        else if (state == "STOPPED")   lbl->add_css_class("log-stopped");
    });
    auto state_col = Gtk::ColumnViewColumn::create("State", state_factory);
    state_col->set_fixed_width(110);
    m_log_column_view->append_column(state_col);

    // --- Job ID column ---
    auto id_factory = Gtk::SignalListItemFactory::create();
    id_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        lbl->add_css_class("monospace");
        lbl->set_ellipsize(Pango::EllipsizeMode::END);
        item->set_child(*lbl);
    });
    id_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<LogEntry>(item->get_item());
        if (!obj) return;
        dynamic_cast<Gtk::Label*>(item->get_child())->set_text(obj->property_job_id.get_value());
    });
    auto id_col = Gtk::ColumnViewColumn::create("Job ID", id_factory);
    id_col->set_fixed_width(290);
    m_log_column_view->append_column(id_col);

    // --- Type column ---
    auto type_factory = Gtk::SignalListItemFactory::create();
    type_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        item->set_child(*lbl);
    });
    type_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<LogEntry>(item->get_item());
        if (!obj) return;
        dynamic_cast<Gtk::Label*>(item->get_child())->set_text(obj->property_job_type.get_value());
    });
    auto type_col = Gtk::ColumnViewColumn::create("Type", type_factory);
    type_col->set_fixed_width(75);
    m_log_column_view->append_column(type_col);

    // --- Contents column (expands) ---
    auto contents_factory = Gtk::SignalListItemFactory::create();
    contents_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        box->set_hexpand(true);

        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.0f);
        lbl->set_hexpand(true);
        lbl->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*lbl);

        auto* btn = Gtk::make_managed<Gtk::Button>();
        btn->set_icon_name("document-open-symbolic");
        btn->add_css_class("flat");
        btn->set_visible(false);
        btn->set_valign(Gtk::Align::CENTER);
        btn->set_tooltip_text("Open the activity log file for this job in the default application");
        // Single permanent handler; path stored as GObject data to survive cell recycling
        btn->signal_clicked().connect([btn]() {
            const char* p = static_cast<const char*>(
                g_object_get_data(G_OBJECT(btn->gobj()), "log-path"));
            if (p && *p)
                Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(p));
        });
        box->append(*btn);
        item->set_child(*box);
    });
    contents_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<LogEntry>(item->get_item());
        if (!obj) return;
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        auto* lbl = dynamic_cast<Gtk::Label*>(box->get_first_child());
        auto* btn = dynamic_cast<Gtk::Button*>(lbl->get_next_sibling());
        lbl->set_text(obj->property_contents.get_value());
        std::string log_path = obj->property_log_path.get_value();
        g_object_set_data_full(G_OBJECT(btn->gobj()), "log-path",
            g_strdup(log_path.c_str()), g_free);
        btn->set_visible(!log_path.empty());
    });
    auto contents_col = Gtk::ColumnViewColumn::create("Activity", contents_factory);
    contents_col->set_expand(true);
    m_log_column_view->append_column(contents_col);

    static bool log_css_installed = false;
    if (!log_css_installed) {
        log_css_installed = true;
        auto css = Gtk::CssProvider::create();
        css->load_from_data(
            "columnview.job-log label { font-family: monospace; font-size: 0.8em; }\n"
            "columnview.job-log label.monospace { font-size: 0.8em; }\n"
            ".log-started   { color: @blue_3;   }\n"
            ".log-completed { color: @green_4;  }\n"
            ".log-failed    { color: @red_3;    }\n"
            ".log-skipped   { color: @orange_3; }\n"
            ".log-retrying  { color: @orange_3; }\n"
            ".log-stopped   { color: @purple_3; }\n"
        );
        Gtk::StyleContext::add_provider_for_display(
            Gdk::Display::get_default(), css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    m_log_scroll.set_child(*m_log_column_view);
    m_log_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_log_scroll.set_size_request(-1, 80);
    m_log_scroll.set_vexpand(true);
    log_box->append(m_log_scroll);

    m_paned.set_vexpand(true);
    m_paned.set_wide_handle(true);
    m_paned.set_start_child(m_scroll);
    m_paned.set_end_child(*log_box);
    m_paned.set_shrink_start_child(false);
    m_paned.set_shrink_end_child(false);
    m_paned.set_resize_start_child(false);
    m_paned.set_resize_end_child(true);
    append(m_paned);

    signal_map().connect([this]() {
        load_jobs();
        rebuild_ui();
        refresh_log();
        Glib::signal_idle().connect_once([this]() {
            int height = m_paned.get_height();
            m_paned.set_position(height > 0 ? height - 200 : 400);
        });
    });
}

JobView::~JobView() {
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
    }
}

void JobView::load_jobs() {
    m_jobs.clear();
    try {
        std::ifstream f(m_config_path);
        if (f) {
            auto j = json::parse(f);
            if (j.contains("jobs")) {
                for (auto& p : j["jobs"])
                    m_jobs.push_back(p.get<rclone::Job>());
            }
        }
    } catch (const std::exception& e) {
        g_warning("Failed to load jobs: %s", e.what());
    }
}

void JobView::save_jobs() {
    json j;
    j["jobs"] = json::array();
    for (auto& job : m_jobs)
        j["jobs"].push_back(job);

    auto target = fs::path(m_config_path);
    fs::create_directories(target.parent_path());
    auto tmp = target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream f(tmp);
        if (!f) { g_warning("save_jobs: cannot write to %s", tmp.c_str()); return; }
        f << j.dump(2);
        if (!f.good()) { g_warning("save_jobs: write error on %s", tmp.c_str()); fs::remove(tmp); return; }
    }
    try { fs::rename(tmp, target); }
    catch (const fs::filesystem_error& e) {
        g_warning("save_jobs: rename failed: %s", e.what());
        fs::remove(tmp);
    }
}

Gtk::Widget* JobView::group_for_type(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Copy:  return m_group_copy;
        case rclone::JobType::Move:  return m_group_move;
        case rclone::JobType::Mount: return m_group_mount;
        default:                     return m_group_sync;
    }
}

void JobView::update_group_visibility() {
    int counts[4] = {};
    for (auto& job : m_jobs) {
        switch (job.type) {
            case rclone::JobType::Sync:  counts[0]++; break;
            case rclone::JobType::Copy:  counts[1]++; break;
            case rclone::JobType::Move:  counts[2]++; break;
            case rclone::JobType::Mount: counts[3]++; break;
        }
    }
    m_group_sync ->set_visible(counts[0] > 0);
    m_group_copy ->set_visible(counts[1] > 0);
    m_group_move ->set_visible(counts[2] > 0);
    m_group_mount->set_visible(counts[3] > 0);
}

void JobView::rebuild_ui() {
    for (auto& ui : m_ui_rows) {
        ui.poll_timer.disconnect();
        if (ui.group)
            adw_preferences_group_remove(
                ADW_PREFERENCES_GROUP(ui.group->gobj()),
                ui.row->gobj());
    }
    m_ui_rows.clear();

    for (size_t i = 0; i < m_jobs.size(); ++i)
        append_job_row(i);
    update_group_visibility();
}

void JobView::remove_job_row(size_t index) {
    if (index >= m_ui_rows.size()) return;
    auto& ui = m_ui_rows[index];
    ui.poll_timer.disconnect();
    if (ui.group)
        adw_preferences_group_remove(
            ADW_PREFERENCES_GROUP(ui.group->gobj()),
            ui.row->gobj());
}

void JobView::append_job_row(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];
    const size_t i = index;

    auto* row = adw::preferences_row_new();
    adw::preferences_row_set_title(row, job_display_name(job).c_str());

    // Outer vertical container
    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    outer->set_margin_top(10);
    outer->set_margin_bottom(10);
    outer->set_margin_start(12);
    outer->set_margin_end(12);

    // Header row: type badge | job id (expands) | buttons
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

    auto* type_img = Gtk::make_managed<Gtk::Image>();
    type_img->set_from_icon_name(type_icon(job.type));
    type_img->set_icon_size(Gtk::IconSize::NORMAL);
    type_img->set_valign(Gtk::Align::CENTER);
    header->append(*type_img);

    auto* id_label = Gtk::make_managed<Gtk::Label>(job_display_name(job));
    id_label->add_css_class("heading");
    id_label->set_halign(Gtk::Align::START);
    id_label->set_hexpand(true);
    header->append(*id_label);

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

    JobUI ui;
    ui.row = row;

    ui.run_btn = std::make_unique<Gtk::Button>();
    ui.run_btn->set_icon_name("media-playback-start-symbolic");
    ui.run_btn->set_valign(Gtk::Align::CENTER);
    ui.run_btn->add_css_class("flat");
    ui.run_btn->add_css_class("success");
    ui.run_btn->set_tooltip_text("Run this job immediately");
    ui.run_btn->signal_clicked().connect([this, i]() { on_run_job(i); });

    ui.stop_btn = std::make_unique<Gtk::Button>();
    ui.stop_btn->set_icon_name("media-playback-stop-symbolic");
    ui.stop_btn->set_valign(Gtk::Align::CENTER);
    ui.stop_btn->add_css_class("flat");
    ui.stop_btn->add_css_class("destructive-action");
    ui.stop_btn->set_visible(false);
    ui.stop_btn->set_tooltip_text("Stop this job while it is running");
    ui.stop_btn->signal_clicked().connect([this, i]() { on_stop_job(i); });

    ui.edit_btn = std::make_unique<Gtk::Button>();
    ui.edit_btn->set_icon_name("document-edit-symbolic");
    ui.edit_btn->set_valign(Gtk::Align::CENTER);
    ui.edit_btn->add_css_class("flat");
    ui.edit_btn->set_tooltip_text("Edit this job's configuration, schedule, and advanced settings");
    ui.edit_btn->signal_clicked().connect([this, i]() { show_edit_dialog(i); });

    ui.del_btn = std::make_unique<Gtk::Button>();
    ui.del_btn->set_icon_name("user-trash-symbolic");
    ui.del_btn->set_valign(Gtk::Align::CENTER);
    ui.del_btn->add_css_class("flat");
    ui.del_btn->add_css_class("destructive-action");
    ui.del_btn->set_tooltip_text("Permanently delete this job from the list");
    ui.del_btn->signal_clicked().connect([this, i]() { on_delete_job(i); });

    // Disclosure toggle button (chevron), placed after delete button
    auto* expand_btn = Gtk::make_managed<Gtk::ToggleButton>();
    expand_btn->set_icon_name("pan-end-symbolic");
    expand_btn->set_valign(Gtk::Align::CENTER);
    expand_btn->add_css_class("flat");
    expand_btn->set_tooltip_text("Show or hide this job's details, including source path, destination path, and UUID");

    btn_box->append(*ui.run_btn);
    btn_box->append(*ui.stop_btn);
    btn_box->append(*ui.edit_btn);
    btn_box->append(*ui.del_btn);
    btn_box->append(*expand_btn);
    header->append(*btn_box);
    outer->append(*header);

    // Collapsible detail section: UUID + full paths
    auto* revealer = Gtk::make_managed<Gtk::Revealer>();
    revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    revealer->set_reveal_child(false);

    auto* detail_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    detail_box->set_margin_top(4);

    auto* uuid_label = Gtk::make_managed<Gtk::Label>(job.id);
    uuid_label->set_halign(Gtk::Align::START);
    uuid_label->set_hexpand(true);
    uuid_label->add_css_class("caption");
    uuid_label->add_css_class("dim-label");
    detail_box->append(*uuid_label);

    auto* path_label = Gtk::make_managed<Gtk::Label>(job.source + " → " + job.destination);
    path_label->set_halign(Gtk::Align::START);
    path_label->add_css_class("caption");
    path_label->add_css_class("dim-label");
    detail_box->append(*path_label);

    revealer->set_child(*detail_box);
    outer->append(*revealer);

    expand_btn->signal_toggled().connect([expand_btn, revealer]() {
        bool open = expand_btn->get_active();
        revealer->set_reveal_child(open);
        expand_btn->set_icon_name(open ? "pan-down-symbolic" : "pan-end-symbolic");
    });

    // Progress bar — only visible while a non-mount job is running
    ui.progress = std::make_unique<Gtk::ProgressBar>();
    ui.progress->set_fraction(0.0);
    ui.progress->set_hexpand(true);
    ui.progress->set_visible(false);
    outer->append(*ui.progress);

    // Footer row: status (left) | stats (right)
    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

    ui.status_label = std::make_unique<Gtk::Label>();
    ui.status_label->set_halign(Gtk::Align::START);
    ui.status_label->add_css_class("caption");
    ui.status_label->add_css_class("dim-label");
    if (!job.last_status.empty()) {
        ui.status_label->set_text("Last run: " + job.last_status);
        ui.status_label->set_visible(true);
    } else {
        ui.status_label->set_visible(false);
    }
    footer->append(*ui.status_label);

    auto* spacer = Gtk::make_managed<Gtk::Label>();
    spacer->set_hexpand(true);
    footer->append(*spacer);

    ui.stats_label = std::make_unique<Gtk::Label>();
    ui.stats_label->set_halign(Gtk::Align::END);
    ui.stats_label->add_css_class("caption");
    ui.stats_label->add_css_class("dim-label");
    ui.stats_label->set_visible(false);
    footer->append(*ui.stats_label);

    outer->append(*footer);

    // Restore UI state for running or mounted jobs
    if (job.type == rclone::JobType::Mount && job.running) {
        ui.run_btn->set_visible(false);
        ui.stop_btn->set_visible(true);
        ui.progress->set_visible(false);
        ui.status_label->set_text("Mounted");
        ui.status_label->set_visible(true);
    } else if (job.running) {
        ui.run_btn->set_visible(false);
        ui.stop_btn->set_visible(true);
        ui.progress->set_visible(true);
        ui.status_label->set_text("Running...");
        ui.status_label->set_visible(true);
    } else if (job.type == rclone::JobType::Mount && job.active) {
        ui.run_btn->set_visible(false);
        ui.stop_btn->set_visible(true);
        ui.status_label->set_text("Mounted");
        ui.status_label->set_visible(true);
    }

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row->gobj()), GTK_WIDGET(outer->gobj()));

    auto* grp = group_for_type(job.type);
    ui.group = grp;
    adw::preferences_group_add(grp, row);
    if (index < m_ui_rows.size()) {
        m_ui_rows[index] = std::move(ui);
    } else {
        m_ui_rows.push_back(std::move(ui));
    }
    update_group_visibility();
}

void JobView::show_add_dialog() {
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::unique_ptr<JobEditDialog>(new JobEditDialog(
        [this](rclone::Job job) { add_job(std::move(job)); }));
    m_edit_dialog->set_save_callback([this](rclone::Job job) {
        add_job_no_run(std::move(job));
    });
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void JobView::show_edit_dialog(size_t index) {
    if (index >= m_jobs.size()) return;
    auto* toplevel = dynamic_cast<Gtk::Window*>(get_root());
    m_edit_dialog = std::unique_ptr<JobEditDialog>(new JobEditDialog(m_jobs[index],
        [this, index](rclone::Job job) {
            if (index < m_jobs.size()) {
                m_jobs[index] = std::move(job);
                save_jobs();
                rebuild_ui();
                if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
                    m_daemon_proxy->update_job(index, m_jobs[index], [](auto) {});
                    if (!m_jobs[index].schedule_enabled)
                        on_run_job(index);
                }
            }
        }));
    m_edit_dialog->set_save_callback([this, index](rclone::Job job) {
        if (index < m_jobs.size()) {
            m_jobs[index] = std::move(job);
            save_jobs();
            rebuild_ui();
            if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
                m_daemon_proxy->update_job(index, m_jobs[index], [](auto) {});
            }
        }
    });
    if (toplevel) m_edit_dialog->set_transient_for(*toplevel);
    m_edit_dialog->present();
}

void JobView::add_job(rclone::Job job) {
    m_jobs.push_back(std::move(job));
    save_jobs();
    size_t index = m_jobs.size() - 1;
    append_job_row(index);

    if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
        m_daemon_proxy->add_job(m_jobs[index], [this, index](auto result) {
            if (!result.has_value()) {
                g_warning("Failed to add job to daemon: %s", result.error().c_str());
                return;
            }
            if (!m_jobs[index].schedule_enabled)
                on_run_job(index);
        });
    } else if (!m_jobs[index].schedule_enabled) {
        on_run_job(index);
    }
}

void JobView::add_job_no_run(rclone::Job job) {
    m_jobs.push_back(std::move(job));
    save_jobs();
    size_t index = m_jobs.size() - 1;
    append_job_row(index);

    if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
        m_daemon_proxy->add_job(m_jobs[index], [](auto) {});
    }
}

void JobView::on_run_job(size_t index) {
    if (index >= m_jobs.size() || index >= m_ui_rows.size()) return;
    auto& ui = m_ui_rows[index];
    auto type = m_jobs[index].type;

    ui.run_btn->set_visible(false);
    ui.stop_btn->set_visible(true);
    // Mount jobs never show progress bar
    if (type != rclone::JobType::Mount) {
        ui.progress->set_fraction(0.0);
        ui.progress->set_visible(true);
    }
    ui.status_label->set_visible(true);
    ui.status_label->set_text("Starting...");

    // Capture job data by value so the async callback is safe if m_jobs is modified
    auto src      = m_jobs[index].source;
    auto dst      = m_jobs[index].destination;
    auto dry_run  = m_jobs[index].dry_run;
    auto bisync   = m_jobs[index].bisync;
    auto bw       = m_jobs[index].bandwidth;
    auto includes = m_jobs[index].includes;

    if (!m_daemon_proxy || !m_daemon_proxy->is_connected()) {
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text("Error: not connected to daemon");
            m_ui_rows[index].run_btn->set_visible(true);
            m_ui_rows[index].stop_btn->set_visible(false);
        }
        return;
    }

    m_daemon_proxy->run_job(index, [this, index, type](auto result) {
        if (!result.has_value()) {
            if (index < m_ui_rows.size()) {
                m_ui_rows[index].status_label->set_text("Error: " + result.error());
                m_ui_rows[index].run_btn->set_visible(true);
                m_ui_rows[index].stop_btn->set_visible(false);
            }
            return;
        }
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text(type == rclone::JobType::Mount ? "Mounted" : "Running...");
        }
    });
}

void JobView::on_stop_job(size_t index) {
    if (index >= m_ui_rows.size()) return;

    if (!m_daemon_proxy || !m_daemon_proxy->is_connected()) {
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].status_label->set_text("Error: not connected to daemon");
        }
        return;
    }

    m_daemon_proxy->stop_job(index, [this, index](auto result) {
        if (index >= m_ui_rows.size()) return;
        m_ui_rows[index].poll_timer.disconnect();
        if (!result.has_value()) {
            m_ui_rows[index].status_label->set_text("Error: " + result.error());
        } else {
            m_ui_rows[index].status_label->set_text("Stopped");
        }
        m_ui_rows[index].run_btn->set_visible(true);
        m_ui_rows[index].stop_btn->set_visible(false);
        m_ui_rows[index].progress->set_fraction(0.0);
        m_ui_rows[index].progress->set_visible(false);
    });
}

void JobView::on_delete_job(size_t index) {
    if (index >= m_jobs.size()) return;
    if (index < m_ui_rows.size()) {
        m_ui_rows[index].poll_timer.disconnect();
    }

    if (m_daemon_proxy && m_daemon_proxy->is_connected()) {
        m_daemon_proxy->delete_job(index, [this](auto) {});
    } else {
        m_jobs.erase(m_jobs.begin() + static_cast<ptrdiff_t>(index));
        save_jobs();
        rebuild_ui();
    }
}

std::string JobView::format_speed(double bytes_per_sec) {
    if (bytes_per_sec < 1024)
        return std::format("{:.0f} B/s", bytes_per_sec);
    if (bytes_per_sec < 1024 * 1024)
        return std::format("{:.1f} KB/s", bytes_per_sec / 1024);
    if (bytes_per_sec < 1024 * 1024 * 1024)
        return std::format("{:.1f} MB/s", bytes_per_sec / (1024 * 1024));
    return std::format("{:.1f} GB/s", bytes_per_sec / (1024.0 * 1024 * 1024));
}

void JobView::refresh_log() {
    auto path = fs::path(g_get_user_state_dir()) / "mtsync" / "mtsync.log";
    std::ifstream f(path);
    m_log_store->remove_all();
    if (!f) return;

    // Only keep the last 100 lines to avoid blocking the UI thread on large logs
    constexpr size_t MAX_LINES = 100;
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(std::move(line));
        if (lines.size() > MAX_LINES) lines.pop_front();
    }

    // Pattern: [YYYY-MM-DD HH:MM:SS] STATE   UUID [TYPE] contents
    static const std::regex re(
        R"(\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\] (\w+)\s+(\S+) \[(\w+)\] (.*))");

    // Newest first: iterate in reverse
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        std::smatch m;
        if (std::regex_match(*it, m, re)) {
            std::string raw_contents = m[5].str();
            std::string log_path;
            auto sep = raw_contents.find(" | log:");
            if (sep != std::string::npos) {
                log_path     = raw_contents.substr(sep + 7);
                raw_contents = raw_contents.substr(0, sep);
            }
            m_log_store->append(LogEntry::create(
                m[1].str(), m[2].str(), m[3].str(), m[4].str(), raw_contents, log_path));
        } else if (!it->empty()) {
            m_log_store->append(LogEntry::create("", "", "", "", *it));
        }
    }

    // Scroll to top (newest entry)
    auto vadj = m_log_scroll.get_vadjustment();
    if (vadj) vadj->set_value(0.0);
}

void JobView::on_daemon_message(const nlohmann::json& msg) {
    auto type = msg.value("type", "");
    auto payload = msg.value("payload", json{});

    if (type == "jobs_list") {
        m_jobs.clear();
        if (payload.contains("jobs")) {
            for (auto& j : payload["jobs"]) {
                m_jobs.push_back(j.get<rclone::Job>());
            }
        }
        rebuild_ui();
    } else if (type == "job_added") {
        auto new_index = payload.value("index", static_cast<size_t>(0));
        if (payload.contains("job")) {
            auto job = payload["job"].get<rclone::Job>();
            if (new_index >= m_jobs.size()) {
                m_jobs.push_back(std::move(job));
            } else {
                m_jobs[new_index] = std::move(job);
            }
        }
        if (m_ui_rows.size() < m_jobs.size())
            append_job_row(m_ui_rows.size());
    } else if (type == "job_updated") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index < m_jobs.size() && payload.contains("job")) {
            m_jobs[index] = payload["job"].get<rclone::Job>();
            // Remove old row, rebuild just this one — avoids O(n) full teardown.
            // The updated row re-appends to the end of its type group.
            remove_job_row(index);
            m_ui_rows[index] = JobUI{};
            append_job_row(index);
        }
    } else if (type == "job_deleted") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index < m_jobs.size()) {
            if (index < m_ui_rows.size())
                m_ui_rows[index].poll_timer.disconnect();
            m_jobs.erase(m_jobs.begin() + static_cast<ptrdiff_t>(index));
            rebuild_ui();
        }
    } else if (type == "job_started") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index < m_ui_rows.size()) {
            m_ui_rows[index].run_btn->set_visible(false);
            m_ui_rows[index].stop_btn->set_visible(true);
            // Mount jobs never show progress bar
            if (index < m_jobs.size() && m_jobs[index].type != rclone::JobType::Mount) {
                m_ui_rows[index].progress->set_fraction(0.0);
                m_ui_rows[index].progress->set_visible(true);
            }
            m_ui_rows[index].status_label->set_visible(true);
            m_ui_rows[index].status_label->set_text("Starting...");
        }
    } else if (type == "job_progress") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index >= m_ui_rows.size()) return;
        auto& ui = m_ui_rows[index];

        auto bytes = payload.value("bytes", int64_t{0});
        auto total_bytes = payload.value("total_bytes", int64_t{1});
        auto transfers = payload.value("transfers", 0);
        auto total_transfers = payload.value("total_transfers", 0);
        auto speed = payload.value("speed", 0.0);

        double frac = (total_bytes > 0) ? static_cast<double>(bytes) / total_bytes : 0.0;
        ui.progress->set_fraction(frac);

        std::string text = std::format("{}/{} files | {}",
            transfers, total_transfers, format_speed(speed));
        if (payload.contains("eta")) {
            auto eta = payload["eta"].get<double>();
            int secs = static_cast<int>(eta);
            text += std::format(" | ETA {}:{:02d}", secs / 60, secs % 60);
        }
        ui.status_label->set_text(text);
    } else if (type == "job_completed") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index < m_ui_rows.size()) {
            auto& ui = m_ui_rows[index];
            ui.poll_timer.disconnect();
            ui.progress->set_fraction(0.0);
            ui.progress->set_visible(false);

            auto now = Glib::DateTime::create_now_local().format_iso8601();
            auto success = payload.value("success", false);
            auto stats_text = payload.value("stats_text", "");

            bool is_mount = (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount);

            if (success) {
                ui.status_label->set_text("Last run: success");
                if (index < m_jobs.size()) {
                    m_jobs[index].last_status = "success";
                    m_jobs[index].last_run = now;
                }
                if (is_mount) {
                    ui.run_btn->set_visible(false);
                    ui.stop_btn->set_visible(true);
                } else {
                    ui.run_btn->set_visible(true);
                    ui.stop_btn->set_visible(false);
                }
            } else {
                ui.status_label->set_text("Last run: error");
                if (index < m_jobs.size()) {
                    m_jobs[index].last_status = "error";
                    m_jobs[index].last_run = now;
                }
                ui.run_btn->set_visible(true);
                ui.stop_btn->set_visible(false);
            }
            ui.status_label->set_visible(true);

            // Show stats on the right
            if (!stats_text.empty()) {
                ui.stats_label->set_text(stats_text);
                ui.stats_label->set_visible(true);
            } else {
                ui.stats_label->set_visible(false);
            }

            save_jobs();
            refresh_log();
        }
    } else if (type == "job_stopped") {
        auto index = payload.value("index", static_cast<size_t>(0));
        if (index < m_ui_rows.size()) {
            auto& ui = m_ui_rows[index];
            ui.poll_timer.disconnect();
            ui.progress->set_fraction(0.0);
            ui.progress->set_visible(false);

            bool is_mount = (index < m_jobs.size() && m_jobs[index].type == rclone::JobType::Mount);
            ui.status_label->set_text(is_mount ? "Unmounted" : "Stopped");
            ui.run_btn->set_visible(true);
            ui.stop_btn->set_visible(false);
            ui.stats_label->set_visible(false);

            // Update job state
            if (index < m_jobs.size()) {
                m_jobs[index].running = false;
                m_jobs[index].last_status = "stopped";
            }
            save_jobs();
            refresh_log();
        }
    }
}

} // namespace mtsync
