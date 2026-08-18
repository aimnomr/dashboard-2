# Previous Dashboard — Node-RED

Source: `CANSAT.json`, `MRC_DualDebug_Flow.json`, `MRC_TwoWay_Flowchart.html`

The v1 dashboard was built in **Node-RED** with the Node-RED Dashboard UI nodes.

## ⚠️ `CANSAT.json` is a full workspace export, not a CanSat flow

234 nodes across 4 tabs, and it contains **unrelated university coursework** mixed in with the
CanSat work — leftover SQLite paths and MQTT brokers from IoT lab exercises:

```
C:\Users\LOQ\Downloads\IoT\PRACTICAL\LAB7 - Node-RED\Env_System_Database.SQlite
C:\Users\LOQ\Downloads\IoT\PRACTICAL\LAB FULL PROJECT\Env_DB_Full_Project.SQlite
C:/Users/USER/Documents/UniKL/SEM 5/IoT/SQLite/smart_room_database.sqlite
```

It also carries **9 MQTT broker configs**, most irrelevant to the CanSat: `broker.hivemq.com`,
`broker.emqx.io`, and several LAN addresses (`192.168.100.160`, `192.168.100.22`) labelled
`Laptop_Arep`, `Laptop_Ipin`, `SelfBroker`. Plus a stray `serial-port` node on **COM8 @ 9600,
7 data bits, even parity, 2 stop bits** — lab equipment settings, nothing to do with the
115200-baud ground unit.

Only the nodes bound to `Local Mosquitto` (`localhost:1883`) and the `CANSAT_DATA` database are
part of this project. Treat the rest as noise.

## Dashboard tabs

| Tab | Contents |
|---|---|
| Real-Time Indicator | live gauges/readouts |
| Data Analytics | min/max/avg aggregates over the whole database |
| Table Listing | paged reading table |
| Charts | time-series plots |

## Charts present

Altitude, Pressure, Speed, Temperature, Humidity, Accelerometer, Gyroscope, RSSI, SNR.

The primary altitude chart is fixed to **y-axis 0–600 m**; the rest are auto-scaled.

## Data path

```
MQTT in (topic: cansat/telemetry, localhost:1883)
  → function nodes (22 total)
  → sqlite insert into readings
  → SELECT last_insert_rowid()  → fan out to imu_data / gps_data / signal_data
  → ui_chart / ui_table / ui_template
```

The `last_insert_rowid()` round-trip is how the child tables get their foreign key — one insert
plus a query plus three more inserts, per packet.

## Analytics queries

The Data Analytics tab runs whole-table aggregates, e.g.:

- max altitude + the timestamp at which it occurred, and max speed
- min/max/avg for temperature, humidity, pressure
- min/max/avg for RSSI and SNR
- a full join across `readings` + `imu_data` + `gps_data` + `signal_data` for export

These scan the entire database, not a single flight — see `telemetry-database.md` for why
that matters.

## `MRC_DualDebug_Flow.json` — separate bench-test flow

15 nodes, two tabs: **🛰 Two-Way Comms** and **🔧 Dual Debug**.

Attaches **both** units to the PC at once:

```
serial in  "Ground Unit (COM5)"   115200  → Ground Handler
serial in  "Flight Unit (COM12)"  115200  → Flight Handler
                                    ↓
                      "Filter: EJECT only → serial"
                                    ↓
                    serial out "Serial OUT → Ground (COM5)"
```

This is the flow that implements the EJECT button. It bypasses MQTT and SQLite entirely — raw
serial in, raw serial out, straight to `ui_template` displays.

## Why this matters for v2

The v1 architecture has the shape: **serial → MQTT → SQLite → UI**, with MQTT and a broker
process as intermediate hops, and business logic spread across 22 Node-RED function nodes plus
inline SQL in the flow export. Understanding what it did is useful; the coupling and the
lab-flow pollution are the parts worth leaving behind.
