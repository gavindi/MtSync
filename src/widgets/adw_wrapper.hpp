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

#include <adwaita.h>
#include <gtkmm.h>
#include <gtkmm/sorter.h>

// Thin C++ helpers over the libadwaita C API.
// Since libadwaitamm doesn't exist, we call the C API directly and use
// Glib::wrap() to embed the resulting GtkWidget* into the gtkmm widget tree.

namespace adw {

inline void init() {
    adw_init();
}

// --- Header Bar ---

inline Gtk::Widget* header_bar() {
    auto* w = adw_header_bar_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void header_bar_set_title_widget(Gtk::Widget* header, Gtk::Widget* title) {
    adw_header_bar_set_title_widget(
        ADW_HEADER_BAR(header->gobj()),
        title->gobj());
}

// --- View Stack ---

inline AdwViewStack* view_stack_new() {
    return ADW_VIEW_STACK(adw_view_stack_new());
}

inline Gtk::Widget* view_stack_widget(AdwViewStack* stack) {
    return Glib::wrap(GTK_WIDGET(stack));
}

inline AdwViewStackPage* view_stack_add_titled(
    AdwViewStack* stack, Gtk::Widget* child,
    const char* name, const char* title) {
    return adw_view_stack_add_titled(stack, child->gobj(), name, title);
}

// --- View Switcher ---

inline Gtk::Widget* view_switcher(AdwViewStack* stack) {
    auto* sw = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(sw), stack);
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(sw), ADW_VIEW_SWITCHER_POLICY_WIDE);
    return Glib::wrap(GTK_WIDGET(sw));
}

// --- Toolbar View ---

inline Gtk::Widget* toolbar_view() {
    auto* w = adw_toolbar_view_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void toolbar_view_add_top_bar(Gtk::Widget* toolbar, Gtk::Widget* bar) {
    adw_toolbar_view_add_top_bar(
        ADW_TOOLBAR_VIEW(toolbar->gobj()),
        bar->gobj());
}

inline void toolbar_view_set_content(Gtk::Widget* toolbar, Gtk::Widget* content) {
    adw_toolbar_view_set_content(
        ADW_TOOLBAR_VIEW(toolbar->gobj()),
        content->gobj());
}

// --- Toast Overlay ---

inline Gtk::Widget* toast_overlay() {
    auto* w = adw_toast_overlay_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void toast_overlay_set_child(Gtk::Widget* overlay, Gtk::Widget* child) {
    adw_toast_overlay_set_child(
        ADW_TOAST_OVERLAY(overlay->gobj()),
        child->gobj());
}

inline void toast_overlay_add_toast(Gtk::Widget* overlay, const char* title) {
    auto* toast = adw_toast_new(title);
    adw_toast_overlay_add_toast(
        ADW_TOAST_OVERLAY(overlay->gobj()),
        toast);
}

// --- Preferences Group ---

inline Gtk::Widget* preferences_group() {
    auto* w = adw_preferences_group_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void preferences_group_set_title(Gtk::Widget* group, const char* title) {
    adw_preferences_group_set_title(
        ADW_PREFERENCES_GROUP(group->gobj()), title);
}

inline void preferences_group_add(Gtk::Widget* group, Gtk::Widget* child) {
    adw_preferences_group_add(
        ADW_PREFERENCES_GROUP(group->gobj()),
        child->gobj());
}

inline void preferences_group_set_header_suffix(Gtk::Widget* group, Gtk::Widget* suffix) {
    adw_preferences_group_set_header_suffix(
        ADW_PREFERENCES_GROUP(group->gobj()),
        suffix->gobj());
}

// --- Preferences Row (base class for all row types) ---

inline Gtk::Widget* preferences_row_new() {
    auto* w = adw_preferences_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void preferences_row_set_title(Gtk::Widget* row, const char* title) {
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row->gobj()), title);
}

// --- Action Row ---

inline Gtk::Widget* action_row() {
    auto* w = adw_action_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void action_row_set_subtitle(Gtk::Widget* row, const char* subtitle) {
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row->gobj()), subtitle);
}

inline void action_row_add_prefix(Gtk::Widget* row, Gtk::Widget* prefix) {
    adw_action_row_add_prefix(ADW_ACTION_ROW(row->gobj()), prefix->gobj());
}

inline void action_row_add_suffix(Gtk::Widget* row, Gtk::Widget* suffix) {
    adw_action_row_add_suffix(ADW_ACTION_ROW(row->gobj()), suffix->gobj());
}

inline void action_row_set_activatable_widget(Gtk::Widget* row, Gtk::Widget* widget) {
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row->gobj()), widget->gobj());
}

// --- Navigation View ---

inline Gtk::Widget* navigation_view() {
    auto* w = adw_navigation_view_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void navigation_view_push_page(Gtk::Widget* nav, AdwNavigationPage* page) {
    adw_navigation_view_push(
        ADW_NAVIGATION_VIEW(nav->gobj()), page);
}

inline void navigation_view_pop(Gtk::Widget* nav) {
    adw_navigation_view_pop(ADW_NAVIGATION_VIEW(nav->gobj()));
}

inline AdwNavigationPage* navigation_page_new(Gtk::Widget* child, const char* title) {
    return ADW_NAVIGATION_PAGE(
        adw_navigation_page_new(child->gobj(), title));
}

// --- Entry Row ---

inline Gtk::Widget* entry_row() {
    auto* w = adw_entry_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline const char* entry_row_get_text(Gtk::Widget* row) {
    return gtk_editable_get_text(GTK_EDITABLE(row->gobj()));
}

inline void entry_row_set_text(Gtk::Widget* row, const char* text) {
    gtk_editable_set_text(GTK_EDITABLE(row->gobj()), text);
}

// --- Password Entry Row ---

inline Gtk::Widget* password_entry_row() {
    auto* w = adw_password_entry_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

// --- Switch Row ---

inline Gtk::Widget* switch_row() {
    auto* w = adw_switch_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline bool switch_row_get_active(Gtk::Widget* row) {
    return adw_switch_row_get_active(ADW_SWITCH_ROW(row->gobj()));
}

inline void switch_row_set_active(Gtk::Widget* row, bool active) {
    adw_switch_row_set_active(ADW_SWITCH_ROW(row->gobj()), active);
}

// --- Combo Row ---

inline Gtk::Widget* combo_row() {
    auto* w = adw_combo_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void combo_row_set_model(Gtk::Widget* row, GListModel* model) {
    adw_combo_row_set_model(ADW_COMBO_ROW(row->gobj()), model);
}

inline void combo_row_set_string_list_model(Gtk::Widget* row, GtkStringList* model) {
    adw_combo_row_set_model(ADW_COMBO_ROW(row->gobj()), G_LIST_MODEL(model));
    auto* expr = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string");
    adw_combo_row_set_expression(ADW_COMBO_ROW(row->gobj()), expr);
}

inline guint combo_row_get_selected(Gtk::Widget* row) {
    return adw_combo_row_get_selected(ADW_COMBO_ROW(row->gobj()));
}

// --- Expander Row ---

inline Gtk::Widget* expander_row() {
    auto* w = adw_expander_row_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void expander_row_add_row(Gtk::Widget* expander, Gtk::Widget* child) {
    adw_expander_row_add_row(ADW_EXPANDER_ROW(expander->gobj()), child->gobj());
}

// --- Custom Sorter (GtkCustomSorter has no gtkmm binding) ---
// Comparator receives GObject* items directly from the list model.

template <typename Func>
inline Glib::RefPtr<Gtk::Sorter> make_sorter(Func&& f) {
    using Fn = std::function<int(GObject*, GObject*)>;
    auto* fn = new Fn(std::forward<Func>(f));
    GtkCustomSorter* sorter = gtk_custom_sorter_new(
        [](gconstpointer a, gconstpointer b, gpointer data) -> gint {
            return (*static_cast<Fn*>(data))(G_OBJECT(a), G_OBJECT(b));
        },
        fn,
        [](gpointer data) { delete static_cast<Fn*>(data); });
    // take_copy=false: take ownership of the ref returned by gtk_custom_sorter_new
    return Glib::wrap(GTK_SORTER(sorter), false);
}

// --- Header Bar title button visibility ---

inline void header_bar_set_show_start_title_buttons(Gtk::Widget* header, bool show) {
    adw_header_bar_set_show_start_title_buttons(
        ADW_HEADER_BAR(header->gobj()), show ? TRUE : FALSE);
}

inline void header_bar_set_show_end_title_buttons(Gtk::Widget* header, bool show) {
    adw_header_bar_set_show_end_title_buttons(
        ADW_HEADER_BAR(header->gobj()), show ? TRUE : FALSE);
}

// --- Spinner ---
// AdwSpinner requires libadwaita >= 1.6; fall back to GtkSpinner on older distros.

inline Gtk::Widget* spinner() {
#if ADW_CHECK_VERSION(1, 6, 0)
    return Glib::wrap(GTK_WIDGET(adw_spinner_new()));
#else
    auto* s = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(s));
    return Glib::wrap(s);
#endif
}

// --- Status Page (for empty states) ---

inline Gtk::Widget* status_page() {
    auto* w = adw_status_page_new();
    return Glib::wrap(GTK_WIDGET(w));
}

inline void status_page_set_icon_name(Gtk::Widget* page, const char* icon) {
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(page->gobj()), icon);
}

inline void status_page_set_title(Gtk::Widget* page, const char* title) {
    adw_status_page_set_title(ADW_STATUS_PAGE(page->gobj()), title);
}

inline void status_page_set_description(Gtk::Widget* page, const char* desc) {
    adw_status_page_set_description(ADW_STATUS_PAGE(page->gobj()), desc);
}

inline void status_page_set_child(Gtk::Widget* page, Gtk::Widget* child) {
    adw_status_page_set_child(ADW_STATUS_PAGE(page->gobj()), child->gobj());
}

// --- Carousel ---

inline AdwCarousel* carousel_new() {
    return ADW_CAROUSEL(adw_carousel_new());
}

inline Gtk::Widget* carousel_widget(AdwCarousel* carousel) {
    return Glib::wrap(GTK_WIDGET(carousel));
}

inline void carousel_append(AdwCarousel* carousel, Gtk::Widget* child) {
    adw_carousel_append(carousel, child->gobj());
}

inline void carousel_scroll_to(AdwCarousel* carousel, Gtk::Widget* child, bool animate) {
    adw_carousel_scroll_to(carousel, child->gobj(), animate ? TRUE : FALSE);
}

inline guint carousel_get_n_pages(AdwCarousel* carousel) {
    return adw_carousel_get_n_pages(carousel);
}

inline Gtk::Widget* carousel_indicator_dots(AdwCarousel* carousel) {
    auto* w = adw_carousel_indicator_dots_new();
    adw_carousel_indicator_dots_set_carousel(ADW_CAROUSEL_INDICATOR_DOTS(w), carousel);
    return Glib::wrap(GTK_WIDGET(w));
}

} // namespace adw
