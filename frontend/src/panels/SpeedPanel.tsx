import { useMemo } from 'react'
import { Panel } from '../components/Panel'
import { Sparkline } from '../components/Sparkline'
import type { FrameRecord } from '../types/telemetry'

/** Samples averaged for the vertical rate. At 1 Hz this is a ~5 s window. */
const RATE_WINDOW = 5

/**
 * Vertical rate from the altitude trend.
 *
 * More useful than GPS ground speed during descent — it is what says whether the chute
 * is working. Derived over a window because per-sample differences at 1 Hz are mostly
 * barometric noise.
 *
 * Timing comes from PC arrival time, not the vehicle (ISS-08), so the rate inherits
 * whatever jitter the link and USB hop introduce.
 */
function verticalRate(history: FrameRecord[]): number | null {
  if (history.length < 2) return null
  const window = history.slice(-RATE_WINDOW)
  const first = window[0]
  const last = window[window.length - 1]
  const seconds = (last.t - first.t) / 1000
  if (seconds <= 0) return null
  return (last.frame.alt - first.frame.alt) / seconds
}

export function SpeedPanel({ history, latest }: { history: FrameRecord[]; latest: FrameRecord | null }) {
  const rate = useMemo(() => verticalRate(history), [history])
  const trend = useMemo(() => history.slice(-120).map((r) => r.frame.spd), [history])

  const descending = rate !== null && rate < -0.5
  const climbing = rate !== null && rate > 0.5

  return (
    <Panel title="Speed" area="speed">
      <div className="readout readout--stack">
        <div>
          <span className="label">Vertical rate</span>
          <div className="value numeric" style={{ fontSize: '1.75rem' }}>
            {rate !== null ? `${rate > 0 ? '+' : ''}${rate.toFixed(1)}` : '—'}
            <span style={{ fontSize: '0.5em', color: 'var(--text-dim)' }}> m/s</span>
          </div>
          <span className="panel__footnote">
            {descending ? 'descending' : climbing ? 'climbing' : rate !== null ? 'level' : ' '}
          </span>
        </div>
        <div>
          <span className="label">Ground speed (GPS)</span>
          <div className="value numeric">
            {latest ? latest.frame.spd.toFixed(1) : '—'}
            <span style={{ fontSize: '0.55em', color: 'var(--text-dim)' }}> km/h</span>
          </div>
        </div>
      </div>
      <Sparkline values={trend} stroke="var(--trace-3)" height={26} minSpan={2} />
    </Panel>
  )
}
