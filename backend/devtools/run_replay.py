"""Replay entry point — CAPTURED TELEMETRY, NOT A LIVE LINK.

    python -m devtools.run_replay 20260820-015822-serial.log        # a session log
    python -m devtools.run_replay 20260820-015822-serial.log --speed 20
    python -m devtools.run_replay FLIGHT21.CSV                      # a vehicle SD card
    python -m devtools.run_replay FLIGHT21.CSV --loop

Runs the real pipeline (raw log -> parser -> WebSocket) against a capture instead of the
serial port. The data is real; the *liveness* is not, which is why this sits in devtools
alongside the mock and is unreachable from `python -m dashboard`.

**Any line-oriented capture works — there is no extension filter anywhere in this path.**
A `.log` written by a live session replays exactly like a `.CSV` pulled off the vehicle's
SD card: the parser classifies each line on its own merits, so a session log's `[GCS]`
status lines and a mixture of packet generations all come through. The examples led with
.CSV for a while and left the impression this was SD-only; it never was.

A bare filename is looked up in logs/raw/ and then backend/tests/fixtures/, so a session
log or a committed capture can be replayed by name from anywhere.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from dashboard.runner import DEFAULT_LOG_DIR, REPO_ROOT, configure_logging, run
from dashboard.sources.file_source import FileSource

#: Searched in order when the argument is not a path that exists as given.
SEARCH_DIRS = (
    REPO_ROOT / "logs" / "raw",
    REPO_ROOT / "backend" / "tests" / "fixtures",
)


def resolve_capture(argument: str) -> Path:
    candidate = Path(argument)
    if candidate.is_file():
        return candidate

    if candidate.parent == Path("."):
        for directory in SEARCH_DIRS:
            found = directory / argument
            if found.is_file():
                return found

    searched = "\n".join(f"  {d}" for d in SEARCH_DIRS)
    raise SystemExit(
        f"No capture found for {argument!r}.\nSearched as given, then in:\n{searched}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m devtools.run_replay",
        description="Replay a captured telemetry log through the dashboard (dev only).",
    )
    parser.add_argument("capture",
                        help="path to a captured log, or a bare filename to search for")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="playback multiplier. 1.0 is real time (default). Above "
                             "roughly x60 there is no further gain: Windows rounds every "
                             "sleep up to its ~15 ms timer granularity")
    parser.add_argument("--loop", action="store_true",
                        help="repeat when the capture ends. Each pass restarts the "
                             "vehicle clock and seq, as a real reboot would")
    parser.add_argument("--hold", action="store_true",
                        help="keep the dashboard up after the capture ends, instead of "
                             "shutting down with it. The link goes stale then lost, as "
                             "it would if the vehicle stopped transmitting")
    parser.add_argument("--interval", type=float,
                        help="force a fixed gap in seconds, ignoring the vehicle clock")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8000)
    parser.add_argument("--log-dir", default=str(DEFAULT_LOG_DIR))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    configure_logging(args.verbose)

    capture = resolve_capture(args.capture)
    print(f"Replaying {capture}", file=sys.stderr)

    run(
        FileSource(capture, speed=args.speed, loop=args.loop,
                   interval=args.interval, hold=args.hold),
        host=args.host,
        port=args.http_port,
        log_dir=args.log_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
