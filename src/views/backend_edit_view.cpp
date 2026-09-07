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

#include "views/backend_edit_view.hpp"
#include "widgets/adw_wrapper.hpp"
#include <algorithm>
#include <unordered_set>
#include <giomm.h>
#include <glibmm/i18n.h>

namespace mtsync {

// ── ProviderItem implementation ──

Glib::RefPtr<ProviderItem> ProviderItem::create(int index, const std::string& name,
                                                  const std::string& prefix,
                                                  const std::string& description,
                                                  const std::string& icon_name) {
    return Glib::make_refptr_for_instance(new ProviderItem(index, name, prefix, description, icon_name));
}

ProviderItem::ProviderItem(int index, const std::string& name, const std::string& prefix,
                           const std::string& description, const std::string& icon_name)
    : Glib::ObjectBase("MtSyncProviderItem"),
      m_index(index), m_name(name), m_prefix(prefix),
      m_description(description), m_icon_name(icon_name) {
}

BackendEditView::BackendEditView(rclone::RcloneManager& manager, DoneCallback on_done)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager)
    , m_on_done(std::move(on_done)) {
    setup_ui();
    load_providers();
}

BackendEditView::BackendEditView(rclone::RcloneManager& manager,
                                   const rclone::RemoteInfo& remote,
                                   DoneCallback on_done)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , m_manager(manager)
    , m_on_done(std::move(on_done))
    , m_editing(remote) {
    setup_ui();
    load_providers();
}

void BackendEditView::setup_ui() {
    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    append(m_scroll);

    auto* clamp = Glib::wrap(GTK_WIDGET(adw_clamp_new()));
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp->gobj()), 600);
    clamp->set_margin_top(24);
    clamp->set_margin_bottom(24);
    clamp->set_margin_start(12);
    clamp->set_margin_end(12);
    m_scroll.set_child(*clamp);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
    adw_clamp_set_child(ADW_CLAMP(clamp->gobj()), GTK_WIDGET(vbox->gobj()));

    // Basic settings group
    auto* basic_group = adw::preferences_group();
    adw::preferences_group_set_title(basic_group, _("Remote Settings"));
    vbox->append(*basic_group);

    // Name entry
    m_name_row = adw::entry_row();
    adw::preferences_row_set_title(m_name_row, _("Name"));
    if (m_editing) {
        adw::entry_row_set_text(m_name_row, m_editing->name.c_str());
        gtk_widget_set_sensitive(m_name_row->gobj(), false);
    }
    adw::preferences_group_add(basic_group, m_name_row);

    // Provider combo with icon factory
    m_provider_combo = adw::combo_row();
    adw::preferences_row_set_title(m_provider_combo, _("Provider"));
    m_provider_model = Gio::ListStore<ProviderItem>::create();
    adw::combo_row_set_model(m_provider_combo, G_LIST_MODEL(m_provider_model->gobj()));
    adw_combo_row_set_enable_search(ADW_COMBO_ROW(m_provider_combo->gobj()), true);

    // Factory for the selected item display
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_pixel_size(20);
        icon->set_valign(Gtk::Align::CENTER);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_halign(Gtk::Align::START);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*icon);
        box->append(*label);
        item->set_child(*box);
    });
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto icon_w = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* label = dynamic_cast<Gtk::Label*>(box->get_first_child()->get_next_sibling());
        auto prov = item->get_item();
        if (!prov || !icon_w || !label) return;
        auto pi = std::dynamic_pointer_cast<ProviderItem>(prov);
        if (!pi) return;
        std::string base = "/io/github/mtsync/provider-icons/" + pi->get_icon_name();
        // Use resource for provider SVGs, fall back to symbolic for defaults
        if (pi->get_icon_name().find("-symbolic") != std::string::npos) {
            icon_w->set_from_icon_name(pi->get_icon_name());
            icon_w->set_pixel_size(20);
        } else {
            std::string res = base + ".svg";
            if (adw_style_manager_get_dark(adw_style_manager_get_default()))
                res = base + "-dark.svg";
            icon_w->set_from_resource(res);
        }
        label->set_text(pi->get_name());
    });
    adw_combo_row_set_factory(ADW_COMBO_ROW(m_provider_combo->gobj()), GTK_LIST_ITEM_FACTORY(factory->gobj()));

    // Factory for the popup list
    auto list_factory = Gtk::SignalListItemFactory::create();
    list_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_pixel_size(20);
        icon->set_valign(Gtk::Align::CENTER);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_halign(Gtk::Align::START);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*icon);
        box->append(*label);
        item->set_child(*box);
    });
    list_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        auto icon_w = dynamic_cast<Gtk::Image*>(box->get_first_child());
        auto* label = dynamic_cast<Gtk::Label*>(box->get_first_child()->get_next_sibling());
        auto prov = item->get_item();
        if (!prov || !icon_w || !label) return;
        auto pi = std::dynamic_pointer_cast<ProviderItem>(prov);
        if (!pi) return;
        std::string base = "/io/github/mtsync/provider-icons/" + pi->get_icon_name();
        if (pi->get_icon_name().find("-symbolic") != std::string::npos) {
            icon_w->set_from_icon_name(pi->get_icon_name());
            icon_w->set_pixel_size(20);
        } else {
            std::string res = base + ".svg";
            if (adw_style_manager_get_dark(adw_style_manager_get_default()))
                res = base + "-dark.svg";
            icon_w->set_from_resource(res);
        }
        label->set_text(pi->get_name());
    });
    adw_combo_row_set_list_factory(ADW_COMBO_ROW(m_provider_combo->gobj()), GTK_LIST_ITEM_FACTORY(list_factory->gobj()));

    adw::preferences_group_add(basic_group, m_provider_combo);

    // Connect provider selection change
    g_signal_connect(m_provider_combo->gobj(), "notify::selected",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto* self = static_cast<BackendEditView*>(data);
            auto idx = adw::combo_row_get_selected(self->m_provider_combo);
            if (idx != GTK_INVALID_LIST_POSITION) {
                auto item = self->m_provider_model->get_item(idx);
                if (item) {
                    auto pi = std::dynamic_pointer_cast<ProviderItem>(item);
                    if (pi) self->on_provider_selected(pi->get_index());
                }
            }
        }), this);

    // OAuth group (hidden by default, shown for OAuth providers)
    m_oauth_group = adw::preferences_group();
    adw::preferences_group_set_title(m_oauth_group, _("Authorization"));
    m_oauth_group->set_visible(false);

    auto* oauth_row = adw::action_row();
    adw::preferences_row_set_title(oauth_row, _("OAuth Login"));
    adw::action_row_set_subtitle(oauth_row,
        _("Click Authorize to sign in via your browser"));

    m_authorize_btn.add_css_class("suggested-action");
    m_authorize_btn.set_valign(Gtk::Align::CENTER);
    m_authorize_btn.set_tooltip_text(_("Open a browser window to sign in and grant rclone access to this storage provider via OAuth"));
    m_authorize_btn.signal_clicked().connect(
        sigc::mem_fun(*this, &BackendEditView::on_authorize));
    adw::action_row_add_suffix(oauth_row, &m_authorize_btn);

    m_oauth_spinner.set_spinning(false);
    m_oauth_spinner.set_visible(false);
    m_oauth_spinner.set_valign(Gtk::Align::CENTER);
    adw::action_row_add_suffix(oauth_row, &m_oauth_spinner);

    adw::preferences_group_add(m_oauth_group, oauth_row);

    m_oauth_status.set_xalign(0);
    m_oauth_status.add_css_class("dim-label");
    m_oauth_status.set_margin_start(12);
    m_oauth_status.set_visible(false);

    vbox->append(*m_oauth_group);
    vbox->append(m_oauth_status);

    // Form group for dynamic fields (created empty, populated on provider select)
    m_form_group = adw::preferences_group();
    adw::preferences_group_set_title(m_form_group, _("Options"));
    vbox->append(*m_form_group);

    // Advanced options expander (inside a separate group)
    auto* advanced_group = adw::preferences_group();
    m_advanced_expander = adw::expander_row();
    adw::preferences_row_set_title(m_advanced_expander, _("Advanced Options"));
    adw::preferences_group_add(advanced_group, m_advanced_expander);
    vbox->append(*advanced_group);

    // Save / Cancel buttons
    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    btn_box->set_halign(Gtk::Align::CENTER);
    btn_box->set_margin_top(12);

    auto* save_btn = Gtk::make_managed<Gtk::Button>(m_editing ? _("Update") : _("Save"));
    save_btn->add_css_class("suggested-action");
    save_btn->set_tooltip_text(_("Save the remote configuration and return to the remotes list"));
    save_btn->signal_clicked().connect(sigc::mem_fun(*this, &BackendEditView::on_save));
    btn_box->append(*save_btn);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>(_("Cancel"));
    cancel_btn->set_tooltip_text(_("Discard all unsaved changes and return to the remotes list"));
    cancel_btn->signal_clicked().connect([this]() {
        if (m_on_done) m_on_done();
    });
    btn_box->append(*cancel_btn);

    vbox->append(*btn_box);
}

void BackendEditView::load_providers() {
    m_manager.cli().get_providers([this](auto result) {
        if (!result.has_value()) return;
        m_providers = std::move(result.value());

        static const std::unordered_set<std::string> excluded = {
            "memory", "alias", "union", "cache", "crypt"
        };
        m_providers.erase(
            std::remove_if(m_providers.begin(), m_providers.end(),
                [](const rclone::ProviderInfo& p) {
                    return excluded.count(p.name) > 0;
                }),
            m_providers.end());

        std::sort(m_providers.begin(), m_providers.end(),
            [](const rclone::ProviderInfo& a, const rclone::ProviderInfo& b) {
                return a.name < b.name;
            });

        m_provider_model->remove_all();
        for (size_t i = 0; i < m_providers.size(); ++i) {
            auto& p = m_providers[i];
            // Determine icon resource name
            std::string icon_name = "network-server-symbolic";
            auto lower_name = p.name;
            for (auto& c : lower_name) c = static_cast<char>(g_ascii_tolower(static_cast<guchar>(c)));
            // Map common provider names to icon resources
            if (lower_name.find("google drive") != std::string::npos) icon_name = "drive";
            else if (lower_name == "dropbox") icon_name = "dropbox";
            else if (lower_name.find("backblaze") != std::string::npos || lower_name == "b2") icon_name = "b2";
            else if (lower_name == "mega") icon_name = "mega";
            else if (lower_name == "box") icon_name = "box";
            else if (lower_name.find("google cloud") != std::string::npos) icon_name = "gcs";
            else if (lower_name.find("proton") != std::string::npos) icon_name = "protondrive";
            else if (lower_name.find("internet archive") != std::string::npos) icon_name = "internetarchive";
            else if (lower_name.find("google photos") != std::string::npos) icon_name = "gphotos";
            else if (lower_name == "seafile") icon_name = "seafile";
            else if (lower_name.find("zoho") != std::string::npos) icon_name = "zoho";
            else if (lower_name == "filen") icon_name = "filen";
            else if (lower_name.find("swift") != std::string::npos) icon_name = "swift";
            else if (lower_name == "hdfs" || lower_name.find("hadoop") != std::string::npos) icon_name = "hdfs";
            else if (lower_name.find("sharefile") != std::string::npos) icon_name = "sharefile";
            else if (lower_name.find("digitalocean") != std::string::npos) icon_name = "digitalocean";
            else if (lower_name == "wasabi") icon_name = "wasabi";
            else if (lower_name.find("cloudflare") != std::string::npos) icon_name = "cloudflare";
            else if (lower_name.find("hetzner") != std::string::npos) icon_name = "hetzner";
            else if (lower_name.find("onedrive") != std::string::npos) icon_name = "onedrive";
            else if (lower_name.find("pcloud") != std::string::npos) icon_name = "pcloud";
            else if (lower_name.find("amazon s3") != std::string::npos || lower_name.find(" s3") != std::string::npos) icon_name = "s3";
            else if (lower_name.find("azure") != std::string::npos) icon_name = "azureblob";
            else if (lower_name.find("yandex") != std::string::npos) icon_name = "yandex";
            else if (lower_name.find("mail.ru") != std::string::npos) icon_name = "mailru";
            else if (lower_name == "koofr") icon_name = "koofr";
            else if (lower_name.find("jotta") != std::string::npos) icon_name = "jottacloud";
            else if (lower_name.find("put.io") != std::string::npos) icon_name = "putio";
            else if (lower_name.find("premiumize") != std::string::npos) icon_name = "premiumize";

            auto item = ProviderItem::create(static_cast<int>(i), p.name, p.prefix, p.description, icon_name);
            m_provider_model->append(item);
        }

        // If editing, select the matching provider
        if (m_editing) {
            for (size_t i = 0; i < m_providers.size(); ++i) {
                if (m_providers[i].name == m_editing->type ||
                    m_providers[i].prefix == m_editing->type) {
                    adw_combo_row_set_selected(
                        ADW_COMBO_ROW(m_provider_combo->gobj()),
                        static_cast<guint>(i));
                    break;
                }
            }
        }
    });
}

void BackendEditView::on_provider_selected(int index) {
    if (index < 0 || index >= static_cast<int>(m_providers.size())) return;
    m_selected_provider = index;
    m_oauth_token.clear();

    // Show/hide OAuth group based on provider
    bool oauth = m_providers[index].needs_oauth();
    m_oauth_group->set_visible(oauth);
    m_oauth_status.set_visible(false);
    m_authorize_btn.set_sensitive(true);
    m_authorize_btn.set_label(_("Authorize"));
    m_oauth_spinner.set_visible(false);
    m_oauth_spinner.set_spinning(false);

    build_form(m_providers[index]);
}

void BackendEditView::build_form(const rclone::ProviderInfo& provider) {
    // Clear existing dynamic fields
    for (auto& f : m_fields) {
        if (f.type == "advanced_bool" || f.type == "advanced_string" ||
            f.type == "advanced_password" || f.type == "advanced_combo") {
            // In expander — can't easily remove individually
        } else {
            adw_preferences_group_remove(
                ADW_PREFERENCES_GROUP(m_form_group->gobj()),
                f.widget->gobj());
        }
    }
    m_fields.clear();

    for (auto& opt : provider.options) {
        // Skip the token field — we handle it via OAuth authorize
        if (opt.name == "token") continue;

        Gtk::Widget* widget = nullptr;
        std::string type;

        if (opt.type == "bool") {
            widget = adw::switch_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            if (opt.default_value == "true")
                adw::switch_row_set_active(widget, true);
            type = "bool";
        } else if (opt.is_password) {
            widget = adw::password_entry_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            type = "password";
        } else if (opt.exclusive && !opt.examples.empty()) {
            widget = adw::combo_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            auto* model = gtk_string_list_new(nullptr);
            for (auto& ex : opt.examples) {
                auto label = ex.help.empty() ? ex.value : ex.value + " - " + ex.help;
                gtk_string_list_append(model, label.c_str());
            }
            adw::combo_row_set_string_list_model(widget, model);
            type = "combo";
        } else {
            widget = adw::entry_row();
            adw::preferences_row_set_title(widget, opt.name.c_str());
            if (!opt.default_value.empty())
                adw::entry_row_set_text(widget, opt.default_value.c_str());
            type = "string";
        }

        // Pre-fill if editing
        if (m_editing && m_editing->params.contains(opt.name)) {
            auto val = m_editing->params[opt.name].get<std::string>();
            if (type == "bool") {
                adw::switch_row_set_active(widget, val == "true");
            } else if (type == "string" || type == "password") {
                adw::entry_row_set_text(widget, val.c_str());
            }
        }

        // Place in appropriate container
        if (opt.advanced) {
            adw::expander_row_add_row(m_advanced_expander, widget);
            type = "advanced_" + type;
        } else {
            adw::preferences_group_add(m_form_group, widget);
        }

        m_fields.push_back({opt.name, widget, type});
    }
}

void BackendEditView::on_authorize() {
    if (m_selected_provider < 0 ||
        m_selected_provider >= static_cast<int>(m_providers.size())) return;

    auto& provider = m_providers[m_selected_provider];

    // Get client_id and client_secret from form fields if the user filled them
    std::string client_id, client_secret;
    for (auto& f : m_fields) {
        if (f.option_name == "client_id") {
            auto base = f.type;
            if (base.starts_with("advanced_")) base = base.substr(9);
            if (base == "string") client_id = adw::entry_row_get_text(f.widget);
        } else if (f.option_name == "client_secret") {
            auto base = f.type;
            if (base.starts_with("advanced_")) base = base.substr(9);
            if (base == "string" || base == "password")
                client_secret = adw::entry_row_get_text(f.widget);
        }
    }

    // Update UI to show authorizing state
    m_authorize_btn.set_sensitive(false);
    m_authorize_btn.set_label(_("Waiting..."));
    m_oauth_spinner.set_visible(true);
    m_oauth_spinner.set_spinning(true);
    m_oauth_status.set_visible(true);
    m_oauth_status.set_text(_("Browser opened — complete sign-in to continue..."));

    std::string backend = provider.prefix.empty() ? provider.name : provider.prefix;

    m_manager.cli().authorize(backend, client_id, client_secret,
        [this, weak_alive = std::weak_ptr<bool>(m_alive)](auto result) {
            if (weak_alive.expired()) return;
            m_oauth_spinner.set_spinning(false);
            m_oauth_spinner.set_visible(false);
            m_authorize_btn.set_sensitive(true);

            if (result.has_value()) {
                m_oauth_token = std::move(result.value());
                m_authorize_btn.set_label(_("Authorized"));
                m_authorize_btn.remove_css_class("suggested-action");
                m_authorize_btn.add_css_class("success");
                m_oauth_status.set_text(_("Authorization successful."));
            } else {
                m_authorize_btn.set_label(_("Retry"));
                m_oauth_status.set_text(std::string(_("Authorization failed: ")) + result.error());
            }
        });
}

void BackendEditView::on_save() {
    std::string name = adw::entry_row_get_text(m_name_row);
    if (name.empty()) return;

    auto params = collect_params();
    auto idx = adw::combo_row_get_selected(m_provider_combo);
    if (idx == GTK_INVALID_LIST_POSITION || idx >= m_providers.size()) return;

    // Include OAuth token if we have one
    if (!m_oauth_token.empty())
        params.emplace_back("token", m_oauth_token);

    std::string provider_type = m_providers[idx].prefix.empty()
        ? m_providers[idx].name : m_providers[idx].prefix;

    auto callback = [this](auto result) {
        if (result.has_value() && m_on_done) m_on_done();
    };

    if (m_editing) {
        m_manager.cli().config_update(name, params, std::move(callback));
    } else {
        m_manager.cli().config_create(name, provider_type, params, std::move(callback));
    }
}

std::vector<std::pair<std::string, std::string>> BackendEditView::collect_params() {
    std::vector<std::pair<std::string, std::string>> params;
    for (auto& f : m_fields) {
        std::string base_type = f.type;
        if (base_type.starts_with("advanced_"))
            base_type = base_type.substr(9);

        std::string val;
        if (base_type == "bool") {
            val = adw::switch_row_get_active(f.widget) ? "true" : "false";
        } else if (base_type == "string" || base_type == "password") {
            val = adw::entry_row_get_text(f.widget);
        } else if (base_type == "combo") {
            auto sel = adw::combo_row_get_selected(f.widget);
            for (auto& provider : m_providers) {
                for (auto& opt : provider.options) {
                    if (opt.name == f.option_name && sel < opt.examples.size()) {
                        val = opt.examples[sel].value;
                        break;
                    }
                }
                if (!val.empty()) break;
            }
        }

        if (!val.empty())
            params.emplace_back(f.option_name, val);
    }
    return params;
}

} // namespace mtsync
