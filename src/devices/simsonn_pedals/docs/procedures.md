# Procedures

Byte-level recipes the host performs. `→` host to device, `←` device to host. Every write on the
vendor interface is a 64-byte report unless stated.

## 1. Firmware probe

```
→ 03 01                       (2 bytes; repeat every 250 ms until answered, give up at 5 s)
← 03 01 <payload…>            first packet of the config read
   firmware   = uint16 at report byte 2   (blob +0x00)
   sub-version= uint16 at report byte 4   (blob +0x02)
```

Requires a reply of at least 6 bytes. Render both in hex: `0x2070 / 0x0204` → `2070.204`. A failure
here is not fatal to the device; it only means the configuration protocol did not answer.

## 2. Read the configuration

18 round trips, see [config-blob.md](config-blob.md#reading--report-0x03-host-paced):

```
for n in 1..18:
    → 03 n
    ← 03 n <62 bytes>         (packet 18: 30 bytes used)
```

Done at connection time; ~0.32 s. Both settings pages are populated from this blob.

## 3. Read brake options

```
→ 06 "read_damping_type"     ← 06 'O' 'K' <s|m|h>
→ 06 "readBrakeCutoff"       ← 06 'B' 'C' 'O' 'K' <en> <lo> <hi>
```

Strictly sequential; resend a question every 250 ms until answered; 3 s overall.

## 4. Live positions

Open the axis interface and read report `0x01` ([input-report.md](input-report.md)). No request.

## 5. Live raw counts (calibration)

```
→ 02 00 … (64 bytes)          every 1 s
← 02 00 <62 bytes>
← 02 01 <8 bytes>             assemble into the 70-byte params area
```

Raw axis *i* = int16 at area offset `4 + 2i`. Keep asking or the stream stops 30 s after the last
request.

## 6. Calibrate

Host-side. Measure min/max raw over the run for each pedal from the params area, or take endpoints
the user set by hand. Then for each pedal:

1. Choose endpoints: measured travel if it moved by ≥ **655 counts** (1 % of the 65534 range),
   otherwise the full range `−32767 / 32767` (what an unused axis ships with — a disconnected pedal
   reports noise); hand-set endpoints override; untouched pedals keep the stored values.
2. Skip the pedal if the endpoints equal the stored ones (avoids rewriting the effective trio from
   the host's derivation).
3. Take the pedal's stored curve and deadzones from the blob; set physical min/max; derive the
   effective trio ([commands.md](commands.md#effective-range-derivation)); send `<letter>_pf`.
4. Read the blob again (procedure 2) and compare physical min/max with what was written. The
   calibration is only "saved" if they agree.

## 7. Save settings (curves, deadzones, filter, brake options)

In this order; each step only when something changed:

1. **Whole blob (`0x04`)** — only if a `Filter` level changed (it lives only in the blob). Start from
   the blob read earlier, patch every edited axis (curve, calibration, deadzones, filter), write it.
2. **`<letter>_pf`** per pedal whose curve, deadzones or calibration differ from stored. Recompute the
   effective trio only if a deadzone or the physical range moved. On the brake, changing the force or
   the start deadzone recomputes `physMax` from the kg figure.
3. **Brake named commands:** `damping_max` when the physical/effective max changed, `damping_type`
   when the elastomer changed, `brakeCutoff` when the throttle cut changed.
4. **Verify:** re-read the blob (and brake options if any were written) and compare with what was
   sent. Mismatch means the device kept different values.

If a write fails part-way, re-read anyway; earlier commands may already have landed.

## Not implemented / unknown

* Report `0x05` (flasher protocol).
* `write_offset` (tare) and `read_raw` commands.
* Most of the 70-byte params area, including the second live triple at `+0x450`.
* Blob regions other than the version words and the per-axis records, notably `0x366`–`0x3D7`, and the
  axis bytes `+0x16..+0x21`, `+0x24..+0x33`, and flag bits 4..0.
* Whether `damping_max` has effects beyond `b_pf`.
* Whether any of the vendor replies carry data beyond what the code reads (only the leading bytes
  are inspected).
