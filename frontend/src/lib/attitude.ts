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
