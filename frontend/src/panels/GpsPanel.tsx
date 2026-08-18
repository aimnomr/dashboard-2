import { Panel } from '../components/Panel'
import { formatCoord, hasFix } from '../lib/geo'
import type { FrameRecord } from '../types/telemetry'

/** NEO-6M needs 4 satellites for a 3D fix; more means better geometry. */
function fixQuality(sat: number, fixed: boolean) {
  if (!fixed) return { label: 'No fix', icon: '■', tone: 'alert' as const }
  if (sat >= 7) return { label: 'Good', icon: '●', tone: 'ok' as const }
  if (sat >= 4) return { label: 'Weak', icon: '▲', tone: 'warn' as const }
  return { label: 'Poor', icon: '▲', tone: 'warn' as const }
}

export function GpsPanel({ latest }: { latest: FrameRecord | null }) {
  const frame = latest?.frame
  const fixed = frame ? hasFix(frame) : false
  const quality = fixQuality(frame?.sat ?? 0, fixed)

  return (
    <Panel title="GPS" area="gps" note={frame ? `${frame.sat} sat` : undefined}>
      <div className={`notice notice--${quality.tone}`} style={{ marginBottom: '0.5rem' }}>
        <span aria-hidden="true">{quality.icon}</span> {quality.label}
      </div>
      <div className="readout">
        <div>
          <span className="label">Latitude</span>
          <div className="value numeric">
            {frame && fixed ? formatCoord(frame.lat, 'N', 'S') : '—'}
          </div>
        </div>
        <div>
          <span className="label">Longitude</span>
          <div className="value numeric">
            {frame && fixed ? formatCoord(frame.lng, 'E', 'W') : '—'}
          </div>
        </div>
      </div>
      {frame && !fixed && (
        <p className="panel__footnote">
          Firmware reports 0.000000 when the fix is invalid — not a position.
        </p>
      )}
    </Panel>
  )
}
