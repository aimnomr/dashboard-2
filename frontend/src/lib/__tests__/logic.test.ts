import { describe, expect, it } from 'vitest'
import { computeAttitude } from '../attitude'
import { hasFix, niceScale, toLocal } from '../geo'
import { formatMeasurement, lossPresentation } from '../link'
import type { LinkStats } from '../../types/telemetry'
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

  it('treats a stable chute descent as reliable', () => {
    // DESCENT_CHUTE: az about 0.85 g, low rates. This is the phase where attitude
    // actually means something, so it must not be suppressed.
    const a = computeAttitude(frame({ ax: 0.05, ay: 0.05, az: 0.85, gx: 2, gy: 1, gz: 1 }))
    expect(a.reliable).toBe(true)
  })
})

describe('absent measurements', () => {
  // RSSI and SNR are measured by the ground station's radio as a packet arrives, so a
  // packet read from an SD capture or replayed from a file has neither. Before this
  // existed, StatusBar called .toFixed() on them directly and a replay took the whole
  // render down with a TypeError.
  it('renders a null measurement as a dash', () => {
    expect(formatMeasurement(null, 0)).toBe('—')
    expect(formatMeasurement(undefined, 1)).toBe('—')
  })

  it('never turns an absent measurement into zero', () => {
    // -0 dBm would read as the strongest signal on the scale, for a measurement
    // nobody took. This is the same failure as showing 0% packet loss with no counter.
    expect(formatMeasurement(null, 0)).not.toBe('0')
    expect(formatMeasurement(null, 0)).not.toBe('-0')
  })

  it('still formats a real measurement, including zero', () => {
    expect(formatMeasurement(-55.6, 0)).toBe('-56')
    expect(formatMeasurement(9.33, 1)).toBe('9.3')
    // A genuine 0 dB SNR is a reading, not an absence, and must survive as one.
    expect(formatMeasurement(0, 1)).toBe('0.0')
  })
})

describe('packet loss presentation', () => {
  const stats = (over: Partial<LinkStats> = {}): LinkStats => ({
    expected: 100, received: 100, lost: 0, loss_pct: 0,
    rolling: { window: 60, expected: 60, received: 60, lost: 0, loss_pct: 0 },
    crc_failed: 0, duplicates: 0, restarts: 0, baseline_seq: 1, last_seq: 100,
    ...over,
  })

  it('says unavailable rather than 0% when there is no counter', () => {
    // S5. GEN1 and GEN2 cannot produce a loss figure at all, and a reassuring 0% is the
    // same fabrication as deriving one from rx_index.
    const loss = lossPresentation(null)
    expect(loss.available).toBe(false)
    expect(loss.value).not.toContain('0')
    expect(loss.detail).toBe('no counter')
  })

  it('leads with the rolling figure, not the session one', () => {
    // S3. A cumulative number hides a link that has just collapsed behind twenty good
    // minutes — and the collapse is the half worth acting on.
    const loss = lossPresentation(stats({
      loss_pct: 2,
      rolling: { window: 60, expected: 60, received: 15, lost: 45, loss_pct: 75 },
    }))
    expect(loss.value).toBe('75%')
    expect(loss.detail).toContain('session 2.0%')
  })

  it('escalates tone as the rolling figure worsens', () => {
    const at = (loss_pct: number) =>
      lossPresentation(stats({
        rolling: { window: 60, expected: 60, received: 0, lost: 0, loss_pct },
      })).tone

    expect(at(0)).toBe('ok')
    expect(at(12)).toBe('warn')
    expect(at(60)).toBe('alert')
  })

  it('reports a genuine zero as a measurement', () => {
    // Distinct from the unavailable case above: this link really did lose nothing.
    const loss = lossPresentation(stats())
    expect(loss.available).toBe(true)
    expect(loss.value).toBe('0.0%')
  })
})
