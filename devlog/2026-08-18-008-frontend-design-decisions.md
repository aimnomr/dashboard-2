# 008 · Frontend design decisions

**Date** 2026-08-18
**Type** decision
**Refs** ISS-02, ISS-08, ISS-09, ISS-11

## What

Settled the frontend design and recorded it in `wiki/decisions/frontend.md`:

- **Layout** — single fixed screen, no scrolling, nothing behind a tab
- **Theme** — high-contrast light, designed for sunlight
- **Panels** — all nine in the first version
- **Mock feed** — Python mock server over WebSocket, not a TypeScript port
- **Wire contract** — envelope carrying both the raw line and the parsed frame *(proposed,
  pending confirmation)*

## Why

**Layout.** During descent the operator has roughly one second to read the vehicle's state.
Anything behind a tab or below the fold is unavailable at the moment it matters.

**Theme.** The dashboard runs on a laptop outdoors in Malaysia. Glare washes out low-contrast
greys, so the conventional dark "mission control" aesthetic is actively wrong for the operating
environment. Cheap to design for now, impossible to retrofit at the pad.

**Mock feed.** Chosen over a TypeScript port so the flight profile has one implementation, the
frontend always talks to a real socket with no mock/live branching, and the same tool serves as
a backend test fixture later.

## Result

The mock choice surfaced a design problem. "GEN2 CSV lines over WebSocket" would have forced the
frontend to parse, producing a second parser free to drift from the backend's — the v1 failure
mode in a new form. Resolved with an envelope carrying both `raw` and a server-parsed `frame`,
so parsing stays server-side and the raw feed panel still gets its lines.

Three constraints deliberately encoded in the contract and the panel spec:

- **`rx_index` is named for what it is** — lines arrived, not packets sent. `ISS-08` means loss
  cannot be detected; naming the field `seq` or `packet_id` would invite a false loss indicator
  built on top of it. Link health may show time since last packet, RSSI and SNR, and nothing
  else.
- **`sent` means sent.** EJECT has no acknowledgement path; deployment is confirmed only
  indirectly by `CHUTE:1` in later telemetry. Showing "deployed" on button press would be a lie
  the operator might act on.
- **Malformed lines are transmitted, not dropped**, so corruption is visible in the raw feed
  rather than presenting as a mysteriously still dashboard.

Also decided: no information carried by colour alone — every state gets an icon, shape or word,
covering both glare and colour vision deficiency.

Ground track ships as a plain XY trace with launch point and scale bar, which sidesteps `ISS-11`
entirely for now. The mock server partly answers `ISS-09` — the real telemetry was lost, but a
faithful substitute is regenerable from the documentation.
