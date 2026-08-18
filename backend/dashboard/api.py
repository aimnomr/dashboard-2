"""FastAPI app: WebSocket telemetry, uplink commands, and the built frontend."""

from __future__ import annotations

import logging
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

from .hub import Hub
from .pipeline import Pipeline
from .sources.base import TelemetrySource

log = logging.getLogger(__name__)

#: Uplink commands the server will forward. Deliberately an allowlist — the uplink
#: fires a parachute, so it is not a passthrough for arbitrary strings.
ALLOWED_COMMANDS = frozenset({"EJECT"})


def create_app(hub: Hub, source: TelemetrySource, pipeline: Pipeline,
               frontend_dist: Path | None = None) -> FastAPI:
    app = FastAPI(title="MRCC CanSat Dashboard", docs_url=None, redoc_url=None)

    @app.get("/api/session")
    async def session() -> JSONResponse:
        return JSONResponse(_session_message(source, pipeline))

    @app.websocket("/ws")
    async def telemetry(websocket: WebSocket) -> None:
        await websocket.accept()
        # Sent before anything else so a client knows immediately whether it is
        # looking at real telemetry or a simulation.
        await websocket.send_json(_session_message(source, pipeline))
        await hub.register(websocket)
        try:
            while True:
                message = await websocket.receive_json()
                await _handle_client_message(websocket, message, source)
        except WebSocketDisconnect:
            pass
        except Exception:
            log.exception("websocket client error")
        finally:
            await hub.unregister(websocket)

    if frontend_dist and frontend_dist.is_dir():
        app.mount("/", StaticFiles(directory=frontend_dist, html=True), name="frontend")
        log.info("serving frontend from %s", frontend_dist)
    else:
        @app.get("/")
        async def no_frontend() -> JSONResponse:
            return JSONResponse(
                {
                    "status": "backend running, frontend not built",
                    "hint": "cd frontend && npm install && npm run build",
                    "websocket": "/ws",
                },
                status_code=503,
            )

    return app


def _session_message(source: TelemetrySource, pipeline: Pipeline) -> dict:
    return {
        "type": "session",
        "source": source.name,
        # The UI shows an unmissable banner when true. The worst outcome available
        # here is somebody at the pad trusting a simulated flight.
        "simulated": source.simulated,
        "rx_index": pipeline.rx_index,
        "server_time": datetime.now(timezone.utc).isoformat(),
    }


async def _handle_client_message(websocket: WebSocket, message: dict,
                                 source: TelemetrySource) -> None:
    if message.get("type") != "command":
        return

    command = str(message.get("command", "")).strip().upper()
    if command not in ALLOWED_COMMANDS:
        await websocket.send_json({
            "type": "command_ack",
            "command": command,
            "sent": False,
            "error": "unknown command",
        })
        return

    sent = await source.send_command(command)
    log.warning("uplink command %s -> transmitted=%s", command, sent)

    await websocket.send_json({
        "type": "command_ack",
        "command": command,
        # `sent` means the bytes left the PC. It does NOT mean the ground unit
        # transmitted them, that the vehicle received them, or that the chute fired.
        # The link carries no acknowledgement; deployment is only ever confirmed
        # indirectly, by CHUTE:1 appearing in later telemetry.
        "sent": sent,
        "at": datetime.now(timezone.utc).isoformat(),
    })
