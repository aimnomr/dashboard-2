"""The source seam.

Everything upstream of the PC is replaceable here: the real serial link, the
development mock, and — later — a file replayer. Downstream of this interface the
pipeline is identical, which is what keeps replay cheap and keeps the mock honest.

See wiki/decisions/ingest-pipeline.md
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import AsyncIterator


class TelemetrySource(ABC):
    """Yields raw lines exactly as they arrive. No parsing, no filtering.

    Implementations must not drop malformed input. Corruption is data: it tells the
    operator the link is degrading, and it is unrecoverable once discarded.
    """

    #: Short identifier, used in log filenames and reported to the UI.
    name: str = "unknown"

    #: True when the data is synthetic. The UI shows an unmissable banner when set,
    #: because the worst outcome available here is somebody trusting a fake flight.
    simulated: bool = False

    @abstractmethod
    def lines(self) -> AsyncIterator[str]:
        """Async-iterate raw lines, newline stripped, as they arrive."""
        raise NotImplementedError

    async def send_command(self, command: str) -> bool:
        """Send an uplink command. Returns whether it was transmitted.

        Transmitted is not the same as received, and neither is the same as acted
        upon — the LoRa link carries no acknowledgement. Callers must not report
        this as confirmation of anything happening on the vehicle.
        """
        return False

    async def aclose(self) -> None:
        """Release the underlying resource. Safe to call more than once."""
