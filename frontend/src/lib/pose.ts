/**
 * Orthographic 3D projection for the attitude model.
 *
 * Hand-rolled rather than WebGL, deliberately. The Three.js attempt (devlog 013) was
 * reverted on practicality — a rendering dependency, a model pipeline and a placeholder
 * to maintain, for 733 kB against a 215 kB bundle. An orthographic projection of a few
 * dozen flat faces is arithmetic. The objection was to the tool, not to the idea.
 *
 * Body frame matches the IMU as mounted: **+z runs along the can's long axis**, which is
 * why `az` reads ~1 g sitting upright on a bench. +x and +y complete a right-handed set
 * across the body.
 */

export interface Vec3 {
  x: number
  y: number
  z: number
}

/** One flat surface of the mesh, in body coordinates. */
/**
 * Surface kinds, each existing to remove a specific ambiguity.
 *
 * `stripe` — a bare cylinder is rotationally symmetric about its own axis, so roll would
 * be invisible without a longitudinal marker.
 *
 * `nose` and `tail` — a cylinder turned upside down has the same silhouette as one the
 * right way up. Colouring the two ends differently is the only cue that separates
 * "upright" from "inverted", which on a descending CanSat is the difference worth seeing.
 */
export interface Face {
  points: Vec3[]
  kind: 'body' | 'stripe' | 'nose' | 'tail'
}

const DEG = Math.PI / 180

/**
 * Smallest signed rotation from `from` to `to`, in degrees.
 *
 * Angles wrap. Interpolating 350° → 10° the direct way sweeps 340° backwards through the
 * whole circle; the vehicle turned 20° forwards. Without this the model spins the wrong
 * way every time a reading crosses the wrap point.
 */
export function shortestAngleDelta(from: number, to: number): number {
  return ((((to - from) % 360) + 540) % 360) - 180
}

/**
 * Exponential approach, framerate independent.
 *
 * Telemetry lands at 1 Hz; a model that snapped to each new sample would jump once a
 * second and read as broken. This eases between them instead.
 *
 * **The smoothed pose lags the data** by roughly `tau`, and between samples it is showing
 * an interpolation rather than a measurement. That is acceptable for a shape whose job is
 * to convey orientation at a glance, and unacceptable for a number — which is why the
 * numeric readouts beside it stay raw and unsmoothed.
 */
export function smoothAngle(
  current: number,
  target: number,
  dtSeconds: number,
  tau: number,
): number {
  if (tau <= 0) return target
  const alpha = 1 - Math.exp(-dtSeconds / tau)
  return current + shortestAngleDelta(current, target) * alpha
}

/** Rotate a body-frame point by roll (about x), then pitch (about y). */
export function rotateBody(p: Vec3, pitchDeg: number, rollDeg: number): Vec3 {
  const cr = Math.cos(rollDeg * DEG)
  const sr = Math.sin(rollDeg * DEG)
  const y1 = p.y * cr - p.z * sr
  const z1 = p.y * sr + p.z * cr

  const cp = Math.cos(pitchDeg * DEG)
  const sp = Math.sin(pitchDeg * DEG)
  return {
    x: p.x * cp + z1 * sp,
    y: y1,
    z: -p.x * sp + z1 * cp,
  }
}

/**
 * Tip the whole scene so the can is seen from slightly above and to one side.
 *
 * A dead-on view collapses a cylinder to a rectangle and loses every cue about which way
 * it is leaning — the exact information the model exists to show.
 */
export function viewTransform(p: Vec3, elevationDeg = 22): Vec3 {
  const ce = Math.cos(elevationDeg * DEG)
  const se = Math.sin(elevationDeg * DEG)

  // Camera looks along -y from above, with world +z as screen up. `y` is the screen-up
  // component; `z` is depth, larger being nearer the viewer.
  //
  // These two were swapped until 2026-08-19, which tipped the body's long axis into
  // screen depth instead of screen height: a CanSat standing upright rendered lying on
  // its side, and only looked upright at a roll of -90 deg. Found on hardware, because
  // every test here checked the body rotation and none checked which way was up.
  //
  // The negated x is the second half of that same bug, found the same way. With a plain
  // `x: p.x` this matrix has determinant -1 — a reflection, not a rotation, so the whole
  // scene rendered mirrored. Conjugating the body rotations through that mirror leaves
  // roll alone (its axis is normal to the mirror plane) and reverses pitch (its axis
  // lies in it), which is exactly the "roll matches, pitch doesn't" seen on hardware.
  // Screen right is therefore world -x, and the determinant is +1.
  //
  // The elevation signs are the THIRD fault in this one function, and the subtlest,
  // because it survives every check the other two failed. Determinant +1, axes correct,
  // nose at the top of the screen — and the camera still sat 22 deg BELOW the horizon
  // rather than above it, so the tail cap faced the viewer and an upright can read as
  // inverted. `se` therefore carries a minus in the screen-up row and a plus in depth:
  //
  //   right (0)  = (-1,   0,   0)
  //   up    (y)  = ( 0, -se,  ce)
  //   depth (z)  = ( 0,  ce,  se)   <- +se puts the nose nearer the viewer
  //
  // Any future change here should be checked against the whole camera basis, not one
  // row of it. Three separate sign errors have now shipped in these nine numbers.
  return {
    x: -p.x,
    y: p.z * ce - p.y * se,
    z: p.y * ce + p.z * se,
  }
}

/**
 * A rocket: nose cone, body tube, flat tail, and three swept fins.
 *
 * Replaces the placeholder cylinder (`status.md` Next 14). The cylinder carried two
 * ambiguities that only COLOUR resolved — it was rotationally symmetric about its own
 * axis, so roll was invisible without the stripe, and its silhouette was identical
 * upside down, so upright and inverted were told apart only by the nose and tail caps
 * being different colours.
 *
 * A cone and fins resolve both in the SHAPE. That matters beyond neatness: the panel is
 * read at a glance, in daylight, possibly by someone colourblind, and 068 removed the
 * reliability shading that used to carry meaning — so silhouette is now doing more of
 * the work than it was designed to.
 *
 * Three fins, to match the airframe this depicts rather than to suit the renderer.
 *
 * The stripe stays. Three fins are three-fold symmetric, so roll would still be ambiguous
 * modulo 120 deg without it — and exactly nose-on, the fins foreshorten to spokes that
 * look the same every third of a turn. Three is a weaker cue than four in that one
 * respect and the right number anyway: a model that does not match the vehicle teaches
 * the operator the wrong shape to look for.
 *
 * Radial extent is held at 0.42, the old cylinder's radius, so the silhouette is no
 * wider than what the canvas was already sized for. The body tube is narrower than the
 * cylinder was; the fins reach where its skin used to be.
 *
 * ⚠ THIS SOLID IS NOT CONVEX, and `projectMesh()` sorts by mean depth. See the note
 * there. The fins are what break it.
 */
export function rocketMesh(
  segments = 24,
  bodyRadius = 0.26,
  halfHeight = 1,
  stripeSegments = 2,
  finCount = 3,
  finSpan = 0.42,
): Face[] {
  const ring = (z: number, r: number): Vec3[] =>
    Array.from({ length: segments }, (_, i) => {
      const a = (i / segments) * Math.PI * 2
      return { x: Math.cos(a) * r, y: Math.sin(a) * r, z }
    })

  const tailZ = -halfHeight
  const shoulderZ = halfHeight * 0.5          // where the cone meets the tube
  const tail = ring(tailZ, bodyRadius)
  const shoulder = ring(shoulderZ, bodyRadius)
  const apex: Vec3 = { x: 0, y: 0, z: halfHeight }

  const faces: Face[] = []

  // Body tube, with the longitudinal stripe as before.
  for (let i = 0; i < segments; i++) {
    const j = (i + 1) % segments
    faces.push({
      points: [tail[i], tail[j], shoulder[j], shoulder[i]],
      kind: i < stripeSegments ? 'stripe' : 'body',
    })
  }

  // Nose cone. Every face is `nose`, so the whole front quarter carries the colour that
  // separates upright from inverted rather than just one flat cap.
  for (let i = 0; i < segments; i++) {
    const j = (i + 1) % segments
    faces.push({ points: [shoulder[i], shoulder[j], apex], kind: 'nose' })
  }

  // Tail as one polygon. Flat shading is enough at this size.
  faces.push({ points: [...tail].reverse(), kind: 'tail' })

  // Fins, swept back so the trailing edge is square with the tail and the leading edge
  // rakes forward. Zero thickness — there is no backface culling here and `faceLight()`
  // never returns 0, so a fin seen from behind shades darker rather than vanishing.
  //
  // `stripe` rather than a new kind: semantically these ARE the roll marker the stripe
  // was invented to be, and reusing the kind means shade() needs no new branch.
  const finRootLeadZ = tailZ + halfHeight * 0.58
  const finTipLeadZ = tailZ + halfHeight * 0.28
  for (let f = 0; f < finCount; f++) {
    const a = (f / finCount) * Math.PI * 2
    const ca = Math.cos(a)
    const sa = Math.sin(a)
    const at = (r: number, z: number): Vec3 => ({ x: ca * r, y: sa * r, z })
    faces.push({
      points: [
        at(bodyRadius, tailZ),        // root trailing
        at(finSpan, tailZ),           // tip trailing
        at(finSpan, finTipLeadZ),     // tip leading
        at(bodyRadius, finRootLeadZ), // root leading
      ],
      kind: 'stripe',
    })
  }

  return faces
}

export interface ProjectedFace {
  /** Screen-space polygon, before translation to the canvas centre. */
  points: { x: number; y: number }[]
  /** Mean depth. Larger is nearer the viewer. */
  depth: number
  /** 0 (facing away) to 1 (facing the light). */
  light: number
  kind: Face['kind']
}

/**
 * Rotate, tip, project, sort.
 *
 * Painter's algorithm — draw far faces first and let near ones cover them.
 *
 * ⚠ This was EXACT while the mesh was a cylinder, because sorting by depth is exact for
 * a convex solid and a cylinder is convex. `rocketMesh()` added fins, and a finned body
 * is not convex, so it is now an approximation.
 *
 * What that costs in practice is small and worth knowing rather than discovering. A fin
 * on the far side of the body sorts behind it and is correctly hidden; one on the near
 * side sorts in front and is correctly drawn over. The error case is a fin roughly
 * side-on, whose mean depth sits near the body axis while part of it is nearer than the
 * body's own skin — its root can be painted over at some angles. The fin is nearly
 * edge-on there and a few pixels wide, so it reads as a flicker at the silhouette rather
 * than as a wrong attitude.
 *
 * A depth buffer would fix it and is not worth 100 lines here. Splitting each fin at the
 * body radius would fix most of it and is the cheap option if it ever looks wrong.
 *
 * ⚠ Nobody has yet looked at this model in a browser (`status.md` Next 11), so the
 * paragraph above is reasoning, not observation.
 */
export function projectMesh(
  mesh: Face[],
  pitchDeg: number,
  rollDeg: number,
  scale: number,
  elevationDeg = 22,
): ProjectedFace[] {
  const projected = mesh.map((face) => {
    const world = face.points.map((p) =>
      viewTransform(rotateBody(p, pitchDeg, rollDeg), elevationDeg),
    )

    let depth = 0
    for (const p of world) depth += p.z
    depth /= world.length

    return {
      // Screen y grows downward; world y grows up.
      points: world.map((p) => ({ x: p.x * scale, y: -p.y * scale })),
      depth,
      light: faceLight(world),
      kind: face.kind,
    }
  })

  return projected.sort((a, b) => a.depth - b.depth)
}

/** Lambert term against a fixed light, so the shape reads as solid rather than flat. */
function faceLight(world: Vec3[]): number {
  if (world.length < 3) return 0.5

  const [a, b, c] = world
  const u = { x: b.x - a.x, y: b.y - a.y, z: b.z - a.z }
  const v = { x: c.x - a.x, y: c.y - a.y, z: c.z - a.z }
  const n = {
    x: u.y * v.z - u.z * v.y,
    y: u.z * v.x - u.x * v.z,
    z: u.x * v.y - u.y * v.x,
  }

  const length = Math.hypot(n.x, n.y, n.z)
  if (length === 0) return 0.5

  // Light from upper left, slightly toward the viewer.
  const lx = -0.45
  const ly = 0.7
  const lz = 0.55
  const dot = (n.x * lx + n.y * ly + n.z * lz) / length

  return Math.min(1, Math.max(0, (dot + 1) / 2))
}
