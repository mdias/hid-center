# 6. Input reports

The base sends ordinary HID **input report `0x01`** on its HID interface. The
host does not request it; the kernel delivers it. Three things are read
from it: the pressed buttons, the clutch paddles and the applied force. None of
them involves any configuration command.

All are read only while something on screen needs them; the interface is opened
for them on demand and released again afterwards, because while it is open the
kernel delivers every report on it (a stream in the kilohertz range) whether or
not anything decodes it.

## Which interface

The interface whose HID report descriptor uses report ids **and** declares an
input report with id `0x01`. A descriptor with no report ids has one unnumbered
input report, and nothing says it is the wanted one, so such an interface is not
used.

## Buttons

Buttons are found from the **report descriptor**, not from fixed offsets:

* Walk the fields of input report `0x01`.
* Keep the fields that are not constant, not arrays, are **1 bit wide**, and carry
  usages on the **Button usage page (`0x09`)**.
* For element `e` of such a field, the usage is `usages[e]` (if there are fewer
  usages than elements, the last usage repeats). Its low 16 bits are the button
  number; usage 0 is ignored.
* The bit position in the received report is `8 + field.bitOffset + e` — the `8`
  is the leading report-id byte, since the descriptor's offsets are into the
  payload.

A button is **pressed** when its bit is set. The button count is the highest
button number found. These are the same "joystick button N" numbers the wheel's
[button mappings](05-wheel-config.md#button-mappings-offsets-0-39) refer to.

## Clutch paddles

The clutch paddles are two of the report's axes, found from the **report
descriptor** by usage, like the buttons:

| Paddle | Usage (Generic Desktop) | Range |
| --- | --- | --- |
| Left | `0x36` Slider | 0 released … 4095 pulled fully |
| Right | `0x35` Rz | 0 released … 4095 pulled fully |

Seen on a GTC with its dual-clutch module by pulling each paddle in turn. The
range is the descriptor's logical range, 12 bits on current firmware.

**With the paddles synced** ([05](05-wheel-config.md#clutch-paddles-offsets-231232))
both drive Slider and Rz stays 0, so where each paddle is cannot be told: only
what they make together.

**The clutch ratio is said back in byte 38** of the report as received (byte 37
of the payload): the percentage last written to the wheel configuration's
offset 232, following each write straight away. The app does not read it.

## Applied force

The force the base is applying is carried in **two bytes of report `0x01` at a
fixed offset**: a signed 16-bit little-endian integer starting at byte 39 of the
report as received (byte 38 of the payload, after the report id). On current
firmware the report is 50 bytes long and the field is no longer at its end; it
was found as the one byte that changes continuously on an idle wheel.

```
raw   = int16( report[39] | report[40] << 8 )
force = raw / 32768         →  −1.0 … +1.0
```

* The divisor is 32768 (not 32767) so −32768 maps to exactly −1.0; the positive
  end stops just inside +1.0.
* If the descriptor declares report ids, a report is used only when byte 0 is
  `0x01`. The report must be long enough to reach the field (41 bytes with an id,
  40 without).
* This is a device-specific addition to a gamepad report and the descriptor is unlikely to
  name it, so the position (byte 39) and the sign are **asserted, not
  derived**. The source calls out that if a trace came out mirrored or reading a
  different field, that one decode function is the whole of what would change.
