# 066 · The GPS reaches a fix, and two vehicles were on one channel

**Date** 2026-09-09
**Type** investigation
**Refs** 042, 048, 062, 065, ISS-13, ISS-14

Written after the fact. The code it refers to was committed in `ec6f068`; the logs it reads
are the twenty ground station sessions recorded on 2026-09-09, `logs/raw/20260909-*`,
00:42 through 13:16. Nothing here was changed as a result — the entry is the reading.

The headline reverses a conclusion committed the same day: **the GPS reaches a valid fix,
with real coordinates and good geometry, and has done so in six separate sessions.**

## What

### 1 · Six sessions carry sustained valid fixes

`fixq=1`, `sat` into double figures, HDOP below 1 at best, and coordinates that are a place
rather than a sentinel:

| session | fixed packets | HDOP | sat max | last position |
|---|---|---|---|---|
| `20260909-004618` | 146 | 0.8 – 1.9 | 14 | `4.12348, 100.90487` |
| `20260909-011251` | 432 | 0.7 – 1.8 | 19 | `4.12351, 100.90491` |
| `20260909-043815` | 168 | 1.1 – 2.0 | 8 | `4.12340, 100.90473` |
| `20260909-044854` | 369 | 1.1 – 6.4 | 7 | `4.12351, 100.90489` |
| `20260909-051040` | 420 | 0.9 – 5.0 | 10 | `4.12352, 100.90488` |
| `20260909-125227` | 390 | 0.9 – 4.0 | 10 | `4.14135, 100.90395` |

**1,925 packets carrying a fix**, across sessions six hours apart. A representative one:

```
$MRC,117,123552,30.47,49.3,1012.93,0.3,…,4.12349,100.90487,0.0,14,0,0,0.8,1*C72C,-69.0,13.2
                                          ^lat      ^lng        ^sat    ^hdop ^fixq
```

Four of the six are single-stream sessions with no interleaving of any kind — see 3 below
for why that qualifier is load-bearing. The last session moved about 1.9 km from the
others and fixed again there, so this is not one lucky position.

**The GPS pins from devlog 062 are confirmed on hardware: `GPS_RX 20` / `GPS_TX 19`.** 062
shipped those numbers unverified and said so. They are correct.

### 2 · The ISS-14 update committed today is wrong on its central claim

`ec6f068` rewrote ISS-14 from *"GPS delivers zero bytes; module appears unpowered"* to
*"GPS never reaches a fix"*, on this evidence:

> `fixq` is **0 in every packet of all four sessions on 2026-09-09** — roughly 1,300
> packets, not one valid fix […] **The leading cause is now sky view or antenna.**

That is not supported by the full set of the day's logs. Two of the six fixing sessions —
`004618` and `011251` — were recorded *before* the sessions the update cites, and were on
disk when it was written. The retitling was right and the wiring conclusion was right; the
diagnosis built on top of them is not.

**ISS-14 should be re-examined and is a candidate to close.** Not closed here: this entry
records a reading, and the issue tracker is a separate edit.

### 3 · Two flight units were transmitting on one channel

This is the most likely way the wrong conclusion was reachable, and it is a fault in its
own right. `20260909-011251` does not contain one telemetry stream. It contains two,
interleaved packet by packet:

```
B seq=55   ms=63070    lat=0.00000  sat=0   rssi=-80.0
A seq=117  ms=123552   lat=4.12349  sat=14  rssi=-69.0
B seq=56   ms=64070    lat=0.00000  sat=0   rssi=-82.0
A seq=118  ms=124554   lat=4.12349  sat=14  rssi=-74.0
B seq=57   ms=65070    lat=0.00000  sat=0   rssi=-80.0
A seq=119  ms=125555   lat=4.12349  sat=14  rssi=-72.0
```

Two independent sequence counters, two independent uptimes about 60 s apart, both at the
1 Hz cadence, with consistently different RSSI. That is two transmitters, not one vehicle
misbehaving. Both are on `919.0 MHz`, sync word `0xAA` and team `MRC`, so the ground station
forwards both and cannot do otherwise.

**One of them has a working GPS and the other has none.** A reader who followed the `B`
stream saw a vehicle that never fixed, through a whole session, with a `sat` of 0 — which
is exactly the reading ISS-14 now records.

`20260909-004618` shows the same interleaving over a shorter overlap, and a third uptime
again reporting `hdop=0.0, fixq=-1` — the *receiver never reported it* sentinel from
`Sensors.ino:171`, which is a different state from `hdop=100, fixq=0` meaning *reported, no
fix*. One packet in that session reads `chute=14, ul=14`, which belongs to no vehicle this
project has otherwise accounted for today.

**The packet has no vehicle identifier.** `seq` is per-boot and `ms` is per-boot, so nothing
in a frame says which unit sent it. The dashboard merges both into one timeline, one
`alt` trace, one `chute`, one `ul`. Every field goes through this — not only GPS.

This is `ISS-13` arriving from inside the team rather than from another one. A new issue
is wanted for it; none is raised here.

### 4 · The 065 sensor corruption recurred once, and it is not the BME280 alone

`20260909-024620`, one session out of twenty:

```
$MRC,1,14942,33.33,100.0,1048.60,-nan,0.000,0.000,-0.000,0.00,0.00,0.00,…
$MRC,2,15922,33.32,100.0,-175.48,0.1,0.000,0.000,-0.000,0.00,0.00,0.00,…
$MRC,3,16922,33.32,100.0,-175.47,-0.1,0.000,0.000,-0.000,0.00,0.00,0.00,…
```

Humidity pinned at 100 %, pressure at −175 hPa, `alt` at `-nan` — the 065 signature. But
**all six MPU axes read exactly `0.000` in the same packets**: 148 packets with a dead
accelerometer and gyro, 199 with humidity pinned, and **five vehicle restarts inside one
session.**

065 called this "the BME280 failed". Both sensors sit on the same I²C bus (`SDA 1 / SCL 2`),
and both went out together while the unit rebooted five times. **A bus or supply fault fits
the evidence better than a failed barometer**, and the restart count points the same way.
Not proven — nothing here distinguishes a brownout from a loose connector, and the flight
unit still has no build stamp to say whether a restart was a reflash.

### 5 · Every session after 03:48 is clean

From `20260909-034824` onward, nine sessions, **11,528 packets**:

```
nan = 0        temp = 28.21 .. 44.88 °C     hum = 43.9 .. 75.1 %
               pres = 1009.21 .. 1012.25 hPa    alt within ±4 m
```

**`status.md` Next 0 — "fix the barometer, nothing about auto-eject is testable until it
reads" — is satisfied by this.** The unit that read −1740 m in 065 reads ~0 m now, with
correct pressure beside it and no NaN in eleven thousand packets. Why it recovered is not
recorded; no repair is described in any commit message from today.

`az` also reads `-1.000` g at rest in the same sessions. `status.md` Next 9 asked whether
`MPU_ACCEL_RANGE` disagreed with `MPU_ACCEL_SCALE`; they agree — `0x10` is ±8 g and 4096
LSB/g in both `Config.h` files — so the 0.92 g of session 5 was not a scale constant.

### 6 · `GPS_PacketTest` exists for the next run

`firmware/tools/GPS_PacketTest/` was added in `ec6f068`: real GPS wrapped in a GEN3.1
packet the ordinary dashboard parses, forwarded by the existing GEN4 ground station with no
reflash. Unmeasured fields carry `-999` rather than a plausible zero, so `parser.py`'s
`_PLAUSIBLE` bands flag every one of them as a warning and nothing invented can be read as
a measurement.

**Not compiled and not flashed.** `arduino-cli` is still not on this machine.

## Why it matters

**Two of today's conclusions were drawn from a stream and not from a vehicle.** With two
units on one channel and no identifier in the packet, "the GPS never fixes" and "the GPS
fixes at HDOP 0.8" were both true readings of the same log. This is the failure mode the
project is organised against, arriving through the door nobody was watching: not a number
the system cannot support, but a number attributed to the wrong source.

**It also puts a hole in the `chute`/`ul` reasoning.** The pair "`chute` rising with `ul`
unchanged proves the release was automatic" assumes both fields came from one vehicle. In a
session like `011251` they need not have.

**The barometer clearing is the first movement on Next 0 in three sessions**, and it moves
auto-eject from blocked to merely untested — for the seventh session running.

## Result

Nothing changed. No firmware, backend or frontend edit was made in the course of this
reading, and nothing was compiled.

Wanted, none of it done here:

- **ISS-14** re-examined against the six fixing sessions, and closed if nothing survives.
  The retitle and the wiring conclusion stand; the "not one valid fix" finding does not.
- **A new issue for two vehicles on one channel**, and a decision on whether the packet
  gains an identifier. That is a GEN3.2 bump, which has been declined twice on other
  grounds — this is a different ground and deserves its own hearing.
- **The 065 fault-3 diagnosis widened** from the BME280 to the I²C bus and the supply.
- **The sensor sanity check from 065 fault 4 is still not written**, and a `-nan` altitude
  still arms nothing and disarms nothing. Session `024620` is what it looks like on a bench.
- `logs/raw/20260909-011251-serial.log` is the evidence for the dual stream and
  `20260909-024620-serial.log` for the sensor fault. `logs/` is gitignored and these are
  the only copies.
