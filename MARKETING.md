# Mt. Sync — Your Network Storage Mounting Experience

> **Mount network storage in comfort.**

Mt. Sync is a native GNOME desktop application that puts the full power of [rclone](https://rclone.org/) behind a polished GTK4 interface. Whether you're mounting a remote drive, scheduling nightly syncs, or browsing files across a dozen cloud providers — Mt. Sync handles it quietly in the background while you get on with your work.

---

## Works With Everything rclone Supports

rclone connects to over 40 storage providers. Mt. Sync gives all of them a native desktop home.

**Cloud storage** — Google Drive, Dropbox, Backblaze B2, MEGA, Box, Amazon S3, Google Cloud Storage, Proton Drive, Wasabi, Cloudflare R2, DigitalOcean Spaces, Hetzner, and more.

**Self-hosted & network** — SFTP, SMB/CIFS, WebDAV, FTP, OpenStack Swift, and others.

**Local & encrypted** — Local filesystem access and encrypted remote overlays via rclone crypt.

Each provider is shown with its own icon — Google Drive looks like Google Drive, Dropbox looks like Dropbox. Light and dark variants switch automatically with your desktop theme.

---

## Mount. Browse. Sync. All in One Place.

### Mount Network Storage as a Local Drive
Mount any remote as a local filesystem with a single click. Mt. Sync passes your VFS cache mode preference (`off`, `minimal`, `writes`, or `full`) directly to rclone so performance matches your workflow. Mounted drives appear as active jobs and can be unmounted just as easily. Mount jobs can be configured to start automatically when the daemon launches.

### Dual-Pane File Browser
A side-by-side browser lets you navigate two locations simultaneously — any combination of local disk and remote storage. Sortable columns, MIME-type icons, breadcrumb navigation, back history, and per-pane hidden file toggles make it feel like a native file manager. A status bar shows file and folder counts with total size. The swap button exchanges the two panes with a single click.

### Compare Before You Commit
Before transferring anything, hit **Compare** to see exactly what differs between the two panes. Mt. Sync runs `rclone check` across both locations and presents the results as a structured, paginated list grouped by subdirectory. Each row shows the filename, size, and modified date on the side where the file exists — nothing on the side where it doesn't. Status glyphs make the picture instantly readable: `→` exists only in source, `←` exists only in destination, `≠` present on both sides but different, `=` identical. Click any column header to sort — files stay grouped with their directory so the structure of your storage is always clear.

Four toggle buttons in the centre of the action bar let you filter the results by status — hide source-only files, destination-only files, differing files, or errors individually. Toggle any combination to focus on exactly what you care about. Directory headers disappear automatically when all their files are filtered out.

Select one or more rows and act on them directly from the action bar — no need to leave the dialog:

- **Copy →** — copy selected files from source to destination
- **← Copy** — copy selected files from destination to source
- **Delete** (source side) — remove selected files from the source remote
- **Delete** (destination side) — remove selected files from the destination remote

After any action the comparison reruns automatically so the list always reflects the current state. A Cancel button lets you abort a long-running scan at any time.

### Copy, Move & Sync
Copy or move selected files between any two locations — local or remote, same provider or different. Sync entire directories with rclone's fast delta transfers. Bi-directional sync (rclone bisync) keeps two locations in agreement without overwriting newer files. File include-filter patterns let you narrow transfers to exactly the files you want.

### Scheduled Jobs
Set any job to run on a cron schedule. The built-in schedule editor takes five familiar fields (minute, hour, day, month, weekday) and shows a plain-English summary of when the job will next run. Jobs re-arm automatically after each completion. If a scheduled instance is still running when the next trigger fires, it is skipped safely — no pile-ups.

### Watch Mode — Sync the Moment Something Changes
Why wait for the next scheduled run? Flip on **Watch for Changes** for any Sync, Copy, Move, or bi-directional sync job pointed at a local folder, and Mt. Sync keeps an eye on it for you — save a file, drop in a batch of photos, let another app write there, and your sync kicks off automatically within seconds, no cron schedule required.

It's smart about it, too: a flood of activity — a big delete, an entire folder dropped in at once — is gathered into a single sync instead of firing off dozens back-to-back, and a built-in safety valve guarantees a sync still happens even if the changes keep coming. Watch mode and scheduled runs work side by side, so a job can react instantly to local changes while a schedule still handles the rest.

---

## A Daemon That Never Sleeps

Mt. Sync separates the GUI from the work. The background daemon manages all job execution, scheduling, and rclone RC lifecycle independently. Close the window — your syncs keep running. Reopen it tomorrow — your job history is right there. The GUI reconnects automatically and picks up where it left off.

The system tray icon keeps you informed at a glance:
- **Spinner** — a job is actively transferring
- **Static icon** — everything is idle
- **Right-click menu** — Open or Quit from anywhere on the desktop

---

## Live Progress. Full Visibility.

While a job runs, the job row shows a live progress bar with bytes transferred, current speed, and estimated time remaining — queried directly from rclone's RC API so the numbers are accurate and per-job, not cumulative.

The **Activity Log** at the bottom of the Jobs tab records every event in a structured column view:

| Time | State | Job ID | Type | Contents |
|------|-------|--------|------|----------|
| 2026-04-07 11:15:00 | COMPLETED | 32930fb5… | SYNC | SUCCESS — 142 files, 1.2 GB, 45.3 MB/s, ran for 27s |
| 2026-04-07 11:14:59 | STARTED | 32930fb5… | SYNC | /home/user/Documents → Storinator:backups |

State labels are colour-coded at a glance. The log shows the last 100 entries newest-first so the latest activity is always visible without scrolling.

---

## Reliability Built In

- **Automatic retries** — Failed jobs retry up to a configurable number of times before being marked as failed. Each attempt is recorded in the activity log.
- **Concurrent job protection** — A scheduled job won't start a new instance if the previous one is still running.
- **Storm-safe watch mode** — A flood of file changes (a bulk delete, a folder full of new files) is batched into a single sync instead of triggering a run per change, with a guaranteed maximum wait so changes never pile up unsynced.
- **Per-job overrides** — Parallel transfers, bandwidth limits, and retry counts can be overridden per job, with global Settings values as the default.
- **Dry run by default** — New jobs default to dry-run mode so you can confirm what will happen before committing.

---

## Notifications That Don't Nag

Desktop notifications via `notify-send` — but only when you want them. Three independent toggles in Settings let you choose exactly when to be notified:

- **On Job Start** — know when a transfer begins
- **On Completion** — confirm successful transfers
- **On Completion with Errors/Warnings** — catch failures without noise from routine successes

All three default to off. Turn on only what matters to you.

---

## Settings That Stay Out of Your Way

Everything persists automatically to `~/.config/mtsync/settings.json`. No Apply button. No restart required for most settings.

- **Start Up & Shut Down** — launch the daemon at login, start minimised to tray, shut down daemon when closing the window
- **Notifications** — per-event notification toggles
- **Transfers** — default bandwidth cap, checksum verification, parallel transfer count, retry limit, watch-mode timing
- **rclone** — custom binary path for non-PATH installations

---

## Native. Fast. Yours.

Mt. Sync is built with C++23, GTK4, and libadwaita. It follows GNOME HIG conventions, respects your system theme (including dark mode), and integrates with your desktop notification service. No Electron. No runtime dependencies beyond the libraries it's built against. One binary, one daemon, done.

---

*Mt. Sync is free software — GNU General Public License v2.0.*
