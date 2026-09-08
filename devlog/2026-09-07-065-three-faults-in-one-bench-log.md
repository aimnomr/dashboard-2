# 065 · A swallowed EJECT, an undercounted chute, and a poisoned altitude baseline

**Date** 2026-09-07
**Type** investigation
**Refs** 058, 061, 063, 064, ISS-17

Nothing here is fixed. Three faults were read out of one bench session's raw logs, and a
fourth was found in the code while explaining the third. All four are open.

Sources: `logs/raw/20260907-195122-serial.log` and `logs/raw/20260907-195326-serial.log` —
one ground station session (build stamp `Sep  7 2026 19:48:44`, no second `[GCS] ready`
line) across two dashboard runs.

## What

### 1 · An unconfirmed burst makes the NEXT EJECT a no-op

Four operator EJECTs, and the second one transmitted nothing at all:

```
 11  seq=86  chute=3  ul=4
 12  [GCS] EJECT armed
 13  [GCS] EJECT confirmed after 0 attempt(s)
 14  seq=87  chute=3  ul=4        <- no attempt lines, ul flat
```

The chain starts one session earlier. EJECT #1 ran its full burst and never saw the
confirmation, because the burst blocks ~1.4 s and ate the two packets that carried it:

```
16-21  [GCS] EJECT armed / attempt 1/5 … 5/5
22     [GCS] EJECT burst complete, 5 sent - watch chute in telemetry
23     seq=16  chute=3  ul=3      <- seq 14 and 15 missed during the burst
```

The release happened — `chute` 0 to 3 — but `fireEjectBurst()` fell out of its loop instead
of returning early, so `ejectConfirmed` stayed false and `chuteBaseline` stayed 0.

`handleCommand()` only re-baselines inside `if (ejectConfirmed)`. With that flag false the
next EJECT goes straight to `fireEjectBurst()`, whose first act is `lastChute > chuteBaseline`
— 3 > 0 — so it confirms against the PREVIOUS release and returns without transmitting.

The state is self-perpetuating. Every burst that ends `burst complete` leaves the baseline
stale, and two of the three real bursts in this log ended that way:

| # | result | chute | ul |
|---|---|---|---|
| 1 | `burst complete, 5 sent` | 0 to 3 | 0 to 3 |
| 2 | `confirmed after 0 attempt(s)` — **nothing sent** | 3 | 4 |
| 3 | `confirmed after 2 attempt(s)` | 3 to 4 | 4 to 6 |
| 4 | `burst complete, 5 sent` | 4 to 7 | 6 to 9 |

**The log ends with the ground station in the broken state.** `chuteBaseline` is 4,
`lastChute` is 7, `ejectConfirmed` is false — so the next EJECT will be swallowed too.

This is ISS-17 and devlog 058 again, on the path 058 did not touch. 058 fixed the absolute
test by baselining the burst the way `fireConfigBurst()` baselines `ul`. It baselined the
`RESET:CHUTE` exit and the cooldown exit. It did not baseline the exit where the burst simply
runs out of attempts, and that exit records nothing at all — not even that a burst was sent.

### 2 · `chute` undercounts in the front listen window

EJECT #3, two attempts ~351 ms apart against a 400 ms window:

```
20  seq=93  chute=3  ul=4
26  seq=94  chute=4  ul=6        <- chute +1, ul +2
```

`radioServiceUplink()` increments `uplinkCount` per packet. `radioListenForEject()` returns
a **bool**, and its caller does one increment for the whole window:

```c
if (radioListenForEject(LISTEN_WINDOW_MS)) { chuteCommands++; chuteFire(); }
```

`holdUntilListening()` increments per packet, the documented way. So the two intake paths
disagree, and `COMMANDS.md:304` — "`chute` counts eject packets RECEIVED, not releases
performed" — is true only of the back-half hold. Burst spacing is under the window width by
construction (the arithmetic in `Uplink.ino`), so two attempts in one window is routine.

Harmless to the mechanism: `chuteFire()` is idempotent either way. Not harmless to the
ground station, whose only confirmation signal is this counter — undercounting makes an
early exit less likely, which feeds fault 1.

### 3 · The BME280 failed, and the reboot captured the failure as the baseline

At seq 146 of the pre-reboot run the sensor stopped returning physical values:

```
85  seq=146  temp=29.80   hum=30.3   pres=1007.50   alt=-nan
88  seq=149  temp=29.86   hum=30.2   pres=-130.36   alt=-1764.6
90  seq=151  temp=180.77  hum=100.0  pres=-164.02   alt=-nan
92  seq=153  temp=180.77  hum=30.1   pres=1238.37   alt=-nan
```

180.77 °C, humidity pinned at 100 %, pressure at −164 and +1238 hPa. `t.pres`
(`Sensors.ino:231`) and `t.alt` (`Sensors.ino:233`) are separate I²C transactions, which is
why one field reads sane while the other in the same packet is garbage.

The vehicle then restarted — `seq=1`, `ms=15162`, `chute=0`, `ul=0`. **Why is not knowable
from the ground:** a brownout and a deliberate power cycle look identical here.

It came back worse. `baseAltitude` is captured once, at `Sensors.ino:126`, with no
plausibility check and no re-read — and it was captured during the corruption:

```
205  seq=43  temp=31.73  hum=100.0  pres=1007.54  alt=-1739.1
```

Pressure correct, altitude off by about **−1740 m**, 5 of 43 packets still `-nan`, humidity
still pinned. **Auto-eject cannot arm in this state** — `alt >= cfg.armAltM` is unreachable
at −1739 m, at the 30 m default or at the 5 m floor.

### 4 · A NaN altitude does not disarm the trigger — it fires it

Found while explaining 3. There is no `isnan` or `isfinite` guard anywhere in the flight
firmware; `sensorsRead()` passes `bme.readAltitude()` through raw and `apogeeUpdate(tm.alt)`
consumes it. Every comparison against NaN is false, and the rule reads those falses in
opposite directions:

```c
if (alt > apogeeAlt) apogeeAlt = alt;                 // false — apogee frozen
if (!apogeeArmed && alt >= cfg.armAltM) { ... }       // false — cannot ARM on NaN
...
float drop = apogeeAlt - alt;                         // NaN
if (drop < cfg.dropM) { descentCycles = 0; return false; }   // false — NOT reset
if (++descentCycles < cfg.confirmN) return false;     // increments every cycle
autoEjectFired = true;                                // fires at confirmN
```

An **already-armed** vehicle that starts reading NaN releases the chute `confirmN` cycles
later, wherever it is. It cannot arm from NaN, so the pad is safe — but a sensor glitch at
40 m on the way up is a deployment. The vehicle in this log never armed (`alt` ~0, `ARM` 30),
so it never fired, and the fault is latent rather than observed.

## Why it matters

**`status.md` Next 2 said auto-eject "needs no reflash" and is ready to test.** That is now
false in both directions: the vehicle cannot arm with the baseline it has, and the trigger
has an input it handles unsafely. Fixing the barometer is the gate on the auto-eject test,
not the trigger.

**Faults 1 and 3 both punish the same reflex** — reading a confident console line as
evidence. `EJECT confirmed after 0 attempt(s)` is printed for a release that did not happen,
and `pres=1007.50` is printed beside an altitude that is wrong by 1.7 km. Both are the system
reporting a number it has, not a number it measured.

**Fault 1 is the third appearance of one bug.** An absolute test where a baseline was needed:
devlog 058 (`lastChute >= 1`), devlog 063 (`chute > 0` gating the panel), and now the
un-baselined burst exit. Each was found by hardware, not by a test.

## Result

Nothing changed. No firmware, backend or frontend edit was made, and nothing was compiled —
`arduino-cli` is still not on this machine.

Proposed fixes, none written:

- **1** — record `chuteBaseline = lastChute` on the burst-complete exit of `fireEjectBurst()`,
  or set `ejectConfirmed` there so the existing re-arm path runs. Needs the SINGLE/MULTI
  interaction from 064 thought through before it is written.
- **2** — `radioListenForEject()` returns a count, and the caller adds it. One line each side.
- **3** — hardware. I²C wiring on `SDA 1 / SCL 2`, then confirm `alt` reads ~0.0 with no NaN
  across a couple of minutes before any auto-eject test is believed.
- **4** — reject a non-finite `alt` at the top of `apogeeUpdate()`, and sanity-band
  `baseAltitude` at calibration. Both are new behaviour on the trigger and are a decision,
  not a patch.
