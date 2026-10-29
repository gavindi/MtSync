# Mt. Sync vs RcloneView — Feature Comparison

*As of 2026-10-13. Mt. Sync 0.9.18.*

## Verdict

Mt. Sync matches RcloneView on every feature that makes RcloneView distinctive, and gives away free
what RcloneView charges for. RcloneView's advantages are reach and resourcing, not capability: three
operating systems, more providers, a funded roadmap and paid support.

The two products agree on an unusual thesis — that an rclone GUI should be a two-pane file manager
with folder comparison, mount management and a job scheduler, not a sync-folder applet. No other
rclone frontend attempts that combination. Within it, Mt. Sync goes deeper on automation and desktop
integration; RcloneView goes wider on platforms and providers.

Choose Mt. Sync for a GNOME desktop where syncs must survive the window closing and react to local
file changes. Choose RcloneView for a mixed Windows/macOS/Linux fleet, or where a support contract
matters more than the source.

## At a glance

| | Mt. Sync | RcloneView |
| --- | --- | --- |
| Positioning | Native GNOME rclone frontend | Cross-platform rclone frontend |
| Operating systems | Linux | Windows, macOS, Linux |
| Stack | C++23, GTK4, libadwaita | Flutter |
| Licence | GPL v2, source public | Proprietary, freemium |
| Price | Free, every feature | Free tier; Plus $19.80/licence/year (2 devices); Lifetime $99.80 (3 devices) |
| Architecture | GUI plus separate background daemon | Single desktop application |
| Packaging | deb, rpm, AppImage, Flatpak, Snap | AppImage, deb, rpm (x86_64 and arm64) |
| Providers | 40+ through rclone | 70+ claimed |
| Config source | Reads existing `~/.config/rclone/rclone.conf` | Reads existing rclone config |

## Feature-by-feature

"Not documented" means RcloneView's public pages do not mention it, not that it is absent.
**Plus** marks a feature behind RcloneView's paid tier.

### Browsing and comparison

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Two-pane browser, any local/remote mix | Yes | Yes |
| Folder comparison | Yes, via `rclone check` | Yes (free) |
| Comparison grouped by subdirectory | Yes | Not documented |
| Per-side filename, size, modified date | Yes, 7 columns | Not documented |
| Status glyphs for source-only, dest-only, differing, identical | Yes | Not documented |
| Filter comparison results by status | Yes, 5 toggles, free | **Plus** |
| Act on comparison rows (copy or delete per side) | Yes | Right-click Copy, Move, Sync |
| Sortable columns throughout | Yes | Not documented |
| Breadcrumb navigation and back history | Yes | Not documented |
| Pane swap | Yes | Not documented |
| Drag and drop between panes | Not present | Yes, whole directory trees |
| Photo and video preview | Not present | Yes (free) |
| Provider capacity shown per remote | Yes, used/free/total | Not documented |
| Multi-window per connection | Not present | **Plus** |

### Transfers and sync

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Copy, Move, Sync jobs | Yes | Yes |
| Bi-directional sync (`bisync`) | Yes | Not documented |
| One source to many destinations | Not present | Yes (free) |
| Include-pattern filters | Yes | Not documented |
| Dry run toggle | Yes, on by default for new jobs | Not documented |
| One-click dry-run preview with per-file actions | Yes, results in Compare window | Not documented |
| Checksum verification | Yes, per job | Not documented |
| Bandwidth limit | Yes, global and per job | Not documented |
| Parallel transfer count | Yes, global and per job | Not documented |
| Arbitrary extra rclone flags | Yes, global and per job | Integrated terminal tab (free) |
| Copy empty directories | Yes | Not documented |

### Mounting

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Mount remote as local drive | Yes | Yes (free) |
| VFS cache mode selection | Yes (`off`, `minimal`, `writes`, `full`) | Yes, with cache size and TTL options |
| Auto-mount at startup | Yes, free, via daemon | **Plus** |
| Mount liveness checks, stale mount detection | Yes, periodic | Not documented |
| Unmount from the UI | Yes | Yes |

### Scheduling and automation

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Scheduled jobs | Yes, free, full 5-field cron | **Plus** |
| Schedule presets (hourly, daily, weekly, monthly) | Yes | Not documented |
| Live schedule preview: cron string, plain English, calendar, next 15 runs | Yes | Not documented |
| Watch mode, sync on local file change | Yes, recursive inotify | Not present |
| Event-storm protection (debounce plus max-wait cap) | Yes, configurable globally and per job | Not applicable |
| Skip run if previous instance still active | Yes, with retry once it finishes | Not documented |
| Automatic retry with exponential backoff | Yes, 2s/4s/8s capped at 60s | Not documented |
| Batch job management | Per-job only | **Plus** |
| Remote control from Slack, Discord, Telegram | Not present | **Plus** |

### Background operation

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Headless daemon independent of the GUI | Yes, `mtsync --daemon` | Not present |
| Jobs keep running with the window closed | Yes | Not documented |
| GUI reconnects to running work on relaunch | Yes, over Unix socket IPC | Not applicable |
| Start at login | Yes, free | **Plus** (auto re-mount) |
| System tray icon | Yes, with activity spinner | Not documented |
| Start minimised to tray | Yes | Not documented |

### Monitoring and notifications

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Live per-job progress: files, bytes, speed, ETA | Yes, from rclone RC | Yes (free) |
| Structured activity log | Yes, last 100 entries, colour-coded | Yes (free) |
| Drill into an entry's full error log | Yes, in-app viewer plus open externally | Not documented |
| Desktop notifications | Yes, 3 independent triggers | Yes, on job completion (free) |
| Email notifications | Not present | Basic free, Smart on **Plus** |

### Setup, security and localisation

| Capability | Mt. Sync | RcloneView |
| --- | --- | --- |
| Guided first-run setup | Welcome dialog | Setup wizard (free) |
| Remote config forms generated from rclone's own option metadata | Yes | Not documented |
| Per-provider branded icons, light and dark | Yes | Not documented |
| Remote config encryption | Not present | Yes (free) |
| App lock with password | Not present | Yes (free) |
| Synology NAS auto-detect | Not present | Yes (free) |
| Custom rclone binary path | Yes | Connect with external rclone (free) |
| Translated UI | English, French, German | Not documented; support site in 7 languages |
| Custom themes | Follows system light/dark | **Plus** |
| AppStream metainfo for software centres | Yes | Not applicable |

## Where Mt. Sync leads

**Watch mode has no equivalent.** Recursive `Gio::FileMonitor` watching with debounce and a
max-wait cap is unique not just against RcloneView but against every rclone frontend surveyed.
Every competitor is cron-or-manual. This is the one feature that cannot be answered by a
competitor's next release without real engineering.

**A true daemon, not an app with a tray icon.** RcloneView is a single desktop application; close
it and the work stops. Mt. Sync splits GUI from execution over Unix socket IPC, so jobs survive the
window closing and the GUI reattaches to work in flight. A Flutter or Tauri competitor cannot bolt
this on cheaply.

**Scheduling is free and better.** RcloneView puts job scheduling behind the $19.80/year Plus tier.
Mt. Sync gives it away, and the editor is richer: presets, per-field clearing, weekday and month
grids, and a live preview showing the raw cron expression, a plain-English description, a marked
calendar and the next 15 run times.

**Reliability engineering RcloneView does not advertise.** Exponential-backoff retries, in-flight
guards that defer rather than drop a trigger, periodic mount liveness checks, HTTP timeouts and
poll-rate guards for network outages. These came out of real fault-tolerance work and are
load-bearing for unattended operation.

**Operational depth per job.** Bandwidth limit, parallel transfers, retry count and arbitrary extra
rclone flags, each overridable per job with a documented global-to-per-job precedence order. This is
the power-user surface RcloneView's simplified UI does not expose.

**Native integration.** GNOME HIG conventions, system theme, StatusNotifierItem tray, AppStream
metainfo, and no webview. Against Flutter this reads as a genuinely native application rather than a
port.

## Where RcloneView leads

**Platforms.** Windows and macOS are entire markets Mt. Sync does not address, and arm64 Linux
builds ship too. For anyone with a mixed fleet this decides it outright, regardless of features.

**Credential security.** Remote config encryption and an app lock with password are both in
RcloneView's free tier and absent from Mt. Sync. For a tool holding tokens to a dozen cloud
accounts, this is the most defensible gap on the list.

**Distribution and discoverability.** RcloneView has a product site in seven languages, versioned
release notes, published pricing and a support channel. Mt. Sync has CI-built packages and a
GitHub repository. On features Mt. Sync wins; on being found, it does not.

**Convenience features that demo well.** Drag and drop of whole directory trees between panes,
photo and video preview, one-source-to-many sync, and Synology NAS auto-detection are all free-tier
and all immediately visible in a first five minutes of use.

**Integrations.** Email notifications, and Slack/Discord/Telegram remote control on Plus. Mt. Sync
has desktop notifications only, which do not reach a user away from the machine.

**Provider count.** 70+ claimed against 40+. Both numbers come from rclone, so this is a
presentation difference rather than a real capability gap — but it is the number a buyer compares.

## The paywall line

RcloneView's free and paid split is the most useful competitive fact in this document, because the
paid tier is largely made of things Mt. Sync already does for free:

| Behind RcloneView's Plus tier | In Mt. Sync |
| --- | --- |
| Job scheduling and automated execution | Free, with a richer cron editor |
| Auto re-mount on startup | Free, via the daemon |
| Folder comparison with filter | Free, 5 status toggles |
| Batch job management | Partially — per-job config only |
| Multi-window per connection | Not present |
| Customisable themes | System light/dark only |
| Smart email notification | Not present |
| Mobile photo and video backup | Not present |
| Chat-app remote control | Not present |

Four of the nine paid features are free in Mt. Sync, and the three most important ones —
scheduling, auto-mount, filtered comparison — are the ones a serious user hits immediately. That is
a clean, verifiable claim: *the automation RcloneView sells, Mt. Sync gives away.*

## Positioning implications

1. **Lead with watch mode.** It is the only capability in this document with no competitor
   equivalent, and the README currently buries it in a sub-bullet of the jobs section. It deserves
   top billing above scheduling, because scheduling is table stakes and watch mode is not.

2. **Make the paywall comparison explicit.** A short "how we compare" section naming scheduling,
   auto-mount and filtered compare as free is concrete, checkable, and lands harder than any
   adjective.

3. **Close the credential-security gap.** Config encryption and an app lock are in a competitor's
   *free* tier. This is the only place where RcloneView is straightforwardly ahead on something
   users care about, and both are tractable — the keyring integration RClone Manager ships is the
   obvious reference.

4. **Fix distribution before adding features.** Mt. Sync is ahead of every open-source rclone GUI on
   capability and behind them on reach. Getting into Flathub proper would do more for adoption than
   the next three features.

## Methodology and caveats

The Mt. Sync column is drawn from the project's own `README.md`, `MARKETING.md` and source tree, so
it is authoritative. The RcloneView column comes from the vendor's published pricing page (feature
lines quoted from their tier list), their interface guide, and their release notes — all vendor
self-description, none independently verified.

Three caveats before using this externally:

- **"Not documented" is not "absent."** RcloneView's pages are marketing material and under-describe
  the product's operational depth. Several rows almost certainly resolve in RcloneView's favour on
  inspection — bandwidth limits and checksum options in particular. Install it before publishing any
  claim that depends on an absence.
- **Versions move.** RcloneView's own download page served mismatched versions across package
  formats (1.5.32 AppImage against 1.5.35 DEB), so treat the version as a snapshot.
- **Pricing was read once.** The $19.80/year and $99.80 lifetime figures and the per-tier feature
  split need re-checking before any public comparison, since this is the claim a vendor is most
  likely to dispute.

### Sources

- [RcloneView pricing and tier features](https://rcloneview.com/src/pricing)
- [RcloneView interface guide](https://rcloneview.com/support/howto/rcloneview-basic/interface-guide)
- [RcloneView two-pane explorer tips](https://rcloneview.com/support/blog/two-pane-explorer-productivity-tips-rcloneview)
- [RcloneView v1.0 release notes](https://rcloneview.com/support/ja/release-notes/v1.0)
- [awesome-rclone client directory](https://awesome-rclone.com/clients-and-guis/)
- [Introducing RcloneView — rclone forum](https://forum.rclone.org/t/introducing-rcloneview-a-gui-front-end-for-rclone/47710)
