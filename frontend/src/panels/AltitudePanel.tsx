import { useMemo } from 'react'
import { Panel } from '../components/Panel'
import { TimeSeriesChart } from '../components/TimeSeriesChart'
import { applyBreaks, timebase } from '../lib/timebase'
import type { FrameRecord } from '../types/telemetry'

interface AltitudePanelProps {
  history: FrameRecord[]
  latest: FrameRecord | null
}

export function AltitudePanel({ history, latest }: AltitudePanelProps) {
  const { x, y, peak, clock } = useMemo(() => {
    const base = timebase(history)
    const ys = new Array<number | null>(history.length)
    let max = Number.NEGATIVE_INFINITY
    for (let i = 0; i < history.length; i++) {
      const alt = history[i].frame.alt
      ys[i] = alt
      if (alt > max) max = alt
    }
    return {
      x: base.seconds,
      y: applyBreaks(ys, base.restarts),
      peak: Number.isFinite(max) ? max : null,
      clock: base.label,
    }
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
        relative to launch · x: {clock}
      </div>
      <TimeSeriesChart
        x={x}
        series={[{ label: 'alt', values: y, stroke: 'var(--trace)' }]}
        minSpan={20}
      />
    </Panel>
  )
}
