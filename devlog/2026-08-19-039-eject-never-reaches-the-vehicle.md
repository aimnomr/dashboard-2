# 039 · EJECT reaches the ground station but never the vehicle

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-02

## What

From `logs/raw/20260819-172515-serial.log`, Aiman's hardware run. EJECT was attempted
three times; each gave up after 15 retries with no confirmation.

```
[GCS] EJECT armed
[GCS] EJECT attempt 1/15
...
[GCS] EJECT attempt 15/15
[GCS] EJECT gave up after 15
```

**Two things this rules out immediately.**

`[GCS] EJECT armed` appears at all, which means `CMD:EJECT` reached the ground station over
serial. The fix in entry 033 works on hardware — that is confirmation, not assumption.

And `EJECT_TOKEN` is `"EJECT"` in **both** `Config.h` files, so the over-the-air token is
not a second spelling mismatch.

So the command leaves the PC, the ground station accepts it, transmits it 15 times, and the
vehicle never reports `chute >= 1`.

## The likely cause: the uplink is transmitted while the vehicle is deaf

The vehicle's cycle, from `MRC_FlightUnit_GEN3.ino:108`, runs **listen first, transmit
last**:

```
t=0     listen window opens      (400 ms)
t=400   listen window closes
t=415   transmit starts          (231 ms worst case)
t=646   transmit ends
t=1000  next listen window opens
```

The ground station transmits its uplink from `uplinkOnPacketReceived()` — the instant it
finishes receiving a telemetry packet. That is **t ≈ 646**, at which point the vehicle's
listen window closed 246 ms ago and the next one does not open for another 354 ms. The
radio is idle; nothing is receiving.

`Uplink.ino:84` states the assumption explicitly, and it is the wrong way round:

> *"The vehicle's listen window has just opened."*

It has just **closed**. The packet that triggers the uplink is emitted at the end of the
cycle, not the start.

If this is right, every one of the 45 transmissions landed in the vehicle's deaf period,
and **PING failed for the same reason** — which would also explain the silent ping in
entry 037, independently of the dead OLED.

## How to confirm it, decisively

Tether the flight unit's own USB at 115200 and press Ping.

| Result | Meaning |
|---|---|
| No `[FLT] PING received` | Nothing is arriving. Timing, as above |
| `[FLT] PING received, count N` appears | The uplink works and the fault is downstream — `chuteFire()`, the servo, or the `heardEject` path |

That single test separates the two halves cleanly, and it needs no code change.

## Proposed fix, not written

Delay the uplink transmission instead of sending it on packet arrival. After receiving a
packet, the vehicle's next listen window opens in about
`CYCLE_PERIOD_MS - (listen + sensors + transmit)` ≈ 354 ms and stays open 400 ms, so
transmitting **~400 ms after packet receipt** lands mid-window with margin on both sides.

It is a ground-station-only change; the flight unit is untouched. Not written here — it is
a firmware change, it cannot be tested without hardware, and the confirming measurement
above should come first. Guessing at a timing fix before knowing whether anything arrives
would be building on an unverified diagnosis.

## Result

Nothing changed. `ISS-02` is not resolved after all: the ground unit firmware exists and
transmits, but there is no evidence any uplink has ever been received by a vehicle.

**The chute has never been commanded successfully over the air.** That is the state to
carry into the next session, and it is worth more than any other open item — a launch with
an unverified uplink is a launch with no recovery command.
