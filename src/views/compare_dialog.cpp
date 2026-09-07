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

#include "views/compare_dialog.hpp"
#include "widgets/adw_wrapper.hpp"
#include <adwaita.h>
#include <algorithm>
#include <format>
#include <glibmm/i18n.h>
#include <unordered_map>

namespace mtsync {

// Helper to extract string property from GObject for sorting
static Glib::ustring get_str_prop(GObject* obj, const char* prop) {
    gchar* val = nullptr;
    g_object_get(obj, prop, &val, nullptr);
    Glib::ustring result(val ? val : "");
    g_free(val);
    return result;
}

// Helper to extract int64 property from GObject for sorting
static gint64 get_int64_prop(GObject* obj, const char* prop) {
    gint64 val = 0;
    g_object_get(obj, prop, &val, nullptr);
    return val;
}

// Helper to extract boolean-like status from GObject
static bool is_dir_header(GObject* obj) {
    gchar* val = nullptr;
    g_object_get(obj, "status", &val, nullptr);
    bool is_dir = val && val[0] == '/';
    g_free(val);
    return is_dir;
}

// Return the canonical directory path this row belongs to.
// Used by column sorters to keep files grouped with their directory header.
static std::string item_dir_str(GObject* obj) {
    if (is_dir_header(obj)) {
        // Header src-name is "subdir/" or "/" — strip trailing slash
        gchar* val = nullptr;
        g_object_get(obj, "src-name", &val, nullptr);
        std::string s = val ? val : "";
        g_free(val);
        if (!s.empty() && s.back() == '/') s.pop_back();
        return s; // "subdir" or "" for root
    }
    // Regular file: extract directory part from path property
    gchar* val = nullptr;
    g_object_get(obj, "path", &val, nullptr);
    std::string path = val ? val : "";
    g_free(val);
    auto pos = path.rfind('/');
    return pos == std::string::npos ? "" : path.substr(0, pos);
}

// ── Static CSS (installed once per display) ───────────────────────────────

static void install_compare_css() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        "label.compare-same      { color: alpha(@window_fg_color, 0.45); }\n"
        "label.compare-missing   { color: #e05555; }\n"
        "label.compare-extra     { color: #4a90d9; }\n"
        "label.compare-different { color: #e6a817; }\n"
        "label.compare-error     { color: #e05555; font-weight: bold; }\n"
        "label.compare-status    { font-family: monospace; font-weight: bold; }\n"
        "label.compare-meta      { font-size: 0.82em; }\n"
        "label.compare-dir-header { font-weight: bold; color: @accent_color; }\n"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// ── Helpers ───────────────────────────────────────────────────────────────

const char* CompareDialog::status_css_class(char status) {
    switch (status) {
        case '=': return "compare-same";
        case '-': return "compare-missing";
        case '+': return "compare-extra";
        case '*': return "compare-different";
        case '!': return "compare-error";
        default:  return "compare-same";
    }
}

std::string CompareDialog::format_size(int64_t bytes) {
    if (bytes < 0)   return "";
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024 * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string CompareDialog::format_date(const std::string& iso) {
    // ISO 8601: "2024-04-08T12:34:56.789Z" → "2024-04-08 12:34"
    if (iso.size() < 16) return iso;
    std::string s = iso.substr(0, 16);
    if (s[10] == 'T') s[10] = ' ';
    return s;
}

// ── Constructor ───────────────────────────────────────────────────────────

CompareDialog::CompareDialog(const std::string& src,
                             const std::string& dst,
                             rclone::RcloneManager& manager)
    : m_src(src), m_dst(dst), m_manager(&manager) {
    set_title(_("Compare"));
    set_default_size(1100, 640);
    set_modal(true);
    set_destroy_with_parent(true);
    install_compare_css();
    m_alive = std::make_shared<bool>(true);
    m_page_store = Gio::ListStore<CompareRowObject>::create();
    setup_ui();
    start_load(manager);

    signal_close_request().connect([this]() -> bool {
        *m_alive = false;
        if (m_load_state) {
            m_load_state->cancelled = true;
            for (auto& proc : m_load_state->procs)
                if (proc) proc->force_exit();
        }
        return false;
    }, false);
}

// ── UI setup ─────────────────────────────────────────────────────────────

void CompareDialog::setup_ui() {
    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_child(*root);

    // Path label below the window's built-in title bar
    auto* path_label = Gtk::make_managed<Gtk::Label>(m_src + "  →  " + m_dst);
    path_label->add_css_class("dim-label");
    path_label->set_margin_top(6);
    path_label->set_margin_bottom(6);
    path_label->set_margin_start(12);
    path_label->set_margin_end(12);
    path_label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    root->append(*path_label);

    // Action bar
    auto* action_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    action_bar->set_margin_start(8);
    action_bar->set_margin_end(8);
    action_bar->set_margin_top(4);
    action_bar->set_margin_bottom(4);

    // Left group — source-side operations
    m_delete_btn = Gtk::make_managed<Gtk::Button>();
    m_delete_btn->set_icon_name("edit-delete-symbolic");
    m_delete_btn->add_css_class("destructive-action");
    m_delete_btn->set_sensitive(false);
    m_delete_btn->set_tooltip_text(_("Permanently delete the selected files from the source directory — this cannot be undone"));
    m_delete_btn->signal_clicked().connect([this]() { on_delete_clicked(); });

    m_copy_btn = Gtk::make_managed<Gtk::Button>(_("Copy →"));
    m_copy_btn->set_sensitive(false);
    m_copy_btn->set_tooltip_text(_("Copy the selected source-only files into the destination directory"));
    m_copy_btn->signal_clicked().connect([this]() { on_copy_clicked(); });

    action_bar->append(*m_delete_btn);
    action_bar->append(*m_copy_btn);

    // Left spacer
    auto* spacer_l = Gtk::make_managed<Gtk::Box>();
    spacer_l->set_hexpand(true);
    action_bar->append(*spacer_l);

    // Centre filter toggles — each shows rows of that status when active
    auto* filter_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    filter_box->set_halign(Gtk::Align::CENTER);

    m_filter_left_btn = Gtk::make_managed<Gtk::ToggleButton>("←");
    m_filter_left_btn->add_css_class("flat");
    m_filter_left_btn->set_tooltip_text(_("Toggle visibility of files that exist only in the source (left pane) and are absent from the destination"));
    m_filter_left_btn->set_active(true);
    m_filter_left_btn->signal_toggled().connect([this]() { apply_filters(); });

    m_filter_right_btn = Gtk::make_managed<Gtk::ToggleButton>("→");
    m_filter_right_btn->add_css_class("flat");
    m_filter_right_btn->set_tooltip_text(_("Toggle visibility of files that exist only in the destination (right pane) and are absent from the source"));
    m_filter_right_btn->set_active(true);
    m_filter_right_btn->signal_toggled().connect([this]() { apply_filters(); });

    m_filter_equal_btn = Gtk::make_managed<Gtk::ToggleButton>("=");
    m_filter_equal_btn->add_css_class("flat");
    m_filter_equal_btn->set_tooltip_text(_("Toggle visibility of files that are identical in both source and destination"));
    m_filter_equal_btn->set_active(true);
    m_filter_equal_btn->signal_toggled().connect([this]() { apply_filters(); });

    m_filter_diff_btn = Gtk::make_managed<Gtk::ToggleButton>("≠");
    m_filter_diff_btn->add_css_class("flat");
    m_filter_diff_btn->set_tooltip_text(_("Toggle visibility of files present in both directories whose content or size differs"));
    m_filter_diff_btn->set_active(true);
    m_filter_diff_btn->signal_toggled().connect([this]() { apply_filters(); });

    m_filter_error_btn = Gtk::make_managed<Gtk::ToggleButton>("!");
    m_filter_error_btn->add_css_class("flat");
    m_filter_error_btn->set_tooltip_text(_("Toggle visibility of files that could not be compared due to a read or permission error"));
    m_filter_error_btn->set_active(true);
    m_filter_error_btn->signal_toggled().connect([this]() { apply_filters(); });

    filter_box->append(*m_filter_left_btn);
    filter_box->append(*m_filter_right_btn);
    filter_box->append(*m_filter_equal_btn);
    filter_box->append(*m_filter_diff_btn);
    filter_box->append(*m_filter_error_btn);
    action_bar->append(*filter_box);

    // Right spacer
    auto* spacer_r = Gtk::make_managed<Gtk::Box>();
    spacer_r->set_hexpand(true);
    action_bar->append(*spacer_r);

    // Right group — destination-side operations
    m_dst_copy_btn = Gtk::make_managed<Gtk::Button>(_("← Copy"));
    m_dst_copy_btn->set_sensitive(false);
    m_dst_copy_btn->set_tooltip_text(_("Copy the selected destination-only files back into the source directory"));
    m_dst_copy_btn->signal_clicked().connect([this]() { on_dst_copy_clicked(); });

    m_dst_delete_btn = Gtk::make_managed<Gtk::Button>();
    m_dst_delete_btn->set_icon_name("edit-delete-symbolic");
    m_dst_delete_btn->add_css_class("destructive-action");
    m_dst_delete_btn->set_sensitive(false);
    m_dst_delete_btn->set_tooltip_text(_("Permanently delete the selected files from the destination directory — this cannot be undone"));
    m_dst_delete_btn->signal_clicked().connect([this]() { on_dst_delete_clicked(); });

    action_bar->append(*m_dst_copy_btn);
    action_bar->append(*m_dst_delete_btn);
    root->append(*action_bar);

    // Main stack: loading / results / error
    m_stack = Gtk::make_managed<Gtk::Stack>();
    m_stack->set_vexpand(true);
    m_stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);

    // ── "loading" page ───────────────────────────────────────────────────
    auto* loading_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    loading_box->set_halign(Gtk::Align::CENTER);
    loading_box->set_valign(Gtk::Align::CENTER);
    loading_box->set_vexpand(true);
    auto* spinner = adw::spinner();
    spinner->set_size_request(32, 32);
    loading_box->append(*spinner);
    auto* loading_lbl = Gtk::make_managed<Gtk::Label>(_("Comparing…"));
    loading_lbl->add_css_class("dim-label");
    loading_box->append(*loading_lbl);

    auto* hint_lbl = Gtk::make_managed<Gtk::Label>(_("Large scans can take a long time"));
    hint_lbl->add_css_class("dim-label");
    hint_lbl->set_margin_top(4);
    loading_box->append(*hint_lbl);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>(_("Cancel"));
    cancel_btn->set_halign(Gtk::Align::CENTER);
    cancel_btn->set_margin_top(12);
    cancel_btn->set_tooltip_text(_("Cancel the comparison scan and close this dialog"));
    cancel_btn->signal_clicked().connect([this]() {
        if (m_load_state) {
            m_load_state->cancelled = true;
            for (auto& proc : m_load_state->procs)
                if (proc) proc->force_exit();
        }
        close();
    });
    loading_box->append(*cancel_btn);

    m_stack->add(*loading_box, "loading");

    // ── "results" page ───────────────────────────────────────────────────
    auto* results_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    build_column_view();
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_child(*m_column_view);
    results_box->append(*scroll);

    // Pagination footer
    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    footer->set_margin_top(6);
    footer->set_margin_bottom(6);
    footer->set_margin_start(8);
    footer->set_margin_end(8);

    m_prev_btn = Gtk::make_managed<Gtk::Button>();
    m_prev_btn->set_icon_name("go-previous-symbolic");
    m_prev_btn->add_css_class("flat");
    m_prev_btn->set_sensitive(false);
    m_prev_btn->set_tooltip_text(_("Show the previous page of comparison results"));
    m_prev_btn->signal_clicked().connect([this]() { show_page(m_current_page - 1); });

    m_page_label = Gtk::make_managed<Gtk::Label>();
    m_page_label->set_hexpand(true);
    m_page_label->set_xalign(0.5f);
    m_page_label->add_css_class("dim-label");

    m_next_btn = Gtk::make_managed<Gtk::Button>();
    m_next_btn->set_icon_name("go-next-symbolic");
    m_next_btn->add_css_class("flat");
    m_next_btn->set_sensitive(false);
    m_next_btn->set_tooltip_text(_("Show the next page of comparison results"));
    m_next_btn->signal_clicked().connect([this]() { show_page(m_current_page + 1); });

    footer->append(*m_prev_btn);
    footer->append(*m_page_label);
    footer->append(*m_next_btn);
    results_box->append(*footer);

    m_stack->add(*results_box, "results");

    // ── "error" page ─────────────────────────────────────────────────────
    auto* error_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    error_box->set_halign(Gtk::Align::CENTER);
    error_box->set_valign(Gtk::Align::CENTER);
    error_box->set_vexpand(true);
    m_error_label = Gtk::make_managed<Gtk::Label>();
    m_error_label->add_css_class("error");
    m_error_label->set_wrap(true);
    m_error_label->set_max_width_chars(60);
    error_box->append(*m_error_label);
    m_stack->add(*error_box, "error");

    m_stack->set_visible_child("loading");
    root->append(*m_stack);
}

// ── ColumnView ────────────────────────────────────────────────────────────

void CompareDialog::build_column_view() {
    // Wrap page store with SortListModel to enable column sorting
    m_sort_model = Gtk::SortListModel::create(m_page_store, {});
    m_file_selection = Gtk::MultiSelection::create(m_sort_model);
    m_file_selection->signal_selection_changed().connect(
        [this](guint, guint) { update_action_buttons(); });
    m_column_view = Gtk::make_managed<Gtk::ColumnView>(m_file_selection);
    m_column_view->set_vexpand(true);
    m_column_view->add_css_class("data-table");

    // Helper: make a simple label column (string property, given xalign, optional fixed width)
    auto make_str_col = [this](const char* title, const char* prop, const char* sort_prop, float xalign, int fixed_w = 0,
                           bool ellipsize = false) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([xalign, ellipsize](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(xalign);
            if (ellipsize)
                lbl->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            if (xalign == 0.0f)
                lbl->set_hexpand(true);
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            bool is_hdr = !st.empty() && static_cast<char>(st[0]) == '/';
            is_hdr ? lbl->add_css_class("compare-dir-header")
                   : lbl->remove_css_class("compare-dir-header");
            gchar* val = nullptr;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(val ? val : "");
            g_free(val);
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        if (fixed_w > 0)
            col->set_fixed_width(fixed_w);
        else
            col->set_expand(true);
        // Add sorter: group by directory, header before its files, then by property
        col->set_sorter(adw::make_sorter([sort_prop](GObject* a, GObject* b) -> int {
            auto dir_a = item_dir_str(a), dir_b = item_dir_str(b);
            if (dir_a != dir_b) return dir_a < dir_b ? -1 : 1;
            bool da = is_dir_header(a), db = is_dir_header(b);
            if (da != db) return da ? -1 : 1;
            if (da && db) return 0;
            return get_str_prop(a, sort_prop).lowercase()
                       .compare(get_str_prop(b, sort_prop).lowercase());
        }));
        return col;
    };

    // Helper: size column (gint64 property, rendered as human-readable or "--")
    auto make_size_col = [this](const char* title, const char* prop, const char* sort_prop) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(1.0f);
            lbl->add_css_class("compare-meta");
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            if (!st.empty() && static_cast<char>(st[0]) == '/') { lbl->set_text(""); return; }
            gint64 val = 0;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(format_size(val));
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        col->set_fixed_width(90);
        // Add sorter: group by directory, header before its files, then by size
        col->set_sorter(adw::make_sorter([sort_prop](GObject* a, GObject* b) -> int {
            auto dir_a = item_dir_str(a), dir_b = item_dir_str(b);
            if (dir_a != dir_b) return dir_a < dir_b ? -1 : 1;
            bool da = is_dir_header(a), db = is_dir_header(b);
            if (da != db) return da ? -1 : 1;
            if (da && db) return 0;
            auto sa = get_int64_prop(a, sort_prop), sb = get_int64_prop(b, sort_prop);
            if (sa < 0 && sb >= 0) return -1;
            if (sa >= 0 && sb < 0) return 1;
            return sa < sb ? -1 : (sa > sb ? 1 : 0);
        }));
        return col;
    };

    // Helper: date column (string property, formatted)
    auto make_date_col = [this](const char* title, const char* prop, const char* sort_prop) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* lbl = Gtk::make_managed<Gtk::Label>();
            lbl->set_xalign(0.0f);
            lbl->add_css_class("compare-meta");
            item->set_child(*lbl);
        });
        factory->signal_bind().connect([prop](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
            auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
            if (!obj || !lbl) return;
            auto st = obj->property_status.get_value();
            if (!st.empty() && static_cast<char>(st[0]) == '/') { lbl->set_text(""); return; }
            gchar* val = nullptr;
            g_object_get(obj->gobj(), prop, &val, nullptr);
            lbl->set_text(format_date(val ? val : ""));
            g_free(val);
        });
        auto col = Gtk::ColumnViewColumn::create(title, factory);
        col->set_fixed_width(120);
        // Add sorter: group by directory, header before its files, then by date string
        col->set_sorter(adw::make_sorter([sort_prop](GObject* a, GObject* b) -> int {
            auto dir_a = item_dir_str(a), dir_b = item_dir_str(b);
            if (dir_a != dir_b) return dir_a < dir_b ? -1 : 1;
            bool da = is_dir_header(a), db = is_dir_header(b);
            if (da != db) return da ? -1 : 1;
            if (da && db) return 0;
            auto sa = get_str_prop(a, sort_prop), sb = get_str_prop(b, sort_prop);
            if (sa.empty() && !sb.empty()) return -1;
            if (!sa.empty() && sb.empty()) return 1;
            return sa.compare(sb);
        }));
        return col;
    };

    // Status column (centered, colored)
    static const char* k_status_classes[] = {
        "compare-same", "compare-missing", "compare-extra", "compare-different", "compare-error"
    };
    auto status_factory = Gtk::SignalListItemFactory::create();
    status_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_xalign(0.5f);
        lbl->add_css_class("compare-status");
        item->set_child(*lbl);
    });
    status_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<CompareRowObject>(item->get_item());
        auto* lbl = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!obj || !lbl) return;
        auto status_ustr = obj->property_status.get_value();
        char s = status_ustr.empty() ? '=' : static_cast<char>(status_ustr[0]);
        const char* new_cls = (s != '/') ? status_css_class(s) : nullptr;
        const char* old_cls = static_cast<const char*>(
            g_object_get_data(G_OBJECT(lbl->gobj()), "status-css"));
        if (old_cls != new_cls) {
            if (old_cls) lbl->remove_css_class(old_cls);
            if (new_cls) lbl->add_css_class(new_cls);
            g_object_set_data(G_OBJECT(lbl->gobj()), "status-css",
                              const_cast<char*>(new_cls));
        }
        if (s == '/') { lbl->set_text(""); return; }
        const char* sym;
        switch (s) {
            case '-': sym = "←"; break;
            case '+': sym = "→"; break;
            case '*': sym = "≠"; break;
            case '!': sym = "!"; break;
            default:  sym = "="; break;
        }
        lbl->set_text(sym);
    });
    auto status_col = Gtk::ColumnViewColumn::create("", status_factory);
    status_col->set_fixed_width(28);
    // Add sorter for status column: group by directory, header before its files, then by status
    status_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        auto dir_a = item_dir_str(a), dir_b = item_dir_str(b);
        if (dir_a != dir_b) return dir_a < dir_b ? -1 : 1;
        bool da = is_dir_header(a), db = is_dir_header(b);
        if (da != db) return da ? -1 : 1;
        if (da && db) return 0;
        return get_str_prop(a, "status").compare(get_str_prop(b, "status"));
    }));

    // Add all 7 columns with sort properties
    m_column_view->append_column(make_str_col(_("Filename"), "src-name", "src-name", 0.0f, 0, true));
    m_column_view->append_column(make_size_col(_("Size"),    "src-size", "src-size"));
    m_column_view->append_column(make_date_col(_("Modified"),"src-mod", "src-mod"));
    m_column_view->append_column(status_col);
    m_column_view->append_column(make_str_col(_("Filename"), "dst-name", "dst-name", 0.0f, 0, true));
    m_column_view->append_column(make_size_col(_("Size"),    "dst-size", "dst-size"));
    m_column_view->append_column(make_date_col(_("Modified"),"dst-mod", "dst-mod"));

    // Connect the ColumnView's sorter to the SortListModel to enable interactive sorting
    m_sort_model->set_sorter(m_column_view->get_sorter());
}

// ── Async loading ─────────────────────────────────────────────────────────

void CompareDialog::start_load(rclone::RcloneManager& manager) {
    m_load_state = std::make_shared<LoadState>();
    auto state = m_load_state;

    auto try_finish = [this, state]() {
        if (state->cancelled) return;
        state->done_count++;
        if (state->done_count < 3) return;
        if (!state->error.empty()) {
            m_error_label->set_text(std::string(_("Error: ")) + state->error);
            m_stack->set_visible_child("error");
            return;
        }
        merge_results(state->src_entries, state->dst_entries, state->check_entries);
        rebuild_filter_cache();
        show_page(0);
    };

    state->procs.push_back(
        manager.cli().lsjson_r(m_src, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->src_entries = std::move(*result);
            else if (state->error.empty()) state->error = result.error();
            try_finish();
        }));

    state->procs.push_back(
        manager.cli().lsjson_r(m_dst, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->dst_entries = std::move(*result);
            else if (state->error.empty()) state->error = result.error();
            try_finish();
        }));

    // check() never returns an error value — non-zero exit means diffs, not failure
    state->procs.push_back(
        manager.cli().check(m_src, m_dst, [state, try_finish](auto result) {
            if (state->cancelled) return;
            if (result) state->check_entries = std::move(*result);
            try_finish();
        }));
}

// ── Merge results ─────────────────────────────────────────────────────────

void CompareDialog::merge_results(const std::vector<rclone::FileEntry>& src_files,
                                   const std::vector<rclone::FileEntry>& dst_files,
                                   const std::vector<rclone::CheckEntry>& checks) {
    // Build path-keyed maps (files only, skip dirs)
    std::unordered_map<std::string, const rclone::FileEntry*> src_map, dst_map;
    for (auto& e : src_files) if (!e.is_dir) src_map[e.path] = &e;
    for (auto& e : dst_files) if (!e.is_dir) dst_map[e.path] = &e;

    auto basename = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return pos == std::string::npos ? p : p.substr(pos + 1);
    };
    auto dirpart = [](const std::string& p) -> std::string {
        auto pos = p.rfind('/');
        return pos == std::string::npos ? "" : p.substr(0, pos);
    };

    // Sort by directory first, then by path — raw path sort interleaves root-level
    // files (dir="") with subdirectory entries, causing duplicate "/" headers.
    std::vector<rclone::CheckEntry> sorted = checks;
    std::sort(sorted.begin(), sorted.end(),
              [&dirpart](const auto& a, const auto& b) {
                  auto da = dirpart(a.path), db = dirpart(b.path);
                  if (da != db) return da < db;
                  return a.path < b.path;
              });

    m_all_rows.clear();
    m_all_rows.reserve(sorted.size() + 32);

    std::string last_dir = "\x01"; // sentinel — never matches a real path
    for (auto& ce : sorted) {
        const rclone::FileEntry* src_fe = nullptr;
        const rclone::FileEntry* dst_fe = nullptr;
        if (auto it = src_map.find(ce.path); it != src_map.end()) src_fe = it->second;
        if (auto it = dst_map.find(ce.path); it != dst_map.end()) dst_fe = it->second;

        // Insert a directory header row when the directory changes
        std::string dir = dirpart(ce.path);
        if (dir != last_dir) {
            std::string header = dir.empty() ? "/" : dir + "/";
            m_all_rows.push_back(CompareRowObject::create('/', header, -1, "", "", -1, ""));
            last_dir = dir;
        }

        std::string bn = basename(ce.path);

        // Show name only on the side where the file exists
        // rclone: '+' = in source only, '-' = in dest only
        std::string src_name = (ce.status != '-') ? bn : "";
        std::string dst_name = (ce.status != '+') ? bn : "";
        int64_t  src_size = src_fe ? src_fe->size    : -1;
        int64_t  dst_size = dst_fe ? dst_fe->size    : -1;
        std::string src_mod = src_fe ? src_fe->mod_time : "";
        std::string dst_mod = dst_fe ? dst_fe->mod_time : "";

        m_all_rows.push_back(CompareRowObject::create(
            ce.status,
            src_name, src_size, src_mod,
            dst_name, dst_size, dst_mod,
            ce.path));
    }

    int file_count = 0;
    for (auto& r : m_all_rows)
        if (r->property_status.get_value() != "/") ++file_count;
    m_total_pages = file_count == 0 ? 1 : (file_count + PAGE_SIZE - 1) / PAGE_SIZE;
}

// ── Pagination ────────────────────────────────────────────────────────────

void CompareDialog::rebuild_filter_cache() {
    // Filter pass: keep headers (s == '/') and rows that pass the status filter
    std::vector<Glib::RefPtr<CompareRowObject>> visible;
    visible.reserve(m_all_rows.size());
    for (auto& row : m_all_rows) {
        auto st = row->property_status.get_value();
        char s = st.empty() ? '=' : static_cast<char>(st[0]);
        if (s != '/' && !is_status_filtered(s)) continue;
        visible.push_back(row);
    }

    // Strip orphan directory headers (headers with no file child before the next header)
    m_filtered_rows.clear();
    m_filtered_rows.reserve(visible.size());
    for (size_t i = 0; i < visible.size(); ++i) {
        auto st = visible[i]->property_status.get_value();
        char s = st.empty() ? '=' : static_cast<char>(st[0]);
        if (s == '/') {
            bool has_file = (i + 1 < visible.size()) && [&]() {
                auto ns = visible[i + 1]->property_status.get_value();
                return (ns.empty() ? '=' : static_cast<char>(ns[0])) != '/';
            }();
            if (has_file) m_filtered_rows.push_back(visible[i]);
        } else {
            m_filtered_rows.push_back(visible[i]);
        }
    }

    m_filtered_file_count = 0;
    for (auto& row : m_filtered_rows) {
        auto st = row->property_status.get_value();
        char s = st.empty() ? '=' : static_cast<char>(st[0]);
        if (s != '/') ++m_filtered_file_count;
    }
    m_total_pages = m_filtered_file_count == 0 ? 1
                  : (m_filtered_file_count + PAGE_SIZE - 1) / PAGE_SIZE;
}

void CompareDialog::show_page(int page) {
    m_current_page = page;
    m_page_store->remove_all();

    int start = page * PAGE_SIZE;
    int end   = std::min(start + PAGE_SIZE, static_cast<int>(m_filtered_rows.size()));
    for (int i = start; i < end; ++i)
        m_page_store->append(m_filtered_rows[i]);
    update_pagination_controls();
    m_stack->set_visible_child("results");
    update_action_buttons();
}

void CompareDialog::update_pagination_controls() {
    int page_no = m_current_page + 1;
    int files   = m_filtered_file_count;
    std::string files_str = std::vformat(ngettext("{} file", "{} files", files),
        std::make_format_args(files));
    m_page_label->set_text(std::vformat(_("Page {} of {}  ({})"),
        std::make_format_args(page_no, m_total_pages, files_str)));
    m_prev_btn->set_sensitive(m_current_page > 0);
    m_next_btn->set_sensitive(m_current_page < m_total_pages - 1);
}

// ── Filter helpers ────────────────────────────────────────────────────────

bool CompareDialog::is_status_filtered(char status) const {
    switch (status) {
        case '-': return m_filter_left_btn  && m_filter_left_btn->get_active();
        case '+': return m_filter_right_btn && m_filter_right_btn->get_active();
        case '=': return m_filter_equal_btn && m_filter_equal_btn->get_active();
        case '*': return m_filter_diff_btn  && m_filter_diff_btn->get_active();
        case '!': return m_filter_error_btn && m_filter_error_btn->get_active();
        default:  return false;
    }
}

void CompareDialog::apply_filters() {
    rebuild_filter_cache();
    show_page(0);
}

// ── Action bar helpers ────────────────────────────────────────────────────

std::vector<std::string> CompareDialog::get_selected_paths() const {
    std::vector<std::string> paths;
    if (!m_file_selection) return paths;
    auto n = m_file_selection->get_n_items();
    for (guint i = 0; i < n; ++i) {
        if (!m_file_selection->is_selected(i)) continue;
        auto obj = std::dynamic_pointer_cast<CompareRowObject>(m_file_selection->get_object(i));
        if (!obj) continue;
        auto st = obj->property_status.get_value();
        if (st.empty() || static_cast<char>(st[0]) == '/') continue; // skip headers
        auto path = std::string(obj->property_path.get_value());
        if (!path.empty())
            paths.push_back("/" + path); // leading "/" = absolute include pattern for rclone
    }
    return paths;
}

void CompareDialog::update_action_buttons() {
    bool in_results = m_stack && m_stack->get_visible_child_name() == "results";
    bool has_selection = in_results && !get_selected_paths().empty();
    if (m_delete_btn)     m_delete_btn->set_sensitive(has_selection);
    if (m_copy_btn)       m_copy_btn->set_sensitive(has_selection);
    if (m_dst_copy_btn)   m_dst_copy_btn->set_sensitive(has_selection);
    if (m_dst_delete_btn) m_dst_delete_btn->set_sensitive(has_selection);
}

void CompareDialog::on_delete_clicked() {
    auto paths = get_selected_paths();
    if (paths.empty() || !m_manager) return;
    m_stack->set_visible_child("loading");
    m_manager->cli().delete_files(m_src, paths,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (auto a = weak_alive.lock(); !a || !*a) return;
            if (!result)
                g_warning("Compare delete failed: %s", result.error().c_str());
            start_load(*m_manager);
        });
}

void CompareDialog::on_copy_clicked() {
    auto paths = get_selected_paths();
    if (paths.empty() || !m_manager) return;
    m_stack->set_visible_child("loading");
    m_manager->cli().copy_files(m_src, m_dst, paths,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (auto a = weak_alive.lock(); !a || !*a) return;
            if (!result)
                g_warning("Compare copy failed: %s", result.error().c_str());
            start_load(*m_manager);
        });
}

void CompareDialog::on_dst_copy_clicked() {
    auto paths = get_selected_paths();
    if (paths.empty() || !m_manager) return;
    m_stack->set_visible_child("loading");
    m_manager->cli().copy_files(m_dst, m_src, paths,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (auto a = weak_alive.lock(); !a || !*a) return;
            if (!result)
                g_warning("Compare dst-copy failed: %s", result.error().c_str());
            start_load(*m_manager);
        });
}

void CompareDialog::on_dst_delete_clicked() {
    auto paths = get_selected_paths();
    if (paths.empty() || !m_manager) return;
    m_stack->set_visible_child("loading");
    m_manager->cli().delete_files(m_dst, paths,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (auto a = weak_alive.lock(); !a || !*a) return;
            if (!result)
                g_warning("Compare dst-delete failed: %s", result.error().c_str());
            start_load(*m_manager);
        });
}

} // namespace mtsync
