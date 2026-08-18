import { LINK_LOST_MS, LINK_STALE_MS, type LinkState } from '../types/telemetry'

/**
 * Downlink freshness, from arrival time alone.
 *
 * This is deliberately the only link metric available. There is no packet counter
 * (ISS-08), so packet loss cannot be computed — and a loss figure invented from
 * `rx_index` would be a number the operator might act on. Time since last packet,
 * RSSI and SNR are what the data actually supports.
 */
export function linkState(lastMessageAt: number | null, now: number): LinkState {
  if (lastMessageAt === null) return 'waiting'
  const age = now - lastMessageAt
  if (age >= LINK_LOST_MS) return 'lost'
  if (age >= LINK_STALE_MS) return 'stale'
  return 'live'
}

export const LINK_PRESENTATION: Record<
  LinkState,
  { label: string; icon: string; tone: 'ok' | 'warn' | 'alert' | 'unknown' }
> = {
  // Icon and word carry the state as well as the colour: sunlight and colour vision
  // deficiency both take the colour away.
  live: { label: 'Live', icon: '●', tone: 'ok' },
  stale: { label: 'Stale', icon: '▲', tone: 'warn' },
  lost: { label: 'Signal lost', icon: '■', tone: 'alert' },
  waiting: { label: 'Waiting', icon: '○', tone: 'unknown' },
}

export function formatAge(lastMessageAt: number | null, now: number): string {
  if (lastMessageAt === null) return '—'
  const seconds = (now - lastMessageAt) / 1000
  if (seconds < 100) return `${seconds.toFixed(1)}s`
  const minutes = Math.floor(seconds / 60)
  return `${minutes}m ${Math.floor(seconds % 60)}s`
}

/** Chute has three states, not two. Null means unknown, never "safe". */
export function chutePresentation(chute: number | null | undefined) {
  if (chute === 1) return { label: 'Deployed', icon: '◆', tone: 'alert' as const }
  if (chute === 0) return { label: 'Armed', icon: '○', tone: 'ok' as const }
  return { label: 'Unknown', icon: '?', tone: 'unknown' as const }
}
