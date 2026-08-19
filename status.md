# Status

**Updated** 2026-08-20 · end of session 4

## Now

**The uplink works. It was confirmed on hardware tonight, and there is a log of it.**

`logs/raw/20260820-015822-serial.log`, a GEN3.1 session from 01:58:

```
[GCS] PING queued  x3        ->  ul went 0 -> 1 -> 2 -> 3
[GCS] EJECT armed
[GCS] EJECT attempt 1/5, 2/5, 3/5
[GCS] EJECT confirmed after 3 attempt(s)   ->  chute 0 -> 2, ul 3 -> 5
```

That single block closes the investigation that ran from entry 039 to 044. The timing
diagnosis was right: the ground station had been transmitting into the vehicle's deaf
period, and a phase-independent burst lands. It landed on attempt 3 of 5, inside the
guarantee the burst was sized for.

**`ISS-02` is resolved** and should be rewritten to say so rather than closed silently —
it has been wrong in three different ways across the project.

Four things proved themselves in the same log, none of which had ever been observed:

- **`ul` is the witness entry 037 asked for.** Three PINGs incremented it *before* EJECT
  was attempted — the non-destructive test finally works, from the dashboard, sealed.
- **`parseChute()` read `chute=2` correctly** out of a 20-field packet. This is the
  index-forward rewrite from entry 048; the old walk-back version would have read `fixq`.
- **The GPS staleness fix works.** `fixq` went 1 -> 0 mid-session and `lat/lng` went to
  `0.00000` with it, instead of freezing on a position the vehicle no longer had.
- **HDOP is arriving** — 15.3 early, 25.5 later. Both terrible, and worth knowing.

Sessions 1-3 built the dashboard. Session 4 found out whether it was telling the truth.

## Next

0. **Replay already handles `.log` — nothing to build.** Verified tonight against the live
   capture: 302 frames, 0 rejected, loss stats computed. There is no extension filter
   anywhere in that path; `run_replay.py`'s examples just led with `.CSV` and left the
   wrong impression. Corrected in its docstring.
   `python -m devtools.run_replay 20260820-015822-serial.log --speed 20`
1. **Link quality is now the largest open problem.** That session shows **53% cumulative
   loss** and RSSI at **-109 dBm** early on, recovering to -24. 32 `RX error code -7`
   (CRC) besides. Rolling loss ended at 0%, so it is not a constant fault — it is
   position or interference. This is `ISS-13` arriving in measured form.
2. **`az` reads ~0.92 g at rest, not 1.00.** An 8% scale or bias error, consistent across
   every log. Check `MPU_ACCEL_RANGE` (0x10, +/-8 g) against `MPU_ACCEL_SCALE` (4096.0)
   in the datasheet. Everything derived from attitude inherits this.
3. **The gyro emits single-sample spikes** of 70-190 deg/s while stationary. Debounced on
   the dashboard (entry 045), still written to SD as fact. Suspect I2C reads.
4. **The pose model has still never been checked in a browser.** Three sign errors have
   been fixed in `viewTransform` across entries 038, 041 and 045, and every one was found
   on hardware rather than by a test. Hard-reload first.
5. **Step 6 — `lib/link.ts` still renders the chute as "Deployed"**, which S8 forbids. Two
   lines, recorded in four places now, still unbuilt. The oldest debt in the tree.
6. **Step 4 — GEN3 mock with injectable `seq` gaps and CRC failures.** Now needs to emit
   GEN3.1. Still the only way to test the loss display before launch day.
7. Field-laptop dry run (`ISS-12`). The checklist exists; running it does not.
8. Replace the placeholder cylinder in `cylinderMesh()` — Aiman's.

## Blocked

- `ISS-13` — **frequency coordination**, and now with numbers behind it: see Next item 1.
  Unblocking needs a clear frequency, not a code change.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not provisioned or dry-run.
- **OLED dead on the current flight unit.** No longer blocking anything: `ul` in telemetry
  supersedes every check that used to need the screen.

## Deferred by decision

- **2 Hz telemetry** — rejected (entry 036). The cycle budget does not close.
- **Packing existing fields** — rejected 2026-08-20. ~8 chars of 105, about 5% airtime,
  not worth a format bump on its own. Would only be worth doing alongside another one.
- **`vb` battery field and `st` status bitmask** — deferred in favour of `ul`/`hdop`/`fixq`.
  `vb` is gated on hardware: the Heltec V3's battery ADC is GPIO1, which `Config.h`
  already uses for `I2C_SDA`.
- **Ground station SD logging** — declined. It stays a pure pass-through.
- `ISS-15` SQLite — unbuilt, and would not be a third backup: same stream as the raw log,
  dies with the same laptop.

## Notes for next session

- **GEN3.1 is flashed on both units.** Do not flash one without the other — a GEN3.1
  vehicle with a GEN3.0 ground station is the false-confirmation case from entry 048.
- **Raw logs are self-describing now.** `<log>.meta.json` carries the full wire contract
  as JSON: fields in wire order with units, formats, sentinels. Zip it against a split
  line and every value has a name. Entry 050.
- **`logs/raw/` was cleared at some point tonight** — the 19 August captures are gone. The
  GEN3.0 corpus survives in `backend/tests/fixtures/` (FLIGHT21/22.CSV) because it is
  committed; `logs/` is gitignored and nothing else is backed up.
- **Neither firmware sketch has been compiled here** — no Arduino toolchain. Aiman builds
  and flashes.
- **`wiki/issues.md` is behind.** `ISS-02` resolved, `ISS-14` resolved (swapped TX/RX,
  entry 042), `ISS-08` long resolved. None reflect tonight.
- `wiki/source/hardware/flight-unit.md:60` describes the GEN1/GEN2 SD format. Stale and
  deliberately not corrected — `source/` is external fact.
- **214 tests: 141 backend, 73 frontend.** Plus `firmware/tests/verify_gen3.py`, 14/14.
- Devlog 040-050 this session. Everything committed except the `run_replay.py` docstring
  fix, which is in the working tree.
