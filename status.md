# Status

**Updated** 2026-08-29 · end of session 7

## Now

**The GEN4 pair reached hardware for the first time, and it found two bugs the desk
could not.** Everything up to a second EJECT works on the bench; the re-arm path does
not, and the fix for it is written but **not yet flashed**.

Branch **`feature/apogee-trigger`**, four commits ahead of `main`:

```
b72de9e  Fix the CHUTE_PIN divergence and audit the command reference   (057)
fdecc00  Add a numeric packet readout, fed by the backend's field table (056)
d4235a6  Add COMMANDS.md, a full command reference                      (054)
089d0b9  Add GEN4: auto-eject trigger configurable over the uplink      (053)
```

`main` is still at `72c9768`. **The previous status.md said 089d0b9 was on `main`. It is
not** — it is on this branch, and so is everything after it. Nothing has merged since
session 5.

### What was confirmed on hardware

```
PING         -> ul rose               ✓
EJECT        -> servo threw           ✓
RESET:CHUTE  -> servo returned ARMED  ✓
EJECT        -> nothing               ✗
```

The first three are the first real proof that the GEN4 uplink works end to end. The
fourth is `ISS-17` below.

### Sessions 6 and 7, which never reached this file

**054** `COMMANDS.md` — every command, flags and traps, assembled from the argparse
definitions rather than the README. **055** the release mechanism became a servo in both
generations (`CHUTE_USE_SERVO 1`, 90°/160°), with `ServoEjectTest` committed as a bench
rig.

**056** the channels view gained a **numeric packet readout** — all 22 fields in wire
order, each at the precision the firmware transmits, with a change column and a `flat N`
marker for a channel that has stopped moving. The field table is generated from
`parser.FIELD_DOC` and delivered in the session message, so the frontend holds no copy
that can drift. **057** `CHUTE_PIN` was `D3` in GEN4 and `3` in GEN3 — reconciled to `3`,
now confirmed correct on hardware. `COMMANDS-QUICK.md` added.

**058, 059** the two bugs from the bench run. Both written, **neither flashed nor
committed.**

## Next

0. **Reflash the ground unit, then re-run `PING → EJECT → RESET:CHUTE → EJECT`.** This is
   the only gate that matters. `ISS-17` is fixed in the working tree and unproven.
1. **The reflash on 2026-08-29 did not take.** The Arduino IDE compiles its in-memory
   editor buffer, not the file on disk, so a sketch left open across an external edit
   uploads the old code silently. A `[GCS] build <date> <time>` stamp was added to the
   ground station boot to make this unambiguous — `__DATE__`/`__TIME__` come from the
   compiler and cannot survive a stale buffer. **Close the IDE completely before
   reopening and uploading.** If the stamp does not move, nothing was rebuilt.
2. **Auto-eject has still never been tested, and needs no reflash.** It is entirely
   vehicle-side and the vehicle firmware is correct. `SET:ARM:5.0`, `SET:DROP:2.0`, then
   a stairwell. Expect `chute` 0→1 with **`ul` unchanged** — that pair is the only
   ground-side proof a release was automatic.
3. **`AUTO_EJECT_CONFIRM_N` still needs a real descent rate.** Three cycles is three
   seconds; at 30 m/s that is 90 m, deploying at 60 m from a 150 m apogee. Unchanged
   since session 5 and unanswerable until something falls.
4. **Nothing pins the apogee state machine.** Sessions 5 and 7 both used throwaway Python
   traces, and both caught real errors — the RESET re-arming behaviour, and the eject
   latch interaction. That is now twice; the trace should be committed.
5. **`backend/devtools/mock_source.py` still emits GEN2** — no `$MRC`, no CRC. It cannot
   exercise auto-eject, GEN4, or the packet readout. Third session running.
6. **`ISS-13` link quality** — unchanged, still the largest open problem.
7. **`az` reads ~0.92 g at rest.** Check `MPU_ACCEL_RANGE` against `MPU_ACCEL_SCALE`.
8. **The gyro emits single-sample spikes** of 70–190 deg/s while stationary.
9. **The pose model has still never been checked in a browser.**
10. **No dashboard UI for the GEN4 commands** — `send_command` remains the only path.
11. **No wiki page for the GEN4 uplink grammar.**
12. **`wiki/decisions/frontend.md:60` is now false** — it still says `lib/link.ts` renders
    "Deployed". It does not, as of 059. `wiki/issues.md` is behind on sessions 4 to 7.
13. Field-laptop dry run (`ISS-12`). Replace the placeholder cylinder in `cylinderMesh()`.

## Blocked

- `ISS-17` — **the eject re-arm.** Fixed in the working tree, blocked on a ground reflash.
- `ISS-13` — frequency coordination. Needs a clear frequency, not code.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not provisioned or dry-run.
- **OLED dead on the current flight unit.** `AUTO x1` vs `CMD x1` is therefore invisible
  until the SD card is read, on both generations — session 5's note that this was a GEN4
  addition was wrong, GEN3 has had it since 052.

## New this session

*Neither is in `wiki/issues.md` yet — that file is behind on sessions 4 to 7 and is
edited deliberately, not in passing. `ISS-16` was already taken (Replay not built).*

**`ISS-17` · RESET:CHUTE could not re-arm the chute over the air.** Two latches on the
GROUND station — `ejectConfirmed`, and the eject burst's `lastChute >= 1` early exit —
and a reset cleared neither. The vehicle was always correct: it received the command and
cleared its fire latch, which is why the servo returned to ARMED. The ground station then
refused to send the EJECT that would exercise it. So the feature `Apogee.ino:41` calls
"the ONLY way to re-run a deployment test on a sealed unit" has never worked since it was
written. Fixed in 058 by baselining the burst the way `fireConfigBurst` already baselines
`ul`, and clearing the latches only on a confirmed reset. **Ground unit reflash only — the
vehicle needs no change.**

**`ISS-18` · the dashboard rendered a fired chute as "Unknown".** `chutePresentation`
tested `chute === 1`, so a count of 2 fell through to Unknown with the Arm and Eject
controls restored beneath it. Not hypothetical: `20260829-125122` reaches 2 and
`20260827-120125` reaches 3, both already in `logs/raw/`. Fixed in 059 by implementing
rule S8 properly — `Commanded ×N`, never "Deployed", with six tests where there were
none. This closes what session 5 called "the oldest debt in the tree", and it was two
bugs rather than the one line recorded.

## Deferred by decision

- **A GEN3.2 packet bump for auto-eject visibility** — declined 2026-08-26, twice. Stands.
  Session 7 reconfirmed the cost: with `ul > 0`, the ground cannot distinguish an
  automatic release from a commanded one. Only `chute ≥ 1` with `ul = 0` proves auto.
- **2 Hz telemetry** — rejected (036). **Packing existing fields** — rejected 2026-08-20.
- **`vb` battery and `st` status bitmask** — deferred; `vb` is gated on hardware.
- **Ground station SD logging** — declined, it stays a pure pass-through.
- **The bench servo pin stays at 18** while `CHUTE_PIN` is 3. Deliberate and documented at
  `ServoEjectTest.ino:27` — different chip, different board. Flagged as drift twice in one
  session by reading the value without the comment above it.
- `ISS-15` SQLite — same stream as the raw log, dies with the same laptop.

## Notes for next session

- **245 tests: 145 backend, 100 frontend**, plus `verify_gen3.py` 14/14.
- **Ten files are uncommitted** — all of 058 and 059, plus this file. Tests green,
  typecheck clean.
- **`chute` will reach 2 on a successful re-arm test**, and that is correct — the counter
  means "releases commanded", and two were. The dashboard now says so.
- **The GEN4 flight sketch still prints `MRC Flight Unit GEN3 booting`** and shows
  `MRC FLIGHT GEN3` on the OLED. Cosmetic, in `MRC_FlightUnit_GEN4.ino:109,116,142`, and
  actively confusing while debugging a flash. Not fixed — it would mean a vehicle reflash
  for a banner.
- **A build stamp exists on the ground station only.** The flight unit has none.
- **`COMMANDS.md` was audited against the code in 057** and carried two false statements
  about `RESET:CHUTE` — the most dangerous command in the system, four days after being
  written. The check is cheap: capture `--help`, import the command tables, compare.
  Re-run it whenever the grammar moves.
- **Auto-eject bounds now live in four places** — `api.py`, the GEN4 ground station, the
  GEN4 vehicle, and `COMMANDS.md`.
- Devlogs 056–059 this session.
