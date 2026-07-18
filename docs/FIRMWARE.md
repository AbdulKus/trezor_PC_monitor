# Firmware and flashing

## Safety first

This firmware is unofficial. Flashing it can erase the Trezor storage, and it must not be used as a cryptocurrency wallet. Use a dedicated Trezor One (model T1B1). Verify that the recovery seed for any previous wallet is complete and readable before connecting the device to the flashing wizard.

The application never presses or acknowledges a physical confirmation on behalf of the user.

## Images

Two formats are produced:

- `pcmonitor-inner.bin` starts with `TRZF`. This is the inner firmware image expected by the legacy bootloader upload flow and by the built-in wizard.
- `pcmonitor-signed.bin` starts with `TRZR`. This is the signed archive container retained for distribution and inspection. Do not send the outer `TRZR` container where the bootloader expects `TRZF`.

Prebuilt copies live in `firmware/prebuilt`. Release assets include both formats and SHA-256 checksums.

## Reproducible build

On a Linux system with Git and Nix:

```bash
git clone https://github.com/AbdulKus/trezor_PC_monitor.git
cd trezor_PC_monitor
./scripts/build-firmware.sh
```

The script:

1. reads the exact official Trezor commit from `UPSTREAM_TREZOR_REF`;
2. clones/fetches that revision into `build/trezor-firmware`;
3. copies `protocol/` to `common/pcmonitor` and `firmware/` to `legacy/pcmonitor`;
4. enters the upstream Nix build environment when needed;
5. builds libopencm3, the legacy common library and the `pcmonitor` target;
6. creates `TRZF`, `TRZR` and `SHA256SUMS` in `artifacts/firmware`.

The manual **Firmware** GitHub Action runs the same script and publishes its outputs as a workflow artifact. A successful CI build is useful evidence, but it is not a substitute for checking the hash and confirming the device model before flashing.

## Runtime design

The application firmware contains a 64-KiB RAM asset buffer. Layouts, glyphs and converted animation frames are not written to flash; after a physical disconnect the desktop app uploads the active package again. A package is activated only after its size, transaction and CRC checks succeed, so an interrupted transfer cannot start a partial layout.

The renderer is event-driven, capped at 20 FPS, with animation capped at 12 FPS. Metric timeouts mark values stale. Holding both buttons opens the system page; monitor firmware can request a reboot to bootloader through the platform service.

## Verifying a download

PowerShell example:

```powershell
Get-FileHash .\pcmonitor-inner.bin -Algorithm SHA256
Get-FileHash .\pcmonitor-signed.bin -Algorithm SHA256
```

Compare the result with the release `SHA256SUMS.txt`. The firmware wizard additionally validates model, header, bounds and internal hashes before erase/upload begins.
