import type { FrameRecord } from '../types/telemetry'

export interface Timebase {
  /** Seconds since the first frame, one entry per record. Always non-decreasing. */
  seconds: number[]
  /** True when the vehicle's own clock is in use rather than PC arrival time. */
  vehicle: boolean
  /** Human label for the axis, so which clock is running is never ambiguous. */
  label: string
  /** Indices where the vehicle clock went backwards — a reboot. */
  restarts: number[]
}

const EMPTY: Timebase = { seconds: [], vehicle: false, label: 'arrival time', restarts: [] }

/**
 * The x axis for every chart, chosen once from the first frame (rule S7).
 *
 * GEN3 carries `vehicle_ms`, sampled on board, so it is free of link and scheduling
 * jitter and is the true time the reading was taken. GEN1 and GEN2 carry no clock, and
 * arrival time is then the only one available.
 *
 * The choice is made once and never mixed within a session: a chart whose x axis
 * silently switched clocks partway through would misplace everything after the switch,
 * and nothing on screen would show that it had happened.
 *
 * **A reboot does not rewind the axis.** When the vehicle resets, `vehicle_ms` returns to
 * near zero. Plotting that literally would send the trace back across the chart and draw
 * a horizontal streak through the entire flight. Instead the gap contributes no elapsed
 * time and the index is reported in `restarts`, so callers can break the line there —
 * which is what actually happened, rather than a continuous trace across a discontinuity.
 */
export function timebase(history: FrameRecord[]): Timebase {
  if (history.length === 0) return EMPTY

  const vehicle = history[0].vehicleMs !== null
  if (!vehicle) {
    return {
      seconds: history.map((r) => r.t / 1000),
      vehicle: false,
      label: 'arrival time',
      restarts: [],
    }
  }

  const seconds = new Array<number>(history.length)
  const restarts: number[] = []
  let elapsedMs = 0
  let previous: number | null = null

  for (let i = 0; i < history.length; i++) {
    const stamp = history[i].vehicleMs

    if (stamp === null) {
      // A frame with no vehicle clock inside a vehicle-clocked session. One source runs
      // one generation, so this cannot arise in practice — but placing the point at the
      // running elapsed value keeps it visible, which is better than dropping data
      // because it did not fit an assumption.
      seconds[i] = elapsedMs / 1000
      continue
    }

    if (previous !== null) {
      const dt = stamp - previous
      if (dt < 0) restarts.push(i)
      else elapsedMs += dt
    }

    seconds[i] = elapsedMs / 1000
    previous = stamp
  }

  return { seconds, vehicle: true, label: 'vehicle clock', restarts }
}

/**
 * Copy `values` with a null at each restart index, so the trace breaks there.
 *
 * Drawing straight through a reboot would assert continuity across the one moment the
 * vehicle demonstrably was not continuous.
 */
export function applyBreaks(
  values: (number | null)[],
  restarts: number[],
): (number | null)[] {
  if (restarts.length === 0) return values
  const out = values.slice()
  for (const index of restarts) out[index] = null
  return out
}
