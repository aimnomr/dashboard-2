import type { TelemetryState } from '../hooks/useTelemetry'
import type { View } from '../types/telemetry'
import {
  chutePresentation,
  formatAge,
  formatMeasurement,
  LINK_PRESENTATION,
  linkState,
} from '../lib/link'

interface StatusBarProps {
  telemetry: TelemetryState
  now: number
  view: View
  onViewChange: (view: View) => void
}

export function StatusBar({ telemetry, now, view, onViewChange }: StatusBarProps) {
  const { session, latest, lastMessageAt, counts, connection } = telemetry
  const state = linkState(lastMessageAt, now)
  const link = LINK_PRESENTATION[state]
  const chute = chutePresentation(latest?.frame.chute)

  return (
    <div className="statusbar">
      {session?.simulated && (
        <div className="simbanner" role="status">
          <span aria-hidden="true">▲</span>
          <span>Simulated data — not a real flight</span>
        </div>
      )}

      <div
        className={`statusbar__item state-box state-box--${link.tone} ${
          state === 'lost' ? 'is-pulsing' : ''
        }`}
      >
        <span className={`chip chip--${link.tone}`}>
          <span aria-hidden="true">{link.icon}</span>
          {link.label}
        </span>
        <span className="value numeric">{formatAge(lastMessageAt, now)}</span>
      </div>

      <div className={`statusbar__item state-box state-box--${chute.tone}`}>
        <span className="label">Chute</span>
        <span className={`chip chip--${chute.tone}`}>
          <span aria-hidden="true">{chute.icon}</span>
          {chute.label}
        </span>
      </div>

      <div className="statusbar__item">
        <span className="label">RSSI</span>
        <span className="value numeric">
          {formatMeasurement(latest?.frame.rssi, 0)}
          <span style={{ fontSize: '0.6em', color: 'var(--text-dim)' }}> dBm</span>
        </span>
      </div>

      <div className="statusbar__item">
        <span className="label">SNR</span>
        <span className="value numeric">
          {formatMeasurement(latest?.frame.snr, 1)}
          <span style={{ fontSize: '0.6em', color: 'var(--text-dim)' }}> dB</span>
        </span>
      </div>

      <div className="statusbar__item">
        {/* Received, not sent. There is no packet counter, so this cannot show loss
            and deliberately does not try. */}
        <span className="label">Received</span>
        <span className="value numeric">{counts.frames}</span>
      </div>

      <div className="statusbar__item">
        <span className="label">Malformed</span>
        <span
          className="value numeric"
          style={{ color: counts.malformed > 0 ? 'var(--warn)' : undefined }}
        >
          {counts.malformed}
        </span>
      </div>

      <div className="statusbar__spacer" />

      {/* Flight stays first and stays the default. Switching views is a deliberate act
          during analysis, never something to discover mid-descent. */}
      <div className="viewswitch" role="group" aria-label="View">
        {(['flight', 'channels'] as const).map((option) => (
          <button
            key={option}
            type="button"
            className={`viewswitch__button ${view === option ? 'is-active' : ''}`}
            aria-pressed={view === option}
            onClick={() => onViewChange(option)}
          >
            {option === 'flight' ? 'Flight' : 'Channels'}
          </button>
        ))}
      </div>

      <div className="statusbar__item">
        <span className="label">Backend</span>
        <span className="chip" style={{ fontSize: 'var(--size-body)' }}>
          {connection === 'open' ? 'Connected' : connection === 'connecting'
            ? 'Connecting…'
            : 'Disconnected'}
        </span>
      </div>
    </div>
  )
}
