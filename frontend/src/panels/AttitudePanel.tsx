import { useEffect, useRef, useState } from 'react'
import { Panel } from '../components/Panel'
import { PoseView } from '../components/PoseView'
import { computeAttitude } from '../lib/attitude'
import type { FrameRecord } from '../types/telemetry'

type Mode = 'model' | 'horizon'

interface AttitudePanelProps {
  latest: FrameRecord | null
}

/**
 * Pitch, roll and spin, as a model or as a horizon.
 *
 * ⚠ This panel makes no judgement about the attitude it shows, and nothing in it
 * discerns one pose from another. Until 2026-09-09 it desaturated the horizon, greyed the
 * model and raised an `Attitude unreliable — <reason>` banner whenever the accelerometer
 * stopped being a usable attitude reference — tumbling above 90 deg/s, or magnitude more
 * than 0.25 g away from 1 g, which is to say under boost and in freefall. All three are
 * gone: every attitude now renders exactly as any other does.
 *
 * `computeAttitude()` still returns `reliable` and `reason`, and `attitudeWarning()` is
 * still exported and still tested. This panel reads neither. That is deliberate — the
 * thresholds and the two-frame confirmation are measured from real logs and worth
 * keeping, and restoring the display is a smaller change than rebuilding it.
 *
 * What the verdict was computed FROM stays on screen either way: magnitude as the `g`
 * figure in the note, and spin rate in the readout. The operator reads the numbers and
 * draws their own conclusion.
 */
export function AttitudePanel({ latest }: AttitudePanelProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const attitude = latest ? computeAttitude(latest.frame) : null
  // The model reads better for a body that can be at any orientation; the horizon is
  // the familiar instrument. Both are the same numbers, so this is a preference and
  // deliberately not persisted.
  const [mode, setMode] = useState<Mode>('model')

  useEffect(() => {
    const canvas = canvasRef.current
    const parent = canvas?.parentElement
    if (!canvas || !parent) return

    const draw = () => {
      const dpr = window.devicePixelRatio || 1
      const width = parent.clientWidth
      const height = parent.clientHeight
      if (width === 0 || height === 0) return
      canvas.width = width * dpr
      canvas.height = height * dpr

      const ctx = canvas.getContext('2d')
      if (!ctx) return
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, width, height)

      const css = getComputedStyle(document.documentElement)
      const colour = (name: string) => css.getPropertyValue(name).trim()

      const cx = width / 2
      const cy = height / 2
      const radius = Math.min(width, height) / 2 - 6
      if (radius <= 0) return

      ctx.save()
      ctx.beginPath()
      ctx.arc(cx, cy, radius, 0, Math.PI * 2)
      ctx.clip()

      if (!attitude) {
        ctx.fillStyle = colour('--unknown-bg')
        ctx.fillRect(0, 0, width, height)
        ctx.restore()
        ctx.strokeStyle = colour('--rule')
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.arc(cx, cy, radius, 0, Math.PI * 2)
        ctx.stroke()
        return
      }

      ctx.translate(cx, cy)
      ctx.rotate((-attitude.roll * Math.PI) / 180)
      const horizonY = attitude.pitch * (radius / 45)

      // Sky and ground, at the same two colours for every attitude. See the note on the
      // component: the desaturated variant these used to switch to is gone.
      ctx.fillStyle = '#dbeafe'
      ctx.fillRect(-radius * 2, -radius * 2 + horizonY, radius * 4, radius * 2)
      ctx.fillStyle = '#d6ccc0'
      ctx.fillRect(-radius * 2, horizonY, radius * 4, radius * 2)

      ctx.strokeStyle = colour('--text')
      ctx.lineWidth = 2
      ctx.beginPath()
      ctx.moveTo(-radius * 2, horizonY)
      ctx.lineTo(radius * 2, horizonY)
      ctx.stroke()
      ctx.restore()

      // Fixed vehicle reference, drawn over the moving horizon.
      ctx.strokeStyle = colour('--text')
      ctx.lineWidth = 3
      ctx.beginPath()
      ctx.moveTo(cx - radius * 0.45, cy)
      ctx.lineTo(cx - radius * 0.12, cy)
      ctx.moveTo(cx + radius * 0.12, cy)
      ctx.lineTo(cx + radius * 0.45, cy)
      ctx.stroke()
      ctx.beginPath()
      ctx.arc(cx, cy, 2.5, 0, Math.PI * 2)
      ctx.fill()

      ctx.strokeStyle = colour('--rule-strong')
      ctx.lineWidth = 2
      ctx.beginPath()
      ctx.arc(cx, cy, radius, 0, Math.PI * 2)
      ctx.stroke()
    }

    draw()
    const observer = new ResizeObserver(draw)
    observer.observe(parent)
    return () => observer.disconnect()
    // `mode` matters: the canvas is unmounted in model mode, so switching back needs a
    // redraw rather than waiting up to a second for the next frame to arrive.
  }, [attitude, mode])

  return (
    <Panel
      title="Attitude"
      area="attitude"
      note={
        <span className="attitude__note">
          <button
            type="button"
            className="attitude__mode"
            onClick={() => setMode(mode === 'model' ? 'horizon' : 'model')}
            title="Same numbers, different representation"
          >
            {mode === 'model' ? 'Model' : 'Horizon'}
          </button>
          {attitude ? `${attitude.magnitude.toFixed(2)} g` : '—'}
        </span>
      }
    >
      <div className="attitude">
        {mode === 'model' ? (
          <PoseView pitch={attitude?.pitch ?? null} roll={attitude?.roll ?? null} />
        ) : (
          <div className="canvas-host attitude__dial">
            <canvas ref={canvasRef} />
          </div>
        )}
        <div className="attitude__readout">
          <div>
            <span className="label">Pitch</span>
            <div className="value numeric">
              {attitude ? `${attitude.pitch.toFixed(0)}°` : '—'}
            </div>
          </div>
          <div>
            <span className="label">Roll</span>
            <div className="value numeric">
              {attitude ? `${attitude.roll.toFixed(0)}°` : '—'}
            </div>
          </div>
          <div>
            <span className="label">Spin</span>
            <div className="value numeric">
              {attitude ? `${attitude.spinRate.toFixed(0)}` : '—'}
              <span style={{ fontSize: '0.55em', color: 'var(--text-dim)' }}> °/s</span>
            </div>
          </div>
        </div>
      </div>
    </Panel>
  )
}
