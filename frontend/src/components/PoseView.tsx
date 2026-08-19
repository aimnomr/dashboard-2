import { useEffect, useRef } from 'react'
import { cylinderMesh, projectMesh, smoothAngle } from '../lib/pose'

interface PoseViewProps {
  /** Degrees, from the accelerometer. Null when no frame has arrived. */
  pitch: number | null
  roll: number | null
  /** False when the accelerometer is not a usable attitude reference right now. */
  reliable: boolean
}

/** Seconds for the model to cover most of the distance to a new sample. */
const SMOOTHING_TAU = 0.22

const MESH = cylinderMesh()

/**
 * The CanSat as a solid, eased between samples.
 *
 * Two rules this component is built around.
 *
 * **The model shows only what is measured.** Pitch and roll come from gravity, so they are
 * absolute, and they are the only two angles this draws. Yaw was integrated from the gyro
 * and shown as a tick on the outer ring until 2026-08-19; hardware testing retired it, and
 * `lib/attitude.ts` records why it is not coming back. The ring remains as a bezel, with
 * nothing on it.
 *
 * **Easing is presentation, not data.** At 1 Hz a model that snapped to each sample would
 * jump once a second. Between samples this is showing an interpolation, and it lags the
 * telemetry by about `SMOOTHING_TAU`. That is fine for a shape read at a glance and wrong
 * for a number, which is why the readouts beside it stay raw.
 */
export function PoseView({ pitch, roll, reliable }: PoseViewProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  // Targets live in a ref so the animation loop reads the latest values without being
  // torn down and restarted on every frame that arrives.
  const target = useRef({ pitch: 0, roll: 0, reliable: true, has: false })
  target.current = {
    pitch: pitch ?? 0,
    roll: roll ?? 0,
    reliable,
    has: pitch !== null && roll !== null,
  }

  useEffect(() => {
    const canvas = canvasRef.current
    const parent = canvas?.parentElement
    if (!canvas || !parent) return

    const shown = { pitch: 0, roll: 0 }
    let raf = 0
    let last = performance.now()

    const css = getComputedStyle(document.documentElement)
    const colour = (name: string) => css.getPropertyValue(name).trim()

    const draw = (now: number) => {
      const dt = Math.min((now - last) / 1000, 0.25)
      last = now

      const t = target.current
      shown.pitch = smoothAngle(shown.pitch, t.pitch, dt, SMOOTHING_TAU)
      shown.roll = smoothAngle(shown.roll, t.roll, dt, SMOOTHING_TAU)

      const dpr = window.devicePixelRatio || 1
      const width = parent.clientWidth
      const height = parent.clientHeight
      if (width > 0 && height > 0) {
        canvas.width = width * dpr
        canvas.height = height * dpr

        const ctx = canvas.getContext('2d')
        if (ctx) {
          ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
          ctx.clearRect(0, 0, width, height)
          render(ctx, width, height, shown, t.has, t.reliable, colour)
        }
      }

      raf = requestAnimationFrame(draw)
    }

    raf = requestAnimationFrame(draw)
    return () => cancelAnimationFrame(raf)
  }, [])

  return (
    <div className="canvas-host pose">
      <canvas ref={canvasRef} />
    </div>
  )
}

function render(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  shown: { pitch: number; roll: number },
  hasData: boolean,
  reliable: boolean,
  colour: (name: string) => string,
) {
  const cx = width / 2
  const cy = height / 2
  const radius = Math.min(width, height) / 2 - 4
  if (radius <= 0) return

  const scale = radius * 0.52

  ctx.save()
  ctx.translate(cx, cy)

  // Bezel. Drawn before the early return below, so the no-data state is a framed empty
  // dial rather than a blank rectangle.
  drawBezel(ctx, radius, colour)

  if (!hasData) {
    ctx.restore()
    return
  }

  // Ground reference. Without it a tilted cylinder floating in space is ambiguous —
  // there is nothing to be tilted relative to.
  drawHorizon(ctx, radius * 0.86, colour)

  const faces = projectMesh(MESH, shown.pitch, shown.roll, scale)

  for (const face of faces) {
    ctx.beginPath()
    ctx.moveTo(face.points[0].x, face.points[0].y)
    for (let i = 1; i < face.points.length; i++) {
      ctx.lineTo(face.points[i].x, face.points[i].y)
    }
    ctx.closePath()

    ctx.fillStyle = shade(face.kind, face.light, reliable)
    ctx.fill()

    // Hairline in the fill colour: closes the seams between adjacent quads without
    // drawing a wireframe over the solid.
    ctx.strokeStyle = ctx.fillStyle
    ctx.lineWidth = 0.7
    ctx.stroke()
  }

  ctx.restore()
}

/**
 * Desaturated to grey when the accelerometer is not a usable reference — under boost, in
 * freefall, or tumbling. An unreliable pose must not look like a confident one.
 */
function shade(
  kind: 'body' | 'stripe' | 'nose' | 'tail',
  light: number,
  reliable: boolean,
): string {
  const level = 0.35 + light * 0.6
  const mix = (base: [number, number, number]) =>
    `rgb(${Math.round(base[0] * level)}, ${Math.round(base[1] * level)}, ${Math.round(base[2] * level)})`

  // Greyed when the accelerometer is not a usable reference — under boost, in freefall,
  // or tumbling. An unreliable pose must not look like a confident one, and the shape
  // carries that as plainly as the warning text below it does.
  if (!reliable) {
    const g = Math.round(150 + level * 70)
    return `rgb(${g}, ${g}, ${g + 4})`
  }

  switch (kind) {
    case 'stripe':
      return mix([215, 70, 70])
    // Light nose, dark tail: the only cue separating upright from inverted, since the
    // silhouette is identical either way.
    case 'nose':
      return mix([245, 250, 255])
    case 'tail':
      return mix([90, 105, 130])
    default:
      return mix([150, 180, 225])
  }
}

function drawHorizon(ctx: CanvasRenderingContext2D, halfWidth: number, colour: (n: string) => string) {
  ctx.save()
  ctx.strokeStyle = colour('--rule')
  ctx.lineWidth = 1
  ctx.beginPath()
  ctx.ellipse(0, 0, halfWidth, halfWidth * 0.34, 0, 0, Math.PI * 2)
  ctx.stroke()
  ctx.restore()
}

/**
 * The dashed outer ring.
 *
 * It carried the yaw tick until 2026-08-19 and now carries nothing. Kept because it is
 * also the frame for the no-data state, which would otherwise be an empty rectangle —
 * and because a bezel with nothing on it reads as "no more information here", which is
 * true.
 */
function drawBezel(
  ctx: CanvasRenderingContext2D,
  radius: number,
  colour: (n: string) => string,
) {
  ctx.save()
  ctx.strokeStyle = colour('--rule')
  ctx.lineWidth = 1
  ctx.setLineDash([2, 3])
  ctx.beginPath()
  ctx.arc(0, 0, radius, 0, Math.PI * 2)
  ctx.stroke()
  ctx.setLineDash([])
  ctx.restore()
}
