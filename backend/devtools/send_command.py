"""Send one uplink command to a running dashboard, from a second terminal.

    python -m devtools.send_command PING
    python -m devtools.send_command SET:DROP:15.0
    python -m devtools.send_command SET:CYCLES:2
    python -m devtools.send_command RESET:CHUTE

**The dashboard must already be running.** This does not open the serial port — it
connects to the dashboard's WebSocket and sends exactly the message the Eject button
sends. That is the whole point: the serial port is held exclusively by
`python -m dashboard`, so a tool that wanted the port for itself could only work while
the dashboard was closed, which is when you least want to be sending commands.

Validation lives in `dashboard.api.translate_command`, not here. A second copy of the
bounds in a convenience script is how the mock and the dashboard once agreed with each
other instead of with the firmware (devlog 033). This sends what you typed and prints
what came back.

What "sent" means, and does not:

    sent=true   the bytes left the PC for the ground station.

It does NOT mean the ground station transmitted them, that the vehicle received them,
or that a value was applied. The uplink carries no acknowledgement. Watch `ul` on the
dashboard, and the `[GCS]` lines in the raw feed, for what actually happened.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys

DEFAULT_URL = "ws://127.0.0.1:8000/ws"


async def send(url: str, command: str, timeout: float) -> int:
    try:
        import websockets
    except ImportError:
        print("websockets is not installed in this environment:\n"
              "    pip install websockets", file=sys.stderr)
        return 3

    try:
        async with websockets.connect(url) as ws:
            # The server greets every client with a session message before anything
            # else. Not waiting for it would leave it in the buffer and make the
            # command_ack read below return the greeting instead.
            await asyncio.wait_for(ws.recv(), timeout=timeout)

            await ws.send(json.dumps({"type": "command", "command": command}))

            # Telemetry is streaming the whole time, so the ack is not necessarily the
            # next message. Read until it arrives or the clock runs out.
            deadline = asyncio.get_event_loop().time() + timeout
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    print(f"no command_ack within {timeout:g}s", file=sys.stderr)
                    return 4

                raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
                try:
                    message = json.loads(raw)
                except json.JSONDecodeError:
                    continue

                if message.get("type") != "command_ack":
                    continue

                if message.get("sent"):
                    print(f"{message.get('command')}  sent=true  "
                          f"at {message.get('at', '')}")
                    print("watch `ul` and the raw feed - `sent` only means the bytes "
                          "left the PC")
                    return 0

                print(f"{message.get('command')}  sent=false  "
                      f"{message.get('error', 'no reason given')}", file=sys.stderr)
                return 1

    except asyncio.TimeoutError:
        print(f"timed out talking to {url}", file=sys.stderr)
        return 4
    except OSError as exc:
        print(f"could not reach {url}: {exc}\n"
              "is `python -m dashboard --port COMx` running?", file=sys.stderr)
        return 2


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m devtools.send_command",
        description="Send one uplink command through a running dashboard.",
        epilog="Commands: PING, EJECT, RESET, RESET:CHUTE, "
               "SET:DROP:<m>, SET:ARM:<m>, SET:CYCLES:<n>, SET:AUTO:<0|1>",
    )
    parser.add_argument("command", help="e.g. PING or SET:DROP:15.0")
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"dashboard WebSocket (default {DEFAULT_URL})")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args(argv)

    return asyncio.run(send(args.url, args.command.strip().upper(), args.timeout))


if __name__ == "__main__":
    raise SystemExit(main())
