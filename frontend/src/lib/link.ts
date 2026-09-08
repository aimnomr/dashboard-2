import {
  LINK_LOST_MS,
  LINK_STALE_MS,
  type LinkState,
  type LinkStats,
} from '../types/telemetry'

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

/**
 * Render a measurement that may not exist.
 *
 * RSSI and SNR are absent — not zero — whenever a packet did not cross a radio: read
 * from an SD card, or replayed from a capture. Formatting null as 0 would show -0 dBm,
 * the strongest reading on the scale, for a measurement nobody took.
 */
export function formatMeasurement(
  value: number | null | undefined,
  digits: number,
): string {
  return value === null || value === undefined ? '—' : value.toFixed(digits)
}

/**
 * How to present packet loss.
 *
 * Rolling is the actionable figure and gets the prominence (S3): a single cumulative
 * number hides a link that has just collapsed behind twenty good minutes. Session is the
 * record, shown smaller.
 *
 * `null` stats means the generation carries no counter, and the answer is the word
 * **unavailable** — never 0%. A fabricated zero is the same failure as deriving loss
 * from `rx_index`, which is the thing `seq` exists to replace.
 */
export function lossPresentation(stats: LinkStats | null) {
  if (stats === null) {
    return {
      value: 'n/a',
      detail: 'no counter',
      tone: 'unknown' as const,
      available: false,
    }
  }

  const rolling = stats.rolling.loss_pct
  const tone = rolling >= 25 ? 'alert' : rolling >= 5 ? 'warn' : 'ok'

  return {
    value: `${rolling.toFixed(rolling >= 10 ? 0 : 1)}%`,
    detail: `session ${stats.loss_pct.toFixed(1)}% · ${stats.lost} lost`,
    tone: tone as 'ok' | 'warn' | 'alert',
    available: true,
  }
}

/**
 * Chute has three states, not two. Null means unknown, never "safe".
 *
 * Rule S8, and it took until 2026-08-29 to implement. Two things were wrong.
 *
 * **The word.** "Deployed" is a claim about a canopy, and no sensor in this system can
 * support it. The vehicle reports that it drove the mechanism; whether anything opened is
 * unknown to every part of this stack. "Commanded" is the strongest honest word.
 *
 * **The test.** This compared `chute === 1`, so a counter reading 2 fell through to
 * "Unknown" — on a vehicle whose chute had demonstrably fired. Two is reachable in normal
 * use, by three separate routes: an eject burst spans more than one vehicle cycle and the
 * counter rises per PACKET received, RESET:CHUTE then EJECT is a supported bench workflow
 * (devlog 058), and since 061 the vehicle re-arms itself so a second EJECT needs no reset
 * at all. The count is now shown rather than matched,
 * which is what S8 asked for and is also the only version that cannot go stale as the
 * number grows.
 */
export function chutePresentation(chute: number | null | undefined) {
  if (chute === 0) return { label: 'Armed', icon: '○', tone: 'ok' as const }
  // Deliberately `> 0` and not `!== 0`: a negative would be corruption, and corruption
  // reads as unknown rather than as a release.
  if (typeof chute === 'number' && chute > 0) {
    return { label: `Commanded ×${chute}`, icon: '◆', tone: 'alert' as const }
  }
  return { label: 'Unknown', icon: '?', tone: 'unknown' as const }
}

/**
 * How long after a release the Eject control stays disabled.
 *
 * ⚠ MIRRORS `CHUTE_REARM_MS` (MRC_FlightUnit_GEN4/Config.h) and `EJECT_REARM_MS`
 * (MRC_GroundStation_GEN4/Config.h). Three copies of one number now, in three
 * languages; devlog 061 records why the firmware pair cannot simply be derived, and
 * this third copy exists because the browser has no way to read either.
 *
 * Being WRONG here is cosmetic in one direction and misleading in the other. Too long
 * and the operator waits needlessly. Too short and the button re-enables while the
 * vehicle's own latch is still set, so the burst is received, `chute` rises, and the
 * panel reports a release that never drove anything — the failure devlog 058 exists to
 * prevent. If they diverge, this one should be the LONGEST of the three.
 */
export const EJECT_REARM_MS = 3000

/**
 * Whether a further release may be commanded yet, given when `chute` last rose.
 *
 * `null` means no rise has been observed in this session — either nothing has fired, or
 * the dashboard was opened after the fact. Both are "not re-arming": a vehicle that
 * fired before this page loaded re-armed long ago, and refusing on a counter that was
 * already non-zero at load is the absolute-test bug from devlog 058 in another costume.
 */
export function rearmPresentation(chuteRoseAt: number | null, now: number) {
  if (chuteRoseAt === null) return { rearming: false, secondsLeft: 0 }

  const elapsed = now - chuteRoseAt
  if (elapsed >= EJECT_REARM_MS) return { rearming: false, secondsLeft: 0 }

  /* Clamped to 1 rather than 0 so the countdown never renders "0s" while still
     refusing. A negative elapsed is clock skew, and fails closed by the same branch. */
  return {
    rearming: true,
    secondsLeft: Math.max(1, Math.ceil((EJECT_REARM_MS - elapsed) / 1000)),
  }
}
