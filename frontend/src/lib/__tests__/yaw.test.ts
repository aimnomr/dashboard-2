import { describe, expect, it } from 'vitest'
import { ALIAS_RATE, integrateYaw, MAX_INTEGRATION_DT } from '../attitude'
import type { FrameRecord, TelemetryFrame } from '../../types/telemetry'

const baseFrame: TelemetryFrame = {
  temp: 32.5, hum: 78, pres: 1007.9, alt: 0,
  ax: 0, ay: 0, az: 1,
  gx: 0, gy: 0, gz: 0,
  lat: 3.07830, lng: 101.71220, spd: 0, sat: 9,
  chute: 0, rssi: -55.6, snr: 9.33,
}

/** One sample: vehicle clock in ms, and the z-axis rate at that instant. */
const at = (vehicleMs: number | null, gz: number): FrameRecord => ({
  rxIndex: 0,
  t: 0,
  receivedAt: 0,
  vehicleMs,
  seq: null,
  frame: { ...baseFrame, gz },
})

describe('yaw availability', () => {
  it('produces nothing without a vehicle clock', () => {
    // GEN1 and GEN2 carry no onboard timestamp, so there is no interval to integrate
    // over. Arrival time is NOT substituted: its jitter would integrate into heading
    // error that looks exactly like real rotation.
    const result = integrateYaw([at(null, 50), at(null, 50), at(null, 50)])
    expect(result.yaw).toBeNull()
    expect(result.reason).toBe('no vehicle clock')
  })

  it('distinguishes "no clock" from "not enough samples yet"', () => {
    // Different situations needing different responses: one can never yield yaw, the
    // other just has not yet.
    expect(integrateYaw([at(1000, 10)]).reason).toBe('not enough samples yet')
  })

  it('produces nothing from an empty history', () => {
    expect(integrateYaw([]).yaw).toBeNull()
  })
})

describe('yaw integration', () => {
  it('accumulates a steady rate over the vehicle clock', () => {
    // 10 deg/s held across three 1 s intervals.
    const result = integrateYaw([at(0, 10), at(1000, 10), at(2000, 10), at(3000, 10)])
    expect(result.yaw).toBeCloseTo(30, 6)
    expect(result.integrated).toBeCloseTo(3, 6)
    expect(result.degraded).toBe(false)
  })

  it('integrates trapezoidally, not by holding the newest sample', () => {
    // 0 deg/s then 20 deg/s over one second is 10 degrees of rotation, not 20. At 1 Hz
    // the rate can change a lot between samples, and holding the newest value across the
    // whole interval biases every single turn in the same direction.
    expect(integrateYaw([at(0, 0), at(1000, 20)]).yaw).toBeCloseTo(10, 6)
  })

  it('wraps into [0, 360)', () => {
    const result = integrateYaw([at(0, 100), at(1000, 100), at(2000, 100), at(3000, 100)])
    // 300 degrees, still in range.
    expect(result.yaw).toBeCloseTo(300, 6)

    const wrapped = integrateYaw([
      at(0, 100), at(1000, 100), at(2000, 100), at(3000, 100), at(4000, 100),
    ])
    expect(wrapped.yaw).toBeCloseTo(40, 6)
  })

  it('reports a negative rotation as a positive heading', () => {
    // -10 degrees is a heading of 350, not a negative number the UI has to explain.
    expect(integrateYaw([at(0, -10), at(1000, -10)]).yaw).toBeCloseTo(350, 6)
  })
})

describe('yaw honesty', () => {
  it('discards the heading when the vehicle reboots', () => {
    // The clock going backwards means a reset, and the boot orientation the heading was
    // measured from no longer exists. Carrying the old figure forward would report a
    // heading relative to an origin that is gone.
    const result = integrateYaw([
      at(0, 10), at(1000, 10),      // +10 degrees, then a reboot
      at(200, 10), at(1200, 10),    // +10 degrees from the new reference
    ])
    expect(result.yaw).toBeCloseTo(10, 6)
    expect(result.integrated).toBeCloseTo(1, 6)
  })

  it('does not integrate across a long dropout', () => {
    // The vehicle kept rotating during the silence and nothing recorded it. Assuming the
    // rate held would invent the part nobody saw, so the interval contributes nothing
    // and the estimate is flagged as having a known omission.
    const gap = (MAX_INTEGRATION_DT + 5) * 1000
    const result = integrateYaw([at(0, 10), at(1000, 10), at(1000 + gap, 10)])

    expect(result.yaw).toBeCloseTo(10, 6)
    expect(result.degraded).toBe(true)
    expect(result.reason).toBe('gap in coverage')
  })

  it('flags rotation faster than 1 Hz sampling can resolve', () => {
    // Past Nyquist the samples alias: the reconstructed heading can be turning the wrong
    // way entirely, so it must not be presented as merely imprecise.
    const result = integrateYaw([at(0, ALIAS_RATE + 20), at(1000, ALIAS_RATE + 20)])
    expect(result.degraded).toBe(true)
    expect(result.reason).toBe('rotation too fast to resolve')
  })

  it('reports how long it has been integrating, because drift grows with it', () => {
    // Nothing corrects a gyro bias without an absolute reference, and a 6-axis IMU has
    // none. Elapsed integration time is the only cue the operator gets about how far the
    // estimate may have wandered.
    const samples = Array.from({ length: 61 }, (_, i) => at(i * 1000, 0))
    expect(integrateYaw(samples).integrated).toBeCloseTo(60, 6)
  })
})
