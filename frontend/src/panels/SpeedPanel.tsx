import { useMemo } from 'react'
import { Panel } from '../components/Panel'
import { Sparkline } from '../components/Sparkline'
import { verticalRate } from '../lib/rates'
import type { FrameRecord } from '../types/telemetry'

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
