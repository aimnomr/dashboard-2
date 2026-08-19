# 038 · Pose model rendered on its side

**Date** 2026-08-19
**Type** fix
**Refs** —

## What

Aiman, from hardware: *"The cylinder is on its side when the CanSat is facing upright. The
cylinder is facing upright when the CanSat's roll is at -90."*

`viewTransform()` in `lib/pose.ts` had the two elevation terms swapped:

```
was:   y: p.y * ce - p.z * se      z: p.y * se + p.z * ce
now:   y: p.y * se + p.z * ce      z: p.y * ce - p.z * se
```

`y` is the screen-up component and `z` is depth. With them swapped, the body's long axis
was projected into screen *depth* instead of screen *height*, so an upright can rendered
foreshortened and lying down — and only stood up when a roll of -90 deg happened to
rotate its long axis into the axis the camera was treating as vertical.

## Why the tests did not catch it

Fifteen tests on this module, all passing, and every one of them missed it.

They tested `rotateBody` — that rotations preserve length, that pitch tips the long axis
over, that the geometry is finite at every attitude. All true, and all about the **body**.
Not one test asked about the **camera**: whether the thing that comes out the far end is
the right way up.

That is the shape of the gap. The maths was verified against itself and never against the
question the module exists to answer, which is *"does an upright CanSat look upright"*.

Five tests added under `which way is up`:

- the body long axis dominates screen height, not depth
- the nose projects above the tail at rest
- an upright can renders taller than it is wide
- pitch 90 lays it across the screen
- depth ordering matches the camera

Also recorded, because it is not obvious and a future camera move would change it: with
the camera looking along -y, **pitch tips the can left and right, roll tips it toward and
away**. So a roll reads as foreshortening rather than as leaning. Both are geometrically
correct. The first version of the new test asserted roll 90 would lay the can across the
screen; it does not, and the test was wrong rather than the code.

## Result

Confirmed by eye against the mock: at pitch 0 / roll 0 the cylinder stands upright, stripe
visible, sitting in the ground ellipse. 68 frontend tests pass, 118 backend.

Worth stating plainly: **this was found on hardware, by a person looking at it.** No amount
of the arithmetic testing already present would have surfaced it, because the arithmetic
was right and the framing was wrong.
