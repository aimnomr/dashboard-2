# 006 · Build against GEN2 as emitted

**Date** 2026-08-18
**Type** decision
**Refs** ISS-07, ISS-08

## What

Decided to consume the GEN2 packet **exactly as it is emitted today**, requesting no firmware
changes. `ISS-07` and `ISS-08` moved from Open to Deferred, with the interim position recorded
in both the tracker and `wiki/decisions/ingest-pipeline.md`.

`ISS-07` takes option (b): the dashboard strips the `CHUTE:` prefix on ingest.

## Why

Both issues are owned by another team member. Waiting on them would block every other feature in
the project for a change that is small on our side.

## Result

Consequences accepted and now documented so they are designed around rather than forgotten:

- **Packet loss cannot be detected** — no counter exists. Link health must be built from time
  since last packet, RSSI and SNR alone, and must not display gap or loss figures it cannot
  compute.
- Time axis is PC arrival time at 1 s resolution, not sampling time. Anything time-derived
  inherits that error.
- Corruption that still parses is undetectable; range checks against the documented clamps are
  the only defence.

Constraint carried into the parser design: adding a counter, timestamp or checksum later must be
**additive** — an extra field should extend the frame, not force a rewrite.
