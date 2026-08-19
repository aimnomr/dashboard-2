# 050 · The contract belongs in the sidecar, not the log

**Date** 2026-08-20
**Type** change
**Refs** —

**Supersedes entry 049 the same day.** The header block it put at the top of every raw log
is removed; the contract now lives in `<log>.meta.json` as structured JSON.

## What changed and why

Entry 049 read the request as "put this in the log". It was "put this where a reader
already looks, and make it usable". Three things were wrong with the log header:

**The log's entire value is being a faithful byte record.** 167 lines of prose at the top
of a file whose one job is to contain exactly what arrived is a contradiction, however
carefully the parser was made to skip it.

**Prose is the wrong shape.** Anything reading a contract wants `fields[13]["name"]`, not a
paragraph to grep. The header could be read by a person and by nothing else.

**The sidecar already existed** and was already the place metadata went. Adding a second,
differently-shaped copy meant two things to keep in step.

## Scope, decided deliberately

Current wire format only. Four exclusions, each a decision rather than an omission, and
each now pinned by `test_scope_is_the_current_wire_format_only`:

| excluded | why |
|---|---|
| GEN1/GEN2 field lists | this file describes the log beside it, not the project's history |
| plausibility bounds | dashboard warning policy, not a property of the bytes. Publishing them invites treating out-of-range as invalid, which is the opposite of the rule |
| hardware caveats | `az` reads ~0.92 g, the gyro spikes — these get *fixed*. The devlog can be corrected; a sidecar written once cannot |
| observed statistics | would require a close-time rewrite, and a file written once can never be caught half-written |

The unifying rule: **the sidecar is written when the log opens and never revisited**, so
anything transient in it is frozen at that instant and quietly wrong forever after. That
is worse than absent, because a file that looks authoritative gets believed.

`test_no_field_note_describes_a_transient_hardware_fault` enforces the same line at field
level — a `note` explains the encoding, never the state of one airframe.

## Shape

Fields are an **array in wire order**, each carrying its own index and name, so the
intended use is a zip:

```python
values = line.split("*")[0].split(",")[1:]
named  = {f["name"]: v for f, v in zip(meta["fields"], values)}
```

`test_a_reader_can_zip_the_contract_against_a_real_packet` is that promise, executed.

`sentinel` and `note` appear only where there is something to say, so `temp` is one line
and `chute` carries its disclaimer. The whole file is **55 lines, 4.4 kB** — against 167
lines and 8.8 kB in the log, for something machine-readable rather than merely legible.

Serialisation keeps each field object on a single line. `json.dumps(indent=2)` explodes 22
fields into ~150 lines and buries the table it is meant to be; this only changes
whitespace and stays valid JSON.

## Kept from 049

The **fixed-interval replay pacing fix**, which is independently right. A `[GCS]` line
arrives *between* packets on a real link, never instead of one, so it must not occupy a
slot in the cadence — the clock-paced branch already knew that and the fixed-interval
branch did not. Found while chasing the header's effect on replay; it outlives the header.

`FIELD_DOC` also stays in `parser.py`, restructured from a tuple to a dict so it can carry
`sentinel` and `note`. It remains the single source of truth, with the drift guards from
049 intact in both directions.

## Result

`dashboard/contract.py` rewritten, `dashboard/parser.py` (`FIELD_DOC`),
`dashboard/rawlog.py` (header write removed, sidecar expanded),
`tests/test_contract.py` rewritten.

**141 backend tests.** The log is byte-faithful again — `test_the_log_itself_stays_byte_faithful`
asserts a written log equals exactly the line put into it, nothing more.

Existing logs are untouched. Their older sidecars remain valid as far as they go; they
simply carry less.
