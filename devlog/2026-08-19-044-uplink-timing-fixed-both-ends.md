# 044 · Uplink timing fixed at both ends

**Date** 2026-08-19
**Type** change
**Refs** ISS-02

## What

The fix entries 039, 040 and 043 built the case for, written at both ends. **Neither half is
compiled or flashed** — there is no Arduino toolchain here; Aiman builds.

### Vehicle — listen through the tail instead of holding blind

The question this answers: after transmitting, should the vehicle listen for the rest of the
cycle, or keep holding blind? **Listen**, and it is almost free.

`radio.startReceive()` is non-blocking. The SX1262 receives autonomously once armed, fills its
own buffer, raises DIO1, and **holds the packet until it is read**. The ground station's own
code already relies on this (`MRC_GroundStation_GEN3/Radio.ino:76-78`). So the vehicle does not
have to choose between listening and working — it can arm, write to SD, and poll DIO1 during
the hold.

| | before | after |
|---|---|---|
| 0–400 | listen | listen |
| 400–415 | sensors — deaf | sensors — deaf |
| 415–646 | transmit — deaf | transmit — deaf |
| 646 | — | **arm receive** |
| 646–~700 | SD write — deaf | SD write, receiving |
| ~700–1000 | blind hold — deaf | hold, polling DIO1 |
| **deaf** | **600 ms** | **246 ms** |

**The ordering is the entire fix.** `radioArmReceive()` goes immediately after
`radio.transmit()` and *before* `storageWrite()`. The ground station transmits ~10–15 ms after
the packet lands; arming at t≈647 is comfortably ahead of that. Arming after the SD write
would put the card's open/append/close in front of exactly the window the ground station uses
— the same bug in a new place. The two are on separate SPI buses (LoRa default, SD on HSPI),
so nothing is contended.

Changed:

- `Radio.ino` — extracted `radioArmReceive()` and `radioServiceUplink()`. The token matching
  now exists **once**; the front window and the tail hold both dispatch through it. Two copies
  is how a second `EJECT`/`CMD:EJECT` mismatch (entry 033) gets written.
- `radioListenForEject()` — services before arming, since the hold now leaves the radio
  receiving and a waiting packet would be discarded by `startReceive()`. No longer calls
  `standby()` at the window's end.
- `MRC_FlightUnit_GEN3.ino` — `holdUntil()` at the end of the loop became
  `holdUntilListening()`, which feeds the GPS *and* services the uplink. `chuteFire()` is
  already idempotent, so a command arriving in the tail needs no special handling.

### Ground station — burst on the command, not one shot per packet

Carried back from `MRC_GroundUnit_V3` (entry 043). `fireEjectBurst()` transmits 5 times at
300 ms spacing (~351 ms including airtime) the moment the serial command arrives, instead of
once per received telemetry packet.

The guarantee, now written next to the constants in `Config.h` because it is load-bearing:

```
spacing <= vehicle LISTEN_WINDOW  and  span >= vehicle CYCLE_PERIOD  =>  hit, any phase
351 <= 400                            1404 >= 1000
```

Three consecutive attempts span 702 ms, wider than the 600 ms deaf period, so three in a row
cannot all miss. `EJECT_MAX_ATTEMPTS 15` became `EJECT_ATTEMPTS 5` + `EJECT_RETRY_MS 300`.
`ejectPending` is gone — the burst is synchronous, so there is no state to carry between loop
iterations. `Display.ino` reports the total sent rather than a live attempt counter.

`radioPoll()` runs inside the inter-attempt gaps, so a confirmation can still land and stop
the burst early.

## Why both, when either would do

They are independent and they compose, and the reason for doing both is that they fail
differently.

The burst needs **no phase estimate at all**, which matters because the vehicle's phase is
known to move — it reports its own `cycle overran` and `cadence resynchronised`
(`MRC_FlightUnit_GEN3.ino:157-172`). Any fix that aims at a moment in the cycle is betting on
a number the vehicle itself says is not fixed. Entry 039's proposed 400 ms delay was exactly
that bet, and this supersedes it.

Tail listening does not fix the command path on its own — it makes every future uplink have
margin instead of precision. It also means PING, which is still a single shot on packet
arrival, now lands *inside* the window rather than 246 ms after it closed.

## Result

Six files changed across the two sketches. Nothing compiled, nothing flashed, nothing
confirmed on hardware.

**The order of operations for the next session has not changed, and it matters:**

1. **Tether the flight unit at 115200 and press Ping on the CURRENT firmware first.** This is
   still the decisive measurement, and flashing first destroys it — afterwards, a working
   uplink cannot be told apart from one that always worked.
2. Then flash both units and repeat.

If step 1 shows `[FLT] PING received`, the diagnosis in 039/040/043 is wrong, the uplink
always worked, and the fault is downstream in `chuteFire()` or the servo — where none of this
helps. That outcome is unlikely on three converging lines of evidence and is still not ruled
out by any of them.

`ISS-02` is now materially different from what it says and should be rewritten once step 1 has
an answer. Entries 040 and 043 both noted this; it is still the right time to wait.

## Costs accepted

- **~1.4 s of ground station transmission per EJECT press**, losing one or two telemetry
  packets. Losing a row in a log while commanding recovery is the correct side of that trade.
- **Continuous receive through the vehicle's tail** — roughly 10 mA more for 354 ms per
  second. Worth naming only because entry 026's brownout hypothesis was never tested and
  entry 042 records that the observation behind it is still unexplained.
