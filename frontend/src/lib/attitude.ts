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
