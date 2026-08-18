# 005 · Set project conventions

**Date** 2026-08-18
**Type** decision
**Refs** —

## What

Settled the three conventions left open since the structure was created, and recorded them in
`wiki/decisions/project-conventions.md`:

- **Wiki naming** — kebab-case filenames, category folders, `.md` throughout. The path is the
  reference; no numeric or ID prefixes. Nothing needed renaming.
- **Devlog** — one file per entry, `YYYY-MM-DD-NNN-short-slug.md`, never reopened after
  writing. `devlog/index.md` carries one appended line per entry. Entry template fixed with
  **What / Why / Result** and a type of `decision`, `change`, `fix` or `investigation`.
- **status.md** — three sections, **Now / Next / Blocked**. Read at session start, written only
  at session end.

Also recorded `wiki/decisions/ingest-pipeline.md`, capturing the pipeline agreed earlier in the
session: raw bytes to disk before parsing, ground unit as a dumb pipe, and the source treated as
a seam so replay stays cheap later.

Backfilled devlog entries 001–005 for the work already completed this session, and created
`status.md`.

## Why

Conventions were deliberately deferred until there was real material to organise, so the choices
were made against actual files rather than guesses. The ingest pipeline had been agreed in
conversation only and needed a durable home.

## Result

One file per devlog entry was chosen specifically because the no-deletion rule makes it
structurally immutable — an entry file, once written, is never touched again. The alternatives
would have left immutability as convention only.

`wiki/decisions/` now holds its first two documents. Replay is explicitly **not** being built:
the seam is preserved in the design, features are taken independently, live first.
