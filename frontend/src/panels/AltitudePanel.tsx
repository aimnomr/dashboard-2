import { useMemo } from 'react'
import { Panel } from '../components/Panel'
import { TimeSeriesChart } from '../components/TimeSeriesChart'
import type { FrameRecord } from '../types/telemetry'

interface AltitudePanelProps {
  history: FrameRecord[]
  latest: FrameRecord | null
}

export function AltitudePanel({ history, latest }: AltitudePanelProps) {
  const { x, y, peak } = useMemo(() => {
    const xs = new Array<number>(history.length)
    const ys = new Array<number>(history.length)
    let max = Number.NEGATIVE_INFINITY
    for (let i = 0; i < history.length; i++) {
      xs[i] = history[i].t / 1000
      const alt = history[i].frame.alt
      ys[i] = alt
      if (alt > max) max = alt
    }
    return { x: xs, y: ys, peak: Number.isFinite(max) ? max : null }
  }, [history])

  return (
    <Panel
      title="Altitude"
      area="altitude"
      note={peak !== null ? `peak ${peak.toFixed(1)} m` : undefined}
    >
      <div className="hero numeric" style={{ marginBottom: '0.5rem' }}>
        {latest ? latest.frame.alt.toFixed(1) : '—'}
        <span className="hero__unit">m</span>
      </div>
      {/* Relative to boot altitude, not sea level — worth stating on the panel so it
          is not read as AMSL during a flight. */}
      <div
        style={{
          fontSize: 'var(--size-label)',
          color: 'var(--text-faint)',
          marginBottom: '0.375rem',
        }}
      >
        relative to launch
      </div>
      <TimeSeriesChart x={x} y={y} yLabel="alt" stroke="var(--trace)" minSpan={20} />
    </Panel>
  )
}
