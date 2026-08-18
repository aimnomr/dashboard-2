# 010 · Frontend scaffold and first panels

**Date** 2026-08-18
**Type** change
**Refs** ISS-08, ISS-12

## What

Vite + React + TypeScript frontend, with the design tokens, layout grid, telemetry hook
and the first two panels.

```
frontend/
├── vite.config.ts              proxies /ws and /api to the backend in dev
├── src/
│   ├── styles/tokens.css       sun-first palette, all custom properties
│   ├── styles/global.css       app shell, grid, panels, state chips
│   ├── types/telemetry.ts      wire contract, mirrors the backend
│   ├── hooks/useTelemetry.ts   WebSocket, reconnect, history, raw feed
│   ├── hooks/useNow.ts         independent clock
│   ├── lib/link.ts             link + chute state derivation
│   ├── components/             Panel, StatusBar, TimeSeriesChart (uPlot)
│   ├── panels/AltitudePanel.tsx
│   └── App.tsx                 full grid; unbuilt panels are placeholders
```

The remaining seven panels render as dashed placeholders so the whole layout can be
reviewed before they are filled in.

## Why

Placeholders rather than an empty screen: the layout is the thing under review at this
checkpoint, and it cannot be judged from two panels floating in space.

## Result

**Verified.** `npm run build` passes strict TypeScript. The backend serves the built
bundle with assets loading, `/api/session` responding, and `/ws` still routing correctly
behind the static mount — mount ordering was the specific risk there.

**No external URLs in the built page.** Checked explicitly, because there is no internet
at the launch site (ISS-12). No web fonts, no CDN; the type stack is system fonts only.

Decisions that became code:

- **`useNow` ticks independently of incoming data.** Rendering only on messages would
  freeze "time since last packet" at the moment the link died — the single most
  misleading thing this dashboard could do. It counts up on its own.
- **A lost link pulses**, and carries an icon and a word as well as a colour. Sunlight
  and colour vision deficiency both remove colour as a channel.
- **Chute has three states, not two.** `null` renders as UNKNOWN, never a reassuring
  ARMED. GEN1 packets carry no chute field at all.
- **The status bar shows "Received", not "Sent" or "Lost".** There is no packet counter
  (ISS-08), so no loss figure is computed or displayed. Malformed lines are counted
  separately and shown.
- **Malformed frames still reach the UI** and are counted; they are excluded from charts
  but never silently dropped.
- **The socket reconnects on close.** A dashboard that gives up needs restarting at
  exactly the wrong moment.
- **uPlot, canvas-based**, for the ~1800 points per series a 30-minute flight produces.
- **Tabular figures** on every displayed value, so digits do not jitter sideways and can
  be read at a glance.

Root `README.md` expanded with quick start, repo layout and a reading order for
teammates. Bundle is 203 kB raw / 72 kB gzipped.

Still open: `frontend/dist/` is **not** gitignored. Given ISS-12 there is an argument
for committing the built bundle so a field laptop can clone and run without a network.
Not decided.
