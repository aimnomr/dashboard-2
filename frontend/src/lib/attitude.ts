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

// --------------------------------------------------------------------------- yaw

/**
 * Heading about the vehicle's own z axis.
 *
 * Read this before trusting the number.
 *
 * **Roll and pitch are measured. Yaw is not.** Gravity gives roll and pitch an absolute
 * reference: tilt the vehicle and the accelerometer sees a different vector. Rotation
 * *about* the gravity vector changes nothing an accelerometer can detect, so no amount
 * of processing extracts yaw from `ax/ay/az`. It has to be integrated from `gz`, and
 * that carries three limits that do not go away:
 *
 * 1. **It is relative, not a compass heading.** Zero is wherever the vehicle happened to
 *    be pointing at boot. The MPU6050 is a 6-axis part with no magnetometer, so there is
 *    nothing on board that knows where north is.
 * 2. **It drifts, without bound.** Every gyro has a small bias; integrating it
 *    accumulates error forever, and with no absolute reference nothing pulls it back.
 *    `integrated` is exposed precisely so the operator can judge how stale the estimate
 *    has become.
 * 3. **It cannot see rotation faster than the sample rate.** At 1 Hz, a vehicle spinning
 *    at 200 deg/s has turned most of the way round between two samples and no record
 *    exists of which way it went.
 */
export interface YawEstimate {
  /** Degrees in [0, 360), relative to the orientation at boot. Null when not derivable. */
  yaw: number | null
  /** Seconds of vehicle time actually integrated. Drift grows with this. */
  integrated: number
  /** True when rotation was demonstrably missed, so the figure has a known error. */
  degraded: boolean
  reason: string | null
}

/**
 * Longest gap that may be integrated across.
 *
 * Beyond this, assuming the rate held constant through the silence is inventing the
 * vehicle's behaviour during the part nobody observed. The contribution is dropped and
 * the estimate is flagged instead.
 */
export const MAX_INTEGRATION_DT = 3.0

/**
 * Rate above which 1 Hz sampling cannot represent the rotation (Nyquist).
 *
 * Past this the samples alias: the reconstructed heading is not merely imprecise, it can
 * be turning the wrong way entirely.
 */
export const ALIAS_RATE = 180

const unavailable = (reason: string): YawEstimate => ({
  yaw: null,
  integrated: 0,
  degraded: false,
  reason,
})

/**
 * Integrate `gz` over the vehicle's own clock.
 *
 * Deliberately uses `vehicleMs`, never arrival time. Arrival intervals carry link and
 * scheduling jitter, and jitter integrated over a flight becomes heading error that
 * looks exactly like real rotation. GEN1 and GEN2 have no clock at all, so they get no
 * yaw rather than a plausible-looking one.
 */
export function integrateYaw(history: FrameRecord[]): YawEstimate {
  let yaw = 0
  let integrated = 0
  let degraded = false
  let reason: string | null = null
  let previous: FrameRecord | null = null
  let samples = 0
  let sawClock = false

  for (const record of history) {
    if (record.vehicleMs !== null) sawClock = true

    if (record.vehicleMs === null) {
      // A generation with no clock. Nothing before it can be integrated against.
      previous = null
      continue
    }

    if (previous === null) {
      previous = record
      continue
    }

    const dt = (record.vehicleMs - previous.vehicleMs!) / 1000

    if (dt <= 0) {
      // The clock went backwards: the vehicle rebooted. Its orientation reference is
      // gone with it, so the accumulator restarts rather than carrying a heading that
      // is now measured from an origin that no longer exists.
      yaw = 0
      integrated = 0
      degraded = false
      reason = null
      samples = 0
      previous = record
      continue
    }

    if (dt > MAX_INTEGRATION_DT) {
      // Rotation happened during the gap and was not recorded. Skipping it keeps the
      // estimate honest about what was observed; the flag keeps it honest about what
      // was not.
      degraded = true
      reason = 'gap in coverage'
      previous = record
      continue
    }

    if (Math.abs(record.frame.gz) > ALIAS_RATE || Math.abs(previous.frame.gz) > ALIAS_RATE) {
      degraded = true
      reason = 'rotation too fast to resolve'
    }

    // Trapezoidal. At 1 Hz the rate can change substantially between samples, and
    // holding the newest value across the whole interval biases every turn.
    yaw += ((record.frame.gz + previous.frame.gz) / 2) * dt
    integrated += dt
    samples += 1
    previous = record
  }

  // Two distinct reasons for no answer, and they call for different things from the
  // operator. No clock at all means this generation can never produce yaw. A clock but
  // no integrated interval means wait — which is also the state immediately after a
  // reboot, when the previous heading has been correctly discarded.
  if (samples === 0) {
    return unavailable(sawClock ? 'not enough samples yet' : 'no vehicle clock')
  }

  return {
    yaw: ((yaw % 360) + 360) % 360,
    integrated,
    degraded,
    reason,
  }
}
