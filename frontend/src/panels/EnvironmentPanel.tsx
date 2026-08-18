import { useMemo } from 'react'
import { Panel } from '../components/Panel'
import { Sparkline } from '../components/Sparkline'
import type { FrameRecord } from '../types/telemetry'

const TREND_SAMPLES = 120

export function EnvironmentPanel({ history, latest }: { history: FrameRecord[]; latest: FrameRecord | null }) {
  const trends = useMemo(() => {
    const window = history.slice(-TREND_SAMPLES)
    return {
      temp: window.map((r) => r.frame.temp),
      hum: window.map((r) => r.frame.hum),
      pres: window.map((r) => r.frame.pres),
    }
  }, [history])

  const frame = latest?.frame

  return (
    <Panel title="Environment" area="env">
      <div className="env">
        <div className="env__item">
          <span className="label">Temp</span>
          <div className="value numeric">
            {frame ? frame.temp.toFixed(1) : '—'}
            <span className="env__unit">°C</span>
          </div>
          <Sparkline values={trends.temp} stroke="var(--trace-2)" height={22} minSpan={1} />
        </div>
        <div className="env__item">
          <span className="label">Humidity</span>
          <div className="value numeric">
            {frame ? frame.hum.toFixed(0) : '—'}
            <span className="env__unit">%</span>
          </div>
          <Sparkline values={trends.hum} stroke="var(--trace-3)" height={22} minSpan={2} />
        </div>
        <div className="env__item">
          <span className="label">Pressure</span>
          <div className="value numeric">
            {frame ? frame.pres.toFixed(1) : '—'}
            <span className="env__unit">hPa</span>
          </div>
          {/* Pressure is the independent cross-check on altitude: both come from the
              BME280, but a pressure trend that disagrees with the altitude trace means
              one of them is wrong. */}
          <Sparkline values={trends.pres} stroke="var(--trace)" height={22} minSpan={1} />
        </div>
      </div>
    </Panel>
  )
}
