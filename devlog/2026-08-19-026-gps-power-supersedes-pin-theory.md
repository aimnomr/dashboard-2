# 026 · GPS fault is power, not pins

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-14

Supersedes the leading cause identified in entry 025.

## What

Aiman reported that the GPS module **used to light up and now does not**, once the whole
system is powered together. Added `firmware/tools/GPS_Minimal` and rewrote the head of
`ISS-14`.

## Why this changes the diagnosis

Entry 025 concluded that `chars=0` most likely meant GPIO19/20 had been claimed by the
ESP32-S3's USB peripheral. That reasoning was sound but aimed at the wrong layer: **an
unpowered module produces `chars=0` regardless of which pins it is wired to.** The simpler
explanation was available and I did not have the observation that pointed to it.

The USB-pin material is kept in `ISS-14` rather than deleted, because it remains worth
ruling out once power is confirmed good.

## Result

Two power hypotheses, both supported by the project's own documentation:

**A — the module is on the switched `Ve` rail, not permanent `3V3`.** The Heltec V3 gates
`Ve` with GPIO36. `Pins_Assignment.md` specifies `VCC - 3.3V` for the GPS, so which pin it
physically sits on needs confirming.

**B — brownout under full load.** LoRa TX peaks near 120 mA at 17 dBm, the SD card bursts
to ~100 mA, plus OLED, two I²C sensors, and a GPS drawing ~50 mA while acquiring. The
telling detail is already in `Pins_Assignment.md`: **the SD card is specified on external
5 V.** Somebody hit this rail's limit before, wrote down the workaround, and the note
survived without the underlying constraint being recorded.

`GPS_Minimal` starts nothing but the GPS UART — no LoRa, no SD, no OLED, no I²C. It is the
lightest possible load, so data flowing there but not in the full firmware isolates a
power budget problem in one run.

Also flagged: **many NEO-6M breakouts carry only a PPS/fix LED**, which stays dark until a
fix exists. A dark LED with no fix is normal and is not evidence of a power fault — worth
establishing which LED is being watched before drawing conclusions from it.

The decisive step is a multimeter on the module's VCC with the system running. One
measurement separates all three possibilities, and no amount of further firmware work
substitutes for it.
