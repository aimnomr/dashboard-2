# Status

**Updated** 2026-08-18 · end of session 1

## Now

Planning. No application code yet — the stack is not settled.

Working rules agreed and enforced by a `PreToolUse` hook. Wiki populated from the source
material: hardware, firmware, LoRa protocol, packet format, and the v1 Node-RED stack are all
documented. Conventions set for wiki naming, devlog entries, and this file.

Settled so far:

- GEN2 packet is the canonical format — 15 fields, 17 at the PC (`ISS-01`)
- Ingest pipeline — raw bytes to disk before parsing, ground unit as a dumb pipe,
  source treated as a seam (`wiki/decisions/ingest-pipeline.md`)
- Live view only for now; replay deferred, features taken independently

## Next

1. Settle the packet contract with the firmware member — `ISS-07` and `ISS-08` together,
   since both are contract changes and are cheaper agreed in one conversation
2. Confirm the stack — Python backend + browser frontend was proposed and parked
3. First ingest spike: serial → raw log → parser, against the GEN2 format

## Blocked

- `ISS-06` — competition requirements unknown, `wiki/source/competition/` is empty
- `ISS-09` — `CANSAT_DATA` not recoverable; no real telemetry available for development
- `ISS-02` — GEN2 ground unit firmware missing, so the uplink cannot be tested end to end
