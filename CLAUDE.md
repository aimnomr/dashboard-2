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
- **`chute` counts RELEASES PERFORMED — one per operator EJECT.** Changed 2026-09-09
  (devlog 067): the increment is gated on `chuteFire()`'s return, so a repeat landing
  inside the drive latch moves nothing. Before that it rose per packet received and one
  EJECT typically moved it by 3 — **any log captured before 2026-09-09 is the old
  behaviour.** It still means the servo was DRIVEN, never that a parachute opened, and
  must never be rendered as one.
- **There is no acknowledgement on the link.** `ul` rising proves the vehicle *heard* a
  command — not that it applied it, and not which one arrived. Every confirmation in this
  system is indirect and is labelled as such. Preserve that labelling.
- **The absolute-test bug has appeared four times** (devlog 058, 063, 065, 070): testing a
  monotonic counter against a constant where a *baseline* was needed. If you are writing
  `chute > 0` or `>= 1`, stop and think about baselines. 070 found the sharper version of
  it — a baseline is not enough if the comparison is made somewhere that cannot observe
  the answer arriving.
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

## State as of 2026-09-10

Branch `feature/dashboard-update`. Devlogs run to **071**. Backend 184 (+1 strict
`xfail`), frontend 121, both passing; `verify_gen3.py` 14/14.

**All four faults from devlog 065 are now closed in code:**

1. ~~An EJECT burst that runs out of attempts records nothing~~ — **fixed in 070.**
   Confirmation moved out of `fireEjectBurst()` into `radioPoll()`, where the packet
   actually arrives. `ejectConfirmed = true` now happens in exactly one place.
2. ~~`chute` undercounts in the front listen window~~ — **fixed in 067**, by counting on
   the drive rather than on receipt. See the `chute` trap above.
3. ~~The BME280 failed and `baseAltitude` was captured during the corruption~~ —
   **cleared on hardware** (066: 11,528 NaN-free packets after 03:48 on 2026-09-09), and
   the **code half fixed in 071**: the baseline is now retried and plausibility-banded,
   and a vehicle that cannot zero refuses to zero rather than zeroing wrongly.
4. ~~A NaN altitude fires the auto-eject trigger~~ — **fixed in 071.** `apogeeUpdate()`
   rejects a non-finite sample and holds state: it cannot fire on NaN, and it does not
   reset a confirmation already in progress either.

**The auto-eject trigger now runs on its own clock** (071): it samples altitude every
`AUTO_EJECT_SAMPLE_MS` (125) from the four poll loops while telemetry stays at 1 Hz.
`AUTO_EJECT_CONFIRM_N` is still 3, but it counts **samples, not cycles** — so three
confirmations cost ~250 ms rather than 2 s, and detection from apogee falls from ~3.4–4.4 s
to ~1.7–1.8 s. **Any log or SD `#` line from before 2026-09-10 means the old thing by
`cycles=`.** The rate is capped by the BME280's ~113 ms conversion, not by the CPU.

⚠ **Nothing in 067, 069, 070 or 071 has been compiled or flashed.** Every ground station in
every log to date is the `Sep  7 2026 19:48:44` build, which has none of it. The fixes are
reviewed and modelled, not verified. **Auto-eject has still never fired on hardware.**

Read `devlog/2026-09-07-065-three-faults-in-one-bench-log.md` and `066` before touching
the apogee or eject paths, and before telling anyone the system is ready to fly. **066 also
found two flight units transmitting on one channel** with no identifier in the packet to
tell them apart — check that before trusting any field in a log.

**Auto-eject has still never been tested on hardware.** Eighth session running — and it
has now been substantially rewritten (071) without ever having fired once. That is the
single largest untested thing in the project.
