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

## Ways to Contribute

- **Bug fixes** — Reproduce, write a test, fix it
- **Feature requests** — Be a part of the direction Mt. Sync takes by halping shape the roadmap
- **Documentation** — Install guides, troubleshooting tips, translations