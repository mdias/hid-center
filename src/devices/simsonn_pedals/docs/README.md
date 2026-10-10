# SIMSONN Plus X — HID protocol

Reference for how a SIMSONN Plus X pedal set (USB `ddfd:6011`) talks to its host. Where the code says something was measured
on hardware, that is noted; where the code does not know, this documentation says "unknown" rather than guessing.

| Document | Contents |
| --- | --- |
| [transport.md](transport.md) | Interfaces, report IDs, framing, timing, and the general rules of the exchanges |
| [input-report.md](input-report.md) | Report `0x01`: the three pedal axes, streamed unprompted |
| [config-blob.md](config-blob.md) | Report `0x03` (read) and `0x04` (write): the 1084-byte configuration blob and its layout |
| [params.md](params.md) | Report `0x02`: the 70-byte live params area and its 30 s stream |
| [commands.md](commands.md) | Report `0x06`: named ASCII commands (`<x>_pf`, `damping_max`, `damping_type`, `brakeCutoff`, …) |
| [procedures.md](procedures.md) | Recipes: firmware probe, full config read, calibration, saving settings, verifying |

## Conventions

* All multi-byte integers are **little-endian**. `int16` is two's complement.
* Offsets are hexadecimal unless written in decimal; `+0x0A` is a byte offset within the structure
  under discussion.
* "Report" means a HID report *including* its leading report-ID byte, as the host reads and writes it
  through hidraw. "Payload" means the bytes after the ID (and after any packet-number byte).
* *Host → device* is an OUT (or output-style) write of a 64-byte report; *device → host* is an IN
  report.
* Pedal order and axis numbers: throttle = axis 0 (`t`), brake = axis 1 (`b`), clutch = axis 2 (`c`).

## Confidence

| Level | Meaning |
| --- | --- |
| **Confirmed** | Behaviour the code reports as measured against a Plus X (timing, byte values, arithmetic that landed exactly) |
| **Implemented** | The code relies on it and it works, but no measurement is cited |
| **Unknown** | Bytes or behaviour the code does not decode; listed explicitly in each document |
