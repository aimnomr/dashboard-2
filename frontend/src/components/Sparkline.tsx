import { useMemo } from 'react'

interface SparklineProps {
  values: number[]
  stroke?: string
  height?: number
  /** Keeps a flat line sitting mid-box instead of snapping to a random edge. */
  minSpan?: number
}

/** Small trend line. One SVG path — cheap enough to run several per screen at 1 Hz. */
export function Sparkline({
  values,
  stroke = 'var(--trace)',
  height = 28,
  minSpan = 0.5,
}: SparklineProps) {
  const path = useMemo(() => {
    if (values.length < 2) return null
    let min = Infinity
    let max = -Infinity
    for (const v of values) {
      if (v < min) min = v
      if (v > max) max = v
    }
    const span = Math.max(max - min, minSpan)
    const mid = (min + max) / 2
    const lo = mid - span / 2

    const step = 100 / (values.length - 1)
    let d = ''
    for (let i = 0; i < values.length; i++) {
      const x = i * step
      const y = 100 - ((values[i] - lo) / span) * 100
      d += `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
    }
    return d
  }, [values, minSpan])

  return (
    <svg
      className="sparkline"
      viewBox="0 0 100 100"
      preserveAspectRatio="none"
      style={{ height }}
      aria-hidden="true"
    >
      {path && <path d={path} fill="none" stroke={stroke} strokeWidth={3} vectorEffect="non-scaling-stroke" />}
    </svg>
  )
}
