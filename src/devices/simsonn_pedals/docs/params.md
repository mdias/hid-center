# Params — report `0x02`

A request to the vendor interface makes the device stream a **70-byte params area**, which includes
the live **raw sensor counts** — the values the device's calibration works in (as opposed to the
post-calibration axis report `0x01`).

## Request

64-byte report, everything after byte 0 zero:

```
02 00 00 00 … (64 bytes)
```

Bytes 1.. carry nothing (the device answers identically for `0x00` or `0xA0`). A bare 1-byte write
is refused by hidraw, so pad to 64.

## Reply stream

* Before the first request the vendor interface is **silent**. After one it streams pairs of IN
  reports at ~250 Hz (**~500 reports/s**).
* **One request buys exactly 30 s** (confirmed to 0.1 s); each further request restarts the 30 s.
  The host therefore re-sends once a second. Closing the interface does not stop the stream — it lapses
  30 s after the last request.
* Live values need no prior `read_raw` command; they track the pedals from the first reply.

Each area arrives as two packets:

| Report | Bytes | Meaning |
| --- | --- | --- |
| `02 00 <62 bytes>` | 64 | first packet: area bytes 0..61 |
| `02 01 <8 bytes …>` | ≥ 10 | second packet: area bytes 62..69 (rest of the report ignored) |

A `02 01` is discarded unless a `02 00` was seen immediately before it, so an area is never assembled
from halves of two different pairs. Any other second byte is ignored: report `0x02` also carries the
replies to some named commands, which the poller did not ask for.

## Area layout

The area is a continuation of the blob address space: **area byte 0 = config blob offset `0x43C`**.

| Blob offset | Area offset | Size | Field |
| --- | --- | --- | --- |
| `0x440` | `+0x04` | 3 × int16 | Live raw count for axis 0, 1, 2 (`0x440 + 2·axis`) |
| `0x450` | `+0x14` | 3 × int16 (assumed) | A second triple that also moves with the pedals; **meaning unknown** |
| the rest | | | not decoded |

Raw values are signed 16-bit little-endian in −32767…32767. Observed behaviour:

* Throttle/clutch: raw count is a position; percentage = `(raw − effective minimum) / (effective maximum − effective minimum)`.
* Brake: raw count is a force; `kg = (raw + 32767) / 360`, deadzones excluded.
* A disconnected pedal still reports, drifting noise (a clutch wandered 16 counts in 25 s).
