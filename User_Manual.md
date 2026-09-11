# Mt. Sync — User Manual

Mt. Sync is a desktop application for managing cloud and network storage using [rclone](https://rclone.org/) as its engine. This manual covers everything you need to get started: connecting storage services, browsing files, and running transfer and mount jobs.

---

## Quick-Start Checklist

1. Install `rclone` and ensure it is on your system PATH (or set a custom path in **Settings**)
2. Launch Mt. Sync
3. Go to the **Backends** tab and add at least one remote (cloud or network storage location)
4. Go to the **Browser** tab and navigate your remote in one of the panes
5. Use the action buttons at the bottom to copy, sync, move, or mount

---

## 1. Adding & Managing Remotes (Backends Tab)

A *remote* is a named connection to a storage service — Google Drive, an SFTP server, an S3 bucket, a local path, and so on. You must add at least one remote before you can transfer files.

### Adding a Remote

1. Click the **+** button at the top of the Backends tab
2. Enter a **Name** — this becomes the identifier used everywhere (e.g. a remote named `work` is referenced as `work:`)
3. Choose a **Provider** from the dropdown — over 40 are supported, including:
   - **Cloud storage:** Google Drive, Dropbox, OneDrive, Box, MEGA, Proton Drive, pCloud, and more
   - **Object storage:** Amazon S3, Backblaze B2, Azure Blob, Cloudflare R2, Wasabi, and others
   - **Network protocols:** SFTP, FTP, SMB (Windows shares)
4. Fill in the fields that appear for your chosen provider (server address, API keys, bucket name, etc.)
5. **OAuth providers** (Google Drive, Dropbox, OneDrive, etc.) show an **OAuth Login** button — click it and complete the sign-in in your browser
6. Expand **Advanced Options** if you need to configure less common settings
7. Click **Save** — the remote appears in the list with its storage usage shown below the name

### Editing or Deleting a Remote

Each remote row has an **Edit** button (pencil icon) and a **Delete** button (trash icon). Editing opens the same dialog with your existing settings pre-filled.

> **Note:** Deleting a remote only removes it from Mt. Sync's configuration. No files are deleted from the storage service itself.

---

## 2. The File Browser (Browser Tab)

The Browser tab shows two independent panes side by side. Each pane can point to a different remote or local folder, letting you navigate both sides of a transfer at the same time.

### Navigating a Pane

- Use the **Remote** dropdown at the top of each pane to choose a configured remote
- Click a folder to open it
- Use the **breadcrumb bar** to jump to any parent folder
- Use the **back arrow** to go to the previous location
- Tick **Show hidden files** (bottom of pane) to include dot-files in the listing
- The status bar at the bottom of each pane shows the total file count and size of the current folder

### Selecting Files

- Click a file or folder to select it
- Hold **Ctrl** and click to select multiple items
- Hold **Shift** and click to select a range

### Swapping Panes

The **Swap ↔** button in the centre of the action bar swaps the remote and path between the left and right panes.

---

### Using Panes to Pre-fill Jobs

> **This is the fastest way to set up a job.** Navigate to your source location in the **left pane** and your destination in the **right pane**, then click one of the green action buttons — **Copy**, **Move**, **Sync**, or **Mount**. The Add Job dialog opens with the source and destination already filled in. If you have files or folders selected, they are automatically added as **File Filters** so only those items are transferred.

---

### Compare

Click **Compare** to run an integrity check between the two panes. Mt. Sync will scan both sides and display a table showing every file and its status:

| Glyph | Meaning |
|-------|---------|
| `=` | File is identical on both sides |
| `←` | File exists only in the source (left) |
| `→` | File exists only in the destination (right) |
| `≠` | File exists on both sides but differs |
| `!` | Error reading the file on one side |

Use the five filter toggles in the toolbar (`←` `→` `=` `≠` `!`) to show or hide each category. All are on by default — click one to hide that category. Columns are sortable, and files stay grouped under their directory header regardless of sort order.

---

## 3. Job Types

Jobs are saved transfer or mount operations you can run on demand or on a schedule.

### Copy

Copies files from the source to the destination. Files already at the destination that are not in the source are **left untouched**. This is the safest option for one-way backups — nothing at the destination is ever deleted.

### Move

Copies files from the source to the destination, then **deletes them from the source**. Use this when you want to relocate files rather than duplicate them.

### Sync

Makes the destination an **exact mirror** of the source. Any file at the destination that does not exist in the source will be **deleted**. Use this when you want the destination to always reflect the current state of the source.

- **Bi-directional** toggle — when enabled, changes flow in both directions (new or updated files on either side are copied to the other). Powered by rclone bisync.

> **Caution:** A standard (one-way) Sync job will delete files from the destination that are not present in the source. Make sure your source is correct before running with **Dry Run** turned off.

### Mount

Mounts the **left-pane remote location** as a virtual filesystem **at the right-pane path** on your computer. After mounting, the remote appears as an ordinary folder you can open in any application.

- The **left pane** is what gets mounted (the remote source)
- The **right pane** is the local folder where it will be mounted
- Use the red **Stop** button on the job row to unmount

Mount-specific options (**Mount at Start-up** and **VFS Cache Mode**) are available on the **Job** tab of the Add/Edit Job dialog.

---

## 4. Adding a Job

Jobs can be added from the **Browser** tab (using the green action buttons) or directly from the **Jobs** tab using the **+** button.

The Add/Edit Job dialog is split into three tabs.

### Job Tab

| Field | Description |
|-------|-------------|
| **Type** | Copy, Move, Sync, or Mount |
| **Source** | The location to read from. Format: `remotename:path/to/folder` or `local:/absolute/path` |
| **Destination** | The location to write to. Same format as Source |
| **File Filters** | Space-separated glob patterns to limit which files are included. Example: `*.jpg *.png` transfers only images. Leave blank for all files |
| **Dry Run** | **On by default.** Simulates the transfer without moving any data — great for checking what would happen. Turn this **off** when you are ready to perform the real transfer |
| **Bi-directional** | (Sync only) Sync changes in both directions |
| **Enable Checksum** | Verifies every file's integrity byte-for-byte after transfer. Slower, but catches corruption |
| **Mount at Start-up** | (Mount only) Automatically re-mount this location each time Mt. Sync's daemon starts |
| **VFS Cache Mode** | (Mount only) Controls local file caching. `off` = no cache; `minimal` = metadata only; `writes` = cache writes; `full` = full read/write cache (best performance, uses local storage) |

### Schedule Tab

The Schedule tab is split into a **cron editor** on the left and a live **Schedule Preview** on the right.

**Enable Schedule** — toggle at the top of the tab to activate scheduling for this job.

**Preset** — choose a common schedule to fill all fields at once:

| Preset | Runs |
|--------|------|
| Every minute | Every minute of every hour |
| Hourly | Once per hour, at minute 0 |
| Daily | Once per day, at midnight |
| Weekly | Once per week, Sunday midnight |
| Monthly | Once per month, on the 1st at midnight |
| Custom | Fill in the fields below manually |

Selecting a preset fills all fields instantly. Editing any field automatically switches the preset to **Custom**.

**Minutes / Hours** — text entry for each field. The × button resets the field to `*` (every value). You can enter a single number, a range (`9-17`), a list (`0,15,30,45`), or a step (`*/5`).

| Field | Valid values |
|-------|-------------|
| **Minute** | 0–59, `*/N`, or `*` |
| **Hour** | 0–23, `*/N`, or `*` |

**Days** — two controls:
- **Day of Month** — text entry (1–31 or `*`), with a × clear button
- **Day of Week** — seven checkboxes (Sun Mon Tue Wed Thu Fri Sat); all checked means `*` (every day); uncheck days to restrict the schedule

**Months** — twelve checkboxes (Jan–Dec) in two rows; all checked means `*` (every month); uncheck months to restrict the schedule.

**Schedule Preview** (right panel) updates live as you edit:
- The raw cron expression (e.g. `0 2 * * 1`)
- A plain-English description (e.g. "Every Monday at 02:00")
- A calendar with run days marked for the current month; use the calendar's navigation arrows to browse other months
- Your system timezone
- A scrollable list of the next 15 upcoming execution times

**Watch for Changes** — below the cron editor, a separate toggle lets this job run automatically shortly after files change in its **Source** folder, instead of (or alongside) the cron schedule above. See [6. Watch Mode](#6-watch-mode-react-to-file-changes) for details. This toggle is hidden for Mount jobs and disabled if **Source** is a remote location rather than a local folder.

### Advanced Tab

| Field | Description |
|-------|-------------|
| **Bandwidth Limit** | Maximum transfer speed, e.g. `10M` for 10 MB/s. Leave blank for unlimited |
| **Parallel Transfers** | How many files transfer at the same time. Higher values speed up transfers of many small files |
| **Retries on Failure** | How many times to retry a file that fails to transfer before giving up |
| **Extra rclone Flags** | Arbitrary rclone flags appended to this job's RC call at run time. Enter them in standard rclone CLI format, space-separated (e.g. `--min-size 10M --max-age 7d --checkers 16`). Supports `--flag value`, `--flag=value`, and boolean `--flag` forms. These take priority over **Global rclone flags** in Settings but do not override the job's own explicit fields (bandwidth, dry run, etc.) |

### Running or Saving

- **Run Now** — executes the job immediately and saves it (shown when no schedule is set)
- **Schedule** — saves the job with its schedule active (shown when Enable Schedule is on)
- **Save** — saves the job without running it (available when no schedule is set)
- **Cancel** — closes the dialog without saving

---

## 5. Scheduling Jobs

Any job can be run automatically on a schedule.

1. Open the job dialog (add new or edit existing)
2. Switch to the **Schedule** tab
3. Toggle **Enable Schedule** on
4. Choose a **Preset** for common schedules, or set the fields manually:
   - Enter values in **Minute** and **Hour**; use the × button to reset a field to `*`
   - Tick/untick **Day of Week** checkboxes and **Month** checkboxes to limit which days or months the job runs
5. Watch the **Schedule Preview** panel on the right — it shows the cron expression, a plain-English description, a calendar with run days highlighted, and the next 15 upcoming times
6. Click **Schedule** to save

Scheduled jobs skip execution if the previous run is still in progress.

---

## 6. Watch Mode (React to File Changes)

Watch Mode runs a job automatically shortly after files change, instead of waiting for a cron schedule or a manual **Run**. It's useful for keeping a destination close to up to date without picking an arbitrary polling interval.

1. Open the job dialog (add new or edit existing) for a **Sync**, **Copy**, or **Move** job whose **Source** is a local folder
2. Switch to the **Schedule** tab
3. Toggle **Watch for Changes** on

Watch Mode and the cron schedule are independent — you can enable either, both, or neither. A common pattern is to enable Watch Mode for fast reaction to local edits and keep a loose cron schedule as a periodic safety net.

**How it decides when to actually run:** rather than syncing on every single file change (which would mean dozens of runs during a large copy or delete), Mt. Sync waits for a short quiet period after the last detected change before starting the job — by default 2 seconds. If changes keep happening continuously (e.g. a large batch of files being deleted or moved), Mt. Sync won't wait forever: it guarantees a sync still runs at least every 30 seconds by default, even if activity hasn't settled down. Both the quiet period and the maximum wait can be tuned in **Settings → Transfers** (see [8. Settings](#8-settings)).

**Limitations:**
- **Watch for Changes** is not available for **Mount** jobs, or when **Source** is a remote location (`remotename:path`) rather than a local folder — only local filesystem changes can be detected this way
- For a **Bi-directional** (bisync) job, Watch Mode only reacts to changes on the local **Source** side; changes made on the remote side are still only picked up by a cron schedule or a manual run
- If a bi-directional sync job does **not** have **Force Deletes** enabled, deleting even a single file will cause the sync to abort with a "too many deletes" error — and since Watch Mode retries automatically, every subsequent triggered run will keep failing with the same error until you either restore the file, delete it from the other side too, or enable **Force Deletes** on the job

The **Activity Log** (see [7. Running & Monitoring Jobs](#7-running--monitoring-jobs-jobs-tab)) marks a run started by Watch Mode with `(triggered by file watch)` so you can tell it apart from a scheduled or manual run.

---

## 7. Running & Monitoring Jobs (Jobs Tab)

The Jobs tab lists all your saved jobs. Each row shows:

- **Type icon** — indicates Copy, Move, Sync, or Mount
- **Job name** — derived from the source and destination paths
- **Control buttons:**
  - Green **Run** button — start the job immediately
  - Red **Stop** button — halt a running job or unmount a mounted drive
  - Pencil **Edit** button — modify the job configuration
  - Trash **Delete** button — remove the job (does not delete any files)
- **Progress bar** — visible while a transfer is running, showing percentage complete
- **Status** — shows the current state or the result of the last run
- **Transfer stats** — live speed, data transferred, and ETA during active jobs

Click the **chevron** button on any row to expand it and see the full source/destination paths and the job's unique ID.

### Activity Log

Below the job list, the Activity Log records every job execution with a timestamp, result (STARTED, COMPLETED, SKIPPED, RETRYING), job type, and a details message. The most recent 100 entries are shown. A run started by [Watch Mode](#6-watch-mode-react-to-file-changes) is marked `(triggered by file watch)` in its STARTED entry.

---

## 8. Settings

Open the **Settings** tab to configure application-wide behaviour.

### Notifications

| Setting | Description |
|---------|-------------|
| **On Job Start** | Show a desktop notification when a job begins |
| **On Completion** | Notify when a job finishes successfully |
| **On Completion with Errors/Warnings** | Notify when a job finishes but encountered problems |

### Start Up & Shut Down

| Setting | Description |
|---------|-------------|
| **Start daemon on login** | Automatically launch the Mt. Sync background process when you log in, so scheduled jobs and mounts are available even without opening the app window |
| **Start minimized to tray** | Launch to the system tray instead of opening the main window |
| **Shutdown daemon when closing application** | When off, the daemon (and any active jobs or mounts) keeps running after you close the window |

### Transfers

These are the defaults applied to all new jobs. Individual jobs can override them.

| Setting | Description |
|---------|-------------|
| **Default bandwidth limit** | Maximum transfer speed for all jobs (e.g. `10M` = 10 MB/s). Leave blank for unlimited |
| **Verify checksums** | Enable checksum verification by default for all jobs |
| **Parallel transfers** | Default number of files to transfer simultaneously |
| **Retries on failure** | Default number of retry attempts for failed files |
| **Watch mode debounce (ms)** | Default quiet period after the last detected file change before a [Watch Mode](#6-watch-mode-react-to-file-changes) job runs |
| **Watch mode max wait (ms)** | Default upper bound on how long continuous file activity can postpone a Watch Mode run |

### rclone

| Setting | Description |
|---------|-------------|
| **rclone binary path** | Full path to the rclone executable if it is not on your system PATH (e.g. `/home/user/bin/rclone`). Leave blank to use the system default. Takes effect after restarting Mt. Sync |
| **Global rclone flags** | Additional flags passed to every job at execution time. Enter them in standard rclone CLI format, space-separated (e.g. `--log-level DEBUG --checkers 8 --max-age 24h`). Per-job settings take precedence and will not be overridden by these flags |

---

## 9. System Tray

When Mt. Sync is running, an icon appears in your system tray. Right-click it for **Open** (show the main window) and **Quit** (exit the application and optionally stop the daemon). An animated spinner overlays the tray icon while any job is actively transferring.
