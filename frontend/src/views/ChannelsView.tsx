import { useMemo } from 'react'
import { TimeSeriesChart, type ChartSeries } from '../components/TimeSeriesChart'
import { computeAttitude } from '../lib/attitude'
import { verticalRateSeries } from '../lib/rates'
import { applyBreaks, timebase } from '../lib/timebase'
import type { FrameRecord } from '../types/telemetry'

interface ChannelsViewProps {
  history: FrameRecord[]
}

interface Channel {
  title: string
  unit: string
  series: ChartSeries[]
  minSpan: number
  /** Shown under the title when the numbers need a caveat to be read correctly. */
  caveat?: string
}

/**
 * Every numeric channel, charted.
 *
 * A separate view rather than more panels on the flight grid. That grid is tuned to fill
 * one screen with the handful of figures that matter while something is in the air;
 * burying those among fifteen diagnostic traces would cost the thing it is for. This is
 * the other job — reading a flight afterwards, or checking a replay parses correctly —
 * and it wants breadth instead.
 */
export function ChannelsView({ history }: ChannelsViewProps) {
  const { x, channels, clock } = useMemo(() => {
    const base = timebase(history)
    const breaks = base.restarts

    const pick = (get: (r: FrameRecord) => number | null): (number | null)[] =>
      applyBreaks(history.map(get), breaks)

    const attitudes = history.map((r) => computeAttitude(r.frame))

    const list: Channel[] = [
      {
        title: 'Acceleration',
        unit: 'g',
        minSpan: 0.5,
        series: [
          { label: 'ax', values: pick((r) => r.frame.ax), stroke: 'var(--trace)' },
          { label: 'ay', values: pick((r) => r.frame.ay), stroke: 'var(--trace-2)' },
          { label: 'az', values: pick((r) => r.frame.az), stroke: 'var(--trace-3)' },
        ],
        caveat: 'gravity plus vehicle acceleration — az reads ~1 g at rest',
      },
      {
        title: 'Angular rate',
        unit: 'deg/s',
        minSpan: 10,
        series: [
          { label: 'gx', values: pick((r) => r.frame.gx), stroke: 'var(--trace)' },
          { label: 'gy', values: pick((r) => r.frame.gy), stroke: 'var(--trace-2)' },
          { label: 'gz', values: pick((r) => r.frame.gz), stroke: 'var(--trace-3)' },
        ],
      },
      {
        title: 'Attitude',
        unit: 'deg',
        minSpan: 20,
        series: [
          {
            label: 'pitch',
            values: applyBreaks(attitudes.map((a) => a.pitch), breaks),
            stroke: 'var(--trace)',
          },
          {
            label: 'roll',
            values: applyBreaks(attitudes.map((a) => a.roll), breaks),
            stroke: 'var(--trace-2)',
          },
        ],
        caveat: 'from gravity — meaningless while accelerating or tumbling',
      },
      {
        title: 'Altitude',
        unit: 'm',
        minSpan: 20,
        series: [{ label: 'alt', values: pick((r) => r.frame.alt), stroke: 'var(--trace)' }],
        caveat: 'relative to boot, not sea level',
      },
      {
        title: 'Vertical rate',
        unit: 'm/s',
        minSpan: 4,
        series: [
          {
            label: 'v-rate',
            values: applyBreaks(verticalRateSeries(history), breaks),
            stroke: 'var(--trace-3)',
          },
        ],
        caveat: 'derived over a 5-sample window',
      },
      {
        title: 'Ground speed',
        unit: 'km/h',
        minSpan: 5,
        series: [{ label: 'spd', values: pick((r) => r.frame.spd), stroke: 'var(--trace-3)' }],
        caveat: 'GPS — zero without a fix',
      },
      {
        title: 'Temperature',
        unit: 'C',
        minSpan: 1,
        series: [{ label: 'temp', values: pick((r) => r.frame.temp), stroke: 'var(--trace-2)' }],
      },
      {
        title: 'Humidity',
        unit: '%',
        minSpan: 2,
        series: [{ label: 'hum', values: pick((r) => r.frame.hum), stroke: 'var(--trace-3)' }],
      },
      {
        title: 'Pressure',
        unit: 'hPa',
        minSpan: 1,
        series: [{ label: 'pres', values: pick((r) => r.frame.pres), stroke: 'var(--trace)' }],
      },
      {
        title: 'Satellites',
        unit: 'count',
        minSpan: 4,
        series: [{ label: 'sat', values: pick((r) => r.frame.sat), stroke: 'var(--trace-2)' }],
      },
      {
        title: 'GPS accuracy',
        unit: 'HDOP',
        minSpan: 4,
        series: [{ label: 'hdop', values: pick((r) => r.frame.hdop), stroke: 'var(--trace-2)' }],
        caveat: 'lower is better — 0 means the receiver did not report it, not a perfect fix',
      },
      {
        title: 'Uplink commands',
        unit: 'count',
        minSpan: 2,
        series: [{ label: 'ul', values: pick((r) => r.frame.ul), stroke: 'var(--trace)' }],
        caveat: 'pings and ejects together; GEN3.1 firmware only',
      },
      {
        title: 'RSSI',
        unit: 'dBm',
        minSpan: 10,
        series: [{ label: 'rssi', values: pick((r) => r.frame.rssi), stroke: 'var(--trace)' }],
        caveat: 'measured by the ground station — absent on a replayed capture',
      },
      {
        title: 'SNR',
        unit: 'dB',
        minSpan: 4,
        series: [{ label: 'snr', values: pick((r) => r.frame.snr), stroke: 'var(--trace-2)' }],
        caveat: 'measured by the ground station — absent on a replayed capture',
      },
    ]

    return { x: base.seconds, channels: list, clock: base.label }
  }, [history])

  if (history.length === 0) {
    return (
      <div className="channels channels--empty">
        <p>No frames yet. Channels appear as telemetry arrives.</p>
      </div>
    )
  }

  return (
    <div className="channels">
      <div className="channels__meta">
        {history.length} samples · x axis: {clock}
      </div>
      <div className="channels__grid">
        {channels.map((channel) => (
          <section className="channel" key={channel.title}>
            <header className="channel__head">
              <h3 className="channel__title">{channel.title}</h3>
              <span className="channel__unit numeric">{channel.unit}</span>
            </header>
            {channel.caveat && <p className="channel__caveat">{channel.caveat}</p>}
            <div className="channel__chart">
              <TimeSeriesChart
                x={x}
                series={channel.series}
                minSpan={channel.minSpan}
                legend={channel.series.length > 1}
              />
            </div>
          </section>
        ))}
      </div>
    </div>
  )
}
