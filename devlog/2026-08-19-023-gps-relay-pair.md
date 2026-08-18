# 023 · GPS relay diagnostic pair

**Date** 2026-08-19
**Type** change
**Refs** —

## What

```
firmware/tools/GPS_Relay_Flight/    CanSat side: digest + raw NMEA over LoRa
firmware/tools/GPS_Relay_Ground/    laptop side: prints everything to USB
firmware/tools/README.md            how to read the output
```

## Why

The `GPS_Passthrough` sketch from entry 022 dumps to USB, which requires the CanSat to be
tethered to the laptop — the wrong place for a device that needs a clear view of the sky.
Aiman asked for a paired version so the units stay where they belong: CanSat outside,
laptop indoors.

Both sketches use the same radio configuration as the GEN3 firmware, so swapping between
diagnostic and flight builds needs no reconfiguration.

## Result

**All the NMEA cannot be relayed.** A NEO-6M emits roughly 400–600 bytes per second across
GGA, GLL, GSA, GSV, RMC and VTG. At SF7/BW125 a 100-byte packet costs about 180 ms of
airtime, so the channel tops out near 500 bytes/second at **100% duty cycle** — forwarding
everything is not possible and attempting it would jam the band.

Sends a **digest plus one full raw sentence** per second instead, rotating GGA → GSV →
RMC → GSA so everything is visible across four seconds. The digest is the more useful
half: it answers the diagnostic questions directly rather than requiring someone to read
NMEA by eye.

**The field the flight firmware cannot give you is `inview`.** `satellites` in the
telemetry packet comes from GGA and counts satellites *used*, which is zero until there is
a fix — so it cannot separate "antenna is dead" from "antenna is fine, still acquiring".
Satellites *in view* comes from GSV and is read here with `TinyGPSCustom`. If `inview > 0`
and `used = 0`, the hardware is working and the answer is patience.

The relay also shows the same summary on the CanSat's own OLED, since someone standing
outside holding it is the most likely reader.

The ground side deliberately forwards **everything** — no team-marker filter, no checksum
check, unlike the real ground station. In a diagnostic, another team's traffic and
corrupted packets are both information.

`GPS_Passthrough` is kept for the desk case where the only question is whether the module
is alive at all, and the README says which to reach for.
