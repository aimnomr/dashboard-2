# Status

**Updated** 2026-09-07 · end of session 8

## Now

**The repeat release works on hardware, and the bench log that proved it contains three
other faults plus a latent one.** None of the four is fixed. Session 8 built the SINGLE/MULTI
release path end to end (060–064) and then spent its last stretch reading the logs that path
produced (065), which is where the faults came from.

Branch **`feature/apogee-trigger`**, seven commits ahead of `main` (`72c9768`):

```
4276712  Return the release mechanism on a timer, and re-arm both ends   (061)
333468e  Changed config word                                            (060)
a57c2b0  Fix the eject re-arm, and render the chute as a count          (058, 059)
b72de9e  Fix the CHUTE_PIN divergence and audit the command reference   (057)
fdecc00  Add a numeric packet readout, fed by the backend's field table (056)
d4235a6  Add COMMANDS.md, a full command reference                      (054)
089d0b9  Add GEN4: auto-eject trigger configurable over the uplink      (053)
```

**062, 063, 064 and 065 are uncommitted** — 23 modified files (this one included) and 4
untracked devlogs.

### What was confirmed on hardware this session

```
EJECT            -> servo threw, chute 0->3                    ✓
EJECT again      -> confirmed after 2 attempts, chute 3->4     ✓   (061/063/064 work)
PING             -> ul rose                                    ✓
EJECT (2 of 4)   -> "burst complete", never confirmed          ✗   fault 1
EJECT (1 of 4)   -> "confirmed after 0 attempts", nothing sent  ✗   fault 1
auto-eject       -> never reached; the vehicle cannot arm      ✗   fault 3
```

The repeat release is real and it is the headline: 061 through 064 do what they were built to
do. Everything else on that list is 065.

### Session 8, by entry

**060** the GEN4 sync word moved off `0xAB` — committed directly as `333468e`, recorded after
the fact. **061** the mechanism returns to ARMED on a timer and the fire latch clears on a
second, longer one; `chuteFire()` stopped blocking and one flag became three. **062** GPS
`RX`/`TX` back to 20/19 for the new PCB — *not* a revert of 042; the hardware moved. **063**
the dashboard can command a repeat release: the banner and controls coexist, and a cooldown
replaced the latch. **064** `SET:REPEAT:0|1`, SINGLE and MULTI switchable from the ground,
with auto-eject deliberately excluded from the re-arm. **065** the four faults below.

## Next

0. **Fix the barometer. Nothing about auto-eject is testable until it reads.** The BME280 on
   the current flight unit returns 180.77 °C, humidity pinned at 100 %, and pressure at −164
   and +1238 hPa. It failed mid-run, the vehicle restarted, and `baseAltitude` was captured
   during the corruption — so `alt` now sits at about **−1740 m** with correct pressure
   beside it, and 5 packets in 43 still read `-nan`. Check the I²C wiring on `SDA 1 / SCL 2`,
   power-cycle flat and still, and confirm `alt` reads ~0.0 with no NaN across a couple of
   minutes **before believing any auto-eject result.** (065 fault 3.)
1. **`fireEjectBurst()` leaves a stale baseline when the burst runs out of attempts, and the
   next EJECT is then silently swallowed.** The log ends with the ground station in exactly
   that state: `chuteBaseline` 4, `lastChute` 7, `ejectConfirmed` false. Two of three real
   bursts ended this way, so it is the common case, not the edge. Fix needs the SINGLE/MULTI
   interaction from 064 thought through first. (065 fault 1, and the third appearance of the
   absolute-test bug after 058 and 063.)
2. **A NaN altitude fires the trigger rather than disarming it.** `drop < cfg.dropM` is false
   for NaN, so `descentCycles` is never reset and increments to `confirmN`. An already-armed
   vehicle that starts reading NaN deploys wherever it is. It cannot arm *from* NaN, so the
   pad is safe. Rejecting non-finite `alt` in `apogeeUpdate()` and sanity-banding
   `baseAltitude` are both new trigger behaviour — a decision, not a patch. (065 fault 4.)
3. **`chute` undercounts in the front listen window.** `radioListenForEject()` returns a bool,
   so two EJECTs in one 400 ms window increment `ul` twice and `chute` once; the back-half
   hold counts per packet. Observed as `chute +1, ul +2`. Return a count instead — one line
   each side. `COMMANDS.md:304` is wrong until it is fixed. (065 fault 2.)
4. **Auto-eject has still never been tested.** Sixth session running. It needs no reflash and
   the vehicle firmware is believed correct, but it is now blocked on 0 and should be
   reconsidered against 2 first. `SET:ARM:5.0`, `SET:DROP:2.0`, `SET:CYCLES:1`, then a
   stairwell; re-arm between runs with `RESET:CHUTE`, never plain `RESET`. Expect `chute`
   rising with **`ul` unchanged** — that pair is the only ground-side proof a release was
   automatic.
5. **`AUTO_EJECT_CONFIRM_N` still needs a real descent rate.** Three cycles is three seconds;
   at 30 m/s that is 90 m. Unchanged since session 5 and unanswerable until something falls.
6. **Nothing pins the apogee state machine.** Sessions 5, 7 and now 8 each found real errors
   in it without a committed test — the RESET re-arming behaviour, the eject latch
   interaction, and now the NaN path. That is three times. The trace should be committed.
7. **`backend/devtools/mock_source.py` still emits GEN2** — no `$MRC`, no CRC. It cannot
   exercise auto-eject, GEN4, or the packet readout. Fourth session running.
8. **`ISS-13` link quality** — unchanged, still the largest open problem.
9. **`az` reads ~0.92 g at rest.** Check `MPU_ACCEL_RANGE` against `MPU_ACCEL_SCALE`.
10. **The gyro emits single-sample spikes** of 70–190 deg/s while stationary.
11. **The pose model has still never been checked in a browser.**
12. **No dashboard UI for the GEN4 commands** — `send_command` remains the only path, and
    064 made that gap wider: the panel cannot set the release mode and cannot see it.
13. **No wiki page for the GEN4 uplink grammar**, and **`wiki/issues.md` is behind on
    sessions 4 to 8.** `wiki/decisions/frontend.md:60` was corrected this session.
14. Field-laptop dry run (`ISS-12`). Replace the placeholder cylinder in `cylinderMesh()`.

## Blocked

- **The BME280 on the current flight unit.** Hardware. Blocks Next 4 outright.
- `ISS-13` — frequency coordination. Needs a clear frequency, not code.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not provisioned or dry-run.
- **OLED dead on the current flight unit.** `AUTO x1` vs `CMD x1` is invisible until the SD
  card is read, on both generations.

## New this session

*None of these are in `wiki/issues.md` yet. `ISS-17` and `ISS-18` were session 7's.*

**Fault 1 · an unconfirmed EJECT burst poisons the next one.** `fireEjectBurst()` has two
exits and only one records anything. On the early exit it sets `ejectConfirmed` and
`ejectConfirmedMs`; on the run-out-of-attempts exit it records nothing at all — not even that
a burst was sent — so `chuteBaseline` is left at a value `lastChute` has already passed. The
next EJECT skips the re-arm block (it is gated on `ejectConfirmed`), reaches
`lastChute > chuteBaseline` on its first iteration, prints `EJECT confirmed after 0
attempt(s)` and transmits nothing. The burst blocks ~1.4 s and eats the very packets that
would have confirmed it, so this is the ordinary outcome rather than a rare one.

**Fault 2 · `chute` means two different things depending on where the packet landed.**
`radioListenForEject()` collapses a whole 400 ms window into one bool; `holdUntilListening()`
counts per packet. Burst spacing (~351 ms) is under the window width by construction.

**Fault 3 · BME280 failure, and `baseAltitude` captured during it.** `t.pres` and `t.alt` are
separate I²C transactions (`Sensors.ino:231`, `:233`), which is why a packet can carry a sane
pressure beside an altitude wrong by 1.7 km. `baseAltitude` is read once at `Sensors.ino:126`
with no plausibility check and no re-read.

**Fault 4 · no `isnan`/`isfinite` guard exists anywhere in the flight firmware**, and the
apogee rule reads NaN comparisons in two opposite directions — it cannot arm, but once armed
it counts to `confirmN` and fires.

## Deferred by decision

- **A GEN3.2 packet bump for auto-eject visibility** — declined 2026-08-26, twice, and
  reaffirmed by 064. With `ul > 0` the ground cannot distinguish an automatic release from a
  commanded one; only `chute` rising with `ul = 0` proves auto. The SD `#` config lines
  (now carrying `repeat=`) stay the only truthful record of vehicle config.
- **2 Hz telemetry** — rejected (036). **Packing existing fields** — rejected 2026-08-20.
- **`vb` battery and `st` status bitmask** — deferred; `vb` is gated on hardware.
- **Ground station SD logging** — declined, it stays a pure pass-through.
- **Auto-eject stays one-shot per boot in both release modes** — settled in 064. Repeating it
  safely needs a re-arm *condition* (a fresh climb through `armAltM`), not an expiring latch;
  the descent condition stays true for the whole fall.
- **The bench servo pin stays at 18** while `CHUTE_PIN` is 3. Different chip, different board,
  documented at `ServoEjectTest.ino:27`.
- `ISS-15` SQLite — same stream as the raw log, dies with the same laptop.

## Notes for next session

- **Tests were not run this session.** Last recorded, in 064: **154 backend, 107 frontend**,
  plus `verify_gen3.py` 14/14. Session 8 after 064 was investigation only — no firmware,
  backend or frontend edit was made, and `arduino-cli` is still not on this machine.
- **The evidence for everything in 065 is `logs/raw/20260907-195122-serial.log` and
  `20260907-195326-serial.log`.** `logs/` is gitignored, so those two files are the only copy
  and they are not backed up. Consider keeping them before the directory is cleared.
- **The ground station is still running the `Sep  7 2026 19:48:44` build**, and it was not
  restarted between the two dashboard runs — which is *why* fault 1 was visible across them.
  The `[GCS] build` stamp from session 7 earned its place here.
- **`chute` reaching 7 in that log is not seven releases.** One operator EJECT moved it by 3.
  The counter means "eject packets received", modulo fault 2.
- **The GEN4 flight sketch still prints `MRC Flight Unit GEN3 booting`** and shows
  `MRC FLIGHT GEN3` on the OLED. Cosmetic, at `MRC_FlightUnit_GEN4.ino:109,116,142`, and
  actively confusing while debugging a flash. A vehicle reflash for a banner.
- **A build stamp exists on the ground station only.** The flight unit still has none — and
  this session it would have answered whether the vehicle restart was a brownout or a reflash.
- **Auto-eject bounds now live in five places** — `api.py`, the GEN4 ground station, the GEN4
  vehicle, `COMMANDS.md`, and `COMMANDS-QUICK.md`. `EJECT_REARM_MS`/`CHUTE_REARM_MS` is a
  sixth pair, and `lib/link.ts` a third copy of that one.
- Devlogs 060–065 this session.
