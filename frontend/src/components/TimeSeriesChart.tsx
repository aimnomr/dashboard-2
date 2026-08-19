import { useEffect, useMemo, useRef } from 'react'
import uPlot from 'uplot'
import 'uplot/dist/uPlot.min.css'

export interface ChartSeries {
  label: string
  /** Null renders as a break in the line, never as zero. */
  values: (number | null)[]
  stroke: string
  fill?: string
}

export interface TimeSeriesChartProps {
  /** Seconds since the first frame. */
  x: number[]
  series: ChartSeries[]
  /** Keeps the axis from collapsing to nothing while the vehicle sits on the pad. */
  minSpan?: number
  /** Shown when more than one series would otherwise be unidentifiable. */
  legend?: boolean
  /** Unit shown beside the legend, e.g. "g" or "deg/s". */
  unit?: string
}

/**
 * uPlot wrapper.
 *
 * Canvas rather than SVG: a 30-minute flight at 1 Hz is ~1800 points per series, and
 * several of these run at once. SVG libraries bog down at that scale, which is the one
 * moment the dashboard must not stutter.
 *
 * The legend is drawn here rather than by uPlot so it can use the project's own colour
 * tokens and stay legible at the small sizes the channels view uses.
 */
export function TimeSeriesChart({
  x,
  series,
  minSpan = 10,
  legend = false,
  unit,
}: TimeSeriesChartProps) {
  const hostRef = useRef<HTMLDivElement | null>(null)
  const plotRef = useRef<uPlot | null>(null)

  const data = useMemo(
    () => [x, ...series.map((s) => s.values)] as uPlot.AlignedData,
    [x, series],
  )
  const dataRef = useRef<uPlot.AlignedData>(data)
  dataRef.current = data

  // Rebuilding the plot on every render would throw away uPlot's canvas each second.
  // Only a change in the SHAPE of the chart justifies it; data flows through setData.
  const shape = series.map((s) => `${s.label}:${s.stroke}:${s.fill ?? ''}`).join('|')

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
        ...series.map((s) => ({
          label: s.label,
          stroke: resolve(s.stroke),
          width: 2.5,
          fill: s.fill ? resolve(s.fill) : undefined,
          points: { show: false },
          // False, so a dropped packet leaves a visible break. Joining across it would
          // draw a straight line through time the vehicle was not observed.
          spanGaps: false,
        })),
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
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shape, minSpan])

  useEffect(() => {
    plotRef.current?.setData(dataRef.current)
  }, [data])

  return (
    <div className="chart-block">
      {legend && (
        <div className="chart-legend">
          {series.map((s) => (
            <span className="chart-legend__item" key={s.label}>
              <span
                className="chart-legend__swatch"
                style={{ background: s.stroke }}
                aria-hidden="true"
              />
              {s.label}
            </span>
          ))}
          {unit && <span className="chart-legend__unit">{unit}</span>}
        </div>
      )}
      <div className="chart" ref={hostRef} />
    </div>
  )
}
