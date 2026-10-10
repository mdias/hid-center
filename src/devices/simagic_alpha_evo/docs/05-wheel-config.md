# 5. Steering-wheel configuration

The wheel attached to the base has its own configuration — what each physical
input presses, the colour and trigger of each lit button, the shift-light
patterns, the LED brightness and how the clutch paddles work together. It is written through the base as a single
**240-byte block**, sent in four 60-byte chunks.

## Report

`SET_FEATURE(report 0x91)`, **62 bytes**: report id, sub-command, one chunk.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `0x91` |
| 1 | 1 | sub-command **N** = chunk number, 0–3 |
| 2 | 60 | bytes `N × 60` … `N × 60 + 59` of the block |

Report id `0x91` is shared with [telemetry](03-telemetry.md) (sub-command 4).

The sizes are as captured: 62 bytes, not padded to 64.

## Nothing can be read back

**The base never answers with this block.** There is no read-modify-write here.
Every byte the host does not itself set must come from somewhere, and the source
takes them from a captured block (`defaultWheelConfig()`, taken from a GTC). Every
write therefore restates the whole of that chunk as the host last knew it —
including button mappings, RPM patterns and clutch settings the host may not
understand. Sending only the chunk that holds a change limits how much is
restated but does not avoid it.

## The block

Offsets are into the 240-byte block (0-based, *not* including the two header
bytes). "Chunk" is the sub-command that carries the byte: `offset / 60`.

| Offset | Size | Chunk | Contents |
| --- | --- | --- | --- |
| 0–39 | 40 | 0 | **Button mappings**: one byte per input of the wheel |
| 40–49 | 10 | 0 | **RPM thresholds**: one per pattern, pattern 9's first |
| 50–79 | 30 | 0, 1 | **LED slots**: one byte per lit part of the wheel |
| 80–229 | 150 | 1, 2, 3 | **RPM LED patterns**: 15 LEDs × 10 patterns |
| 230 | 1 | 3 | **LED brightness**, percent (confirmed) |
| 231 | 1 | 3 | **Clutch mode**: 0 synced, 1 independent (tried on hardware) |
| 232 | 1 | 3 | **Clutch ratio**, percent (tried on hardware) |
| 233 | 1 | 3 | RPM LED brightness (never exercised) |
| 234–239 | 6 | 3 | Unused |

Chunk boundaries: chunk 0 = bytes 0–59, chunk 1 = 60–119, chunk 2 = 120–179,
chunk 3 = 180–239.

### Button mappings (offsets 0–39)

One byte per wheel input. **Byte N holds the number of the joystick button that
input presses, minus one** (so `0x00` = button 1, up to `0xFF` = button 256; the
app offers buttons 1–40). Which input is which physical control depends on the
wheel model.

For a GTC (wheel id 4), every input `0…39` presses a button except `18`, `20`,
`23` and `26`. The 36 that do: 8 buttons, two 7-way switches, four tuners and two
encoders; the shift paddles are inputs `12` and `13` (buttons 13 and 14). The
clutch paddles are axes and have no mapping. The default mapping gives input `N`
the button `N + 1`.

Two inputs mapped to the same joystick button press the same button; the app
warns about that but does not forbid it.

### RPM thresholds (offsets 40–49)

Ten bytes, each a percentage (0–100) of engine RPM. They go out **reversed**:
offset 40 holds pattern 9's threshold and offset 49 holds pattern 0's, i.e.
`threshold(pattern) = block[40 + 9 − pattern]`.

Pattern 0 is the top of the range — the one that shows a whole shift light lit —
so thresholds fall as the pattern number rises. The captured GTC block has
pattern 0 = 90 % and pattern 9 = 76 %.

The telemetry report's `rpmLeds` field ([03](03-telemetry.md)) selects which
pattern the wheel displays; the thresholds here are what the wheel compares the
telemetry's RPM percentage against when it selects one itself.

### LED slots (offsets 50–79)

One byte per lit part of the wheel, buttons first: `block[50 + slot]`.

```
bits 7–4   colour   1…8 a colour, 9 = off
bits 3–0   trigger  what makes it light
```

**Colours** (the wheel's own numbering):

| Value | Colour |
| --- | --- |
| 1 | Red |
| 2 | Orange |
| 3 | Yellow |
| 4 | Green |
| 5 | Blue |
| 6 | Cyan |
| 7 | Purple |
| 8 | White |
| 9 | Black (LED off) |

**Triggers** (low nibble):

| Value | Meaning | Status |
| --- | --- | --- |
| 0 | Static effect — lit all the time | Confirmed |
| 1 | While ABS is active | Confirmed |
| 2 | While traction control is active | Confirmed |
| 3 | While the pit limiter is on | Guess from ordering, not seen working |
| 4 | While DRS is on | Guess, not seen working |
| 5 | While a flag is out | Guess, not seen working |

On a GTC, slots 0–9 are used (button LEDs plus the ABS window in slot 8 and the TC
window in slot 9). The captured block has slot 8 = orange/ABS, slot 9 = purple/TC,
slot 1 = cyan/always. Slot numbers are per-wheel; the app takes them from each
control's `data-led` attribute in the wheel's SVG.

### RPM LED patterns (offsets 80–229)

Ten patterns for up to fifteen LEDs, **LED-first**: the byte for LED `l` in
pattern `p` is

```
block[80 + l × 10 + p]      l = 0…14,  p = 0…9
```

```
bits 7–4   colour   1…8 a colour, 9 = off (leave the LED dark)
bit  3     flash    1 = flashing
bits 2–0   trigger  meaning in a pattern not known (3 bits)
```

The captured GTC block uses trigger 5 for every LED, not flashing, with colours
running from green (LED 0) to red (LED 8) across nine LEDs. Pattern 0 has all nine
lit; each higher-numbered pattern lights fewer, matching the falling thresholds.

The host keeps each LED's trigger bits as the block already had them.

### Brightness (offset 230)

A plain percentage, 0–100, dimming every LED on the wheel. Confirmed on hardware.
Values above 100 have never been captured.

### Clutch paddles (offsets 231–232)

Found by writing chunk 3 with different values to a GTC with its dual-clutch
module and pulling the paddles while watching the joystick report
([06](06-input-reports.md#clutch-paddles)).

**Offset 231, the mode.** Written whole.

| Value | Paddles | What the joystick report shows |
| --- | --- | --- |
| 1 | Independent (the captured block) | Left paddle on Slider, right on Rz, each over its full range |
| 0 | Synced | Both on Slider, Rz stays 0. Either paddle alone reaches the ratio; both together reach the full range |

Other values were not tried.

**Offset 232, the ratio**: a plain percentage, 0–100. With synced paddles,
one paddle alone stops at that share of the range: at 50 and at 25, a single
paddle pulled fully read 2047 and 1023 of 4095, and both together 4095. That is
a launch clutch: hold both, let one go, and the clutch sits at the bite point.
It has no effect on independent paddles. The captured block has 34.

The wheel says the ratio back in the joystick report (byte 38, as received) as
soon as chunk 3 arrives, which is a way to see that a write reached it. The mode
is not said back anywhere that has been found.

Both go to every wheel, as the brightness does: they are in chunk 3, which is
always sent.

## Sending rules

These come from observed behaviour on a GTC and are what makes the protocol
reliable in practice:

1. **The apply chunk is chunk 3.** Sub-command 3 alone sets the brightness, but
   sub-command 0 alone changes nothing on a GTC: the colours it carries only show
   once a 3 follows. So chunks 0–2 are "taken in" without effect until chunk 3
   arrives, and **any change must end with chunk 3.**
2. **Send changed chunks in ascending order, then chunk 3, once.** A change to
   chunk 1 is sent as `1, 3`; a change to chunks 0 and 2 as `0, 2, 3`. Chunk 3 is
   sent once however many of the others went before it.
3. **Minimum 1 s between updates.** A GTC drops an update that follows the
   previous one too soon; switching a button between two colours with 150 ms
   spacing showed only the first. After an update, wait 1 s; whatever changed
   meanwhile goes out as one update carrying the latest values. (The timer must be
   a precise one: a coarse timer fired up to 5 % early, at 475 ms, in a 500 ms
   test.)
4. **Only for a GTC.** The block is a GTC's, and its mappings, thresholds and LED
   slots mean something else on other wheels. The app only sends chunks 0–2 when
   the attached wheel id (from [`0x23` byte 10](02-status-and-settings.md#get-0x23--live-base-status))
   is `4`. Chunk 3 — the brightness and the clutch paddles — is sent regardless.
5. **At start-up** send the saved brightness at once (chunk 3). When the wheel id is
   first learned to be a GTC — and whenever a GTC reappears after being another
   wheel or none — send the whole block: chunks 0, 1, 2, then 3.

## Worked example

Setting slot 3 to orange, lit all the time, changes one byte in chunk 0. Block
offset `50 + 3 = 53` becomes `(2 << 4) | 0 = 0x20`. In the captured chunk-0
report (header + 60 bytes), that is report byte `2 + 53 = 55`:

```
before:  … 33 60 10 90 90 20 33 90 21 72     byte 55 = 0x90 (colour 9 = off, trigger 0)
after:   … 33 60 10 20 90 20 33 90 21 72     byte 55 = 0x20 (colour 2 = orange, trigger 0)
```
Followed, to make it take effect, by chunk 3.

Brightness 3 % is block offset 230 = chunk 3 payload byte 50 = report byte 52:

```
91 03 … 03 03 01 22 50 00 00 00 00 00 00
                └ byte 52 = 0x03
```
