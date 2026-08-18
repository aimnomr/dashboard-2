import { useCallback, useEffect, useRef, useState } from 'react'
import type {
  CommandAck,
  ConnectionState,
  FrameMessage,
  FrameRecord,
  RawRecord,
  ServerMessage,
  SessionMessage,
} from '../types/telemetry'

/** ~80 minutes at 1 Hz. Well beyond any single flight. */
const HISTORY_LIMIT = 5000
/** Raw feed is a debugging window, not an archive — the raw log on disk is the archive. */
const RAW_LIMIT = 200
const RECONNECT_MS = 1000

export interface TelemetryState {
  connection: ConnectionState
  session: SessionMessage | null
  /** Most recent successfully parsed frame. */
  latest: FrameRecord | null
  history: FrameRecord[]
  raw: RawRecord[]
  /** Epoch ms of the last message of any kind, including malformed and status lines. */
  lastMessageAt: number | null
  counts: { frames: number; malformed: number; status: number }
  lastAck: CommandAck | null
  sendCommand: (command: string) => void
}

export function useTelemetry(url = wsUrl()): TelemetryState {
  const [connection, setConnection] = useState<ConnectionState>('connecting')
  const [session, setSession] = useState<SessionMessage | null>(null)
  const [latest, setLatest] = useState<FrameRecord | null>(null)
  const [history, setHistory] = useState<FrameRecord[]>([])
  const [raw, setRaw] = useState<RawRecord[]>([])
  const [lastMessageAt, setLastMessageAt] = useState<number | null>(null)
  const [counts, setCounts] = useState({ frames: 0, malformed: 0, status: 0 })
  const [lastAck, setLastAck] = useState<CommandAck | null>(null)

  const socketRef = useRef<WebSocket | null>(null)
  const originRef = useRef<number | null>(null)
  const rawIdRef = useRef(0)
  const closedByUsRef = useRef(false)

  useEffect(() => {
    closedByUsRef.current = false
    let reconnectTimer: number | undefined

    const connect = () => {
      setConnection('connecting')
      const socket = new WebSocket(url)
      socketRef.current = socket

      socket.onopen = () => setConnection('open')

      socket.onmessage = (event) => {
        const message = JSON.parse(event.data as string) as ServerMessage
        const now = Date.now()
        setLastMessageAt(now)

        switch (message.type) {
          case 'session':
            setSession(message)
            break
          case 'frame':
            handleFrame(message, now)
            break
          case 'status':
            setCounts((c) => ({ ...c, status: c.status + 1 }))
            pushRaw({
              id: rawIdRef.current++,
              at: now,
              text: message.raw,
              kind: 'status',
              ok: true,
            })
            break
          case 'command_ack':
            setLastAck(message)
            break
        }
      }

      socket.onclose = () => {
        setConnection('closed')
        if (!closedByUsRef.current) {
          // The backend may simply not be up yet during development. Keep trying —
          // a dashboard that gives up needs restarting at exactly the wrong moment.
          reconnectTimer = window.setTimeout(connect, RECONNECT_MS)
        }
      }

      socket.onerror = () => socket.close()
    }

    const pushRaw = (record: RawRecord) => {
      setRaw((prev) => {
        const next = prev.length >= RAW_LIMIT ? prev.slice(prev.length - RAW_LIMIT + 1) : prev
        return [...next, record]
      })
    }

    const handleFrame = (message: FrameMessage, now: number) => {
      pushRaw({
        id: rawIdRef.current++,
        at: now,
        text: message.raw,
        kind: 'frame',
        ok: message.ok,
        error: message.error,
      })

      if (!message.ok || message.frame === null) {
        // Malformed lines are counted and shown, never silently dropped — corruption
        // the operator cannot see is corruption they will act on.
        setCounts((c) => ({ ...c, malformed: c.malformed + 1 }))
        return
      }

      if (originRef.current === null) originRef.current = now
      const record: FrameRecord = {
        rxIndex: message.rx_index,
        t: now - originRef.current,
        receivedAt: now,
        frame: message.frame,
      }

      setLatest(record)
      setCounts((c) => ({ ...c, frames: c.frames + 1 }))
      setHistory((prev) => {
        const next = prev.length >= HISTORY_LIMIT
          ? prev.slice(prev.length - HISTORY_LIMIT + 1)
          : prev
        return [...next, record]
      })
    }

    connect()

    return () => {
      closedByUsRef.current = true
      if (reconnectTimer) window.clearTimeout(reconnectTimer)
      socketRef.current?.close()
    }
  }, [url])

  const sendCommand = useCallback((command: string) => {
    const socket = socketRef.current
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      setLastAck({ type: 'command_ack', command, sent: false, error: 'not connected' })
      return
    }
    socket.send(JSON.stringify({ type: 'command', command }))
  }, [])

  return {
    connection,
    session,
    latest,
    history,
    raw,
    lastMessageAt,
    counts,
    lastAck,
    sendCommand,
  }
}

function wsUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${window.location.host}/ws`
}
