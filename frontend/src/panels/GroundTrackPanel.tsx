import { useEffect, useMemo, useRef } from 'react'
import { Panel } from '../components/Panel'
import { hasLiveFix, niceScale, toLocal } from '../lib/geo'
import type { FrameRecord } from '../types/telemetry'

interface GroundTrackPanelProps {
  history: FrameRecord[]
  latest: FrameRecord | null
}

/**
 * Ground track as a plain XY trace in metres from the first fix.
 *
 * No basemap: there is no internet at the launch site, so tiles would render as blank
 * grey squares exactly when they are needed (ISS-11). A trace with a marked launch
 * point and a scale bar carries the information that actually matters — where the
 * vehicle is relative to where it went up — and works anywhere.
 */
export function GroundTrackPanel({ history, latest }: GroundTrackPanelProps) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)

  const points = useMemo(() => {
    // hasLiveFix, not hasFix: a stale position repeats the same coordinate every
    // second, which would pin the trace's last point somewhere the vehicle has left
    // and quietly bias the drift figure toward it.
    const fixes = history.filter((r) => hasLiveFix(r.frame))
    if (fixes.length === 0) return []
    const origin = fixes[0].frame
    return fixes.map((r) => toLocal(r.frame, origin))
  }, [history])

  const drift = useMemo(() => {
    if (points.length === 0) return null
    const last = points[points.length - 1]
    return Math.hypot(last.x, last.y)
  }, [points])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const parent = canvas.parentElement
    if (!parent) return

    const draw = () => {
      const dpr = window.devicePixelRatio || 1
      const width = parent.clientWidth
      const height = parent.clientHeight
      canvas.width = width * dpr
      canvas.height = height * dpr
      canvas.style.width = `${width}px`
      canvas.style.height = `${height}px`

      const ctx = canvas.getContext('2d')
      if (!ctx) return
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, width, height)

      const css = getComputedStyle(document.documentElement)
      const colour = (name: string) => css.getPropertyValue(name).trim()

      if (points.length === 0) {
        ctx.fillStyle = colour('--text-faint')
        ctx.font = '12px ui-sans-serif, system-ui, sans-serif'
        ctx.textAlign = 'center'
        ctx.fillText('No GPS fix', width / 2, height / 2)
        return
      }

      // Square aspect so the track is not distorted — a stretched trace misreads as
      // drift in a direction the vehicle never went.
      let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity
      for (const p of points) {
        if (p.x < minX) minX = p.x
        if (p.x > maxX) maxX = p.x
        if (p.y < minY) minY = p.y
        if (p.y > maxY) maxY = p.y
      }
      const pad = 18
      const span = Math.max(maxX - minX, maxY - minY, 20)
      const cx = (minX + maxX) / 2
      const cy = (minY + maxY) / 2
      const scale = (Math.min(width, height) - pad * 2) / span

      const px = (x: number) => width / 2 + (x - cx) * scale
      const py = (y: number) => height / 2 - (y - cy) * scale

      ctx.strokeStyle = colour('--trace')
      ctx.lineWidth = 2
      ctx.lineJoin = 'round'
      ctx.beginPath()
      points.forEach((p, i) => (i === 0 ? ctx.moveTo(px(p.x), py(p.y)) : ctx.lineTo(px(p.x), py(p.y))))
      ctx.stroke()

      // Launch point — cross.
      const originX = px(0)
      const originY = py(0)
      ctx.strokeStyle = colour('--text')
      ctx.lineWidth = 2
      ctx.beginPath()
      ctx.moveTo(originX - 6, originY)
      ctx.lineTo(originX + 6, originY)
      ctx.moveTo(originX, originY - 6)
      ctx.lineTo(originX, originY + 6)
      ctx.stroke()

      // Current position — filled dot.
      const last = points[points.length - 1]
      ctx.fillStyle = colour('--trace-2')
      ctx.beginPath()
      ctx.arc(px(last.x), py(last.y), 5, 0, Math.PI * 2)
      ctx.fill()

      // Scale bar.
      const bar = niceScale(span)
      const barPx = bar * scale
      const bx = pad
      const by = height - pad
      ctx.strokeStyle = colour('--text')
      ctx.lineWidth = 2
      ctx.beginPath()
      ctx.moveTo(bx, by)
      ctx.lineTo(bx + barPx, by)
      ctx.moveTo(bx, by - 4)
      ctx.lineTo(bx, by + 4)
      ctx.moveTo(bx + barPx, by - 4)
      ctx.lineTo(bx + barPx, by + 4)
      ctx.stroke()
      ctx.fillStyle = colour('--text-dim')
      ctx.font = '11px ui-sans-serif, system-ui, sans-serif'
      ctx.textAlign = 'left'
      ctx.fillText(`${bar} m`, bx + barPx + 6, by + 4)
    }

    draw()
    const observer = new ResizeObserver(draw)
    observer.observe(parent)
    return () => observer.disconnect()
  }, [points])

  const fixed = latest ? hasLiveFix(latest.frame) : false

  return (
    <Panel
      title="Ground track"
      area="track"
      note={drift !== null ? `${drift.toFixed(0)} m from launch` : 'no fix'}
    >
      <div className="canvas-host">
        <canvas ref={canvasRef} />
        {!fixed && points.length > 0 && (
          <span className="canvas-host__badge chip chip--warn">▲ Fix lost</span>
        )}
      </div>
    </Panel>
  )
}
