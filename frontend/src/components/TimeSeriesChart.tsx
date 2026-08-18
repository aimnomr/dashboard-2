import { useEffect, useRef } from 'react'
import uPlot from 'uplot'
import 'uplot/dist/uPlot.min.css'

export interface TimeSeriesChartProps {
  /** Seconds since the first frame. */
  x: number[]
  y: (number | null)[]
  stroke?: string
  fill?: string
  yLabel?: string
  /** Keeps the axis from collapsing to nothing while the vehicle sits on the pad. */
  minSpan?: number
}

/**
 * uPlot wrapper.
 *
 * Canvas rather than SVG: a 30-minute flight at 1 Hz is ~1800 points per series, and
 * several of these run at once. SVG libraries bog down at that scale, which is the one
 * moment the dashboard must not stutter.
 */
export function TimeSeriesChart({
  x,
  y,
  stroke = 'var(--trace)',
  fill,
  yLabel,
  minSpan = 10,
}: TimeSeriesChartProps) {
  const hostRef = useRef<HTMLDivElement | null>(null)
  const plotRef = useRef<uPlot | null>(null)
  const dataRef = useRef<uPlot.AlignedData>([x, y] as uPlot.AlignedData)

  dataRef.current = [x, y] as uPlot.AlignedData

  useEffect(() => {
    const host = hostRef.current
    if (!host) return

    const css = getComputedStyle(document.documentElement)
    const resolve = (value: string) =>
      value.startsWith('var(')
        ? css.getPropertyValue(value.slice(4, -1)).trim() || '#1d4ed8'
        : value

    const options: uPlot.Options = {
      width: host.clientWidth || 300,
      height: host.clientHeight || 150,
      padding: [8, 12, 0, 0],
      cursor: { show: true, y: false },
      legend: { show: false },
      scales: {
        x: { time: false },
        y: {
          range: (_u, min, max) => {
            if (!Number.isFinite(min) || !Number.isFinite(max)) return [0, minSpan]
            const span = Math.max(max - min, minSpan)
            const pad = span * 0.12
            return [min - pad, max + pad]
          },
        },
      },
      series: [
        {},
        {
          label: yLabel ?? 'value',
          stroke: resolve(stroke),
          width: 2.5,
          fill: fill ? resolve(fill) : undefined,
          points: { show: false },
          spanGaps: false,
        },
      ],
      axes: [
        {
          stroke: resolve('var(--text-dim)'),
          grid: { stroke: resolve('var(--grid)'), width: 1 },
          ticks: { stroke: resolve('var(--grid)') },
          font: '11px ui-sans-serif, system-ui, sans-serif',
          values: (_u, splits) => splits.map((s) => `${Math.round(s)}s`),
        },
        {
          stroke: resolve('var(--text-dim)'),
          grid: { stroke: resolve('var(--grid)'), width: 1 },
          ticks: { stroke: resolve('var(--grid)') },
          font: '11px ui-sans-serif, system-ui, sans-serif',
          size: 44,
        },
      ],
    }

    const plot = new uPlot(options, dataRef.current, host)
    plotRef.current = plot

    const observer = new ResizeObserver(() => {
      plot.setSize({ width: host.clientWidth, height: host.clientHeight })
    })
    observer.observe(host)

    return () => {
      observer.disconnect()
      plot.destroy()
      plotRef.current = null
    }
    // Options are built once; data flows through setData below.
  }, [stroke, fill, yLabel, minSpan])

  useEffect(() => {
    plotRef.current?.setData(dataRef.current)
  }, [x, y])

  return <div className="chart" ref={hostRef} />
}
