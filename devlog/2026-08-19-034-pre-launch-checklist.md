# 034 · Pre-launch checklist

**Date** 2026-08-19
**Type** change
**Refs** ISS-12, ISS-13, ISS-14, ISS-06

## What

Created `wiki/decisions/pre-launch-checklist.md` — seven sections, A to G, from
provisioning the field laptop through to recovering the flight logs.

Filed under `decisions/` because it carries its reasoning. Noted in the document that an
`operations/` folder would be the better home if more procedural documents appear;
inventing a new wiki category for one file would be a structural change to
`project-conventions.md` that a single document does not justify.

## Why

`ISS-12`'s **Needed to resolve** asks for exactly two things: *"a written pre-launch
checklist, and a dry run on the actual field laptop with the actual ground unit."* This is
the first. The second is Aiman's to run, and the document is written to be run *from*.

The organising idea is that a site rehearsal is not a bench test. A bench test asks
whether the system works; a checklist has to hold up when nothing can be fixed and
nothing can be looked up. So every failure line carries a **STOP** response rather than a
diagnosis, and section A ends by turning the WiFi off — because a provisioning gap that
surfaces at home is free, and the same gap at the gate is the whole day.

Three things went in that came out of reading the code rather than from experience:

- **A USB knock kills the dashboard.** `serial_source.py:50` logs and re-raises, which
  ends the pipeline task and cancels the server. Written into section F as expected
  behaviour with a one-line recovery, rather than left to be discovered as a crash.
- **Do not restart the backend to "start clean".** `S2` makes a late start harmless, so
  the instinct to restart is not dangerous — but it costs the session's loss baseline and
  splits the raw log. The browser is the disposable half; reload that instead.
- **Collect the vehicle's SD card first.** It is the only record independent of both the
  link and the laptop.

## Result

The checklist exists. `ISS-12` stays 🔴 Open until the dry run happens — half of what it
asks for is not the whole of it.

Surfaced while writing it:

- **The ground station stores nothing.** `MRC_GroundStation_GEN3/` has no `Storage.ino`
  and no SD code at all; it receives, appends RSSI/SNR, and forwards to the PC. So there
  are exactly two independent records of a flight — the vehicle's card and the PC's raw
  log — and the PC's exists only while the PC is running. Recorded in section G.
- **`flight-unit.md:60` describes the GEN1/GEN2 SD format** (`temp,hum,pres,…` header).
  GEN3 writes the framed `$MRC,…` form, as `FLIGHT21.CSV` shows. Stale, not corrected
  here — `source/` holds externally supplied fact and rewriting it is its own decision.
