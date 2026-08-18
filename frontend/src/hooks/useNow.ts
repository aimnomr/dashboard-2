import { useEffect, useState } from 'react'

/**
 * A ticking clock.
 *
 * "Time since last packet" has to keep counting up when nothing is arriving — that is
 * precisely the situation it exists to reveal. Rendering only on incoming messages
 * would freeze the number at the moment the link died, which is the single most
 * misleading thing this dashboard could do.
 */
export function useNow(intervalMs = 250): number {
  const [now, setNow] = useState(() => Date.now())

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), intervalMs)
    return () => window.clearInterval(id)
  }, [intervalMs])

  return now
}
