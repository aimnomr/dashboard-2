import { describe, expect, it } from 'vitest'
import { computeAttitude, integrateSpin } from '../attitude'
import { hasFix, niceScale, toLocal } from '../geo'
import type { TelemetryFrame } from '../../types/telemetry'

const base: TelemetryFrame = {
  temp: 32.5, hum: 78, pres: 1007.9, alt: 0,
  ax: 0, ay: 0, az: 1,
  gx: 0, gy: 0, gz: 0,
  lat: 3.07830, lng: 101.71220, spd: 0, sat: 9,
  chute: 0, rssi: -55.6, snr: 9.33,
}

const frame = (over: Partial<TelemetryFrame>): TelemetryFrame => ({ ...base, ...over })

describe('GPS fix validity', () => {
  it('treats 0,0 as no fix', () => {
    // The firmware sends 0.0,0.0 when the GPS is invalid. Plotted verbatim that puts
    // the CanSat in the Gulf of Guinea and drags a trace across the Atlantic.
    expect(hasFix(frame({ lat: 0, lng: 0 }))).toBe(false)
  })

  it('accepts a real fix', () => {
    expect(hasFix(base)).toBe(true)
  })

  it('accepts a partial zero, which is a real place', () => {
    expect(hasFix(frame({ lat: 0, lng: 101.7 }))).toBe(true)
  })
})

describe('local projection', () => {
  it('puts the origin at 0,0', () => {
    expect(toLocal(base, base)).toEqual({ x: 0, y: 0 })
  })

  it('converts a degree offset to plausible metres', () => {
    // 0.00045 deg of latitude is about 50 m — the mock's GPS drift amplitude.
    const north = toLocal(frame({ lat: base.lat + 0.00045 }), base)
    expect(north.y).toBeCloseTo(49.7, 0)
    expect(north.x).toBeCloseTo(0, 6)
  })

  it('shrinks longitude by cos(latitude)', () => {
    const east = toLocal(frame({ lng: base.lng + 0.001 }), base)
    expect(east.x).toBeCloseTo(111.32 * Math.cos((base.lat * Math.PI) / 180), 1)
  })

  it('signs south and west negative', () => {
    const p = toLocal(frame({ lat: base.lat - 0.001, lng: base.lng - 0.001 }), base)
    expect(p.x).toBeLessThan(0)
    expect(p.y).toBeLessThan(0)
  })
})

describe('scale bar', () => {
  it('picks a round number inside the span', () => {
    for (const span of [20, 55, 130, 700, 3000]) {
      const scale = niceScale(span)
      expect(scale).toBeLessThanOrEqual(span)
      expect(String(scale)).toMatch(/^[125]0*$/)
    }
  })
})

describe('attitude', () => {
  it('reads level when the only acceleration is gravity', () => {
    const a = computeAttitude(frame({ ax: 0, ay: 0, az: 1 }))
    expect(a.pitch).toBeCloseTo(0, 6)
    expect(a.roll).toBeCloseTo(0, 6)
    expect(a.reliable).toBe(true)
  })

  it('reads roll from lateral gravity', () => {
    const a = computeAttitude(frame({ ax: 0, ay: 0.7071, az: 0.7071 }))
    expect(a.roll).toBeCloseTo(45, 1)
  })

  it('reads pitch from longitudinal gravity', () => {
    const a = computeAttitude(frame({ ax: -0.7071, ay: 0, az: 0.7071 }))
    expect(a.pitch).toBeCloseTo(45, 1)
  })

  it('flags true freefall as unreliable', () => {
    // APOGEE in the mock: every axis is noise around zero. With no gravity vector to
    // measure there is no attitude to recover, and any horizon drawn would be noise.
    const a = computeAttitude(frame({ ax: 0.04, ay: -0.03, az: 0.05 }))
    expect(a.reliable).toBe(false)
    expect(a.reason).toBe('freefall')
  })

  it('treats inverted-but-1g as a real attitude, not an error', () => {
    // DESCENT_FREE sits near az = -0.8 g. Magnitude is still about 1 g, so gravity
    // still dominates and the reading is genuine: the vehicle is upside down.
    // Suppressing this would hide real information at the moment it matters most.
    const a = computeAttitude(frame({ ax: 0.1, ay: 0.1, az: -0.8 }))
    expect(a.reliable).toBe(true)
    expect(Math.abs(a.roll)).toBeGreaterThan(90)
  })

  it('flags boost as unreliable', () => {
    // BOOST sits near az = 6.5 g. An accelerometer under thrust is measuring thrust,
    // not attitude, so a confident horizon there would be invented information.
    const a = computeAttitude(frame({ ax: 0, ay: 0, az: 6.5 }))
    expect(a.reliable).toBe(false)
    expect(a.reason).toBe('under acceleration')
  })

  it('flags tumbling ahead of acceleration', () => {
    const a = computeAttitude(frame({ ax: 0, ay: 0, az: 1, gx: 120, gy: 40, gz: 0 }))
    expect(a.reliable).toBe(false)
    expect(a.reason).toBe('tumbling')
  })

  it('integrates spin at the expected rate', () => {
    // 90 deg/s for 1 s is a quarter turn.
    expect(integrateSpin(0, 90, 1)).toBeCloseTo(Math.PI / 2, 6)
  })

  it('integrates negative rates the other way', () => {
    expect(integrateSpin(0, -90, 1)).toBeCloseTo(-Math.PI / 2, 6)
  })

  it('clamps a long gap instead of whipping the model round', () => {
    // A dropout, or a backgrounded tab with throttled timers, produces a huge delta.
    // Integrating 300 s of rotation would spin the model through dozens of turns and
    // read as real motion. The gap is capped at 2 s instead.
    const capped = integrateSpin(0, 90, 300)
    expect(capped).toBeCloseTo(integrateSpin(0, 90, 2), 6)
  })

  it('ignores non-advancing or invalid deltas', () => {
    expect(integrateSpin(1.2, 90, 0)).toBe(1.2)
    expect(integrateSpin(1.2, 90, -5)).toBe(1.2)
    expect(integrateSpin(1.2, 90, Number.NaN)).toBe(1.2)
  })

  it('wraps so a long session cannot lose precision', () => {
    let angle = 0
    for (let i = 0; i < 2000; i++) angle = integrateSpin(angle, 200, 1)
    expect(Math.abs(angle)).toBeLessThanOrEqual(Math.PI * 2)
  })

  it('treats a stable chute descent as reliable', () => {
    // DESCENT_CHUTE: az about 0.85 g, low rates. This is the phase where attitude
    // actually means something, so it must not be suppressed.
    const a = computeAttitude(frame({ ax: 0.05, ay: 0.05, az: 0.85, gx: 2, gy: 1, gz: 1 }))
    expect(a.reliable).toBe(true)
  })
})
