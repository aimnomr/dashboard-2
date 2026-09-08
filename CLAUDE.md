# CLAUDE.md

Orientation for a Claude Code session opening this repo for the first time.
**Read `status.md` before doing anything else** — it is the current state and it is
written fresh at the end of every session. This file is the durable part: the rules, the
layout, and the traps that have already cost time.

---

## What this is

Ground station dashboard for the **MRCC CanSat**, second iteration. A LoRa link at 1 Hz
carries telemetry from a flight unit to a Heltec ground unit; a Python backend owns the
serial port and both logs; a React frontend displays. There is also an **uplink** — the
ground can command the vehicle (`EJECT`, `PING`, `RESET`, `RESET:CHUTE`, `SET:*`).

This is real hardware that will be launched. A number shown on the dashboard that the
system cannot actually support is the failure mode this whole project is organised
against. Read the tone of the existing comments before writing new ones — claims are
hedged deliberately, and "the mechanism was driven" is never written as "the parachute
opened".

## Working rules — these are not suggestions

1. **Plan → propose → execute.** Decisions are made jointly. Nothing is built
   unilaterally. Propose the change, get agreement, then do it.
2. **No deletions.** Files are only created or edited. All deletion is Aiman's action.
   Blanking a file counts as a deletion.
3. **Git is Aiman's.** Propose a short commit title and a concise body. Never run a
   mutating `git` subcommand. Read-only git is fine. `.gitignore` may be edited freely.
4. **`status.md` is read at session start and written only at session end.** Not touched
   during work.

Rules 2 and 3 are enforced by a `PreToolUse` hook, not left to memory:
`.claude/hooks/guard-destructive.ps1`, wired in `.claude/settings.json`, matching
`Bash|PowerShell|Write`. If a call is blocked, that is the hook — hand the command to
Aiman rather than working around it.

⚠ The guard scrubs quoted strings before pattern matching, but **not heredoc bodies**. A
document containing the words "mutating git command" written via `cat > file <<'EOF'`
is blocked as though it were running `git command`. Use the `Write` tool for prose that
mentions git or `rm`. Devlog 015 fixed an earlier false positive of the same class; this
one is still open.

Full text of the rules: `wiki/decisions/project-conventions.md`.

## Where to start reading

| Question | File |
|---|---|
| What is happening right now, and what is next | `status.md` |
| What the packet looks like **now** (GEN3.1) | `wiki/decisions/gen3-packet-format.md` |
| Every command, with flags and caveats | `COMMANDS.md` |
| The same commands with prose stripped, for copying | `COMMANDS-QUICK.md` |
| Why the pipeline is shaped this way | `wiki/decisions/ingest-pipeline.md` |
| UI decisions and their rationale | `wiki/decisions/frontend.md` |
| Open issues, `ISS-xx` | `wiki/issues.md` |
| How the project is organised and worked on | `wiki/decisions/project-conventions.md` |
| What was actually done, in order | `devlog/index.md`, then the entry |

`devlog/` is the real history — 65 entries and counting. When something looks wrong or
arbitrary, there is almost always an entry explaining it. Search there before concluding
it is a bug.

## Layout

```
backend/dashboard/   launch code — serial, raw log, parser, WebSocket, uplink API
backend/devtools/    development only, NEVER imported by dashboard/
backend/tests/       pytest
frontend/src/        React + Vite; panels/, components/, lib/, views/, hooks/
firmware/            MRC_{FlightUnit,GroundStation}_{GEN3,GEN4}/ plus tools/ diagnostics
wiki/source/         externally supplied facts — read-only reference
wiki/decisions/      conclusions reached, with rationale
wiki/issues.md       issue tracker, ISS-xx
devlog/              append-only record of executed changes
status.md            current state
logs/                gitignored — raw serial captures, the only copy
```

## Conventions

**Devlog is append-only.** One file per entry, `YYYY-MM-DD-NNN-short-slug.md`, `NNN`
zero-padded and never reset. An entry is **never reopened after it is written** — a
correction is a *new* entry that says what it supersedes. Sections are `## What`,
`## Why`, `## Result`; the header carries `**Date**`, `**Type**`
(`decision|change|fix|investigation`) and `**Refs**`. Append one line to
`devlog/index.md`.

**The wiki split is provenance.** `source/` is external ground truth — the team, the
competition, the hardware. `decisions/` is what this project concluded. Never put an
opinion in `source/`. kebab-case filenames, category folders, the path is the reference.

**Issues** are `ISS-01`… assigned in order and never reused. Resolved ones stay in the
document with a filled-in **Resolution** — they record why something is the way it is.

## Running it

```bash
# tests
cd backend  && python -m pytest tests -q     # 154 expected
cd frontend && npm test -- --run             # 107 expected
cd frontend && npx tsc --noEmit              # noUnusedLocals is ON; an unused prop breaks the build

# simulated flight, no hardware
cd backend  && python -m devtools.run_mock
cd frontend && npm run dev                   # http://localhost:5173

# real ground unit
python -m dashboard --list-ports
python -m dashboard --port COM12             # http://127.0.0.1:8000
```

There is deliberately **no flag on `python -m dashboard` that selects simulated data.**
Mock telemetry lives in a separate package with its own entry point so it cannot be
reached by accident at the pad.

## Traps that have already cost time

- **`arduino-cli` is not installed on Aiman's machine.** No firmware change in this repo
  has ever been compiled here. Never report firmware as verified — write "not compiled
  and not flashed", the way the devlog entries do.
- **`chute` counts eject packets RECEIVED, not releases performed.** One operator EJECT
  can move it by 3. It is not a release count and must never be rendered as one.
- **There is no acknowledgement on the link.** `ul` rising proves the vehicle *heard* a
  command — not that it applied it, and not which one arrived. Every confirmation in this
  system is indirect and is labelled as such. Preserve that labelling.
- **The absolute-test bug has appeared three times** (devlog 058, 063, 065): testing a
  monotonic counter against a constant where a *baseline* was needed. If you are writing
  `chute > 0` or `>= 1`, stop and think about baselines.
- **Mirrored constants drift silently.** `CHUTE_PIN` (057), the sync word (060), the GPS
  pins (062) and `VEHICLE_DEFAULT_REPEAT` (064) all diverged across files. Auto-eject
  bounds now live in five places, and `EJECT_REARM_MS`/`CHUTE_REARM_MS` in three
  languages. Change one, grep for the rest.
- **`logs/` is gitignored and not backed up.** The raw serial captures are the only
  evidence behind several devlog entries. Do not assume they are recoverable.
- **The GEN4 flight sketch still prints `GEN3`** on boot and on the OLED. Cosmetic, but
  actively confusing while confirming a flash.
- **GPS pins moved twice** (042, then 062) and the second move is *not* a revert of the
  first. Read 062 before "fixing" them back.

## State as of 2026-09-09

Branch `feature/apogee-trigger`. Devlogs run to **065**. Backend 154, frontend 107, both
passing; `verify_gen3.py` 14/14.

**Four faults from devlog 065 are open and none are fixed:**

1. An EJECT burst that runs out of attempts records nothing, leaving a stale
   `chuteBaseline` — so the *next* EJECT reports "confirmed after 0 attempt(s)" and
   transmits nothing at all. Two of three real bursts ended this way.
2. `chute` undercounts in the front listen window: `radioListenForEject()` returns a
   bool, so two eject packets inside one 400 ms window count `chute +1` against `ul +2`.
3. The BME280 on the current flight unit failed mid-run and the restart captured
   `baseAltitude` during the corruption. `alt` now reads about −1740 m.
4. **A NaN altitude fires the auto-eject trigger rather than disarming it.** No
   `isnan`/`isfinite` guard exists anywhere in the flight firmware; `drop < cfg.dropM` is
   false for NaN, so `descentCycles` is never reset and climbs to `confirmN`. It cannot
   arm *from* NaN, so the pad is safe — but an already-armed vehicle that starts reading
   NaN deploys wherever it is.

Read `devlog/2026-09-07-065-three-faults-in-one-bench-log.md` and `status.md` **Next 0–3**
before touching the apogee or eject paths, and before telling anyone the system is ready
to fly.

**Auto-eject has still never been tested on hardware.** Sixth session running.
