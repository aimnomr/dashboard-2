import type { FrameRecord } from '../types/telemetry'

/** Samples averaged for the vertical rate. At 1 Hz this is a ~5 s window. */
export const RATE_WINDOW = 5

/**
 * Seconds for this sample, from the best clock available.
 *
 * The vehicle clock is the moment of sampling; arrival time is the moment a packet
 * finished crossing a radio, a USB hop and a Python process. For a *rate* the difference
 * is not cosmetic — arrival jitter divides straight into the result, so a steady descent
 * reads as a fluctuating one.
 */
function sampleSeconds(record: FrameRecord): number {
  return record.vehicleMs !== null ? record.vehicleMs / 1000 : record.t / 1000
}

/**
 * Vertical rate from the altitude trend.
 *
 * More useful than GPS ground speed during descent — it is what says whether the chute
 * is working. Derived over a window because per-sample differences at 1 Hz are mostly
 * barometric noise.
 *
 * Returns null rather than a number whenever the window cannot support one: too few
 * samples, or a non-positive time span, which is what a vehicle reboot inside the window
 * looks like.
 */
export function verticalRate(history: FrameRecord[]): number | null {
  if (history.length < 2) return null
  return rateOver(history.slice(-RATE_WINDOW))
}

function rateOver(window: FrameRecord[]): number | null {
  if (window.length < 2) return null
  const first = window[0]
  const last = window[window.length - 1]
  const seconds = sampleSeconds(last) - sampleSeconds(first)
  if (seconds <= 0) return null
  return (last.frame.alt - first.frame.alt) / seconds
}

/**
 * The same figure at every point in the history, for charting.
 *
 * Null at the start, where the window has not filled yet, and at any reboot. Both render
 * as a break in the trace rather than as a rate of zero — which would read as "holding
 * altitude" at exactly the moment the truth is "not known".
 */
export function verticalRateSeries(history: FrameRecord[]): (number | null)[] {
  const out = new Array<number | null>(history.length)
  for (let i = 0; i < history.length; i++) {
    out[i] = rateOver(history.slice(Math.max(0, i - RATE_WINDOW + 1), i + 1))
  }
  return out
}
