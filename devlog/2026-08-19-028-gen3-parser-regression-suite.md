# 028 · GEN3 parser regression suite

**Date** 2026-08-19
**Type** change
**Refs** ISS-14

## What

Two new SD captures were imported to `logs/raw/` — `FLIGHT21.CSV` (786 packets, 785 s) and
a replaced `FLIGHT22.CSV` (227 packets, 226 s). Both were copied verbatim into
`backend/tests/fixtures/` and made the corpus for step 1 of the GEN3 dashboard plan.

Created:

- `backend/tests/fixtures/FLIGHT21.CSV`, `FLIGHT22.CSV` — 1013 packets, committed
- `backend/tests/conftest.py` — session-scoped corpus loader, parametrised over both files
- `backend/tests/test_parser_gen3.py` — 33 tests

Edited `crc16_ccitt`'s docstring in `parser.py`: the "85/85 from the SD card" figure was
stale once `FLIGHT22.CSV` was replaced.

The tests split three ways:

| Group | Count | Covers |
|---|---|---|
| Corpus sweep | 9 × 2 files | CRC, `seq` continuity, exact 1000 ms cadence, field set, types, absent link quality, GPS, chute, corpus size |
| Damaged packets | 11 | Checksum mismatch, truncation, non-hex, missing marker, field count, junk tail, partial link quality, raw-text survival |
| Shape | 2 | Ground-station line vs SD line; `chute` as a count |

## Why

The parser was verified ad hoc against hardware output and nothing held it there. The
existing `test_parser.py` covers GEN1 and GEN2 entirely with hand-written lines, which
prove the parser handles input we imagined rather than input the vehicle emits.

The captured CRCs are the point. Checking `crc16_ccitt` against checksums this module
generates itself would only prove it agrees with itself; checking it against 1013 CRCs
computed by `crc16Ccitt()` in Packet.ino proves the thing that actually has to be true.

Fixtures live in `backend/tests/fixtures/`, not `logs/raw/`, because `logs/` is gitignored
by decision — it holds per-run evidence that accumulates. Un-ignoring it would reverse that
for a growing exemption, and skipping the tests when the file is absent would leave a green
suite that tested nothing on a clean checkout. Promoting two files out of `logs/` draws the
line on provenance instead: `logs/raw/` is what a run produced, `tests/fixtures/` is what we
decided to keep forever.

The most valuable single test is `test_a_corrupt_sequence_number_is_never_reported` (rule
S1). A failed checksum means `seq` is suspect too, and reporting it anyway would feed a
fabricated sequence number into link accounting — producing a phantom gap or a false
vehicle restart exactly when the link is worst and the figures are being relied on most.

## Result

55 tests pass, 33 of them new. Step 1 of `wiki/decisions/dashboard-gen3-plan.md` is pinned.

Surfaced, and left open:

- **The previous `FLIGHT22.CSV` was not the same file.** Entry 024 records it as 85 packets
  spanning `seq` 1–200, with rows removed by hand to keep it small — so it contained
  artificial `seq` gaps. The replacement is 227 contiguous packets with none. No real
  coverage was lost, since gaps produced by hand-deleting rows are an artefact of the
  trimming rather than of the link, but the corpus no longer contains any gap at all.
- **Nothing here can test loss arithmetic.** Both captures are clean bench runs: no gaps,
  no corruption, no restarts, altitude within ±1.2 m, chute never commanded, GPS at zero
  satellites throughout (ISS-14). S2–S4 need the fault-injecting mock source, which is
  step 4 of the plan, not a captured file.
- `status.md` still quotes the stale 85/85 figure. It is written at session end by
  convention, so it was left alone.
