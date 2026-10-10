# SIMSONN

Configuration module for SIMSONN sim-racing pedal sets.

| | |
| --- | --- |
| VID:PID | `ddfd:6011` (SIMSONN Plus X) |
| Namespace | `hidcenter::simsonn` |
| Built as | `libhidcenter-simsonn.so` |

**Partly implemented.** The module reads from the device
and writes one thing to it — a pedal calibration, from the Calibration page. Everything else is
read-only: Main Settings shows the curves and deadzones the pedals are actually holding, but
editing them there changes nothing on the device.

What touches hardware:

| | What it does | When |
| --- | --- | --- |
| [FirmwareProbe](FirmwareProbe.h) | Packet 1 of the report-`0x03` config read, for the firmware version | Once, on `initialize()` |
| [PedalInput](PedalInput.h) | Reads input report `0x01` — the three axes, for the graphs | Only while the Main Settings tab is visible |
| [ConfigRead](ConfigRead.h) | All eighteen packets of the report-`0x03` config read — the stored curves, deadzones and calibration | Once, on `initialize()` |
| [ParamsPoll](ParamsPoll.h) | Asks for the report-`0x02` params once a second and decodes the replies — the live raw sensor counts | Only while the Calibration tab is visible |
| [CalibrationSession](CalibrationSession.h) | Watches a calibration run; on save, writes one `<letter>_pf` per pedal and reads the blob back to check it | Only when the user presses the buttons |

Asking for the params puts the pedals into a ~500 report/s stream, so it is done only for the
page that reads them. It also has to keep being asked: the device stops 30 seconds after the last
request, which is why `ParamsPoll` holds a timer rather than writing once. Closing the interface
does not cut the stream off either — it runs out the remaining 30 s whatever the host does. The
config read is unbothered by all that traffic on the same node: measured against a Plus X, all
eighteen packets came back in 0.32 s with the params stream running.

The **Main Settings** page shows what each pedal does with its travel, as the device holds it.
Both controls are filled from the configuration blob, which is read once in `initialize()` —
at connection rather than on the first visit to a tab, because both settings pages want it and
nothing but the pedals themselves can change it. [CurveEditor](ui/CurveEditor.h) has eleven control
points because the device stores eleven (points 0 to 10, signed bytes in -100..100 at evenly
spaced inputs), so what is on screen is point-for-point what is on the pedals.

The deadzones are one [RangeSlider](ui/RangeSlider.h) rather than the two spin boxes that were there
before. They are two ends of one range — the pedal reads 0 % below the low thumb and 100 % above
the high one — and two independent numbers could be set past each other into a pedal with no
travel at all. The device stores them as two *sizes*, each measured inward from its own end, so
the low thumb is start deadzone size as it stands and the high thumb is `100 -
end deadzone size`.

A blob that arrives later is deliberately **not** taken: the Calibration page asks for a re-read
after it writes, and stamping that over the curve somebody is in the middle of editing would
throw away work to no purpose, since a calibration does not touch the curves or the deadzones.

The **Calibration** page reads a calibration and sets one. Each pedal gets a
[PedalBar](ui/PedalBar.h): the live raw count, two thumbs at the endpoints the device has stored
(physical minimum / physical maximum), and the calibrated span shaded between them. Beside the raw
count the throttle and clutch give the output percentage — measured across the *effective* range,
effective minimum..effective maximum, so it is the same number the Main Settings graph shows. The brake gives
kilograms instead, at 360 counts/kg, because a load cell's reading is a force and how hard it is
pressed matters more than how far it has moved.

**Every kilogram figure is taken straight off a raw count, deadzones and all.** A deadzone moves
where the output reaches 0 % and 100 %; it does not change how hard the pedal is being pressed
there, and a force that shrank because somebody widened a deadzone would be a lie about the
hardware. So the live reading counts from physical minimum, and "full brake" is the force at
physical maximum — which is also the endpoint a save writes, so the number does not move when it is
saved. The consequence worth knowing: this is *not* the figure the "Maximum force"
box holds. That box is set against the effective top, dividing by `1 - ldz/100` on the way in,
which the end deadzone coming back out very nearly undoes — so on a unit with 5 % at each end its
18 kg is 18.9 kg of actual load.

**Two ways to change one, sharing one pair of buttons.** *Start Calibration* watches the raw
values and remembers how far each pedal travels, moving the thumbs to the measurement as it goes.
Dragging a thumb — which works at any time, run or no run — sets one endpoint of one pedal by
hand and starts a run without watching anything, so the pedals nobody touched go on showing, and
keep, what the device already holds. Either way *Cancel* throws it away and touches nothing, and
the line under each bar says which of the four situations that pedal is in: measured, barely
moved, set by hand, or untouched.

A dragged thumb also outranks the measurement for its own pedal, for the rest of the run. Someone
who has taken hold of an endpoint is saying where they want it, and a value that slid out from
under them the next time the pedal moved would make the gesture useless. The two thumbs cannot
cross, and cannot come within `kMovementThreshold` of each other: a span that narrow is what this
page already calls an axis with nothing attached to it.

**Saving.** *Save Calibration* writes one `<letter>_pf`
report per pedal — a command that carries a pedal's whole state, so the curve points and both
deadzones are copied back out of the blob unchanged and only the five calibration values move.
The effective trio (effective minimum/effective centre/effective maximum) is derived here, in a fixed
order of operations, because the device does not derive it: it trusts whatever range it is handed.

A pedal already holding what the save would write is **not written at all**. Partly because a
write that changes nothing is a write worth not making, and partly because `<x>_pf` carries the
whole pedal: rewriting one would replace the device's own effective trio with this host's
derivation of it, and a factory axis can be holding a range the deadzones were never taken out of
— the clutch on the unit this was written against holds `-32767 / 0 / 32767` where the derivation
gives `-29490 / 0 / 29490`. Leaving it alone is the difference between changing one endpoint and
quietly normalising three pedals.

**Two guards on the write**, both of which exist because the button that changes the hardware
lives on the same row as the one that starts a run. Both apply to a run begun by dragging a thumb
exactly as they do to one begun by pressing *Start*:

* All three buttons stay in fixed positions and are only enabled and disabled. Hiding the
  inapplicable ones put *Save* exactly where *Start* had been, under a cursor that had not moved —
  and a single click on *Start* came back as a completed save.
* *Save* stays disabled for a second after a run begins (`kSaveDelayMs`), so no double click,
  bounced mouse or held key can reach it. Nobody notices, because the first thing a calibration
  asks you to do is reach for the pedals.

Two things that shape the implementation:

* **A `<letter>_pf` is not acknowledged.** The device answers nothing at all, so the save reads
  the whole blob back afterwards and compares it against what went out. Until that agrees, the
  run is not reported as saved.
* **A pedal that never moved is not calibrated to a point.** A disconnected pedal still reports,
  and reports noise — a disconnected clutch measured here drifted sixteen counts in twenty-five
  seconds. Anything that travels less than `kMovementThreshold` (one percent of the sensor range,
  655 counts) is written with the full range instead, which is what an unused axis ships with.

Still not written: deadzones, response curves, the tare (`write_offset`) and the whole-blob write
(report `0x04`).

## Decoding the rest of the protocol

Still unknown, per the notes: most of the 70-byte params area — including the second live triple
at `+0x450`, which moves with the pedals but has no known meaning — the `0x366`–`0x3D7` span of
the config blob, and the flasher protocol behind report `0x05`.

Use the **Protocol Analyzer** tab. It lists every report the pedals declare and logs traffic;
tick *Listen* to start capturing. It sees everything hid-center exchanges plus every input report
the pedals send, but not output or feature reports written by another process — so it will not
show what another application sends. See the analyzer notes in the top-level
[README](../../../README.md).

The SIMAGIC Alpha Evo module is the worked example of what to build once the protocol is
known: [../simagic_alpha_evo/README.md](../simagic_alpha_evo/README.md).
