import { useMemo } from 'react'
import {
  FLAT_FRAMES,
  formatDelta,
  formatFieldValue,
  readPacket,
  type FieldReadout,
} from '../lib/packet'
import type { FrameRecord, SessionMessage } from '../types/telemetry'

interface PacketReadoutProps {
  history: FrameRecord[]
  session: SessionMessage | null
}

/**
 * The latest packet, field by field, in wire order.
 *
 * Wire order is the point, not a detail. Laid out this way the readout lines up with a
 * line in the raw feed: field 14 here is the fourteenth comma-separated value there, so
 * a suspect number can be traced back to the bytes that carried it without counting
 * commas by hand. Chart order would read more naturally and lose exactly that.
 *
 * Nothing here decides how a field is rendered. Index, label, unit, precision and
 * sentinel meanings all come from the field table in the session message — generated
 * from the parser, delivered over the same socket as the data it describes.
 */
export function PacketReadout({ history, session }: PacketReadoutProps) {
  const fields = session?.fields
  const outsideCrc = useMemo(
    () => new Set(session?.outside_checksum ?? []),
    [session?.outside_checksum],
  )

  const readout = useMemo(
    () => (fields ? readPacket(history, fields) : null),
    [history, fields],
  )

  if (!fields) {
    return (
      <section className="packet packet--unavailable">
        <p>
          This backend sends no packet contract, so the fields cannot be labelled or
          formatted. The charts below are unaffected.
        </p>
      </section>
    )
  }

  if (!readout) return null

  // Split down the middle rather than into two interleaved columns: each half stays in
  // wire order top to bottom, so counting across a raw line still works, and it still
  // reads correctly when the layout collapses to one column on a narrow screen.
  const half = Math.ceil(readout.rows.length / 2)
  const columns = [readout.rows.slice(0, half), readout.rows.slice(half)]

  return (
    <section className="packet">
      <header className="packet__head">
        <h3 className="packet__title">Packet</h3>
        {/* "contract GEN3.1", not "GEN3.1". The version describes the table this readout
            is built from, not the packet on screen — a GEN3.0 vehicle renders here
            against the same table, and a bare version string beside it would read as a
            claim about the data. Which fields that packet actually carried is stated per
            row, by the ones marked "not in this packet". */}
        <span className="packet__meta numeric">
          contract {session?.contract ?? 'unknown'} · {readout.rows.length} fields ·{' '}
          {history.length} received
        </span>
      </header>

      {readout.rebooted && (
        <p className="packet__notice">
          <span aria-hidden="true">▲</span> The vehicle rebooted between the last two
          packets — no change column is meaningful across that.
        </p>
      )}

      <div className="packet__columns">
        {columns.map((rows, index) => (
          <table className="packet__table" key={index}>
            <thead>
              <tr>
                <th scope="col" className="packet__i">#</th>
                <th scope="col">field</th>
                <th scope="col" className="packet__value">value</th>
                <th scope="col" className="packet__unit">unit</th>
                <th scope="col" className="packet__delta" title="change since the previous packet">
                  Δ
                </th>
                <th scope="col" className="packet__flags"></th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <Row key={row.field.name} row={row} outsideCrc={outsideCrc.has(row.field.name)} />
              ))}
            </tbody>
          </table>
        ))}
      </div>

      <p className="packet__footnote">
        <span aria-hidden="true">▪</span> marks a field that has held the same value for{' '}
        {FLAT_FRAMES} packets or more — steady on a calm channel, a stuck sensor on one
        that should be moving. Counted in packets, not seconds, so a dropped packet
        cannot inflate it.
      </p>
    </section>
  )
}

function Row({ row, outsideCrc }: { row: FieldReadout; outsideCrc: boolean }) {
  const { field, value, delta, flat, flatCapped, sentinel } = row
  const absent = value === null
  const isFlat = flat >= FLAT_FRAMES

  return (
    <tr className={absent ? 'is-absent' : undefined}>
      <td className="packet__i numeric">{field.i}</td>
      {/* The whole contract entry, one hover away, without putting it on screen. */}
      <th scope="row" title={field.note ? `${field.desc} — ${field.note}` : field.desc}>
        {field.name}
      </th>
      <td className="packet__value numeric">{formatFieldValue(value, field.fmt)}</td>
      <td className="packet__unit">{field.unit ?? ''}</td>
      <td className="packet__delta numeric">{formatDelta(delta, field.fmt)}</td>
      <td className="packet__flags">
        {/* A sentinel is annotated, never substituted. This view's job is to show the
            packet as it arrived; rewriting 0.00000 into "no fix" would make it the one
            surface that cannot be checked against the raw line. */}
        {sentinel && (
          <span className="packet__tag packet__tag--note" title="not a measurement">
            <span aria-hidden="true">ⓘ</span> {sentinel}
          </span>
        )}
        {isFlat && (
          <span className="packet__tag" title={`unchanged for ${flat} packets`}>
            <span aria-hidden="true">▪</span> flat {flat}
            {flatCapped ? '+' : ''}
          </span>
        )}
        {absent && (
          <span className="packet__tag packet__tag--note">
            not in this packet
          </span>
        )}
        {outsideCrc && (
          <span className="packet__tag packet__tag--note" title="appended by the ground station, not covered by the vehicle's checksum">
            <span aria-hidden="true">✳</span> after CRC
          </span>
        )}
      </td>
    </tr>
  )
}
