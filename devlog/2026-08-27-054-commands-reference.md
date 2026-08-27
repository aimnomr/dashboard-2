# 054 · Every command in one file

**Date** 2026-08-27
**Type** change
**Refs** ISS-12

How to run this system was spread across five files, three of them source code.

## What

**`COMMANDS.md` at the repo root.** Nine sections — setup, launch, mock, replay,
uplink, frontend, tests, firmware, diagnostics — plus a table of the log files every
one of them reads or writes. Each command carries its full flag set, one line on when
it is reached for, and the traps that are not visible from the command itself.

**Assembled from the entry points, not from the README.** `dashboard/__main__.py`,
`devtools/run_mock.py`, `run_replay.py`, `send_command.py`, `frontend/package.json`,
`frontend/vite.config.ts`, `firmware/tests/verify_gen3.py`, `firmware/tools/README.md`.
The argparse definitions are the authority, so the flag tables are complete rather than
being the subset the README happened to show.

**Root, not `wiki/`.** The wiki is for knowledge; this is a thing you open on a field
laptop while something is going wrong. It sits next to `README.md` where it is found
without knowing the wiki exists. `wiki/decisions/pre-launch-checklist.md` remains the
procedure — the order to do things in and what to STOP on — and `COMMANDS.md` links to
it rather than restating any of it.

**README gained a pointer** under Quick start, and `COMMANDS.md` in the Layout tree.
Quick start stays as it is: it is the thirty-second version and duplicating a third of
the reference is what a quick start is for.

## Why

The flags for `run_replay` existed in `devtools/README.md`. The flags for
`python -m dashboard` existed only in argparse. The GEN4 command grammar existed in a
firmware header, in `api.py`, and in devlog 053. The firmware diagnostics existed in
`firmware/tools/README.md`. Nothing listed all of it, and the file that comes closest —
the pre-launch checklist — is deliberately a procedure and would be worse if it tried.

`ISS-12` is a field-laptop issue. Part of what makes a field laptop usable is not having
to remember which of four READMEs holds the flag you want.

## Result

**Three things were only findable by reading source, and are now written down.**

`--http-port` on any value other than 8000 breaks `npm run dev`. `vite.config.ts`
proxies `/ws` and `/api` to `127.0.0.1:8000` as a literal, so the dev server silently
talks to nothing. Harmless at launch, where FastAPI serves the bundle itself and the
proxy is not involved. Not recorded anywhere before this.

`send_command` needs `pip install websockets`, which is in neither `requirements.txt`
nor `requirements-dev.txt`. Deliberate — one convenience script should not weigh on the
launch environment — but the only notice was the script's own ImportError.

`run_replay` has no extension filter and never did. Its docstring says the `.CSV`-led
examples gave the opposite impression; the reference now leads with a `.log`.

**The GEN4 bounds are now duplicated in four places**, not three: `api.py`, the GEN4
ground station, the GEN4 vehicle, and this file. All three code copies already carry a
comment saying they must move together. `COMMANDS.md` says the same and names `api.py`
as the one that validates, so the table is a reader's convenience rather than a fourth
authority — but it is a fourth copy and will go stale the same way.

**This does not close `status.md` Next 10.** That asks for a wiki page on the GEN4
uplink grammar. `COMMANDS.md` lists what to type and what each command does; it says
nothing about burst geometry, `ul` confirmation, prefix-versus-exact matching, or why
`RESET` and `RESET:CHUTE` are separate tokens. Those are protocol facts and belong in
the wiki.

**Nothing here was verified by running it.** Every command was transcribed from its
definition, and the traps from the code and from `status.md`. The flag names and
defaults are as accurate as the source; the claim that a given invocation behaves as
described is untested, and the same Arduino-toolchain gap that left GEN4 uncompiled
means section 7 lists sketch folders rather than a build command.
