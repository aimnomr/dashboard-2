# Project Conventions

Decided 2026-08-18. How this project is organised and worked on.

---

## Repository layout

```
dashboard-2/
├── status.md              current state — read at session start, written at session end
├── wiki/
│   ├── issues.md          issue tracker, ISS-xx
│   ├── source/            externally supplied facts. Read-only reference.
│   │   ├── competition/   rules, requirements, deadlines
│   │   ├── hardware/      components, pinouts, datasheets
│   │   ├── firmware/      packet format, protocol, firmware behaviour
│   │   └── previous-system/  the v1 Node-RED / MQTT / SQLite stack
│   └── decisions/         conclusions reached jointly, with rationale
└── devlog/                append-only record of executed changes
```

Application code directories are not yet created — the stack is not settled.

## Wiki naming

**kebab-case filenames, category folders, `.md` throughout.**

- The **path is the reference**: `source/firmware/packet-format.md`. Link between pages with
  relative paths.
- Folders carry the grouping; filenames stay human-readable with no numeric or ID prefix.
- Ordering within a folder is alphabetical. If reading order matters, state it in the parent
  page rather than encoding it in filenames.

**`source/` vs `decisions/`** — the split is provenance:

| | `source/` | `decisions/` |
|---|---|---|
| Holds | external ground truth | conclusions we reached |
| Authority | the team, the competition, the hardware | this project |
| Changes when | new information arrives | we decide differently |

Extracted summaries of source material live in `source/`, attributed to the file they came from.
Analysis and choices live in `decisions/`.

## Issue tracker

`wiki/issues.md`. IDs are `ISS-01`, `ISS-02`, … assigned in order and never reused.
Status is 🔴 Open · 🟡 Deferred · 🟢 Resolved.

Resolved issues **stay in the document** — they record why something is the way it is.
Update the status in both the index table and the detail entry, and fill in **Resolution**.

Reference issues by ID from anywhere in the wiki.

## Devlog

`devlog/`, **one file per entry, append-only**. An entry file is never reopened after it is
written. Corrections are made by writing a *new* entry that supersedes the old one and says so.

**Filename:** `YYYY-MM-DD-NNN-short-slug.md` — `NNN` is a zero-padded sequence that never resets.

**Entry template:**

```markdown
# NNN · Title

**Date** YYYY-MM-DD
**Type** decision | change | fix | investigation
**Refs** ISS-xx, or —

## What
What was actually done.

## Why
The reason it was done.

## Result
What changed as a result, including anything surfaced or left open.
```

**Types:**

| Type | Use for |
|---|---|
| `decision` | a choice was made and is now binding |
| `change` | files were created or modified |
| `fix` | something broken was corrected |
| `investigation` | something was examined; findings recorded, nothing changed |

`devlog/index.md` carries one appended line per entry:
`- YYYY-MM-DD · NNN · Title`

## status.md

Three sections: **Now**, **Next**, **Blocked**. Read at the **start** of every session, written
**only** when a session ends. Not touched during work.

It stays short by design — detail belongs in the devlog and the issue tracker. Blocked items
reference issues by ID rather than restating them.

## Working rules

- **Plan → propose → execute.** Decisions are made jointly. Nothing is built unilaterally.
- **No deletions.** Files are only created or edited; all deletion is Aiman's action.
  Blanking a file counts as deletion.
- **Git is Aiman's.** Claude proposes a short commit title and concise body, and never runs a
  mutating git command. Read-only git is allowed. `.gitignore` may be edited freely.

The last two are enforced by a `PreToolUse` hook, not left to memory —
`.claude/hooks/guard-destructive.ps1`, wired in `.claude/settings.json`.
