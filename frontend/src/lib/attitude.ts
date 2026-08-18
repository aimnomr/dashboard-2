import type { TelemetryFrame } from '../types/telemetry'

export interface Attitude {
  /** Degrees. Positive = nose up. */
  pitch: number
  /** Degrees. Positive = right wing down. */
  roll: number
  /** Total acceleration magnitude, in g. */
  magnitude: number
  /** Total angular rate, deg/s. */
  spinRate: number
  /**
   * Whether pitch and roll mean anything right now.
   *
   * An accelerometer measures the sum of gravity and vehicle acceleration; it can only
   * be used as an attitude reference when the vehicle is not accelerating. Under boost
   * or in freefall the "horizon" is measuring thrust or nothing at all, and showing it
   * as attitude would be inventing information.
   *
   * There is no sensor fusion here and no onboard timestamp to integrate gyro against
   * (ISS-08), so the honest move is to say when the reading is not trustworthy rather
   * than to draw a confident horizon from meaningless numbers.
   */
  reliable: boolean
  reason: string | null
}

/** Acceleration magnitude may sit this far from 1 g before attitude is untrustworthy. */
const G_TOLERANCE = 0.25
/** Above this rotation rate the vehicle is tumbling and a static horizon is a lie. */
const SPIN_LIMIT = 90

/**
 * Longest gap that may be integrated across, in seconds.
 *
 * Spin angle is integrated from arrival-time deltas, because there is no onboard clock
 * (ISS-08). After a dropout — or a browser tab left in the background, where timers are
 * throttled — the next delta can be minutes wide, and integrating it would whip the
 * model through hundreds of revolutions. Beyond this the gap is skipped: the displayed
 * rotation is already relative and approximate, so losing a little of it is far better
 * than a spinning artefact that looks like real motion.
 */
const MAX_INTEGRATION_DT = 2

/**
 * Advance the relative spin angle.
 *
 * Returns radians. This is rotation about the vehicle's own long axis, integrated from
 * the gyro's z rate — it is RELATIVE and it DRIFTS. There is no heading reference on
 * this vehicle (no magnetometer), so absolute orientation about vertical is unknown and
 * the UI must say so.
 */
export function integrateSpin(
  previousRad: number,
  gzDegPerSec: number,
  dtSeconds: number,
): number {
  if (!Number.isFinite(dtSeconds) || dtSeconds <= 0) return previousRad
  const dt = Math.min(dtSeconds, MAX_INTEGRATION_DT)
  const next = previousRad + (gzDegPerSec * Math.PI) / 180 * dt
  // Wrap so the value cannot grow without bound over a long session and lose precision.
  return next % (Math.PI * 2)
}

export function computeAttitude(frame: TelemetryFrame): Attitude {
  const { ax, ay, az, gx, gy, gz } = frame

  const roll = (Math.atan2(ay, az) * 180) / Math.PI
  const pitch = (Math.atan2(-ax, Math.hypot(ay, az)) * 180) / Math.PI

  const magnitude = Math.hypot(ax, ay, az)
  const spinRate = Math.hypot(gx, gy, gz)

  let reason: string | null = null
  if (spinRate > SPIN_LIMIT) reason = 'tumbling'
  else if (magnitude < 1 - G_TOLERANCE) reason = 'freefall'
  else if (magnitude > 1 + G_TOLERANCE) reason = 'under acceleration'

  return { pitch, roll, magnitude, spinRate, reliable: reason === null, reason }
}
