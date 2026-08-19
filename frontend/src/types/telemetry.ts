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

/** Loss over a bounded span of recent packets. */
export interface LinkWindow {
  /** Packets in the window, e.g. 60 — one minute at 1 Hz. */
  window: number
  expected: number
  received: number
  lost: number
  loss_pct: number
}

export interface LinkStats {
  /** Since the first packet this backend session saw — not since the vehicle booted. */
  expected: number
  received: number
  lost: number
  loss_pct: number
  /**
   * The actionable figure during descent. A single cumulative number hides a link that
   * has just collapsed behind twenty good minutes.
   */
  rolling: LinkWindow
  /** Frames rejected by their checksum. RF corruption, counted separately from loss. */
  crc_failed: number
  duplicates: number
  restarts: number
  baseline_seq: number
  last_seq: number
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
  /**
   * Arrival time at the PC. Use for staleness and link health only — the vehicle clock
   * cannot measure silence, because a dropped packet brings no timestamp with it.
   */
  pc_time: string
  /**
   * The vehicle's own packet counter. GEN3 only; null for GEN1/GEN2 (ISS-08).
   *
   * Do NOT subtract two of these to get packet loss. Baselines, vehicle restarts,
   * duplicates and the rolling window all have to be handled together, and that belongs
   * to the backend (step 2 of the GEN3 plan) so every client sees the same figure
   * regardless of when it connected.
   */
  seq: number | null
  /**
   * Vehicle uptime in ms, at the moment of sampling. GEN3 only; null otherwise.
   *
   * This is the clock everything derived from the data belongs on — descent rate,
   * integrated yaw, the chart x-axis — because it carries no link or scheduling jitter.
   */
  vehicle_ms: number | null
  /** GEN3 only. Null means the generation carries no checksum, NOT that one passed. */
  crc_ok: boolean | null
  /**
   * Packet loss, computed by the backend.
   *
   * Null when the generation carries no counter and loss cannot be computed at all —
   * the UI must then read "unavailable", never 0%.
   *
   * Never recompute this in the browser. The backend has seen the whole session; a
   * client that connected late has not, and would show a different number for the same
   * flight.
   */
  link: LinkStats | null
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

/**
 * The vehicle rebooted: its sequence counter went backwards.
 *
 * Sent as its own message, before the frame that revealed it, so the chart can break
 * the trace rather than draw one segment across the discontinuity.
 */
export interface VehicleRestartMessage {
  type: 'vehicle_restart'
  previous_seq: number
  new_seq: number
  pc_time: string
  simulated: boolean
}

export type ServerMessage =
  | SessionMessage
  | FrameMessage
  | StatusMessage
  | CommandAck
  | VehicleRestartMessage

/** A frame plus the moment it landed, for charting. */
export interface FrameRecord {
  rxIndex: number
  /** Milliseconds since the first frame of this session, by PC arrival time. */
  t: number
  receivedAt: number
  /** Vehicle uptime in ms at sampling. Null for GEN1/GEN2, which have no clock. */
  vehicleMs: number | null
  /** Vehicle packet counter. Null for GEN1/GEN2. Never a loss metric. */
  seq: number | null
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
 * Which screen is showing.
 *
 * `flight` is the tuned single-screen grid used while something is in the air.
 * `channels` charts every numeric field, for reading a flight afterwards or checking a
 * replay parsed correctly.
 */
export type View = 'flight' | 'channels'

/**
 * Freshness of the downlink, derived only from arrival time. At 1 Hz, three seconds
 * is three missed packets.
 */
export type LinkState = 'live' | 'stale' | 'lost' | 'waiting'

export const LINK_STALE_MS = 3_000
export const LINK_LOST_MS = 10_000
