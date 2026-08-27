# Status

**Updated** 2026-08-26 · end of session 5

## Now

**The vehicle can deploy its own parachute, and the trigger can be retuned without
reflashing it.** Two commits, both on `main`:

```
72c9768  Add auto-eject on descent, decouple ul from chute      (devlog 052)
089d0b9  Add GEN4: auto-eject trigger configurable over uplink  (devlog 053)
```

**GEN3 gained auto-eject.** `Apogee.ino` tracks the highest altitude seen and fires
when the vehicle has descended `AUTO_EJECT_DROP_M` below it for `AUTO_EJECT_CONFIRM_N`
consecutive cycles — defaults **30 m arm · 10 m drop · 3 cycles**. The arming floor is
the whole safety argument: altitude is relative to boot, so without it pad drift sets a
false apogee and any dip below it is a live trigger a metre off the ground.

**`ul` had to be decoupled from `chute`.** It was computed at the packet as
`pingCount + chuteCommands`, an identity that only held while the uplink was the sole
thing that could move the chute counter. A self-releasing vehicle would have reported
`ul = 1` having never heard the ground station — inverting the one field that exists to
prove reception. It is counted at the radio now.

**`chute` therefore means "releases commanded, from either path"**, not "eject commands
received", and is no longer a measure of uplink quality. `chute ≥ 1` with `ul = 0` is now
a valid state: an automatic release on a flight where the ground was never heard.

**GEN4 makes the trigger configurable in flight** — `SET:DROP` `SET:CYCLES` `SET:ARM`
`SET:AUTO`, plus `RESET` and `RESET:CHUTE`. New sketch folders; **GEN3 is untouched and
stays flashable**, because it is the only pair proven on hardware (entry 051). The packet
stays GEN3.1 byte for byte, so the parser, contract, dashboard and all 214 tests needed
nothing.

Session 4 found out whether the dashboard was telling the truth. Session 5 gave the
vehicle a way to save itself, and then a way to be argued with about how.

## Next

0. **Nothing built this session has been compiled.** No Arduino toolchain here — Aiman
   builds and flashes. That is the first gate on everything below.
1. **`AUTO_EJECT_CONFIRM_N` needs a real descent rate.** Traced against the wiki's V7
   profile it fires 23 m below a 150 m apogee. At genuine freefall — 30 m/s — the same
   three cycles cost **90 m**, deploying at 60 m. Three cycles is three seconds, and
   three seconds is cheap only while the vehicle falls slowly. One-character change;
   the right value is unknown until something has actually fallen.
2. **Nothing pins the apogee state machine.** `verify_gen3.py` pins the packet; the
   trigger was checked by a throwaway Python trace, not a committed test. That trace
   caught a real error (see item 3), which is the argument for keeping one.
3. **`RESET` re-bases the trigger, it does not cancel it.** Arming tests altitude above
   BOOT, not a climb, so a vehicle still high when RESET arrives re-arms on the next
   cycle and fires lower. `SET:AUTO:0` is the cancel. A power cycle *does* prevent
   re-arming — but only because it re-zeroes the barometric baseline too.
4. **`backend/devtools/mock_source.py` still emits GEN2** — no `$MRC`, no CRC, `CHUTE:n`.
   It now also cannot exercise auto-eject or GEN4 at all. This was Next item 6 last
   session and has grown a second reason.
5. **`ISS-13` link quality** — 53% cumulative loss and RSSI -109 dBm early in the
   20 August session, recovering to -24. Still the largest open problem.
6. **`az` reads ~0.92 g at rest**, not 1.00. Check `MPU_ACCEL_RANGE` (0x10) against
   `MPU_ACCEL_SCALE` (4096.0). Everything derived from attitude inherits it.
7. **The gyro emits single-sample spikes** of 70-190 deg/s while stationary. Debounced
   on the dashboard, still written to SD as fact.
8. **The pose model has still never been checked in a browser.** Three sign errors
   fixed in `viewTransform`, every one found on hardware rather than by a test.
9. **`lib/link.ts` still renders the chute as "Deployed"**, which S8 forbids — and the
   word is now wronger than it was, since a release may be automatic. Two lines,
   recorded in five places, still unbuilt. The oldest debt in the tree.
10. **No wiki page for the GEN4 uplink grammar.** The wiki records what is true, and
    the command set currently exists only in firmware headers and devlog 053.
11. **No dashboard UI for GEN4 commands** — `python -m devtools.send_command <cmd>`
    from a second terminal is the only path. Needs `websockets` in the venv.
12. Field-laptop dry run (`ISS-12`). The checklist exists; running it does not.
13. Replace the placeholder cylinder in `cylinderMesh()` — Aiman's.

## Blocked

- `ISS-13` — **frequency coordination**. Unblocking needs a clear frequency, not code.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not provisioned or dry-run.
- **OLED dead on the current flight unit.** Not blocking: `ul` supersedes every check
  that needed the screen. Note GEN4 put the auto/commanded distinction on that glass —
  `AUTO x1` vs `CMD x1` — so on this unit it is invisible until the SD card is read.

## Deferred by decision

- **A GEN3.2 packet bump for auto-eject visibility** — declined 2026-08-26, twice. The
  consequence is accepted and worth restating: `ul` rising proves the vehicle received
  A command, never which one or what value it applied. A sealed, flying vehicle cannot
  be asked what it is configured to do. Bench USB and the SD card `#` lines answer it
  afterwards.
- **2 Hz telemetry** — rejected (entry 036). The cycle budget does not close.
- **Packing existing fields** — rejected 2026-08-20. ~5% airtime, not worth a format
  bump alone.
- **`vb` battery field and `st` status bitmask** — deferred. `vb` is gated on hardware:
  the Heltec V3's battery ADC is GPIO1, which `Config.h` already uses for `I2C_SDA`.
- **Ground station SD logging** — declined. It stays a pure pass-through.
- `ISS-15` SQLite — same stream as the raw log, dies with the same laptop.

## Notes for next session

- **GEN4 must be flashed to BOTH units** for `SET`/`RESET` to work. Unlike the
  GEN3.0/GEN3.1 split, a mismatched GEN4 pair degrades safely in both directions: a
  GEN3 vehicle ignores `SET` and never moves `ul`, so a GEN4 ground station reports
  failure loudly rather than pretending.
- **After an automatic release, the ground station's EJECT button transmits nothing.**
  `fireEjectBurst()` checks `lastChute >= 1` first and reports `EJECT confirmed after 0
  attempt(s)` without sending. True about the chute; not evidence the uplink works.
  Use PING for that. Applies to GEN3 and GEN4 alike; left unfixed deliberately.
- **The SD header was stale.** It described GEN3.0's 17 fields for as long as GEN3.1 was
  flying, so every card written in between misdescribed itself by three columns. Fixed
  in both generations. It is a hand-written copy of the format and nothing in the build
  will catch it drifting again.
- **GEN4 writes `#` config lines to the SD card** mid-file whenever the trigger is
  reconfigured. Verified they parse as status, not as rejected frames.
- **Auto-eject bounds live in three places** — `api.py`, the GEN4 ground station, the
  GEN4 vehicle. Change them together; all three files say so.
- **`wiki/issues.md` is still behind.** `ISS-02` resolved, `ISS-14` resolved (entry 042),
  `ISS-08` long resolved. None of it reflects session 4 or 5.
- `wiki/source/hardware/flight-unit.md:60` describes the GEN1/GEN2 SD format. Stale and
  deliberately not corrected — `source/` is external fact.
- **214 tests: 141 backend, 73 frontend.** Plus `firmware/tests/verify_gen3.py`, 14/14.
  Unchanged this session — the packet never moved.
- Devlog 052-053 this session. Working tree clean; both commits on `main`.
