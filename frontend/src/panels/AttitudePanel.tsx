import { useEffect, useRef, useState } from 'react'
import { Panel } from '../components/Panel'
import { PoseView } from '../components/PoseView'
import { computeAttitude } from '../lib/attitude'
import type { FrameRecord } from '../types/telemetry'

type Mode = 'model' | 'horizon'

interface AttitudePanelProps {
  latest: FrameRecord | null
}

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

      // Desaturated when the reading is not trustworthy, so an unreliable horizon does
      // not look like a confident one.
      ctx.fillStyle = attitude.reliable ? '#dbeafe' : colour('--unknown-bg')
      ctx.fillRect(-radius * 2, -radius * 2 + horizonY, radius * 4, radius * 2)
      ctx.fillStyle = attitude.reliable ? '#d6ccc0' : '#e4e4e7'
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
          <PoseView
            pitch={attitude?.pitch ?? null}
            roll={attitude?.roll ?? null}
            reliable={attitude?.reliable ?? true}
          />
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

      {/* An accelerometer measures gravity plus vehicle acceleration. Under boost or in
          freefall the horizon is measuring thrust or nothing, so say so rather than
          draw a confident attitude from meaningless numbers. */}
      {attitude && !attitude.reliable && (
        <div className="notice notice--warn">
          <span aria-hidden="true">▲</span> Attitude unreliable — {attitude.reason}
        </div>
      )}
    </Panel>
  )
}
