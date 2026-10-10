# SIMAGIC Alpha Evo — host protocol

How a host program talks to a SIMAGIC Alpha Evo wheelbase (and the steering wheel
attached to it), as implemented by the C++ in `src/devices/simagic_alpha_evo/`.

These documents were written **from that code alone** — its structs, composers,
`static_assert`s, embedded captures and comments. Nothing here was checked against
hardware or against any other source. Where the code itself says a field is a
guess, so does this documentation (see [Confidence](#confidence)).

## Contents

| Document | Covers |
| --- | --- |
| [01-transport.md](01-transport.md) | USB ids, interfaces, the three ways bytes move (feature reports, input reports, interrupt OUT), report-id map, conventions |
| [02-status-and-settings.md](02-status-and-settings.md) | Reading settings (`0x81`) and live status (`0x23`), writing settings (`0x80`, four blocks), value ranges |
| [03-telemetry.md](03-telemetry.md) | The car-telemetry report (`0x91`, sub-command 4) that drives the wheel's shift lights, flags, etc. |
| [04-ring-led.md](04-ring-led.md) | The base's LED ring: a protobuf message fragmented over 64-byte interrupt transfers |
| [05-wheel-config.md](05-wheel-config.md) | The steering wheel's own 240-byte configuration block (`0x91`, sub-commands 0–3): buttons, LED colours, RPM lights, brightness, clutch paddles |
| [06-input-reports.md](06-input-reports.md) | Input report `0x01`: buttons and the applied-force field |
| [07-host-behaviour.md](07-host-behaviour.md) | Sequencing, rates, keep-alives and other timing rules a host has to follow |

## The protocol at a glance

Everything is either a **HID feature report**, a **HID input report**, or an
**interrupt transfer on endpoint 0x02**.

| Report id / channel | Direction | Size | Purpose |
| --- | --- | --- | --- |
| GET feature `0x81` | base → host | 64 | Every FFB/base setting, one block |
| GET feature `0x23` | base → host | 18 (of 64 requested) | What the base is *doing*: state, temps, torque, attached wheel id |
| SET feature `0x80`, byte 1 = `0x01` | host → base | 64 | Settings block 1: force feedback and game effects |
| SET feature `0x80`, byte 1 = `0x02` | host → base | 64 | Settings block 2: angle lock, feedback detail, inertia |
| SET feature `0x80`, byte 1 = `0x10`, byte 2 = `0x38` | host → base | 64 | Settings block 3: ring-light enable/brightness |
| SET feature `0x80`, byte 1 = `0x10`, byte 2 = `0x39` | host → base | 64 | Settings block 4: signal filtering |
| SET feature `0x91`, byte 1 = `0x04` | host → base | 62 | Telemetry (RPM, shift-light preset, ABS/TC/limiter, flag) |
| SET feature `0x91`, byte 1 = `0x00`…`0x03` | host → base | 62 | Steering-wheel configuration, four 60-byte chunks |
| Interrupt OUT, endpoint `0x02`, first byte `0x80`, second `0x1E` | host → base | 64 × 1..6 | Base LED ring colours (protobuf 3) |
| Input report `0x01` | base → host | — | Buttons, plus applied force (i16 at byte 39) |

Two facts shape everything else:

* **Settings are written a block at a time**, and each block contains bytes whose
  meaning is unknown that the base expects to get back unchanged. A host must
  read `0x81` first, copy the block out of it, alter the one field, and write the
  block. See [02](02-status-and-settings.md#read-modify-write).
* **Nothing about the wheel's own configuration, or the LED ring, can be read
  back.** The host has to remember what it sent, and the LED ring has to be
  re-sent every few seconds or the base forgets it.
  See [04](04-ring-led.md#keep-alive) and [05](05-wheel-config.md#nothing-can-be-read-back).

## Conventions used in these documents

* Offsets are **zero-based byte offsets from the start of the report, including
  the report id byte**, unless stated otherwise.
* Multi-byte integers are **little-endian**, with one exception: the LED packet's
  `payloadTotalSize` is big-endian ([04](04-ring-led.md#packet-header)).
* Bit numbers count from the least-significant bit (bit 0 = `0x01`).
* Reports shorter than the size given are followed by zero padding to that size.
* "Unknown" means the source names the field but does not claim to know what it
  does. The base still needs those bytes, so they are documented with the value
  the host sends.

## Confidence

The code marks how much of the protocol was verified. Summarised here so a reader
does not have to rediscover it:

| Area | Status per the source |
| --- | --- |
| Settings blocks 1–4 layout | Transcribed from the `hid-simagic-ff` kernel driver; the composer copies unknown bytes verbatim. |
| Status `0x81` layout | Same origin. Firmware version, ranges and offsets are static-asserted. |
| Base status `0x23` layout | Layout is implemented but the comments disagree on whether real hardware ever answered: one says "never seen answered", another marks the two temperature fields "confirmed on hardware". The host treats a missing answer as normal. |
| Telemetry `0x91`/4 | Only RPM, RPM percent, RPM-LED preset, ABS/TC/limiter bits and the flag byte are filled by the app. Everything else is a fixed default. |
| LED ring | Recovered from six captures (1, 2, 3, 5, 9, 10 colours). The composer is asserted byte-for-byte against them. Stop counts 4, 6, 7, 8 and 11–16 were never captured. |
| Wheel config brightness (byte 230) | Confirmed from captures. |
| Wheel config LED slots, mappings, RPM thresholds/patterns | Inferred from captured blocks and checked against them; the trigger meanings 3–5 (pit limiter, DRS, flag) "have not been seen working". |
| Wheel config bytes 231–232 (clutch mode and ratio) | Tried on a GTC: written, and the paddles watched in the joystick report. Mode values other than 0 and 1 not tried. |
| Wheel config byte 233 (RPM LED brightness) | Named in a table; never exercised. |
| Force field of input report `0x01` | Position and sign are asserted, not derived from a descriptor. |

## Open questions listed in the source

* `StatusReport` bytes 1, 6, 19, 24–46 (23 bytes), 51, 53, 57, 60–63 — meaning unknown.
  Byte 6 "appears to be some kind of smooth level" and is always `0x02`; byte 19 is
  "maybe interpolation level"; bytes 51 and 53 are "high bytes" of the filter and
  slew-rate words; byte 57 is "emergency stop state?".
* Byte 1 of a `0x91` report is a sub-command: `0`–`3` wheel configuration, `4`
  telemetry. The source's header comment says telemetry is "sub-commands 4 and 5";
  nothing in the code sends `5`.
* Many `TelemetryBlock` fields (pedals, lap, timing, tyre pressures, temperatures)
  have names and default values but the app never fills them, so whether the wheel
  acts on them is not established by this code.
* Whether the base's reassembly of a multi-frame LED packet requires exactly the
  header rules the sender uses was never observed; the sender simply copies the
  captured ones.
