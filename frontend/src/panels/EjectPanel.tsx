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
  /* What `chute` read at the moment Eject was pressed, so THIS command can be confirmed
     rather than the one before it. The counter is monotonic and never returns to zero —
     not even on RESET:CHUTE, which clears the vehicle's fire latch and deliberately
     leaves the count alone. Testing an absolute value instead is the same bug that made
     the ground station refuse to re-send EJECT after a reset (devlog 058). */
  const [chuteAtSend, setChuteAtSend] = useState<number | null>(null)
  const [pingAt, setPingAt] = useState<number | null>(null)

  const chute = latest?.frame.chute ?? null
  /* Total uplink commands the vehicle reports receiving. GEN3.1 only — null on older
     firmware, which is NOT the same as zero and must not be shown as a count. */
  const ul = latest?.frame.ul ?? null
  /* Releases COMMANDED, from either path — an uplink EJECT or the vehicle's own
     auto-eject. Never "deployed": no canopy sensor exists anywhere in this system, so
     that word is a claim nothing here can support (rule S8). */
  const commanded = chute !== null && chute > 0

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
    setChuteAtSend(chute)
    setArmedAt(null)
  }

  const ping = () => {
    sendCommand('ping')
    setPingAt(Date.now())
  }

  /* Relative to the press, not absolute. On a re-armed unit `chute` is already 1 when
     Eject is pressed again, and an absolute test would report the new command confirmed
     before it had been sent. Null baseline (firmware with no chute field) never rises,
     which is the honest answer for a vehicle that cannot report this at all. */
  const roseSinceSend =
    sentAt !== null && chute !== null && chuteAtSend !== null && chute > chuteAtSend
  const awaitingConfirmation = sentAt !== null && !roseSinceSend
  const confirmationOverdue = awaitingConfirmation && now - sentAt > CONFIRM_TIMEOUT_MS
  const pingAck = lastAck?.command === 'PING' ? lastAck : null

  return (
    // Titled for the path, not for the one dangerous command on it: this panel now
    // carries both uplink commands the ground station accepts.
    <Panel title="Uplink" area="eject">
      {commanded ? (
        <div className="notice notice--alert" style={{ fontWeight: 800 }}>
          <span aria-hidden="true">◆</span> Release commanded ×{chute}
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
                roseSinceSend ? 'alert' : confirmationOverdue ? 'warn' : 'unknown'
              }`}
            >
              {roseSinceSend
                ? '◆ Mechanism driven'
                : confirmationOverdue
                  ? '▲ No confirmation'
                  : '○ Awaiting…'}
            </div>
          </div>
        </div>
      )}

      {!commanded && (
        <p className="panel__footnote">
          No acknowledgement path — the only signal is `chute` rising in later telemetry,
          and that reports the mechanism was driven, never that a canopy opened.
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
      {/* The answer to the question devlogs 037 to 044 were all argued without.
          Until GEN3.1 this number existed only on the vehicle's OLED, which cannot be
          seen once the CanSat is sealed and was dead on the unit that mattered. */}
      {ul !== null ? (
        <div className={`notice notice--${ul > 0 ? 'ok' : 'warn'}`}>
          <span aria-hidden="true">{ul > 0 ? '●' : '▲'}</span>{' '}
          {ul > 0
            ? `Uplink confirmed — vehicle has received ${ul} command${ul === 1 ? '' : 's'}`
            : 'Uplink unproven — the vehicle has received nothing'}
        </div>
      ) : (
        <p className="panel__footnote">
          Ping fires nothing, and this firmware reports no uplink counter. Confirmation is
          on the vehicle's OLED, or its USB serial at 115200 — never here.
        </p>
      )}
      {ul !== null && (
        <p className="panel__footnote">
          Counts pings and ejects together. Non-zero proves the two-way link has worked;
          it does not prove the parachute opened, which nothing on board can confirm.
        </p>
      )}
    </Panel>
  )
}
