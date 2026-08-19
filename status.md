# Status

**Updated** 2026-08-19 · end of session 3

## Now

**The GEN3 dashboard is functionally complete and has been run against real hardware.**

Steps 1, 2, 3 and 6-in-part of the seven-step plan in
`wiki/decisions/dashboard-gen3-plan.md` are done, plus replay, which was not in the plan:

- **Parser** pinned by 1013 real packets from two SD captures, promoted into
  `backend/tests/fixtures/` so the suite runs on any clone
- **Replay source** — a capture plays through the real pipeline at the vehicle's own
  cadence (`ISS-16` resolved)
- **Envelope** carries `seq`, `vehicle_ms`, `crc_ok` and `link`
- **`linkstats.py`** — real packet loss, rolling and session, backend-owned (S1–S5)
- **Channels view** — every numeric field charted, three-axis IMU
- **3D pose model** on plain canvas, eased, 4 kB
- **Pre-launch checklist** — `wiki/decisions/pre-launch-checklist.md`

`ISS-08` and `ISS-16` are resolved. 180 tests pass: 118 backend, 62 frontend.

**Two defects found by putting it on hardware, and both were serious.**

The dashboard sent `EJECT` where the ground station compares against `CMD:EJECT`, so the
command never left the PC correctly. It survived because the mock matched the dashboard's
spelling instead of the firmware's. Fixed, `PING` exposed, tests now read `Config.h`
directly (devlog 033) — and the hardware log confirms the fix works.

The 3D model rendered lying down when the CanSat stood upright: `viewTransform` had its
elevation terms swapped, projecting the body's long axis into screen depth instead of
screen height. Fifteen passing tests missed it because every one checked the body rotation
and none checked the camera (devlog 038).

**And one still open, which is the most important thing in this file — see Next item 0.**

**Nothing is committed.** Devlog entries 029–036 are all in the working tree.

## Next

0. **The uplink has never worked over the air.** EJECT was attempted 3 × 15 times on
   hardware and the vehicle never acknowledged (devlog 039). The command reaches the
   ground station — `[GCS] EJECT armed` proves entry 033's fix works — but the ground
   station transmits the instant it *receives* a packet, which is ~250 ms after the
   vehicle's listen window has **closed**. `Uplink.ino:84` assumes the opposite.
   **First action next session:** tether the flight unit's USB at 115200, press Ping,
   and look for `[FLT] PING received`. Absent → timing, as diagnosed. Present → the fault
   is downstream in `chuteFire()` or the servo. Nothing else should be built until this
   is known — a launch with an unverified uplink has no recovery command.
1. **Frequency change** — one line in each `Config.h`. Free, no range cost, and worth
   about four times the data that doubling the sample rate would have been. Needs a
   frequency to move to (`ISS-13`).
2. **Step 4 — GEN3 mock with injectable `seq` gaps and CRC failures.** No non-zero loss
   figure has ever appeared on screen; both captures are clean and the mock is GEN2. This
   is the difference between testing the loss display now and testing it on launch day.
3. **Add a `ul` field to the telemetry packet** (devlog 037). Uplink health currently
   exists only on the vehicle's OLED, so it cannot be checked once the unit is sealed, or
   during flight, by any route — and the OLED on the current set is dead. Until then the
   only witness is the flight unit's USB serial: `[FLT] PING received, count N` at 115200.
   Costs a packet format change: 17 vehicle fields become 18, and the parser must accept
   both, since the regression corpus is 17-field.
4. **Step 6 — `lib/link.ts` still renders the chute as "Deployed"**, which S8 forbids. Two
   lines, recorded in three places, still unbuilt.
5. **Field-laptop dry run** (`ISS-12`). The checklist exists; running it does not.
6. Replace the placeholder cylinder in `cylinderMesh()` — Aiman's, and nothing else in the
   renderer knows what shape it draws.

## Blocked

- `ISS-13` — **frequency coordination.** Still the largest launch-day risk. Roughly 75% of
  packets lost to collisions on a shared 919.0 MHz. Unblocking it needs a clear frequency,
  not a code change.
- `ISS-06` — competition requirements unknown; `wiki/source/competition/` still empty.
- `ISS-12` — field laptop not yet provisioned or dry-run.
- `ISS-14` — GPS unpowered. Set aside by choice. Starts with a multimeter on the module's
  VCC, then `firmware/tools/GPS_Minimal`.
- **OLED dead on the current flight unit.** Other sets still in development. Not blocking
  — the USB serial route covers bench checks — but it is what surfaced devlog 037.
- `ISS-02` — **reopened in effect.** The ground unit firmware exists and transmits, but no
  uplink has been shown to reach a vehicle. Not restated in `issues.md`; devlog 039 holds
  the detail, and the diagnosis should be confirmed before the issue is rewritten.

## Deferred by decision

- **2 Hz telemetry** — rejected (devlog 036). The cycle budget is 646 ms of 1000; at 500 ms
  it overruns by 146. Closing that costs either the parachute command's listen window or
  3 dB of range. Not worth it.
- **Dashboard rate-independence** — dropped with 2 Hz. At a fixed 1 Hz those constants are
  correct, not fragile.
- **Ground station SD logging** — declined. It remains a pure pass-through.
- `ISS-15` SQLite — still unbuilt. Worth knowing it would **not** be a third backup: it
  would be fed from the same stream as the raw log and die with the same laptop.

## Notes for next session

- **Only the CanSat's SD card is an independent record.** RSSI and SNR exist nowhere else
  but the laptop's raw log — the vehicle never knows them.
- **Neither firmware sketch has been compiled here** — no Arduino toolchain. Aiman builds
  and flashes.
- `logs/` is gitignored and where real flight logs get archived is still undecided.
- `frontend/tsconfig.tsbuildinfo` is tracked and churns on every build. `*.tsbuildinfo` is
  now in `.gitignore`, but it needs `git rm --cached frontend/tsconfig.tsbuildinfo` to take
  effect.
- `wiki/source/hardware/flight-unit.md:60` describes the GEN1/GEN2 SD format. GEN3 writes
  the framed `$MRC,…` form. Stale, deliberately not corrected — `source/` is external fact.
