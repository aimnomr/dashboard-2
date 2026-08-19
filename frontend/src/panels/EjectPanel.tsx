import { useEffect, useState } from 'react'
import { Panel } from '../components/Panel'
import type { CommandAck, FrameRecord } from '../types/telemetry'

interface EjectPanelProps {
  latest: FrameRecord | null
  lastAck: CommandAck | null
  now: number
  sendCommand: (command: string) => void
}

/** Arming lapses on its own, so a control that fires a parachute is never left hot. */
const ARM_TIMEOUT_MS = 10_000
/** After this long with no CHUTE:1, the command probably did not get through. */
const CONFIRM_TIMEOUT_MS = 6_000

export function EjectPanel({ latest, lastAck, now, sendCommand }: EjectPanelProps) {
  const [armedAt, setArmedAt] = useState<number | null>(null)
  const [sentAt, setSentAt] = useState<number | null>(null)
  const [pingAt, setPingAt] = useState<number | null>(null)

  const chute = latest?.frame.chute ?? null
  const deployed = chute === 1

  useEffect(() => {
    if (armedAt === null) return
    const id = window.setTimeout(() => setArmedAt(null), ARM_TIMEOUT_MS)
    return () => window.clearTimeout(id)
  }, [armedAt])

  const armed = armedAt !== null && now - armedAt < ARM_TIMEOUT_MS
  const armSecondsLeft = armedAt ? Math.ceil((ARM_TIMEOUT_MS - (now - armedAt)) / 1000) : 0

  const fire = () => {
    sendCommand('eject')
    setSentAt(Date.now())
    setArmedAt(null)
  }

  const ping = () => {
    sendCommand('ping')
    setPingAt(Date.now())
  }

  const awaitingConfirmation = sentAt !== null && !deployed
  const confirmationOverdue = awaitingConfirmation && now - sentAt > CONFIRM_TIMEOUT_MS
  const pingAck = lastAck?.command === 'PING' ? lastAck : null

  return (
    // Titled for the path, not for the one dangerous command on it: this panel now
    // carries both uplink commands the ground station accepts.
    <Panel title="Uplink" area="eject">
      {deployed ? (
        <div className="notice notice--alert" style={{ fontWeight: 800 }}>
          <span aria-hidden="true">◆</span> Chute deployed
        </div>
      ) : (
        <div className="eject__controls">
          <button
            type="button"
            className={`btn btn--arm ${armed ? 'is-armed' : ''}`}
            onClick={() => setArmedAt(armed ? null : Date.now())}
          >
            {armed ? `Armed · ${armSecondsLeft}s` : 'Arm'}
          </button>
          <button
            type="button"
            className="btn btn--fire"
            disabled={!armed}
            onClick={fire}
          >
            Eject
          </button>
        </div>
      )}

      {/* "Sent" means the bytes left the PC. It does not mean the ground unit
          transmitted them, that the vehicle heard them, or that the chute fired. The
          link carries no acknowledgement — confirmation only ever arrives indirectly,
          as CHUTE:1 in later telemetry. Conflating the two would be a lie the operator
          might act on. */}
      {sentAt !== null && (
        <div className="eject__status">
          <div>
            <span className="label">Command</span>
            <div className={`chip chip--${lastAck?.sent === false ? 'alert' : 'ok'}`}>
              {lastAck?.sent === false ? '■ Not sent' : '● Sent'}
              {lastAck?.error ? ` — ${lastAck.error}` : ''}
            </div>
          </div>
          <div>
            <span className="label">Vehicle</span>
            <div
              className={`chip chip--${
                deployed ? 'alert' : confirmationOverdue ? 'warn' : 'unknown'
              }`}
            >
              {deployed
                ? '◆ Confirmed'
                : confirmationOverdue
                  ? '▲ No confirmation'
                  : '○ Awaiting…'}
            </div>
          </div>
        </div>
      )}

      {!deployed && (
        <p className="panel__footnote">
          No acknowledgement path — deployment is only confirmed by CHUTE:1 arriving in
          telemetry.
        </p>
      )}

      {/* The only way to test the uplink without deploying a parachute to test it.
          Deliberately not arm-guarded: a control that fires nothing does not need a
          guard, and guarding it would discourage the pre-launch check it exists for. */}
      <div className="uplink__test">
        <button type="button" className="btn btn--small" onClick={ping}>
          Ping
        </button>
        {pingAt !== null && (
          <span className={`chip chip--${pingAck?.sent === false ? 'alert' : 'ok'}`}>
            {pingAck?.sent === false ? '■ Not sent' : '● Sent'}
            {pingAck?.error ? ` — ${pingAck.error}` : ''}
          </span>
        )}
      </div>
      <p className="panel__footnote">
        Ping fires nothing. Confirmation is on the vehicle's OLED — its UL counter
        resets — never here.
      </p>
    </Panel>
  )
}
