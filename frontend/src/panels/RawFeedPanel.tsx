import { useEffect, useRef, useState } from 'react'
import { Panel } from '../components/Panel'
import type { RawRecord } from '../types/telemetry'

/**
 * Unfiltered view of what is arriving on the wire — malformed lines included.
 *
 * Unglamorous, and the first thing anyone reaches for when something is wrong in the
 * field. Everything else on this screen is an interpretation; this is the evidence.
 */
export function RawFeedPanel({ raw }: { raw: RawRecord[] }) {
  const listRef = useRef<HTMLDivElement | null>(null)
  const [following, setFollowing] = useState(true)

  useEffect(() => {
    if (!following) return
    const el = listRef.current
    if (el) el.scrollTop = el.scrollHeight
  }, [raw, following])

  const onScroll = () => {
    const el = listRef.current
    if (!el) return
    // Reading back through history should not be yanked away by the next packet.
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 24
    setFollowing(atBottom)
  }

  return (
    <Panel
      title="Raw feed"
      area="raw"
      note={following ? undefined : <span className="chip chip--warn">▲ Paused</span>}
    >
      <div className="rawfeed" ref={listRef} onScroll={onScroll}>
        {raw.length === 0 && <div className="rawfeed__empty">Waiting for data…</div>}
        {raw.map((record) => (
          <div
            key={record.id}
            className={`rawfeed__line ${
              !record.ok ? 'is-bad' : record.kind === 'status' ? 'is-status' : ''
            }`}
            title={record.error}
          >
            <span className="rawfeed__text">{record.text}</span>
            {!record.ok && <span className="rawfeed__error">{record.error}</span>}
          </div>
        ))}
      </div>
      {!following && (
        <button type="button" className="btn btn--small" onClick={() => setFollowing(true)}>
          Resume
        </button>
      )}
    </Panel>
  )
}
