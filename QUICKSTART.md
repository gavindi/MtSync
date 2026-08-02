# Mt. Sync — Quickstart

Get up and running in five minutes. For full details on every feature, see [User_Manual.md](User_Manual.md).

## 1. Prerequisites

Install [rclone](https://rclone.org/) and make sure it's on your system `PATH`. If it's installed somewhere else, you can point Mt. Sync at it later in **Settings → rclone → rclone binary path**.

## 2. Add a Remote

A *remote* is a named connection to a storage service — cloud storage, an SFTP/FTP/SMB server, or a local path.

1. Open the **Backends** tab and click **+**
2. Enter a **Name** (e.g. `work` — it will be referenced as `work:`)
3. Choose a **Provider** (Google Drive, Dropbox, S3, SFTP, SMB, and 40+ others)
4. Fill in the fields for your provider. OAuth providers show an **OAuth Login** button — click it and sign in via your browser
5. Click **Save**

## 3. Browse & Pre-fill a Job

This is the fastest way to set up a transfer:

1. Open the **Browser** tab
2. Navigate to your **source** in the left pane and your **destination** in the right pane
3. Select specific files/folders if you only want to transfer those (otherwise the whole folder is used)
4. Click a green action button: **Copy**, **Move**, **Sync**, or **Mount**

The Add Job dialog opens with source, destination, and any selected files already filled in.

## 4. Understand the Job Types

| Type | What it does |
|------|---------------|
| **Copy** | Copies files to the destination. Nothing at the destination is ever deleted — safest for backups |
| **Move** | Copies then deletes from the source — relocates files |
| **Sync** | Makes the destination an exact mirror of the source, **deleting** anything at the destination not present in the source. Enable **Bi-directional** to sync changes both ways instead |
| **Mount** | Mounts the left-pane remote as a virtual filesystem at the right-pane local path |

> **Dry Run is on by default.** Review what a job *would* do before turning it off and running for real — especially for Sync jobs, which delete files.

## 5. Run It

In the Add Job dialog:
- **Run Now** runs the job immediately (no schedule set)
- **Schedule** (Schedule tab → Enable Schedule) runs it automatically on a cron-style schedule
- **Save** stores the job without running it

Track progress and history on the **Jobs** tab — each row shows live status, a progress bar, and Run/Stop/Edit/Delete controls. The **Activity Log** below the list records every run.

## 6. Bi-directional Sync (bisync) Gotchas

If you enable **Bi-directional** on a Sync job:
- The **first run** for a new pair automatically establishes a baseline — this is expected and handled for you.
- By default, **any deletion on either side blocks the sync** until you review it. If you want deletions to propagate automatically (including large accidental ones — there's no in-between), enable **Force Deletes** in the job's Job tab.

## Next Steps

- **Compare** two folders file-by-file from the Browser tab
- Set global defaults (bandwidth, checksum, parallel transfers, retries) in **Settings → Transfers**
- Enable **Start daemon on login** in **Settings → Start Up & Shut Down** so scheduled jobs and mounts work without opening the app

For the complete reference — every field, every setting, cron syntax, and more — see [User_Manual.md](User_Manual.md).
