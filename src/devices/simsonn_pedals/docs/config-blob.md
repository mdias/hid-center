# Configuration blob — reports `0x03` (read) and `0x04` (write)

The device's whole stored configuration is **1084 bytes (`0x43C`)**. It moves in
**18 packets** of 62 payload bytes: `ceil(1084 / 62) = 18`; packets 1–17 carry 62 bytes each, packet 18
carries the last **30**.

Packet *n* (1-based) covers blob bytes `(n − 1) × 62` … `(n − 1) × 62 + 61`.

## Reading — report `0x03` (host-paced)

Request (host → device), **2 bytes**:

```
03 <n>          n = packet number, 1..18
```

Reply (device → host), 64 bytes:

```
03 <n> <62 payload bytes>
```

Rules the code follows:

* Ask for packet 1; accept only a reply with ID `0x03` **and** the packet number just asked for;
  copy it into place; ask for the next. Anything else on the interface is ignored.
* A reply shorter than `2 + (bytes that packet should carry)` is a failure.
* An unanswered request is repeated every 250 ms; the whole read has an 8 s deadline.
* Packet 1 alone is enough to read the firmware version (blob `+0x00`, `+0x02`), which is how the
  firmware probe works ([procedures.md](procedures.md)).

## Writing — report `0x04` (device-paced)

The reverse of the read, and the device drives it.

```
host → device   04 00                    open the transfer (rest zero-padded to 64 bytes)
device → host   04 01                    device asks for packet 1
host → device   04 01 <62 blob bytes>    64-byte report
device → host   04 02                    ...
   ...
host → device   04 12 <30 blob bytes, zero padded>   packet 18
```

* Each host packet is a full 64-byte report; the payload slicing is the same as the read.
* The host sends packet *k* only when the device has asked for it with `04 <k>`, and only if
  `k == last sent + 1` — an echo of an already-answered request (a resend can cause one) is ignored.
  An unanswered packet is re-sent every 250 ms; deadline 5 s.
* After packet 18 the host **listens for 500 ms**. Silence means success (there is no positive
  acknowledgement). A `04 FE` from the device means the blob was **refused**; the code's reading of
  that is a firmware-version mismatch.
* What the host should send is exactly the blob it read, with edited fields patched: the blob holds
  pin assignments, buttons, LEDs and the firmware version the device checks, none of which is
  decoded. The only patcher is per-axis (below).
* Verify by reading the blob back with report `0x03`.

## Layout

Decoded regions only. Everything else in the blob is **unknown** and must be preserved verbatim.

| Offset | Size | Field |
| --- | --- | --- |
| `0x000` | 2 | firmware version (uint16) |
| `0x002` | 2 | firmware sub-version (uint16) |
| `0x004`–`0x03F` | | not decoded |
| `0x040` | 8 × 52 | eight 52-byte per-axis records; the Plus X uses axes 0–2 |
| `0x1E0`… `0x43B` | | not decoded (the code notes the `0x366`–`0x3D7` span as unknown) |

Firmware version is displayed in **base 16**: `0x2070 / 0x0204` is firmware `2070.204`, not
`8304.516`; no zero padding on the sub-version.

### Per-axis record (52 bytes at `0x040 + 52 × axis`)

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `+0x00` | int16 | effective minimum | effective floor: where output is 0 % |
| `+0x02` | int16 | effective centre | |
| `+0x04` | int16 | effective maximum | effective ceiling: where output is 100 % |
| `+0x06` | int16 | physical minimum | physical minimum: raw count at one end of travel |
| `+0x08` | int16 | physical maximum | physical maximum |
| `+0x0A` | u8 | flags | bits 7..5 = `Filter` (0..7, 0 = none). Bits 4..0 (reported axis, invert, centre, function…) not decoded — preserve |
| `+0x0B` | 11 × int8 | points 0 to 10 | response curve, −100..100, at evenly spaced inputs across the travel |
| `+0x16`–`+0x21` | 12 | | not decoded |
| `+0x22` | u8 | start deadzone size | start deadzone, % of physical span, measured inward from the low end |
| `+0x23` | u8 | end deadzone size | end deadzone, % of span, measured inward from the high end |
| `+0x24`–`+0x33` | 16 | | not decoded |

Absolute offsets for axis *i*: `0x40 + 52i + field`. E.g. brake (axis 1) effective minimum is at `0x74`.

### Two ranges per axis

* **Physical** (`physical minimum/max`) — what a calibration measures: raw counts at the ends of travel.
* **Effective** (`effective minimum/Center/Max`) — the physical range with the deadzones removed. The device
  maps effective min → 0 % and effective max → 100 %. **The host derives it and the device trusts it**;
  it never recomputes it. Anything that changes the physical range or a deadzone must rewrite the
  effective trio. Formula in [commands.md](commands.md#effective-range-derivation).

A factory axis may hold an effective range the deadzones were never taken out of (a clutch was seen
holding `-32767 / 0 / 32767` where the derivation gives `-29490 / 0 / 29490`), so the trio in the blob
can disagree with the derivation.

### Response curve

Eleven signed bytes, Point 0 at 0 % input to point 10 at 100 %, evenly spaced (every 10 %). Output
range −100..100. A straight line (the editor's "Linear" preset) is −100 → +100 across the points.

### Patching an axis

The one blob edit implemented (`patchAxis`) rewrites, for one axis: the five int16 calibration values,
the two deadzone bytes, the eleven curve points, and the `Filter` bits of the flag byte (other flag
bits untouched). `Filter` is stored **only** in the blob; changing it requires the whole-blob write.
