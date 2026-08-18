"""Reference implementation and cross-check for the GEN3 packet.

    python firmware/tests/verify_gen3.py

Three separate implementations must agree byte for byte on this checksum: the flight
unit computes it, the ground station recomputes it to decide whether `chute` can be
trusted, and the dashboard verifies it. This file is the reference the other two are
checked against, and it is a transliteration of the C in
`firmware/MRC_GroundStation_GEN3/Radio.ino` so a divergence shows up here first.
"""

from __future__ import annotations

import sys


def crc16_ccitt(data: bytes) -> int:
    """CRC16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build(body: str) -> str:
    """Wrap a packet body (everything between '$' and '*') with marker and checksum."""
    return f"${body}*{crc16_ccitt(body.encode()):04X}"


def packet_crc_valid(packet: str) -> bool:
    if not packet or packet[0] != "$":
        return False
    star = packet.rfind("*")
    if star == -1 or len(packet) - star - 1 < 4:
        return False
    try:
        expected = int(packet[star + 1:star + 5], 16)
    except ValueError:
        return False
    return crc16_ccitt(packet[1:star].encode()) == expected


def parse_chute(packet: str) -> int:
    """Last comma-separated field before '*'. -1 when it cannot be read."""
    star = packet.rfind("*")
    if star == -1:
        return -1
    comma = packet.rfind(",", 0, star)
    if comma == -1:
        return -1
    length = star - comma - 1
    if length == 0 or length > 5:
        return -1
    try:
        return int(packet[comma + 1:star])
    except ValueError:
        return -1


ARMED = build(
    "MRC,412,412340,31.52,70.4,1010.02,148.3,0.012,-0.008,0.998,"
    "0.31,-0.22,0.10,3.07830,101.71220,8.2,9,0"
)
FIRED = build(
    "MRC,689,689115,29.84,74.1,1013.55,2.1,0.921,0.383,-0.052,"
    "-0.40,-0.30,4.10,3.07902,101.71188,1.4,11,3"
)

CASES = [
    # name,                packet,                                  crc,   chute
    ("valid armed",        ARMED,                                   True,  0),
    ("valid commanded",    FIRED,                                   True,  3),
    ("one bit flipped",    ARMED[:20] + ("1" if ARMED[20] != "1" else "2") + ARMED[21:],
                                                                    False, 0),
    ("truncated mid-packet", ARMED[:60],                            False, -1),
    ("checksum truncated", ARMED[:-2],                              False, 0),
    ("no star",            ARMED.replace("*", ""),                  False, -1),
    ("wrong team marker",  "$XYZ,1,2,3*0000",                       False, 3),
    ("empty chute field",  build("MRC,1,2,3,"),                     True,  -1),
    ("chute field too long", build("MRC,1,2,1234567"),              True,  -1),
    ("garbage",            "hello world",                           False, -1),
    ("bare $",             "$",                                     False, -1),
]

#: Known-good checksums. If these ever change, the wire format changed and every
#: implementation must be revisited — that is the point of pinning them.
GOLDEN = {ARMED[-4:]: "DA98", FIRED[-4:]: "AEAF"}


def main() -> int:
    failures: list[str] = []

    for name, packet, want_crc, want_chute in CASES:
        got_crc = packet_crc_valid(packet)
        got_chute = parse_chute(packet)
        ok = got_crc == want_crc and got_chute == want_chute
        if not ok:
            failures.append(name)
        print(f"{'ok  ' if ok else 'FAIL'} {name:22s} "
              f"crc={str(got_crc):5s}(want {str(want_crc):5s})  "
              f"chute={got_chute:3d} (want {want_chute:3d})")

    for actual, expected in GOLDEN.items():
        if actual != expected:
            failures.append(f"golden checksum drifted: {actual} != {expected}")

    print()
    print(f"{len(CASES) - len([f for f in failures if not f.startswith('golden')])}"
          f"/{len(CASES)} cases passed")
    if failures:
        print("FAILED:", ", ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
