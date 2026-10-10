# 1. Transport

## Devices

Vendor id `0x3670`. The plugin claims three product ids, all shown as
"SIMAGIC Alpha Evo":

| Product id | LED-protocol `deviceModel` byte | Notes |
| --- | --- | --- |
| `0x0500` | `8` | Confirmed from captures. The source says this appears to cover both the "EVO Sport" and "EVO" variants. |
| `0x0501` | `9` | Inferred: EVO / EVO2 / EVO3 are numbered consecutively. |
| `0x0502` | `10` | Same inference. |

An unrecognised product id falls back to `8`. The `deviceModel` byte only appears
in the LED ring header ([04](04-ring-led.md#packet-header)).

## Interfaces

The base exposes more than one USB interface, and the protocol is split across
them:

| Interface | Kernel binding | Used for |
| --- | --- | --- |
| A HID interface (the joystick/gamepad one) | `hidraw` node | All feature reports (`0x80`, `0x81`, `0x23`, `0x91`) and input report `0x01`. |
| A second interface owning **endpoint `0x02`** (OUT) | No HID driver, no hidraw node | The LED ring's frames, as raw interrupt OUT transfers. |

### Finding the configuration interface

The feature reports are **not declared in the HID report descriptor** on
every firmware, so the host cannot rely on parsing the descriptor to find them.
The implementation tries HID interfaces in this order and settles on the first
that answers a `GET 0x81`:

1. Any interface whose descriptor declares feature report `0x81`.
2. Any interface whose top-level usage is Generic Desktop / Joystick (`0x01:0x04`)
   or Gamepad (`0x01:0x05`) — "the one the kernel driver talks to".
3. Every remaining HID interface.

A candidate "answers" if `GET feature 0x81` returns at least 64 bytes and byte 0
is `0x81`. A candidate that does not answer is released again.

### Finding the LED interface

The interface that owns endpoint `0x02` is located by endpoint address. If it
cannot be opened (typically a missing udev rule for `/dev/bus/usb`), the base
still configures normally — only the ring is unreachable.

## Report types

### Feature reports (HID control transfers)

* **GET** with an explicit length of **64** bytes, because the reports are not in
  the descriptor and so no size can be derived. The reply's byte 0 must echo the
  report id.
* **SET** takes the whole buffer with byte 0 as the report id.
* Sizes sent: 64 bytes for `0x80` blocks; **62 bytes** for `0x91` (telemetry and
  wheel configuration). The wheel-config chunks were captured at 62 bytes and are
  kept that way rather than padded to 64.

### Input reports

Report id `0x01` on the HID interface, delivered by the kernel as the device sends
them. See [06](06-input-reports.md).

### Interrupt OUT on endpoint `0x02`

64-byte frames, byte 0 = `0x80`. A report id is part of the wire data on an
interrupt transfer, so the same leading `0x80` appears here as in the feature
protocol; the two protocols are told apart by byte 1 (see below). See
[04](04-ring-led.md).

## Report-id map

| Byte 0 | Byte 1 | Channel | Meaning |
| --- | --- | --- | --- |
| `0x81` | — | GET feature | Settings status ([02](02-status-and-settings.md#get-0x81--settings-status)) |
| `0x23` | — | GET feature | Live base status ([02](02-status-and-settings.md#get-0x23--live-base-status)) |
| `0x80` | `0x01` | SET feature | Settings block 1 |
| `0x80` | `0x02` | SET feature | Settings block 2 |
| `0x80` | `0x10` | SET feature | Extended settings; byte 2 selects: `0x38` ring light, `0x39` signal |
| `0x80` | `0x1E` | **Interrupt OUT** | LED ring packet (protobuf 3 payload) |
| `0x91` | `0x00`–`0x03` | SET feature | Wheel configuration chunk N |
| `0x91` | `0x04` | SET feature | Telemetry |
| `0x01` | — | Input | Buttons and force |

`0x80` therefore carries two unrelated protocols. Byte 1 is the discriminator:
`0x01`, `0x02`, `0x10` select a fixed C-struct settings block; `0x1E` says a
protobuf follows a ten-byte header.

`0x91` likewise carries two unrelated things, split by byte 1.

## Byte order and packing

* All structs are packed (no padding) and laid out exactly as listed in the
  following documents; the source pins every offset with `static_assert`s.
* Integers are little-endian. The single big-endian field is the LED header's
  `payloadTotalSize`.
* The telemetry report contains bit-fields that are laid out **LSB-first**; the
  source refuses to build on a big-endian target rather than reorder them.

## Concurrency (implementation note)

Every transfer is synchronous and may block for as long as the base takes to
answer. The implementation therefore runs all transfers on one worker thread,
which serialises them: a status poll can never fall between the read and the
write of a read-modify-write.
