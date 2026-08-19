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

/**
 * A position the vehicle is still reporting after it stopped being able to measure one.
 *
 * `sat === 0` alongside a non-zero position is a contradiction: the vehicle is claiming
 * a fix while saying it is tracking nothing. On 2026-08-19 that state ran for 14
 * consecutive packets with latitude, longitude and speed frozen to the digit, because
 * TinyGPSPlus's `isValid()` latches true on the first fix and never returns to false.
 * The firmware now checks the fix age, but this stays as the ground's own guard: a
 * vehicle asserting something impossible must not be believed just because it is the
 * only one talking.
 *
 * It also makes every capture already on disk readable — the corpus predates the
 * firmware fix, and a stale position in a replay is exactly as misleading as a live one.
 *
 * Deliberately separate from `hasFix()`, which stays a pure statement about the
 * coordinates. This is a statement about whether to trust them.
 */
export function fixStale(frame: TelemetryFrame): boolean {
  if (!hasFix(frame)) return false

  // GEN3.1 carries the receiver's own verdict, which beats any inference: 0 means it
  // says the fix is invalid. -1 means it never reported, and must not veto anything —
  // a module that does not send the field would otherwise blank the track forever.
  if (frame.fixq === 0) return true

  return frame.sat === 0
}

/** A fix that is both present and current — what anything plotting a position wants. */
export function hasLiveFix(frame: TelemetryFrame): boolean {
  return hasFix(frame) && !fixStale(frame)
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
