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
python -m devtools.send_command SET:REPEAT:1
python -m devtools.send_command RESET
python -m devtools.send_command RESET:CHUTE
```

| Command | Effect | Bounds |
|---|---|---|
| `PING` | uplink proof. Increments `ul` on the vehicle | — |
| `EJECT` | command a chute release | — |
| `RESET` | re-base the auto-eject trigger. **Not a cancel** — `SET:AUTO:0` is | — |
| `RESET:CHUTE` | the above, **plus clear the fire latch** — makes a fired chute fireable again, *immediately* | — |
| `SET:DROP:<m>` | metres below peak before firing. Default **2.0**, the flight value | 2.0 – 100.0 |
| `SET:ARM:<m>` | altitude above boot before the trigger arms | 5.0 – 200.0 |
| `SET:CYCLES:<n>` | consecutive confirming **samples** — 125 ms each since 2026-09-10, not 1 s | 1 – 10 |
| `SET:AUTO:<0\|1>` | enable/disable auto-eject | 0 or 1 |
| `SET:REPEAT:<0\|1>` | release mode: **1** MULTI, the drive latch expires; **0** SINGLE, only `RESET:CHUTE` re-arms | 0 or 1 |

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
- **After a CONFIRMED release, `EJECT` is refused by name until the cooldown passes** —
  `EJECT already confirmed, ignoring`, with the reason given. It no longer *silently*
  transmits nothing.

  Until 2026-09-10 a burst whose confirmation arrived late left `ejectConfirmed` false
  with `chuteBaseline` already passed, and the next `EJECT` printed `EJECT confirmed after
  0 attempt(s)` and sent nothing at all — a release the operator was shown that never
  happened. That was devlog 065 fault 1, fixed in 070 by moving the confirmation decision
  into `radioPoll()`. **A rise can no longer confirm anything unless a burst is actually
  outstanding.** If you are reading a log captured before 2026-09-10, that message may be
  reporting a command that was never transmitted.

  An automatic release is still not a link test — use `PING`.
- **`SET:REPEAT` chooses the release mode, and it governs the COMMANDED path only.**
  `1` (MULTI) lets the vehicle's drive latch expire after `CHUTE_REARM_MS`, so repeat
  releases can be commanded. `0` (SINGLE) restores one drive per boot, with `RESET:CHUTE`
  as the only re-arm. **Auto-eject is one-shot per boot in both modes** — the descent
  condition stays true for the whole descent, so an expiring latch there would drive the
  mechanism every cycle of the fall. `RESET`/`RESET:CHUTE` remain its only re-arm.
- **A reboot forgets `SET:REPEAT`.** The vehicle returns to `CHUTE_AUTO_REARM` from its
  `Config.h`, the same way the apogee config returns to its compile-time defaults. The
  ground station detects the restart (`ul` falling) and resets its own assumption to
  `VEHICLE_DEFAULT_REPEAT` — but **that is an assumption, never a readback**: GEN3.1
  carries no config fields, so neither the ground nor the dashboard can see the vehicle's
  actual mode. The `#` config lines on the SD card are the only true record, after
  recovery, and they now carry `repeat=`.
- **What the console says during and after a burst.** Changed 2026-09-10 (devlog 070) —
  confirmation is now decided when the packet arrives, not inside the burst, so the
  messages come from two different places and mean different things:

  ```
  [GCS] EJECT armed
  [GCS] EJECT attempt 1/5                     ... up to 5, ~351 ms apart
  [GCS] EJECT confirmed, chute 4 -> 5         from radioPoll(), the moment it lands
  [GCS] EJECT burst stopped after 2 attempt(s) - already confirmed
  ```

  `EJECT confirmed, chute X -> Y` is the only line that means a release happened. It can
  appear **after** the burst has ended — that is the point of the change — in which case
  the burst prints:

  ```
  [GCS] EJECT burst complete, 5 sent - awaiting confirmation, watch chute in telemetry
  ```

  and the confirmation follows a second or two later. If it never comes:

  ```
  [GCS] EJECT confirmation timed out - no chute rise seen, assume it was NOT received
  ```

  after `EJECT_CONFIRM_TIMEOUT_MS` (5 s). **Timing out blocks nothing** — the next
  `EJECT` is treated as an ordinary first attempt and transmits immediately.

  The old `EJECT confirmed after N attempt(s)` is gone: N is not knowable once the packet
  may arrive cycles after the burst that caused it. `chute X -> Y` says only what is
  actually known.

- **A second `EJECT` is allowed once the cooldown has passed (061).** The vehicle returns
  its mechanism to ARMED `CHUTE_HOLD_MS` after driving it and clears its own fire latch at
  `CHUTE_REARM_MS` (3 s), so the console mirrors that with `EJECT_REARM_MS` and stops
  refusing. Inside the cooldown you get:

  ```
  [GCS] EJECT already confirmed, ignoring - vehicle still re-arming
  [GCS] wait for the cooldown, or send RESET:CHUTE to re-arm now
  ```

  and past it, `[GCS] EJECT re-armed after cooldown, chute baseline N` followed by an
  ordinary burst. The refusal inside the window is deliberate: a burst sent then would be
  received, counted into `chute`, and drive nothing — a release the operator is shown and
  that never happened.
- **`EJECT_REARM_MS` must never be shorter than the vehicle's `CHUTE_REARM_MS`.** The
  vehicle's value decides when the mechanism can actually move; the ground's only decides
  when the console stops saying no. Ground shorter than vehicle recreates exactly the
  phantom release described above. Equal, or longer.
- **`SET` and `RESET` need GEN4 on both units.** A GEN3 vehicle ignores them and never
  moves `ul`, so a GEN4 ground station reports failure loudly rather than pretending.
- **Neither RESET clears the `chute` counter.** That counter is the ground station's
  confirmation signal for the eject burst, and zeroing it would make an already-fired
  chute look armed to the operator. `RESET` clears trigger state; `RESET:CHUTE` clears
  trigger state and the fire latch. Both leave `chute` where it is.
- **`RESET:CHUTE` is no longer the only way to re-arm, but it is the only immediate one.**
  Since 061 the vehicle re-arms itself after `CHUTE_REARM_MS`; `RESET:CHUTE` does it at
  once, works on a vehicle built with `CHUTE_AUTO_REARM 0`, and is still the only thing
  that clears `chuteEverFired` — which is what stops auto-eject firing after a commanded
  release. Use it when you do not want to wait, or when the vehicle may not have re-armed.
- **`RESET:CHUTE` also re-arms the GROUND station, and only if the vehicle confirmed.**
  The ground unit holds two latches of its own — `ejectConfirmed`, and the burst's test
  against `chuteBaseline` — and until 2026-08-29 neither was cleared by a reset, so
  `EJECT` afterwards printed `EJECT already confirmed, ignoring` and transmitted nothing.
  Expect `[GCS] EJECT re-armed at ground, chute baseline N` on success. On failure it
  says `EJECT still latched here - the vehicle did not confirm` and stays latched, which
  is the safe direction: the vehicle's own fire latch is still set, so a transmitted
  EJECT would raise `chute` and drive nothing.
- **A re-armed release takes `chute` to 2.** That is correct — the counter means
  "releases commanded", and two were. The dashboard renders it as `Commanded ×N` for
  every N (rule S8, devlog 059); it does not use the word "deployed" anywhere, because no
  canopy sensor exists.
- **`chute` counts RELEASES PERFORMED — one per operator `EJECT`.** Changed 2026-09-09.
  `chuteCommands++` is now gated on `chuteFire()`'s return, which is true only on the call
  that actually drives the mechanism, so the four remaining attempts of a 5-shot burst
  move nothing. `CHUTE_REARM_MS` (3000 ms) is still longer than the burst span (~1404 ms),
  so a single command still cannot drive the mechanism twice.

  Until this change the counter sat outside the fire latch and rose per packet received,
  so one `EJECT` typically moved it by 3. If you are reading a log captured before
  2026-09-09, that is the behaviour you are looking at.

  **Receipt is `ul`'s job and is unchanged** — it still rises on every uplink packet the
  vehicle hears, including the burst attempts this counter now ignores. `chute` rising
  with `ul` UNCHANGED remains the only ground-side proof a release was automatic.
- **`SET:CYCLES` counts SAMPLES, and a sample is 125 ms — not 1 s.** Changed 2026-09-10
  (devlog 071). The trigger no longer runs once per telemetry cycle; it samples altitude
  on its own clock at `AUTO_EJECT_SAMPLE_MS` while telemetry stays at 1 Hz. The command,
  its wire format and its bounds (1–10) are all unchanged — only what a unit *means* is.

  ```
  SET:CYCLES:3   ->  ~250 ms of confirmation   (was 2000 ms)
  SET:CYCLES:10  ->  ~1.25 s                   (still faster than the old 3)
  ```

  Total detection from apogee falls from ~3.4–4.4 s to **~1.7–1.8 s**, most of which is
  now the physical fall to `DROP` rather than the trigger. **If you are reading a log
  from before 2026-09-10, `cycles=3` in an SD `#` line meant two seconds.**

  ⚠ The window three confirmations span is now ~250 ms, so they filter a ~250 ms
  excursion rather than a ~2 s one. If the barometer proves noisy in flight, **raise
  `SET:CYCLES` rather than expecting the old behaviour** — 10 samples is 1.25 s and still
  beats the old 3.
- **`SET:CYCLES` above ~8 Hz is not available, and the barometer is why.** The BME280 at
  the firmware's sampling settings takes ~113 ms worst case per conversion, so sampling
  faster would count one physical measurement as two confirmations. The rate is fixed at
  compile time and is deliberately not a `SET`.
- **`DROP` now defaults to 2.0 m — its own floor** (devlog 072, was 10.0). It could not
  usefully be lowered before 071: at one sample per second and a 20 m/s descent the first
  post-apogee sample was already 20 m down, so every threshold under ~20 m fired on the
  same sample and `DROP:10` and `DROP:2` were indistinguishable. At 125 ms a sample is
  well under a metre of travel early in the fall, so the number finally means what it says.

  Free fall from apogee, drag-free, `CYCLES:3`:

  | `DROP` | detection | altitude lost | confirmation window |
  |:--|:--|:--|:--|
  | 10 m *(the old default)* | 1.7 – 1.8 s | 14 – 16 m | 250 ms |
  | 5 m | 1.26 – 1.39 s | 8 – 9 m | 250 ms |
  | **2 m** *(current)* | **0.89 – 1.01 s** | **4 – 5 m** | **250 ms** |

  ⚠ **The 071 and 072 changes compound, and not in the safe direction.** The rule now
  wants a **5× smaller drop held for an 8× shorter window** than the vehicle that flew
  before them — 2 m over 250 ms, where it was 10 m over 2 s. Sensor noise alone cannot
  span that (~0.11 m RMS at 16× pressure oversampling puts 2 m at ~18σ), but a real
  pressure transient lasting 375 ms can: a gust, slipstream, a venting bay. The arming
  floor still means it cannot fire below 30 m, so **the pad is safe** — the exposure is a
  transient during ascent faking a 2 m dip below the highest altitude seen.

  **If that shows up, raise `SET:CYCLES`, not `SET:DROP`.** `DROP` has no room left below
  it, and `CYCLES` is settable at the pad with no reflash:

  | `CYCLES` at `DROP:2` | window | detection | altitude lost |
  |:--|:--|:--|:--|
  | 3 *(current)* | 250 ms | 0.89 – 1.01 s | 4 – 5 m |
  | 5 | 500 ms | 1.14 – 1.26 s | 6 – 8 m |
  | 10 *(the ceiling)* | 1.125 s | 1.76 – 1.89 s | 15 – 17 m |

  Even at the ceiling it is no worse than the old `DROP:10 / CYCLES:3`, and it carries
  three times the evidence.
- **GEN3 keeps `DROP` at 10.0 m, deliberately.** That vehicle samples once per second and
  has no `SET`, so 2 m there would be the no-op described above with none of the ways to
  correct it in flight. 10 m is the right default for a 1 Hz trigger.
- **`RESET` re-bases the trigger, it does not cancel it.** Arming tests altitude above
  BOOT, not a climb, so a vehicle still high when `RESET` arrives re-arms on the next
  cycle against a fresh apogee and fires again once it has dropped `DROP` from there.
  Traced: `RESET` at 150 m re-armed at 140 m and fired at 120 m. `SET:AUTO:0` is the
  cancel.
- **A non-finite altitude is rejected and the trigger holds its state** (071). It cannot
  fire on NaN, and it does not reset a confirmation already in progress either. A
  barometer that fails permanently leaves auto-eject inert for the rest of the flight —
  it will not fire wrongly and it will not fire at all, so **the uplink is the backup for
  that case.** Watch for `[FLT] auto-eject: non-finite altitude REJECTED` on the raw feed.
- **A vehicle that could not zero its altitude at boot cannot auto-eject at all** (071).
  If the baseline fails the plausibility band, the unit says so on serial and on the OLED,
  reports `alt` as 0.0, and flies with the trigger inert. Everything else — telemetry, the
  uplink, `EJECT` from the ground — still works.
- Validation lives in `dashboard.api.translate_command`, not in this script. The bounds
  above are duplicated in three places — `api.py`, the GEN4 ground station, the GEN4
  vehicle — and must be changed together.
- **The re-arm delay is a fourth duplicated pair**: `CHUTE_REARM_MS` in the vehicle and
  `EJECT_REARM_MS` in the ground station. Nothing enforces the relationship between them,
  and the failure is a confirmed release that never happened.
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
| `GPS_PacketTest` | **the GPS test to start with.** Flight unit only — emits real GEN3.1 `$MRC` packets on `SYNC_WORD 0xAA`, so the **existing** GEN4 ground station and dashboard show the fix with no reflash. Everything but the GPS fields is `-999` and arrives flagged; see the note below |
| `GPS_Relay_Flight` + `GPS_Relay_Ground` | raw NMEA **sentences**, one per unit; CanSat outside under sky, ground unit on USB at 115200. ⚠ both halves are still on `SYNC_WORD 0xAB` (stale since devlog 060) so neither reaches the GEN4 ground station — flash both or neither, because a mismatch is silent |
| `UART_PinTest` | run when a GPS tool reports `chars=0`. Jumper pin 19 to pin 20 with the GPS disconnected |
| `GPS_Passthrough` | desk test only — is the module alive at all |
| `GPS_Minimal` | minimal read |

**`GPS_PacketTest` writes `-999` into `temp`, `hum`, `pres`, `alt` and all six IMU axes**,
because it measures none of them. The parser rejects any frame with a blank or non-finite
field, so a number has to go there; `-999` is outside every range in `parser.py::_PLAUSIBLE`,
so each one arrives as a warning instead of passing as a reading. Environment, Altitude and
Attitude will show nonsense while it runs — that is the point. Its logs are otherwise
indistinguishable from a real flight, so label any you keep.

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
