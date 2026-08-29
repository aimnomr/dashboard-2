import type { FrameRecord, PacketField } from '../types/telemetry'

/**
 * The live packet, read as numbers rather than as traces.
 *
 * The charts answer "what has this been doing"; nothing answered "what is it right now,
 * and is it moving". Reading a current value off a sparkline is guesswork, and a channel
 * that has quietly frozen looks identical to one that is genuinely steady — a `pres`
 * stuck at its last reading and a calm afternoon draw the same flat line.
 *
 * So both directions get an answer: a delta for "is this moving", and a flat-run count
 * for "has this stopped". The second is the diagnostic one and the harder to notice.
 *
 * Every label, unit, precision and sentinel comes from the field table in the session
 * message. Nothing here decides how a field should look.
 */

/** Frames a value must sit unchanged for before the readout calls it flat. */
export const FLAT_FRAMES = 10

/**
 * How far back the flat-run scan will look. Five minutes at 1 Hz.
 *
 * Bounds the work at 22 fields x this per frame, and keeps the number readable: past
 * here the answer is "for as long as you care about", which `300+` says just as well.
 */
export const FLAT_SCAN_MAX = 300

/**
 * Decimal places from a C printf spec — `%.2f` -> 2, `%d` and `%lu` -> 0.
 *
 * The spec is the firmware's, carried through the parser and the contract untouched, so
 * the dashboard renders each field at the precision the vehicle actually transmits.
 * Anything else either invents digits that were never sent or hides ones that were.
 */
export function decimalsFor(fmt: string): number {
  const match = /%\.(\d+)f/.exec(fmt)
  return match ? Number(match[1]) : 0
}

/** A value at its field's own precision. Null renders as an em dash, never as 0. */
export function formatFieldValue(value: number | null, fmt: string): string {
  if (value === null || !Number.isFinite(value)) return '—'
  return value.toFixed(decimalsFor(fmt))
}

/**
 * A signed delta, or `·` for no change.
 *
 * `·` rather than `+0.00`: a formatted zero reads as a measured quantity, and this
 * column is about movement, not magnitude. The distinction matters most on the fields
 * that are supposed to sit still — `chute` holding 0 is the system working.
 */
export function formatDelta(delta: number | null, fmt: string): string {
  if (delta === null) return '—'
  const digits = decimalsFor(fmt)
  if (delta === 0) return '·'
  const sign = delta > 0 ? '+' : '-'
  return sign + Math.abs(delta).toFixed(digits)
}

/**
 * One field's value out of a record, by contract name.
 *
 * `seq` and `ms` are fields 1 and 2 of the packet but live one level up on the record,
 * beside the arrival metadata, so the lookup has to reach both places. Anything the
 * frame does not carry comes back null — a GEN3.0 vehicle has no `ul`, an SD replay has
 * no `rssi`, and null is the honest answer for both.
 */
export function fieldValue(record: FrameRecord, name: string): number | null {
  if (name === 'seq') return record.seq
  if (name === 'ms') return record.vehicleMs
  const value = (record.frame as unknown as Record<string, number | null | undefined>)[name]
  return value === undefined || value === null ? null : value
}

export interface FieldReadout {
  field: PacketField
  value: number | null
  /** Change since the previous frame. Null across a reboot, or when either side is absent. */
  delta: number | null
  /**
   * Most recent consecutive frames carrying this exact value, counting the current one.
   * 1 means it just changed. 0 means the field is absent, which is not the same as stuck.
   */
  flat: number
  /** The scan hit `FLAT_SCAN_MAX` and stopped; the true run is at least `flat`. */
  flatCapped: boolean
  /** What this value means if it is a sentinel rather than a measurement. */
  sentinel: string | null
}

export interface PacketReadout {
  rows: FieldReadout[]
  /**
   * The previous frame came from a different boot, so no delta on this frame is
   * meaningful. Detected from the vehicle clock going backwards, the same signal
   * `timebase()` breaks the charts on.
   */
  rebooted: boolean
}

/** True when the vehicle clock went backwards between these two frames. */
function rebootBetween(previous: FrameRecord, current: FrameRecord): boolean {
  return (
    previous.vehicleMs !== null &&
    current.vehicleMs !== null &&
    current.vehicleMs < previous.vehicleMs
  )
}

/**
 * Read the latest frame against the field table.
 *
 * Returns null on an empty history — there is no packet to describe, and a table of
 * dashes would imply one arrived and was empty.
 */
export function readPacket(
  history: FrameRecord[],
  fields: PacketField[],
): PacketReadout | null {
  if (history.length === 0) return null

  const last = history.length - 1
  const current = history[last]
  const previous = last > 0 ? history[last - 1] : null

  // A reboot returns `seq` and `ms` to near zero. Differencing across it would report
  // `seq −1481`, which reads as catastrophic loss when nothing at all was lost. The
  // charts break at the same discontinuity for the same reason.
  const rebooted = previous !== null && rebootBetween(previous, current)

  const rows = fields.map((field) => {
    const value = fieldValue(current, field.name)

    let delta: number | null = null
    if (!rebooted && previous !== null && value !== null) {
      const before = fieldValue(previous, field.name)
      if (before !== null) delta = value - before
    }

    // An absent field is not a frozen one. Counting nulls as a flat run would put
    // "flat 300+" beside every GEN3.1 field on a GEN3.0 flight and call missing
    // firmware a stuck sensor.
    let flat = 0
    let flatCapped = false
    if (value !== null) {
      flat = 1
      for (let i = last - 1; i >= 0; i--) {
        if (flat >= FLAT_SCAN_MAX) {
          flatCapped = true
          break
        }
        // Stop at a reboot. A sensor that read the same before and after a power cycle
        // was re-initialised in between, so the run is two runs, and calling it one
        // would attribute a stuck reading to hardware that has since restarted.
        if (rebootBetween(history[i], history[i + 1])) break
        if (fieldValue(history[i], field.name) !== value) break
        flat++
      }
    }

    const sentinel =
      field.sentinel !== undefined && value !== null && value === field.sentinel.value
        ? field.sentinel.means
        : null

    return { field, value, delta, flat, flatCapped, sentinel }
  })

  return { rows, rebooted }
}
