# 014 · Revert 3D attitude back to the 2D horizon

**Date** 2026-08-18
**Type** change
**Refs** —

Supersedes entry 013, which added the Three.js attitude panel.

## What

Reverted the attitude panel to the canvas 2D artificial horizon. Done **forward in the
working tree**, not with a git revert — the history stays as it is.

- `panels/AttitudePanel.tsx` — rewritten as the 2D canvas horizon
- `lib/attitude.ts` — `integrateSpin()` and `MAX_INTEGRATION_DT` removed
- `lib/__tests__/logic.test.ts` — the five spin-integration tests removed; 16 remain
- `vite.config.ts` — `chunkSizeWarningLimit` removed, since the bundle is small again
- `styles/panels.css` — `.attitude__nogl` removed
- `three` and `@types/three` uninstalled
- `tsconfig.json` — temporary `exclude` for the orphaned model file, see below

## Why

The 3D pose was judged not feasible. The reasoning for the original change is preserved
in entry 013 and summarised in `wiki/decisions/frontend.md`, so it is not re-proposed
without new information.

## Result

Bundle returns to **215 kB raw / 76 kB gzipped**, from 733 kB / 207 kB. The CSS output
hash matches the pre-Three.js build exactly, which confirms the styling reverted cleanly
rather than approximately.

16 frontend tests, 17 backend tests, build and end-to-end serve all pass.

**Retained from the Three.js work**, because it fixed a real bug that predated it: the
canvas sizing correction from entry 012 stays. `.canvas-host > canvas` remains
absolutely positioned, which is what stops the ground track canvas walking out of its
panel too.

**One file could not be removed.** `src/lib/rocketModel.ts` is now orphaned and still
imports `three`, so it breaks the typecheck. Deletion is the user's action, so it is
excluded from the TypeScript build with a comment marking it pending deletion. Once the
file is deleted, the `exclude` block in `tsconfig.json` should go with it — an exclude
that points at a nonexistent file is exactly the kind of thing that survives for years.
