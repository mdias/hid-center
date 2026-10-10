# SIMAGIC Alpha Evo plugin

A HID Center device plugin for SIMAGIC Alpha Evo direct-drive wheelbases
(USB `3670:0500`, `0501` and `0502`). It lets you configure the base and the
steering wheel attached to it from Linux.

## What it does

The plugin adds five tabs to the device page.

- **Status**: shows what the base is doing: firmware version, state, motor and
  MOSFET temperatures, bus voltage, and which steering wheel is attached. It also
  has a live trace of the force the base is applying.
- **FFB Settings**: reads and writes the base's force-feedback settings:
  - force-feedback strength
  - maximum rotation and angle lock (with lock strength)
  - mechanical centering, damper, friction and inertia
  - game centering, inertia, damper and friction
  - wheel rotation speed and feedback detail
  - signal filter level and slew-rate control
  - the base's ring light on/off and brightness
- **Telemetry**: feeds car data from a running game or sim to the wheel: RPM and
  RPM percentage, ABS, traction control, pit limiter and the current flag. The
  wheel uses it to drive its shift lights and status LEDs.
- **Wheelbase LED**: sets the colours of the LED ring on the base. There are
  separate modes for idle, ABS, traction control and other events, and they
  switch automatically from telemetry.
- **Wheel**: configures the attached steering wheel, currently the GTC:
  - which joystick button each input presses
  - the colour and trigger of each lit button
  - the RPM shift-light patterns and thresholds
  - LED brightness
  - whether the clutch paddles are synced, and their ratio

  It also shows pressed buttons and the clutch paddles live.

Settings are saved and re-applied to the wheel at start-up. Each page has a dry-run
option that logs the exact bytes instead of sending them. An offline preview lets
you explore the pages with no hardware connected.

## Requirements

- The base's HID interface is used for settings and telemetry.
- The LED ring is on a separate USB interface that has no hidraw node. It needs
  permission to open `/dev/bus/usb`, which the project's udev rules provide. Without
  it, everything works except the ring.

## Limitations

- Only the GTC wheel's own configuration is written.
- The wheel's configuration and the LED ring cannot be read back from the hardware.
  The plugin remembers what it sent and re-sends the ring colours every few seconds.
- Parts of the protocol are inferred from captures. The
  [protocol documentation](docs/README.md) says which.

## Documentation

The wire protocol is documented in [docs/](docs/README.md).

## Source layout

| Files | Role |
| --- | --- |
| `SimagicAlphaEvo*` | Plugin entry point and device page |
| `AlphaEvoProtocol.h`, `AlphaEvoLedProtocol.h`, `AlphaEvoWheelConfig.h`, `AlphaEvoProto3.h` | Wire formats |
| `AlphaEvoBlocks.*` | Composes and parses reports |
| `AlphaEvoIo.*`, `AlphaEvoSettings.*` | Transfers, polling and the worker thread |
| `AlphaEvoTelemetry.*`, `AlphaEvoAnimator.*` | Telemetry stream and LED animation clock |
| `WheelConfigController.*`, `WheelLedController.*` | Wheel and ring state, persistence |
| `ui/` | The tab pages and widgets |
