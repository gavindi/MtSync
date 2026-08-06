# Changelog

## 0.9.15 — Tray Animation Use-After-Free Fix

- **Use-after-free in the tray animation timer on daemon shutdown**: `TrayIcon`'s 100 ms animation timer was connected with a lambda capturing `this` in `start_animation()`, but `~TrayIcon()` never disconnected it — a `sigc::connection`'s destructor does not remove the glibmm timeout source, so when the daemon was stopped (SIGTERM, tray "Quit", IPC `quit`) while a non-mount job was running and the spinner animating, the source fired the callback on the already-destroyed `TrayIcon`, dereferencing freed members (`m_anim_frame`, `m_connection`). The destructor now calls `m_anim_timer.disconnect()` and clears a new `m_alive` shared-ptr sentinel that the timer lambda captures and checks before touching any state — even if the timer ever fires after destruction, it bails out without dereferencing the object. Confirmed under ASan: the pre-fix daemon aborted with `heap-use-after-free` in the `start_animation()` timer callback on every shutdown-while-animating run; the fixed daemon exits cleanly
- **About tab version bump**: the version shown on the About tab was still `0.9.14-pre`; updated to `0.9.15`

## 0.9.14 — Bisync Fixes

- **Bisync RC endpoint typo**: `RcloneRc::bisync_async` posted to `"bisync/bisync"`, which doesn't exist — rclone registers bisync under the `sync` RC group as `sync/bisync`, matching its siblings `sync/sync`, `sync/copy`, `sync/move`; every bi-directional sync job failed immediately with `couldn't find method "bisync/bisync"`
- **Missing `checkSync` default**: rclone's `sync/bisync` RC handler doesn't apply the `--check-sync` CLI default (`true`) when the `checkSync` param is omitted from the RC body — it decoded to the Go zero value `""` and rejected the request with `unknown check-sync mode for bisync: ""`; `bisync_async` now sends `checkSync: "true"` explicitly
- **Spurious job resubmission during checksum-heavy bisync**: a single missed `job/status` poll was treated as fatal job failure, causing the daemon to resubmit a brand-new bisync job while the original was often still running — two concurrent bisync runs against the same pair then collided on its workdir lock (`hash: failed to read: context canceled`), consuming hundreds of rclone job IDs without ever completing; polling now tolerates up to 6 consecutive failed polls (tracked via a new `JobState::poll_failures` counter) before giving up, and the shared `SoupSession` HTTP timeout is raised from 5s to 20s to reduce spurious timeouts while `rcd` is busy hashing files
- **First-run bisync baseline**: bisync refuses to run without an established baseline, aborting with `bisync aborted` on a pair that had never been synced before. Proactively forcing `resync: true` until a run succeeded (the initial fix) turned out to be unsafe — since bisync's own `--resync` re-establishes the baseline from current state on *every* invocation, forcing it repeatedly after unrelated failures could resurrect files the user had just deleted, and could misfire the delete-safety check. Replaced with a reactive, single-shot retry: a normal run is always attempted first; only when rclone's captured log reports it can't find a prior baseline (`cannot find prior Path1 or Path2 listings`) does the daemon retry once with `resync: true` (`JobState::bisync_force_resync` / `bisync_resync_retried`). Other bisync aborts (e.g. empty-directory, too-many-deletes) are left alone rather than auto-"fixed"
- **`JobStatus` now captures rclone's raw job log**: `job/status` only ever surfaced the generic top-level `error` string, which for bisync is the same `"bisync aborted"` for several unrelated causes (missing baseline, empty directory, too many deletes) — indistinguishable without more detail. `JobStatus` gained an `output_log` field populated from the RC response's `output.output` text, used to tell these cases apart reliably
- **`maxDelete` ignored by rclone's RC handler**: bisync's delete-safety check aborted on *any* deletion, even a single file — rclone's `sync/bisync` RC endpoint disregards the `maxDelete` parameter entirely (verified with values 50 and 100: no effect) and behaves as if the threshold were 0%, unlike the CLI's documented 50% default. Since there's no way to get a real percentage threshold over RC, added a per-job opt-in **Force Deletes** switch (bisync jobs only, default off, `Job::bisync_force_deletes`) that sends `force: true` to bypass the check — off by default so a single deletion still blocks the sync for review, on to let deletions of any size propagate with no protection against a large accidental deletion
- **Quickstart guide**: added `QUICKSTART.md` — a condensed five-minute setup path (add a remote, browse-and-prefill a job, job types at a glance, run/schedule, bisync gotchas) that links out to `User_Manual.md` for full reference detail
- **About tab version bump**: the version shown on the About tab was a hardcoded literal, independent of `CMakeLists.txt`'s `PROJECT_VERSION` (which only feeds the CPack package filename) and had drifted stale at `0.9.13`; updated to `0.9.14-pre`
- **Browse tab: no feedback on remote connection failure**: switching the Browse tab's remote dropdown to an unreachable remote left the file pane stuck on the loading spinner — `RcloneCli::lsjson` had no timeout and relied on rclone's own defaults (60s `contimeout` × 10 low-level-retries), and once it did fail, `BrowserPane::navigate()` funnelled the error into the same `"empty"` state page as a genuinely empty folder, discarding the error text entirely. `lsjson` now passes `--contimeout 10s --timeout 15s --low-level-retries 1` so a hung remote fails within seconds, and a new `"error"` content-stack page (`AdwStatusPage`, "Unable to Connect") shows rclone's actual error message; the Refresh button stays enabled on this page so the user can retry

## 0.9.13 — Tray "Open" Fix

- **Tray "Open" required two clicks when "Start minimized" was enabled**: the GUI process spawned by the tray honoured the `start_minimized` setting and started hidden, so the first click launched an invisible window and only the second click (routed via IPC `show_window` to the now-connected client) presented it — the daemon now spawns the GUI with a new `--show` flag that overrides `start_minimized` on first activation, so an explicit tray click always shows the window immediately; `start_minimized` still applies to plain `mtsync` launches such as login autostart
- **Daemon/GUI mutual spawning prefers `/proc/self/exe` over PATH lookup**: both the daemon launching a GUI and the GUI launching a daemon previously resolved `mtsync` via `Glib::find_program_in_path` first, so a development build would silently launch a stale system-installed binary; the running executable's own path is now preferred, with PATH lookup as fallback

## 0.9.12 - Change of tagline

 - **Changed tagline**: Changed the tagline from "Mount or sync network storage in comfort" to "Mount or sync network storage anywhere"

## 0.9.11 — Race Condition & Safety Fixes

- **Null deref on daemon shutdown with in-flight jobs**: `done_cb` now returns immediately when `m_running` is false — previously, stopping the daemon while a sync/copy/move submission was in-flight caused libsoup to fire the completion callback after `m_tray` and `m_ipc_server` had already been reset to nullptr, crashing the daemon
- **Use-after-free in CompareDialog on WM close**: an `m_alive` shared-ptr sentinel is set to false in a `signal_close_request` handler; action-button async callbacks (`delete_files`, `copy_files`) capture a `weak_ptr` and bail out if the dialog was destroyed before the operation completed — previously only the cancel button set the cancellation flag, leaving WM-close unguarded
- **Use-after-free in BackendsView idle callbacks**: `populate()` now creates a new `m_populate_token` on each call, invalidating all `weak_ptr` copies held by in-flight `get_about` callbacks and their posted idle closures — previously a theme change or second `refresh()` call could destroy widget rows while a prior round's idle callbacks still held raw `Gtk::Label*`/`Gtk::ProgressBar*` pointers
- **Timer leak in `schedule_job` and retry path**: `sched_timer.disconnect()` is now called before overwriting the connection in `schedule_job()`; `retry_timer.disconnect()` likewise before the retry assignment in `on_job_completed()` — previously the old GLib timeout source survived until its next fire, potentially triggering an unintended duplicate job start
- **Integer overflow (UB) in retry backoff**: the left-shift `2000u << (retry_count - 1)` is now clamped to a maximum shift of 4 before the shift executes — previously a user-configured retry count ≥ 33 caused a shift of ≥ 32 on an unsigned 32-bit value, which is undefined behaviour in C++
- **`notify-send` / `kdialog` blocking the GLib main loop**: both `Glib::spawn_sync` calls in `notification.cpp` replaced with `Glib::spawn_async` — the previous blocking spawn suspended all IPC message processing, timer callbacks, and HTTP response callbacks for the duration of the notification helper process
- **Unchecked `fcntl` return values**: `F_GETFL` return value is now tested before `F_SETFL` in `ipc/client.cpp` and both call-sites in `ipc/server.cpp` — a failure returned -1 which, OR-ed with `O_NONBLOCK`, set arbitrary flags on the file descriptor and could leave the socket in blocking mode
- **`g_signal_connect` with no disconnect in SettingsView**: added `~SettingsView()` destructor that calls `g_signal_handler_disconnect` on all 12 stored handler IDs; IDs are now captured from the `g_signal_connect` return values at setup time, matching the pattern already used in `BackendsView`

## 0.9.10 — Animated About Logo

- **Animated logo on About tab**: the static application icon is replaced with the 16-frame Mt. Sync smoke animation — frames are loaded from a 4096×256 horizontal spritesheet embedded in GLib resources and rendered via a `Gtk::DrawingArea` using Cairo; the animation runs at 80 ms per frame (matching the original APNG timing) and starts/stops automatically when the About tab is shown or hidden

## 0.9.9 — Toggle Tooltips

- **Verbose tooltips on all toggle switches**: all 12 `AdwSwitchRow` toggles in the Settings tab and Job Edit dialog now have descriptive tooltips — Settings toggles (On Job Start, On Completion, On Completion with Errors/Warnings, Start daemon on login, Start minimized to tray, Shutdown daemon when closing, Verify checksums) and Job Edit toggles (Dry Run, Bi-directional sync, Enable Checksum, Mount at Start-up, Enable Schedule) each explain what the toggle does and any relevant consequences of changing it

## 0.9.8 — Button Tooltips

- **Verbose tooltips on all buttons**: every interactive button in the GUI now has a descriptive tooltip — 23 buttons that previously had none (job run/stop/edit/delete/expand, add job, add remote, edit/delete remote, authorize OAuth, save/cancel in all dialogs, cron field clear, compare pagination, log file open) have been annotated, and 20 existing short tooltips (Back, Up, Refresh, Copy, Move, Sync, Mount, Swap, Delete, Compare, New Folder, and all Compare dialog filter/action buttons) have been rewritten as full sentences describing what the button does and any destructive consequences

## 0.9.7 — Performance Improvements Sprint 3

- **Release builds use `-O3 -flto`**: `CMakeLists.txt` now enables `-O3` and link-time optimisation for `Release` builds via `target_compile_options` / `target_link_options` guards — previously Release builds relied on CMake's default `-O2` with no LTO
- **Shutdown `waitpid` poll granularity reduced from 50 ms to 5 ms**: the SIGTERM → SIGKILL escalation loop in `stop_daemon()` previously slept 50 ms between each `waitpid(WNOHANG)` check, adding up to 50 ms of unnecessary latency after the process has already exited; the interval is now 5 ms so a cooperative rclone exit is detected within one tick rather than up to ten
- **Mount-point membership check: `std::set` → `std::unordered_set`**: the `live_mounts` set constructed on each 60-second mount health-check tick now uses `std::unordered_set<std::string>` — `.count()` lookups drop from O(log n) to O(1) average; `#include <set>` replaced with `<unordered_set>`

## 0.9.6 — Performance Improvements Sprint 2

- **HTTP session timeout reduced from 15 s to 5 s**: all rclone RC requests now time out after 5 seconds — the previous 15-second limit meant a hung localhost rclone could stall 10 concurrent poll callbacks for 15 s each before the async queue drained; 5 s is sufficient for a local loopback call
- **Mount health timer skips HTTP call when no mounts are active**: the 60-second `list_mounts` liveness check now returns early if no jobs of type Mount are currently active, avoiding a pointless HTTP round-trip every minute during normal sync-only use
- **Cron `day_of_week` computed once per day**: the next-occurrence search previously called `day_of_week()` on every iteration of the up-to-2.1 M step minute loop — the result is now cached and recomputed only when the date changes, reducing calls by up to 1440× per day
- **Compare dialog pagination: O(3n) → O(n) per filter change**: `show_page`, `update_pagination_controls`, and `apply_filters` each scanned `m_all_rows` independently; the filter+strip pass is now computed once in `rebuild_filter_cache()` into a `m_filtered_rows` cache that all three callers read, eliminating two redundant full-list traversals per filter toggle or page navigation
- **Compare dialog status column: CSS class swap guarded**: the status-column bind callback previously removed all five CSS classes unconditionally on every scroll event; it now stores the current class via `g_object_set_data` and only removes the old class and adds the new one when they differ
- **Browser pane name column: dynamic casts replaced and CSS swap guarded**: the name-column bind callback previously traversed the widget tree with two `dynamic_cast` calls and removed 12 CSS classes unconditionally on every scroll event; widget pointers are now stored in the setup callback via `g_object_set_data` and retrieved in O(1) in the bind callback, and the icon CSS class is only swapped when it changes

## 0.9.5 — Performance Improvements Sprint 1

- **Parallel job-state vectors consolidated into a struct**: eight separate `std::vector`s (`m_poll_timers`, `m_sched_timers`, `m_retry_timers`, `m_job_ids`, `m_job_submitting`, `m_poll_in_flight`, `m_retry_counts`, `m_last_stats`) replaced by a single `std::vector<JobState>` — job deletion now performs one `O(n)` erase instead of eight, all per-job state is cache-locally contiguous, and independent resize calls scattered across the daemon are eliminated
- **Redundant `get_stats` HTTP call on job completion removed**: `on_job_completed` previously fired a second `core/stats` HTTP round-trip to fetch final transfer counts despite the poll loop already caching them in `m_last_stats` (now `JobState::last_stats`) at most 500 ms earlier; completion now uses the cached value directly, eliminating one RC call per job lifecycle
- **`job_updated` no longer rebuilds the entire job list**: the `job_updated` daemon broadcast previously called `rebuild_ui()` which disconnected all timers, removed every row widget, and recreated the full list; it now removes and recreates only the single affected row — `O(1)` widget operations instead of `O(n)`
- **Activity log not re-read on job start**: `refresh_log()` was called on `job_started` in addition to `job_completed` and `job_stopped`; the log file does not change meaningfully mid-transfer so the disk read on start is removed — log view refreshes only on terminal job events

## 0.9.4 — Job Counter & Scheduler Fixes

- **`m_running_job_count` leak on retries**: each retry attempt incremented the counter in `on_run_job` without a corresponding decrement from the previous failed run — `on_job_completed` returned early via the retry path before reaching the decrement, so after N retries the counter was inflated by N, keeping the tray animation spinning indefinitely even when no jobs were active; the decrement now fires immediately before the retry return so the counter is balanced and the retry re-increments when it actually starts
- **`schedule_job` timer missing UUID guard**: the scheduled-run timer lambda captured only the job index with no UUID check — every other timer in the daemon (poll, retry) guards against index reuse after a job deletion; the schedule timer now captures `sched_uuid` and returns early if `m_jobs[index].id` no longer matches, consistent with the rest of the codebase

## 0.9.3 — Race Condition & Lifetime Fixes

- **`ensure_daemon` use-after-free**: the `core/version` probe in `ensure_daemon` captured `this` in its `rc_post` callback with no cancellation path — `stop_daemon()` already guarded the startup-verification callback via `m_verify_cancelled` but left this earlier probe unguarded; added a parallel `m_ensure_cancelled` sentinel that `stop_daemon()` sets before releasing the libsoup session, preventing the callback from touching a destroyed `RcloneRc`
- **Poll in-flight flag cleared for wrong job**: the `job_status` callback reset `m_poll_in_flight[index]` before the UUID/bounds guard — if a job deletion shifted a different job into that index, the flag was cleared for the new occupant, causing its next poll tick to fire a duplicate HTTP request; the clear is now ordered after the guard so it only executes when the index still refers to the originating job
- **`std::vector<bool>` guard vectors**: `m_job_submitting` and `m_poll_in_flight` were `std::vector<bool>`, which packs bits and returns proxy objects instead of real references — changed to `std::vector<uint8_t>` to give elements stable addresses and avoid the proxy trap if a reference is ever held across a resize

## 0.9.2 — Safe Shutdown & Signal Handling

- **SIGTERM/SIGINT now stop the daemon**: signal handling switched from `std::signal()` to `g_unix_signal_add()` — on Linux, `std::signal()` installs handlers with `SA_RESTART`, which causes the blocking `g_main_context_iteration` call to be transparently restarted, making the daemon unresponsive to `kill` and `systemctl stop`; the GLib-native approach delivers the signal as a main-loop callback that calls `stop()` cleanly
- **Startup verification timer tracked and cancellable**: the 1500 ms rclone rcd startup probe timer now stores its GLib source ID and uses `g_timeout_add_full` with a `GDestroyNotify` so the heap-allocated callback data is freed whether the timer fires or is cancelled; a `shared_ptr<bool>` sentinel prevents the in-flight `rc_post` callback from touching a destroyed `RcloneRc`
- **Child watch source ID stored and removed**: `g_child_watch_add` return value is now saved in `m_child_watch_id` and removed via `g_source_remove` in `stop_daemon()` before sending SIGTERM, preventing a dangling-`this` write after `RcloneRc` is destroyed
- **Shutdown timeout reduced from 5 s to 1 s**: the SIGTERM→SIGKILL escalation polling loop in `stop_daemon()` now waits at most 1 second before escalating; SIGKILL guarantees fast process exit so there is no benefit to the previous 5-second maximum

## 0.9.1 — Network Fault Tolerance

- **HTTP request timeout**: all rclone RC HTTP requests now have a 15-second timeout — previously requests could hang indefinitely on an unresponsive network, causing unbounded memory growth in the libsoup session as the 500 ms poll timer queued new requests each tick
- **Exponential retry backoff**: failed jobs with retries enabled now wait 2 s, 4 s, 8 s, … (capped at 60 s) between attempts — previously retries fired immediately, creating a tight CPU-burning loop when the network was down
- **Poll in-flight guard**: the 500 ms job-status poll now skips a tick if the previous HTTP request is still pending — prevents request pile-up during slow or hanging network conditions
- **Mount liveness monitoring**: a 60-second periodic health check via rclone RC `list_mounts` now detects mounts that silently died (network loss, rclone crash) and marks them inactive — previously dead FUSE mount points stayed marked active, risking uninterruptible-sleep hangs when accessed
- **Parallel vector erase fix**: `m_retry_counts` is now erased alongside other parallel vectors on job deletion — previously the missing erase caused index drift after deleting a job that had retried

## 0.9.0 — Jobs Tab Grouped by Type

- **Job type grouping**: the Jobs tab now organises jobs into four `AdwPreferencesGroup` sections — **Sync**, **Copy**, **Move**, and **Mount** — in a fixed order below the "Jobs" header; sections with no jobs are hidden automatically

## 0.8.14 — Stopped Job Logging & Browse Nav Polish

- **Stopped job activity log**: manually stopping a job now writes a `STOPPED` entry to the activity log for both mount and non-mount jobs — previously the log showed `STARTED` with no corresponding terminal entry; `STOPPED` is displayed in purple to distinguish it from `COMPLETED` (green) and `FAILED` (red)
- **Compact browse nav buttons**: Back and Up buttons on the Browse tab now use the `circular` CSS class for a tighter, icon-button appearance

## 0.8.13 — Tray Animation & Package Description

- **16-frame sprite sheet**: tray icon animation now uses the 16-frame sprite sheet (`MtSync_smoke_spritesheet_16frames_47px.png`) — `ANIM_FRAMES` increased from 8 to 16 and `ANIM_INTERVAL_MS` set to 100 ms for a smooth 50 fps cycle
- **DEB package description**: `CPACK_PACKAGE_DESCRIPTION_SUMMARY` updated to "Mount or sync network storage in comfort"

## 0.8.12 — Performance Improvements

- **Settings loaded once per job**: `on_run_job` and `on_job_completed` now call `load_settings()` once and reuse the result — previously `settings.json` was parsed 3–5× per job cycle (notify, parallel transfers, retries, notify on completion, notify on errors)
- **IPC buffer O(n²) → O(1)**: `read_from_client` (server) and `read_message` (client) now track a read offset instead of calling `buffer.erase(0, n)` after each message — a burst of N messages went from O(N²) total bytes shifted to a single compaction at the end of each read call
- **Incremental job list updates**: `job_added`, `job_updated`, and `job_deleted` daemon messages no longer trigger a disk read (`load_jobs`) before rebuilding the UI — job data is now included in the daemon broadcast payload and applied in-place; `job_added` appends a single row without tearing down and recreating the full list
- **JSON serialised once in `send_to_all`**: previously each connected GUI client caused a separate `msg.dump()` call; now the JSON is serialised once and the string is sent to each client
- **Zero-copy IPC send**: `send_to` (server) and `send_message` (client) now issue two `write_all` calls (header then body) instead of building a concatenated packet string, eliminating one full payload copy per message
- **Sequential poll calls**: `get_stats` now fires only after `job_status` confirms the job is still running — on completion, the redundant stats HTTP round-trip is skipped entirely; `get_stats` results are unchanged for running jobs
- **Tray icon pixmap in one allocation**: `idle_icon_pixmap()` and `frame_pixmap()` now use `g_variant_new_fixed_array` to build the byte array in a single allocation — previously ~1936 individual `g_variant_builder_add` calls fired per animation frame (47×47×4 bytes at 100 ms intervals)
- **Reduced `Job` copies on add**: `add_job` and `add_job_no_run` now move the job into `m_jobs` and pass `m_jobs.back()` to the daemon, reducing three copies to one move plus one copy

## 0.8.11 — Sprite Sheet Tray Icon Animation
- **Sprite sheet animation**: tray icon animation now uses the artist-created 8-frame sprite sheet instead of a procedurally generated Cairo spinner overlay — idle state shows frame 1; active jobs cycle through all 8 frames at 100 ms per frame
- **Icon resolution**: `ICON_SIZE` increased from 22 to 47 px to match the sprite dimensions; the system tray compositor scales as needed, preserving crispness at HiDPI
- **Spritesheet resource**: `MtSync_smoke_spritesheet_8frames_47px.png` embedded via GLib resources at `/io/github/mtsync/icons/spritesheet.png`

## 0.8.10 — Error Log Files & Failure Reporting
- **Error log files**: failed sync/copy/move/mount jobs now write a structured log to `~/.local/state/mtsync/errors/<source>-<timestamp>.log` containing the timestamp, job ID, type, source, destination, and rclone error message
- **Clickable log link**: the activity log shows a `document-open` icon button on failed entries; clicking it opens the error log file in the system default text viewer
- **Activity log on submission failure**: jobs that fail before receiving an rclone RC job ID (e.g. invalid remote, daemon unreachable) now always write a `FAILED` activity log entry — previously these failures were silent
- **Actual rclone errors surfaced**: `rc_post` now checks the HTTP status code and extracts rclone's `"error"` field from non-200 responses; callers previously received a generic "No jobid in response" message instead of the real error
- **Duration flag coercion**: `inject_flags` now converts Go duration strings (e.g. `--timeout 5m`, `--contimeout 30s`) to nanoseconds before placing them in the `_config` block — rclone's `time.Duration` fields reject string input and previously caused all jobs to fail with a Reshape unmarshal error; integer flags are also stored as JSON numbers rather than strings
- **FAILED state in activity log**: failed jobs now log with state `FAILED` (red) instead of `COMPLETED` (green); the redundant "FAILED --" prefix is removed from the contents column since the state column already indicates failure

## 0.8.9 — Code Review Fixes
- **Double-free in schedule preview**: `update_preview` now assigns `cursor = nxt` before the null check so the post-loop `g_date_time_unref` never fires on an already-freed pointer
- **Probe socket failure on bind**: `IpcServer::start` now returns `false` when the liveness-probe `socket()` call itself fails, instead of falling through to `::unlink` a potentially live daemon's socket
- **Double settings file read**: `on_run_job` now loads `global_rclone_flags` once into a local before calling `inject_flags`, eliminating a redundant `settings.json` parse
- **Duplicate quit handling**: a `m_quit_pending` flag prevents multiple simultaneous `"quit"` IPC messages from each queuing a separate deferred `stop()` call
- **Duplicate build-job logic**: `on_commit` and `on_save` now delegate to a shared `build_job()` helper; removed the dead `m_advanced_row` field left over from the pre-tab layout

## 0.8.8 — IPC & Daemon Safety Fixes
- **IPC send reliability**: `::send()` return values are now checked on both server and client paths; a `write_all()` loop with `MSG_NOSIGNAL` retries on `EINTR` and handles partial writes — previously a failed or partial send was silently dropped
- **Outbound message size guard**: both server and client now reject outbound messages larger than 1 MB before the `uint32_t` length cast, preventing silent truncation on the wire
- **Socket TOCTOU on bind**: removed the `fs::exists()` + `fs::remove()` pre-check before `bind()` — server now calls `bind()` directly; on `EADDRINUSE` it probes with `connect()` to distinguish a live daemon (returns an error) from a stale socket (unlinks and retries bind), eliminating the check-then-act race window; `stop()` uses `::unlink()` directly instead of an existence check
- **Startup callback liveness guard**: `ensure_daemon` and `list_mounts` async callbacks now check `m_running` on entry — if the daemon was stopped before the HTTP response arrived, the callbacks return immediately rather than accessing freed `m_jobs` state or a reset `m_ipc_server`
- **`EWOULDBLOCK` alongside `EAGAIN`**: both `read_from_client` (server) and `read_message` (client) now treat `EWOULDBLOCK` as a non-fatal would-block condition, matching POSIX allowance for the two values to differ

## 0.8.7 — Correctness & Safety Fixes
- **Atomic config writes**: `jobs.json` and `settings.json` are now written to a `.tmp` sibling file and renamed into place — a crash or I/O error during a save no longer corrupts the existing file
- **Config read TOCTOU**: removed `fs::exists()` pre-checks before opening config files; file is now opened directly and a missing file is treated as an empty config, eliminating the check-then-open race window
- **Use-after-free on IPC quit**: `stop()` was previously called synchronously from inside `IpcServer::read_from_client()`, destroying the server's iterator mid-execution; it is now deferred to the next event-loop tick via `Glib::signal_idle()`
- **Stale index in async callbacks**: every async callback that captures a job `index` now also captures the job's UUID (`job.id`) and verifies the match at callback entry — prevents a deleted job from shifting vector indices and causing a different job's state to be overwritten by an in-flight HTTP response
- **Running-job counter on delete**: deleting a job that is actively running now immediately decrements `m_running_job_count` and updates the tray animation; previously the counter could stay elevated until an orphaned callback eventually fired
- **Pending IPC callbacks on disconnect**: `DaemonProxy::disconnect()` now drains `m_pending_callbacks` by invoking each with a `"disconnected"` error response before clearing the map, so callers receive a proper error instead of silently hanging

## 0.8.6 — Mount Cache Mode Default
- VFS Cache Mode for new Mount jobs now defaults to **minimal** instead of `off`
- Existing jobs load their saved cache mode unchanged

## 0.8.5 — Per-Job Extra rclone Flags
- Added **Extra rclone Flags** field to the Advanced tab of the add/edit job dialog
- Flags are parsed at run time and injected into the rclone RC `_config` block alongside the job's other options (e.g. `--min-size 10M --max-age 7d --checkers 16`)
- Supports `--flag value`, `--flag=value`, and boolean `--flag` forms; flag names are converted from CLI kebab-case to rclone's internal PascalCase automatically
- Persisted as `extra_flags` in `jobs.json`
- Priority order: explicit job fields (bandwidth, dry-run, etc.) > per-job extra flags > global rclone flags from Settings

## 0.8.4 — Schedule Tab Cron Builder & Preview

- Schedule tab rebuilt as a two-column layout: **cron editor** on the left, **live preview** on the right
- **Presets** dropdown (Every minute, Hourly, Daily, Weekly, Monthly, Custom) — selecting a preset fills all fields instantly; editing any field switches the preset to Custom automatically
- **Minutes / Hours** group replaces the old single-line minute/hour rows; each field has a clear (×) button that resets it to `*`
- **Days** group: Day of Month text entry + **Day of Week** checkbox row (Sun Mon Tue Wed Thu Fri Sat) — all checked = `*`; partial selection generates a comma-separated cron value
- **Months** group: twelve checkboxes (Jan–Dec) in two rows — same all/partial logic
- **Schedule Preview** panel shows the live cron expression, a human-readable description, a `Gtk::Calendar` with run days marked for the displayed month, the system timezone, and a scrollable list of the next 15 upcoming execution times
- Calendar marks update when navigating months; upcoming list rebuilds on every field change
- Dialog widened to 840 px; Job and Advanced tab content remains centred via AdwClamp(520)
- All existing save/load paths updated to use the new checkbox-derived cron fields

## 0.8.3 — Job Dialog Tabbed Layout
- Add/edit job dialog reorganised into three tabs: **Job** (`document-edit-symbolic`), **Schedule** (`alarm-symbolic`), and **Advanced** (`preferences-other-symbolic`)
- **Job tab**: type, source, destination, file filters, dry run, bi-directional sync, checksum, mount options
- **Schedule tab**: enable schedule switch, repeat schedule fields (Minute, Hour, Day of month, Month, Day of week), and live schedule summary — fields are always visible, no expand/collapse
- **Advanced tab**: bandwidth limit, parallel transfers, retries on failure — previously hidden inside a collapsible expander on the Job tab
- Button row (Run Now / Schedule, Save, Cancel) sits outside the tab stack so it is always accessible

## 0.8.2 — Schedule UI Improvements
- Scheduling section group title renamed from "Schedule (cron)" to "Repeat Schedule"
- Added group description explaining `*`, ranges (`1–5`), lists (`1,3,5`), and steps (`*/2`)
- "Day" field renamed to "Day of month" to distinguish it from "Day of week"
- "Weekday" field renamed to "Day of week"
- Placeholders updated to show practical examples (`0–59, */5, or *`; `0 (Sun) to 6 (Sat) or *`)

## 0.8.1 — Distribution Name in Package Filenames
- DEB filenames now include the target Ubuntu version (e.g. `mtsync_0.8.1_ubuntu24.04_x86_64.deb`)
- RPM filename now includes the target Fedora version, read from `/etc/os-release` at build time (e.g. `mtsync_0.8.1_fedora41_x86_64.rpm`)

## 0.8.0 — Consistent Package Filename Format
- All build artifacts now follow the `name_version_arch.ext` format with underscores as separators
- DEB/RPM: changed `CPACK_PACKAGE_FILE_NAME` separator from `-` to `_` (e.g. `mtsync_0.8.0_x86_64.deb`)
- AppImage: added `VERSION` env var to linuxdeploy step so the version is embedded in the filename; output renamed to underscore format (e.g. `mtsync_0.8.0_x86_64.AppImage`)
- Flatpak: bundle name is now computed from `CMakeLists.txt` at build time (e.g. `mtsync_0.8.0_x86_64.flatpak`)
- Snap was already in the correct format (`mtsync_0.8.0_amd64.snap`) — no change

## 0.7.12 — Dead Code Removal
- Removed `MtSyncWindow::show_toast()` — declared and defined but never called
- Removed `class Notification` wrapper — never instantiated; free `send_notification()` function is still used
- Removed 10 unused `adw_wrapper.hpp` helpers: `toolbar_view_add_bottom_bar`, the entire `AdwNavigationSplitView` block (`navigation_split_view_new/widget/set_sidebar/set_content/set_show_content/set_min_sidebar_width/set_sidebar_width_fraction`), and `header_bar_pack_start/end`
- Removed unread struct fields: `AboutInfo::{trashed,other,objects}`, `SyncStats::{checks,total_checks}`, `JobStatus::duration` — all were parsed from rclone responses but never read by any caller
- Deleted stale `Saddle.code-workspace` left over from the 0.7.0 project rename

## 0.7.11 — Sandbox Autostart, Snap Local Browse & App Icon Fixes
- Added `src/sandbox.hpp` — runtime sandbox detection (`in_flatpak()`, `in_snap()`) plus `real_home()`, `real_config_dir()` and `autostart_exec()` helpers that bypass the sandbox's XDG redirection and emit the correct launcher command for the host session
- **Flatpak – autostart**: `write_autostart()` now writes to the host's real `~/.config/autostart/` (via `--filesystem=home`, not the redirected `$XDG_CONFIG_HOME`) and emits `Exec=flatpak run --command=mtsync com.mtsync.MtSync --daemon` so the host session can launch the daemon on login
- **Snap – local home**: Browse → Local now resolves `$SNAP_REAL_HOME` instead of `$HOME` (which Snap points at the empty `$SNAP_USER_DATA` dir), so the user's real home directory contents are listed
- **Snap – autostart**: Added `dot-config-autostart` `personal-files` plug (`write: [$HOME/.config/autostart]`) because the `home` plug excludes hidden directories; autostart `.desktop` now emits `Exec=/snap/bin/mtsync --daemon`. Sideloaded `.snap` installs must run `sudo snap connect mtsync:dot-config-autostart :personal-files` once
- **Snap – app icon**: `override-prime` now copies the 256×256 PNG to `meta/gui/icon.png` so snapd launchers and GNOME Shell resolve the app icon at install time

## 0.7.10 — Provider Dropdown Filtering & Sorting
- Removed unsupported internal rclone backends (Memory, Alias, Union, Cache, Crypt) from the Add/Edit Remote provider dropdown
- Provider list is now sorted alphabetically

## 0.7.9 — Global rclone Flags Setting
- Added **Global rclone flags** field to Settings → rclone group
- Flags are parsed at job execution time and injected into every rclone RC call via the `_config` block (e.g. `--log-level DEBUG --checkers 8 --max-age 24h`)
- Flag names are converted from CLI kebab-case to rclone's internal PascalCase automatically; supports `--flag value`, `--flag=value`, and boolean `--flag` forms
- Per-job settings take precedence — global flags do not overwrite explicitly configured job options

## 0.7.8 — Flatpak & Snap Packaging Fixes
- **Flatpak – rclone config**: `RcloneCli` and `RcloneRc` now pass `--config ~/.config/rclone/rclone.conf` explicitly to every rclone invocation; Flatpak redirects `$XDG_CONFIG_HOME` to a per-app directory, causing rclone to see an empty config with no remotes
- **Flatpak – tray icon**: Added `--own-name=com.mtsync.Daemon` to `finish-args`; without it the D-Bus name acquisition fails silently inside the sandbox and the SNI registration never fires
- **Flatpak – app icon**: Added `gtk-update-icon-cache` post-install step to the Flatpak module; without it the hicolor theme cache inside the sandbox is stale and GNOME Shell cannot resolve the app icon
- **Desktop file**: Corrected `StartupWMClass` from `mtsync` to `com.mtsync.MtSync` to match the GTK application ID used as the Wayland xdg_toplevel app_id
- **Snap**: Added `desktop:` key to the `apps.mtsync` declaration for correct icon and title integration; local `.snap` bundles must be installed with `sudo snap install --dangerous mtsync_*.snap`

## 0.7.7 — Refined Application Icon
- Removed background colour and border from icon

## 0.7.6 — DEB gtkmm Dependency Fix
- Fixed `CPACK_DEBIAN_PACKAGE_DEPENDS`: gtkmm runtime package name is now detected at configure time via `dpkg -S` on the build host, so each matrix DEB correctly declares the package name that exists on its target Ubuntu release (e.g. `libgtkmm-4.0-1t64` on 24.04/25.10 rather than the hardcoded `libgtkmm-4.0-1v5`)

## 0.7.5 — DEB Multi-Distro Matrix
- DEB build now runs as a matrix across Ubuntu 24.04, 25.10, and 26.04 using native OS containers, so CPack auto-detects the correct runtime library names (e.g. `libgtkmm-4.0-1t64` on newer releases) for each target instead of hard-coding the build host's soname

## 0.7.4 — About Tab & Build Fixes
- About tab subtitle ("Mount or sync network storage in comfort") is now **bold**
- Fixed `CMakeLists.txt` install path: stale `Saddle App Icon 1.svg` corrected to `MtSync App Icon 1.svg`, fixing DEB/RPM/AppImage packaging

## 0.7.3 — CI/CD Rename Fix
- Fixed GitHub Actions workflow: updated all stale `saddle` artifact names, Flatpak bundle filename, and Flatpak manifest path to `mtsync` following the 0.7.0 project rename

## 0.7.2 — User Manual
- Added `User_Manual.md` covering remote setup, all four job types (Copy, Move, Sync, Mount), job options, scheduling, the browser's pane-prefill workflow, the Compare dialog, and Settings

## 0.7.1 — Compare Dialog Filter Toggle Logic
- Added `=` toggle to show/hide identical files, positioned between `→` and `≠`
- Filter toggles (←, →, =, ≠, !) now default to **on** and **show** their category when active (previously defaulted off and hid when active)
- Deactivating a toggle hides that category; all rows visible by default on dialog open
- Updated tooltips from "Hide…" to "Show…"

## 0.7.0 — Project Rename: Saddle → Mt. Sync
- Project renamed from "Saddle" to "Mt. Sync" (`mtsync`)
- Executable renamed from `saddle` to `mtsync`
- Application ID changed from `com.saddle.Saddle` to `com.mtsync.MtSync`
- D-Bus service renamed from `com.saddle.Daemon` to `com.mtsync.Daemon`
- GLib resource prefix changed from `/io/github/saddle` to `/io/github/mtsync`
- Config directory changed from `~/.config/saddle/` to `~/.config/mtsync/`
- Cache directory changed from `~/.cache/saddle/` to `~/.cache/mtsync/`
- Log file changed from `~/.local/state/saddle/saddle.log` to `~/.local/state/mtsync/mtsync.log`
- All C++ namespaces renamed from `saddle` to `mtsync`; class prefix changed from `Saddle` to `MtSync`
- New Mt. Sync branded application icon displayed on the About tab
- New Mt. Sync branded system tray icon; used as the background of the animated spinner overlay during active jobs

## 0.6.20 — About Tab rclone Info
- About tab now shows a "rclone" section with three rows: Version (fetched async via `rclone version --json` on first visit), Socket (IPC socket path, static), and Status (Connected/Disconnected, refreshed on each visit)
- Added `RcloneCli::get_version()` method

## 0.6.19 — Provider Dropdown Fix
- Fixed provider dropdown not showing the current provider when editing a remote
- Fixed "Failed to wrap object of type 'GObject'" warning when selecting a provider — `ProviderItem` was missing `Glib::ObjectBase("MtSyncProviderItem")` registration, causing the GType system to treat every instance as a plain GObject and `dynamic_pointer_cast` to return null

## 0.6.18 — Collapsible Job Details
- Job rows in the Jobs tab now collapse the UUID and full source/destination paths behind a disclosure chevron button, saving two lines per row in the default view
- Chevron is positioned to the right of the delete button; clicking expands/collapses with a slide animation and flips the icon between `pan-end` and `pan-down`
- Rows start collapsed; state resets on rebuild (add/edit/delete)

## 0.6.17 — Provider Dropdown Icons
- Provider dropdown in the Add/Edit Remote dialog now shows branded SVG icons alongside each provider name
- Icons render at 20px with automatic light/dark theme variants via `AdwStyleManager`
- Providers without custom icons fall back to the symbolic `network-server` icon
- Replaced plain `GtkStringList` model with `Gio::ListStore<ProviderItem>` and custom `Gtk::SignalListItemFactory` for rich icon+label rendering

## 0.6.16 — Remotes Tab Delete Button Colour
- Delete button on the Remotes tab is now red (`destructive-action` class) for visual consistency with the Jobs tab

## 0.6.15 — Auto-Mount Verification & Job Button Colours
- Daemon now verifies existing mount points at startup via `mount/listmounts` RC endpoint before auto-mounting
- Mount jobs whose destinations are already active are marked `running: true` / `active: true` without re-mounting
- Updated job list is broadcast to connected GUI clients so mount state displays correctly even if mounts survived from a previous session
- Play button in the Jobs tab is now green (`success` class), stop and delete buttons are red (`destructive-action` class)

## 0.6.14 — Extended Provider Icons
- Added provider icons for OneDrive, pCloud, Amazon S3, Azure Blob Storage, Yandex Disk, Mail.ru Cloud, Koofr, Jottacloud, put.io, and premiumize.me
- Each icon includes both light and dark theme variants for automatic dark mode support
- Icons appear in the Backends tab remote list and the file manager remote dropdown

## 0.6.13 — Defunct rclone Process & Job Skipping Fixes
- Replaced polling-based rclone rcd death detection with `g_child_watch_add()` for async SIGCHLD notification — zombies are now reaped immediately when the daemon exits
- `stop_daemon()` now uses a 5-second timeout with SIGTERM→SIGKILL escalation instead of blocking indefinitely on `waitpid()`, preventing shutdown hangs
- Adopted rclone rcd processes (from previous sessions) are now tracked with PID = -1 and stopped via `core/quit` HTTP call
- Spawn verification failure now properly terminates the half-started daemon and zeros the PID so subsequent retries succeed cleanly
- Fixed `is_daemon_running()` to recognise adopted daemons (PID ≠ 0 instead of PID > 0)
- Fixed jobs being permanently skipped after rclone rcd crashes or restarts mid-job: `job_status()` now correctly identifies rclone's RC error envelope (`{"error":"...","status":N}`) and returns it as a failure rather than parsing it as a still-running job; the poll timer now calls `on_job_completed()` on any error, clearing the stale job ID
- Fixed `m_running_job_count` leak and persistent `running:true` state when a job submission fails before receiving an RC job ID — cleanup is now performed inline rather than going through `on_job_completed()` which returned early on its guard
- `load_jobs()` now resets `running:false` for all job types on daemon startup, not just `active` for mount jobs

## 0.6.12 — Animated Tray Icon Overlay & Security Fixes
- Tray spinner animation frames are now composited over the idle icon rather than rendered on a transparent background
- The idle icon remains visible through the gaps between spinner dots while a job is running
- IPC Unix socket is now created with `0600` permissions, preventing other local users from connecting to the daemon
- IPC socket path length is now validated before use in both server and client; fails fast with a clear error if the path exceeds the kernel's 108-byte limit
- New Folder dialog rejects names containing `/` or `..` to prevent directory traversal outside the current location
- Fixed defunct rclone rcd process blocking subsequent operations after app restart: `stop_daemon()` now calls `waitpid()` so port 5571 is guaranteed free before returning; `ensure_daemon()` now probes the port before spawning and adopts any already-running rclone rcd rather than spawning a new one that would fail to bind
- Fixed file descriptor leak: `/dev/null` fd opened during rclone rcd spawn was not closed in the parent process

## 0.6.11 — Flatpak Build Fixes & App Icon fixes
- Fixed icon squareness rendering issue
- Added sigc++, glibmm, cairomm, pangomm, and gtkmm as explicit Flatpak manifest modules in correct dependency order (required because the Flatpak SDK does not include the C++ bindings)
- All meson modules now pass `--wrap-mode=nofallback` to prevent meson attempting GitHub clones (blocked by the Flatpak sandbox)
- All meson modules now pass `--libdir=lib` so `.pc` files land in `/app/lib/pkgconfig/` where pkg-config can find them
- Removed `-Dbuild-examples=false` from all five C++ dependency modules (not valid in their meson builds)
- Corrected pangomm SHA-256 hash in the manifest (extra leading zero caused hash mismatch)
- Fixed rclone binary install path: flatpak-builder strips one path component from archives so the binary lands at `rclone` directly

## 0.6.10 — CI Build Fixes
- Replaced `AdwSpinner` with a compile-time fallback to `GtkSpinner` on libadwaita < 1.6 (Ubuntu 24.04 ships 1.5)
- Flatpak CI job now installs `flatpak` and `flatpak-builder` before invoking the builder action
- Snap build fixed: removed `filesets` keyword (not valid in core24/snapcraft 8.x) and replaced `$rclone` fileset reference with a direct `prime` path
- RPM dependency corrected from `gtkmm40-devel` to `gtkmm4-devel` (Fedora package name)

## 0.6.9 — Compare Dialog Filter Toggles
- Four small toggle buttons (`←` `→` `≠` `!`) added to the centre of the Compare dialog action bar
- When a toggle is active, rows with that status are hidden from the results list
- Directory headers with no remaining visible children are suppressed automatically
- Pagination count updates to reflect only the visible (unfiltered) files
- Filter state persists across reloads triggered by copy or delete actions

## 0.6.8 — Remote Capacity Progress Bar
- Remotes tab now shows a graphical progress bar indicating storage usage for each remote
- Progress bar is positioned above the text usage stats (e.g. "4.9 GB of 20 GB")
- Bar renders crisply at 6px height with rounded corners and accent colour fill
- Capacity info is fetched asynchronously via `operations/about` endpoint; bar remains hidden if data unavailable

## 0.6.7 — Compare Dialog Directory Grouping
- Files in the Compare dialog now stay grouped with their directory header when sorting by any column
- Previously, clicking a column header caused all directory headers to float above all file rows, breaking grouping
- Sorting now orders by directory first, keeps the directory header above its files, then sorts within each group by the selected column

## 0.6.6 — Scalable Application Icon
- Scalable SVG application icon now installed alongside the 256×256 PNG to `share/icons/hicolor/scalable/apps/com.mtsync.MtSync.svg`
- Icon renders crisply at any size in launchers and taskbars that prefer vector icons

## 0.6.5 — Compare Dialog Column Sorting
- All seven columns in the Compare dialog now support interactive sorting by clicking column headers
- Click a column header to sort ascending/descending; sort direction indicated by arrow glyphs
- Directory header rows always sort to the top of their groups, preserving visual hierarchy
- Filename columns sort case-insensitively, size columns handle missing values correctly

## 0.6.4 — Compare Dialog Actions
- Action bar added below the source → destination path line; results list supports multi-select
- Left side: **Delete** (icon, removes selected files from source) and **Copy →** (copies selected from source to destination)
- Right side: **← Copy** (copies selected from destination to source) and **Delete** (icon, removes selected files from destination)
- After any action the comparison reruns automatically to reflect the updated state

## 0.6.3 — Compare Dialog Improvements
- Filename columns in the diff view now show "Filename" instead of "Source" / "Destination"
- Fixed duplicate `"/"` directory header appearing when root-level files and subdirectory files were both present — results are now sorted by directory first, then by path, ensuring the root group is always contiguous
- Loading page now shows a "Large scans can take a long time" hint below the spinner
- Cancel button on the loading page kills all running rclone subprocesses and closes the dialog immediately

## 0.6.2 — Compare Dialog Polish
- Corrected which side filename, size, and date are displayed on — `+` (rclone: in source only) now shows in the source column, `-` (in destination only) shows in the destination column
- Empty size and date cells are now truly blank instead of showing `--`

## 0.6.1 — Compare Dialog Refinements
- Results are now grouped by subdirectory, with a bold accent-coloured directory header row before each group
- Files within each group are sorted alphabetically by path
- Status symbols replaced with Unicode glyphs: `→` in source only, `←` in destination only, `≠` different content, `!` error, `=` identical
- Filename, file size, and modified date are shown only on the side where the file exists
- Size and date columns render at a smaller font size for visual hierarchy

## 0.6.0 — Compare Button & Dialog
- Added **Compare** button to the Browse tab action bar, to the left of Delete
- Clicking Compare opens a child window showing the differences between the left pane (source) and right pane (destination) via `rclone check --combined -`
- Results are displayed in a 7-column view: Source Filename, Source Size, Source Modified, Status, Destination Filename, Destination Size, Destination Modified
- Status column is colour-coded: `=` dim (identical), `-` red (missing from destination), `+` blue (extra in destination), `*` amber (different content), `!` bold red (check error)
- File metadata (size and modified date) is fetched recursively via `rclone lsjson -R` on both sides and cross-referenced with the check output
- Results are paginated at 50 items per page with Previous/Next navigation and a page count label
- A loading spinner is shown while the three parallel async operations complete

## 0.5.7 — Activity Log & Job List Improvements
- Fixed activity log reporting stale transfer counts on job completion — daemon now does a final `core/stats` query right before logging to capture the most up-to-date file count
- Resolved race condition where cached stats from the previous 500ms poll cycle missed the final batch of completed transfers
- Activity log column header renamed from "Contents" to "Activity"
- Job list footer now shows "Last run: status" left-aligned and transfer stats (files, size, speed) right-aligned on the same line
- Job UUID moved above the source/destination line for better visual hierarchy
- Source and destination paths combined into a single `source → destination` line

## 0.5.6 — Mount Job UI Improvements
- Mount jobs no longer show a progress bar — they display a stop button and "Mounted" status instead
- Progress bar and "Running..." status are now exclusive to non-mount jobs (Sync, Copy, Move)

## 0.5.5 — Activity Log Font Consistency
- Activity log columns now use a uniform monospace font at 0.8em across all fields (Time, State, Job ID, Type, Contents)

## 0.5.4 — Job Cancel Fix & Browse Button Icons
- Fixed cancelled jobs not clearing `running` state — daemon and GUI now set `running = false` and `last_status = "stopped"` when a job is stopped
- Delete and New Folder buttons on the Browse tab now show icons to the left of their labels

## 0.5.3 — Job View Refresh Fix & About Tab Update
- Fixed job list not reflecting running state when switching back to the Jobs tab — stop button, progress bar, and status label now correctly restore from persisted `running` flag
- Jobs now track `running` state in `jobs.json`, set by daemon on start and cleared on completion
- About tab now includes a lyric quote below the copyright section

## 0.5.2 — Custom Idle Tray Icon
- Replaced the system systray idle icon (`network-server-symbolic`) with a custom Mt. Sync-branded icon
- Custom icon is bundled as a GLib resource and rendered via Cairo (no external icon dependencies)
- Idle icon is scaled to 22×22 ARGB32 to match the busy animation frame dimensions
- Fixed tray icon not updating when animation stopped by emitting the standard D-Bus `PropertiesChanged` signal alongside `NewIcon`
- Icon now correctly transitions from busy spinner back to idle state

## 0.5.1 — Tab Styling Update
- Main navigation tabs (Browse, Jobs, Remotes, Settings, About) now show a deep blue background when selected
- Selected tab hover state uses a slightly lighter blue for visual feedback

## 0.5.0 — Notification Settings & Tray Menu Title
- Added **Notifications** section to the Settings tab with three independent toggles: On Job Start, On Completion, On Completion with Errors/Warnings
- All three default to off
- Settings tab general section renamed to **Start Up &amp; Shut Down** and moved below Notifications
- Notification toggles are applied at the daemon level via `load_settings()` at the point of dispatch
- System tray right-click menu now shows "Mt. Sync" as a non-clickable title above a separator, followed by Open and Quit

## 0.4.8 — Notification Fix
- Fixed desktop notifications falling back to a console `g_message` instead of displaying via `notify-send`
- `Glib::spawn_sync` now receives the resolved full executable path instead of the bare command name

## 0.4.7 — Activity Log Column View
- Activity log now displays as a structured column view with five columns: Time, State, Job ID, Type, and Contents
- State column is colour-coded: blue for STARTED, green for COMPLETED, orange for SKIPPED/RETRYING
- Log lines are parsed from the existing log file format; unparsed lines fall back to the Contents column

## 0.4.6 — About Tagline Update
- About tab description updated to "Mount network storage in comfort."

## 0.4.5 — VFS Cache Mode for Mount Jobs & Job Start Time
- Mount jobs now include a **Cache Mode** dropdown (off, minimal, writes, full) that maps to rclone's `--vfs-cache-mode` option
- Cache mode is passed to the rclone RC `mount/mount` endpoint via `vfsOpt.CacheMode`
- Jobs now track `last_start` (when a job was invoked) separately from `last_run` (when it completed)

## 0.4.4 — Improved Log Timestamp Format
- Activity log timestamps now use a cleaner `YYYY-MM-DD HH:MM:SS` format instead of ISO 8601
- Log entries are now more readable at a glance (e.g. `[2026-04-07 14:32:01] COMPLETED ...`)

## 0.4.3 — Job Duration in Activity Log
- Completed job log entries now include total run time (e.g. `ran for 2m 15s`)
- Duration is displayed in human-readable format: seconds, minutes+seconds, or hours+minutes+seconds as appropriate

## 0.4.2 — Per-Job Stats in Activity Log
- Fixed activity log showing incorrect (cumulative) transfer stats for sync/copy/move jobs
- Stats are now queried per-job via rclone RC `core/stats` with the `group` parameter, so each job reports only its own bytes transferred, file count, and speed
- Sync jobs that find nothing to transfer now correctly log `0 files, 0 B, 0 B/s`

## 0.4.1 — Remote Capacity Display & Browser Action Buttons
- Remotes tab now shows used/free capacity for each configured remote
- Capacity is fetched asynchronously via the rclone RC `operations/about` endpoint
- Backends that don't report usage gracefully show no capacity text
- Copy, Move, Sync, and Mount buttons on the browser tab now show both icon and text label
- Action buttons styled with a muted green background to match the destructive-action Delete button

## 0.4.0 — Updated Application & Tray Icons
- New application icon replacing the previous one
- Updated AppIndicator/tray icon for improved visibility and consistency

## 0.3.27 — Tray Spinner Fix
- Fixed tray spinner stopping when multiple jobs ran concurrently and one finished before the others
- Replaced `any_job_running()` scan with a `m_running_job_count` counter so the spinner reflects the actual number of active transfers
- Fixed race condition where rapid duplicate job starts (e.g. from the scheduler) incremented the counter multiple times; added a `m_job_submitting` guard to prevent duplicate increments
- Fixed duplicate completion callbacks from overlapping poll cycles double-decrementing the counter
- Fixed `set_attention()` emitting `NewStatus` mid-run, which caused GNOME AppIndicator to reset its property cache and fall back to the static icon
- Suppressed rclone `rcd` daemon output so its notices and warnings no longer clutter the console

## 0.3.26 — Performance Improvements
- Removed unnecessary 10-second job polling in Jobs tab; updates now arrive exclusively via daemon broadcast messages
- Optimised activity log loading to tail only the last 100 lines instead of reading the entire file
- Fixed race condition in daemon poll timer where `m_job_ids[index]` could be cleared between the status check and the async callback
- Added type-safe index parsing throughout daemon message handler to prevent implicit integer conversion

## 0.3.25 — Tray Spinner Fix
- Fixed animated tray spinner not appearing on the second and subsequent jobs
- After a successful job the tray entered `NeedsAttention` state; starting a new job now resets the status to `Active` before beginning animation so the system tray displays the spinner instead of the static attention icon

## 0.3.24 — Animated Tray Icon
- Tray icon now shows a rotating spinner while any job is in progress
- Animation stops and the static icon is restored when all job activity has finished
- Uses Cairo-rendered 8-frame spinner delivered via SNI `IconPixmap`

## 0.3.23 — Job Dialog Auto-resize
- Job add/edit dialog now shrinks correctly when switching to Mount type (which hides several fields)
- Dialog also shrinks when the Advanced Options expander is collapsed

## 0.3.22 — Job Retry Support
- New **Retries on failure** setting in Settings → Transfers (default 0)
- Failed jobs are automatically retried up to the configured number of times before being marked as failed
- Each retry attempt is recorded in the activity log as `RETRYING … attempt k/N`
- Per-job retry count can be overridden in the job add/edit dialog under **Advanced Options**; defaults to the global setting

## 0.3.21 — Concurrent Job Protection
- Scheduled jobs now skip execution if the previous instance of the same job is still running
- Skipped attempts are recorded in the activity log as `SKIPPED … previous instance still running`

## 0.3.20 — Activity Log Newest-First
- Activity log on the Jobs tab now displays entries newest-first, so the latest messages are always visible without scrolling

## 0.3.19 — Job Dialog Defaults & UI Tweaks
- Default window width increased to 1250px
- File Filters field renamed to **File Include Filters** for clarity
- Dry Run is now enabled by default when creating a new job
- Job dialog now includes a **Parallel Transfers** field; defaults to the value set in Settings, but can be overridden per-job
- Bandwidth Limit and Parallel Transfers are now grouped under a collapsed **Advanced Options** expander row
- Fixed: completed jobs were being logged repeatedly — poll timer now stops immediately when a job finishes by resetting the internal job ID to -1

## 0.3.18 — Mount Job Dialog Cleanup (continued)
- Dry Run, Enable Checksum, and Bandwidth Limit options are now hidden when the job type is Mount

## 0.3.17 — Smaller Job Log Font
- Activity log in the Jobs tab now renders at 80% font size for a more compact display

## 0.3.16 — Extended Provider Icons
- Added Simple Icons SVGs for Google Photos, Seafile, Zoho WorkDrive, Filen, OpenStack Swift, Apache HDFS, Citrix ShareFile, DigitalOcean, Wasabi, Cloudflare, and Hetzner
- SMB remotes now use `folder-remote-symbolic` instead of the generic terminal icon
- Provider icons appear in both the Backends tab and the file manager remote dropdown

## 0.3.15 — Provider Icons
- Remote type icons now use Simple Icons SVGs (Google Drive, Dropbox, Backblaze, MEGA, Box, Google Cloud, Proton Drive, Internet Archive) with symbolic fallbacks for all other types
- Light and dark variants of each icon — dark mode shows white icons automatically
- Icons appear both in the Backends tab remote list and in the file manager remote dropdown
- Remote dropdown width increased to 150px to accommodate icon + name

## 0.3.14 — Remote Type Icons on Backends Tab
- Each remote row now shows a symbolic icon representing its type (Google Drive, Dropbox, encrypted, local disk, terminal-based, or generic network server)

## 0.3.13 — Compact Browser Navigation Bar
- Navigation strip (Back, Up, breadcrumbs, Reload) is now more compact: halved spacing, margins, and remote dropdown minimum width
- Breadcrumb buttons use reduced horizontal padding for a tighter path display

## 0.3.12 — Mount Job Dialog Cleanup
- File Filters field is now hidden when the job type is Mount, as rclone mount does not use include filters

## 0.3.11 — Job Row Display
- Job title now shows `SourceDir → DestDir` (last path component of each) instead of the raw UUID
- Footer line of each job row now shows the UUID left-justified and the status text right-justified

## 0.3.10 — Job Type Icons
- Job rows in the Jobs tab now show the job type as a symbolic icon instead of a text badge (`[SYNC]`, `[COPY]`, etc.)
- Icons match those used on the Copy/Move/Sync/Mount buttons in the browse tab action bar

## 0.3.9 — Browser Pane Cleanup
- Removed source/destination role labels from browser pane headers
- Removed virtual swap logic (`m_source_on_left`, `update_pane_roles`, `BrowserPane::Role`) — left pane is always source now that swapping is physical
- Removed vertical separator between remote dropdown and navigation buttons

## 0.3.8 — Physical Pane Swap
- Swap button now exchanges the actual remote and path between the two browser panes, rather than just flipping the source/destination role labels
- Navigation history is also swapped, so the Back button continues to work correctly after a swap

## 0.3.7 — Security Hardening
- Fixed: deleting a job now disconnects its poll/schedule timers and removes it from all internal tracking vectors — previously stale timer callbacks could fire after deletion and reference the wrong job
- Fixed: poll timer lambda now exits early if the job index is out of range or the job ID is unset — prevents stale callbacks after a job delete
- Fixed: `rclone authorize` callback now uses a weak lifetime guard — no longer accesses the widget if it was destroyed while OAuth was in progress
- Fixed: malformed rclone authorize output with reversed `-->` / `<--` markers no longer causes a size_t underflow in the token extraction

## 0.3.6 — Breadcrumb Bar Overflow Fix
- File path bar no longer grows the main window when navigating into deep directories
- Breadcrumbs are now clipped to their allocated space and scroll to show the deepest segment

## 0.3.5 — File Filters in Job Dialog
- Job add/edit dialog now shows a **File Filters** field displaying the include patterns for the job
- Patterns are shown space-separated and can be edited directly
- Fixed: editing an existing job no longer silently discards its file filter patterns

## 0.3.4 — Job Activity Log
- Job execution results are now written to `~/.local/state/mtsync/mtsync.log`
- Each job logs a STARTED line and a COMPLETED line (with file count, bytes transferred, and speed for sync/copy/move jobs)
- Mount jobs log success or failure with error message
- Jobs tab now shows an **Activity Log** panel at the bottom, displaying the log file contents and auto-scrolling to the latest entry

## 0.3.3 — Progress Bar Visibility
- Progress bar in the job list is now hidden when a job is inactive
- Progress bar appears only while a job is running and is hidden again on completion or stop

## 0.3.2 — Job Dialog Improvements & Coloured File Icons
- Scheduling off: shows **Run Now** + **Save** buttons; scheduling on: shows **Schedule** only
- Cron fields section is now hidden entirely when scheduling is disabled (previously greyed out)
- Dialog shrinks to fit when cron section is hidden
- Run Now button styled as destructive-action; Cancel button styled as success
- File browser icons are now coloured by type (folders amber, images blue, video purple, audio green, archives orange, documents/PDFs red, code teal, etc.)

## 0.3.1 — Desktop Integration
- Application icon now appears in taskbar, launcher, and dock when the app is running
- Added `data/com.mtsync.MtSync.desktop` for XDG desktop entry
- CMake install rules deploy the icon to `share/icons/hicolor/256x256/apps/` and the `.desktop` file to `share/applications/`
- Window sets `icon-name` to `com.mtsync.MtSync` for X11 tray/taskbar resolution
- Added Installing section to README

## 0.3.0 — Application Icon
- Application icon now embedded in the binary via GLib resources and displayed on the About tab
- Added `data/mtsync.gresource.xml` and updated CMakeLists.txt to compile resources at build time
- No installation step required — icon is always available regardless of system paths

## 0.2.6 - Mount at startup fix
- Fixed "Mount at Start-up" jobs not mounting on daemon start — auto-mount now runs inside the `ensure_daemon` callback so rclone RC is ready before the mount is attempted

## 0.2.5 - Sync empty directory fix
- Fixed `--create-empty-src-dirs` not being passed correctly to rclone RC (`createEmptySrcDirs` is a direct `sync/sync` parameter, not a `_config` override)

## 0.2.4 - Job Edit Checksum Fix
- Fixed "Enable Checksum" toggle not loading correctly when editing existing jobs

## 0.2.3 - Job dialog Save button and sync fixes
- Added "Save" button to JobEditDialog to save job without running it
- Fixed jobs not being synced to daemon when created from browser view
- Fixed Sync jobs using `--create-empty-src-dirs` flag (was using wrong case)

## 0.2.2 - Removed redundant scheduler from GUI
- Removed duplicate job scheduling from JobView since daemon handles all scheduling
- Removed sched_timer member and schedule_job() method from job_view.hpp/cpp
- Sync jobs now run with `--create-empty-src-dirs` by default

## 0.2.1 - Code clean-up of dead code as operations have shifted from GUI to Daemon
- Removed copy_files / move_files from rclone_cli.hpp, rclone_cli.cpp
- Removed set_running_jobs / g_running_jobs from tray.hpp, tray.cpp
- Removed ErrorCallback typedef from daemon_proxy.hpp
- Removed m_args member + <vector> include from notification.hpp
- Removed m_old_config_path + migration from job_view.hpp, job_view.cpp
- Removed redundant inner bounds check from daemon.cpp:283
- Removed spurious 500ms connectivity ping from rclone_rc.cpp

## 0.2.0 — Real-time Job Progress & Mount Improvements

- Real-time progress updates in the Jobs tab showing transfer stats (files transferred, speed, ETA)
- Mount jobs now properly show stop button when active
- "Ignore Checksum" enabled by default; "Enable Checksum" option to disable
- Mount status persists via `active` field and is restored when switching to Jobs tab
- Jobs tab periodically refreshes job list from daemon to keep UI in sync

## 0.1.7 — Daemon RC Startup

- Daemon now starts `rclone rcd` immediately upon startup
- Jobs no longer need to ensure the daemon on-demand; it's always running
- GUI only configures the daemon and performs no backend work

## 0.1.6 — Bi-directional Sync

- **Bi-directional sync** toggle added to the job dialog when type is **Sync**
- When enabled, the job uses the rclone RC `bisync/bisync` endpoint instead of `sync/sync`
- Persisted as `bisync` field in `jobs.json`

## 0.1.5 — About Tab

- New **About** tab to the right of Settings
- Shows app name, description, version, license, and copyright

## 0.1.4 — Settings Tab

- New **Settings** tab to the right of Backends
- **General** group: Start daemon on login (writes/removes `~/.config/autostart/mtsync-daemon.desktop`), Start minimized to tray, Shutdown daemon when closing application
- **Transfers** group: Default bandwidth limit, Verify checksums, Parallel transfers count
- **rclone** group: Override rclone binary path (leave empty for PATH lookup; restart required)
- Settings persist to `~/.config/mtsync/settings.json`
- "Close to tray" takes effect immediately without restart

## 0.1.3 — Mount Job Type

- **Mount** button added to the browser action bar (after Sync)
- Mount is a first-class job type: persists in `jobs.json`, supports cron scheduling
- "Mount at Start-up" option in the job dialog — daemon automatically mounts on startup when enabled
- Mount jobs use the rclone RC `mount/mount` endpoint; stop button calls `mount/unmount`
- Job list shows `[MOUNT]` type badge
- Running a mount job shows "Mounted" status; stopping shows "Unmounted"

## 0.1.2 — File Browser Status Bar

- Footer bar in each browser pane now shows file/folder counts and total file size on the left (e.g. "12 files, 3 folders, Total: 45.2 MB")
- Counts reflect the current hidden-files filter state
- Status clears while navigating and on empty directories

## 0.1.1 — Show Hidden Files

- Each browser pane now has a "Show hidden files" checkbox in the bottom-right corner, off by default
- Files and directories with names starting with `.` are hidden unless the checkbox is ticked
- Each pane's setting is independent

## 0.1.0 — System Tray Icon & Bug Fixes

- Fixed: tray **Open** no longer silently drops the message when the GUI is closed — daemon now spawns a fresh GUI process if no clients are connected

## 0.0.11 — System Tray Icon

- System tray icon implemented via StatusNotifierItem (SNI/`org.kde.StatusNotifierItem`) protocol — no additional dependencies, pure GIO D-Bus
- Icon uses `network-server-symbolic` (GNOME monochrome network drive icon)
- Right-click context menu via `com.canonical.dbusmenu` with **Open** and **Quit** items
- Open broadcasts `show_window` over the IPC socket; connected GUI clients call `present()` on the window (if no GUI is running, one is spawned)
- Quit stops the daemon and rclone RC daemon
- SNI registration deferred to `on_name_acquired` to ensure the bus name is owned before the watcher contacts us
- `set_attention()` now emits `NewStatus` signal to toggle `NeedsAttention` state

## 0.0.10 — Code Quality & IPC Fixes

- DaemonProxy methods now properly correlate request/response via `request_id` instead of returning stub data
- Daemon forwards `request_id` from incoming requests to outgoing responses
- DaemonProxy uses `ipc::RequestType` enum instead of raw strings
- Removed dead code (`allocate_request_id`, unused `m_pending_callbacks` population)
- Fixed scheduling timer overflow: `MAX_DELAY_MS` was 576 days (`24*24*3600*1000`), corrected to 24 hours
- Window allocation uses `Gtk::make_managed` instead of raw `new`
- Hardcoded `/usr/bin/rclone` replaced with PATH lookup via `Glib::find_program_in_path`
- Silent `catch (...) {}` blocks replaced with `catch (const std::exception&)` + `g_warning()`
- IPC socket directory is now created if missing before client connection
- Added missing `<nlohmann/json.hpp>` include in `daemon_proxy.cpp`

## 0.0.9 — GUI ↔ Daemon Integration

- GUI automatically starts daemon if not running
- JobView connects to daemon via IPC for real-time updates
- Jobs continue running in daemon when GUI is closed
- Daemon broadcasts job status changes to all connected GUI clients
- Fixed IPC server/client communication (messages now properly sent/received)
- Desktop notifications on job completion (when notify-send or kdialog is available)
- D-Bus service (com.mtsync.MtSync) for external communication
- Daemon exposes ShowWindow and Quit methods via D-Bus

## 0.0.8 — Background Daemon Mode

- `mtsync --daemon` runs a background daemon process
- Daemon manages job scheduling and rclone RC daemon lifecycle
- Jobs continue running even when GUI is closed
- IPC server (Unix socket at `~/.cache/mtsync/socket`) for GUI ↔ Daemon communication
- Note: System tray icon is stubbed (GTK4 lacks native tray support; full tray integration planned)

## 0.0.7 — Selected Files in Copy/Move/Sync

- Sync button added to the browser action bar alongside Copy and Move
- Copy, Move, and Sync buttons now display icons
- Copy and Move dialogs now include selected files as `--include` filter patterns
- If no files are selected, the entire current directory is used (existing behavior)

## 0.0.6 — Simplified Transfer Actions

- Single Copy and Move buttons in dual-pane browser (instead of four directional buttons)
- Swap button to reverse source/destination between panes
- Role labels displayed in each pane header ("← Source" / "Destination →")

## 0.0.5 — Cron Scheduling

- Recurring cron-style scheduling in `JobEditDialog`: enable switch + five individual fields (Minute, Hour, Day, Month, Weekday) with live human-readable summary
- Action button reads "Run Now" when scheduling is disabled, "Schedule" when enabled
- Jobs re-arm automatically after each run for the next cron occurrence
- `cron_utils.hpp`: header-only cron engine — field parser, next-occurrence calculator, human-readable description

## 0.0.4 — Dual-Pane File Manager & Jobs System

The file browser has been redesigned as a dual-pane manager (rcloneview-style), and the Sync tab has been generalised into a Jobs system supporting Sync, Copy, and Move operations with optional scheduling.

- Two independent browser panes in a resizable split view (`Gtk::Paned`)
- Each pane has its own remote dropdown, breadcrumb navigation, and file list
- Multi-selection support in both panes
- Active pane tracked automatically (accent stripe indicator)
- New Folder popover with Enter-key support
- New rclone CLI operations: `copy`, `move`, `delete` (with `--include` file filters), `mkdir`
- Sync tab renamed to Jobs; supports three job types: Sync, Copy, Move
- `Job` data model replaces `SyncPair`; stored in `~/.config/mtsync/jobs.json`
- Automatic migration from `sync_pairs.json` on first launch (existing sync pairs become [SYNC] jobs)
- Browser Copy/Move buttons open a job-configuration dialog instead of running immediately
- `JobEditDialog` pre-fills source/destination from the active and other pane
- Job rows show type badge [SYNC]/[COPY]/[MOVE] and last run status
- Delete button shows an AdwAlertDialog confirmation before deleting files
- `RcloneRc` gains `copy_async` and `move_async` methods (rclone RC `sync/copy`, `sync/move` endpoints)

## 0.0.3 — Local Filesystem Browser

- Local filesystem always available as the first sidebar entry, starting at the user's home directory
- Breadcrumbs, Up, and Back navigation all handle absolute paths and filesystem root correctly

## 0.0.2 — File Browser Overhaul

The file browser has been redesigned from a flat single-pane layout to a GNOME Files-style interface with a collapsible sidebar, breadcrumb navigation, sortable columns, MIME-type-aware icons, and proper empty/loading states.

- Redesigned file browser with sidebar + content pane layout (`AdwNavigationSplitView`)
- Sidebar lists remotes with type-appropriate icons
- Breadcrumb navigation bar with clickable path segments
- Sortable columns (Name, Size, Modified) with directories always sorted above files
- MIME-type icons for files (images, video, audio, archives, documents, scripts)
- Empty folder and loading spinner states
- Stale-load guard prevents out-of-order async results from corrupting the file list
- Fixed duplicate window controls when running under a server-side decorated window manager

## 0.0.1

Initial release.

- GTK4/libadwaita window with three-tab navigation (Backends, Sync, Browse)
- rclone CLI integration for backend configuration (create, edit, delete remotes)
- Dynamic form generation from rclone provider options
- File browser with ColumnView and directory navigation via `rclone lsjson`
- Sync management with rclone RC daemon, live progress (bytes, speed, ETA)
- Sync pair persistence in `~/.config/mtsync/sync_pairs.json`
