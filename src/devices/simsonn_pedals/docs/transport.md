# Transport

## Device and interfaces

| | |
| --- | --- |
| USB VID:PID | `ddfd:6011` — "SIMSONN Plus X" |
| Access | HID through the Linux hidraw nodes of the interfaces below |

The device exposes (at least) two HID interfaces, told apart by usage page:

| Interface | Usage page | Carries |
| --- | --- | --- |
| Generic desktop | `0x01` | Input report `0x01` — the pedal axes ([input-report.md](input-report.md)) |
| Vendor-defined | `0xFF00`–`0xFFFF` | Everything else: reports `0x02`, `0x03`, `0x04`, `0x06` (this is the "configuration interface") |

The host picks the configuration interface as the first HID interface whose usage page is
`>= 0xFF00`. The axis interface is found by shape instead: the first HID interface whose input
report `0x01` has a non-constant field of 16 bits with a count of at least 3.

The descriptor declares three axes and names none (all use usage `0001:0030`), so which axis is
which pedal is a hardware finding, not something in the descriptor
(see [input-report.md](input-report.md)).

## Report IDs

| ID | Direction | Interface | Purpose | Document |
| --- | --- | --- | --- | --- |
| `0x01` | IN | axis | Pedal positions, unprompted, ~500 Hz | [input-report.md](input-report.md) |
| `0x02` | OUT request / IN replies | vendor | Params request; live raw sensor area | [params.md](params.md) |
| `0x03` | OUT request / IN replies | vendor | Read configuration blob, packet by packet | [config-blob.md](config-blob.md) |
| `0x04` | OUT packets / IN requests | vendor | Write configuration blob | [config-blob.md](config-blob.md) |
| `0x05` | — | vendor | Flasher protocol — **unknown**, never used by this code | — |
| `0x06` | OUT commands / IN replies | vendor | Named ASCII commands | [commands.md](commands.md) |

## Framing rules

* Every host write on the vendor interface is a **full 64-byte report** (ID + 63 bytes), zero padded.
  A bare 1-byte `0x02` was refused by Linux hidraw. The config *read* request is the exception: it is
  written as exactly **2 bytes** (`03 <n>`), the size the firmware declares for that report's OUT.
  Sending more is not what the firmware expects.
* Replies arrive as IN reports whose first byte is the same report ID as the request. Several
  exchanges share the one interface at once, so a reader must filter on the ID (and, where present,
  the packet number) and ignore everything else.
* The device sends nothing on the vendor interface until asked (see the params stream below).
* Nothing has a length field or checksum. Payload widths are implied by the report ID / command name.

## Timing and reliability

Constants the code uses; the device is known to tolerate them.

| Exchange | Resend interval | Overall deadline |
| --- | --- | --- |
| Firmware probe (packet 1) | 250 ms | 5 s |
| Config read (18 packets) | 250 ms per unanswered packet | 8 s total |
| Config write | 250 ms per unanswered packet | 5 s total, then 500 ms settle |
| Brake options read | 250 ms | 3 s total (both questions) |
| Params request | new request every 1 s | — (see below) |

Config-read exchanges are **host-paced**: ask for a packet, wait for that packet number, ask for the
next; re-ask the same one if it is lost. The config write is **device-paced** (the device asks for
each packet). Confirmed: all 18 read packets complete in ~0.32 s even while the params stream is
flooding the same node at 500 reports/s.

## Acknowledgements

Most writes are **not acknowledged**. `<letter>_pf` and the other named commands produce no reply
on success; the config-blob write ends silently unless the device refuses it (`04 FE`). The only
way to know a write landed is to read the configuration back (report `0x03`, or the named
`read_…` / `readBrakeCutoff` commands) and compare.

## Streams that keep running

* **Axis report `0x01`** streams whenever the interface is being read; it needs no request.
* **Params `0x02`** streams after a request for **30 s** (confirmed to a tenth of a second) at ~250
  pairs/s (500 reports/s). Each new request restarts the 30 s. Closing the interface does *not*
  cut the stream; it lapses 30 s after the last request.
