# 020 · Flight unit GEN3, and the OLED as a pre-launch instrument

**Date** 2026-08-19
**Type** change
**Refs** ISS-02, ISS-08, ISS-13

## What

```
firmware/MRC_FlightUnit_GEN3/
├── MRC_FlightUnit_GEN3.ino   setup, loop, deadline scheduler
├── Config.h                  every tunable
├── Radio.ino                 LoRa init + receive window
├── Sensors.ino               BME280, MPU6050, GPS, calibration
├── Packet.ino                GEN3 framing + CRC16
├── Storage.ino               SD
├── Chute.ino                 release mechanism, isolated
└── Display.ino               OLED
```

Both units' OLED output rebuilt, and `PING` added to the uplink on both sides.

## Why

Aiman asked whether both units use the on-board OLED, wanting vital information legible
without a serial monitor — most of all on the flight unit, which sits sealed on the pad
nowhere near a laptop.

Both already did. The flight unit's was not good enough for that job, and looking at it
surfaced a real gap.

## Result

**The uplink could not be tested without firing the parachute.** The only evidence the
vehicle had ever heard the ground station was the chute counter rising — a destructive
test. A pre-launch two-way check was therefore impossible.

Fixed with a `PING` command. `CMD:PING` on the ground station transmits `PING`; the
vehicle records it, counts it, and resets the "UL" age on its screen without touching the
release. The pad procedure is one person watching the sealed unit while another sends it.

`PING` is timed to follow a received packet like `EJECT`, so it lands in the listen
window — but falls back to a blind transmission after 3 s. Waiting forever for a packet
to time against would make a link test impossible in exactly the case where you most want
one: nothing being received.

**Flight unit display rebuilt around five questions**, one dense page rather than GEN1's
three rotating screens. Rotation is wrong here — you look up when you look up, and the
page you need may be four seconds away.

| Question | Shown as |
|---|---|
| Alive and cycling? | sequence number climbing |
| Sensors real? | accelerometer magnitude, must read ~1.00 g at rest |
| Calibration sound? | worst-axis gyro bias |
| Fix and logging? | satellite count and SD state |
| **Uplink proven?** | seconds since last `PING`/`EJECT`, or `UL --` plus a `!` marker |

The gyro-bias figure is on the glass because real hardware showed 4.1 °/s after
calibration, which integrates to a full turn over a flight. That was previously printed
only to serial, where nobody at the pad will read it.

**Ground station display** now leads with packet age. A ground station that has gone deaf
looks identical to one watching a quiet vehicle, and the difference matters most when it
is worst.

**Verified.** All four sample packets round-trip from the flight unit's builder through
the ground station's parser; single-bit corruption is caught at three positions; the
reference test still passes 11/11. Every OLED line checked against the 21-character limit
at extreme values — a clipped line is a fault nobody would see.

Fixes from the plan that made it into the code: GPS speed offset moved out of the display
function into the GPS path and gated on a usable fix; sensors read once per cycle and
shared; SD header replaced with a format comment, since the log now holds complete framed
packets rather than bare CSV; `delay(200)` replaced by a deadline scheduler; OLED bus
clock raised to 400 kHz.

## Not done

**Neither sketch has been compiled.** There is no Arduino toolchain here. The logic is
cross-checked in Python but has never been through a compiler — expect something small on
the first build. The likeliest candidate is Arduino's automatic prototype generation
around functions taking `Telemetry &`, which is a known rough edge.
