import { describe, expect, it } from 'vitest'
import { verticalRate, verticalRateSeries } from '../rates'
import { applyBreaks, timebase } from '../timebase'
import type { FrameRecord, TelemetryFrame } from '../../types/telemetry'

const baseFrame: TelemetryFrame = {
  temp: 32.5, hum: 78, pres: 1007.9, alt: 0,
  ax: 0, ay: 0, az: 1,
  gx: 0, gy: 0, gz: 0,
  lat: 3.07830, lng: 101.71220, spd: 0, sat: 9,
  chute: 0, ul: null, hdop: null, fixq: null, rssi: -55.6, snr: 9.33,
}

/** `vehicleMs` null models GEN1/GEN2, which carry no onboard clock. */
const at = (vehicleMs: number | null, alt = 0, arrivalMs = 0): FrameRecord => ({
  rxIndex: 0,
  t: arrivalMs,
  receivedAt: arrivalMs,
  vehicleMs,
  seq: null,
  frame: { ...baseFrame, alt },
})

describe('chart timebase', () => {
  it('uses the vehicle clock when the packets carry one', () => {
    const base = timebase([at(8683), at(9683), at(10683)])
    expect(base.vehicle).toBe(true)
    expect(base.label).toBe('vehicle clock')
    // Elapsed from the first sample, not raw uptime — a chart starting at 8.683 s
    // would be reporting how long the vehicle had been powered before anyone listened.
    expect(base.seconds).toEqual([0, 1, 2])
  })

  it('falls back to arrival time when there is no vehicle clock', () => {
    const base = timebase([at(null, 0, 0), at(null, 0, 1000), at(null, 0, 2500)])
    expect(base.vehicle).toBe(false)
    expect(base.label).toBe('arrival time')
    expect(base.seconds).toEqual([0, 1, 2.5])
  })

  it('names the clock in use, so which one is running is never ambiguous', () => {
    // Rule S7: mixing clocks within a session would misplace everything after the
    // switch with nothing on screen showing it happened.
    expect(timebase([at(1000)]).label).not.toBe(timebase([at(null)]).label)
  })

  it('is empty for an empty history', () => {
    expect(timebase([]).seconds).toEqual([])
  })
})

describe('vehicle reboot', () => {
  const history = [at(0), at(1000), at(2000), at(0), at(1000)]

  it('never rewinds the axis', () => {
    // Plotting a reset clock literally sends the trace back across the chart and draws a
    // horizontal streak through the whole flight.
    const { seconds } = timebase(history)
    for (let i = 1; i < seconds.length; i++) {
      expect(seconds[i]).toBeGreaterThanOrEqual(seconds[i - 1])
    }
  })

  it('attributes no elapsed time to the discontinuity', () => {
    // The vehicle was off. Inventing a duration for that would misplace every sample
    // after it.
    expect(timebase(history).seconds).toEqual([0, 1, 2, 2, 3])
  })

  it('reports where the restart happened', () => {
    expect(timebase(history).restarts).toEqual([3])
  })

  it('breaks the trace there rather than drawing through it', () => {
    // Rule S4: a continuous line across a reboot asserts continuity at the one moment
    // the vehicle demonstrably was not continuous.
    expect(applyBreaks([1, 2, 3, 4, 5], [3])).toEqual([1, 2, 3, null, 5])
  })

  it('leaves the values alone when nothing restarted', () => {
    const values = [1, 2, 3]
    expect(applyBreaks(values, [])).toBe(values)
  })
})

describe('vertical rate', () => {
  it('measures against the vehicle clock when one exists', () => {
    // 10 m over 5 s of vehicle time. The arrival timestamps here are deliberately wrong
    // — if the rate were taken from them it would read 5 m/s instead of 2.
    const history = [
      at(0, 0, 0), at(1000, 2, 0), at(2000, 4, 500),
      at(3000, 6, 1200), at(4000, 8, 1600), at(5000, 10, 2000),
    ]
    expect(verticalRate(history)).toBeCloseTo(2, 6)
  })

  it('returns null rather than zero when it cannot be derived', () => {
    // Zero would read as "holding altitude", which is a claim. Null is the truth: not
    // known.
    expect(verticalRate([])).toBeNull()
    expect(verticalRate([at(0, 0)])).toBeNull()
  })

  it('returns null across a reboot instead of a fabricated rate', () => {
    // The window spans a clock reset, so the elapsed time in it is meaningless.
    expect(verticalRate([at(5000, 50), at(0, 0)])).toBeNull()
  })

  it('leaves the series undefined until the window has data', () => {
    const series = verticalRateSeries([at(0, 0), at(1000, 3)])
    expect(series[0]).toBeNull()
    expect(series[1]).toBeCloseTo(3, 6)
  })
})
