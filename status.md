# Status

**Updated** 2026-08-18 · end of session 1

## Now

**The dashboard runs end to end against simulated telemetry.** No hardware needed.

```bash
cd backend && .venv\Scripts\activate
python -m devtools.run_mock --interval 0.3     # simulated flight
cd frontend && npm run dev                      # http://localhost:5173
```

Or single-process against the built bundle: `python -m devtools.run_mock`, then
http://127.0.0.1:8000.

**Backend** — `source → raw log → parser → WebSocket`. The mock plugs into the source
seam at the top, so development exercises the real launch code. Serial and mock are
separated by entry point: `python -m dashboard` has no flag that selects simulated data.

**Frontend** — single fixed screen, nine panels, high-contrast light theme for outdoor
sunlight. Live socket with reconnect, link freshness on an independent clock.

**Verified** — 17 backend tests, 16 frontend tests, 25 guard-hook cases, strict
TypeScript build, end-to-end serve with no external URLs in the bundle.

**Not verified** — nobody has looked at the UI in a browser. Everything visual is
reasoned, not observed.

Settled this session: rules and enforcement hooks · wiki and devlog conventions · GEN2
packet as canonical · ingest pipeline · stack · frontend design. All in
`wiki/decisions/`.

## Next

1. **Look at the UI in a browser.** First real feedback on layout, density and the
   sunlight treatment. Nothing else should be built until someone has seen it.
2. **SQLite store** — decided in `wiki/decisions/stack.md`, deferred during the frontend
   push. One flat table, one insert per packet. The only major piece of the pipeline
   still missing.
3. **`ISS-07` + `ISS-08` with the firmware member** — packet contract. Deferred to
   unblock development, not resolved. A monotonic counter remains the highest-value
   addition; without it packet loss stays undetectable.

## Blocked

- `ISS-06` — competition requirements unknown, `wiki/source/competition/` still empty
- `ISS-09` — `CANSAT_DATA` not recovered; the mock is the only telemetry source
- `ISS-02` — GEN2 ground unit firmware missing, so the uplink cannot be tested against
  hardware

## Waiting on Aiman

Deletions, since all deletion is yours:

- `frontend/src/lib/rocketModel.ts` — orphaned by the 3D revert. Currently excluded in
  `tsconfig.json`; **remove that exclude block once the file is gone.**
- Six redundant `.gitkeep` files — only `wiki/source/competition/` still needs one.
