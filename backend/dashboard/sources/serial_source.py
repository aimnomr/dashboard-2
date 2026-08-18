"""The real ground station, over USB serial."""

from __future__ import annotations

import asyncio
import logging
from typing import AsyncIterator

import serial
from serial.tools import list_ports

from .base import TelemetrySource

log = logging.getLogger(__name__)

DEFAULT_BAUD = 115200


class SerialSource(TelemetrySource):
    name = "serial"
    simulated = False

    def __init__(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        self.port = port
        self.baud = baud
        self._serial: serial.Serial | None = None
        self._closed = False

    @staticmethod
    def available_ports() -> list[tuple[str, str]]:
        """(device, description) for every serial port currently enumerated.

        The port is never hardcoded — it changes with USB slot and machine, and v1's
        hardcoded COM12 needed editing on every move. See ISS-05.
        """
        return [(p.device, p.description or "") for p in list_ports.comports()]

    async def lines(self) -> AsyncIterator[str]:
        self._serial = await asyncio.to_thread(
            serial.Serial, self.port, self.baud, timeout=1
        )
        log.info("serial open: %s @ %d baud", self.port, self.baud)

        while not self._closed:
            try:
                chunk = await asyncio.to_thread(self._serial.readline)
            except serial.SerialException as exc:
                # A USB re-enumeration mid-flight must not look like a clean end of
                # stream. v1 broke its loop and exited here, ending the session.
                log.error("serial read failed: %s", exc)
                raise

            if not chunk:
                continue  # read timeout, no data — normal between packets

            # errors="replace", not "ignore". Corruption becomes a visible U+FFFD that
            # fails parsing and reaches the operator, rather than being silently
            # repaired into a plausible-looking line as v1 did.
            yield chunk.decode("utf-8", errors="replace").strip()

    async def send_command(self, command: str) -> bool:
        if self._serial is None or not self._serial.is_open:
            log.error("cannot send %r: serial port not open", command)
            return False
        try:
            await asyncio.to_thread(self._serial.write, f"{command}\n".encode("ascii"))
            await asyncio.to_thread(self._serial.flush)
        except serial.SerialException as exc:
            log.error("failed to send %r: %s", command, exc)
            return False
        return True

    async def aclose(self) -> None:
        self._closed = True
        if self._serial is not None and self._serial.is_open:
            await asyncio.to_thread(self._serial.close)
            log.info("serial closed: %s", self.port)
