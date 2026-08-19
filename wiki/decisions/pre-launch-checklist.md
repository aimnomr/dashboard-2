# Pre-Launch Checklist

Written 2026-08-19. Closes the documentation half of `ISS-12`; the other half is a dry
run on the actual field laptop, which this document exists to be run *from*.

**How to use it.** Work top to bottom. Do not skip a line because it is obviously fine —
the lines that look obviously fine are the ones that end launch days. Anything that fails
has a **STOP** response written next to it.

> Filed under `decisions/` because it carries its reasoning. If more operational
> documents appear, an `operations/` folder is the better home and this should move.

---

## A · Before leaving — on the field laptop, not a dev machine

There is no internet at the site. Everything here needs the network, so everything here
happens first. Any one of them missing means no dashboard, with no way to fix it.

- [ ] Python installed, virtual environment created
- [ ] `pip install -r backend/requirements.txt` completed
- [ ] `cd frontend && npm install && npm run build` completed — **`frontend/dist/` exists**
- [ ] USB serial driver installed for the Heltec board (CP210x or CH34x)
- [ ] Ground unit plugged in, `python -m dashboard --list-ports` shows it
- [ ] Full run against the mock: `python -m devtools.run_mock` → dashboard loads in browser
- [ ] Laptop charged; charger packed
- [ ] SD card in the flight unit, and it has free space

**Then turn WiFi off and leave it off.** Everything after this point must work without it.
If something needs the network now, you have found what would have stopped you at the
gate.

**STOP if `frontend/dist/` is missing.** The backend will serve a 503 with a build hint
and nothing else. It cannot be built at the site.

---

## B · On arrival — cold start

- [ ] Close the Arduino IDE Serial Monitor. The port is exclusive
- [ ] `cd backend && python -m dashboard --list-ports` — note the port; it may differ from home (`ISS-05`)
- [ ] `python -m dashboard --port COMxx`
- [ ] Raw feed shows `[GCS] ready 919.0 MHz SF7 team MRC` — proof of correct baud and radio settings
- [ ] Browser at `http://127.0.0.1:8000`
- [ ] **No purple SIMULATED banner.** If it is there, you are on the mock, not the radio
- [ ] Note the raw log path printed at startup

**STOP if the ready banner never appears.** Wrong port, wrong baud, or the Serial Monitor
still holds the port. Do not proceed on the assumption it will sort itself out.

---

## C · Vehicle power-on

- [ ] **Flat and still.** The MPU6050 averages 500 samples as zero offsets at boot. Get
      this wrong and every attitude reading is biased for the entire flight, with nothing
      on screen to show it
- [ ] Power on at, or as near as possible to, the height you will launch from — the first
      barometer reading becomes `baseAltitude`, and all altitude is relative to it
- [ ] Frames appear: `$MRC,…` in the raw feed
- [ ] **Malformed** counter at or near 0
- [ ] **Loss** shows a real percentage, not `n/a` — `n/a` means the packets are not GEN3
- [ ] RSSI and SNR show numbers, not `—`
- [ ] Note `[GCS] foreign packet, N so far` — this is the band-congestion instrument (`ISS-13`)

---

## D · Uplink check

**On GEN3.1 firmware this can be done at any time, sealed or not** — which is new as of
2026-08-20 and is the point of the `ul` field. On GEN3.0 it must happen before the unit
is closed.

There is still no acknowledgement on the uplink itself, and there never has been: the
ground station prints *"no acknowledgement exists"* because none does. Silence after a
Ping is **not** evidence of failure. What changed is that the vehicle now reports how many
commands it has received, in every telemetry packet.

**Preferred — the dashboard** (GEN3.1 firmware; works sealed, works at altitude):

- [ ] Note the **Uplink** figure on the Eject panel
- [ ] Press **Ping**
- [ ] Confirm the count **increments within a second or two**

A reading of *"Uplink unproven — the vehicle has received nothing"* after a Ping is the
only unambiguous failure signal this system has ever had. Treat it as a STOP.

**Or — the vehicle's OLED** (GEN3.0, and only while the screen is visible and working):

- [ ] Press **Ping**
- [ ] Confirm the **UL counter resets on the vehicle's OLED**

**Or — the vehicle's USB serial** (works with a dead screen; the OLED on the current set
is not working):

- [ ] Tether the flight unit to a USB port — separate from the ground station's
- [ ] Open a serial monitor on it at **115200**
- [ ] Press **Ping**
- [ ] Confirm `[FLT] PING received, count N`

That line is printed unconditionally by `Radio.ino:72`, independent of
`ENABLE_SERIAL_ECHO` and of the display.

**STOP if neither witness responds.** EJECT uses the same path. If Ping does not arrive,
the chute command will not either, and you will not find that out until you need it.

**Never test the uplink with EJECT.**

> **Known gap.** `pingCount`, `uplinkHeard` and `lastUplinkMs` exist only in
> `Display.ino` — the uplink count never reaches telemetry. So uplink health cannot be
> checked once the unit is sealed, or at any point during flight, by any route. Adding a
> `ul` field to the packet would fix it. See devlog 037.

---

## E · Final checks before flight

- [ ] Chute shows **ARMED**
- [ ] GPS: satellite count and a plausible fix (`ISS-14` — if the GPS is still unpowered,
      expect NO FIX and decide *in advance* whether that is acceptable)
- [ ] Altitude reads ~0 m
- [ ] Attitude horizon responds when you tilt the vehicle
- [ ] Agree who watches the screen and who handles the vehicle
- [ ] Agree who calls EJECT, and on what
- [ ] Leave the dashboard running. Do not restart it to "start clean" — see F

---

## F · During the flight

Watch, in priority order: **Loss (rolling)** · **time since last packet** · altitude ·
chute state.

| If this happens | Do this |
|---|---|
| Link goes **Stale** then **Signal lost** | Nothing on the PC helps. The vehicle keeps logging to its own SD card |
| Rolling loss climbs | Expected at range and with the band shared. The session figure is the record |
| Malformed count rises | RF corruption. Those frames are rejected, never charted |
| `vehicle restart` appears | The vehicle rebooted. Loss counters reset from the new baseline; the chart breaks. Not a dashboard fault |
| **Dashboard exits after a USB knock** | Known: a serial error ends the process. Re-run the same command. It resumes with a new baseline and a new raw log; nothing already written is lost |
| Browser shows Disconnected | It retries every second on its own. If the backend also died, restart it first |

**Do not close the terminal to fix a browser problem.** Reload the browser instead — the
backend holds the session, and the browser is disposable.

---

## G · After the flight — recovery

Three records exist. Collect them in this order, most fragile first.

- [ ] **Vehicle SD card** → `/FLIGHTnn.CSV`. Independent of the link and of the laptop;
      complete even if the dashboard never ran. Copy it off before anything else
- [ ] **Laptop raw log** → `logs/raw/<stamp>-serial.log`, plus its `.meta.json` sidecar.
      Every line `fsync`'d as it arrived, so a hard power loss costs at most one line
- [ ] Back both up to a second device **before leaving the site**
- [ ] Replay to confirm recovery worked:
      `python -m devtools.run_replay FLIGHTnn.CSV --hold`

**There is no third copy.** `logs/` is gitignored and the ground station stores nothing —
where flight logs are archived permanently is still undecided.

---

## What this checklist assumes, and does not cover

- **`ISS-13` frequency coordination is unresolved.** Three other teams on 919.0 MHz is
  the largest risk on the day and no software change affects it. The foreign-packet
  counter distinguishes "band is busy" from "my vehicle is silent" — that is all the
  dashboard can offer.
- **`ISS-06` competition requirements are unknown.** Mandatory displays, telemetry rate
  floors and reporting duties may add steps here.
- **`ISS-14` GPS.** Written assuming it may still be dead. If it is fixed, add a
  fix-acquisition wait to section C.
- Nothing here covers rocket, recovery-team or range-safety procedure. This is the
  telemetry chain only.
