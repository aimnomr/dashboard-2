"""Development entry point — SIMULATED TELEMETRY.

    python -m devtools.run_mock

Runs the real pipeline (raw log -> parser -> WebSocket) against a synthetic flight
instead of the serial port. Nothing here is used at launch.
"""

from __future__ import annotations

import argparse

from dashboard.runner import DEFAULT_LOG_DIR, configure_logging, run

from .mock_source import MockSource


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m devtools.run_mock",
        description="Run the dashboard against a simulated flight (development only).",
    )
    parser.add_argument("--interval", type=float, default=1.0,
                        help="seconds between packets (default 1.0, matching 1 Hz)")
    parser.add_argument("--once", action="store_true",
                        help="stop after one 78 s flight, as the real firmware does")
    parser.add_argument("--clean", action="store_true",
                        help="no malformed lines or [GCS] status lines")
    parser.add_argument("--seed", type=int, help="fix the RNG for a reproducible flight")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8000)
    parser.add_argument("--log-dir", default=str(DEFAULT_LOG_DIR))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    configure_logging(args.verbose)

    run(
        MockSource(
            interval=args.interval,
            loop=not args.once,
            noise=not args.clean,
            seed=args.seed,
        ),
        host=args.host,
        port=args.http_port,
        log_dir=args.log_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
