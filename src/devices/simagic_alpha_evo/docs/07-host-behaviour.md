# 7. Host behaviour: sequencing and timing

The wire formats are in the preceding documents. This one collects what a host has
to *do*, and when, from the way the reference implementation does it.

## Start-up

1. **Find the configuration interface** ([01](01-transport.md#finding-the-configuration-interface)):
   for each candidate HID interface, `GET 0x81` (64 bytes). The first that returns
   64 bytes with byte 0 = `0x81` wins. `GET 0x23` is read too, if available, but
   does not affect whether an interface is accepted.
2. **Open the LED interface** by its endpoint (`0x02` OUT). Failure here is not
   fatal: the base configures without it and only the ring is lost.
3. **Start polling** (below).
4. **Restore wheel state:** send the saved wheel LED brightness and clutch
   paddle settings (report `0x91`, sub-command 3). If the attached wheel (from `0x23` byte 10) is a GTC, send the
   full wheel-configuration block (`0`, `1`, `2`, then `3`).
5. **Start the telemetry and ring clocks.** The ring is driven from the same clock
   as telemetry so the shift lights and the ring describe the same instant.

If no interface answers `0x81`, the base is unusable for configuration; the
reference implementation keeps the device visible so the failure can be
inspected.

## Polling

* **Every 500 ms**, `GET 0x81` and `GET 0x23`.
* A failed `0x81` is a read failure. A failed `0x23` is **not**: the settings
  still arrived and everything depending on them works. Keep asking for `0x23` on
  every poll anyway — a transient failure then heals itself.
* Report changes only when the bytes differ (compare `0x81` and `0x23` separately;
  the live status changes far faster than the settings).
* Polling picks up changes made by anything else — the base's own controls, the
  kernel driver's sysfs files, another program.

## Writing settings

* Re-read `0x81` immediately before composing (do not poll `0x23` for this).
* Send only the blocks whose fields changed, in the order 1, 2, 3, 4.
* Re-read `0x81` afterwards and treat that as the truth: the base may clamp values
  itself.
* Serialise with polling: a poll must not land between the read and the writes.
* Do not write a second batch while one is in flight.

## Telemetry

* 60 Hz default (20–90 Hz), one `0x91`/4 report per tick while a game (or test
  mode) is live. See [03](03-telemetry.md#rate-and-startstop-behaviour).
* On loss of source or when the user disables it: send one all-default report,
  then stop.

## Ring

* Send whenever the colours change, at most every 40 ms, one packet outstanding at
  a time.
* Re-send the current colours **every 3 s** while nothing else is being sent; the
  base forgets them after ~10 s. See [04](04-ring-led.md#keep-alive).
* Telemetry-driven modes take priority over a user preview: a preview shows only
  while the car is asking for nothing.

## Wheel configuration

* Minimum **1 s** between configuration updates; merge changes made in the
  meantime into one update with the latest values.
* Always finish an update with chunk 3. See
  [05](05-wheel-config.md#sending-rules).

## Error handling

The reference implementation deliberately does not retry behind the user's back.

* A failed feature-report SET is reported once per run of failures (not once per
  attempt), and again when writes recover.
* A failed ring write stops the remaining frames of that packet; the next colour
  change or keep-alive sends the whole packet again.
* Read failures are likewise reported once per run and clear on the next success.

## Other notes

* The device is unplugged when either handle reports a disconnect; all transfers
  stop and live readings are discarded (a frozen temperature looks current).
* A "dry run" mode composes every report and logs it without sending, which is a
  convenient way to inspect the exact bytes of any write.
* An **offline** mode exists for developing without hardware: the host seeds a
  zeroed status (clamped to each field's minimum) and applies edits to that
  in-memory copy, composing and logging the real reports as it goes.
