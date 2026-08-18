# Issue Tracker

Open questions and conflicts that need resolving. One line per issue in the index; full detail
below. Referenced from the wiki by ID (`ISS-01`).

**Status:** 🔴 Open · 🟡 Deferred · 🟢 Resolved
**Updating:** change the status in both the index and the detail entry, and fill in
**Resolution**. Resolved issues stay in the document — they are the record of *why* a thing is
the way it is. Actions taken go in the devlog; this file tracks state.

---

## Index

| ID | Issue | Status | Owner | Blocks |
|:--|:--|:--|:--|:--|
| [ISS-01](#iss-01) | Which packet generation is canonical | 🟢 Resolved | — | — |
| [ISS-02](#iss-02) | GEN2 ground unit firmware missing | 🟡 Deferred | Firmware member | Two-way testing |
| [ISS-03](#iss-03) | Heltec V3 vs V4 labelling | 🟢 Resolved | — | — |
| [ISS-04](#iss-04) | RA-01 cannot operate at 919 MHz | 🟡 Deferred | Firmware member | Ground station BOM |
| [ISS-05](#iss-05) | COM port assignments contradict | 🟡 Deferred | — | Ingest config |
| [ISS-06](#iss-06) | Competition requirements unknown | 🔴 Open | Aiman | Scope, mandatory displays |
| [ISS-07](#iss-07) | `CHUTE:` field is not numeric | 🟡 Deferred | Firmware member | — (worked around) |
| [ISS-08](#iss-08) | No packet counter, timestamp, or checksum | 🟡 Deferred | Firmware member | Loss detection, true time axis |
| [ISS-09](#iss-09) | Raw source files no longer present | 🔴 Open | Aiman | Test data availability |
| [ISS-10](#iss-10) | Ground unit output buffer smaller than payload | 🟢 Resolved | — | — |
| [ISS-11](#iss-11) | Offline map tiles for field use | 🔴 Open | — | GPS map panel |
| [ISS-12](#iss-12) | Field laptop provisioning | 🔴 Open | Aiman | Launch day |
| [ISS-13](#iss-13) | Frequency coordination with other teams | 🔴 Open | Aiman | Launch day, link viability |

---

<a id="iss-01"></a>
## ISS-01 — Which packet generation is canonical

**Status** 🟢 Resolved · **Raised** 2026-08-18 · **Resolved** 2026-08-18

**Problem.** GEN1 emits 14 fields (16 at the PC); GEN2 emits 15 (17 at the PC). The v1 Python
bridge accepts only 16 and would reject every GEN2 packet.

**Resolution.** **GEN2 is the canonical packet format.** GEN1 is the one-way build; GEN2 is the
two-way build and its packet is GEN1's with the chute flag appended and precision raised. The
dashboard targets the 17-field GEN2 form. Documented in `source/firmware/packet-format.md`.

---

<a id="iss-02"></a>
## ISS-02 — GEN2 ground unit firmware missing

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** `MRC_TwoWay_Flowchart.html` describes a ground unit that polls serial every 10 ms,
matches `EJECT`, calls `fireEject()` and transmits the command 5× at 300 ms spacing. The only
ground unit firmware in the source set (`14.GROUND_919MHz.ino`) does none of this — it uses a
blocking `radio.receive()` and never reads from serial.

**Impact.** The uplink path cannot be exercised end to end. The dashboard's EJECT control has
nothing to talk to until this firmware exists.

**Needed to resolve.** Locate or write the GEN2 ground unit firmware; confirm the serial command
wire format (currently `EJECT\n`) and whether any acknowledgement is returned.

---

<a id="iss-03"></a>
## ISS-03 — Heltec V3 vs V4 labelling

**Status** 🟢 Resolved · **Raised** 2026-08-18 · **Resolved** 2026-08-18

**Problem.** Source files disagree on board revision — V3 in `Pins_Assignment.md` and
`13.CANSAT_919MHZ.ino`, V4 in `MRC_FlightUnit_V7.ino`.

**Resolution.** Naming error in the files. Not a hardware difference. LoRa pinout is identical
either way. No action.

---

<a id="iss-04"></a>
## ISS-04 — RA-01 cannot operate at 919 MHz

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Owner** Firmware member

**Problem.** `Pins_Assignment.md` describes an Arduino Nano wired to an **RA-01** transceiver.
The RA-01 carries an SX1278, a 433 MHz part. The link runs at **919 MHz**. That band needs an
RA-01H / SX1276 variant. No firmware in the source set targets this combination.

**Needed to resolve.** Confirm whether the Nano + RA-01 section is obsolete, a spare/alternate
unit, or a planned change. If planned, the part number needs correcting.

---

<a id="iss-05"></a>
## ISS-05 — COM port assignments contradict

**Status** 🟡 Deferred · **Raised** 2026-08-18

**Problem.**

| Source | COM5 | COM12 |
|---|---|---|
| `serial_to_mqtt_V3.py` | — | "new ground unit port" |
| `MRC_DualDebug_Flow.json` | Ground Unit | Flight Unit |

**Impact.** Low — port numbers are machine- and USB-slot-specific and change on re-enumeration.
The real lesson is that the v2 ingest should **not hardcode a port**; it should enumerate and
either auto-detect or let the operator pick.

**Needed to resolve.** Confirm which unit is normally connected during a live run, and whether
both are ever connected at once outside bench testing.

---

<a id="iss-06"></a>
## ISS-06 — Competition requirements unknown

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** `wiki/source/competition/` is empty. Nothing in the source set names the
competition, its rules, mandatory telemetry, required displays, submission format, or dates.

**Impact.** Scope risk. A mandated field, display, or export format discovered late is a rework
of the packet contract and the UI.

**Needed to resolve.** Add the competition rulebook or the relevant extract to
`wiki/source/competition/`. Even the competition's name and date would narrow it.

---

<a id="iss-07"></a>
## ISS-07 — `CHUTE:` field is not numeric

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Deferred** 2026-08-18 · **Owner** Firmware member

**Problem.** GEN2 field 15 is the literal string `CHUTE:0` / `CHUTE:1`, not a bare digit. It is
the only non-numeric field in an otherwise uniform numeric CSV, so every parser needs a special
case, and `float()` on it raises.

**Interim decision (2026-08-18).** Take **option (b)** — the **dashboard strips the `CHUTE:`
prefix on ingest**, and GEN2 is consumed exactly as it is emitted today. No firmware change is
requested, so dashboard development is not blocked waiting on another team member.

- **(a)** Firmware drops the prefix, emitting `0` / `1` — uniform packet, one-line firmware
  change, but breaks anything already reading the `CHUTE:` form.
- **(b)** ✅ Dashboard strips the prefix on ingest — no firmware change, permanent special case.

**Still to revisit.** Whether the prefix is eventually dropped at source. Low urgency: the
workaround is a few lines and is confined to the parser. Worth raising with the firmware member
whenever `ISS-08` is discussed, since both are packet-contract changes.

---

<a id="iss-08"></a>
## ISS-08 — No packet counter, timestamp, or checksum

**Status** 🟡 Deferred · **Raised** 2026-08-18 · **Deferred** 2026-08-18 · **Owner** Firmware member

**Deferred (2026-08-18).** Development proceeds against the GEN2 packet **exactly as it is
emitted today** — no additions requested — so that the dashboard is not blocked on a firmware
change owned by another team member.

The consequences below are therefore **accepted for now** and must be designed around:

- The dashboard **cannot detect packet loss**. Link health must be presented from what is
  available — time since last packet, RSSI and SNR — and must not imply gap counts it cannot
  compute.
- The time axis is **PC arrival time**, at 1 s resolution, not sampling time.
- A corrupted-but-well-formed line is **indistinguishable from good data**. Range checks against
  the known clamps are the only defence available.

Revisit before flight if the firmware member has capacity — a monotonic counter remains the
single highest-value addition, and the parser should be written so that adopting one later is
additive rather than a rewrite.

**Problem.** Neither generation includes any of the three.

| Missing | Consequence |
|---|---|
| Packet counter | Packet loss is invisible. A dropped packet is indistinguishable from a dead transmitter — and with LoRa, loss is normal. |
| Onboard timestamp | Time is stamped on PC arrival, at 1 s resolution, local timezone. True sampling time is unrecoverable. |
| Checksum | LoRa's CRC covers the RF hop only. Nothing covers the USB serial hop or a truncated `Serial.println`. |

**Evidence.** Visible in the v1 data: `CANSAT_DATA` has 5,481 readings with no way to tell how
many packets never arrived, and 989 readings missing their `signal_data` row with no diagnostic.

**Impact.** Link-health display, gap detection, and any accurate time axis all depend on this.
A monotonic counter is the single highest-value addition.

**Needed to resolve.** Agree the additions with the firmware member. Cost is a few bytes per
packet at 1 Hz — airtime is not a constraint here.

---

<a id="iss-09"></a>
## ISS-09 — Raw source files no longer present

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** The raw files removed from `wiki/source/` root are not present anywhere under
`D:\MRCC`, and were never committed to git — the initial commit contained only `.claude/`,
`.gitattributes` and `README.md`. The extracted wiki documents are currently the only record of
their contents in this repo.

**Most significant loss: `CANSAT_DATA`** — 5,481 rows of real logged telemetry, the only genuine
flight data in the project. Its schema and statistics are preserved in
`source/previous-system/telemetry-database.md`, but **the rows themselves are not**. That
dataset is the natural test input for the v2 parser and lets the UI be built and validated with
no hardware on the desk.

**Needed to resolve.** Confirm the originals exist elsewhere (backup, another machine, the
firmware member). If `CANSAT_DATA` can be recovered, it is worth keeping — the `.ino` and
Node-RED files matter less now that they are documented.

---

<a id="iss-10"></a>
## ISS-10 — Ground unit output buffer smaller than flight payload

**Status** 🟢 Resolved · **Raised** 2026-08-18 · **Resolved** 2026-08-18

**Resolution.** Measured rather than assumed. A full-width GEN2 packet fits both buffers:

| | flight payload (buf 180) | + RSSI/SNR (buf 160) | ground headroom |
|---|---|---|---|
| Typical | 97 | 111 | 49 chars |
| Worst case | 118 | 132 | **28 chars** |

Worst case assumes a negative sign on every float, three-digit longitude, altitude below
launch, and full-scale IMU readings — wider than any real flight produces. Truncation cannot
occur with the GEN2 format as specified.

⚠️ **The headroom is not unlimited.** Resolving `ISS-08` would consume most of it: a packet
counter costs roughly 11 characters and a CRC16 suffix about 5, leaving around 12. Re-measure
before adding fields, and raise the ground unit buffer at the same time.

**Original problem, retained for context.** The GEN2 flight unit builds its packet in a
**180-byte** buffer. The ground unit re-emits it plus RSSI and SNR through a **160-byte**
buffer:

```c
char buf[180];     // MRC_FlightUnit_V7.ino  — payload
char output[160];  // 14.GROUND_919MHz.ino   — payload + ",%.1f,%.2f"
```

`snprintf` truncates silently rather than overflowing, so a long packet would reach the PC
**short of its trailing fields** — losing SNR, RSSI, or the chute flag with no error raised.

**Impact.** Unlikely at typical field widths, but it fails silently and it fails at the end of
the packet, where the link-health and chute-state fields live. Worst case the dashboard shows a
stale chute state during descent.

**Needed to resolve.** Size the ground unit buffer at or above 180 + RSSI/SNR width, and confirm
worst-case packet length against the GEN2 format specifiers.

---

<a id="iss-11"></a>
## ISS-11 — Offline map tiles for field use

**Status** 🔴 Open · **Raised** 2026-08-18

**Problem.** The GPS panel needs a map. Leaflet or MapLibre fetch tiles from a remote server by
default. **There is no internet at the launch site**, so the map renders as blank grey squares
exactly when it is needed.

**Options.**

- **(a)** Pre-cache tiles for the launch site into the repo or a local folder, and point Leaflet
  at a local tile path. Requires knowing the launch coordinates and zoom range in advance, and
  tile licensing must be checked for OpenStreetMap.
- **(b)** Skip the basemap. Plot the ground track as a plain XY trace with a scale bar and the
  launch point marked. No dependency, no tiles, works anywhere.
- **(c)** Both — XY trace always available, basemap layered underneath when tiles are present.

**Impact.** Easy to solve early, painful to discover on launch day. Blocks nothing else — the
GPS panel can be built against a plain XY trace and gain a basemap later.

**Needed to resolve.** Launch site coordinates (also needed for `ISS-06`), then decide.

---

<a id="iss-12"></a>
## ISS-12 — Field laptop provisioning

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** The chosen stack has runtime prerequisites that must be present *before* leaving for
the launch site, because there is no internet there:

- Python installed, with the virtual environment created and dependencies installed
- The frontend **already built** — `npm run build` needs the network
- The correct USB serial driver for the Heltec board (CP210x or CH34x)
- COM port identified, or auto-detection verified on that specific machine

**Impact.** Any one of these missing means no dashboard on launch day, with no way to fix it in
the field. This is the accepted cost of the Python + browser choice — cheap to manage, expensive
to forget.

**Needed to resolve.** A written pre-launch checklist, and a dry run on the actual field laptop
with the actual ground unit — not on a development machine.

---

<a id="iss-13"></a>
## ISS-13 — Frequency coordination with other teams

**Status** 🔴 Open · **Raised** 2026-08-18 · **Owner** Aiman

**Problem.** Other teams will fly rockets carrying similar LoRa systems. If any of them
transmits on 919.0 MHz, their packets and ours destroy each other on the air. Measured
collision rate at SF7 with a 185 ms CSV packet in a 1 s cycle:

| Other teams on the same frequency | Our packets destroyed |
|---|---|
| 1 | **37%** |
| 3 | **75%** |
| 5 | **90%** |

A packet is lost if any other transmission overlaps it at all, so the vulnerable window
is twice the airtime — 370 ms of every second.

**The common misconception, stated plainly: a different sync word does not prevent
collisions.** It prevents *decoding* someone else's packet. It does nothing about their
RF energy landing on top of ours. The same is true of a team ID in the payload and of the
CRC — those detect damage, they do not prevent it.

| Mechanism | Prevents collision | Prevents decoding wrong packet |
|---|---|---|
| **Different frequency** | ✅ | ✅ |
| Different spreading factor | mostly | ✅ |
| Different sync word | ❌ | ✅ |
| Team ID in payload | ❌ | ✅ |
| CRC | ❌ | detects only |

**Only frequency separation gives real isolation.**

**Needed to resolve.**

1. Ask the organisers whether frequencies are assigned. This is now the most important
   unknown inside `ISS-06`.
2. If not assigned, agree channels with the other teams on the day. Space them at least
   250 kHz apart — twice the 125 kHz bandwidth.
3. Keep frequency, sync word and team ID in the `Config.h` block of both units so they
   can be changed at the launch site without hunting through code.
4. Keep **SF7**. Shortest airtime is the smallest collision target; in a crowded band a
   low spreading factor is a defence, not a compromise.

**If a clean channel proves unavailable**, revisit binary framing: 39 bytes instead of
109 halves the collision exposure (16% instead of 37% against one other team). Decide the
frequency question first — see `wiki/decisions/gen3-packet-format.md`.

**Diagnostic worth building regardless.** Have the ground station count received packets
that are not ours and report `[GCS] foreign packet, N so far`. That single line separates
"the band is busy" from "my vehicle is silent" — otherwise indistinguishable, and they
look identical at exactly the wrong moment.

