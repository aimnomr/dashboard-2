# 069 · The release fires on receipt, and the gap before the transmit is no longer deaf

**Date** 2026-09-09
**Type** fix
**Refs** 033, 039, 043, 044, 061, 065, 067

Flight unit only. Two latency faults on the commanded release path, found by tracing the
path end to end rather than by a log. Nothing on the ground station changed — the
swallowed-EJECT fault (065 fault 1) is still open and is the subject of its own entry.

## What

### 1 · The drive waited for the listen window to close

`radioListenForEject()` accumulated a bool across the whole 400 ms window and returned it
at the end; the caller drove the mechanism only then:

```c
if (radioListenForEject(LISTEN_WINDOW_MS) && chuteFire()) { chuteCommands++; … }
```

So a command heard at t=5 ms of the window was not acted on until t=400 ms. **Up to
~395 ms of latency, on the one path in this system where latency costs a parachute.**

The back half never had this. `holdUntilListening()` has always fired per packet, inside
its loop. The two intake paths disagreed about *when* a release happens, on top of the
disagreement about *how it is counted* that 065 recorded as fault 2 and 067 half-fixed.

Both halves now fire on receipt. `radioListenForEject()` is `void` and drives from inside
its own loop; the caller is one line. The window still runs to completion — leaving early
would shorten the cycle and break the fixed cadence — but the release no longer waits for
it.

### 2 · A command arriving during the sensor read was silently destroyed

There was no `radioServiceUplink()` call anywhere between the listen window closing and
the back-half hold. The cycle in between is:

```
t=400 → 415   sensors (~15 ms)   radio armed, nothing reading it
t=415         auto-eject decision
t=415 → 646   TRANSMIT           radio switches to TX
t=~647        radioArmReceive()  startReceive() discards an unread held packet
```

The SX1262 receives autonomously and **holds** a packet until it is read — which is the
property that makes the back-half listening nearly free, and which `radioListenForEject()`
relies on when it deliberately does not `standby()`. But holding only helps if something
reads it. Nothing polled DIO1 across that stretch, so a command landing in the ~15 ms
sensor read was held, then thrown away by the transmit and the re-arm after it.

Recovering it costs a whole retry — **351 ms** — because the ground station's next burst
attempt is the only thing that will try again.

One service call after `sensorsRead()` closes it. Placed before the auto-eject check on
purpose: a commanded release decided there is visible in *this* cycle's `chute`, and
`apogeeUpdate()` then correctly refuses on `chuteIsFired()` rather than claiming the same
release as automatic. That guard was written in 067 as "currently unreachable, but cheaper
than one that is missing when the guard above it moves." The guard above it just moved.

### 3 · One function instead of four copies

There are now four sites that service the uplink — the front window's pre-arm check, the
front window's poll loop, the new post-sensor call, and the back-half hold. Each needs the
same three lines after a successful drive, so they went into `chuteFireFromUplink()` in the
main sketch rather than being written out four times.

`Radio.ino:35` already warns that two copies of the token matching is how the entry 033
mismatch got written, and 065 fault 2 was the front window and the back half drifting
apart. Same failure, one level up.

## Why

The commanded path is the only release path an operator controls, and it is the backup to
an auto-eject that has never fired. Its budget, against the vehicle's 1000 ms cycle:

| | before | after |
|---|---|---|
| best case | ~56 ms | ~56 ms |
| **typical** | ~220 ms | **~56 ms, in ~75% of cycles** |
| worst case | ~446 ms | ~400 ms |

The residual worst case is the transmit itself — 231 ms of half-duplex deafness — plus one
351 ms retry when an attempt lands inside it. Neither is touched here. **The burst timing
was deliberately left alone:** the constraint at `Uplink.ino:26` requires the retry spacing
to stay wider than the deaf window, and buying ~100 ms by narrowing it would convert a
guarantee into a probability with nothing failing loudly.

Both faults are the same shape as the ones this project keeps finding: a value that exists
and is correct, arriving somewhere nothing reads it. The bool was computed on the first
tick of the window and used on the last; the packet was held by the radio and discarded
unread.

## Result

**Not compiled and not flashed.** `arduino-cli` is still not on this machine, and no
hardware has run any of this. Backend 154 pass and `verify_gen3.py` is 14/14, but neither
exercises flight firmware — they pin the packet format and the command grammar, and
nothing in either suite reaches `radioListenForEject()` or `chuteFire()`.

Reviewed by inspection instead. `heardEject` survives only as `radioServiceUplink()`'s own
local; `radioListenForEject()` has one caller and it ignores no return value because there
is none; `chuteCommands++` appears exactly twice, in the new helper and on the auto-eject
path, which keeps its own distinct console line. `MRC_FlightUnit_GEN3` was not touched and
still returns a bool from its own listen window — a deliberate divergence, recorded here so
it is a decision and not an oversight: GEN3 has no auto-eject and no `SET`, and nothing is
flying it.

Also corrected: `chuteCommands`' declaration comment still read "eject commands received",
which 067 made false.

**Still open on this path, and not addressed here:**

- **065 fault 1** — the ground station's `fireEjectBurst()` still records nothing on its
  run-out-of-attempts exit, so a confirmation arriving after the burst ends leaves a stale
  `chuteBaseline` and silently swallows the *next* EJECT. That is unbounded latency and it
  outranks everything measured above. Its own entry, next.
- **065 fault 4** — a non-finite `alt` still fires the auto-eject trigger rather than
  disarming it. Untouched.
- The auto path is where the seconds are: `AUTO_EJECT_CONFIRM_N` is counted in cycles, so
  a confirmation costs 1000 ms each and the trigger samples altitude once per second.
  Deliberately not changed here — it alters what the trigger does, which per 065 is a
  decision rather than a patch.
