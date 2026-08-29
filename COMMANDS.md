# Commands

Every command this system is run by, with what it does and what it will not do.

**This is not a procedure.** `wiki/decisions/pre-launch-checklist.md` is the procedure —
the order to do things in on a launch day, and what to STOP on. This file answers the
narrower question: *what do I type.*

**Nor is it a cheatsheet.** [`COMMANDS-QUICK.md`](COMMANDS-QUICK.md) is that — the same
commands with every explanation stripped out, for someone who has already read this once
and wants to copy a line rather than retype it. Read this file first: several commands
here do not do what their name suggests, and the quick sheet does not stop to say so.

Backend commands are run from `backend/`, frontend commands from `frontend/`, and the two
firmware commands from the repo root. Each section says which.

**Every Python entry point here takes `--help`**, and its output is generated from the
code rather than copied into this file. When the two disagree, `--help` is right.

```bash
python -m dashboard --help
python -m devtools.run_mock --help
python -m devtools.run_replay --help
python -m devtools.send_command --help
```

---

## Contents

| | |
|---|---|
| [0 · Setup](#0--setup-once-per-machine) | venv, dependencies, the frontend bundle |
| [1 · Launch](#1--launch--real-ground-unit) | the only command that reads the radio |
| [2 · Mock](#2--mock--a-simulated-flight) | a synthetic flight, no hardware |
| [3 · Replay](#3--replay--a-captured-flight) | real telemetry from a file |
| [4 · Uplink](#4--uplink-commands-ping-eject-gen4-set) | PING, EJECT, and the GEN4 `SET` grammar |
| [5 · Frontend](#5--frontend) | dev server, build, preview |
| [6 · Tests](#6--tests) | backend, frontend, firmware |
| [7 · Firmware](#7--firmware) | what gets flashed where |
| [8 · Diagnostics](#8--diagnostics) | ports, GPS, UART |

---

## 0 · Setup (once per machine)

```bash
cd backend
python -m venv .venv
.venv\Scripts\activate          # Windows.  source .venv/bin/activate elsewhere
pip install -r requirements.txt
```

`requirements.txt` is launch-only: `fastapi`, `uvicorn`, `pyserial`. Nothing else is
needed to fly.

**Activate the venv in every new terminal**, not just the one you created it in. This is
the first thing that goes wrong and it does not look like what it is — a bare `python`
finds the system interpreter, which has none of these installed, and the failure is
`ModuleNotFoundError: No module named 'uvicorn'` rather than anything mentioning the
environment. It applies to every backend command in sections 1 to 4 and 6, and to the
second terminal in section 4 as much as the first.

```bash
cd backend
.venv\Scripts\activate                 # Windows.  source .venv/bin/activate elsewhere
```

```bash
pip install -r requirements-dev.txt    # adds pytest, httpx, pandas
pip install websockets                 # only for section 4 — see the trap there
```

```bash
cd frontend
npm install
npm run build                          # produces frontend/dist/
```

- **`npm install` and `npm run build` need the network.** There is none at the launch
  site. Both happen before you leave, on the machine that will be there.
- **The backend serves `frontend/dist/`.** If it does not exist you get a 503 with a
  build hint and nothing else — and you cannot build it at the pad.
- `websockets` is deliberately not in `requirements-dev.txt`; it is only used by one
  convenience script, never by the dashboard.

---

## 1 · Launch — real ground unit

Run from `backend/`. **This is the only entry point that opens the serial port, and it
has no flag that can select simulated data.** Choosing the mock requires running a
different module from a different package, so it cannot be reached by fumbling an option
at the pad.

```bash
python -m dashboard --list-ports        # find the ground unit
python -m dashboard --port COM12        # then open http://127.0.0.1:8000
```

| Flag | Default | Effect |
|---|---|---|
| `--port COM12` | — | serial port. Omit it and, if exactly one port exists, it is used |
| `--baud 115200` | 115200 | ground station serial rate |
| `--host 127.0.0.1` | 127.0.0.1 | bind address |
| `--http-port 8000` | 8000 | HTTP + WebSocket port |
| `--log-dir` | `logs/raw` | where the raw log and its `.meta.json` sidecar are written |
| `--list-ports` | — | list ports and exit |
| `-v` / `--verbose` | off | debug logging |

- **The port is exclusive.** Close the Arduino IDE Serial Monitor first, or the dashboard
  cannot open it.
- **A USB knock ends the process.** A serial error is fatal by design. Re-run the same
  command; it resumes with a new baseline and a new raw log, and nothing already written
  is lost.
- **`[GCS] ready 919.0 MHz SF7 team MRC` in the raw feed is the proof** that the baud and
  radio settings are right. No banner, no link.
- **No purple SIMULATED banner should be visible.** If it is, you are on the mock.
- Changing `--http-port` breaks the Vite dev proxy, which targets `127.0.0.1:8000`
  literally (`frontend/vite.config.ts`). Fine at launch, not during development.

---

## 2 · Mock — a simulated flight

Run from `backend/`. Nothing in `devtools/` is used at the launch site, and nothing in
`dashboard/` imports from it.

```bash
python -m devtools.run_mock
```

Then open <http://127.0.0.1:8000>, or run the Vite dev server against it (section 5).

| Flag | Effect |
|---|---|
| `--interval 0.2` | speed the flight up for faster UI iteration |
| `--once` | stop after one 78 s flight, as the real firmware does |
| `--clean` | no malformed lines, no `[GCS]` status lines |
| `--seed 42` | reproducible flight, for tests and comparisons |
| `--host` `--http-port` `--log-dir` `-v` | as section 1 |

The profile is a port of `MRC_FlightUnit_V7.ino`: eight phases, 78 seconds, apogee 150 m.

- **The feed is imperfect on purpose.** ~2% of lines are malformed and ~4% of intervals
  carry a `[GCS] Timeout - no packet` line instead of a packet. A UI built against a
  clean feed has never been tested against the feed it will get. Use `--clean` only when
  isolating something else.
- **The mock still emits GEN2** — no `$MRC`, no CRC, `CHUTE:n`. It cannot exercise
  auto-eject or any GEN4 command. (`status.md`, Next 4.)
- Every simulated run is labelled: the raw log ends `-mock.log`, the session and every
  frame envelope carry `"simulated": true`, and the UI shows a banner.

---

## 3 · Replay — a captured flight

Run from `backend/`. Feeds a real capture through the real pipeline (raw log → parser →
WebSocket) at the rate the vehicle produced it.

```bash
python -m devtools.run_replay 20260820-015822-serial.log        # a session log
python -m devtools.run_replay FLIGHT21.CSV --speed 8 --hold     # a vehicle SD card
python -m devtools.run_replay FLIGHT21.CSV --loop
```

| Flag | Effect |
|---|---|
| `--speed 8` | playback multiplier. Above ~x60 Windows' ~15 ms timer granularity dominates |
| `--hold` | keep the dashboard up after the capture ends, instead of exiting with it |
| `--loop` | repeat. Each pass restarts the vehicle clock and `seq`, as a reboot would |
| `--interval 0.2` | fixed gap, ignoring the vehicle clock entirely |
| `--host` `--http-port` `--log-dir` `-v` | as section 1 |

**A bare filename is searched for** in `logs/raw/`, then `backend/tests/fixtures/` — so a
session log or a committed capture can be replayed by name from anywhere. A path that
exists as given is used directly.

- **Any line-oriented capture works.** There is no extension filter in this path: a `.log`
  written by a live session replays exactly like a `.CSV` off the SD card, `[GCS]` lines
  and mixed packet generations included. The examples led with `.CSV` long enough to give
  the opposite impression.
- **Pacing comes from the capture's own `ms` field**, so the replay reproduces the cadence
  the vehicle actually ran at, irregularities included.
- **RSSI and SNR stay `null` and render as `—`.** They are measured by the ground
  station's radio as a packet arrives; a packet read from a file crossed no radio, so
  there is no measurement. Inventing a plausible dBm figure would be a fabricated
  measurement on the operator's screen.
- **The SIMULATED banner appears even though the data is real.** Only the liveness is
  false — and that is the sharper version of the risk, which is why `FileSource` sets
  `simulated = True` regardless.
- **`EJECT` reports failure during a replay** rather than appearing to work.
- The fixtures in `tests/fixtures/` are **bench runs, not flights**: altitude within
  ±1.2 m, no GPS fix (`ISS-14`), chute never commanded. They test parsing and transport.

---

## 4 · Uplink commands (PING, EJECT, GEN4 `SET`)

Run from `backend/`, **in a second terminal, while the dashboard is already running.**

```bash
python -m devtools.send_command PING
python -m devtools.send_command EJECT
python -m devtools.send_command SET:DROP:15.0
python -m devtools.send_command SET:ARM:50.0
python -m devtools.send_command SET:CYCLES:2
python -m devtools.send_command SET:AUTO:0
python -m devtools.send_command RESET
python -m devtools.send_command RESET:CHUTE
```

| Command | Effect | Bounds |
|---|---|---|
| `PING` | uplink proof. Increments `ul` on the vehicle | — |
| `EJECT` | command a chute release | — |
| `RESET` | re-base the auto-eject trigger. **Not a cancel** — `SET:AUTO:0` is | — |
| `RESET:CHUTE` | the above, **plus clear the fire latch** — makes a fired chute fireable again | — |
| `SET:DROP:<m>` | metres below peak before firing | 2.0 – 100.0 |
| `SET:ARM:<m>` | altitude above boot before the trigger arms | 5.0 – 200.0 |
| `SET:CYCLES:<n>` | consecutive confirming cycles | 1 – 10 |
| `SET:AUTO:<0\|1>` | enable/disable auto-eject | 0 or 1 |

| Flag | Effect |
|---|---|
| `--url ws://127.0.0.1:8000/ws` | dashboard WebSocket, if not the default |
| `--timeout 5.0` | seconds to wait for the `command_ack` |

- **This does not open the serial port.** It connects to the running dashboard's
  WebSocket and sends exactly the message the Eject button sends. The port is held
  exclusively by `python -m dashboard`, so a tool wanting the port for itself could only
  work while the dashboard was closed — which is when you least want to send commands.
- **Needs `pip install websockets`**, which is in neither requirements file. The script
  says so and exits 3 if it is missing.
- **`sent=true` means the bytes left the PC.** It does not mean the ground station
  transmitted them, that the vehicle received them, or that a value was applied. **The
  uplink carries no acknowledgement.** Watch `ul` on the dashboard and the `[GCS]` lines
  in the raw feed for what actually happened.
- **`ul` rising proves the vehicle received *a* command, never which one or what value it
  applied.** A sealed, flying vehicle cannot be asked what it is configured to do — bench
  USB and the SD card `#` lines answer that afterwards. (Declined GEN3.2 bump, twice.)
- **Never test the uplink with EJECT.** Use PING.
- **After an automatic release, the ground station's EJECT button transmits nothing.**
  `fireEjectBurst()` checks `lastChute >= 1` first and prints `EJECT confirmed after 0
  attempt(s)` without sending. True about the chute; not evidence the uplink works.
- **`SET` and `RESET` need GEN4 on both units.** A GEN3 vehicle ignores them and never
  moves `ul`, so a GEN4 ground station reports failure loudly rather than pretending.
- **Neither RESET clears the `chute` counter.** That counter is the ground station's
  confirmation signal for the eject burst, and zeroing it would make an already-fired
  chute look armed to the operator. `RESET` clears trigger state; `RESET:CHUTE` clears
  trigger state and the fire latch. Both leave `chute` where it is.
- **`RESET:CHUTE` also re-arms the GROUND station, and only if the vehicle confirmed.**
  The ground unit holds two latches of its own — `ejectConfirmed`, and the burst's test
  against `chuteBaseline` — and until 2026-08-29 neither was cleared by a reset, so
  `EJECT` afterwards printed `EJECT already confirmed, ignoring` and transmitted nothing.
  Expect `[GCS] EJECT re-armed at ground, chute baseline N` on success. On failure it
  says `EJECT still latched here - the vehicle did not confirm` and stays latched, which
  is the safe direction: the vehicle's own fire latch is still set, so a transmitted
  EJECT would raise `chute` and drive nothing.
- **A re-armed release takes `chute` to 2.** That is correct — the counter means
  "releases commanded", and two were. Note the dashboard currently renders only
  `chute === 1` as deployed.
- **`RESET` re-bases the trigger, it does not cancel it.** Arming tests altitude above
  BOOT, not a climb, so a vehicle still high when `RESET` arrives re-arms on the next
  cycle against a fresh apogee and fires again once it has dropped `DROP` from there.
  Traced: `RESET` at 150 m re-armed at 140 m and fired at 120 m. `SET:AUTO:0` is the
  cancel.
- Validation lives in `dashboard.api.translate_command`, not in this script. The bounds
  above are duplicated in three places — `api.py`, the GEN4 ground station, the GEN4
  vehicle — and must be changed together.
- **There is no dashboard UI for the GEN4 commands.** This script is the only path.

---

## 5 · Frontend

Run from `frontend/`.

```bash
npm run dev          # Vite dev server, hot reload, http://localhost:5173
npm run build        # tsc -b && vite build  ->  frontend/dist/
npm run preview      # serve the built bundle
npm run typecheck    # tsc -b --noEmit
npm run test         # vitest run
```

- **`npm run dev` needs a backend on `127.0.0.1:8000`.** Vite proxies `/ws` and `/api`
  there so the app always connects to a same-origin `/ws` — one code path in development
  and at launch, with no environment branching. The target is hardcoded.
- **The build inlines everything** (`assetsInlineLimit: 0`, no CDN, no remote fonts).
  There is no internet at the launch site. See `ISS-12`.
- At launch FastAPI serves `dist/` itself; the Vite server is not involved.

---

## 6 · Tests

```bash
cd backend  && python -m pytest tests -q          # 145
cd frontend && npm run test                       # 94
cd frontend && npm run typecheck
python firmware/tests/verify_gen3.py              # from the repo root — 14 checks
```

**239 tests: 145 backend, 94 frontend**, plus the firmware verifier.

Narrowing the run, while working on one thing:

```bash
python -m pytest tests/test_parser_gen3.py -q     # one file
python -m pytest tests -q -k contract             # one subject, by name
python -m pytest tests -q -x                      # stop at the first failure
npm run test -- packet                            # frontend, one file by name
```

`verify_gen3.py` is the reference implementation of the GEN3 packet checksum, and a
transliteration of the C in `firmware/MRC_GroundStation_GEN3/Radio.ino`. Three separate
implementations must agree byte for byte — the flight unit computes it, the ground station
recomputes it to decide whether `chute` can be trusted, the dashboard verifies it — and a
divergence shows up here first.

- **Nothing pins the apogee state machine.** `verify_gen3.py` pins the packet; the
  auto-eject trigger was checked by a throwaway trace that was never committed.
  (`status.md`, Next 2.)

---

## 7 · Firmware

**There is no CLI build here.** No Arduino toolchain is installed on this machine —
building and flashing is done from the Arduino IDE, by hand. These are the sketch folders,
not commands.

| Folder | Flashed to |
|---|---|
| `firmware/MRC_FlightUnit_GEN3/` | the CanSat — proven on hardware, still flashable |
| `firmware/MRC_GroundStation_GEN3/` | the ground unit — the matching half |
| `firmware/MRC_FlightUnit_GEN4/` | the CanSat — adds the configurable trigger |
| `firmware/MRC_GroundStation_GEN4/` | the ground unit — sends `SET` / `RESET` |

- **Flash GEN3 and GEN4 as a pair, both units.** GEN3 is the only pair proven on hardware.
- A mismatched GEN4 pair degrades safely in both directions: a GEN3 vehicle ignores `SET`
  and never moves `ul`, so a GEN4 ground station reports failure rather than pretending.
- **The packet is GEN3.1 byte for byte in both generations.** The parser, contract,
  dashboard and every test needed nothing when GEN4 landed.
- The flight unit prints `[FLT] PING received, count N` over USB at **115200**
  unconditionally — independent of `ENABLE_SERIAL_ECHO` and of the display. That is the
  uplink witness that still works with a dead OLED.
- **Power the vehicle on flat, still, and at launch height.** The MPU6050 averages 500
  samples as zero offsets at boot, and the first barometer reading becomes `baseAltitude`.
  Both are silent if wrong.

---

## 8 · Diagnostics

```bash
cd backend && python -m dashboard --list-ports
```

Lists serial ports and exits. If it finds nothing, the ground unit is unplugged or the
CP210x / CH34x USB driver is missing (`ISS-12`). The port may differ between machines
(`ISS-05`).

**`firmware/tools/`** — sketches that answer one question faster than reflashing the real
firmware would. Nothing here flies. Radio settings match GEN3 exactly, so the relay pair
works with no reconfiguration. Full detail in `firmware/tools/README.md`.

| Sketch | Use |
|---|---|
| `ServoEjectTest` | **bench the release servo**, thrown by a button instead of a radio. Run before trusting a deployment. Its pin and angles must agree with `CHUTE_*` in both `Config.h` files |
| `GPS_Relay_Flight` + `GPS_Relay_Ground` | **the GPS test you actually want.** One per unit; CanSat outside under sky, ground unit on USB at 115200 |
| `UART_PinTest` | run when the relay reports `chars=0`. Jumper pin 19 to pin 20 with the GPS disconnected |
| `GPS_Passthrough` | desk test only — is the module alive at all |
| `GPS_Minimal` | minimal read |

The `inview` vs `used` split in the relay output is the useful part: satellites in view
prove the antenna and sky are fine even with no fix yet, which is exactly the distinction
the flight firmware's `sat` field cannot make.

**Reading the dashboard without the dashboard.** Two HTTP endpoints, useful when the UI
is not the thing you are testing:

```bash
curl http://127.0.0.1:8000/api/session      # source, simulated flag, packet contract
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8000/    # 200, or 503 = no dist/
```

`/api/session` returns the same message every WebSocket client gets first, including the
generated field table — 22 entries with wire index, unit, precision and sentinels. It is
the fastest way to confirm the backend is up, whether it is on real or simulated data, and
what contract version it is serving, without opening a browser.

---

## Log files, for reference

Not commands, but every command above writes or reads them.

| Path | Written by | Note |
|---|---|---|
| `logs/raw/<stamp>-serial.log` | `python -m dashboard` | every line `fsync`'d as it arrived |
| `logs/raw/<stamp>-mock.log` | `run_mock` | the suffix is the label — it can never be mistaken for a flight |
| `logs/raw/<stamp>-replay.log` | `run_replay` | likewise |
| `logs/raw/<stamp>-*.meta.json` | all three | the packet contract for that session |
| `/FLIGHTnn.CSV` on the vehicle SD | the flight unit | independent of the link and the laptop; complete even if the dashboard never ran |

`logs/` is gitignored, and the ground station stores nothing — it is a pure pass-through.
**There is no third copy.** Where flight logs are archived permanently is still undecided.
