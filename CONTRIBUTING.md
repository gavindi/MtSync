# Contributing to Mt. Sync

Thanks for your interest in contributing! This project is a Gnome application to (auto)mount and/or sync your data with rclone and we welcome bug fixes, suggesting features and improvements or  refining documentation.

## Development Setup

```bash
git clone https://github.com/gavindi/MtSync.git
cd MtSync
./build.sh     # Will creeate a build directory and compile the software
```

## Branch Strategy

1. Fork the repo and create a branch off `dev`
2. Make your changes and ensure tests pass
3. Open a PR targeting `dev`

> `master` is the default branch for the latest stable release. All development, releases, and user-facing clones are in branches.  Future releases have their own branch based on their future version number. Incoming work is to target 'dev' and will be merged into a version branch which aligns with the development cycle.

## Translations

Mt. Sync is translated with GNU gettext. The message catalog lives in `po/`:

| File             | Purpose                                              |
|------------------|------------------------------------------------------|
| `po/mtsync.pot`  | Template extracted from the source (do not edit)     |
| `po/<lang>.po`   | One file per language (e.g. `fr.po`, `de.po`)        |

Build-time compilation of `.po` → `.mo` and installation under
`share/locale/<lang>/LC_MESSAGES/mtsync.mo` are handled by CMake, so packaged
deb/rpm/flatpak/snap builds pick translations up automatically. At runtime the
language follows the desktop locale (`LANG`/`LC_MESSAGES`); no language
picker is needed.

### Adding a language

```bash
cd po
msginit --locale=<code> --input=mtsync.pot   # e.g. --locale=es → es.po
```

Edit the resulting `<lang>.po` (Poedit, Lokalize, or any text editor), then:

```bash
cmake --build build update-pot   # refresh template from sources
cmake --build build update-po    # merge new strings into all .po files
msgfmt --check --check-format po/<lang>.po   # validate before committing
```

Also add `Name[<lang>]=…` / `Comment[<lang>]=…` keys for the app's desktop
file in `data/com.mtsync.MtSync.desktop`, and register the file in
`TRANSLATION_FILES` in `CMakeLists.txt`.

### Source string conventions

- Wrap user-visible strings in `_()` (from `<glibmm/i18n.h>`); the `mtsync`
  text domain and localedir are set up automatically in `main.cpp`.
- Use `ngettext()` for plurals and `C_()` where a string needs context.
- Do **not** translate: D-Bus names, resource paths, provider brand names,
  rclone's own messages, internal log files, or JSON field names.
- Job statuses (`success`, `error`, …) are stored in English and translated
  only at display time (`translate_status()` in `job_view.cpp`).

## Ways to Contribute

- **Bug fixes** — Reproduce, write a test, fix it
- **Feature requests** — Be a part of the direction Mt. Sync takes by halping shape the roadmap
- **Documentation** — Install guides, troubleshooting tips, translations