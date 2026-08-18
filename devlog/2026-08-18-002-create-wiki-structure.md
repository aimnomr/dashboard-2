# 002 · Create wiki and devlog directory structure

**Date** 2026-08-18
**Type** change
**Refs** —

## What

Created the project skeleton:

```
wiki/source/competition/
wiki/source/hardware/
wiki/source/firmware/
wiki/decisions/
devlog/
```

`wiki/source/` holds externally supplied facts; `wiki/decisions/` holds conclusions reached
jointly. The split exists so provenance is visible at a glance.

## Why

Aiman needed somewhere to drop known facts before decisions continued, with supplied material
kept separate from agreed conclusions.

## Result

Structure only. Deliberately **not** created, because each would have presumed something still
open at the time:

- README/index files in the wiki dirs — naming convention unsettled
- `status.md` — format unsettled
- `src/`, `backend/`, `frontend/` — stack not chosen
- `.gitignore` — depends on whether flight logs get committed

The repository was initialised by Aiman during this session (`e1830f3 Initial commit`,
remote `github.com/aimnomr/dashboard-2`).
