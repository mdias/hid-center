# Named commands — report `0x06`

A second command channel on the vendor interface. A command is a 64-byte report:

```
byte 0        0x06
byte 1..N     command name, plain ASCII, no terminator
byte N+1..    payload (the device knows each name's payload width)
rest          zero padding to 64 bytes
```

There is no length field or separator. Replies arrive as IN reports with ID `0x06`, starting with
an ASCII tag (`OK`, `BCOK`, …). Most commands are **not acknowledged** — verify by reading back.

| Command | Payload | Reply | Used for |
| --- | --- | --- | --- |
| `t_pf`, `b_pf`, `c_pf` | 23 bytes | none | Write one pedal's whole curve/calibration/deadzone state |
| `damping_max` | 10 bytes | none | Brake maximum force (physical + effective top) |
| `damping_type` | 10 bytes | none | Brake elastomer + force count |
| `brakeCutoff` | 3 bytes | none | Throttle-cut brake point |
| `read_damping_type` | none | `06 'O' 'K' <s\|m\|h>` | Read elastomer |
| `readBrakeCutoff` | none | `06 'B' 'C' 'O' 'K' <en> <lo> <hi>` | Read throttle cut |

Also mentioned in the code but **not implemented/decoded**: `read_raw` (found to be unnecessary),
`write_offset` (the tare command).

## `<letter>_pf` — write one pedal (23-byte payload)

Name is exactly 4 bytes: `<letter> '_' 'p' 'f'` with letter `t` (throttle), `b` (brake), `c` (clutch).
Payload starts at report byte 5.

| Payload offset | Report byte | Size | Field |
| --- | --- | --- | --- |
| 0–10 | 5–15 | 11 × int8 | Response curve points 0 to 10 |
| 11 | 16 | int16 | Physical minimum (physical minimum) |
| 13 | 18 | int16 | Physical maximum (physical maximum) |
| 15 | 20 | int16 | Effective minimum (effective minimum) |
| 17 | 22 | int16 | Effective centre (effective centre) |
| 19 | 24 | int16 | Effective maximum (effective maximum) |
| 21 | 26 | u8 | Start deadzone (start deadzone size) |
| 22 | 27 | u8 | End deadzone (end deadzone size) |

**All-or-nothing.** The command carries a pedal's whole state; whatever is not copied across is what
the device is told to forget. To change one part, send back every other part as read from the blob.
Never acknowledged.

### Effective range derivation

The device does not compute the effective trio; the host must. The exact order of operations
(rounding matters — a byte or two off makes an unchanged device look changed). With
`span = physMax − physMin` as a plain `int` (a full-range axis is 65534, beyond int16):

```
effMin    = (int16)(int)( (startDz * span) * 0.01 + physMin )
effMax    = (int16)(int)( physMax − (endDz * span) * 0.01 )
effCentre = (int16)( effMin + (effMax − effMin) / 2 )        // integer divide
```

## Brake commands

### Brake force ↔ kilograms

360 raw counts per kg (confirmed against two writes that landed exactly).

```
physMax = (int)( kg * 360 / (1 − startDz/100) ) + physMin      // clamped to int16; (1 − dz) floored at 0.01
kg      = round( (physMax − physMin) * (1 − startDz/100) / 360 )
```

Examples: 18 kg with 5 % start deadzone → `(int)(18·360/0.95) = 6821` counts above `physMin`; with 0 %
→ 6480.

Allowed "Maximum force" by elastomer (a UI range chosen by the host): minimum 6 kg; maximum 60 kg
soft, 100 kg medium, 140 kg hard.

Elastomer letters: `s` soft, `m` medium, `h` hard.

### `damping_max` (10-byte payload)

| Payload offset | Size | Field |
| --- | --- | --- |
| 0 | int16 | new physical maximum |
| 2 | int16 | new effective maximum |
| 4–9 | | zero |

Sent whenever the force changes, together with the brake's `b_pf`. Whether the firmware does anything
beyond what `b_pf` already does is unknown.

### `damping_type` (10-byte payload)

| Payload offset | Size | Field |
| --- | --- | --- |
| 0 | int16 | `(int16)(int)(kg * 360 + effective minimum)` — force as a raw count above the effective floor |
| 2 | u8 | elastomer letter `s`/`m`/`h` |
| 3–9 | | zero |

### `brakeCutoff` (3-byte payload)

| Payload offset | Size | Field |
| --- | --- | --- |
| 0 | u8 | enabled (1) / disabled (0) |
| 1 | uint16 | position in **tenths of a percent** of brake travel, 1..500 (0.1 %..50 %) |

### `read_damping_type` → reply

```
06 'O' 'K' <s|m|h> …
```

This reply has no name of its own (just `OK` and a letter), so it can only be told from other `OK`
replies by which question was last asked. **Ask one question at a time.**

### `readBrakeCutoff` → reply

```
06 'B' 'C' 'O' 'K' <enabled: 1 = on> <tenths lo> <tenths hi> …
```

Replies shorter than 8 bytes are ignored. `tenths` is uint16 LE.

Elastomer and throttle cut are held by the device **outside the config blob** and must be read with
these commands.
