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

#include "widgets/browser_pane.hpp"
#include "sandbox.hpp"
#include <adwaita.h>
#include <format>
#include <unordered_map>

namespace mtsync {

// GObject property helpers used inside C comparison callbacks
static Glib::ustring get_str_prop(GObject* obj, const char* prop) {
    gchar* s = nullptr;
    g_object_get(obj, prop, &s, nullptr);
    Glib::ustring r = s ? s : "";
    g_free(s);
    return r;
}

static gint64 get_int64_prop(GObject* obj, const char* prop) {
    gint64 v = 0;
    g_object_get(obj, prop, &v, nullptr);
    return v;
}

static bool get_bool_prop(GObject* obj, const char* prop) {
    gboolean v = FALSE;
    g_object_get(obj, prop, &v, nullptr);
    return static_cast<bool>(v);
}

static void set_remote_icon(Gtk::Image* img, const std::string& type) {
    bool dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    std::string base = "/io/github/mtsync/provider-icons/" + type;
    auto try_res = [&](const std::string& path) -> bool {
        GBytes* b = g_resources_lookup_data(path.c_str(),
                        G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
        if (b) { g_bytes_unref(b); gtk_image_set_from_resource(GTK_IMAGE(img->gobj()), path.c_str()); return true; }
        return false;
    };
    if (dark && try_res(base + "-dark.svg")) return;
    if (try_res(base + ".svg")) return;
    // symbolic fallback
    if (type == "local")   { img->set_from_icon_name("drive-harddisk-symbolic");   return; }
    if (type == "crypt")   { img->set_from_icon_name("channel-secure-symbolic");    return; }
    if (type == "sftp" || type == "ftp" || type == "ftps")
                           { img->set_from_icon_name("utilities-terminal-symbolic"); return; }
    if (type == "smb")     { img->set_from_icon_name("folder-remote-symbolic");     return; }
    img->set_from_icon_name("network-server-symbolic");
}

static void install_mime_icon_css() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        "image.icon-folder       { color: #f5a623; }\n"
        "image.icon-image        { color: #4a90d9; }\n"
        "image.icon-video        { color: #9b59b6; }\n"
        "image.icon-audio        { color: #1db954; }\n"
        "image.icon-archive      { color: #e67e22; }\n"
        "image.icon-document     { color: #e74c3c; }\n"
        "image.icon-spreadsheet  { color: #27ae60; }\n"
        "image.icon-presentation { color: #e74c3c; }\n"
        "image.icon-code         { color: #00b894; }\n"
        "image.icon-font         { color: #8e44ad; }\n"
        "image.icon-executable   { color: #d63031; }\n"
        "image.icon-text         { color: #a0a8b0; }\n"
        ".breadcrumb-bar > button { padding-left: 4px; padding-right: 4px; min-height: 0; }\n"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

BrowserPane::BrowserPane(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {
    set_vexpand(true);
    set_hexpand(true);
    install_mime_icon_css();

    m_list_store        = Gio::ListStore<FileObject>::create();
    m_remote_string_list = Gtk::StringList::create({});

    setup_header();

    // Content stack
    m_content_stack = Gtk::make_managed<Gtk::Stack>();
    m_content_stack->set_vexpand(true);
    m_content_stack->set_hexpand(true);
    m_content_stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);

    m_no_remote_status = adw::status_page();
    adw::status_page_set_icon_name(m_no_remote_status, "network-server-symbolic");
    adw::status_page_set_title(m_no_remote_status, "No Remote Selected");
    adw::status_page_set_description(m_no_remote_status, "Choose a remote from the dropdown above.");
    m_content_stack->add(*m_no_remote_status, "no-remote");

    auto* loading_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    loading_box->set_halign(Gtk::Align::CENTER);
    loading_box->set_valign(Gtk::Align::CENTER);
    loading_box->set_vexpand(true);
    auto* spinner_widget = adw::spinner();
    spinner_widget->set_size_request(32, 32);
    loading_box->append(*spinner_widget);
    m_content_stack->add(*loading_box, "loading");

    m_empty_status = adw::status_page();
    adw::status_page_set_icon_name(m_empty_status, "folder-open-symbolic");
    adw::status_page_set_title(m_empty_status, "Empty Folder");
    adw::status_page_set_description(m_empty_status, "This directory contains no files.");
    m_content_stack->add(*m_empty_status, "empty");

    m_error_status = adw::status_page();
    adw::status_page_set_icon_name(m_error_status, "network-error-symbolic");
    adw::status_page_set_title(m_error_status, "Unable to Connect");
    adw::status_page_set_description(m_error_status, "The remote did not respond. Check the connection and try again.");
    m_content_stack->add(*m_error_status, "error");

    build_column_view();
    auto* file_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    file_scroll->set_vexpand(true);
    file_scroll->set_child(*m_column_view);
    m_content_stack->add(*file_scroll, "files");

    append(*m_content_stack);

    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    footer->set_margin_start(6);
    footer->set_margin_end(6);
    footer->set_margin_top(2);
    footer->set_margin_bottom(2);
    m_status_label = Gtk::make_managed<Gtk::Label>();
    m_status_label->set_xalign(0.0f);
    m_status_label->add_css_class("dim-label");
    m_status_label->set_margin_start(4);
    footer->append(*m_status_label);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    footer->append(*spacer);
    m_show_hidden_check = Gtk::make_managed<Gtk::CheckButton>("Show hidden files");
    m_show_hidden_check->set_active(false);
    m_show_hidden_check->signal_toggled().connect([this]() {
        m_show_hidden = m_show_hidden_check->get_active();
        if (!m_current_remote.empty())
            navigate(m_current_path);
    });
    footer->append(*m_show_hidden_check);
    append(*footer);

    show_content_state("no-remote");

    signal_map().connect([this]() {
        if (!m_initialized) {
            m_initialized = true;
            load_remotes();
        }
    });

    // Trigger factory rebind (icon colour swap) when theme switches
    m_dark_signal_id = g_signal_connect(
        adw_style_manager_get_default(), "notify::dark",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<BrowserPane*>(data);
            guint n = self->m_remote_string_list->get_n_items();
            if (n > 0)
                g_list_model_items_changed(
                    G_LIST_MODEL(self->m_remote_string_list->gobj()), 0, n, n);
        }), this);

    signal_unrealize().connect([this]() {
        if (m_dark_signal_id) {
            g_signal_handler_disconnect(adw_style_manager_get_default(), m_dark_signal_id);
            m_dark_signal_id = 0;
        }
    });
}

void BrowserPane::setup_header() {
    auto* nav_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    nav_bar->add_css_class("toolbar");
    nav_bar->set_margin_start(3);
    nav_bar->set_margin_end(3);
    nav_bar->set_margin_top(2);
    nav_bar->set_margin_bottom(2);

    m_remote_dropdown = Gtk::make_managed<Gtk::DropDown>();
    m_remote_dropdown->set_model(m_remote_string_list);
    m_remote_dropdown->set_size_request(150, -1);

    // Factory: render each remote entry as icon + name
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_pixel_size(16);
        auto* lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*img);
        box->append(*lbl);
        item->set_child(*box);
    });
    factory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto* img = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* lbl = dynamic_cast<Gtk::Label*>(img ? img->get_next_sibling() : nullptr);
        auto pos = item->get_position();
        std::string name, type;
        if (pos == 0) {
            name = "Local"; type = "local";
        } else if (pos - 1 < m_remotes.size()) {
            name = m_remotes[pos - 1].name;
            type = m_remotes[pos - 1].type;
        }
        if (lbl) lbl->set_text(Glib::ustring(name));
        if (img) set_remote_icon(img, type);
    });
    m_remote_dropdown->set_factory(factory);
    nav_bar->append(*m_remote_dropdown);


    m_back_btn.set_icon_name("go-previous-symbolic");
    m_back_btn.set_tooltip_text("Navigate back to the previous directory in history");
    m_back_btn.add_css_class("flat");
    m_back_btn.add_css_class("circular");
    m_back_btn.signal_clicked().connect([this]() { go_back(); });
    nav_bar->append(m_back_btn);

    m_up_btn.set_icon_name("go-up-symbolic");
    m_up_btn.set_tooltip_text("Navigate up to the parent directory");
    m_up_btn.add_css_class("flat");
    m_up_btn.add_css_class("circular");
    m_up_btn.signal_clicked().connect([this]() { go_up(); });
    nav_bar->append(m_up_btn);

    m_breadcrumb_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    m_breadcrumb_box->add_css_class("breadcrumb-bar");

    m_breadcrumb_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_breadcrumb_scroll->set_hexpand(true);
    m_breadcrumb_scroll->set_policy(Gtk::PolicyType::EXTERNAL, Gtk::PolicyType::NEVER);
    m_breadcrumb_scroll->set_child(*m_breadcrumb_box);
    nav_bar->append(*m_breadcrumb_scroll);

    m_refresh_btn.set_icon_name("view-refresh-symbolic");
    m_refresh_btn.set_tooltip_text("Reload the current directory listing from the remote");
    m_refresh_btn.add_css_class("flat");
    m_refresh_btn.signal_clicked().connect([this]() {
        signal_focused.emit();
        navigate(m_current_path);
    });
    nav_bar->append(m_refresh_btn);


    m_dropdown_conn = m_remote_dropdown->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &BrowserPane::on_remote_selection_changed));

    append(*nav_bar);
}

void BrowserPane::build_column_view() {
    m_sort_model     = Gtk::SortListModel::create(m_list_store, {});
    m_file_selection = Gtk::MultiSelection::create(m_sort_model);

    m_column_view = Gtk::make_managed<Gtk::ColumnView>(m_file_selection);
    m_column_view->set_vexpand(true);
    m_column_view->add_css_class("data-table");

    // --- Name column ---
    auto name_factory = Gtk::SignalListItemFactory::create();
    name_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box   = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* icon  = Gtk::make_managed<Gtk::Image>();
        icon->set_pixel_size(16);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        box->append(*icon);
        box->append(*label);
        item->set_child(*box);
        g_object_set_data(G_OBJECT(item->gobj()), "bp-icon",  icon);
        g_object_set_data(G_OBJECT(item->gobj()), "bp-label", label);
    });
    name_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* icon  = static_cast<Gtk::Image*>(g_object_get_data(G_OBJECT(item->gobj()), "bp-icon"));
        auto* label = static_cast<Gtk::Label*>(g_object_get_data(G_OBJECT(item->gobj()), "bp-label"));
        if (icon) {
            std::string mime = std::string(obj->property_mime_type.get_value());
            bool is_dir = obj->property_is_dir.get_value();
            icon->set_from_icon_name(mime_to_icon(mime, is_dir));
            const char* new_cls = mime_to_css_class(mime, is_dir);
            const char* old_cls = static_cast<const char*>(
                g_object_get_data(G_OBJECT(icon->gobj()), "bp-css"));
            if (old_cls != new_cls) {
                if (old_cls) icon->remove_css_class(old_cls);
                icon->add_css_class(new_cls);
                g_object_set_data(G_OBJECT(icon->gobj()), "bp-css",
                                  const_cast<char*>(new_cls));
            }
        }
        if (label) label->set_text(obj->property_name.get_value());
    });
    auto name_col = Gtk::ColumnViewColumn::create("Name", name_factory);
    name_col->set_expand(true);
    name_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        auto na = get_str_prop(a, "name").lowercase();
        auto nb = get_str_prop(b, "name").lowercase();
        return na < nb ? -1 : (na > nb ? 1 : 0);
    }));
    m_column_view->append_column(name_col);

    // --- Size column ---
    auto size_factory = Gtk::SignalListItemFactory::create();
    size_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(1.0f);
        item->set_child(*label);
    });
    size_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        label->set_text(obj->property_is_dir.get_value()
            ? "--" : format_size(obj->property_size.get_value()));
    });
    auto size_col = Gtk::ColumnViewColumn::create("Size", size_factory);
    size_col->set_fixed_width(100);
    size_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        auto sa = get_int64_prop(a, "size");
        auto sb = get_int64_prop(b, "size");
        return sa < sb ? -1 : (sa > sb ? 1 : 0);
    }));
    m_column_view->append_column(size_col);

    // --- Modified column ---
    auto mod_factory = Gtk::SignalListItemFactory::create();
    mod_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(0.0f);
        item->set_child(*label);
    });
    mod_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto obj = std::dynamic_pointer_cast<FileObject>(item->get_item());
        if (!obj) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!label) return;
        auto mod = std::string(obj->property_mod_time.get_value());
        if (mod.size() >= 10) mod = mod.substr(0, 10);
        label->set_text(mod);
    });
    auto mod_col = Gtk::ColumnViewColumn::create("Modified", mod_factory);
    mod_col->set_fixed_width(120);
    mod_col->set_sorter(adw::make_sorter([](GObject* a, GObject* b) -> int {
        return get_str_prop(a, "mod-time").compare(get_str_prop(b, "mod-time"));
    }));
    m_column_view->append_column(mod_col);

    m_column_view->signal_activate().connect(
        sigc::mem_fun(*this, &BrowserPane::on_item_activated));

    // Any click in the column view marks this pane as active
    auto gesture = Gtk::GestureClick::create();
    gesture->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    gesture->signal_pressed().connect([this](int, double, double) {
        signal_focused.emit();
    });
    m_column_view->add_controller(gesture);

    // Dirs-first primary sort + user column sort via MultiSorter
    auto dirs_first = adw::make_sorter([](GObject* a, GObject* b) -> int {
        bool da = get_bool_prop(a, "is-dir");
        bool db = get_bool_prop(b, "is-dir");
        if (da == db) return 0;
        return da ? -1 : 1;
    });

    auto multi = Gtk::MultiSorter::create();
    multi->append(dirs_first);
    multi->append(m_column_view->get_sorter());
    m_sort_model->set_sorter(multi);
}

void BrowserPane::load_remotes() {
    // Block signal while rebuilding to avoid double-trigger
    m_dropdown_conn.block();

    while (m_remote_string_list->get_n_items() > 0)
        m_remote_string_list->remove(m_remote_string_list->get_n_items() - 1);
    m_remotes.clear();
    m_remote_string_list->append("Local");
    m_remote_dropdown->set_selected(0);

    m_dropdown_conn.unblock();
    on_remote_selection_changed(); // navigate to Local home

    m_manager.cli().list_remotes([this](auto result) {
        if (!result.has_value()) return;
        m_remotes = std::move(result.value());
        for (auto& r : m_remotes)
            m_remote_string_list->append(Glib::ustring(r.name));
    });
}

void BrowserPane::on_remote_selection_changed() {
    auto idx = m_remote_dropdown->get_selected();
    if (idx == GTK_INVALID_LIST_POSITION) {
        m_current_remote.clear();
        m_is_local = false;
        show_content_state("no-remote");
        return;
    }
    signal_focused.emit();
    if (idx == 0) {
        m_current_remote = "Local";
        m_is_local       = true;
        m_current_path   = sandbox::real_home();
    } else {
        auto remote_idx = static_cast<size_t>(idx - 1);
        if (remote_idx >= m_remotes.size()) return; // list still loading
        m_current_remote = m_remotes[remote_idx].name;
        m_is_local       = false;
        m_current_path   = "";
    }
    m_path_history.clear();
    navigate(m_current_path);
}

void BrowserPane::navigate(const std::string& path) {
    if (m_current_remote.empty()) return;
    m_current_path = path;
    uint64_t gen   = ++m_load_generation;

    if (m_status_label) m_status_label->set_text("");
    show_content_state("loading");
    rebuild_breadcrumbs();

    std::string rclone_path = m_is_local ? path : (m_current_remote + ":" + path);
    m_manager.cli().lsjson(rclone_path, [this, gen](auto result) {
        if (gen != m_load_generation) return;
        if (!result.has_value()) {
            if (m_status_label) m_status_label->set_text("");
            adw::status_page_set_description(m_error_status, result.error().c_str());
            show_content_state("error");
            return;
        }
        m_list_store->remove_all();
        int file_count = 0, folder_count = 0;
        int64_t total_size = 0;
        for (auto& e : result.value()) {
            if (!m_show_hidden && !e.name.empty() && e.name[0] == '.')
                continue;
            m_list_store->append(FileObject::create(e));
            if (e.is_dir) { ++folder_count; }
            else          { ++file_count; total_size += e.size; }
        }
        if (m_status_label) {
            auto size_str = format_size(total_size);
            m_status_label->set_text(std::format(
                "{} file{}, {} folder{}, Total: {}",
                file_count,   file_count   == 1 ? "" : "s",
                folder_count, folder_count == 1 ? "" : "s",
                size_str));
        }
        show_content_state(result.value().empty() ? "empty" : "files");
    });
}

void BrowserPane::rebuild_breadcrumbs() {
    while (auto* child = m_breadcrumb_box->get_first_child())
        m_breadcrumb_box->remove(*child);

    Glib::ustring root_label = m_is_local ? "Local"
        : (m_current_remote.empty() ? Glib::ustring("Remote") : Glib::ustring(m_current_remote));
    std::string root_path = m_is_local ? "/" : "";

    auto* root_btn = Gtk::make_managed<Gtk::Button>(root_label);
    root_btn->add_css_class("flat");
    root_btn->signal_clicked().connect([this, root_path]() {
        m_path_history.push_back(m_current_path);
        navigate(root_path);
    });
    m_breadcrumb_box->append(*root_btn);

    std::string path_to_split = m_current_path;
    if (m_is_local && !path_to_split.empty() && path_to_split[0] == '/')
        path_to_split = path_to_split.substr(1);

    bool at_root = m_is_local ? (m_current_path == "/" || m_current_path.empty())
                              : m_current_path.empty();
    if (at_root) return;

    std::vector<std::string> segments;
    std::string seg;
    for (char c : path_to_split) {
        if (c == '/') {
            if (!seg.empty()) { segments.push_back(seg); seg.clear(); }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) segments.push_back(seg);

    std::string accumulated;
    for (size_t i = 0; i < segments.size(); ++i) {
        auto* separator = Gtk::make_managed<Gtk::Label>("›");
        separator->add_css_class("dim-label");
        m_breadcrumb_box->append(*separator);

        accumulated = m_is_local ? (accumulated + "/" + segments[i])
                                 : (accumulated.empty() ? segments[i] : accumulated + "/" + segments[i]);

        auto* btn = Gtk::make_managed<Gtk::Button>(Glib::ustring(segments[i]));
        btn->add_css_class("flat");
        if (i == segments.size() - 1) {
            btn->set_sensitive(false);
        } else {
            std::string target = accumulated;
            btn->signal_clicked().connect([this, target]() {
                m_path_history.push_back(m_current_path);
                navigate(target);
            });
        }
        m_breadcrumb_box->append(*btn);
    }

    // Scroll to the end so the deepest segment is always visible
    if (m_breadcrumb_scroll) {
        auto adj = m_breadcrumb_scroll->get_hadjustment();
        if (adj) adj->set_value(adj->get_upper() - adj->get_page_size());
    }
}

void BrowserPane::on_item_activated(guint position) {
    signal_focused.emit();
    auto obj = std::dynamic_pointer_cast<FileObject>(m_sort_model->get_object(position));
    if (!obj || !obj->property_is_dir.get_value()) return;
    m_path_history.push_back(m_current_path);
    std::string new_path;
    if (m_is_local) {
        new_path = (m_current_path == "/")
            ? "/" + std::string(obj->property_name.get_value())
            : m_current_path + "/" + std::string(obj->property_name.get_value());
    } else {
        new_path = m_current_path.empty()
            ? std::string(obj->property_path.get_value())
            : m_current_path + "/" + std::string(obj->property_name.get_value());
    }
    navigate(new_path);
}

void BrowserPane::go_back() {
    if (m_path_history.empty()) return;
    signal_focused.emit();
    auto prev = m_path_history.back();
    m_path_history.pop_back();
    navigate(prev);
}

void BrowserPane::go_up() {
    signal_focused.emit();
    if (m_is_local) {
        if (m_current_path == "/" || m_current_path.empty()) return;
        m_path_history.push_back(m_current_path);
        auto pos = m_current_path.rfind('/');
        std::string parent = (pos == 0) ? "/" : m_current_path.substr(0, pos);
        navigate(parent);
    } else {
        if (m_current_path.empty()) return;
        m_path_history.push_back(m_current_path);
        auto pos = m_current_path.rfind('/');
        navigate(pos == std::string::npos ? "" : m_current_path.substr(0, pos));
    }
}

void BrowserPane::swap_location_with(BrowserPane& other) {
    // Block both dropdown signals to prevent on_remote_selection_changed
    // from overwriting state while we're setting it
    m_dropdown_conn.block();
    other.m_dropdown_conn.block();

    std::swap(m_current_remote, other.m_current_remote);
    std::swap(m_current_path,   other.m_current_path);
    std::swap(m_is_local,       other.m_is_local);
    std::swap(m_path_history,   other.m_path_history);

    auto find_index = [](const BrowserPane& p) -> guint {
        if (p.m_is_local) return 0;
        for (size_t i = 0; i < p.m_remotes.size(); ++i)
            if (p.m_remotes[i].name == p.m_current_remote)
                return static_cast<guint>(i + 1);
        return GTK_INVALID_LIST_POSITION;
    };

    m_remote_dropdown->set_selected(find_index(*this));
    other.m_remote_dropdown->set_selected(find_index(other));

    m_dropdown_conn.unblock();
    other.m_dropdown_conn.unblock();

    navigate(m_current_path);
    other.navigate(other.m_current_path);
}

void BrowserPane::show_content_state(const std::string& name) {
    m_content_stack->set_visible_child(name);
    bool active  = (name == "files" || name == "empty");
    bool at_root = m_is_local ? (m_current_path == "/" || m_current_path.empty())
                              : m_current_path.empty();
    m_back_btn.set_sensitive(!m_path_history.empty());
    m_up_btn.set_sensitive(!at_root && active);
    m_refresh_btn.set_sensitive(!m_current_remote.empty() && name != "no-remote");
}

std::string BrowserPane::get_current_rclone_path() const {
    if (m_current_remote.empty()) return "";
    return m_is_local ? m_current_path : (m_current_remote + ":" + m_current_path);
}

std::vector<Glib::RefPtr<FileObject>> BrowserPane::get_selected_files() const {
    std::vector<Glib::RefPtr<FileObject>> result;
    auto n = m_file_selection->get_n_items();
    for (guint i = 0; i < n; ++i) {
        if (m_file_selection->is_selected(i)) {
            auto obj = std::dynamic_pointer_cast<FileObject>(m_sort_model->get_object(i));
            if (obj) result.push_back(std::move(obj));
        }
    }
    return result;
}

void BrowserPane::refresh() {
    signal_focused.emit();
    navigate(m_current_path);
}

void BrowserPane::set_active(bool active) {
    if (active)
        add_css_class("browser-pane-active");
    else
        remove_css_class("browser-pane-active");
}


// static
std::string BrowserPane::format_size(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.1f} GB", bytes / (1024.0 * 1024 * 1024));
}

// static
const char* BrowserPane::mime_to_css_class(const std::string& mime, bool is_dir) {
    if (is_dir) return "icon-folder";
    if (mime.empty()) return "icon-text";

    if (mime == "application/pdf")               return "icon-document";
    if (mime == "application/x-executable")      return "icon-executable";
    if (mime == "application/json" ||
        mime == "application/xml")               return "icon-code";

    // Archives
    for (const auto* s : {"zip","tar","gzip","bzip","x-xz","7z","rar"})
        if (mime.find(s) != std::string::npos)   return "icon-archive";

    // Office
    if (mime.find("spreadsheet") != std::string::npos ||
        mime.find("excel") != std::string::npos) return "icon-spreadsheet";
    if (mime.find("presentation") != std::string::npos ||
        mime.find("powerpoint") != std::string::npos) return "icon-presentation";
    if (mime.find("word") != std::string::npos ||
        mime.find("document") != std::string::npos) return "icon-document";

    auto slash = mime.find('/');
    std::string cat = (slash != std::string::npos) ? mime.substr(0, slash) : mime;
    std::string sub = (slash != std::string::npos) ? mime.substr(slash + 1) : "";

    if (cat == "image") return "icon-image";
    if (cat == "video") return "icon-video";
    if (cat == "audio") return "icon-audio";
    if (cat == "font")  return "icon-font";
    if (cat == "text") {
        if (sub == "csv")                        return "icon-spreadsheet";
        if (sub == "html" || sub == "xml")       return "icon-code";
        if (sub.find("script") != std::string::npos ||
            sub == "javascript" || sub == "x-python" ||
            sub == "x-go"       || sub == "x-rust"   ||
            sub == "x-csrc"     || sub == "x-c++src" ||
            sub == "x-java")                     return "icon-code";
    }
    return "icon-text";
}

// static
std::string BrowserPane::mime_to_icon(const std::string& mime, bool is_dir) {
    if (is_dir) return "folder-symbolic";
    if (mime.empty()) return "text-x-generic-symbolic";

    static const std::unordered_map<std::string, std::string> exact = {
        {"application/pdf",               "x-office-document-symbolic"},
        {"application/zip",               "package-x-generic-symbolic"},
        {"application/x-tar",             "package-x-generic-symbolic"},
        {"application/gzip",              "package-x-generic-symbolic"},
        {"application/x-bzip2",           "package-x-generic-symbolic"},
        {"application/x-xz",              "package-x-generic-symbolic"},
        {"application/x-7z-compressed",   "package-x-generic-symbolic"},
        {"application/x-rar-compressed",  "package-x-generic-symbolic"},
        {"application/vnd.ms-excel",      "x-office-spreadsheet-symbolic"},
        {"application/msword",            "x-office-document-symbolic"},
        {"application/vnd.ms-powerpoint", "x-office-presentation-symbolic"},
        {"application/x-executable",      "application-x-executable-symbolic"},
        {"application/json",              "text-x-script-symbolic"},
        {"application/xml",               "text-xml-symbolic"},
        {"image/svg+xml",                 "image-x-generic-symbolic"},
    };
    if (auto it = exact.find(mime); it != exact.end()) return it->second;

    auto slash = mime.find('/');
    std::string cat = (slash != std::string::npos) ? mime.substr(0, slash) : mime;
    std::string sub = (slash != std::string::npos) ? mime.substr(slash + 1) : "";

    if (cat == "image") return "image-x-generic-symbolic";
    if (cat == "video") return "video-x-generic-symbolic";
    if (cat == "audio") return "audio-x-generic-symbolic";
    if (cat == "font")  return "font-x-generic-symbolic";
    if (cat == "text") {
        if (sub == "csv") return "x-office-spreadsheet-symbolic";
        if (sub == "html" || sub == "xml") return "text-xml-symbolic";
        if (sub.find("script") != std::string::npos || sub == "javascript" ||
            sub == "x-python" || sub == "x-go" || sub == "x-rust" ||
            sub == "x-csrc" || sub == "x-c++src" || sub == "x-java")
            return "text-x-script-symbolic";
        return "text-x-generic-symbolic";
    }
    return "text-x-generic-symbolic";
}

} // namespace mtsync
