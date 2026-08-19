import { describe, expect, it } from 'vitest'
import {
  cylinderMesh,
  projectMesh,
  rotateBody,
  shortestAngleDelta,
  smoothAngle,
  viewTransform,
} from '../pose'

describe('angle wrapping', () => {
  it('takes the short way round the circle', () => {
    // 350 -> 10 is 20 degrees forwards, not 340 backwards. Without this the model spins
    // the wrong way every time a reading crosses the wrap point.
    expect(shortestAngleDelta(350, 10)).toBeCloseTo(20, 6)
    expect(shortestAngleDelta(10, 350)).toBeCloseTo(-20, 6)
  })

  it('handles the ordinary case unchanged', () => {
    expect(shortestAngleDelta(0, 45)).toBeCloseTo(45, 6)
    expect(shortestAngleDelta(45, 0)).toBeCloseTo(-45, 6)
  })

  it('never returns more than half a turn', () => {
    for (let from = -720; from <= 720; from += 37) {
      for (let to = -720; to <= 720; to += 53) {
        expect(Math.abs(shortestAngleDelta(from, to))).toBeLessThanOrEqual(180.000001)
      }
    }
  })
})

describe('easing', () => {
  it('moves toward the target without overshooting', () => {
    let angle = 0
    for (let i = 0; i < 5; i++) angle = smoothAngle(angle, 90, 1 / 60, 0.22)
    expect(angle).toBeGreaterThan(0)
    expect(angle).toBeLessThan(90)
  })

  it('converges on the target', () => {
    let angle = 0
    for (let i = 0; i < 240; i++) angle = smoothAngle(angle, 90, 1 / 60, 0.22)
    expect(angle).toBeCloseTo(90, 1)
  })

  it('is framerate independent', () => {
    // Same elapsed time, different step counts, same result — otherwise the model
    // eases at a different speed on a slow machine than on a fast one.
    let fast = 0
    for (let i = 0; i < 120; i++) fast = smoothAngle(fast, 100, 1 / 120, 0.22)

    let slow = 0
    for (let i = 0; i < 30; i++) slow = smoothAngle(slow, 100, 1 / 30, 0.22)

    expect(fast).toBeCloseTo(slow, 1)
  })

  it('eases the short way across the wrap point', () => {
    const next = smoothAngle(350, 10, 1 / 60, 0.22)
    // Forwards past 360, not backwards through 180.
    expect(next).toBeGreaterThan(350)
  })

  it('snaps when smoothing is disabled', () => {
    expect(smoothAngle(0, 42, 1 / 60, 0)).toBe(42)
  })
})

describe('body rotation', () => {
  const nose = { x: 0, y: 0, z: 1 }

  it('leaves the model alone at zero attitude', () => {
    const p = rotateBody(nose, 0, 0)
    expect(p.z).toBeCloseTo(1, 6)
    expect(p.x).toBeCloseTo(0, 6)
    expect(p.y).toBeCloseTo(0, 6)
  })

  it('tips the long axis over when pitched', () => {
    // +z is the can's long axis, which is why az reads ~1 g upright.
    const p = rotateBody(nose, 90, 0)
    expect(p.x).toBeCloseTo(1, 6)
    expect(p.z).toBeCloseTo(0, 6)
  })

  it('preserves length', () => {
    // A rotation that stretched the model would be a bug visible only as a subtly
    // wrong shape, which is the kind that survives review.
    for (const [pitch, roll] of [[30, 0], [0, 45], [12, -70], [140, 200]]) {
      const p = rotateBody({ x: 0.3, y: -0.5, z: 0.8 }, pitch, roll)
      expect(Math.hypot(p.x, p.y, p.z)).toBeCloseTo(Math.hypot(0.3, -0.5, 0.8), 6)
    }
  })
})

describe('which way is up', () => {
  // The whole class of bug the tests missed until hardware found it. Every existing test
  // checked the body rotation; none checked the camera. A CanSat standing upright
  // rendered lying on its side, and only looked upright at roll -90.

  it('puts the body long axis along screen height, not screen depth', () => {
    // +z is the can's long axis. Upright, it must dominate screen-up, not lean away.
    const top = viewTransform({ x: 0, y: 0, z: 1 })
    expect(top.y).toBeGreaterThan(0.9)
  })

  it('projects the nose above the tail at rest', () => {
    const faces = projectMesh(cylinderMesh(), 0, 0, 100)
    const meanY = (kind: string) => {
      const face = faces.find((f) => f.kind === kind)!
      return face.points.reduce((sum, p) => sum + p.y, 0) / face.points.length
    }
    // Screen y grows downward, so the nose must have the SMALLER value.
    expect(meanY('nose')).toBeLessThan(meanY('tail'))
  })

  it('renders an upright can taller than it is wide', () => {
    // The failure mode was a foreshortened cylinder that read as lying down.
    const faces = projectMesh(cylinderMesh(), 0, 0, 100)
    const ys = faces.flatMap((f) => f.points.map((p) => p.y))
    const xs = faces.flatMap((f) => f.points.map((p) => p.x))
    const height = Math.max(...ys) - Math.min(...ys)
    const width = Math.max(...xs) - Math.min(...xs)
    expect(height).toBeGreaterThan(width * 1.5)
  })

  it('lays the can across the screen at pitch 90, not at rest', () => {
    const onSide = projectMesh(cylinderMesh(), 90, 0, 100)
    const ys = onSide.flatMap((f) => f.points.map((p) => p.y))
    const xs = onSide.flatMap((f) => f.points.map((p) => p.x))
    expect(Math.max(...xs) - Math.min(...xs)).toBeGreaterThan(Math.max(...ys) - Math.min(...ys))
  })

  it('points the can at the camera on roll, so it foreshortens rather than lying across', () => {
    // A consequence of the camera looking along -y: pitch tips the can left and right,
    // roll tips it toward and away. Both are correct; recorded because the difference
    // is not obvious and a future camera move would change it.
    const rolled = projectMesh(cylinderMesh(), 0, 90, 100)
    const upright = projectMesh(cylinderMesh(), 0, 0, 100)
    const height = (faces: typeof rolled) => {
      const ys = faces.flatMap((f) => f.points.map((p) => p.y))
      return Math.max(...ys) - Math.min(...ys)
    }
    expect(height(rolled)).toBeLessThan(height(upright))
  })

  it('keeps depth ordering consistent with the camera', () => {
    // +y is toward the viewer, so it must come out nearer than -y.
    expect(viewTransform({ x: 0, y: 1, z: 0 }).z)
      .toBeGreaterThan(viewTransform({ x: 0, y: -1, z: 0 }).z)
  })
})

describe('the camera is a rotation, not a mirror', () => {
  // The second half of the camera bug, found on hardware the same way as the first: the
  // model was mirrored, so roll matched reality and pitch was reversed. That pairing is
  // the signature — mirroring about the screen-x plane leaves the roll axis (normal to
  // the plane) alone and reverses the pitch axis (lying in it).
  //
  // Every test above passes just as happily under a reflection, which is why none of
  // them caught it. These two do not.

  it('preserves handedness — determinant is +1', () => {
    // A reflection has determinant -1 and mirrors the whole scene. This is the single
    // assertion that would have caught the bug at the time it was written.
    const e = (v: { x: number; y: number; z: number }) => viewTransform(v)
    const a = e({ x: 1, y: 0, z: 0 })
    const b = e({ x: 0, y: 1, z: 0 })
    const c = e({ x: 0, y: 0, z: 1 })

    const det =
      a.x * (b.y * c.z - b.z * c.y) -
      a.y * (b.x * c.z - b.z * c.x) +
      a.z * (b.x * c.y - b.y * c.x)

    expect(det).toBeCloseTo(1, 6)
  })

  it('leans the can the same way for +pitch as the body frame says it should', () => {
    // Pins the SIGN of pitch on screen, not just the axis. The body's nose is +z; a
    // positive pitch rotates it toward +x, and with screen-right at world -x it must
    // therefore appear on the LEFT. Reverse the camera parity and this flips.
    const nose = viewTransform(rotateBody({ x: 0, y: 0, z: 1 }, 45, 0))
    expect(nose.x).toBeLessThan(0)

    // ...and symmetrically the other way, so this cannot pass by accident on a
    // transform that collapses x entirely.
    const other = viewTransform(rotateBody({ x: 0, y: 0, z: 1 }, -45, 0))
    expect(other.x).toBeGreaterThan(0)
  })
})

describe('mesh and projection', () => {
  it('builds a closed cylinder with a visible stripe', () => {
    const mesh = cylinderMesh()
    // Without a stripe the cylinder is rotationally symmetric and roll is invisible —
    // the model would sit still through the motion it most needs to show.
    expect(mesh.filter((f) => f.kind === 'stripe').length).toBeGreaterThan(0)
  })

  it('gives the two ends distinct kinds', () => {
    // An inverted cylinder has the same silhouette as an upright one. Different ends
    // are the only thing that separates them, and on a descending CanSat that is the
    // distinction worth seeing.
    const mesh = cylinderMesh()
    expect(mesh.filter((f) => f.kind === 'nose')).toHaveLength(1)
    expect(mesh.filter((f) => f.kind === 'tail')).toHaveLength(1)
  })

  it('sorts faces far to near', () => {
    const faces = projectMesh(cylinderMesh(), 20, 35, 50)
    for (let i = 1; i < faces.length; i++) {
      expect(faces[i].depth).toBeGreaterThanOrEqual(faces[i - 1].depth)
    }
  })

  it('produces finite geometry at every attitude', () => {
    for (let pitch = -180; pitch <= 180; pitch += 30) {
      for (let roll = -180; roll <= 180; roll += 30) {
        for (const face of projectMesh(cylinderMesh(8), pitch, roll, 40)) {
          for (const p of face.points) {
            expect(Number.isFinite(p.x)).toBe(true)
            expect(Number.isFinite(p.y)).toBe(true)
          }
          expect(face.light).toBeGreaterThanOrEqual(0)
          expect(face.light).toBeLessThanOrEqual(1)
        }
      }
    }
  })
})
