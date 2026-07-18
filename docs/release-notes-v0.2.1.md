# Trezor PC Monitor v0.2.1

This release refreshes the built-in project and makes multi-screen button setup easier.

- The complete supplied `.tmon` project is now the default, including all four animated media resources.
- Its six pages are named `Main`, `Monitor+GIF`, `Small data`, `GIF ghost`, `GIF road` and `GIF cat`.
- The Buttons tab has a **Shared buttons for all screens** option. Enabling it copies the selected screen's assignments to every screen and keeps later edits synchronized.
- Newly created screens start with previous/next page actions on short presses. When shared mode is active, they inherit the common assignments instead.
- Double-click a screen name to rename it without leaving the screen list.
- Project format v5 stores the shared-button setting while remaining compatible with existing `.tmon` projects.

The monitor firmware is unchanged from v0.2.0; matching firmware images are included for convenient clean installation.
