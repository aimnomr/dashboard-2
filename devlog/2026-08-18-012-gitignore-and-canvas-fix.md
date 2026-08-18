# 012 · Project .gitignore and attitude canvas fix

**Date** 2026-08-18
**Type** fix
**Refs** ISS-12

## What

**1. Full project `.gitignore`.** Replaced the minimal placeholder with sectioned rules
covering Python (`.venv`, `__pycache__`, `.pytest_cache`, caches), Node
(`node_modules`, `.vite`, debug logs), build output (`frontend/dist/`), flight data
(`logs/`, `*.sqlite`, `*.db`), environment files, editors and OS cruft.

`.claude/settings.json` and `.claude/hooks/` remain **tracked deliberately** — they carry
the project's guard hook, which is a team rule rather than a personal preference. Only
`.claude/settings.local.json` is ignored.

**2. Attitude panel canvas overflow.** The dial was growing beyond its panel.

## Why

**Build output.** `frontend/dist/` was the only tracked path the new rules cover. The
case for committing it was ISS-12 — no internet at the launch site — but Vite
content-hashes asset filenames, so every rebuild commits a brand-new copy and orphans
the previous one in history forever. Demonstrated during this change: the index held
`index-CDzJMC2c.js` / `index-swUIq3nE.css` while a single rebuild produced
`index-BJwDBkkx.js` / `index-DrF5TKjG.css`. ISS-12 is better served by a pre-launch
checklist item, or a release archive if a network-free clone-and-run is genuinely
wanted.

**Flight data.** `logs/` and the parsed store are per-run evidence produced on the
machine that recorded them, not project artefacts. Resolves the question left open
since entry 007.

**Canvas overflow.** Two compounding causes:

- The canvas was in normal flow inside `.canvas-host`, while the drawing code sized the
  canvas from `host.clientHeight`. Host sized by canvas, canvas sized by host — every
  `ResizeObserver` pass nudged it larger and it walked out of the panel.
- `.attitude__dial` carried `aspect-ratio: 1`, so height followed width. In a wide,
  short panel the resulting square was taller than the panel body. The `1fr` column
  made it worse: a plain `fr` track has an automatic minimum of its content size, so
  the dial pushed the column wider rather than shrinking.

## Result

Fixes applied:

- `.canvas-host > canvas` is now `position: absolute; inset: 0`, taking the canvas out
  of flow so it cannot contribute to its host's height. This breaks the feedback loop
  at the source and applies to the ground track canvas too.
- `.canvas-host` gained `overflow: hidden` as a backstop.
- `aspect-ratio` removed from `.attitude__dial`; the column is now `minmax(0, 1fr)` and
  the grid uses `align-items: stretch`. The dial fills whatever cell it gets, and the
  drawing already centres its circle at `min(width, height)`, so a non-square cell is
  harmless.

Build passes. **Not visually confirmed** — no browser available in this environment, so
the fix is reasoned from the layout rather than observed.

Also noted: the guard hook produced a **false positive** during this work, blocking a
command because the word "git" appeared inside a quoted echo string. The pattern matches
`git` anywhere in a command segment rather than only in command position. Reported, not
changed — the hook is an agreed project rule and altering it is not a unilateral call.
