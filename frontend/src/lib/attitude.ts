import type { FrameRecord, TelemetryFrame } from '../types/telemetry'

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
   * There is no sensor fusion here, so the honest move is to say when the reading is
   * not trustworthy rather than to draw a confident horizon from meaningless numbers.
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

// ------------------------------------------------------------------- confirmation

/**
 * Consecutive unreliable frames required before the warning is shown.
 *
 * Two, from measurement rather than taste. Across three hardware sessions with the unit
 * sitting still on a table, every spurious `unreliable` was a **single** frame: 10 of 10
 * in `20260819-225006`, 9 of 13 in `20260819-224339`. The genuinely sustained episode in
 * `20260819-212959` ran for 163 consecutive frames. One sample of separation splits those
 * two populations almost perfectly.
 */
export const UNRELIABLE_CONFIRM = 2

/**
 * Whether to tell the operator the attitude cannot be trusted.
 *
 * Deliberately separate from `computeAttitude()`, which stays pure and per-frame: pitch,
 * roll and spin are readings and must never lag behind the data. This is a *claim about
 * the readings*, and a claim that flickers is worse than no claim at all — an operator
 * who has watched the warning blink on a stationary unit has learned to ignore it, which
 * is precisely the opposite of what it exists for.
 *
 * The single-frame dropouts it suppresses are not physical. A CanSat on a table does not
 * spend exactly one second at 0.365 g, or spin at 184 deg/s and stop; those are sensor
 * glitches, and entry 045 records the firmware side. Real boost and real freefall last
 * many seconds and still warn, one frame later than before.
 *
 * Returns the reason to display, or null when there is nothing to say.
 */
export function attitudeWarning(history: FrameRecord[]): string | null {
  if (history.length < UNRELIABLE_CONFIRM) return null

  const recent = history.slice(-UNRELIABLE_CONFIRM).map((r) => computeAttitude(r.frame))

  // Every one of the last N must be bad. A single good frame clears the warning
  // immediately — asymmetric on purpose, because "you can trust this again" is a safer
  // thing to be quick about than "you cannot".
  if (recent.some((a) => a.reliable)) return null

  // The newest reason, not the oldest: if the vehicle went from tumbling to freefall the
  // operator needs what is true now.
  return recent[recent.length - 1].reason
}

// --------------------------------------------------------------------------- yaw
//
// There is deliberately no yaw here.
//
// `integrateYaw()` existed until 2026-08-19 and was removed after repeated hardware
// testing, not after an argument about it. The reasoning it was built on was already
// correct and is worth keeping, because it is what makes this permanent rather than a
// gap waiting to be filled:
//
//   The MPU6050 is a 6-axis part. Gravity gives roll and pitch an absolute reference,
//   but rotation *about* the gravity vector leaves the accelerometer unchanged, so no
//   amount of processing extracts yaw from ax/ay/az. It has to be integrated from gz,
//   and with no magnetometer nothing ever corrects the result: it is relative to boot,
//   it drifts without bound, and at 1 Hz anything past 180 deg/s aliases badly enough
//   that the reconstruction can be turning the wrong way entirely.
//
// The estimate was labelled and flagged accordingly, and on hardware it was still not
// usable. A number that must be disclaimed every time it is read is not carrying its
// weight on a screen that exists to be read at a glance.
//
// `gx`, `gy` and `gz` remain untouched — every raw gyro and accelerometer channel is
// still parsed, charted in ChannelsView, and logged. What went is the *derived* heading,
// not the measurement. Re-proposing yaw needs new hardware (a magnetometer), not new
// code. See devlog 030 for the original build and 041 for the removal.
