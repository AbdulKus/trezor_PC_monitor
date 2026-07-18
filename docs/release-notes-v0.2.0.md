# Trezor PC Monitor v0.2.0

This update focuses on everyday editor reliability and graph usability.

- Sparkline graphs now offer automatic visible-history scaling. The option is stored in `.tmon`, previewed on the PC and rendered by the updated firmware.
- The user's dense "Экран 8" dashboard is now the first default screen.
- The application restores the last opened project from portable settings.
- PC shortcuts have inline instructions, a test button and reconnect-safe event sequencing.
- A proper icon is embedded in the Windows executable and shared by the window and tray.

Automatic graph scaling requires the v0.2.0 monitor firmware from this release. Older firmware remains compatible with the pack but uses the graph's stored fixed range.
