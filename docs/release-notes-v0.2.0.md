# Trezor PC Monitor v0.2.0

This update focuses on everyday editor reliability and graph usability.

- Sparkline graphs now offer an adaptive upper bound while zero remains at the baseline. Scaling has headroom, fast rise, slow decay and dotted change markers in both preview and firmware.
- Selected widgets move by exactly one OLED pixel with the arrow keys and remain inside the 128×64 canvas.
- Pixel preview keeps a stable zoom and grid when selecting layers with different property sets.
- PresentMon is discovered beside the executable and in the standard Intel PresentMon SDK installation directory; release archives include the official loader.
- FPS auto scaling now uses stable refresh-rate presets with hysteresis and a 10-second confidence window; transient drops remain visible instead of changing the scale.
- A global single-instance lock prevents multiple portable copies from competing for USB HID.
- Russian/English language and Dark/Light/Forest theme selections are persisted.
- Optional burn-in protection adds a project-level safe border and clockwise two-minute firmware pixel shift.
- The first default screen is the exact attached `Экран 8` layout, renamed to `Основной`.
- The application restores the last opened project from portable settings.
- PC shortcuts have inline instructions, a test button and reconnect-safe event sequencing.
- A proper icon is embedded in the Windows executable and shared by the window and tray.

Automatic graph scaling requires the v0.2.0 monitor firmware from this release. Older firmware remains compatible with the pack but uses the graph's stored fixed range.
