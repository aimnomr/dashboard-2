/**
 * Wire contract — mirrors backend/dashboard/pipeline.py and api.py.
 *
 * Parsing happens server-side only. These types describe what arrives already parsed;
 * the frontend never splits a CSV line. A second parser here would be free to drift
 * from the real one, which is exactly the failure mode this shape exists to prevent.
 */

export interface TelemetryFrame {
  temp: number
  hum: number
  pres: number
  /** Metres, relative to boot altitude — not above sea level. */
  alt: number
  ax: number
  ay: number
  az: number
  gx: number
  gy: number
  gz: number
  lat: number
  lng: number
  spd: number
  sat: number
  /**
   * 0 armed, 1 deployed, or null when the packet carries no chute field at all
   * (GEN1). Null is NOT "not deployed" — it means unknown, and must be displayed
   * as such rather than as a reassuring ARMED.
   */
  chute: number | null
  /**
   * Measured by the ground station's radio as the packet arrives — so it exists only
   * when the packet actually crossed a radio. A packet read from an SD capture or
   * replayed from a file has no such measurement, and both arrive as null.
   *
   * Null is NOT 0. Rendering a missing measurement as 0 would put a -0 dBm reading on
   * screen, which reads as the strongest possible signal.
   */
  rssi: number | null
  snr: number | null
}

export interface SessionMessage {
  type: 'session'
  source: string
  /** True when the feed is synthetic. Drives an unmissable banner. */
  simulated: boolean
  rx_index: number
  server_time: string
}

export interface FrameMessage {
  type: 'frame'
  /**
   * How many telemetry-looking lines have ARRIVED. Not a packet counter — the vehicle
   * does not send one (ISS-08). A jump here means nothing was received, not that
   * something was lost, so no loss figure may be derived from it.
   */
  rx_index: number
  /** Arrival time at the PC, not sampling time. There is no onboard clock. */
  pc_time: string
  raw: string
  ok: boolean
  frame: TelemetryFrame | null
  simulated: boolean
  generation?: 'GEN1' | 'GEN2' | 'GEN3'
  error?: string
  warnings?: string[]
}

/** Ground unit status line, e.g. "[GCS] Timeout - no packet". Never parsed. */
export interface StatusMessage {
  type: 'status'
  pc_time: string
  raw: string
  simulated: boolean
}

export interface CommandAck {
  type: 'command_ack'
  command: string
  /**
   * The bytes left the PC. NOT that the ground unit transmitted them, that the
   * vehicle received them, or that the chute fired. The link carries no
   * acknowledgement; deployment is confirmed only by chute === 1 in later telemetry.
   */
  sent: boolean
  at?: string
  error?: string
}

export type ServerMessage =
  | SessionMessage
  | FrameMessage
  | StatusMessage
  | CommandAck

/** A frame plus the moment it landed, for charting. */
export interface FrameRecord {
  rxIndex: number
  /** Milliseconds since the first frame of this session. */
  t: number
  receivedAt: number
  frame: TelemetryFrame
}

export interface RawRecord {
  id: number
  at: number
  text: string
  kind: 'frame' | 'status'
  ok: boolean
  error?: string
}

export type ConnectionState = 'connecting' | 'open' | 'closed'

/**
 * Freshness of the downlink, derived only from arrival time. At 1 Hz, three seconds
 * is three missed packets.
 */
export type LinkState = 'live' | 'stale' | 'lost' | 'waiting'

export const LINK_STALE_MS = 3_000
export const LINK_LOST_MS = 10_000
