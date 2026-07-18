#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="${1:-$ROOT/build/trezor-firmware}"
REF="$(tr -d '\r\n' < "$ROOT/UPSTREAM_TREZOR_REF")"

if [[ ! -d "$UPSTREAM/.git" ]]; then
  mkdir -p "$(dirname "$UPSTREAM")"
  git clone https://github.com/trezor/trezor-firmware.git "$UPSTREAM"
fi
git -C "$UPSTREAM" fetch --depth=1 origin "$REF"
git -C "$UPSTREAM" checkout --detach FETCH_HEAD
git -C "$UPSTREAM" submodule update --init --depth=1 \
  vendor/libopencm3 vendor/nanopb vendor/ts-tvl

mkdir -p "$UPSTREAM/common/pcmonitor" "$UPSTREAM/legacy/pcmonitor"
cp "$ROOT/protocol/protocol.h" "$ROOT/protocol/protocol.c" \
   "$UPSTREAM/common/pcmonitor/"
cp "$ROOT/firmware/Makefile" "$ROOT/firmware/main.c" \
   "$ROOT/firmware/monitor.c" "$ROOT/firmware/monitor.h" \
   "$ROOT/firmware/renderer.c" "$ROOT/firmware/renderer.h" \
   "$UPSTREAM/legacy/pcmonitor/"

if [[ -z "${IN_NIX_SHELL:-}" && "${PCMONITOR_IN_NIX:-0}" != "1" ]]; then
  exec nix-shell "$UPSTREAM/shell.nix" --run \
    "PCMONITOR_IN_NIX=1 bash '$ROOT/scripts/build-firmware.sh' '$UPSTREAM'"
fi

cd "$UPSTREAM"
uv run make -C legacy/vendor/libopencm3 lib/stm32/f2
PRODUCTION=0 uv run make -C legacy
PRODUCTION=0 uv run make -C legacy/pcmonitor clean
PRODUCTION=0 uv run make -C legacy/pcmonitor APPVER=1.8.0
uv run python legacy/bootloader/firmware_sign_dev.py \
  -f legacy/pcmonitor/pcmonitor.bin

OUTPUT="$ROOT/artifacts/firmware"
mkdir -p "$OUTPUT"
cp legacy/pcmonitor/pcmonitor.bin "$OUTPUT/pcmonitor-signed.bin"
tail -c +257 legacy/pcmonitor/pcmonitor.bin > "$OUTPUT/pcmonitor-inner.bin"
sha256sum "$OUTPUT/pcmonitor-inner.bin" "$OUTPUT/pcmonitor-signed.bin" \
  > "$OUTPUT/SHA256SUMS.txt"
echo "Firmware artifacts are ready in $OUTPUT"
