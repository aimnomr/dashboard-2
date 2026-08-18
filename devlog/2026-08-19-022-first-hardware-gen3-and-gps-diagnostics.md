# 022 · First GEN3 hardware packets, and GPS diagnostics

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-08, ISS-13

## What

Aiman flashed the GEN3 firmware and supplied five real packets. Analysed them, then added
GPS fault separation to the flight unit and a standalone passthrough diagnostic.

- `firmware/MRC_FlightUnit_GEN3/Sensors.ino` — `gpsHasData()` and counters
- `firmware/MRC_FlightUnit_GEN3/Display.ino` — "GPS NO DATA" distinguished from "no fix"
- `firmware/tools/GPS_Passthrough/GPS_Passthrough.ino` — new, sweeps baud rates
- SD log format confirmed unchanged, pandas recipe documented and verified
- `pandas` added to `requirements-dev.txt`

## Result

**The firmware works.** It compiled, it runs, and three things are confirmed on real
hardware:

- **CRC agreement, 5/5.** Every checksum computed by the C on the vehicle matches the
  Python reference exactly. Two of the three implementations that must agree are now
  confirmed against real output rather than against each other's assumptions.
- **Cadence is exact.** `ms` deltas of 1000, 1000, 1000, 1000 with `seq` stepping by one
  each time. The deadline scheduler holds 1 Hz with no overruns and no local loss.
- **Gyro bias is fixed.** 0.10–0.58 °/s at rest, against the 4.1 °/s measured on GEN1
  earlier. The recalibration worked.

**Accelerometer magnitude reads 0.918 g at rest, not 1.000.** Consistent across all five
packets, so it is bias rather than movement. The −82 mg sits right at the MPU6050
datasheet's ±80 mg zero-g offset tolerance for the Z axis, so this is part variation, not
a fault. Worth recording because it explains a real design choice: GEN1 deliberately does
not offset-correct `az`, because with the unit nominally flat there is no way to separate
gravity from bias on the vertical axis. Correcting it would require normalising the whole
vector to 1 g at rest — possible later, not needed now.

**GPS has no fix, and the firmware could not say why.** `sat=0` with `lat/lng=0.00000`
means no fix, but "the module is not reaching the ESP32" and "the module cannot see sky"
presented identically. Those are completely different faults.

`gpsHasData()` now separates them, and the OLED shows **GPS NO DATA** rather than
**GPS no fix(0)** when nothing is arriving at all. TinyGPSPlus's `charsProcessed()`,
`sentencesWithFix()` and `failedChecksum()` are exposed alongside.

One inference from the packets themselves: `ms=668683` is 11 minutes of uptime. If that
was spent outdoors under clear sky, a cold start no longer explains the absence of a fix
and suspicion moves to the antenna or the wiring.

`GPS_Passthrough` sweeps five baud rates and dumps raw module output, which separates the
faults without waiting for a reflash of the full firmware.

**SD format confirmed unchanged** — auto-incrementing `/FLIGHTnn.CSV`, open/append/close
per write, exactly GEN1's scheme. Contents are framed GEN3 packets rather than bare CSV.
Aiman chose to keep the `.CSV` extension for spreadsheet and pandas compatibility,
accepting the glued chute/checksum column as a cleaning step. A verified pandas recipe is
now in `gen3-packet-format.md`.
