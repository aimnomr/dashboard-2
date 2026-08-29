# Commands — quick

Copy-paste sheet. **No explanation here on purpose** — every command below has caveats,
and they live in [`COMMANDS.md`](COMMANDS.md). If you have not read that at least once,
read it before you use this.

Comments mark only what you cannot guess: value ranges, and the two commands that are
dangerous. Section numbers match `COMMANDS.md`.

---

## Setup · §0

```bash
cd backend && python -m venv .venv
.venv\Scripts\activate                          # every new terminal
pip install -r requirements.txt
pip install -r requirements-dev.txt             # tests
pip install websockets                          # uplink script only
cd frontend && npm install && npm run build
```

## Run · §1–3

```bash
# backend/ — activate the venv first
python -m dashboard --list-ports
python -m dashboard --port COM12                # → http://127.0.0.1:8000
python -m devtools.run_mock
python -m devtools.run_mock --interval 0.2 --clean --seed 42
python -m devtools.run_replay <file>
python -m devtools.run_replay <file> --speed 8 --hold
python -m devtools.run_replay <file> --loop
```

Common to all three: `--host` `--http-port` `--log-dir` `-v`
Replay looks in `logs/raw/`, then `backend/tests/fixtures/`.

## Uplink · §4

```bash
# backend/, SECOND terminal, dashboard already running
python -m devtools.send_command PING            # test the uplink with this, never EJECT
python -m devtools.send_command EJECT
python -m devtools.send_command SET:DROP:15.0   # 2.0–100.0
python -m devtools.send_command SET:ARM:50.0    # 5.0–200.0
python -m devtools.send_command SET:CYCLES:2    # 1–10
python -m devtools.send_command SET:AUTO:0      # 0|1 — the cancel
python -m devtools.send_command RESET           # re-bases the trigger, does NOT cancel
python -m devtools.send_command RESET:CHUTE     # ⚠ makes a FIRED chute fireable again
```

`--url ws://127.0.0.1:8000/ws` · `--timeout 5.0` · GEN4 on both units required.

## Frontend · §5

```bash
# frontend/
npm run dev                                     # needs a backend on :8000
npm run build
npm run preview
npm run typecheck
npm run test
```

## Tests · §6

```bash
cd backend  && python -m pytest tests -q        # 145
cd frontend && npm run test                     # 94
cd frontend && npm run typecheck
python firmware/tests/verify_gen3.py            # repo root — 14 checks

python -m pytest tests/test_parser_gen3.py -q   # one file
python -m pytest tests -q -k contract           # one subject
python -m pytest tests -q -x                    # stop at first failure
npm run test -- packet                          # frontend, one file
```

## Diagnostics · §8

```bash
cd backend && python -m dashboard --list-ports
curl http://127.0.0.1:8000/api/session          # source, sim flag, packet contract
python -m dashboard --help                      # every entry point takes --help
```

## Firmware · §7

No CLI build. Arduino IDE, by hand. Flash as a **pair**, both units.

```
firmware/MRC_FlightUnit_GEN3/      firmware/MRC_GroundStation_GEN3/
firmware/MRC_FlightUnit_GEN4/      firmware/MRC_GroundStation_GEN4/
firmware/tools/ServoEjectTest/     bench the servo before trusting it
firmware/tools/GPS_Relay_Flight/   + GPS_Relay_Ground — the GPS test that works
firmware/tools/UART_PinTest/       when the relay reports chars=0
```

Vehicle USB is **115200**. Power it flat, still, and at launch height.

---

## Launch day, in order

The procedure is `wiki/decisions/pre-launch-checklist.md` — this is only the typing.

```bash
cd backend && .venv\Scripts\activate
python -m dashboard --list-ports
python -m dashboard --port COM12
# open http://127.0.0.1:8000 — expect [GCS] ready … in the raw feed,
# and NO purple SIMULATED banner
python -m devtools.send_command PING            # second terminal; watch ul rise
```
