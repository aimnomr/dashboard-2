"""Launch entry point.

    python -m dashboard --port COM12

This module reads from the real ground station over serial and NOTHING ELSE. There is
deliberately no flag that selects simulated data: mock telemetry lives in a separate
package with its own entry point, so it cannot be reached by accident at the pad.

See backend/devtools/README.md
"""

from __future__ import annotations

import argparse
import sys

from .runner import DEFAULT_LOG_DIR, configure_logging, run
from .sources.serial_source import DEFAULT_BAUD, SerialSource


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m dashboard",
        description="MRCC CanSat ground dashboard (live telemetry).",
    )
    parser.add_argument("--port", help="serial port, e.g. COM12. Omit to list options.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8000)
    parser.add_argument("--log-dir", default=str(DEFAULT_LOG_DIR))
    parser.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    configure_logging(args.verbose)
    ports = SerialSource.available_ports()

    if args.list_ports:
        _print_ports(ports)
        return 0

    port = args.port
    if port is None:
        if len(ports) == 1:
            port = ports[0][0]
            print(f"Using the only serial port available: {port} ({ports[0][1]})")
        else:
            print("No --port given.\n", file=sys.stderr)
            _print_ports(ports)
            return 2

    run(
        SerialSource(port, args.baud),
        host=args.host,
        port=args.http_port,
        log_dir=args.log_dir,
    )
    return 0


def _print_ports(ports: list[tuple[str, str]]) -> None:
    if not ports:
        print("No serial ports found. Is the ground unit plugged in, and is the")
        print("CP210x / CH34x USB driver installed? (ISS-12)")
        return
    print("Available serial ports:")
    for device, description in ports:
        print(f"  {device:10s} {description}")


if __name__ == "__main__":
    raise SystemExit(main())
