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

#include "rclone/rclone_manager.hpp"
#include "rclone/rclone_types.hpp"
#include "widgets/file_row.hpp"
#include <giomm.h>
#include <gtkmm.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mtsync {

class CompareDialog : public Gtk::Window {
public:
    CompareDialog(const std::string& src,
                  const std::string& dst,
                  rclone::RcloneManager& manager);

    // Dry-run preview: runs `job` with --dry-run (whatever its Dry Run toggle
    // says) and shows the actions it would have taken, read-only.
    CompareDialog(const rclone::Job& job, rclone::RcloneManager& manager);
    ~CompareDialog() override;

private:
    std::string m_src;
    std::string m_dst;
    rclone::RcloneManager* m_manager = nullptr;
    std::optional<rclone::Job> m_dry_run_job; // set = dry-run preview mode

    // Dry-run tallies for the summary line, filled by dry_run_to_checks()
    struct DryRunCounts {
        int copy = 0, update = 0, move = 0, del = 0, del_src = 0, error = 0;
    };
    DryRunCounts m_dry_counts;
    std::vector<std::string> m_dry_errors; // errors not tied to a single file

    // Full merged dataset — populated after all three async ops complete
    std::vector<Glib::RefPtr<CompareRowObject>> m_all_rows;
    // Filtered+stripped view of m_all_rows; rebuilt by rebuild_filter_cache()
    std::vector<Glib::RefPtr<CompareRowObject>> m_filtered_rows;
    int m_filtered_file_count = 0;

    static constexpr int PAGE_SIZE = 50;
    int m_current_page = 0;
    int m_total_pages  = 1;

    // UI elements (managed by widget tree)
    Gtk::Stack*      m_stack       = nullptr;
    Gtk::ColumnView* m_column_view = nullptr;
    Gtk::Label*      m_page_label  = nullptr;
    Gtk::Button*     m_prev_btn    = nullptr;
    Gtk::Button*     m_next_btn    = nullptr;
    Gtk::Label*      m_error_label = nullptr;
    Gtk::Label*      m_summary_label = nullptr; // dry-run mode only
    Gtk::Button*       m_delete_btn      = nullptr;  // delete from source
    Gtk::Button*       m_copy_btn        = nullptr;  // copy src → dst
    Gtk::Button*       m_dst_copy_btn    = nullptr;  // copy dst → src
    Gtk::Button*       m_dst_delete_btn  = nullptr;  // delete from destination
    Gtk::ToggleButton* m_filter_left_btn  = nullptr;  // show '-' (← only-in-src)
    Gtk::ToggleButton* m_filter_right_btn = nullptr;  // show '+' (→ only-in-dst)
    Gtk::ToggleButton* m_filter_equal_btn = nullptr;  // show '=' (= identical)
    Gtk::ToggleButton* m_filter_diff_btn  = nullptr;  // show '*' (≠ different)
    Gtk::ToggleButton* m_filter_error_btn = nullptr;  // show '!' (! error)

    // Holds only the current page's items; wrapped in SortListModel and MultiSelection for the ColumnView
    Glib::RefPtr<Gio::ListStore<CompareRowObject>>   m_page_store;
    Glib::RefPtr<Gtk::SortListModel>                 m_sort_model;
    Glib::RefPtr<Gtk::MultiSelection>                m_file_selection;

    // Shared state for coordinating three parallel async operations
    struct LoadState {
        std::vector<rclone::FileEntry>  src_entries;
        std::vector<rclone::FileEntry>  dst_entries;
        std::vector<rclone::CheckEntry> check_entries;
        std::vector<rclone::DryRunAction> dry_actions;
        int         done_count = 0;
        std::string error;
        std::vector<Glib::RefPtr<Gio::Subprocess>> procs;
        bool cancelled = false;
    };

    std::shared_ptr<LoadState> m_load_state;

    // Alive sentinel: set to false on close so in-flight async callbacks skip `this`
    std::shared_ptr<bool> m_alive;

    void init();
    void cancel_load();
    void setup_ui();
    void build_column_view();
    void start_load(rclone::RcloneManager& manager);
    void merge_results(const std::vector<rclone::FileEntry>& src_files,
                       const std::vector<rclone::FileEntry>& dst_files,
                       const std::vector<rclone::CheckEntry>& checks);
    std::vector<rclone::CheckEntry> dry_run_to_checks(
        const std::vector<rclone::FileEntry>& src_files,
        const std::vector<rclone::FileEntry>& dst_files,
        const std::vector<rclone::DryRunAction>& actions);
    void update_dry_run_summary();
    void rebuild_filter_cache();
    void show_page(int page);
    void update_pagination_controls();
    void update_action_buttons();
    std::vector<std::string> get_selected_paths() const;
    void on_delete_clicked();
    void on_copy_clicked();
    void on_dst_copy_clicked();
    void on_dst_delete_clicked();
    void apply_filters();
    bool is_status_filtered(char status) const;

    static std::string format_size(int64_t bytes);
    static std::string format_date(const std::string& iso);
    static const char* status_css_class(char status);
};

} // namespace mtsync
