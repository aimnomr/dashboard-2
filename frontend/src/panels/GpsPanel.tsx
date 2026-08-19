import { Panel } from '../components/Panel'
import { fixStale, formatCoord, hasFix } from '../lib/geo'
import type { FrameRecord, TelemetryFrame } from '../types/telemetry'

/**
 * NEO-6M needs 4 satellites for a 3D fix; more means better geometry.
 *
 * Satellite count is a proxy for quality, not a measurement of it — what actually
 * governs accuracy is the satellites' geometry (HDOP), which the vehicle does not
 * send. Ten bunched in one patch of sky are worse than five well spread.
 */
function fixQuality(frame: TelemetryFrame | undefined, fixed: boolean, stale: boolean) {
  if (!frame || !fixed) return { label: 'No fix', icon: '■', tone: 'alert' as const }

  // Alert, not warn. A stale position is worse than no position: it is wrong AND it
  // looks right, and it is the coordinate somebody would drive to.
  if (stale) {
    const why = frame.fixq === 0 ? 'receiver reports invalid' : '0 satellites'
    return { label: `Stale — ${why}`, icon: '■', tone: 'alert' as const }
  }

  // HDOP first when the vehicle sends it (GEN3.1). It is the measurement; the satellite
  // count is the proxy. They usually agree, and when they do not, this one is right.
  const { hdop, sat } = frame
  if (hdop !== null && hdop > 0) {
    if (hdop <= 2) return { label: `Good · HDOP ${hdop.toFixed(1)}`, icon: '●', tone: 'ok' as const }
    if (hdop <= 5) return { label: `Fair · HDOP ${hdop.toFixed(1)}`, icon: '▲', tone: 'warn' as const }
    return { label: `Poor · HDOP ${hdop.toFixed(1)}`, icon: '▲', tone: 'warn' as const }
  }

  if (sat >= 7) return { label: 'Good', icon: '●', tone: 'ok' as const }
  if (sat >= 4) return { label: 'Weak', icon: '▲', tone: 'warn' as const }
  return { label: 'Poor', icon: '▲', tone: 'warn' as const }
}

export function GpsPanel({ latest }: { latest: FrameRecord | null }) {
  const frame = latest?.frame
  const fixed = frame ? hasFix(frame) : false
  const stale = frame ? fixStale(frame) : false
  const quality = fixQuality(frame, fixed, stale)

  return (
    <Panel
      title="GPS"
      area="gps"
      note={
        frame
          ? frame.hdop !== null && frame.hdop > 0
            ? `${frame.sat} sat · HDOP ${frame.hdop.toFixed(1)}`
            : `${frame.sat} sat`
          : undefined
      }
    >
      <div className={`notice notice--${quality.tone}`} style={{ marginBottom: '0.5rem' }}>
        <span aria-hidden="true">{quality.icon}</span> {quality.label}
      </div>
      <div className="readout">
        <div>
          <span className="label">Latitude</span>
          <div className="value numeric">
            {frame && fixed && !stale ? formatCoord(frame.lat, 'N', 'S') : '—'}
          </div>
        </div>
        <div>
          <span className="label">Longitude</span>
          <div className="value numeric">
            {frame && fixed && !stale ? formatCoord(frame.lng, 'E', 'W') : '—'}
          </div>
        </div>
      </div>
      {frame && !fixed && (
        <p className="panel__footnote">
          Firmware reports 0.000000 when the fix is invalid — not a position.
        </p>
      )}
      {stale && (
        <p className="panel__footnote">
          Position withheld: the vehicle reports coordinates with 0 satellites, so the
          last figure it sent is not current. Last known track is on the ground plot.
        </p>
      )}
    </Panel>
  )
}
