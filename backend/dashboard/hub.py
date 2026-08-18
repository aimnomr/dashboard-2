"""Fan-out of envelopes to connected browsers."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

log = logging.getLogger(__name__)


class Hub:
    """Tracks connected clients and broadcasts to them.

    A client that has gone away must never be able to stall the pipeline — telemetry
    ingest continues regardless of whether anyone is watching.
    """

    def __init__(self) -> None:
        self._clients: set[Any] = set()
        self._lock = asyncio.Lock()

    async def register(self, websocket: Any) -> None:
        async with self._lock:
            self._clients.add(websocket)

    async def unregister(self, websocket: Any) -> None:
        async with self._lock:
            self._clients.discard(websocket)

    @property
    def client_count(self) -> int:
        return len(self._clients)

    async def broadcast(self, message: dict) -> None:
        async with self._lock:
            targets = list(self._clients)
        if not targets:
            return

        results = await asyncio.gather(
            *(ws.send_json(message) for ws in targets), return_exceptions=True
        )
        dead = [ws for ws, r in zip(targets, results) if isinstance(r, Exception)]
        if dead:
            async with self._lock:
                for ws in dead:
                    self._clients.discard(ws)
            log.debug("dropped %d disconnected client(s)", len(dead))
