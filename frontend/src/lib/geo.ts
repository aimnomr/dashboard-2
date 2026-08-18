import type { TelemetryFrame } from '../types/telemetry'

/**
 * The firmware sends `0.0, 0.0` for latitude and longitude whenever the GPS has no
 * valid fix — it does not send null, and it does not omit the field. Plotting those
 * verbatim puts the CanSat in the Gulf of Guinea and drags a trace line across the
 * Atlantic to get there.
 *
 * Treat exact zeros as "no fix". A real fix at exactly 0.000000, 0.000000 is possible
 * in principle and irrelevant in Malaysia.
 */
export function hasFix(frame: TelemetryFrame): boolean {
  return frame.lat !== 0 || frame.lng !== 0
}

export interface LocalPoint {
  x: number
  y: number
}

const METRES_PER_DEG_LAT = 110_540
const METRES_PER_DEG_LNG = 111_320

/**
 * Equirectangular projection to metres relative to an origin fix.
 *
 * Accurate to well under a metre over the few hundred metres a CanSat covers, and it
 * avoids pulling in a projection library for a plot whose whole extent is smaller than
 * a football pitch.
 */
export function toLocal(frame: TelemetryFrame, origin: TelemetryFrame): LocalPoint {
  const latRad = (origin.lat * Math.PI) / 180
  return {
    x: (frame.lng - origin.lng) * METRES_PER_DEG_LNG * Math.cos(latRad),
    y: (frame.lat - origin.lat) * METRES_PER_DEG_LAT,
  }
}

/** A round-ish number of metres that fits comfortably inside the given span. */
export function niceScale(spanMetres: number): number {
  const target = spanMetres / 4
  const magnitude = 10 ** Math.floor(Math.log10(Math.max(target, 1)))
  for (const step of [1, 2, 5, 10]) {
    if (magnitude * step >= target) return magnitude * step
  }
  return magnitude * 10
}

export function formatCoord(value: number, positive: string, negative: string): string {
  const hemisphere = value >= 0 ? positive : negative
  return `${Math.abs(value).toFixed(6)}° ${hemisphere}`
}
