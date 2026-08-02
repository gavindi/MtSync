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

#include "views/job_edit_dialog.hpp"
#include "rclone/cron_utils.hpp"
#include "settings.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <format>
#include <random>
#include <sstream>

namespace mtsync {

namespace {

struct Preset { const char* label; const char* minute; const char* hour;
                const char* dom;   const char* month;  const char* dow; };

constexpr Preset k_presets[] = {
    {"Every minute", "*", "*", "*", "*", "*"},
    {"Hourly",       "0", "*", "*", "*", "*"},
    {"Daily",        "0", "0", "*", "*", "*"},
    {"Weekly",       "0", "0", "*", "*", "0"},
    {"Monthly",      "0", "0", "1", "*", "*"},
    {"Custom",       nullptr, nullptr, nullptr, nullptr, nullptr},
};

constexpr guint job_type_to_index(rclone::JobType t) {
    switch (t) {
        case rclone::JobType::Sync:  return 0;
        case rclone::JobType::Copy:  return 1;
        case rclone::JobType::Move:  return 2;
        case rclone::JobType::Mount: return 3;
    }
    return 0;
}

constexpr rclone::JobType index_to_job_type(guint i) {
    switch (i) {
        case 0:  return rclone::JobType::Sync;
        case 1:  return rclone::JobType::Copy;
        case 2:  return rclone::JobType::Move;
        case 3:  return rclone::JobType::Mount;
        default: return rclone::JobType::Sync;
    }
}

} // namespace

JobEditDialog::JobEditDialog(DoneCallback on_done)
    : m_on_done(std::move(on_done)) {
    setup_ui(rclone::JobType::Sync, "", "");
}

JobEditDialog::JobEditDialog(const rclone::Job& job, DoneCallback on_done)
    : m_on_done(std::move(on_done)), m_editing(job), m_includes(job.includes) {
    setup_ui(job.type, job.source, job.destination);
}

JobEditDialog::JobEditDialog(rclone::JobType type,
                               const std::string& src, const std::string& dst,
                               const std::vector<std::string>& includes,
                               DoneCallback on_done)
    : m_on_done(std::move(on_done)), m_includes(includes) {
    setup_ui(type, src, dst);
}

void JobEditDialog::setup_ui(rclone::JobType initial_type,
                               const std::string& initial_src,
                               const std::string& initial_dst) {
    set_title(m_editing ? "Edit Job" : "New Job");
    set_default_size(840, -1);
    set_modal(true);
    set_destroy_with_parent(true);

    // ── Outer container ───────────────────────────────────────────────────
    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_child(*outer);

    auto* stack = adw::view_stack_new();
    auto* switcher = adw::view_switcher(stack);
    switcher->set_margin_top(6);
    switcher->set_margin_start(4);
    switcher->set_margin_end(4);
    outer->append(*switcher);
    outer->append(*adw::view_stack_widget(stack));

    // ── "Job" tab ─────────────────────────────────────────────────────────
    auto* job_clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(job_clamp->gobj()), 520);
    job_clamp->set_margin_top(24);
    job_clamp->set_margin_bottom(12);
    job_clamp->set_margin_start(12);
    job_clamp->set_margin_end(12);
    adw_view_stack_page_set_icon_name(
        adw::view_stack_add_titled(stack, job_clamp, "job", "Job"),
        "document-edit-symbolic");

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(job_clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // Job Configuration group
    auto* group = adw::preferences_group();
    adw::preferences_group_set_title(group, "Job Configuration");
    vbox->append(*group);

    // Type combo row
    auto* type_list = gtk_string_list_new(nullptr);
    gtk_string_list_append(type_list, "Sync");
    gtk_string_list_append(type_list, "Copy");
    gtk_string_list_append(type_list, "Move");
    gtk_string_list_append(type_list, "Mount");
    m_type_combo = adw::combo_row();
    adw::preferences_row_set_title(m_type_combo, "Type");
    adw::combo_row_set_string_list_model(m_type_combo, type_list);
    g_object_unref(type_list);
    adw_combo_row_set_selected(ADW_COMBO_ROW(m_type_combo->gobj()),
        job_type_to_index(initial_type));
    adw::preferences_group_add(group, m_type_combo);

    // Source
    m_source_entry = adw::entry_row();
    adw::preferences_row_set_title(m_source_entry, "Source (remote:path)");
    if (!initial_src.empty())
        adw::entry_row_set_text(m_source_entry, initial_src.c_str());
    else if (m_editing)
        adw::entry_row_set_text(m_source_entry, m_editing->source.c_str());
    adw::preferences_group_add(group, m_source_entry);

    // Destination
    m_dest_entry = adw::entry_row();
    adw::preferences_row_set_title(m_dest_entry, "Destination (remote:path)");
    if (!initial_dst.empty())
        adw::entry_row_set_text(m_dest_entry, initial_dst.c_str());
    else if (m_editing)
        adw::entry_row_set_text(m_dest_entry, m_editing->destination.c_str());
    adw::preferences_group_add(group, m_dest_entry);

    // File filters
    m_includes_entry = adw::entry_row();
    adw::preferences_row_set_title(m_includes_entry, "File Include Filters (space-separated patterns)");
    gtk_text_set_placeholder_text(
        GTK_TEXT(gtk_editable_get_delegate(GTK_EDITABLE(m_includes_entry->gobj()))),
        "All files");
    if (!m_includes.empty()) {
        std::string joined;
        for (size_t i = 0; i < m_includes.size(); ++i) {
            if (i > 0) joined += ' ';
            joined += m_includes[i];
        }
        adw::entry_row_set_text(m_includes_entry, joined.c_str());
    }
    m_includes_entry->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_includes_entry);

    // Dry Run
    m_dry_run_switch = adw::switch_row();
    adw::preferences_row_set_title(m_dry_run_switch, "Dry Run");
    m_dry_run_switch->set_tooltip_text("Simulate the transfer without moving or modifying any files — useful for previewing what would change before committing");
    adw::switch_row_set_active(m_dry_run_switch, m_editing ? m_editing->dry_run : true);
    m_dry_run_switch->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_dry_run_switch);

    // Bi-directional sync (Sync only)
    m_bisync_switch = adw::switch_row();
    adw::preferences_row_set_title(m_bisync_switch, "Bi-directional sync");
    m_bisync_switch->set_tooltip_text("Synchronise changes in both directions so that new or modified files on either side are propagated to the other; uses rclone bisync");
    m_bisync_switch->set_visible(initial_type == rclone::JobType::Sync);
    if (m_editing) adw::switch_row_set_active(m_bisync_switch, m_editing->bisync);
    adw::preferences_group_add(group, m_bisync_switch);

    // Force delete propagation (Bi-directional sync only)
    m_bisync_force_switch = adw::switch_row();
    adw::preferences_row_set_title(m_bisync_force_switch, "Force Deletes");
    m_bisync_force_switch->set_tooltip_text("Allow bi-directional sync to propagate deletions. rclone's delete-safety check is unreliable when run this way, so without this enabled ANY deletion on either side — even a single file — will block the sync until you intervene manually. Enabling this removes that protection entirely, including for large accidental deletions.");
    m_bisync_force_switch->set_visible(initial_type == rclone::JobType::Sync
        && m_bisync_switch->get_visible() && adw::switch_row_get_active(m_bisync_switch));
    if (m_editing) adw::switch_row_set_active(m_bisync_force_switch, m_editing->bisync_force_deletes);
    adw::preferences_group_add(group, m_bisync_force_switch);

    // Enable Checksum
    m_enable_checksum_switch = adw::switch_row();
    adw::preferences_row_set_title(m_enable_checksum_switch, "Enable Checksum");
    m_enable_checksum_switch->set_tooltip_text("Verify each transferred file's checksum against the source — slower but guarantees bit-perfect copies");
    if (m_editing) adw::switch_row_set_active(m_enable_checksum_switch, !m_editing->ignore_checksum);
    m_enable_checksum_switch->set_visible(initial_type != rclone::JobType::Mount);
    adw::preferences_group_add(group, m_enable_checksum_switch);

    // Mount at Start-up (Mount only)
    m_mount_startup_switch = adw::switch_row();
    adw::preferences_row_set_title(m_mount_startup_switch, "Mount at Start-up");
    m_mount_startup_switch->set_tooltip_text("Automatically mount this remote when the Mt. Sync daemon starts, so the mount point is available on every login without manual intervention");
    m_mount_startup_switch->set_visible(initial_type == rclone::JobType::Mount);
    if (m_editing) adw::switch_row_set_active(m_mount_startup_switch, m_editing->mount_at_startup);
    adw::preferences_group_add(group, m_mount_startup_switch);

    // VFS Cache Mode (Mount only)
    auto* cache_list = gtk_string_list_new(nullptr);
    gtk_string_list_append(cache_list, "off");
    gtk_string_list_append(cache_list, "minimal");
    gtk_string_list_append(cache_list, "writes");
    gtk_string_list_append(cache_list, "full");
    m_cache_mode_row = adw::combo_row();
    adw::preferences_row_set_title(m_cache_mode_row, "Cache Mode");
    adw::combo_row_set_string_list_model(m_cache_mode_row, cache_list);
    g_object_unref(cache_list);
    m_cache_mode_row->set_visible(initial_type == rclone::JobType::Mount);
    {
        const char* modes[] = {"off", "minimal", "writes", "full"};
        int sel = 1; // default: minimal
        if (m_editing && !m_editing->vfs_cache_mode.empty()) {
            for (int i = 0; i < 4; i++) {
                if (m_editing->vfs_cache_mode == modes[i]) { sel = i; break; }
            }
        }
        adw_combo_row_set_selected(ADW_COMBO_ROW(m_cache_mode_row->gobj()), sel);
    }
    adw::preferences_group_add(group, m_cache_mode_row);

    // ── "Schedule" tab ────────────────────────────────────────────────────
    auto* sched_outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    adw_view_stack_page_set_icon_name(
        adw::view_stack_add_titled(stack, sched_outer, "schedule", "Schedule"),
        "alarm-symbolic");

    // Enable Schedule switch (full-width, above two-column area)
    auto* sched_enable_group = adw::preferences_group();
    sched_enable_group->set_margin_top(16);
    sched_enable_group->set_margin_bottom(8);
    sched_enable_group->set_margin_start(16);
    sched_enable_group->set_margin_end(16);
    sched_outer->append(*sched_enable_group);

    m_schedule_switch = adw::switch_row();
    adw::preferences_row_set_title(m_schedule_switch, "Enable Schedule");
    m_schedule_switch->set_tooltip_text("Run this job automatically on the cron schedule defined below instead of only when started manually");
    bool sched_on = m_editing && m_editing->schedule_enabled;
    adw::switch_row_set_active(m_schedule_switch, sched_on);
    adw::preferences_group_add(sched_enable_group, m_schedule_switch);

    // Two-column layout
    auto* sched_columns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    sched_columns->set_hexpand(true);
    sched_columns->set_vexpand(true);
    sched_outer->append(*sched_columns);

    // ── LEFT: editor panel ────────────────────────────────────────────────
    auto* left_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    left_scroll->set_hexpand(true);
    left_scroll->set_vexpand(true);
    left_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sched_columns->append(*left_scroll);

    auto* left_vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
    left_vbox->set_margin_top(8);
    left_vbox->set_margin_bottom(16);
    left_vbox->set_margin_start(16);
    left_vbox->set_margin_end(16);
    left_scroll->set_child(*left_vbox);

    // Presets group
    auto* preset_group = adw::preferences_group();
    left_vbox->append(*preset_group);

    auto* preset_list = gtk_string_list_new(nullptr);
    for (auto& p : k_presets) gtk_string_list_append(preset_list, p.label);
    m_preset_combo = adw::combo_row();
    adw::preferences_row_set_title(m_preset_combo, "Preset");
    adw::combo_row_set_string_list_model(m_preset_combo, preset_list);
    g_object_unref(preset_list);
    adw::preferences_group_add(preset_group, m_preset_combo);

    // Helper: make entry row with "clear to *" suffix button
    auto make_cron_entry = [&](const char* title, const char* initial) -> Gtk::Widget* {
        auto* row = adw::entry_row();
        adw::preferences_row_set_title(row, title);
        adw::entry_row_set_text(row, initial);
        auto* btn = Gtk::make_managed<Gtk::Button>();
        btn->set_icon_name("edit-clear-symbolic");
        btn->add_css_class("flat");
        btn->set_tooltip_text("Reset this cron field to wildcard (*), which matches every minute, hour, or day");
        btn->signal_clicked().connect([row]() { adw::entry_row_set_text(row, "*"); });
        adw_entry_row_add_suffix(ADW_ENTRY_ROW(row->gobj()), GTK_WIDGET(btn->gobj()));
        return row;
    };

    // Minutes / Hours group
    auto* time_group = adw::preferences_group();
    adw::preferences_group_set_title(time_group, "Minutes / Hours");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(time_group->gobj()),
        "Use * for every value, a number, a range (1-5), a list (1,3), or a step (*/2).");
    left_vbox->append(*time_group);

    m_minute_entry = make_cron_entry("Minute", m_editing ? m_editing->cron_minute.c_str() : "0");
    m_hour_entry   = make_cron_entry("Hour",   m_editing ? m_editing->cron_hour.c_str()   : "*");
    adw::preferences_group_add(time_group, m_minute_entry);
    adw::preferences_group_add(time_group, m_hour_entry);

    // Days group
    auto* days_group = adw::preferences_group();
    adw::preferences_group_set_title(days_group, "Days");
    left_vbox->append(*days_group);

    m_dom_entry = make_cron_entry("Day of Month", m_editing ? m_editing->cron_day.c_str() : "*");
    adw::preferences_group_add(days_group, m_dom_entry);

    // Day-of-week checkboxes as a plain row inside the preferences group
    auto* dow_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    dow_row->set_margin_top(8);
    dow_row->set_margin_bottom(8);
    dow_row->set_margin_start(12);
    dow_row->set_margin_end(12);
    auto* dow_label = Gtk::make_managed<Gtk::Label>("Day of Week");
    dow_label->set_xalign(0.0f);
    dow_label->add_css_class("caption");
    dow_label->add_css_class("dim-label");
    dow_row->append(*dow_label);
    auto* dow_checks_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    constexpr const char* dow_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    for (int i = 0; i < 7; ++i) {
        m_dow_checks[i] = Gtk::make_managed<Gtk::CheckButton>(dow_names[i]);
        m_dow_checks[i]->set_active(true);
        dow_checks_box->append(*m_dow_checks[i]);
    }
    dow_row->append(*dow_checks_box);
    adw::preferences_group_add(days_group, dow_row);

    // Months group
    auto* months_group = adw::preferences_group();
    adw::preferences_group_set_title(months_group, "Months");
    left_vbox->append(*months_group);

    auto* months_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    months_box->set_margin_top(8);
    months_box->set_margin_bottom(8);
    months_box->set_margin_start(12);
    months_box->set_margin_end(12);
    constexpr const char* month_names[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    auto* months_row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* months_row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    for (int i = 0; i < 12; ++i) {
        m_month_checks[i] = Gtk::make_managed<Gtk::CheckButton>(month_names[i]);
        m_month_checks[i]->set_active(true);
        (i < 6 ? months_row1 : months_row2)->append(*m_month_checks[i]);
    }
    months_box->append(*months_row1);
    months_box->append(*months_row2);
    adw::preferences_group_add(months_group, months_box);

    // Load existing cron values into checkboxes (if editing)
    if (m_editing) {
        set_dow_from_cron(m_editing->cron_weekday);
        set_month_from_cron(m_editing->cron_month);
    }

    // Detect which preset matches (default to Custom = index 5)
    {
        std::string cur_min  = m_editing ? m_editing->cron_minute  : "0";
        std::string cur_hour = m_editing ? m_editing->cron_hour    : "*";
        std::string cur_dom  = m_editing ? m_editing->cron_day     : "*";
        std::string cur_mo   = m_editing ? m_editing->cron_month   : "*";
        std::string cur_dow  = m_editing ? m_editing->cron_weekday : "*";
        guint preset_idx = 5; // Custom
        for (guint i = 0; i < 5; ++i) {
            if (cur_min  == k_presets[i].minute &&
                cur_hour == k_presets[i].hour   &&
                cur_dom  == k_presets[i].dom    &&
                cur_mo   == k_presets[i].month  &&
                cur_dow  == k_presets[i].dow) {
                preset_idx = i;
                break;
            }
        }
        m_preset_updating = true;
        adw_combo_row_set_selected(ADW_COMBO_ROW(m_preset_combo->gobj()), preset_idx);
        m_preset_updating = false;
    }

    // ── RIGHT: preview panel ──────────────────────────────────────────────
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sched_columns->append(*sep);

    auto* right_vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    right_vbox->set_size_request(270, -1);
    right_vbox->set_margin_top(8);
    right_vbox->set_margin_bottom(16);
    right_vbox->set_margin_start(16);
    right_vbox->set_margin_end(16);
    sched_columns->append(*right_vbox);

    auto* preview_title = Gtk::make_managed<Gtk::Label>("Schedule Preview");
    preview_title->add_css_class("heading");
    preview_title->set_xalign(0.0f);
    right_vbox->append(*preview_title);

    m_preview_cron_label = Gtk::make_managed<Gtk::Label>("");
    m_preview_cron_label->add_css_class("monospace");
    m_preview_cron_label->set_xalign(0.0f);
    right_vbox->append(*m_preview_cron_label);

    m_preview_desc_label = Gtk::make_managed<Gtk::Label>("");
    m_preview_desc_label->add_css_class("dim-label");
    m_preview_desc_label->set_xalign(0.0f);
    m_preview_desc_label->set_wrap(true);
    right_vbox->append(*m_preview_desc_label);

    right_vbox->append(*Gtk::make_managed<Gtk::Separator>());

    m_preview_calendar = Gtk::make_managed<Gtk::Calendar>();
    m_preview_calendar->set_hexpand(true);
    right_vbox->append(*m_preview_calendar);

    {
        GDateTime* now = g_date_time_new_now_local();
        GTimeZone* tz  = g_date_time_get_timezone(now);
        auto* tz_lbl = Gtk::make_managed<Gtk::Label>(
            std::format("System Time Zone: {}", g_time_zone_get_identifier(tz)).c_str());
        tz_lbl->add_css_class("dim-label");
        tz_lbl->add_css_class("caption");
        tz_lbl->set_xalign(0.0f);
        tz_lbl->set_wrap(true);
        right_vbox->append(*tz_lbl);
        g_date_time_unref(now);
    }

    auto* upcoming_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    upcoming_scroll->set_size_request(-1, 150);
    upcoming_scroll->set_vexpand(false);
    upcoming_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    right_vbox->append(*upcoming_scroll);

    m_upcoming_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    upcoming_scroll->set_child(*m_upcoming_box);

    // Calendar navigation → refresh marks
    m_preview_calendar->property_year().signal_changed().connect(
        sigc::mem_fun(*this, &JobEditDialog::refresh_calendar_marks));
    m_preview_calendar->property_month().signal_changed().connect(
        sigc::mem_fun(*this, &JobEditDialog::refresh_calendar_marks));

    // ── "Advanced" tab ────────────────────────────────────────────────────
    auto* adv_clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(adv_clamp->gobj()), 520);
    adv_clamp->set_margin_top(24);
    adv_clamp->set_margin_bottom(12);
    adv_clamp->set_margin_start(12);
    adv_clamp->set_margin_end(12);
    adw_view_stack_page_set_icon_name(
        adw::view_stack_add_titled(stack, adv_clamp, "advanced", "Advanced"),
        "preferences-other-symbolic");

    auto* adv_vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(adv_clamp->gobj()), GTK_WIDGET(adv_vbox->gobj()));

    auto* adv_group = adw::preferences_group();
    adv_vbox->append(*adv_group);

    m_bandwidth_entry = adw::entry_row();
    adw::preferences_row_set_title(m_bandwidth_entry, "Bandwidth Limit (e.g. 10M)");
    if (m_editing && !m_editing->bandwidth.empty())
        adw::entry_row_set_text(m_bandwidth_entry, m_editing->bandwidth.c_str());
    adw::preferences_group_add(adv_group, m_bandwidth_entry);

    {
        auto settings = load_settings();
        int pt_val = (m_editing && m_editing->parallel_transfers > 0)
                     ? m_editing->parallel_transfers
                     : settings.parallel_transfers;
        m_parallel_transfers_entry = adw::entry_row();
        adw::preferences_row_set_title(m_parallel_transfers_entry, "Parallel Transfers");
        adw::entry_row_set_text(m_parallel_transfers_entry,
            std::format("{}", pt_val).c_str());
        adw::preferences_group_add(adv_group, m_parallel_transfers_entry);

        int r_val = (m_editing && m_editing->retries >= 0)
                    ? m_editing->retries
                    : settings.retries;
        m_retries_entry = adw::entry_row();
        adw::preferences_row_set_title(m_retries_entry, "Retries on Failure");
        adw::entry_row_set_text(m_retries_entry, std::format("{}", r_val).c_str());
        adw::preferences_group_add(adv_group, m_retries_entry);
    }

    m_extra_flags_entry = adw::entry_row();
    adw::preferences_row_set_title(m_extra_flags_entry, "Extra rclone Flags");
    adw_entry_row_set_input_hints(ADW_ENTRY_ROW(m_extra_flags_entry->gobj()), GTK_INPUT_HINT_NO_SPELLCHECK);
    if (m_editing && !m_editing->extra_flags.empty())
        adw::entry_row_set_text(m_extra_flags_entry, m_editing->extra_flags.c_str());
    adw::preferences_group_add(adv_group, m_extra_flags_entry);

    // ── Buttons (outside tab stack) ───────────────────────────────────────
    m_action_btn = Gtk::make_managed<Gtk::Button>(sched_on ? "Schedule" : "Run Now");
    m_action_btn->add_css_class("destructive-action");
    m_action_btn->set_tooltip_text("Run or schedule this job immediately with the current settings");
    m_action_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobEditDialog::on_commit));

    m_save_btn = Gtk::make_managed<Gtk::Button>("Save");
    m_save_btn->add_css_class("suggested-action");
    m_save_btn->set_visible(!sched_on);
    m_save_btn->set_tooltip_text("Save this job's configuration without running or scheduling it now");
    m_save_btn->signal_clicked().connect(sigc::mem_fun(*this, &JobEditDialog::on_save));

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel_btn->add_css_class("success");
    cancel_btn->set_tooltip_text("Discard any unsaved changes and close this dialog");
    cancel_btn->signal_clicked().connect([this]() { close(); });

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);
    btn_box->set_margin_top(6);
    btn_box->set_margin_bottom(18);
    btn_box->append(*m_action_btn);
    btn_box->append(*m_save_btn);
    btn_box->append(*cancel_btn);
    outer->append(*btn_box);

    // ── Reactive wiring ───────────────────────────────────────────────────

    // Show/hide type-specific options when type changes
    g_signal_connect(m_type_combo->gobj(), "notify::selected",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            guint sel = adw::combo_row_get_selected(self->m_type_combo);
            self->m_bisync_switch->set_visible(sel == 0);             // Sync only
            self->m_bisync_force_switch->set_visible(sel == 0
                && adw::switch_row_get_active(self->m_bisync_switch));
            self->m_mount_startup_switch->set_visible(sel == 3);      // Mount only
            self->m_cache_mode_row->set_visible(sel == 3);            // Mount only
            self->m_includes_entry->set_visible(sel != 3);            // Not for Mount
            self->m_dry_run_switch->set_visible(sel != 3);            // Not for Mount
            self->m_enable_checksum_switch->set_visible(sel != 3);    // Not for Mount
            self->set_default_size(460, 1);
        }), this);

    // Bi-directional sync switch → show/hide dependent Force Deletes row
    g_signal_connect(m_bisync_switch->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            self->m_bisync_force_switch->set_visible(self->m_bisync_switch->get_visible()
                && adw::switch_row_get_active(self->m_bisync_switch));
        }), this);

    // Schedule switch → update button label and save visibility
    g_signal_connect(m_schedule_switch->gobj(), "notify::active",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            bool on = adw::switch_row_get_active(self->m_schedule_switch);
            self->m_action_btn->set_label(on ? "Schedule" : "Run Now");
            self->m_save_btn->set_visible(!on);
        }), this);

    // Cron entry fields → update preview, switch preset to Custom
    auto entry_changed_cb = G_CALLBACK(+[](GtkEditable*, gpointer data) {
        auto* self = static_cast<JobEditDialog*>(data);
        if (!self->m_preset_updating) {
            self->m_preset_updating = true;
            adw_combo_row_set_selected(ADW_COMBO_ROW(self->m_preset_combo->gobj()), 5);
            self->m_preset_updating = false;
        }
        self->update_preview();
    });
    g_signal_connect(m_minute_entry->gobj(), "changed", entry_changed_cb, this);
    g_signal_connect(m_hour_entry->gobj(),   "changed", entry_changed_cb, this);
    g_signal_connect(m_dom_entry->gobj(),    "changed", entry_changed_cb, this);

    // Checkboxes → update preview, switch preset to Custom
    auto check_cb = [this]() {
        if (!m_preset_updating) {
            m_preset_updating = true;
            adw_combo_row_set_selected(ADW_COMBO_ROW(m_preset_combo->gobj()), 5);
            m_preset_updating = false;
        }
        update_preview();
    };
    for (auto* cb : m_dow_checks)   cb->signal_toggled().connect(check_cb);
    for (auto* cb : m_month_checks) cb->signal_toggled().connect(check_cb);

    // Preset combo → populate all fields
    g_signal_connect(m_preset_combo->gobj(), "notify::selected",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<JobEditDialog*>(data);
            if (self->m_preset_updating) return;
            guint idx = adw::combo_row_get_selected(self->m_preset_combo);
            if (idx >= 5) return;
            self->m_preset_updating = true;
            adw::entry_row_set_text(self->m_minute_entry, k_presets[idx].minute);
            adw::entry_row_set_text(self->m_hour_entry,   k_presets[idx].hour);
            adw::entry_row_set_text(self->m_dom_entry,    k_presets[idx].dom);
            self->set_dow_from_cron(k_presets[idx].dow);
            self->set_month_from_cron(k_presets[idx].month);
            self->m_preset_updating = false;
            self->update_preview();
        }), this);

    // Initial preview
    update_preview();
}

rclone::Job JobEditDialog::get_cron_job() const {
    rclone::Job tmp;
    tmp.cron_minute  = adw::entry_row_get_text(m_minute_entry);
    tmp.cron_hour    = adw::entry_row_get_text(m_hour_entry);
    tmp.cron_day     = adw::entry_row_get_text(m_dom_entry);
    tmp.cron_month   = month_to_cron();
    tmp.cron_weekday = dow_to_cron();
    if (tmp.cron_minute.empty())  tmp.cron_minute  = "*";
    if (tmp.cron_hour.empty())    tmp.cron_hour    = "*";
    if (tmp.cron_day.empty())     tmp.cron_day     = "*";
    return tmp;
}

std::string JobEditDialog::dow_to_cron() const {
    int count = 0;
    std::string s;
    for (int i = 0; i < 7; ++i) {
        if (m_dow_checks[i]->get_active()) {
            ++count;
            if (!s.empty()) s += ',';
            s += std::to_string(i);
        }
    }
    return (count == 7) ? "*" : s;
}

std::string JobEditDialog::month_to_cron() const {
    int count = 0;
    std::string s;
    for (int i = 0; i < 12; ++i) {
        if (m_month_checks[i]->get_active()) {
            ++count;
            if (!s.empty()) s += ',';
            s += std::to_string(i + 1);
        }
    }
    return (count == 12) ? "*" : s;
}

void JobEditDialog::set_dow_from_cron(const std::string& s) {
    auto vals = cron::detail::expand_field(s, 0, 6);
    bool all = (vals.size() == 7) || s == "*";
    for (int i = 0; i < 7; ++i)
        m_dow_checks[i]->set_active(all ||
            std::ranges::find(vals, i) != vals.end());
}

void JobEditDialog::set_month_from_cron(const std::string& s) {
    auto vals = cron::detail::expand_field(s, 1, 12);
    bool all = (vals.size() == 12) || s == "*";
    for (int i = 0; i < 12; ++i)
        m_month_checks[i]->set_active(all ||
            std::ranges::find(vals, i + 1) != vals.end());
}

void JobEditDialog::update_preview() {
    if (!m_preview_calendar) return;
    rclone::Job tmp = get_cron_job();

    m_preview_cron_label->set_text(std::format("{} {} {} {} {}",
        tmp.cron_minute, tmp.cron_hour, tmp.cron_day,
        tmp.cron_month,  tmp.cron_weekday));
    m_preview_desc_label->set_text(cron::describe(tmp));

    refresh_calendar_marks();

    while (auto* child = m_upcoming_box->get_first_child())
        m_upcoming_box->remove(*child);

    GDateTime* cursor = g_date_time_new_now_local();
    for (int i = 0; i < 15; ++i) {
        GDateTime* nxt = cron::next_occurrence(tmp, cursor);
        g_date_time_unref(cursor);
        cursor = nxt;
        if (!cursor) break;
        gchar* fmt = g_date_time_format(cursor, "%Y-%m-%d %H:%M:%S");
        auto* lbl = Gtk::make_managed<Gtk::Label>(fmt);
        lbl->set_xalign(0.0f);
        lbl->add_css_class("monospace");
        m_upcoming_box->append(*lbl);
        g_free(fmt);
    }
    if (cursor) g_date_time_unref(cursor);
}

void JobEditDialog::refresh_calendar_marks() {
    m_preview_calendar->clear_marks();
    rclone::Job tmp = get_cron_job();
    auto date = m_preview_calendar->get_date();
    int y  = date.get_year();
    int mo = date.get_month();  // 1-based
    int ndays = cron::detail::days_in_month(y, mo);

    for (int d = 1; d <= ndays; ++d) {
        GDateTime* midnight = g_date_time_new_local(y, mo, d, 0, 0, 0);
        GDateTime* from = g_date_time_add_minutes(midnight, -1);
        g_date_time_unref(midnight);
        GDateTime* nxt = cron::next_occurrence(tmp, from);
        g_date_time_unref(from);
        if (nxt) {
            bool same = (g_date_time_get_year(nxt) == y &&
                         g_date_time_get_month(nxt) == mo &&
                         g_date_time_get_day_of_month(nxt) == d);
            g_date_time_unref(nxt);
            if (same) m_preview_calendar->mark_day(d);
        }
    }
}

rclone::Job JobEditDialog::build_job() const {
    rclone::Job job;
    job.id              = m_editing ? m_editing->id : generate_uuid();
    job.type            = index_to_job_type(adw::combo_row_get_selected(m_type_combo));
    job.source          = adw::entry_row_get_text(m_source_entry);
    job.destination     = adw::entry_row_get_text(m_dest_entry);
    job.dry_run         = adw::switch_row_get_active(m_dry_run_switch);
    job.bisync          = m_bisync_switch->get_visible()
                       && adw::switch_row_get_active(m_bisync_switch);
    job.bisync_force_deletes = job.bisync
                       && adw::switch_row_get_active(m_bisync_force_switch);
    job.ignore_checksum = !adw::switch_row_get_active(m_enable_checksum_switch);
    job.bandwidth       = adw::entry_row_get_text(m_bandwidth_entry);
    try { job.parallel_transfers = std::stoi(adw::entry_row_get_text(m_parallel_transfers_entry)); }
    catch (...) { job.parallel_transfers = -1; }
    try { job.retries = std::stoi(adw::entry_row_get_text(m_retries_entry)); }
    catch (...) { job.retries = -1; }
    job.extra_flags     = adw::entry_row_get_text(m_extra_flags_entry);
    {
        auto cron = get_cron_job();
        job.schedule_enabled = adw::switch_row_get_active(m_schedule_switch);
        job.cron_minute  = cron.cron_minute;
        job.cron_hour    = cron.cron_hour;
        job.cron_day     = cron.cron_day;
        job.cron_month   = cron.cron_month;
        job.cron_weekday = cron.cron_weekday;
    }
    job.mount_at_startup = m_mount_startup_switch->get_visible()
                        && adw::switch_row_get_active(m_mount_startup_switch);
    if (m_cache_mode_row->get_visible()) {
        guint idx = adw::combo_row_get_selected(m_cache_mode_row);
        const char* modes[] = {"off", "minimal", "writes", "full"};
        job.vfs_cache_mode = idx < 4 ? modes[idx] : "off";
    } else {
        job.vfs_cache_mode = "";
    }
    job.last_start  = m_editing ? m_editing->last_start  : "";
    job.last_run    = m_editing ? m_editing->last_run    : "";
    job.last_status = m_editing ? m_editing->last_status : "";
    {
        std::istringstream ss(adw::entry_row_get_text(m_includes_entry));
        std::string token;
        while (ss >> token) job.includes.push_back(token);
    }
    return job;
}

void JobEditDialog::on_commit() {
    auto job = build_job();
    if (job.source.empty() || job.destination.empty()) return;
    if (m_on_done) m_on_done(std::move(job));
    close();
}

void JobEditDialog::set_save_callback(DoneCallback cb) {
    m_on_save = std::move(cb);
}

void JobEditDialog::on_save() {
    auto job = build_job();
    if (job.source.empty() || job.destination.empty()) return;
    if (m_on_save) m_on_save(std::move(job));
    close();
}

std::string JobEditDialog::generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;

    auto r = [&]() { return dis(gen); };
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        r(), r() & 0xFFFF, (r() & 0x0FFF) | 0x4000,
        (r() & 0x3FFF) | 0x8000, r() & 0xFFFF, r());
    return buf;
}

} // namespace mtsync
