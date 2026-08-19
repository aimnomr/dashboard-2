# 043 · The GEN2 ground unit was found, and it solved the uplink problem already

**Date** 2026-08-19
**Type** investigation
**Refs** ISS-02

## What

Aiman obtained `MRC_GroundUnit_V3.ino` from the team member who wrote it — it had been left
out of the original handover. Added to `wiki/source/`. Nothing else changed.

**This is the firmware `ISS-02` says is missing.** The issue describes a ground unit that
"polls serial every 10 ms, matches `EJECT`, calls `fireEject()` and transmits the command 5×
at 300 ms spacing", and records that no such firmware existed in the source set. V3 does
exactly that, at `MRC_GroundUnit_V3.ino:70-93` and `:129-145`. The artefact `ISS-02` was
opened for is no longer missing.

## Why it matters far more than closing an issue

**GEN2 already solved the timing problem that GEN3 has, and GEN3 replaced the solution with
the failure.**

V3's header states the intent outright:

> *"Retransmit EJECT up to 5 times with 300 ms gaps to maximise chance of landing in flight
> unit's RX window."*

Its author knew the vehicle is deaf most of the time and that the ground station cannot know
the phase. So V3 does not try to know it. `fireEject()` transmits **immediately on the serial
command** and sprays five attempts across ~1.4 s:

```
standby -> delay 10 -> transmit (~41 ms at SF7) -> startReceive -> delay 300
```

≈ 351 ms per iteration, with transmits beginning at t ≈ 10, 361, 712, 1063, 1414 ms.

**Against GEN3's vehicle timing that is not a good chance — it is a guarantee.** The vehicle's
cycle is 1000 ms with a 400 ms listen window, so the deaf period is 600 ms. Attempts are
351 ms apart, so any three consecutive attempts span 702 ms, which is wider than the deaf
period; three in a row cannot all miss. Five attempts spanning 1404 ms exceed a full cycle.
At least one lands inside the window **regardless of phase**.

The general condition, worth writing down because it is what makes the strategy sound:

| Requirement | GEN2 against GEN3 timing |
|---|---|
| Spacing ≤ listen window | 351 ms ≤ 400 ms ✓ |
| Total span ≥ cycle period | 1404 ms ≥ 1000 ms ✓ |

**GEN3 does the opposite.** `uplinkOnPacketReceived()` transmits once per *received telemetry
packet*, which is deterministically t ≈ 646 ms into the vehicle's cycle — 246 ms after the
window closed. That is not a strategy that usually misses. It is **phase-locked to the one
moment in the cycle that cannot work**, which is why 15 attempts in entries 039 and 040
performed exactly as well as one would have: not at all.

GEN3 traded a phase-*independent* burst for a phase-*locked* single shot, justified by a
comment that had the vehicle's cycle order backwards — listen-first read as transmit-first,
recorded in two places (`Uplink.ino:84`, `Radio.ino:126`) and wrong in both.

## Proposed fix — adopt V3's burst, not entry 039's delay

Entry 039 proposed transmitting ~400 ms after packet receipt, to land mid-window. That works
only while the phase estimate holds, and it depends on the same class of reasoning that
produced the bug: a belief about where the vehicle is in its cycle. The vehicle already
reports `cycle overran` and `cadence resynchronised` (`MRC_FlightUnit_GEN3.ino:157-172`), so
that phase is known to move.

V3's approach needs no phase estimate at all. Recommended over 039's, which this supersedes:

- transmit on the serial command, not on packet receipt
- 5 attempts, ~300 ms apart, `startReceive()` between them as V3 does
- keep GEN3's `chute >= 1` confirmation check to stop the burst early
- keep `PING` on the same path, so the link test exercises the real command timing

**Cost:** the ground station is transmitting for ~1.4 s per press and will drop one or two
telemetry packets. Losing a packet while commanding recovery is the right trade, and V3's
`startReceive()` between retries already minimises it.

**Caveat worth pinning:** the guarantee depends on spacing ≤ listen window. At GEN3's 400 ms
window a 300 ms gap has 49 ms of margin. If `LISTEN_WINDOW_MS` is ever shortened, this stops
being guaranteed and quietly reverts to a probability. That constraint belongs next to the
constant.

## Result

Nothing built. `wiki/source/MRC_GroundUnit_V3.ino` added; `source/` is external fact and is
not edited.

`ISS-02` should now be rewritten — not resolved. The firmware it was opened about has been
found, and its absence is no longer the problem; the problem is that GEN3 did not carry its
timing strategy forward. Entry 040 already noted the issue text is stale.

**The decisive test from entry 039 is still worth running first**, and is now cheaper to
interpret. Tether the flight unit at 115200 and press Ping:

- **no `[FLT] PING received`** → timing, and this entry's fix is the one to write
- **`[FLT] PING received`** → the uplink already works and the fault is downstream in
  `chuteFire()` or the servo, which no amount of retry timing will fix

Three independent lines now point at timing: the vehicle's code audited clean (040), the
ground station's assumption documented backwards in two files (039, 040), and GEN2 having
deliberately engineered around the exact problem GEN3 reintroduced. That is strong enough to
write the fix against — and still not a measurement.
