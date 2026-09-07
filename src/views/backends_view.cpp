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

#include "views/backends_view.hpp"
#include "views/backend_edit_view.hpp"
#include "widgets/adw_wrapper.hpp"
#include <glibmm/i18n.h>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace mtsync {

namespace {

static const char* remote_type_symbolic(const std::string& type) {
    if (type == "crypt")   return "channel-secure-symbolic";
    if (type == "local")   return "drive-harddisk-symbolic";
    if (type == "alias"   ||
        type == "union"   ||
        type == "chunker" ||
        type == "combine" ||
        type == "cache"   ||
        type == "compress") return "folder-symbolic";
    if (type == "sftp" ||
        type == "ftp"  ||
        type == "ftps") return "utilities-terminal-symbolic";
    if (type == "smb") return "folder-remote-symbolic";
    return "network-server-symbolic";
}

static bool resource_exists(const std::string& path) {
    GBytes* b = g_resources_lookup_data(path.c_str(),
                    G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (b) { g_bytes_unref(b); return true; }
    return false;
}

// Maps rclone type strings to icon resource names where they differ
static const std::string& icon_name_for_type(const std::string& type) {
    static const std::unordered_map<std::string, std::string> overrides = {
        {"googlephotos", "gphotos"},
    };
    auto it = overrides.find(type);
    return it != overrides.end() ? it->second : type;
}

static Gtk::Image* make_remote_icon(const std::string& type) {
    bool dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    std::string base = "/io/github/mtsync/provider-icons/" + icon_name_for_type(type);
    std::string res;
    if (dark && resource_exists(base + "-dark.svg"))
        res = base + "-dark.svg";
    else if (resource_exists(base + ".svg"))
        res = base + ".svg";

    auto* img = Gtk::make_managed<Gtk::Image>();
    if (!res.empty())
        gtk_image_set_from_resource(GTK_IMAGE(img->gobj()), res.c_str());
    else
        img->set_from_icon_name(remote_type_symbolic(type));
    img->set_pixel_size(20);
    img->set_valign(Gtk::Align::CENTER);
    return img;
}

static std::string format_bytes(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit_idx = 0;
    double value = static_cast<double>(bytes);
    while (value >= 1024.0 && unit_idx < 5) {
        value /= 1024.0;
        ++unit_idx;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(value >= 100 ? 0 : (value >= 10 ? 1 : 1))
        << value << " " << units[unit_idx];
    return oss.str();
}

static std::string format_capacity(const rclone::AboutInfo& about) {
    // Show "X used of Y" or "X free" or similar, depending on what's available
    if (about.used && about.total) {
        return format_bytes(*about.used) + " " + _("of") + " " + format_bytes(*about.total);
    }
    if (about.free && about.total) {
        return format_bytes(*about.free) + " " + _("free of") + " " + format_bytes(*about.total);
    }
    if (about.used) {
        return format_bytes(*about.used) + " " + _("used");
    }
    if (about.free) {
        return format_bytes(*about.free) + " " + _("free");
    }
    if (about.total) {
        return format_bytes(*about.total) + " " + _("total");
    }
    return "";
}

static std::optional<double> compute_usage_percent(const rclone::AboutInfo& about) {
    if (about.used && about.total && *about.total > 0) {
        return (static_cast<double>(*about.used) / static_cast<double>(*about.total)) * 100.0;
    }
    return std::nullopt;
}

} // namespace

BackendsView::~BackendsView() {
    if (m_dark_signal_id)
        g_signal_handler_disconnect(adw_style_manager_get_default(), m_dark_signal_id);
}

BackendsView::BackendsView(rclone::RcloneManager& manager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager) {

    set_vexpand(true);
    set_hexpand(true);

    // Apply CSS styling for the capacity progress bar
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".capacity-bar trough {"
        "    min-height: 6px;"
        "    border-radius: 3px;"
        "}"
        ".capacity-bar progress {"
        "    min-height: 6px;"
        "    border-radius: 3px;"
        "}"
    );
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    // Navigation view for list <-> edit push/pop
    m_nav_view = adw::navigation_view();

    // Build the list page content
    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    m_scroll.set_child(*clamp);

    m_prefs_group = adw::preferences_group();
    adw::preferences_group_set_title(m_prefs_group, _("Configured Remotes"));

    // Add button in the header suffix
    auto* add_btn = Gtk::make_managed<Gtk::Button>();
    add_btn->set_icon_name("list-add-symbolic");
    add_btn->add_css_class("flat");
    add_btn->set_tooltip_text(_("Add a new remote storage location such as Google Drive, S3, or SFTP"));
    add_btn->signal_clicked().connect(sigc::mem_fun(*this, &BackendsView::show_add_remote));
    adw::preferences_group_set_header_suffix(m_prefs_group, add_btn);

    // Empty state
    m_empty_status = adw::status_page();
    adw::status_page_set_icon_name(m_empty_status, "preferences-system-symbolic");
    adw::status_page_set_title(m_empty_status, _("No Remotes Configured"));
    adw::status_page_set_description(m_empty_status,
        _("Click the + button above to add your first remote."));
    m_empty_status->set_visible(false);

    auto* content_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    content_box->append(*m_prefs_group);
    content_box->append(*m_empty_status);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(content_box->gobj()));

    // Wrap the scroll in a navigation page
    m_list_page = adw::navigation_page_new(&m_scroll, _("Backends"));
    adw::navigation_view_push_page(m_nav_view, m_list_page);

    append(*m_nav_view);

    signal_map().connect([this]() { refresh(); });

    m_dark_signal_id = g_signal_connect(
        adw_style_manager_get_default(), "notify::dark",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            static_cast<BackendsView*>(data)->refresh();
        }), this);
}

void BackendsView::refresh() {
    m_manager.cli().list_remotes([this](auto result) {
        if (result.has_value()) {
            populate(result.value());
        }
    });
}

void BackendsView::populate(const std::vector<rclone::RemoteInfo>& remotes) {
    m_populate_token = std::make_shared<bool>(true);

    for (auto& r : m_rows) {
        adw_preferences_group_remove(
            ADW_PREFERENCES_GROUP(m_prefs_group->gobj()),
            r.row->gobj());
    }
    m_rows.clear();

    m_empty_status->set_visible(remotes.empty());

    for (auto& remote : remotes) {
        auto* row = adw::action_row();
        adw::preferences_row_set_title(row, remote.name.c_str());
        adw::action_row_set_subtitle(row, remote.type.c_str());

        adw::action_row_add_prefix(row, make_remote_icon(remote.type));

        RemoteRow rr;
        rr.row = row;

        // Capacity container: vertical box with progress bar above text
        rr.capacity_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        rr.capacity_box->set_halign(Gtk::Align::END);
        rr.capacity_box->set_valign(Gtk::Align::CENTER);
        rr.capacity_box->set_margin_end(6);

        // Capacity progress bar
        rr.capacity_bar = Gtk::make_managed<Gtk::ProgressBar>();
        rr.capacity_bar->set_halign(Gtk::Align::FILL);
        rr.capacity_bar->set_valign(Gtk::Align::CENTER);
        rr.capacity_bar->set_size_request(120, -1);
        rr.capacity_bar->set_fraction(0.0);
        rr.capacity_bar->set_visible(false); // hidden until we get data
        rr.capacity_bar->add_css_class("capacity-bar");

        // Capacity label (dimmed style)
        rr.capacity_label = Gtk::make_managed<Gtk::Label>();
        rr.capacity_label->set_halign(Gtk::Align::CENTER);
        rr.capacity_label->set_valign(Gtk::Align::CENTER);
        rr.capacity_label->add_css_class("dim-label");
        rr.capacity_label->set_text(""); // initially empty

        rr.capacity_box->append(*rr.capacity_bar);
        rr.capacity_box->append(*rr.capacity_label);

        adw::action_row_add_suffix(row, rr.capacity_box);

        // Edit button
        rr.edit_btn = std::make_unique<Gtk::Button>();
        rr.edit_btn->set_icon_name("document-edit-symbolic");
        rr.edit_btn->set_valign(Gtk::Align::CENTER);
        rr.edit_btn->add_css_class("flat");
        rr.edit_btn->set_tooltip_text(_("Edit the connection settings for this remote storage location"));

        auto remote_copy = remote;
        rr.edit_btn->signal_clicked().connect([this, remote_copy]() {
            show_edit_remote(remote_copy);
        });

        // Delete button
        rr.del_btn = std::make_unique<Gtk::Button>();
        rr.del_btn->set_icon_name("user-trash-symbolic");
        rr.del_btn->set_valign(Gtk::Align::CENTER);
        rr.del_btn->add_css_class("flat");
        rr.del_btn->add_css_class("destructive-action");
        rr.del_btn->set_tooltip_text(_("Permanently remove this remote storage location from rclone's configuration"));

        std::string name = remote.name;
        rr.del_btn->signal_clicked().connect([this, name]() {
            on_delete_remote(name);
        });

        adw::action_row_add_suffix(row, rr.edit_btn.get());
        adw::action_row_add_suffix(row, rr.del_btn.get());
        adw::preferences_group_add(m_prefs_group, row);

        m_rows.push_back(std::move(rr));

        // Fetch about info asynchronously for this remote
        // Capture raw row pointers since m_rows may be reallocated
        auto* cap_label = m_rows.back().capacity_label;
        auto* cap_bar = m_rows.back().capacity_bar;
        std::string fs_name = remote.name + ":";
        m_manager.rc().get_about(fs_name, [cap_label, cap_bar,
                                           weak_token = std::weak_ptr<bool>(m_populate_token)](auto result) {
            if (result.has_value()) {
                auto text = format_capacity(result.value());
                auto percent = compute_usage_percent(result.value());
                if (!text.empty()) {
                    Glib::signal_idle().connect_once([cap_label, text, weak_token]() {
                        if (auto t = weak_token.lock(); !t || !*t) return;
                        cap_label->set_text(text);
                    });
                }
                if (percent.has_value()) {
                    Glib::signal_idle().connect_once([cap_bar, pct = *percent, weak_token]() {
                        if (auto t = weak_token.lock(); !t || !*t) return;
                        cap_bar->set_fraction(pct / 100.0);
                        cap_bar->set_visible(true);
                    });
                }
            }
            // If about fails, label stays empty and bar stays hidden — no error shown
        });
    }
}

void BackendsView::on_delete_remote(const std::string& name) {
    m_manager.cli().config_delete(name, [this](auto result) {
        if (result.has_value()) refresh();
    });
}

void BackendsView::show_add_remote() {
    m_edit_view = std::make_unique<BackendEditView>(m_manager,
        [this]() { on_edit_done(); });

    auto* page = adw::navigation_page_new(m_edit_view.get(), _("New Remote"));
    adw::navigation_view_push_page(m_nav_view, page);
}

void BackendsView::show_edit_remote(const rclone::RemoteInfo& remote) {
    m_edit_view = std::make_unique<BackendEditView>(m_manager, remote,
        [this]() { on_edit_done(); });

    auto* page = adw::navigation_page_new(m_edit_view.get(), _("Edit Remote"));
    adw::navigation_view_push_page(m_nav_view, page);
}

void BackendsView::on_edit_done() {
    adw::navigation_view_pop(m_nav_view);
    refresh();
}

} // namespace mtsync
