# 077 · The pose model is a rocket, not a placeholder cylinder

**Date** 2026-09-10
**Type** change
**Refs** 013, 014, 035, 038, 041, 045, 068

Frontend only. Closes `status.md` Next 14 — "replace the placeholder cylinder in
`cylinderMesh()`". `cylinderMesh()` is now `rocketMesh()`: nose cone, body tube, flat
tail, four swept fins, and the longitudinal stripe kept.

## What

The cylinder carried two ambiguities that **only colour resolved**:

- rotationally symmetric about its own axis, so roll was invisible without the stripe
- identical in silhouette upside down, so upright and inverted were separated only by the
  nose and tail caps being different colours

A cone and fins resolve both **in the shape**. The four `Face` kinds are unchanged, so
`shade()` needed no new branch — the whole cone is `nose` rather than one flat cap, and
the fins are `stripe`, which is what they semantically are: the roll marker the stripe was
invented to be.

Radial extent is held at **0.42**, the old cylinder's radius, so the silhouette is no
wider than the canvas was already sized for. The tube is narrower; the fins reach where
the skin used to be.

The stripe stays. Four fins are four-fold symmetric, so roll would still be ambiguous
modulo 90° without it, and exactly nose-on the fins foreshorten to spokes that look the
same every quarter turn.

## Why it matters more than it looks

**Devlog 068 removed the reliability shading** — the greying, the desaturated horizon and
the `Attitude unreliable` banner. Colour used to carry meaning in this panel and now
carries less, so silhouette is doing more of the work than it was designed to. A shape
that states its own orientation is worth more here than it was when the cylinder was
written.

The same argument covers the cases colour never handled: a washed-out screen in daylight,
and a colourblind operator.

## The cost, stated plainly

**`projectMesh()` is no longer exact.** Its comment said so in as many words: *"Painter's
algorithm … for a convex solid this is exact, and a cylinder is convex."* A finned body is
not convex, and the comment has been rewritten rather than left to quietly become false.

What that costs: a fin on the far side sorts behind the body and is correctly hidden; one
on the near side sorts in front and is correctly drawn over. The error case is a fin
roughly side-on, whose mean depth sits near the body axis while part of it is nearer than
the body's own skin — its root can be painted over at some angles. The fin is nearly
edge-on there and a few pixels wide, so it should read as a flicker at the silhouette
rather than as a wrong attitude.

A depth buffer would fix it and is not worth 100 lines. Splitting each fin at the body
radius would fix most of it and is the cheap option if it ever looks wrong.

## Result

Frontend **122 pass** (121 before), `npx tsc --noEmit` clean with `noUnusedLocals` on.

Two tests changed and one is new:

- **`gives the two ends distinct kinds`** no longer asserts one `nose` face and one
  `tail` face — `nose` is now every face of the cone. It checks both kinds exist and that
  the nose group's mean z is above the tail's, which is the property that actually
  mattered.
- **`resolves roll and inversion in the SHAPE, not only in colour`** is new, and is the
  regression test for this entry: strip every kind to one colour and the silhouette must
  still say which way up it is and which way it is rolled. It asserts the nose end is
  narrower than the tail end, and that geometry exists beyond the tube radius.

⚠ **Nobody has looked at this model in a browser.** `status.md` Next 11 has said so since
session 5 and it is still true — 038, 041 and 045 were each a rendering fault found on
hardware rather than in a test, and the camera alone has shipped three separate sign
errors. Everything above is reasoning and arithmetic, not observation. **Run
`npm run dev` and look at it before trusting any of it.**

## Not addressed

The vehicle is a CanSat — a can — and this model is a rocket. That is a deliberate
legibility choice rather than a likeness: a cone and fins state orientation at a glance in
a way a can cannot. If the panel should instead depict the actual airframe, that is a
different decision and a smaller mesh, and this entry is the place it would be argued
against.
