# Status

**Updated** 2026-08-19 · end of session 2

## Now

**GEN3 firmware flies on real hardware. The dashboard is mid-conversion to read it.**

**Firmware — working.** Both units written, flashed and running. Confirmed on hardware:

- CRC agreement — 5/5 over the air, 85/85 from the SD card
- Cadence exact — 1000.00 ms per packet across 199 s, zero overruns
- Barometer sound — −0.993 pressure/altitude correlation, predicted noise matches observed
- IMU healthy — −47 mg bias, inside datasheet tolerance
- SD logging clean — no corrupt writes, pandas recipe verified against the real file

**One hardware fault: the GPS has never worked** (`ISS-14`). Deliberately set aside.

**Dashboard — step 1 of 7 done.** The GEN3 parser is written and verified against real
hardware data. Steps 2–7 are unstarted. Plan in
`wiki/decisions/dashboard-gen3-plan.md`, with eight numbered design rules (S1–S8) that the
remaining work implements.

The live view still runs against the GEN2 mock and is unaffected.

## Next

1. **Pin the GEN3 parser down with tests.** Verified ad hoc, not yet a regression suite.
   `logs/raw/FLIGHT22.CSV` is the corpus — 85 real packets with valid checksums.
2. **`linkstats.py`** — pure module owning `seq` tracking, gap arithmetic, restart
   detection and the rolling loss window. Rules S1–S5. No I/O, so the awkward cases
   (restart, duplicate, mid-flight start) are unit-testable without hardware.
3. **Pipeline and envelope** — carry `seq`, `vehicle_ms`, `crc_ok`, `link`.
4. **Mock source → GEN3**, with injectable `seq` gaps and CRC failures. Otherwise the loss
   display gets tested for the first time on launch day.
5. **Frontend** — nullable `seq`/`ms` so GEN1/GEN2 still render; panels per S5–S8,
   including relabelling the chute state from **"Deployed"** to **"Commanded"**.

## Blocked

- `ISS-13` — **frequency coordination.** Still the largest launch-day risk and entirely
  external. Three other teams on 919.0 MHz costs 75% of packets and no firmware change
  helps. Needs an answer from the organisers.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-14` — GPS unpowered. Set aside by choice, not blocked on us. Resolving it starts
  with a multimeter on the module's VCC, then `firmware/tools/GPS_Minimal`.

## Deferred by decision

- `ISS-15` SQLite store · `ISS-16` replay — both out of scope for the GEN3 dashboard work.
  Replay is unusually cheap now: the parser already reads SD packets, so a `file_source`
  would replay a real flight with no conversion.

## Notes for next session

- **Neither firmware sketch has been compiled here** — no Arduino toolchain. Aiman builds
  and flashes; that has worked so far.
- `logs/` is gitignored, so `FLIGHT22.CSV` exists only on this machine. Fine for a bench
  sample; worth deciding where real flight logs get archived before there is one.
