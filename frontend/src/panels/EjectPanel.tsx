import { useEffect, useState } from 'react'
import { Panel } from '../components/Panel'
import { chuteIndicator } from '../lib/link'
import type { CommandAck, FrameRecord } from '../types/telemetry'

interface EjectPanelProps {
  latest: FrameRecord | null
  lastAck: CommandAck | null
  now: number
  sendCommand: (command: string) => void
}

/**
 * The uplink panel: chute state, and PING.
 *
 * ⚠ There is deliberately NO eject control here, and no keyboard shortcut for one
 * either. The Arm/Eject pair was removed on 2026-09-09; a manual release is sent from a
 * terminal with `python -m devtools.send_command EJECT`, which is unchanged and still
 * the supported path. Arm went with it — it existed only to gate Eject and had nothing
 * left to guard.
 *
 * ⚠ That changes what is on the SCREEN and nothing about the RECORD. A commanded release
 * is still fully visible afterwards, in two places that cannot be quietly removed: `ul`
 * rises in every packet the vehicle sends, which reaches the raw log, the SD card and
 * every replay; and the ground station's `[GCS] EJECT armed` / `attempt n/5` /
 * `confirmed after n attempt(s)` lines are written verbatim by rawlog.py, which filters
 * nothing by design. `chute` rising with `ul` UNCHANGED remains the only ground-side
 * proof that a release was automatic, and that pair still means exactly what it always
 * meant. Nothing here should ever be built to blur it.
 *
 * A `SET:DROP` control lived here briefly on 2026-09-09 and was removed the same day.
 * The command itself is unaffected — `api.py` still validates it and
 * `send_command SET:DROP:15` still works; it simply has no button. `dropValidation()`
 * and the DROP bounds remain in `lib/link.ts`, still pinned against the backend by a
 * test, for whenever a control wants them again.
 */
export function EjectPanel({ latest, lastAck, now, sendCommand }: EjectPanelProps) {
  const [chuteRoseAt, setChuteRoseAt] = useState<number | null>(null)
  const [seenChute, setSeenChute] = useState<number | null>(null)

  const chute = latest?.frame.chute ?? null
  /* Total uplink commands the vehicle reports receiving. GEN3.1 only — null on older
     firmware, which is NOT the same as zero and must not be shown as a count. */
  const ul = latest?.frame.ul ?? null

  /* When `chute` last ROSE, which is when the vehicle started re-arming. Tracked as a
     rise rather than as an absolute value so that opening the dashboard on a vehicle
     that fired an hour ago does not show a release that is long finished — reading the
     absolute counter instead is the devlog 058 bug in another costume.

     A vehicle reboot returns `chute` to 0, which is a FALL and correctly records
     nothing: a rebooted vehicle has an armed mechanism and no cooldown to serve. */
  useEffect(() => {
    if (chute === null) return
    if (seenChute === null) {
      setSeenChute(chute)
      return
    }
    if (chute > seenChute) {
      setSeenChute(chute)
      setChuteRoseAt(Date.now())
    } else if (chute < seenChute) {
      setSeenChute(chute)
    }
  }, [chute, seenChute])

  const indicator = chuteIndicator(chuteRoseAt, now)

  /* Filtered to PING, the one command this panel sends. `lastAck` is a single value for
     the whole app, so an unfiltered read would surface an ack for an EJECT typed at a
     terminal as though this panel had issued it. No `hasSent` flag is needed: the chip
     cannot appear before an ack arrives. */
  const ack = lastAck && lastAck.command === 'PING' ? lastAck : null

  return (
    // Titled for the path, not for any one command on it.
    <Panel title="Uplink" area="eject">
      {/* The chute state as one light. Red armed, green released, grey re-arming — see
          chuteIndicator() for why returning to red is an assumption on a vehicle whose
          release mode the dashboard cannot read.

          "Released" means the MECHANISM WAS DRIVEN. It does not mean a canopy opened:
          no sensor anywhere in this system can report that, so the word "deployed" is
          not used here (rule S8). */}
      <div className={`chute-state chute-state--${indicator.tone}`}>
        <span className="chute-state__lamp" aria-hidden="true" />
        <span className="chute-state__label">
          {indicator.label}
          {indicator.state === 'cooldown' ? ` · ${indicator.secondsLeft}s` : ''}
        </span>
        {/* The count does not expire, because the green flash does. A release that was
            missed on screen must still be answerable afterwards. */}
        <span className="chute-state__count">
          {chute === null ? 'no chute field' : chute === 0 ? 'none released' : `×${chute}`}
        </span>
      </div>

      {/* The only way to test the uplink without deploying a parachute to test it.
          Deliberately not guarded: a control that fires nothing does not need a guard,
          and guarding it would discourage the pre-launch check it exists for. */}
      <div className="uplink__test">
        <button type="button" className="btn btn--small" onClick={() => sendCommand('ping')}>
          Ping
        </button>
        {/* "Sent" is the bytes leaving the PC. It is not receipt: the link carries no
            acknowledgement, and the only evidence the vehicle heard anything is `ul`
            rising in the notice below. */}
        {ack && (
          <span className={`chip chip--${ack.sent === false ? 'alert' : 'ok'}`}>
            {ack.sent === false ? '■ Not sent' : '● Sent'}
            {ack.error ? ` — ${ack.error}` : ''}
          </span>
        )}
      </div>

      {/* "Heard" is doing the work a second footnote line used to do. `ul` rises on
          receipt whether the vehicle applied a command or refused it, so the honest verb
          is heard, never applied or set. */}
      {ul !== null ? (
        <div className={`notice notice--${ul > 0 ? 'ok' : 'warn'}`}>
          <span aria-hidden="true">{ul > 0 ? '●' : '▲'}</span>{' '}
          {ul > 0
            ? `Uplink confirmed — ${ul} heard, not applied`
            : 'Uplink unproven — nothing heard'}
        </div>
      ) : (
        <p className="panel__footnote">No uplink counter on this firmware.</p>
      )}
    </Panel>
  )
}
