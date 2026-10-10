# Input report `0x01` — pedal axes

Sent unprompted by the device on the generic-desktop interface, at about 500 Hz, whenever the
interface is being read. Nothing is written to start it.

| Byte | Field | Type |
| --- | --- | --- |
| 0 | Report ID `0x01` | u8 |
| 1–2 | Axis 0 | int16 LE |
| 3–4 | Axis 1 | int16 LE |
| 5–6 | Axis 2 | int16 LE |

(The code requires at least 7 bytes; whether the report carries more is not decoded.)

## Range

Logical range **-32767 … +32767** (confirmed from the report descriptor). A pedal at rest reports
`0x8001` (−32767), so a released pedal is at the *minimum*; percentage = `(v + 32767) / 65534 × 100`.

These values are **after the device's own calibration and response curve** have been applied. The
counts underneath are in the params area ([params.md](params.md)).

## Which axis is which pedal

| Axis | Pedal | Letter in `<x>_pf` |
| --- | --- | --- |
| 0 | Throttle | `t` |
| 1 | Brake | `b` |
| 2 | Clutch | `c` |

Determined on hardware (pressing the throttle moves axis 0, the brake axis 1), cross-checked by the
params capture: axis 0 tops out just past its stored physical maximum (a pedal with a physical stop),
axis 1 runs far beyond its own (a load cell, whose physical maximum is a force setting, not an end of
travel).

The same index selects the pedal's per-axis record in the [config blob](config-blob.md) (its
source axis is 0, 1, 2 on a Plus X) and its raw value in the [params area](params.md).

## Brake load cell

The brake reading is a force. Linear model used by the code (only the factor is confirmed):

```
kg = (raw − (−32767)) / 360        (360 counts per kilogram; zero = rest = −32767)
```

An unloaded brake reads physical minimum on every axis seen so far. A device tared with
`write_offset` (unimplemented here) would have a different zero. Not checked against a known weight.
