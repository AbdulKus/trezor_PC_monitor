# Architecture

## Data flow

```text
Windows counters / PresentMon / optional vendor telemetry
                         │
                         ▼
               normalized metric registry
                         │ changed values + snapshots
                         ▼
Qt editor ──compile──► 64-KiB asset pack ──USB HID──► Trezor firmware
   ▲                                                        │
   └──────────── button events / page state ─────────────────┘
```

The editor owns semantic channel names, units, formatting and actions. Firmware intentionally knows only numeric channel IDs and small drawing primitives. That keeps the embedded parser stable when new Windows sensors are added.

## Desktop application

- **Project model/compiler:** screens, widgets, actions, fonts and source media are serialized as a project and compiled into a bounded binary pack.
- **Media pipeline:** images and every GIF frame are resized to the widget's physical dimensions, thresholded/dithered to 1 bit and encoded. Identical transformed variants are deduplicated.
- **Telemetry:** Windows APIs provide CPU, memory and GPU engine counters. PresentMon supplies foreground presentation timing and supported dynamic metrics. Extended GPU access is gated by recent activity.
- **Transport:** enumerates the application/bootloader interfaces, performs the v1 handshake, retries acknowledged transfers and restores the package after reconnect.
- **Actions:** local page actions execute in firmware; desktop actions are deduplicated before invoking SendInput, media keys, programs or explicitly enabled commands.

## Firmware

`firmware/main.c` wires the legacy platform setup, OLED, buttons and USB transport. `monitor.c` validates protocol and package state, tracks metrics and emits button events. `renderer.c` draws bounded widgets directly for the 128×64 framebuffer.

The active package resides only in RAM. Parser code treats all USB and pack fields as untrusted: lengths, offsets, indices and nested resources are checked before use.

## Resource limits

- package RAM: 64 KiB;
- screens: 8;
- widgets per screen: 24;
- fonts: 8;
- images: 64;
- animations: 16;
- telemetry channels: 64;
- display refresh: at most 20 FPS;
- animation: at most 12 FPS.

The compiler reports exact usage and refuses a project that exceeds firmware limits.
