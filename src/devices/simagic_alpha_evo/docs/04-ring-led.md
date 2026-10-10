# 4. Base LED ring

The ring of LEDs on the wheelbase is set by a **second protocol**, not an
extension of the settings protocol. Its message is a small **protobuf 3**
message, wrapped in a ten-byte header and split across as many 64-byte transfers
as it needs.

It was recovered from six captures of the ring set to 1, 2, 3, 5, 9 and 10
colours; the composer is asserted against all six at compile time.

## Channel

| | |
| --- | --- |
| Transfer type | Interrupt OUT |
| Endpoint | `0x02` (the OUT half) |
| Interface | The base's second interface, which the kernel binds no HID driver to and which has no hidraw node |
| Frame size | 64 bytes, always |

Because there is no hidraw node it is unreachable through HID feature reports;
frames go out as raw interrupt transfers on the endpoint itself (on Linux this is
claimed through `/dev/bus/usb`, which needs its own udev permission).

## Packet header

Ten bytes, repeated at the start of every frame.

| Offset | Size | Field | Value | Notes |
| --- | --- | --- | --- | --- |
| 0 | 1 | `fixedHeader` | `0x80` | Doubles as the report id |
| 1 | 1 | `dataType` | `0x1E` | Payload is protobuf 3 (discriminates from the settings protocol) |
| 2 | 1 | `deviceType` | `0x01` |  |
| 3 | 1 | `deviceModel` | `8` (`9`, `10`) | By product id; see [01](01-transport.md#devices) |
| 4 | 1 | `cmdCode` | `0x02` | Run effects |
| 5 | 1 | `cmdGroup` | `0x03` | Control group |
| 6 | 2 | `payloadTotalSize` | **big-endian** | Size of the **whole** protobuf payload, in every frame |
| 8 | 1 | `totalFrames` | 1…6 | |
| 9 | 1 | `frameIndex` | 1-based | |

Two easy mistakes: `cmdCode` comes *before* `cmdGroup` (the opposite of how they
are usually named), and `payloadTotalSize` is big-endian in an otherwise
little-endian protocol (37 bytes is `00 25`).

## Payload: `RunEffects`

Field numbers are as observed in the captures. Only the fields below are used.

```
message RunEffects {
    uint32      ctrl    = 1;      // always 1
    LedKeyColor ledkeys = 5;
}

message LedKeyColor {
    uint32              led_id    = 2;   // always 1  — selects the ring among the base's light groups
    uint32              part_type = 3;   // always 1  — the light group
    repeated LedKeyColorItem colors = 8; // one per colour stop
    uint32              led_scale = 9;   // always 4
}

message LedKeyColorItem {
    uint32  key_id    = 1;   // position of the stop on the ring (see below)
    fixed32 key_color = 2;   // the colour
}
```

The ring uses `ledkeys` (an explicit colour per stop). The alternative `leds`
field, which carries palette indices at a bit depth, is what the captures do *not*
use and must not be substituted without new captures.

### Encoding rules that matter

These are ordinary proto3 rules, but the reference captures depend on them:

* **Zero-valued scalar fields are omitted.** The first colour stop is at position
  0, so its `key_id` field is absent altogether, and a colour of pure black
  (`0x00000000`) would drop its `key_color` field too.
* **Length-delimited fields are never omitted**, even when empty. Each repeated
  colour item is present with its own length.
* **Varints** are base-128, low group first, continuation bit on all but the last
  byte. The length prefix of a submessage is itself a varint, so a submessage of
  128+ bytes needs a two-byte length.
* **`fixed32`** is little-endian.
* Tag = `(field number << 3) | wire type` (0 varint, 2 length-delimited, 5 fixed32).

### Colour word

`key_color` is a fixed32 whose **bytes run red, green, blue, 0**. As an integer
that puts red in the *low* byte — the reverse of the usual `0x00RRGGBB`. The
captured `d3 20 29 00` is R=211, G=32, B=41.

### Stop positions (`key_id`)

`key_id` is a position on the ring, not an index. The ring is modelled as 50 LEDs
× 16 units = **800 units**. For `n` colour stops at index `i`:

| Rule | Formula | Where used |
| --- | --- | --- |
| Original (fits all captures) | `i × (50 / n) × 16` (integer division) | The captures and the compile-time checks |
| "Precise angles" | `(800 / n) × i` (integer division) | Not used by this plugin |

For `n = 1` the position is `0`. Both rules agree for `n = 2` (0, 400), `n = 5`
(0, 160, 320, 480, 640) and `n = 10` (0, 80, …, 720) and diverge otherwise:

| n | Original | "Precise" |
| --- | --- | --- |
| 3 | 0, 256, 512 | 0, 266, 532 |
| 9 | 0, 80, 160, …, 640 | 0, 88, 176, …, 704 |

The captures for 3 and 9 stops match the *original* rule. The "precise" rule was
added later and is not backed by a capture; a host wanting the verified behaviour
should use the original one. Counts 4, 6, 7, 8 and 11–16 have never been captured
under either rule.

Note the original rule's consequence: with 9 or 10 stops the step is 80, so the
last stop never reaches the end of the ring for 9 stops (640 of 800).

### Payload size and colour-stop limit

| Stops | Payload bytes | Frames |
| --- | --- | --- |
| 1 | 17 | 1 |
| 2 | 27 | 1 |
| 3 | 37 | 1 |
| 4 | 47 | 1 |
| 5 | 57 | 2 |
| 10 | 106 | 2 |
| 16 | 166 | 4 |

Each stop beyond the first adds 9–10 bytes (a one-byte position 1–127 saves a
byte, a two-byte 128+ position does not). The app's ceiling is **16 stops**; the
buffers allow six frames (324 bytes of payload), so the limit is a UI choice, not
a wire one.

## Fragmenting

A payload longer than the 54 bytes a frame has room for (64 − 10) is split across
frames:

* Every frame repeats the header; only `frameIndex` changes (1, 2, …).
* `payloadTotalSize` carries the size of the **whole** payload in **every** frame,
  not the part that frame holds.
* `totalFrames` is the same in every frame.
* The body is a plain byte split at 54-byte boundaries. Nothing marks the seam:
  a protobuf field may straddle two frames.
* The final frame is zero-padded. The captured sender leaves stale bytes from
  the previous frame there instead; the receiver evidently does not read past
  `payloadTotalSize`.

If the payload needs more than six frames the packet is refused and nothing is sent.

## Worked example — one red stop

Red = (211, 32, 41), device model 8. Payload (17 bytes):

```
08 01                    ctrl = 1
2A 0D                    ledkeys, 13 bytes
   10 01                 led_id = 1
   18 01                 part_type = 1
   42 05                 colors[0], 5 bytes
      15 D3 20 29 00     key_color = fixed32 (R G B 0);  key_id omitted (0)
   48 04                 led_scale = 4
```

Single frame (64 bytes):

```
80 1E 01 08 02 03 00 11 01 01  08 01 2A 0D 10 01 18 01 42 05 15 D3 20 29 00 48 04  00 … 00
└───── header ─────────────┘   └───────────────── payload (17 bytes) ───────────┘
```
Header: `payloadTotalSize` = `00 11` (17), `totalFrames` = 1, `frameIndex` = 1.

## Worked example — two stops

Two red stops (positions 0 and 400):

```
80 1E 01 08 02 03 00 1B 01 01  08 01 2A 17 10 01 18 01
   42 05 15 D3 20 29 00
   42 08 08 90 03 15 D3 20 29 00
   48 04  00 …
```
The second item carries `key_id = 400` as varint `90 03` (tag `08`), length 8.

## Keep-alive

**The base forgets the colours about ten seconds after the last packet**, after
which the ring reverts to whatever it shows by itself. So a set of colours lasts
only as long as the host keeps restating it; there is no "set and leave".

* The host restates the last colours **every 3 seconds** (chosen so two refreshes
  in a row can be lost and the third still lands before the timeout).
* While an animation is running, the animation's own frames are the keep-alive and
  the timer never fires.
* No reply or acknowledgement is defined for the ring packet; nothing can be read
  back. If a multi-frame packet fails partway, the base is left holding a fragment
  with no way to know what it made of it — the host gives up on that packet and
  sends the whole thing again on the next edit or refresh.

## Animation rate

Animated ring modes send a new packet whenever the colours change, at most every
**40 ms** (25 packets/second), and never with more than one packet awaiting
completion (the next is composed only after the previous transfer returned).
