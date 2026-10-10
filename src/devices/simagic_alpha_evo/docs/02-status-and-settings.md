# 2. Status reads and settings writes

Settings are read with one GET and written with four possible SET blocks. This is
the protocol the source describes as transcribed from the `hid-simagic-ff` kernel
driver (`hid-simagic-settings.h`, `hid-simagic-sysfs.c`).

## GET `0x81` — settings status

One 64-byte block holding every setting the base has. Send
`GET_FEATURE(report 0x81, length 64)`; byte 0 of the reply is `0x81`. Anything
shorter than 64 bytes, or with a different byte 0, is an error.

| Offset | Size | Field | Type | Notes |
| --- | --- | --- | --- | --- |
| 0 | 1 | `reportId` | u8 | `0x81` |
| 1 | 1 | unknown01 | u8 | Unknown |
| 2 | 2 | `maxAngle` | u16 | Maximum rotation, degrees, 90–2520 |
| 4 | 2 | `ffStrength` | i16 | Force-feedback strength, −100…100 |
| 6 | 1 | unknown06 | u8 | "Some kind of smooth level"; always `0x02` in practice |
| 7 | 1 | `wheelRotationSpeed` | u8 | 0–100 |
| 8 | 1 | `mechanicalCentering` | u8 | 0–100 |
| 9 | 1 | `mechanicalDamper` | u8 | 0–100 |
| 10 | 1 | `centerDamper` | u8 | 0–100 |
| 11 | 1 | `mechanicalFriction` | u8 | 0–100 |
| 12 | 1 | `gameCentering` | u8 | 0–200 |
| 13 | 1 | `gameInertia` | u8 | 0–200 |
| 14 | 1 | `gameDamper` | u8 | 0–200 |
| 15 | 1 | `gameFriction` | u8 | 0–200 |
| 16 | 2 | `angleLock` | u16 | Degrees, 90–2520 but bounded by `maxAngle` |
| 18 | 1 | `feedbackDetail` | u8 | 0–100 |
| 19 | 1 | unknown19 | u8 | "Maybe interpolation level?" Copied into block 2 |
| 20 | 1 | `angleLockStrength` | u8 | 0 Soft, 1 Normal, 2 Firm |
| 21 | 1 | `syncSoftLock` | u8 | Not exposed by the app; copied through |
| 22 | 1 | `mechanicalInertia` | u8 | 0–100 |
| 23 | 1 | `torqueLimit` | u8 | Not exposed by the app; copied through |
| 24 | 23 | unknown24 | u8[23] | Unknown |
| 47 | 1 | `ringLight` | u8 | Bit 7 = enabled; bits 0–6 = brightness 0–100 |
| 48 | 1 | `dynamicPredictionLevel` | u8 | |
| 49 | 1 | `unknown49` | u8 | |
| 50 | 1 | `filterLevel` | u8 | 0–20. Low byte of a 16-bit word |
| 51 | 1 | unknown51 | u8 | "Filter level high byte?" Copied into block 4 |
| 52 | 1 | `slewRateControl` | u8 | 0–100. Low byte of a 16-bit(?) word |
| 53 | 1 | unknown53 | u8 | "Slew rate high byte?" |
| 54 | 1 | `steeringTorqueAssist` | u8 | |
| 55 | 1 | `wheelChannel` | u8 | |
| 56 | 1 | `powerSupplyVoltage` | u8 | |
| 57 | 1 | unknown57 | u8 | "Emergency stop state?" |
| 58 | 2 | `firmwareVersion` | u16 | See below |
| 60 | 4 | unknown60 | u8[4] | Not read |

**Firmware version.** The raw field is a u16. The app displays `raw % 1000` (the
kernel driver shows "the low three digits") and also thresholds the raw value to
decide the base's torque class.

## GET `0x23` — live base status

What the base is *doing*, as opposed to `0x81`, which is what it is configured to
do. Send `GET_FEATURE(report 0x23, length 64)`; only the first 18 bytes are
parsed. A reply shorter than 18 bytes, or whose byte 0 is not `0x23`, is treated
as "not available" — this is **not** an error for the host as a whole (see
[07](07-host-behaviour.md#polling)).

| Offset | Size | Field | Type | Notes |
| --- | --- | --- | --- | --- |
| 0 | 1 | `reportId` | u8 | `0x23` |
| 1 | 1 | `state` | u8 | 1 align, 2 run, 3 stop, 4 fault, 5 tune, 6 self-test, 7 zero |
| 2 | 1 | `error` | u8 | |
| 3 | 1 | `motorTemp` | u8 | °C ("confirmed on hardware") |
| 4 | 1 | `mosfetTemp` | u8 | °C ("confirmed on hardware") |
| 5 | 1 | `maxTorque` | u8 | N·m |
| 6 | 2 | `feedbackTorque` | i16 | Signed |
| 8 | 1 | `input` | u8 | |
| 9 | 1 | `output` | u8 | |
| 10 | 1 | `wheelId` | u8 | Which steering wheel is attached. `4` = GTC; `0` when none is known |
| 11 | 1 | `canId` | u8 | |
| 12 | 1 | `canFdId` | u8 | |
| 13 | 2 | `busVoltage` | u16 | Unsigned |
| 15 | 1 | `version` | u8 | Not the firmware version (that is in `0x81`) |
| 16 | 1 | `flags` | u8 | Bit 0 (`0x01`) and bit 1 (`0x02`) are named, meaning unknown |
| 17 | 1 | unknown17 | u8 | |

`wheelId` is the one field the wheel-configuration protocol depends on: the app
only sends the [wheel configuration](05-wheel-config.md) when it reads `4` (GTC),
because the block's mappings and LED slots mean something else on other wheels.

## SET `0x80` — settings blocks

Four blocks, each sent as `SET_FEATURE(report 0x80, 64 bytes)` (the block bytes
followed by zero padding). Byte 0 is `0x80`; byte 1 selects the block.

### Read-modify-write

**Nothing is written a field at a time.** To change a setting:

1. `GET 0x81` (fresh — never reuse an earlier read).
2. For each block the change touches, build the block from that status, copying
   **every** field including the unknown ones.
3. Overwrite the changed field(s).
4. `SET 0x80` the block(s), in order 1, 2, 3, 4, skipping untouched ones.
5. Optionally `GET 0x81` again to see what the base actually accepted (it may
   clamp values itself).

The read in step 1 is only for the unknown bytes; it does not need `0x23`.

### Block 1 — selector `0x01`: force feedback and game effects (16 bytes)

| Offset | Size | Field | Copied from status offset |
| --- | --- | --- | --- |
| 0 | 1 | `0x80` | — |
| 1 | 1 | `0x01` | — |
| 2 | 2 | `maxAngle` | 2 |
| 4 | 2 | `ffStrength` | 4 |
| 6 | 1 | unknown | **Always `0x02`** — not copied from status byte 6 |
| 7 | 1 | `wheelRotationSpeed` | 7 |
| 8 | 1 | `mechanicalCentering` | 8 |
| 9 | 1 | `mechanicalDamper` | 9 |
| 10 | 1 | `centerDamper` | 10 |
| 11 | 1 | `mechanicalFriction` | 11 |
| 12 | 1 | `gameCentering` | 12 |
| 13 | 1 | `gameInertia` | 13 |
| 14 | 1 | `gameDamper` | 14 |
| 15 | 1 | `gameFriction` | 15 |

The block mirrors status offsets 2–15 directly.

### Block 2 — selector `0x02`: angle lock, detail, inertia (10 bytes)

| Offset | Size | Field | Copied from status offset |
| --- | --- | --- | --- |
| 0 | 1 | `0x80` | — |
| 1 | 1 | `0x02` | — |
| 2 | 2 | `angleLock` | 16 |
| 4 | 1 | `feedbackDetail` | 18 |
| 5 | 1 | unknown | **19** |
| 6 | 1 | `angleLockStrength` | 20 |
| 7 | 1 | `syncSoftLock` | 21 |
| 8 | 1 | `mechanicalInertia` | 22 |
| 9 | 1 | `torqueLimit` | 23 |

The block is *not* laid out at the same offsets as the status: it is the status's
bytes 16–23 moved down to 2–9.

**Angle-lock clamp.** The base clamps `angleLock` to `maxAngle`. When a write
changes both, clamp the new lock against the **new** maximum, not the one in the
last status read.

### Block 3 — selector `0x10`, sub-selector `0x38`: ring light (6 bytes)

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 1 | `0x80` | |
| 1 | 1 | `0x10` | extended block |
| 2 | 1 | `0x38` | ring light |
| 3 | 1 | unknown | always `0x00` |
| 4 | 1 | unknown | always `0x01` |
| 5 | 1 | `ringLight` | bit 7 enable, bits 0–6 brightness 0–100 |

Enable and brightness share the byte, so each change must touch only its own
bits: switching the light off must not zero the brightness and changing the
brightness must not clear the enable bit. Start from the status's byte 47.
Changing brightness needs a further packet
before the base refreshes its LEDs: the host resends the current ring colours (see [04](04-ring-led.md)) after the write.

### Block 4 — selector `0x10`, sub-selector `0x39`: signal filtering (10 bytes)

| Offset | Size | Field | Copied from status offset |
| --- | --- | --- | --- |
| 0 | 1 | `0x80` | — |
| 1 | 1 | `0x10` | — |
| 2 | 1 | `0x39` | signal |
| 3 | 1 | unknown | always `0x00` |
| 4 | 1 | unknown | always `0x07` |
| 5 | 1 | `dynamicPredictionLevel` | 48 |
| 6 | 1 | `unknown06` | 49 |
| 7 | 1 | `filterLevel` | 50 |
| 8 | 1 | unknown | 51 |
| 9 | 1 | `slewRateControl` | 52 |
| 10 | 1 | unknown | 53 |
| 11 | 1 | `steeringTorqueAssist` | 54 |

Bytes 5–11 mirror status bytes 48–54: a byte pair (dynamic prediction level and an
unknown byte), then filter level and slew rate as 16-bit words, then the steering
torque assist.

## Settings and their blocks

| Setting | Range | Block | Where in block |
| --- | --- | --- | --- |
| Force-feedback strength | −100…100 | 1 | 4 (i16) |
| Mechanical centering | 0–100 | 1 | 8 |
| Mechanical damper | 0–100 | 1 | 9 |
| Center damper | 0–100 | 1 | 10 |
| Mechanical friction | 0–100 | 1 | 11 |
| Game centering | 0–200 | 1 | 12 |
| Game inertia | 0–200 | 1 | 13 |
| Game damper | 0–200 | 1 | 14 |
| Game friction | 0–200 | 1 | 15 |
| Maximum rotation (°) | 90–2520 | 1 | 2 (u16) |
| Wheel rotation speed | 0–100 | 1 | 7 |
| Angle lock (°) | 90–`maxAngle` | 2 | 2 (u16) |
| Feedback detail | 0–100 | 2 | 4 |
| Angle lock strength | 0 Soft / 1 Normal / 2 Firm | 2 | 6 |
| Mechanical inertia | 0–100 | 2 | 8 |
| Ring light enable | 0/1 | 3 | 5, bit 7 |
| Ring light brightness | 0–100 | 3 | 5, bits 0–6 |
| Filter level | 0–20 | 4 | 7 |
| Slew rate control | 0–100 | 4 | 9 |
| Dynamic prediction level | 0–10 | 4 | 5 |
| Steering torque assist | 0–100 | 4 | 11 |

Ranges come from the clamps in the kernel driver's sysfs store/sanitize functions;
the host clamps a value to its range before writing.

Not exposed by the app, but present and copied through untouched: `syncSoftLock`,
`torqueLimit`, `wheelChannel`,
`powerSupplyVoltage`.

## Worked example

Set force-feedback strength to 75 and mechanical damper to 20 (block 1 only), with
a status whose block-1 fields were maxAngle 900 (`84 03`), rotation speed 50,
centering 30, center damper 10, friction 5, game 100/100/100/100:

```
80 01 84 03 4B 00 02 32 1E 14 0A 05 64 64 64 64  00 … (zero-padded to 64)
      └maxAngle┘└ff┘ └unk (always 02)
```

Block 3, turning the ring light on at brightness 80 (`0x80 | 0x50 = 0xD0`):

```
80 10 38 00 01 D0  00 … (zero-padded to 64)
```
