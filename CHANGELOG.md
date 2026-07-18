# Changelog

All notable changes are documented here. Versions follow Semantic Versioning.

## [0.2.1] - 2026-07-19

### Added

- The complete user-provided six-screen project, including its four animated
  media resources, as the built-in default project.
- A shared button-assignment mode that keeps all screens on one button layout.
- Double-click screen renaming in the screen list.

### Changed

- New screens use previous/next page actions by default instead of empty
  short-press assignments.
- Default screens are named `Main`, `Monitor+GIF`, `Small data`, `GIF ghost`,
  `GIF road` and `GIF cat`.

## [0.2.0] - 2026-07-18

### Added

- Automatic visible-history scaling for sparkline graphs, including firmware rendering.
- The custom dense "Экран 8" dashboard from `main.tmon` as the first default page.
- A persistent last-project setting in portable data and command-line `.tmon` opening.
- A consistent application, taskbar, executable and tray icon.
- A visible shortcut explanation and a **Test selected** action button.

### Fixed

- PC actions disappearing after restart because the application returned to the default project.
- Button event deduplication remaining stale after a device reconnect.
- Invalid single-character shortcuts being passed to `SendInput`.

## [0.1.0] - 2026-07-18

Initial public community release.

### Added

- Qt 6/C++20 Windows editor with tray operation and Russian interface.
- Pixel-accurate 128×64 preview, per-widget property panels and six default screens.
- Dynamic text, images, GIF animations, bars, rings, segmented gauges, sparklines, icons and warnings.
- Final-size 1-bit media compilation with live dithering/inversion preview and GIF delta encoding.
- Windows CPU/RAM/GPU telemetry and PresentMon foreground FPS integration.
- Reliable v1 HID handshake, metric updates, package transactions, reconnect recovery and button events.
- Dedicated Trezor One monitor firmware with RAM-only asset storage and bootloader reboot service.
- Firmware wizard, pinned upstream firmware build, portable packaging and GitHub Actions workflows.

[0.1.0]: https://github.com/AbdulKus/trezor_PC_monitor/releases/tag/v0.1.0
[0.2.0]: https://github.com/AbdulKus/trezor_PC_monitor/releases/tag/v0.2.0
[0.2.1]: https://github.com/AbdulKus/trezor_PC_monitor/releases/tag/v0.2.1
