# dashboard-2

Ground station dashboard for the MRCC CanSat. Second iteration.

Live telemetry over LoRa at 1 Hz, received by a Heltec ground unit and read over USB
serial. Python backend owns the port and both logs; React frontend displays.

## Quick start

**Development** — simulated flight, no hardware needed:

```bash
# terminal 1 — backend with a synthetic flight
cd backend
python -m venv .venv
.venv\Scripts\activate            # Windows
pip install -r requirements-dev.txt
python -m devtools.run_mock

# terminal 2 — frontend with hot reload
cd frontend
npm install
npm run dev                        # http://localhost:5173
```

**Launch** — real ground unit, one process:

```bash
cd frontend && npm run build       # needs network. Do this BEFORE the launch site.
cd ../backend
python -m dashboard --list-ports   # find the ground unit
python -m dashboard --port COM12   # then open http://127.0.0.1:8000
```

There is deliberately **no flag on `python -m dashboard` that selects simulated data.**
Mock telemetry lives in a separate package with its own entry point so it cannot be
reached by accident at the pad. See `backend/devtools/README.md`.

Every command this system is run by — replay, mock, uplink, tests, diagnostics — with
its flags and its caveats: **[`COMMANDS.md`](COMMANDS.md)**.

## Layout

```
backend/dashboard/   launch code — serial, raw log, parser, WebSocket
backend/devtools/    development only, never imported by dashboard/
frontend/            React + Vite dashboard
wiki/                project knowledge base
  source/              externally supplied facts
  decisions/           conclusions reached, with rationale
  issues.md            issue tracker (ISS-xx)
devlog/              append-only record of executed changes
status.md            current state — read at session start
COMMANDS.md          every command, with flags and caveats
```

## Where to start reading

| | |
|---|---|
| What the packet looks like **now** | `wiki/decisions/gen3-packet-format.md` — GEN3.1 |
| What GEN1/GEN2 sent | `wiki/source/firmware/packet-format.md` — external fact, not updated |
| Why the pipeline is shaped this way | `wiki/decisions/ingest-pipeline.md` |
| Stack and repo layout | `wiki/decisions/stack.md` |
| UI decisions | `wiki/decisions/frontend.md` |
| Open questions | `wiki/issues.md` |

## Testing

```bash
cd backend && python -m pytest tests -q
cd frontend && npm run typecheck
```
