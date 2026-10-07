# Heterogeneous TSN Configurator

> **Centralized controller (CNC) for Heterogeneous Time-Sensitive Networking (H-TSN).**
> One control plane over a **mixed wired + wireless** network: ESP32 / Raspberry Pi /
> STM32 / NXP / Linux nodes are managed against **IEEE 802.1Qcc** (QoS, VLAN,
> gPTP/time-sync, TAS/GCL, stream reservation). **Two data planes**: wireless nodes
> (ESP32) talk **MQTT** over WiFi; the wired TSN endpoint (STM32) talks **OPC UA** over
> Ethernet with **PTP (802.1AS / gPTP)** time-synchronization. **Zero-touch onboarding**:
> flash an agent, power it on, and it provisions and connects itself. **Local AI on the
> edge**: the RPi runs a local LLM assistant, an autonomous policy engine and
> camera-based vision — all decisions are validated, executed with provenance and
> auditable.

A production-oriented configuration and control plane for H-TSN. The **control-plane
core is written in pure C (C11)** and runs as a headless service on the RPi. The
**front-end is a Node-RED dashboard** (`htsn-nodered/`) — 231 nodes across 11 tabs
covering devices, TSN config, monitoring, firmware OTA, camera proxy, AI assistant,
and backend listeners. **Everything runs on the RPi 5** — it is the CNC.

---

## Table of contents

1. [Quick start](#quick-start)
2. [Architecture at a glance](#architecture-at-a-glance)
3. [The two data planes](#the-two-data-planes)
4. [Components](#components)
    - [C core](#c-core)
    - [Node-RED dashboard](#node-red-dashboard)
    - [Wired TSN endpoint + RPi CNC](#wired-tsn-endpoint--rpi-cnc)
    - [Edge AI services](#edge-ai-services)
    - [Firmware agents](#firmware-agents)
5. [Raspberry Pi edge deployment](#raspberry-pi-edge-deployment)
6. [AI on the edge](#ai-on-the-edge)
7. [Firmware & OTA](#firmware--ota)
8. [Provisioning & onboarding](#provisioning--onboarding)
9. [MQTT / FXMQTT protocol](#mqtt--fxmqtt-protocol)
10. [OPC UA data plane + PTP](#opc-ua-data-plane--ptp)
11. [Security model](#security-model)
12. [Building from source](#building-from-source)
13. [Project layout](#project-layout)
14. [Requirements](#requirements)
15. [FAQ / notes](#faq--notes)
16. [License](#license)

---

## Quick start

Everything runs on the RPi 5.

```bash
# on the RPi (Debian bookworm, PREEMPT_RT kernel recommended)
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev libmosquitto-dev \
  mosquitto nodejs npm

# build the C core
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

# run the C tests
./build/htsn-tests

# the dashboard: http://<rpi-ip>:1880/
systemctl status htsn-nodered
```

The RPi services (all `systemd`, all enabled at boot):

| Service | Port | Purpose |
|---------|------|---------|
| `htsn-nodered` | 1880 | Node-RED dashboard (the GUI) |
| `htsn-cnc` | 4840 | OPC UA server + MQTT bridge |
| `htsn-poller` | — | Telemetry poller (OPC UA → JSON) |
| `mosquitto` | 1883 | MQTT broker (wireless plane) |
| `ptpd` | 319/320 | PTP v2 grandmaster (wired TSN) |
| `htsn-ai` | — | Vision (YOLOv4 on CAM stream) |
| `htsn-policy` | — | Autonomous TSN rules |
| `htsn-llm` | 8081 | LLM bridge (Ollama) |
| `ollama` | 11434 | Local LLM runtime (qwen2.5:1.5b) |

---

## Architecture at a glance

```
  ┌──────────────────────────── RPi 5 (the CNC) ────────────────────────────────────┐
  │                                                                                  │
  │   ┌──────────────────────────────────────────────────────────────────┐           │
  │   │           Node-RED dashboard (:1880, 11 tabs, 231 nodes)        │           │
  │   │ Devices | TSN Config | Monitor | Firmware | Cameras | AI        │           │
  │   │ TSN Endpoint | Settings | Backend | HTSN | TSN Config (p2)      │           │
  │   └──────────────────┬──────────────────────────────┬────────────────┘           │
  │                      │ OPC UA + MQTT                │ HTTP (llm_chat)            │
  │   ┌──────────────────▼──────────────┐  ┌────────────▼───────────────┐            │
  │   │  C11 control core (src/)        │  │ Edge AI (rpi-ai/)          │            │
  │   │  device, qos, vlan, timesync,   │  │ vision (YOLOv4 on CAM)     │            │
  │   │  tas, stream, ...  SQLite       │  │ policy engine (R1..R3)     │            │
  │   └──────────────────┬──────────────┘  │ LLM bridge (Ollama)        │            │
  │                      │                 └────────────────────────────┘            │
  │   ┌──────────────────▼──────────────┐  ┌────────────────────────────┐            │
  │   │ mosquitto (MQTT :1883)         │  │ Ollama (:11434)            │            │
  │   └──────────────────┬──────────────┘  └────────────────────────────┘            │
  │   ┌──────────────────▼──────────────┐  ┌────────────────────────────┐            │
  │   │ OPC UA server (cnc_opcua :4840)│  │ ptpd grandmaster (eth0)    │            │
  │   │ + tsn_opcua_link poller        │  │ PREEMPT_RT, 802.1AS        │            │
  │   └──────────────────┬──────────────┘  └────────────────────────────┘            │
  └──────────────────────┼───────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼────────────────────────┐
         │ MQTT (WiFi)   │ OPC UA + PTP (Ethernet)│
         ▼               ▼                        ▼
   ESP32 wireless     ESP32-CAM            STM32 TSN endpoint
   (esp32-agent)     (esp32-cam)           (stm32-tsn/, Zephyr)
   sensors, relay    MJPEG stream,         PTP slave + OPC UA client,
   WiFiVision, OTA   microSD clips,        sonar/display/servo/relay
   zero-touch        OTA                   192.168.1.20
```

- **C core** (`src/`) — modular, dependency-injected managers connected through an
  event bus; all state persisted in **SQLite**; communicates only via **MQTT/FXMQTT**.
- **Node-RED dashboard** (`htsn-nodered/`) — the full GUI: 231 nodes, 11 tabs.
- **Wired TSN CNC** (`rpi-tsn/`) — the **OPC UA server** (wired data plane), poller,
  and **ptpd grandmaster**.
- **Wired TSN endpoint** (`stm32-tsn/`) — Zephyr STM32: **PTP slave** + **OPC UA
  client** with actuators (sonar, display, buzzer/servo).
- **Edge AI** (`rpi-ai/`) — vision, policy engine, LLM bridge (systemd services).
- **Firmware agents** (`esp32-agent/`, `esp32-cam/`) — ESP-IDF with zero-touch
  provisioning and OTA.

> **Heterogeneous, by design.** The wired nodes do real TSN: **PTP** time-sync and
> (on capable switches) **GCL/TAS** — the STM32 endpoint locks to the RPi grandmaster.
> Ordinary 802.11 cannot deliver deterministic TSN, so **wireless** nodes get the
> *management plane* instead: QoS, VLAN, TAS/GCL, gPTP and stream reservation are
> configured over MQTT, and the radio layer maps 802.1P onto WMM access categories.

---

## The two data planes

| Plane | Nodes | Transport | Time-sync |
|-------|-------|-----------|-----------|
| **Wireless** | ESP32, ESP32-CAM | **MQTT** (FXMQTT) over WiFi | software gPTP (best-effort, sub-ms) |
| **Wired TSN** | STM32 endpoint | **OPC UA** (`opc.tcp :4840`) over Ethernet | **PTP / 802.1AS** — RPi GM, endpoint slave |

- The **C core / Node-RED / Edge AI** all sit on the RPi and see *both* planes.
- **MQTT** is the single channel for the wireless plane (commands, telemetry, FX, OTA).
- **OPC UA** is the single channel for the wired plane (telemetry + `cmd_*`);
  **PTP** carries the shared time base.

---

## Components

### C core

Built as `htsn-core` (static lib) with two executables:

| Binary | Purpose |
|--------|---------|
| `htsn-cli` | headless controller (`--headless`, `--db`, `--mqtt-host`) |
| `tsn-node-agent` | host firmware agent (Linux/RPi adapter) |

### Node-RED dashboard

The [Node-RED](https://nodered.org/) flow (`htsn-nodered/flow-production.json`,
231 nodes) is the **sole GUI**, running on the RPi as `htsn-nodered.service` (port 1880).

| Tab | Content |
|-----|---------|
| **HTSN** (live) | servo gauge, PTP status, TSN applied state, relay/buzzer/sonar, MQTT feed |
| **TSN Config** | VLAN, PCP, traffic class, preemption, timesync, stream role, TAS, GCL → `cmd_tsn_config` |
| **Devices** | device list, add/remove, firmware status, discovery |
| **Monitor** | live telemetry, sensor values, network metrics |
| **TSN Endpoint** | servo angle, relay, buzzer, sonar trigger, reboot |
| **Firmware** | OTA upload, version tracking, A/B slot management |
| **Cameras** | ESP32 camera proxy (live stream, last frame, replay) |
| **AI Assistant** | LLM chat (Ollama), policy engine, decisions |
| **Settings** | broker config, OPC UA settings, system parameters |
| **Backend** | MQTT listener status, service health, log viewer |
| **TSN Config (p2)** | advanced QoS/VLAN/TAS/stream management |

Deploy: `htsn-nodered/deploy.sh all` (see [htsn-nodered/PORT_PLAN.md](htsn-nodered/PORT_PLAN.md)).

### Wired TSN endpoint + RPi CNC

- **RPi CNC — `rpi-tsn/`** ([open62541](https://open62541.org/) v1.5):
  - `cnc_opcua` — **OPC UA server** (`opc.tcp://<ip>:4840`)
  - `tsn_opcua_link` — poller: OPC UA → JSON for Node-RED
  - `htsn_opcua_cli` — CLI for manual node read/write
  - `ptpd.conf` — PTP v2 grandmaster (domain 0, priority1 11)
- **STM32 endpoint — `stm32-tsn/`** ([Zephyr](https://docs.zephyrproject.org/), Nucleo-F767ZI):
  - **PTP slave** (`tsn/ptp.c`) — locks to the RPi grandmaster
  - **OPC UA client** — writes telemetry, monitors `cmd_*`
  - **Actuators** — sonar, OLED display, buzzer/servo (PWM)
  - Static L3: endpoint `192.168.1.20`, RPi `192.168.1.10` (no DHCP)

Build/flash: `west build -b nucleo_f767zi stm32-tsn && west flash`

### Edge AI services

Three services (`rpi-ai/`) on the RPi (one systemd unit each):

| Service | Purpose |
|---------|---------|
| `htsn-ai` | YOLOv4-tiny detection on ESP32-CAM MJPEG stream |
| `htsn-policy` | Autonomous TSN rules (R1: motion→QoS, R2: offset→GM swap, R3: latency→stream) |
| `htsn-llm` | Local LLM bridge (Ollama, allowlist-validated actions) |

All AI activity is audited in the **AI Decisions** table (source, action, params, reason).

### Firmware agents

- **`esp32-agent/`** — ESP-IDF agent: zero-touch provisioning, MQTT commands,
  sensors (BME280, light, PIR, sonar, WiFiVision), relay, OTA A/B (CRC-verified),
  software gPTP, LWT, SNTP.
- **`esp32-cam/`** — ESP32-CAM: MJPEG stream, microSD clip recording, OTA.

---

## Raspberry Pi edge deployment

Everything runs on **one RPi 5** — the CNC. No PC needed.

| Service | Unit |
|---------|------|
| Node-RED dashboard | `htsn-nodered.service` |
| C11 CNC core | `htsn-cnc.service` |
| OPC UA poller | `htsn-poller.service` |
| MQTT broker | `mosquitto.service` |
| PTP grandmaster | `ptpd` (via `htsn-cnc` or standalone) |
| Edge AI | `htsn-ai`, `htsn-policy`, `htsn-llm` |
| LLM runtime | `ollama.service` |
| Auto-update (git pull → rebuild → restart) | `htsn-update.timer` (30 min) |
| Backup (hot DB + configs, 14 d) | `htsn-backup.timer` (daily) |
| Stable remote address | `tailscaled.service` |

Secrets in **`/etc/htsn/env`** (0600). Unit files in `rpi-ai/systemd/`.
PREEMPT_RT kernel for tight PTP jitter: `sudo bash deploy/enable_preempt_rt.sh`.

---

## AI on the edge

Three AI paths, all **local** (no cloud), all **audited**:

1. **LLM assistant** — Node-RED AI tab → LLM bridge (:8081) → Ollama. Actions
   validated against an allowlist with clamped params.
2. **Policy engine** — watches MQTT telemetry + DB state, applies rules with cooldowns.
3. **Vision** — YOLOv4-tiny on ESP32-CAM stream → sensor values + clip triggers.

Every change recorded in **AI Decisions** (time, source AI/LLM/user, device, action, params).

---

## Firmware & OTA

- **Upload** via Node-RED Firmware tab → CRC32 computed, version derived from filename.
- **OTA** — device downloads to inactive A/B partition, **verifies CRC32** before
  reboot, auto-rollback on bad boot.
- **STM32** — flashed via ST-Link (`west flash`) or OTA over Ethernet.

---

## Provisioning & onboarding

ESP32 nodes: SoftAP `HTSN-Setup-<id>` → portal at http://192.168.4.1/ → enter MQTT
broker → auto-connect. No manual IP/DNS. LWT + heartbeat for offline detection.

---

## MQTT / FXMQTT protocol

All wireless traffic on `tsn/#`:
- `tsn/discover/<id>` — node announces (will/alive)
- `tsn/cmd/<id>/<action>` — commands (apply, qos, vlan, timesync, tas, stream, ota, …)
- `tsn/sensors/<id>` — telemetry (1 Hz)
- `tsn/sensors/event/<id>` — motion/AI events
- `tsn/fx/data` — FXMQTT field exchange (C2C)

---

## OPC UA data plane + PTP

Wired TSN link (RPi eth0 ↔ STM32):
- **OPC UA** (`opc.tcp://192.168.1.10:4840`): telemetry nodes + `cmd_*` variables
- **PTP v2** (2-step, domain 0): RPi grandmaster → STM32 slave
- **UDP 4000/4001**: htsn frame protocol (telemetry 10 Hz, commands)

Node IDs: see `rpi-tsn/htsn_opcua_ids.h`.

---

## Security model

- MQTT: username/password (mosquitto acl)
- OPC UA: anonymous (LAN-only, eth0 isolated)
- Node-RED: Basic auth (configured in `settings.js`)
- OTA: CRC32 verification, A/B rollback
- AI: allowlist-validated actions, clamped params, audit trail
- Secrets: `/etc/htsn/env` (0600, root-only)

---

## Building from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -- -j$(nproc)
./build/htsn-tests          # 113 assertions
cpack -G TGZ                # package
```

**CI** (GitHub Actions): host build + tests + ASan/UBSan + cppcheck + ESP-IDF builds.

---

## Project layout

```
src/                  C11 control-plane core
  app/                entry points, tests
  common/             logging, string utils, errors
  db/                 SQLite schema + CRUD
  device/             device model + manager
  qos|vlan|timesync|tas|sensors/   domain services
  stream/             802.1Qcc stream reservation
  mqtt|fxmqtt/        MQTT + FX over MQTT
  radio/              WMM/802.11e mapping (802.1P -> AC)
  config_version/     config snapshots + diff/rollback
esp32-agent/          ESP-IDF ESP32 firmware (wireless/MQTT)
esp32-cam/            ESP-IDF ESP32-CAM (MJPEG, clips)
stm32-tsn/            Zephyr STM32 wired TSN endpoint
rpi-tsn/              RPi wired TSN CNC (OPC UA, poller, ptpd.conf)
shared/               shared ESP-IDF components (prov, ota, version)
rpi-ai/               Edge AI services (vision, policy, LLM bridge)
htsn-nodered/         Node-RED dashboard (flow-production.json, deploy.sh)
deploy/               enable_preempt_rt.sh
docs/                 ARCHITECTURE, BUILD, EDGE
tests/                C unit tests (113 assertions)
```

---

## Requirements

| What | Version | Purpose |
|------|---------|---------|
| CMake | >= 3.16 | Build system |
| GCC/Clang | C11 | Compiler |
| SQLite3 | >= 3.30 | Database |
| libmosquitto | >= 2.0 dev | MQTT client |
| Node.js | >= 20 | Node-RED dashboard |
| open62541 | 1.5 | OPC UA (rpi-tsn) |
| Zephyr SDK | 1.0+ | STM32 firmware build |
| Ollama | latest | Local LLM (optional) |

---

## FAQ / notes

- **Why no PC?** The RPi 5 is the CNC — it runs the broker, OPC UA server, PTP
  grandmaster, dashboard, and AI. No other machine is needed at runtime.
- **Why Node-RED?** Visual, extensible, runs natively on the RPi, no extra runtime
  beyond Node.js. 231 nodes cover the full feature set.
- **PTP jitter** — needs PREEMPT_RT kernel for <100 µs. Stock kernel: ~1 ms.
- **Multiple STM32 endpoints** — set unique MACs via PD2/PG2-to-GND bridges.

---

## License

MIT — see [LICENSE](LICENSE).
