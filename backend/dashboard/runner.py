"""Wiring shared by the launch entry point and the dev entry point.

Both paths run *this* code. That is the point: developing against the mock exercises
the real raw logger, the real parser and the real transport, so bugs surface during
development instead of at the pad.
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

import uvicorn

from .api import create_app
from .hub import Hub
from .pipeline import Pipeline
from .rawlog import RawLog
from .sources.base import TelemetrySource

log = logging.getLogger(__name__)

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOG_DIR = REPO_ROOT / "logs"
DEFAULT_FRONTEND_DIST = REPO_ROOT / "frontend" / "dist"


def configure_logging(verbose: bool = False) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s  %(levelname)-7s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )


async def serve(source: TelemetrySource, *, host: str = "127.0.0.1", port: int = 8000,
                log_dir: Path | None = None,
                frontend_dist: Path | None = None) -> None:
    log_dir = log_dir or DEFAULT_LOG_DIR
    frontend_dist = frontend_dist or DEFAULT_FRONTEND_DIST

    hub = Hub()
    rawlog = RawLog.create(log_dir, source.name)
    pipeline = Pipeline(source, rawlog, hub)
    app = create_app(hub, source, pipeline, frontend_dist)

    log.info("raw log: %s", rawlog.path)
    log.info("dashboard: http://%s:%d", host, port)
    if source.simulated:
        log.warning("=" * 68)
        log.warning("SIMULATED DATA — this is not a real flight.")
        log.warning("=" * 68)

    server = uvicorn.Server(uvicorn.Config(app, host=host, port=port, log_level="warning"))

    pipeline_task = asyncio.create_task(pipeline.run(), name="pipeline")
    server_task = asyncio.create_task(server.serve(), name="http")

    done, pending = await asyncio.wait(
        {pipeline_task, server_task}, return_when=asyncio.FIRST_COMPLETED
    )
    for task in pending:
        task.cancel()
    await asyncio.gather(*pending, return_exceptions=True)
    for task in done:
        if (exc := task.exception()) is not None:
            raise exc


def run(source: TelemetrySource, **kwargs) -> None:
    try:
        asyncio.run(serve(source, **kwargs))
    except KeyboardInterrupt:
        log.info("stopped by operator")
