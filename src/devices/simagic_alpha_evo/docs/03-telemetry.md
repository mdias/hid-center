# 3. Telemetry report

The host tells the base about the car — engine speed, ABS/TC/limiter state, the
current flag — so the wheel can drive its shift lights and status LEDs. The base
is *fed a stream*, not told about changes.

## Report

`SET_FEATURE(report 0x91)`, **62 bytes**, byte 1 = `0x04`. It shares report id
`0x91` with the [wheel configuration](05-wheel-config.md); byte 1 tells them apart
(`0`–`3` configuration, `4` telemetry).

Fields not listed as filled by the host go out with the defaults shown.

| Offset | Size | Field | Sent value | Notes |
| --- | --- | --- | --- | --- |
| 0 | 1 | report id | `0x91` | |
| 1 | 1 | sub-command | `0x04` | |
| 2 | 1 | unknown | `0x00` | |
| 3 | 2 | `rpm` | live | u16 LE, engine RPM clamped to 0–65535 |
| 5 | 1 (bit 0) | `absActive` | live | 1 = ABS active |
| 5 | (bit 1) | `tcActive` | live | 1 = traction control active |
| 5 | (bit 2) | `speedLimiterActive` | live | 1 = pit limiter on |
| 5 | (bit 3) | unknown | 0 | |
| 5 | (bits 4–7) | `rpmLeds` | live | Shift-light preset **10 = no LEDs … 0 = all LEDs** (see below) |
| 6 | 1 | unknown | `0x10` | |
| 7 | 1 | `unfilteredBrake` | 0 | Not filled |
| 8 | 1 | `unfilteredThrottle` | 0 | Not filled |
| 9 | 1 | `unfilteredClutch` | 100 | Not filled |
| 10–13 | 4 | unknown | 0 | |
| 14 | 1 | `rpmPercent` | live | 0–100 |
| 15 | 1 | `currentLap` | 0 | Not filled |
| 16 | 1 | `flag` | live | See flag table |
| 17 | 2 | `elapsedTimeMinutes` | 0 | Not filled |
| 19 | 1 | `elapsedTimeSeconds` | 0 | Not filled |
| 20 | 3 | `elapsedTimeMs` | 0 | 24-bit, not filled |
| 23 | 1 | unknown | 0 | |
| 24–31 | 8 | unknown | 0 | |
| 32–35 | 4 | unknown | `0xEF` | "Maybe wheel temp?" |
| 36 | 1 | unknown | `0x01` | |
| 37 | 1 | `tirePressureFL` | `0x01` | PSI × 10, not filled |
| 38 | 1 | `tirePressureFR` | `0x01` | PSI × 10, not filled |
| 39 | 1 | `tirePressureRL` | `0x01` | PSI × 10, not filled |
| 40 | 1 | `tirePressureRR` | `0x01` | PSI × 10, not filled |
| 41–42 | 2 | unknown | 0 | |
| 43 | 1 | `engineWaterTempCelcius` | 0 | Not filled |
| 44 | 1 | `engineOilTempCelcius` | 0 | Not filled |
| 45–46 | 2 | unknown | 0 | |
| 47–49 | 3 | `brakeTempFL` (12 bits) + `brakeTempFR` (12 bits) | 0 | Packed LSB-first; not filled |
| 50 | 1 | unknown | 0 | "Seems related to brake RL, but only the most significant 4 bits??" |
| 51–52 | 2 | `brakeTempRR` (12 bits) + unknown (4 bits) | 0 | Not filled |
| 53–55 | 3 | unknown | 0 | |
| 56–61 | 6 | unknown | 0 | |

Total: 62 bytes. Offsets 5, 47–52 are bit-fields; the packing is LSB-first within
each byte and runs contiguously across bytes.

The report carries many fields (pedals, lap/time, tyre pressure, temperatures)
that this host never fills. They are named in the source from observation, but
whether the wheel acts on them is not established by this code.

### Flag byte (offset 16)

| Value | Flag |
| --- | --- |
| 0 | none |
| 1 | green |
| 2 | blue |
| 3 | yellow |
| 4 | black |
| 5 | white |

Checkered, orange and red have no slot and go out as `0`.

### RPM percent (offset 14)

`rpm × 100 / rpmMax`, clamped to 0–100, where `rpmMax` is the car's redline (a
built-in default is used when the game does not report one).

### Shift-light preset (`rpmLeds`, offset 5 bits 4–7)

The wheel's RPM LEDs follow **ten pre-programmed patterns** (see the RPM patterns
in [05](05-wheel-config.md#rpm-lights)). The host chooses which pattern is shown by
sending its index here: **10 selects "all off", 0 selects "all on"**. The source's
mapping from RPM percent assumes the wheel is still on the factory patterns:

```
preset = (rpmPercent − 80) × 9 / 15          (integer division, truncating)
rpmLeds = clamp(10 − preset, 0, 10)
```

So below 80 % the LEDs are dark (10), and from about 97 % they are all lit (0).
The default when the host has nothing to say is `10`.

## Rate and start/stop behaviour

* Sent at a fixed rate, **default 60 Hz**, adjustable from 20 to 90 Hz.
* Sent **every tick while a live source exists** (a game is feeding telemetry, or a
  test mode is on), whether or not the values changed — a gap in the stream is
  what the base notices, not an unchanged value.
* When the source goes away (falling edge), one **all-default report** is sent —
  RPM 0, percent 0, flags clear, `rpmLeds = 10`, flag 0 — and then the stream
  stops. A stale last report would leave the shift lights lit at the last revs.
* When telemetry is switched off by the user, the same all-default report is sent
  once (if a stream was live).
* With no live source and no falling edge, nothing is sent: "a base should not be
  told sixty times a second about a car that does not exist".

## Example

Engine at 7 200 rpm, 90 % of redline, ABS active, no flag:

```
91 04 00 20 1C 41 10 00 00 64 00 00 00 00 5A 00 00 …
```
bytes 3–4 = `0x1C20` (7200), byte 5 = `rpmLeds<<4 | flags`. With
`preset = (90−80)×9/15 = 6`, `rpmLeds = 4`, so byte 5 = `0x41` (ABS bit set);
byte 6 = `0x10`; byte 14 = `0x5A` (90); the rest are the defaults above.
