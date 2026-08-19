# 049 · Raw logs now explain themselves

**Date** 2026-08-20
**Type** change
**Refs** —

## What

Every new raw log opens with a `#`-prefixed block giving the full packet contract: wire
format for all four generations, every field with its index, printf format, unit and
meaning, the CRC specification, the sentinel values, the plausibility bounds, and the
traps that cannot be inferred from the numbers.

About 167 lines, 8.8 kB — roughly 4% of a thirty-minute flight log.

New: `backend/dashboard/contract.py`, `backend/tests/test_contract.py`.
Edited: `parser.py` (`FIELD_DOC`), `rawlog.py`, `sources/file_source.py`.

## Why it goes inside the file

This reverses the decision `rawlog.py` used to state outright — *"Run metadata goes beside
the log, never inside it."*

That reasoning was about replayability, and it was sound. What made the reversal safe is
that `parse_line()` **already** classifies `#` lines as status, because the vehicle's own
SD logs use them. A header costs a replay nothing: `file_source` reads it, the parser
skips it, the pipeline reports it like any `[GCS]` line. Nothing has to be stripped and no
consumer needs to know it exists.

And the reason to want it there at all: a sidecar is a separate file, and separate files
get lost. A log copied to a USB stick, pasted into a chat, or attached to a competition
submission arrives alone — and then nobody can tell whether column 14 is a longitude or a
sentinel meaning "no fix". The contract belongs where the data is.

The `.meta.json` sidecar stays. That one is for programs; this one is for people.

## Generated, not written

The field table is rendered from `parser.FIELD_DOC`, which sits beside the field tuples it
documents. `test_field_doc_covers_every_field` fails if a field is added to the wire
without an entry, and its mirror fails if a documented field is removed.

That is the whole point. A contract maintained separately from the parser is wrong within
two revisions, and a wrong contract is worse than none because it is believed.

## The bug this introduced, and how it was caught

The header stalled fixed-interval replays.

`_delay_before()` returned immediately for anything with no vehicle clock — headers
included, with a comment already saying so. But that check sat **below** the
`self.interval is not None` branch, so `--interval 1` paced every line equally: 167
intervals, nearly three minutes of empty dashboard, before the first packet.

The clock-paced branch was right and the fixed-interval branch was not, which is why
adding a hundred-odd comment lines to the top of a file was not the harmless change it
looked like. The check moved above both branches.

## What the header warns about

The sections that matter are the ones a reader would otherwise get wrong:

- `lat/lng = 0.00000` is **no fix**, not a position in the Gulf of Guinea
- `hdop = 0.0` is **not reported**, never a perfect fix
- `fixq = -1` is **not reported**, distinct from `0` meaning the receiver rejects the fix
- `chute` counts commands **received**, never deployments — nothing on board can sense the
  canopy, and no reading in the file ever confirms one
- `alt` is relative to **boot**, so negative values are normal
- `seq` restarts at 1 on reboot; subtracting across a restart gives a nonsense loss figure
- `rssi`/`snr` are appended **after** the checksum, so the CRC does not cover them, and
  they are absent from anything read off the vehicle's SD card
- `az` reads ~0.92 g at rest on the current unit, not 1.00 — unresolved
- the gyro emits single-sample spikes of 70–190 deg/s while stationary

The last two are open faults rather than format facts. They are in the header because
somebody analysing a capture six weeks from now will hit them and needs to know they were
already known.

## Result

**136 backend tests** (was 125). Twelve new, including that every header line parses as
status, that a written log replays with zero rejections, that the header is one block at
the top rather than interleaved, and that it never consumes replay cadence in either
pacing mode.

Existing logs in `logs/raw/` are untouched — this affects newly created files only.

Worth noting for later: the S8 test here checks a *property* rather than banning the word
"deployed", because the header is required to say "never deployed". A naive substring ban
failed on the very disclaimer it existed to enforce.
