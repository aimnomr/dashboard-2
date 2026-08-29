"""FastAPI app: WebSocket telemetry, uplink commands, and the built frontend."""

from __future__ import annotations

import logging
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

from .contract import CONTRACT_VERSION, field_table
from .hub import Hub
from .parser import GEN3_LINK_FIELDS
from .pipeline import Pipeline
from .sources.base import TelemetrySource

log = logging.getLogger(__name__)

#: UI command name -> the exact bytes the ground station compares against.
#:
#: Both an allowlist and a translation. Allowlist because the uplink fires a parachute
#: and must never be a passthrough for arbitrary strings; translation because the wire
#: protocol is defined by the firmware, in
#: `firmware/MRC_GroundStation_GEN3/Config.h` (`CMD_EJECT`, `CMD_PING`), and the
#: dashboard does not get to invent it.
#:
#: This mapping did not exist until 2026-08-19, and its absence was a real defect: the
#: dashboard sent "EJECT" while the ground station's `handleCommand()` compares against
#: "CMD:EJECT", so every eject was answered with `[GCS] unknown command`. It survived
#: because MockSource accepted the dashboard's spelling — the mock agreed with the
#: dashboard instead of with the firmware, so both sides of every test shared the same
#: wrong assumption. If this mapping changes, `mock_source.py` changes with it.
UPLINK_COMMANDS: dict[str, str] = {
    "EJECT": "CMD:EJECT",
    # Fires nothing. Tests the uplink and nothing else, which is the only way to prove
    # the path works without deploying a parachute to prove it.
    "PING": "CMD:PING",
}

ALLOWED_COMMANDS = frozenset(UPLINK_COMMANDS)

#: GEN4 adds commands that carry a VALUE, which a fixed allowlist cannot express.
#:
#: The allowlist above is not decoration — "the uplink fires a parachute and must never
#: be a passthrough for arbitrary strings". So the value-carrying commands are admitted
#: by an explicit grammar instead, and the rule is unchanged: anything that does not
#: match exactly is refused here and never reaches the serial port.
#:
#: Mirrors `CMD_SET_PREFIX`, `CMD_RESET` and `CMD_RESET_CHUTE` in
#: `firmware/MRC_GroundStation_GEN4/Config.h`, and the bounds mirror
#: `firmware/MRC_FlightUnit_GEN4/Config.h`. Three places now hold these numbers, which
#: is two too many — but the alternative is the vehicle being the only thing that
#: knows, and finding out by being refused in flight. If they change, change all three.
#:
#: A GEN3 ground station answers any of these with `[GCS] unknown command`, harmlessly.
GEN4_SIMPLE_COMMANDS: dict[str, str] = {
    "RESET": "CMD:RESET",
    "RESET:CHUTE": "CMD:RESET:CHUTE",
}

#: key -> (kind, low, high). Inclusive bounds.
GEN4_SET_KEYS: dict[str, tuple[type, float, float]] = {
    "DROP": (float, 2.0, 100.0),
    "ARM": (float, 5.0, 200.0),
    "CYCLES": (int, 1, 10),
    "AUTO": (int, 0, 1),
}


def translate_command(command: str) -> tuple[str | None, str | None]:
    """UI command -> the exact bytes the ground station expects.

    Returns `(wire, None)` when accepted, `(None, reason)` when refused. Refusing here
    rather than at the vehicle is the point: `ul` rises whether the vehicle applies a
    value or rejects it, so a bad value transmitted is a command that looks confirmed
    and changes nothing. The ground station validates too; this stops it earlier.
    """
    if command in UPLINK_COMMANDS:
        return UPLINK_COMMANDS[command], None

    if command in GEN4_SIMPLE_COMMANDS:
        return GEN4_SIMPLE_COMMANDS[command], None

    if not command.startswith("SET:"):
        return None, "unknown command"

    parts = command.split(":")
    if len(parts) != 3 or not parts[2]:
        return None, "expected SET:KEY:VALUE"

    _, key, raw = parts
    if key not in GEN4_SET_KEYS:
        return None, f"unknown SET key {key!r}"

    kind, low, high = GEN4_SET_KEYS[key]
    try:
        value = kind(raw)
    except ValueError:
        return None, f"{key} value must be {kind.__name__}, got {raw!r}"

    if not low <= value <= high:
        return None, f"{key} out of range: {value} not in [{low}, {high}]"

    return f"CMD:SET:{key}:{raw}", None


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
        # ---- the packet contract -------------------------------------------------
        #
        # The same table written into every log's `.meta.json`, generated from
        # `parser.FIELD_DOC`. The Channels view renders a numeric readout of the live
        # packet from it: wire index, label, unit, the field's own precision, and which
        # values are sentinels rather than measurements.
        #
        # Sent rather than hard-coded in the frontend for the reason `types/telemetry.ts`
        # already gives about parsing — a second description of the wire format drifts
        # from the real one and is believed anyway. Sent once per connection, ~3 KB.
        "contract": CONTRACT_VERSION,
        "fields": field_table(),
        # Mirrors `packet.outside_checksum` in the sidecar. These are appended by the
        # GROUND station after the CRC, so they are not the vehicle's word and are not
        # covered by the checksum that vouches for everything else on the line.
        "outside_checksum": list(GEN3_LINK_FIELDS),
    }


async def _handle_client_message(websocket: WebSocket, message: dict,
                                 source: TelemetrySource) -> None:
    if message.get("type") != "command":
        return

    # .upper() is safe for every command in the grammar: the keys are uppercase and
    # the only values are numbers. Do not extend this with a case-sensitive payload
    # without moving the normalisation into translate_command().
    command = str(message.get("command", "")).strip().upper()
    wire, error = translate_command(command)
    if wire is None:
        await websocket.send_json({
            "type": "command_ack",
            "command": command,
            "sent": False,
            "error": error,
        })
        return
    sent = await source.send_command(wire)
    log.warning("uplink %s (wire %r) -> transmitted=%s", command, wire, sent)

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
