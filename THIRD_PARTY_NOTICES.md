# Third-party notices

Trezor PC Monitor builds on the following projects. This list is provided for convenience; the license files shipped with each dependency are authoritative.

## Trezor firmware

Embedded platform code is built against a pinned revision of the official [Trezor firmware](https://github.com/trezor/trezor-firmware). Relevant legacy/common portions are licensed under LGPL-3.0-or-later. The exact revision is stored in `UPSTREAM_TREZOR_REF`.

## Qt

The Windows application uses [Qt 6](https://www.qt.io/) dynamically. Open-source Qt modules are available under LGPLv3/GPL terms. Portable packages created by the release workflow include the Qt license text and dynamically linked Qt DLLs.

## PresentMon

[PresentMon](https://github.com/GameTechDev/PresentMon) is an optional FPS/telemetry provider distributed under the MIT License. Its official installer may be included as a separate, unmodified file in portable release archives.

## libusb

[libusb](https://github.com/libusb/libusb) is an optional USB runtime component distributed under LGPL-2.1-or-later. Its DLL may be included separately in portable release archives.

## Spleen

Pixel fonts under `app/fonts/spleen` are from the [Spleen](https://github.com/fcambus/spleen) project. The bundled license is in `app/fonts/LICENSE`.

Trezor is a trademark of SatoshiLabs. This community project is not affiliated with or endorsed by SatoshiLabs, Qt Group, Intel/GameTechDev or the other upstream projects.
