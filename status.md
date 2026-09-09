# Status

**Updated** 2026-09-10 · end of session 9

## Now

**Auto-eject fired on hardware for the first time, and all four faults from devlog 065 are
closed in code.** Eight sessions of "never tested" ended in
`logs/raw/20260910-032536-serial.log`: three automatic releases, each with `chute` rising
and `ul` unchanged, plus hardware confirmation of both 067 and 070 in the same log.

Branch **`feature/dashboard-update`**, 15 commits ahead of `main` (`72c9768`, 2026-08-26 —
`main` has not moved in two weeks).

**A toolchain now exists.** The ground station in that log carries
`[GCS] build Sep 10 2026 03:23:50`, so firmware in this repo has been compiled and flashed
for the first time. **CLAUDE.md's first trap — "`arduino-cli` is not installed, no firmware
change has ever been compiled here" — is retired.** Corrected there this session.

### What was confirmed on hardware

```
seq=54   chute 0->1   ul 6->6    AUTO-EJECT, apogee 1.7 m, released at 0.6 m
seq=19   chute 0->1   ul 4->4    AUTO-EJECT, apogee 1.9 m, released at 0.2 m
seq=28   chute 0->1   ul 1->1    AUTO-EJECT
seq=181  chute 1->2   ul 6->8    commanded - chute +1 against ul +2, which is 067 working
[GCS] EJECT confirmed, chute 0 -> 2                       070 working
[GCS] vehicle restarted - ... pending EJECT confirmation cleared    070's reboot handler
```

⚠ **Read 079 before calling it tested.** It fired **by hand, at bench thresholds**
(`ARM 0.5 / DROP 0.5`), on a descent roughly thirty times slower than a real one. The
flight configuration has never been flashed or run.

### Session 9, by entry

**066** the day's logs re-read: the GPS *does* reach a fix (1,925 packets, HDOP 0.7, 19
sats), which contradicts the ISS-14 rewrite committed the same day — and session
`20260909-011251` turns out to carry **two flight units on one channel** with no identifier
in the packet to separate them. **067** `chute` counts releases, not packets. **068** the
eject control and the attitude reliability signalling both left the dashboard. **069** the
release fires on receipt rather than at window close, and the sensor-read gap stopped
dropping commands. **070** EJECT confirmation moved to `radioPoll()`, closing 065 fault 1.
**071** the trigger got its own 8 Hz clock and an `isfinite` guard, closing fault 4, and
`baseAltitude` got a plausibility band, closing fault 3's code half. **072–076, 078** the
drop threshold walked 2.0 → 1.0 → 0.2 → 0.5 → 2.0 and the `SET` bounds went down and back.
**077, 078** the pose model became a three-finned rocket. **079** the bench log, read.

## Next

0. **Flash both units and confirm the flight configuration runs.** Being done as this was
   written; the result is not recorded. Expect on the flight unit: `altitude zeroed at X m
   (attempt 1)`, **no** `NOT FLIGHT SAFE` banner, **zero** `cycle overran` lines across ten
   minutes, and no return of the `20260909-024620` signature (humidity pinned at 100, all
   six MPU axes exactly `0.000`) now that I²C traffic is roughly tripled by 071. **Restart
   the backend too** — `api.py`'s bounds changed and it is Python, not firmware.
1. **Auto-eject has never run at flight thresholds.** 079 was `ARM 0.5 / DROP 0.5` at
   walking pace. `ARM 30 / DROP 2.0` needs real altitude, which needs a stairwell or a
   flight, and **the desk test is no longer reachable over the uplink** — 078 put the
   bounds back, so `SET:ARM:0.5` is refused at both ends. Another bench run means editing
   `MRC_FlightUnit_GEN4/Config.h`, `MRC_GroundStation_GEN4/Config.h` and `api.py` together.
   0.5 m is the floor when that day comes; below it the barometer's running-maximum drift
   alone fires the rule (074, 075).
2. **`ISS-14` should be re-examined and probably closed.** 066 found the GPS reaching valid
   fixes in six sessions — real coordinates, HDOP down to 0.7, up to 19 satellites. The
   issue's own 2026-09-09 rewrite says "not one valid fix", and that claim is not supported
   by the full set of logs.
3. **Two vehicles on one channel needs an issue of its own.** Next free is `ISS-19`. The
   packet carries no vehicle identifier, so a second unit on `919.0 / 0xAA / MRC` is merged
   into one timeline — `alt`, `chute` and `ul` included. It also puts a hole in "`chute`
   rising with `ul` unchanged proves automatic", which assumes both fields came from the
   same vehicle.
4. **`wiki/issues.md` is behind sessions 4 to 9**, and `ISS-17`/`ISS-18` from session 7 were
   never entered at all. Nothing from 066–079 is in it.
5. **`AUTO_EJECT_CONFIRM_N` still has no real descent rate behind it.** Three samples is
   250 ms now rather than 2 s, so it costs far less than it did — but the 14–16 m figure in
   072 is drag-free arithmetic and nothing has fallen.
6. **`backend/devtools/mock_source.py` still emits GEN2** — no `$MRC`, no CRC. Fifth
   session. It cannot exercise auto-eject, GEN4 or the packet readout.
7. **`ISS-13` link quality.** The 2026-09-10 log has 15 consecutive `RX error code -7` and
   two `SET:ARM` bursts that failed with `ul did not rise` while `ul` was demonstrably
   moving. Still the largest open problem.
8. **The pose model has still never been checked in a browser**, and 077 just replaced the
   mesh — so this matters more than it did. 038, 041 and 045 were each a rendering fault
   found on hardware rather than in a test, and the camera alone has shipped three sign
   errors. `npm run dev` and look at it.
9. **No dashboard UI for the GEN4 commands.** 068 widened this deliberately: the dashboard
   now sends `PING` and nothing else. Decide whether that is the flight shape, and if so
   put "who holds the terminal with EJECT" on the pre-launch checklist.
10. **No wiki page for the GEN4 uplink grammar.**
11. `az` and the gyro spikes — unexamined this session. Field laptop dry run (`ISS-12`).
12. **The GEN4 flight sketch still prints `GEN3`** on boot and on the OLED.

## Blocked

- `ISS-13` — frequency coordination. Needs a clear frequency, not code. Now with an
  in-house instance: two of our own units were on one channel.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not provisioned or dry-run.
- **OLED dead on the current flight unit.**

## New this session

*None of these are in `wiki/issues.md`.*

**Two vehicles on one channel.** `20260909-011251` interleaves two independent streams
packet by packet — two sequence counters, two uptimes ~60 s apart, both at 1 Hz, distinct
RSSI. One had a 14-satellite fix, the other none. See Next 3.

**The `EJECT confirmed, chute X -> Y` line can read as two releases.** In 079's log it
printed `chute 0 -> 2` because the baseline was 0 while an automatic release had already
moved the counter to 1. Both numbers are true; the message invites misreading. The `X -> Y`
form was chosen in 070 because attempt counts stopped being knowable.

**`SET` bursts failed twice while `ul` was rising.** Link quality, but worth knowing the
failure mode looks like a vehicle that cannot hear.

## Deferred by decision

- **A GEN3.2 packet bump** — declined three times now (2026-08-26 twice, reaffirmed by
  064). 066 raised a new argument for it — a vehicle identifier — which is a *different*
  ground and has not been heard.
- **A FreeRTOS task for the trigger** — declined in 071. The barometer is the rate limit at
  ~10 Hz, not the CPU, so a second core would sample no faster while adding an I²C mutex
  and making `chuteFire()` reentrant.
- **`FILTER_X16` on the BME280** — would cut pressure noise fourfold and make a 0.2 m
  threshold viable, at ~3.75 s of settling lag. Bench-only if ever.
- **2 Hz telemetry** — rejected (036). 071 is not that: telemetry stayed at 1 Hz.
- **`vb` battery and `st` status bitmask** — deferred. **Ground station SD logging** —
  declined. **`ISS-15` SQLite** — same stream as the raw log.

## Notes for next session

- **Backend 190 (+1 strict `xfail`), frontend 122, `verify_gen3.py` 14/14.** All passing.
  The `xfail` is deliberate and strict: `chute` cannot say *why* it rose, so an auto-eject
  release is indistinguishable from a commanded one. If it ever starts passing, someone
  fixed it.
- **Two committed models now exist** where there were none: `test_eject_state_machine.py`
  and `test_apogee_trigger.py`. They are MODELS of firmware, not the firmware, and their
  docstrings say so. `status.md` Next 6 asked for the apogee one for three sessions.
- **A test that encodes a value rather than the reason for it broke three times today**
  (072, 075, 078). The rule that came out of it: *a constant belongs in the assertion only
  when changing it should fail the test.* `USABLE_DROP_FLOOR_M` is derived from sensor
  noise in one place and everything else refers to it.
- **072–079 are uncommitted** — 8 devlog entries and 11 modified files.
- **Flight configuration, for reference:** `ARM` 30 m · `DROP` 2.0 m · `CYCLES` 3 samples ·
  `SAMPLE_MS` 125. Bounds `DROP` 2.0–100, `ARM` 5.0–200, `CYCLES` 1–10.
- **`logs/raw/20260910-032536-serial.log` is the evidence for 079** and `logs/` is
  gitignored. It is the only record that auto-eject has ever fired. Consider keeping it.
- Three commits on this branch are not project work — `c8a7a37` "Test file for gh desktop",
  `5639788` and `ceb0864` (a create/delete pair). Squash candidates if `main` should read
  cleanly; entirely optional.
