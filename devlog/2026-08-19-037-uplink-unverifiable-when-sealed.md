# 037 · Uplink confirmation is display-only, and the OLED is dead

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-02, ISS-14

## What

The OLED on the current flight unit is not working. Aiman pressed Ping and saw no
acknowledgement from the CanSat, which left the uplink unverified.

Traced where a ping is actually observable. Three findings.

**1 · "No ack" is expected, not a failure.** There is no acknowledgement path at all.
`Uplink.ino` prints *"PING sent - no acknowledgement exists, check the vehicle's screen"*.
Silence after a Ping is absence of evidence, not evidence of absence — the ping may well
have arrived.

**2 · The vehicle does report it, over USB.** `Radio.ino:66-73` receives `PING_TOKEN`,
increments `pingCount`, and prints:

```
[FLT] PING received, count N
```

That print is **not** guarded by `#if ENABLE_SERIAL_ECHO`. It is unconditional, so it
works today with no firmware change: tether the flight unit to its own USB port at 115200
and watch. Independent of the display.

**3 · The real gap.** `pingCount`, `uplinkHeard` and `lastUplinkMs` are referenced in
`Display.ino` **and nowhere else**. Uplink health lives entirely in a display and never in
data.

## Why it matters more than the broken screen

A dead OLED is a hardware fault on one unit. The design gap behind it is not.

Even with a perfectly working screen, **the OLED cannot be seen once the CanSat is sealed
in the rocket**. `Config.h` assumes *"one person watches the sealed unit, another presses
PING"* — which works on a bench and stops working the moment the unit goes in. And in
flight, at the point where someone is deciding whether EJECT will land, there is no route
to the answer at all.

So uplink verification is available only in the one situation where it is least needed:
vehicle in your hands, screen visible, nothing at stake.

## Result

`wiki/decisions/pre-launch-checklist.md` section D rewritten. It previously said *"confirm
the UL counter resets on the vehicle's OLED"* and *"STOP if the UL counter does not move"* —
an instruction that cannot be followed on this set. It now offers both witnesses, marks the
serial route as the one that survives a dead screen, and records the gap.

**Proposed fix, not built: add a `ul` field to the telemetry packet**, alongside `chute`.

| | |
|---|---|
| Gains | Uplink health visible with a dead OLED, sealed, at altitude, on the dashboard |
| Firmware | One field in `packetBuild()` |
| Parser | 17 vehicle fields become 18, and both shapes must parse — `FLIGHT21/22.CSV` are 17-field and are the regression corpus |
| Airtime | ~3 bytes, roughly 7 ms. Negligible |

It converts the only two-way control on the vehicle from unverifiable to verifiable, and
`chute` already proves the pattern works: it is a *count* reaching the ground in telemetry,
which is exactly what `ul` would be.

Deliberately not implemented at session end — a packet format change is a decision, and it
touches the corpus that pins the parser.
