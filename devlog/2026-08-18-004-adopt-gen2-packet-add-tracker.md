# 004 · Adopt GEN2 packet format, add issue tracker

**Date** 2026-08-18
**Type** decision
**Refs** ISS-01, ISS-03, ISS-06 … ISS-10

## What

Aiman clarified that GEN1 and GEN2 are not different hardware generations: they share
substantially the same component list and differ in **communication direction**. GEN2 adds a
two-way uplink as a **redundancy path**, letting the ground station command parachute
deployment.

Decisions recorded:

- **ISS-01 resolved** — the GEN2 packet (15 fields, 17 at the PC) is canonical. GEN1's 14-field
  form is kept as reference.
- **ISS-03 resolved** — Heltec V3/V4 is a naming error in the files, not a hardware difference.
- ISS-02, ISS-04, ISS-05 deferred.

Created `wiki/issues.md` and rewrote `source/firmware/packet-format.md` and
`source/firmware/firmware-versions.md` around the GEN1/GEN2 framing.

## Why

The packet format is the dashboard's data contract; it had to be settled before ingest design.
A tracker was needed so unresolved questions stay visible instead of living in chat.

## Result

Ten issues tracked, two resolved. Five were new, surfaced during the rewrite:

- **ISS-06** competition requirements entirely unknown; `source/competition/` is empty
- **ISS-07** `CHUTE:` field is not numeric — needs a decision on where the prefix is stripped
- **ISS-08** no packet counter, timestamp, or checksum in either generation
- **ISS-09** raw source files removed and not recoverable — including `CANSAT_DATA`
- **ISS-10** ground unit re-emits a 180-byte payload through a 160-byte buffer; `snprintf`
  truncates silently, and the fields at risk are RSSI, SNR and the chute flag

Also noted while rewriting: GEN2 carries higher precision than GEN1, including two extra
decimals on lat/lng — ≈1.1 m to ≈0.11 m of GPS resolution. A parser written to GEN1 precision
would discard it.

Aiman removed the raw source files from `wiki/source/` during this work. They are not present
elsewhere under `D:\MRCC` and were never committed, so git cannot recover them. Recorded as
ISS-09; the extracted documents are now the only record of their contents in this repo.
