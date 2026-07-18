# Contributing

Issues and focused pull requests are welcome. Please search existing issues first and keep changes scoped to one problem.

## Development workflow

1. Fork the repository and create a branch from `main`.
2. Build with `BUILD_TESTING=ON` and reproduce the issue before changing code.
3. Add or update tests for parsers, project compilation, media conversion or transport behavior.
4. Run the full desktop test suite. Firmware parser changes should also be exercised with malformed and truncated input.
5. Update user documentation when behavior, limits or UI changes.

Use C++20 for desktop code and the existing C style for legacy firmware. Keep the shared wire format in `protocol/`; do not create host-only copies of protocol constants.

## Bug reports

Include:

- application version and Windows build;
- Trezor model and firmware hash/version;
- exact reproduction steps and expected/actual behavior;
- a minimal `.tmon` project when the problem is layout-specific;
- relevant logs with personal paths, window titles and process names redacted if necessary.

Never attach wallet seeds, PINs or other secrets. This project should be used only on a dedicated non-wallet device.

## Release checklist

- `VERSION` matches the `vMAJOR.MINOR.PATCH` tag;
- Windows CI and tests pass;
- firmware is rebuilt from `UPSTREAM_TREZOR_REF` and hashes are recorded;
- portable package starts on a clean Windows x64 machine;
- real-device checks cover upload, reconnect, GIF, buttons and bootloader entry;
- `CHANGELOG.md` and user-facing documentation are updated.
