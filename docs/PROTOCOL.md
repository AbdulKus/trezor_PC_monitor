# HID protocol v1

The desktop and firmware communicate using fixed 64-byte USB HID reports. Multibyte integers are little-endian.

## Report layout

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 2 | `magic` | ASCII `TM` |
| 2 | 1 | `version` | Protocol version, currently `1` |
| 3 | 1 | `type` | Message type |
| 4 | 1 | `sequence` | Wraparound packet sequence |
| 5 | 1 | `flags` | Type-specific flags |
| 6 | 2 | `payloadLength` | Valid bytes in payload, maximum 52 |
| 8 | 52 | `payload` | Type-specific data |
| 60 | 4 | `crc32` | CRC-32 of bytes 0–59 |

The parser rejects the wrong magic/version, oversized payloads and invalid CRC before dispatching a message.

## Message types

- `HELLO`, `CAPABILITIES` — handshake, firmware/protocol versions and resource limits.
- `PACK_BEGIN`, `PACK_DATA`, `PACK_COMMIT` — transactional upload with transaction ID, offset, total size and final CRC.
- `METRICS` — changed telemetry records or a periodic full snapshot.
- `BUTTON_EVENT` — short/long action with a monotonic event number.
- `SET_PAGE` — select a local page.
- `PING`, `PONG` — liveness.
- `REBOOT_BOOTLOADER` — explicit monitor-firmware reboot request.
- `ACK`, `NACK` — operation result.

The host waits 250 ms for an acknowledgement and retries a report at most three times. Duplicate action events are ignored by monotonic event number. After a broken connection, the host performs a new handshake and a complete package upload.

## Metrics

Metric payloads contain universal fixed-size records:

```text
channelId:u16
status:u8
scaleExponent:i8
value:i32
```

`real_value = value × 10^scaleExponent`. Channel semantics, labels and units live in the project/host; firmware only formats the record requested by a widget. Status distinguishes valid, unavailable and stale readings. Unavailable data is never silently converted to zero.

## Asset upload guarantees

`PACK_BEGIN` establishes an expected size and transaction. `PACK_DATA` is accepted only inside that range. `PACK_COMMIT` validates the completed byte map, package header, all nested bounds and the final checksum before atomically switching the renderer to the new package. The previous layout remains active if validation fails.

Canonical constants and structures are defined in [`protocol/protocol.h`](../protocol/protocol.h). Both targets compile the same implementation to reduce drift.

For `TM_WIDGET_SPARKLINE`, `TM_WIDGET_FLAG_AUTO_RANGE` tells firmware to retain raw centi-unit samples and interpolate the visible history between its current minimum and maximum. Without the flag, the widget's compiled fixed range is used. This is a widget flag rather than a new report type, so protocol-v1 transport remains compatible.
