import { describe, expect, it } from 'vitest'
import {
  decimalsFor,
  fieldValue,
  FLAT_SCAN_MAX,
  formatDelta,
  formatFieldValue,
  readPacket,
} from '../packet'
import type { FrameRecord, PacketField, TelemetryFrame } from '../../types/telemetry'

/** A cut-down field table, entries copied verbatim from `parser.FIELD_DOC`. */
const FIELDS: PacketField[] = [
  { i: 1, name: 'seq', type: 'int', unit: null, fmt: '%lu', desc: 'packet counter' },
  { i: 2, name: 'ms', type: 'int', unit: 'ms', fmt: '%lu', desc: 'vehicle uptime' },
  { i: 3, name: 'pres', type: 'float', unit: 'hPa', fmt: '%.2f', desc: 'pressure' },
  {
    i: 4, name: 'lat', type: 'float', unit: 'deg', fmt: '%.5f', desc: 'latitude',
    sentinel: { value: 0.0, means: 'no fix' },
  },
  { i: 5, name: 'sat', type: 'int', unit: 'count', fmt: '%d', desc: 'satellites' },
  { i: 6, name: 'ul', type: 'int', unit: 'count', fmt: '%lu', desc: 'uplink commands' },
  {
    i: 7, name: 'fixq', type: 'int', unit: null, fmt: '%d', desc: 'fix verdict',
    sentinel: { value: -1, means: 'not reported by the receiver' },
  },
]

const base: TelemetryFrame = {
  temp: 32.5, hum: 78, pres: 1007.9, alt: 0,
  ax: 0, ay: 0, az: 1,
  gx: 0, gy: 0, gz: 0,
  lat: 3.0783, lng: 101.7122, spd: 0, sat: 9,
  chute: 0, ul: 0, hdop: 1.1, fixq: 1, rssi: -55.6, snr: 9.33,
}

const record = (i: number, over: Partial<TelemetryFrame> = {}, ms = i * 1000): FrameRecord => ({
  rxIndex: i,
  t: i * 1000,
  receivedAt: i * 1000,
  vehicleMs: ms,
  seq: i,
  frame: { ...base, ...over },
})

const row = (history: FrameRecord[], name: string) => {
  const readout = readPacket(history, FIELDS)
  if (readout === null) throw new Error('no readout')
  const found = readout.rows.find((r) => r.field.name === name)
  if (found === undefined) throw new Error('no row for ' + name)
  return found
}

describe('a field is rendered at the precision the vehicle transmits', () => {
  // The whole reason the field table is fetched rather than written here. Five decimals
  // on a latitude is about a metre; three is about a hundred, and both look equally
  // authoritative on screen.

  it('reads the decimal count out of the firmware printf spec', () => {
    expect(decimalsFor('%.5f')).toBe(5)
    expect(decimalsFor('%.2f')).toBe(2)
    expect(decimalsFor('%d')).toBe(0)
    expect(decimalsFor('%lu')).toBe(0)
  })

  it('formats to that precision and no further', () => {
    expect(formatFieldValue(3.0783, '%.5f')).toBe('3.07830')
    expect(formatFieldValue(1007.9, '%.2f')).toBe('1007.90')
    expect(formatFieldValue(1482376, '%lu')).toBe('1482376')
  })

  it('renders a missing value as a dash, never as zero', () => {
    // -0 dBm is the strongest reading on the RSSI scale. A null formatted as 0 would
    // put it on screen for a measurement nobody took.
    expect(formatFieldValue(null, '%.1f')).toBe('—')
  })
})

describe('the change column', () => {
  it('signs the change and keeps the field precision', () => {
    expect(formatDelta(0.03, '%.2f')).toBe('+0.03')
    expect(formatDelta(-0.03, '%.2f')).toBe('-0.03')
    expect(formatDelta(1, '%lu')).toBe('+1')
  })

  it('marks no change with a dot rather than a formatted zero', () => {
    // "+0.00" reads as a measured quantity. This column is about movement.
    expect(formatDelta(0, '%.2f')).toBe('·')
  })

  it('has no answer when there is nothing to compare against', () => {
    expect(formatDelta(null, '%.2f')).toBe('—')
  })
})

describe('finding a field on the record', () => {
  it('reaches past the frame for the two fields that live beside it', () => {
    // seq and ms are packet fields 1 and 2 but sit on the record, next to the arrival
    // metadata. A lookup that only searched the frame would report the packet counter
    // missing on every generation that has one.
    const r = record(7, {}, 7000)
    expect(fieldValue(r, 'seq')).toBe(7)
    expect(fieldValue(r, 'ms')).toBe(7000)
    expect(fieldValue(r, 'pres')).toBe(1007.9)
  })

  it('returns null for a field this firmware does not carry', () => {
    const r = record(1, { ul: null })
    expect(fieldValue(r, 'ul')).toBeNull()
  })
})

describe('reading the latest packet', () => {
  it('has nothing to describe before the first frame', () => {
    // A table of dashes would imply a packet arrived and was empty.
    expect(readPacket([], FIELDS)).toBeNull()
  })

  it('differences against the previous frame', () => {
    const history = [record(1, { pres: 1007.9 }), record(2, { pres: 1007.87 })]
    expect(row(history, 'pres').delta).toBeCloseTo(-0.03, 5)
    expect(row(history, 'seq').delta).toBe(1)
  })

  it('offers no delta on the very first frame', () => {
    expect(row([record(1)], 'pres').delta).toBeNull()
  })
})

describe('spotting a channel that has stopped moving', () => {
  // The diagnostic half. A frozen sensor and a calm one draw the same flat trace, and
  // nothing on the dashboard said which was which.

  it('counts consecutive packets holding the same value, current one included', () => {
    const history = [
      record(1, { sat: 4 }),
      record(2, { sat: 0 }),
      record(3, { sat: 0 }),
      record(4, { sat: 0 }),
    ]
    expect(row(history, 'sat').flat).toBe(3)
  })

  it('reports 1 for a value that has just changed', () => {
    const history = [record(1, { sat: 0 }), record(2, { sat: 0 }), record(3, { sat: 9 })]
    expect(row(history, 'sat').flat).toBe(1)
  })

  it('does not call an absent field a stuck one', () => {
    // GEN3.0 carries no `ul`. Counting nulls as a run would put "flat 300+" beside every
    // GEN3.1 field on an older flight and blame a sensor for missing firmware.
    const history = [record(1, { ul: null }), record(2, { ul: null }), record(3, { ul: null })]
    const ul = row(history, 'ul')
    expect(ul.flat).toBe(0)
    expect(ul.value).toBeNull()
  })

  it('stops the scan at a bound and says the run is longer', () => {
    const history = Array.from({ length: FLAT_SCAN_MAX + 50 }, (_, i) =>
      record(i + 1, { sat: 0 }, (i + 1) * 1000),
    )
    const sat = row(history, 'sat')
    expect(sat.flat).toBe(FLAT_SCAN_MAX)
    expect(sat.flatCapped).toBe(true)
  })
})

describe('a reboot is a discontinuity, not a measurement', () => {
  // Same signal the charts break on: the vehicle clock going backwards.

  const rebooted = () => [
    record(1481, { pres: 1007.9 }, 1481000),
    record(1, { pres: 1007.9 }, 1000),
  ]

  it('suppresses every delta across it', () => {
    // Differencing here would report seq -1480, which reads as catastrophic packet loss
    // when nothing at all was lost.
    const history = rebooted()
    const readout = readPacket(history, FIELDS)
    expect(readout?.rebooted).toBe(true)
    expect(readout?.rows.every((r) => r.delta === null)).toBe(true)
  })

  it('does not let a flat run cross it', () => {
    // The pressure sensor read 1007.90 either side, but it was re-initialised between.
    // That is two runs, and calling it one blames hardware that has since restarted.
    expect(row(rebooted(), 'pres').flat).toBe(1)
  })
})

describe('sentinels are annotated, never substituted', () => {
  // This view is the one that has to match the raw line byte for byte. Rewriting
  // 0.00000 into "no fix" would make it the single surface that cannot be checked.

  it('flags a sentinel and still shows the number', () => {
    const lat = row([record(1, { lat: 0 })], 'lat')
    expect(lat.sentinel).toBe('no fix')
    expect(formatFieldValue(lat.value, lat.field.fmt)).toBe('0.00000')
  })

  it('flags the negative sentinel too', () => {
    expect(row([record(1, { fixq: -1 })], 'fixq').sentinel)
      .toBe('not reported by the receiver')
  })

  it('leaves a real reading unflagged', () => {
    expect(row([record(1, { lat: 3.0783 })], 'lat').sentinel).toBeNull()
    expect(row([record(1, { fixq: 1 })], 'fixq').sentinel).toBeNull()
  })

  it('says nothing about a field that has no sentinel', () => {
    expect(row([record(1, { pres: 0 })], 'pres').sentinel).toBeNull()
  })
})
