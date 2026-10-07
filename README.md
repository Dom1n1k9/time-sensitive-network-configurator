# Heterogeneous TSN Configurator

> **Centralized controller (CNC) for Heterogeneous Time-Sensitive Networking (H-TSN).**
> One control plane over a **mixed wired + wireless** network: ESP32 / Raspberry Pi /
> STM32 / NXP / Linux nodes are managed against **IEEE 802.1Qcc** (QoS, VLAN,
> gPTP/time-sync, TAS/GCL, stream reservation). **Two data planes**: wireless nodes
> (ESP32) talk **MQTT** over WiFi; the wired TSN endpoint (STM32) talks **OPC UA** over
> Ethernet with **PTP (802.1AS / gPTP)** time-synchronization. **Zero-touch onboarding**:
> flash an agent, power it on, and it provisions and connects itself. **Local AI on the
> edge**: a Raspberry Pi runs a local LLM assistant, an autonomous policy engine and
> camera-based vision — all decisions are validated, executed with provenance and
> auditable.

A production-oriented configuration and control plane for H-TSN. The **control-plane
core is written in pure C (C11)** and ships as a CLI/headless service and a host
firmware agent. The **front-end is a Node-RED dashboard** (`htsn-nodered/`) —
231 nodes across 11 tabs covering devices, TSN config, monitoring, firmware OTA,
camera proxy, AI assistant, and backend listeners. The **edge AI services**
(`rpi-ai/`) and the **wired TSN CNC** (`rpi-tsn/`) run on the Raspberry Pi.

It acts as a centralized controller (CNC-style, aligned with IEEE 802.1Qcc) that
discovers and manages heterogeneous nodes — wireless over MQTT and the wired TSN
endpoint over OPC UA — applies QoS / VLAN / time-synchronization / schedule policies,
reads sensors, performs firmware OTA with CRC-verified images, and exposes the whole
network over **FXMQTT** — OPC UA FX / C2C Field Exchange carried over MQTT.

---

## Table of contents

1. [Quick start](#quick-start)
2. [Architecture at a glance](#architecture-at-a-glance)
3. [The two data planes](#the-two-data-planes)
4. [Components](#components)
    - [C core (CLI / headless)](#c-core)
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
11. [Security](#security)
12. [Build & test](#build--test)
13. [Project layout](#project-layout)
14. [Requirements](#requirements)
15. [FAQ / notes](#faq--notes)
16. [License](#license)

---

## Quick start

Everything runs from one machine. See [Raspberry Pi edge deployment](#raspberry-pi-edge-deployment)
for the full Pi service setup, [Provisioning & onboarding](#provisioning--onboarding)
for a physical **ESP32** node, and [OPC UA data plane + PTP](#opc-ua-data-plane--ptp)
for the wired TSN endpoint.

```bash
# host dependencies (Debian/Ubuntu)
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev libmosquitto-dev \
  mosquitto python3 python3-pip
python3 -m pip install paho-mqtt

# build the C core (CLI + tests + host agent)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

 # run the C tests
 ./build/htsn-tests

 # Node-RED dashboard (on RPi): http://<rpi-ip>:1880/
 systemctl status htsn-nodered
 ```

### Everything with one command — `run.sh`

`run.sh` is the one-launcher script:

```bash
./run.sh                 # MQTT broker + web GUI + browser + provisioning helper
./run.sh --flash         # also build & flash the ESP32 agent first
./run.sh --headless      # services only (broker + GUI + mDNS), no browser/terminal
```

It:
- starts **mosquitto** on `0.0.0.0:1883` (auto-detects your LAN IP),
- launches **`webgui.py`** on http://127.0.0.1:8000 (with a self-healing watchdog,
  log `/tmp/htsn_mon.log`) and opens the browser,
- advertises this PC as the MQTT broker via **mDNS** (`htsn-broker.local`) with avahi,
- spawns a terminal showing how to reach the **`HTSN-Setup`** SoftAP
  (http://192.168.4.1/).

With **`HTSN_TSN=1`** (on the RPi), it also starts the **wired TSN CNC**: the **ptpd
grandmaster** on the wired NIC and the **OPC UA server** + poller (see
[OPC UA data plane + PTP](#opc-ua-data-plane--ptp)).

**Windows:** `run.ps1` is the equivalent — LAN IP detection, optional mosquitto startup,
GUI health check + restart loop, browser launch:
`.\run.ps1` / `.\run.ps1 -Headless`.

**Desktop launcher** (double-click): run `./launcher/install.sh` once, then double-click
the **"HTSN Configurator"** icon.

**Auto-start at login:** the helper installs a desktop autostart entry that runs
`run.sh --headless`.

---

## Architecture at a glance

```
  ┌──────────────────────────── RPi edge node (CNC) ─────────────────────────────┐
  │                                                                              │
   │   ┌────────────────────────────────────────────────────────────────┐         │
   │   │          Node-RED dashboard (:1880, 11 tabs)                   │         │
   │   │ Devices | TSN Config | Monitor | Firmware | Cameras | AI      │         │
   │   │ TSN Endpoint | Settings | Backend | HTSN | TSN Config (p2)    │         │
   │   └───────────────┬───────────────────────────────┬────────────────┘         │
   │                   │ OPC UA + MQTT                 │ llm_chat (HTTP)          │
  │   ┌───────────────▼──────────────┐    ┌───────────▼───────────────┐          │
  │   │  C11 control core (src/)     │    │ Edge AI (rpi-ai/)         │          │
  │   │  device, qos, vlan, timesync,│    │ vision (YOLOv4 on CAM)    │          │
  │   │  tas, stream, ...  SQLite    │    │ policy engine (R1..R3)    │          │
  │   └───────────────┬──────────────┘    │ LLM bridge (Ollama, allow-│          │
  │                   │                   │  list-validated actions)  │          │
  │   ┌───────────────▼──────────────┐    └───────────────────────────┘          │
  │   │ mosquitto (MQTT broker)      │    ┌───────────────────────────┐          │
  │   └───────────────┬──────────────┘    │ Ollama (qwen2.5:1.5b)     │          │
  │                   │                   └───────────────────────────┘          │
  │   ┌───────────────▼──────────────┐    ┌───────────────────────────┐          │
  │   │ OPC UA server (rpi-tsn,      │    │ ptpd grandmaster          │          │
  │   │  opc.tcp :4840) + poller     │    │ (PREEMPT_RT, 802.1AS)     │          │
  │   └───────────────┬──────────────┘    └───────────────┬───────────┘          │
  └───────────────────┼───────────────────────────────────┼──────────────────────┘
                       │ MQTT (wireless)                   │ OPC UA + PTP (wired)
        ┌──────────────┼───────────────────┐               │
        ▼              ▼                   └───────────────▼
   esp32-agent /     tsn-node-agent                   STM32 TSN endpoint
   esp32-cam         (host agent, Linux/RPi)          (stm32-tsn/, Zephyr)
   (ESP-IDF,                                          PTP slave + OPC UA client,
    zero-touch,                                       sonar/display/actuator
    sensors, OTA A/B)
```

- **C core** (`src/`) — modular, dependency-injected managers connected through an
  event bus; all state persisted in **SQLite**; communicates only via **MQTT/FXMQTT**.
- **Node-RED dashboard** (`htsn-nodered/`) — the full GUI: 231 nodes, 11 tabs
  (devices, TSN config, monitoring, firmware, cameras, AI, settings, backend).
- **Wired TSN CNC** (`rpi-tsn/`) — the **OPC UA server** (the wired data plane) and a
  poller that feeds telemetry, plus the **ptpd grandmaster** config.
- **Wired TSN endpoint** (`stm32-tsn/`) — a Zephyr STM32 node that is a **PTP slave**
  and an **OPC UA client** (writes telemetry, monitors `cmd_*`), with actuators
  (sonar, display, buzzer/servo).
- **Edge AI** (`rpi-ai/`) — vision, policy engine and LLM bridge; runs next to the
  GUI on the Pi, talks to the GUI through its action API.
- **Firmware agents** (`esp32-agent/`, `esp32-cam/`) — ESP-IDF software for physical
  boards with zero-touch provisioning; a host agent (`tsn-node-agent`) for
     Linux/Raspberry Pi and compile-safe stubs for STM32/NXP.

> **Heterogeneous, by design.** The wired nodes do real TSN: **PTP** time-synchronization
> and (on capable switches) **GCL/TAS** — the STM32 endpoint locks to the RPi grandmaster.
> Ordinary 802.11 cannot deliver deterministic TSN, so the **wireless** nodes get the
> *management plane* instead: QoS, VLAN, TAS/GCL, gPTP and stream reservation are
> configured, applied and monitored over MQTT, and the radio layer maps 802.1P
> priorities onto WMM access categories. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## The two data planes

H-TSN keeps the control plane unified but splits the **data plane** by link type:

| Plane | Nodes | Transport | Time-sync |
|-------|-------|-----------|-----------|
| **Wireless** | ESP32, ESP32-CAM | **MQTT** (FXMQTT) over WiFi | software gPTP (best-effort, sub-ms on WiFi) |
| **Wired TSN** | STM32 endpoint | **OPC UA** (`opc.tcp :4840`) over Ethernet | **PTP / 802.1AS** — RPi grandmaster, endpoint slave |

- The **C core / web GUI / edge AI** sit on the RPi and see *both* planes.
- **MQTT is the single channel for the wireless plane** (commands, telemetry, FX, OTA).
- **OPC UA is the single channel for the wired plane** (telemetry + `cmd_*` control);
  **PTP** carries the shared time base. The two planes are bridged in the GUI, so a
  SCADA/HMI or the AI can address any node regardless of link type.

---

## Components

### C core

The control-plane engine, built as `htsn-core` (static lib) with two executables:

| Binary | Purpose |
|--------|---------|
| `htsn-cli` | headless/CLI controller (`--headless`, `--db`, `--mqtt-host`, `--mqtt-port`, `--plugin-dir`) |
| `tsn-node-agent` | host firmware agent (Linux/RPi adapter) that executes controller commands |

See [docs/BUILD.md](docs/BUILD.md) for platform details.

### Web GUI

A self-contained SPA served on http://127.0.0.1:8000:

| Page       | Purpose |
|------------|---------|
| Devices    | add/remove/ping nodes; status, **deploy status** (last apply + ACK per device), USB port, domain; **per-device firmware manager** (upload / flash / OTA with CRC), **live camera** (ESP32-CAM), TSN features, **config versions / rollback** |
| Monitor    | live network/frame trace with search filter, **Pause/Start**, Clear |
| Metrics    | control-plane E2E latency + gPTP clock offset/jitter history, per-device summary + SVG charts (legend, axis labels, time windows) |
| Sensors    | live values per node + **history sparklines** (temperature, pressure, humidity, light, PIR, sonar, **WiFi CSI motion**, **AI detection**) |
| TSN Endpoint | the **wired** STM32 node: live PTP offset/lock, OPC UA telemetry, and `cmd_*` actuator control (sonar / display / buzzer / servo) |
| AI Assistant | **local LLM chat** — executes validated TSN actions or guides configuration step by step; **AI Decisions** audit trail with provenance (AI / LLM / user) |
| Architecture | live network topology: nodes, links and live traffic |
| FXMQTT     | Field Server / Participant, broker address, **live FX data feed** (C2C field exchange values) + test-send |
| Synchronization | gPTP grandmaster / slave setup (802.1AS), per-node offset reports |
| QoS / VLAN | 802.1Q priority mapping (priority 0–7, traffic class, bandwidth, latency), VLAN groups + membership |
| TAS / GCL  | gate control lists with **visual gate windows** (802.1Qbv) |
| Preemption | eMAC / pMAC priority split (802.1Qbu) |
| Streams    | 802.1Qcc talker / listener reservations, deploy, status (ready / standby / failed) |

### Node-RED dashboard

The [Node-RED](https://nodered.org/) flow (`htsn-nodered/flow-production.json`,
231 nodes) is the **sole GUI**, running on the RPi as `htsn-nodered.service`
(port 1880). It connects to the OPC UA server, MQTT broker, and SQLite DB.

| Tab | Content |
|-----|---------|
| **HTSN** (live) | servo gauge, PTP/gPTP status, TSN applied state, relay/buzzer/sonar/buttons, MQTT `tsn/#` feed |
| **TSN Config** | form to write TSN config (VLAN, PCP, traffic class, preemption, timesync, stream role, TAS, GCL) to `cmd_tsn_config` via OPC UA |
| **Devices** | device list, add/remove, firmware status, discovery |
| **Monitor** | live telemetry, sensor values, network metrics |
| **TSN Endpoint** | servo angle, relay, buzzer, sonar trigger, reboot commands |
| **Firmware** | OTA upload, version tracking, A/B slot management |
| **Cameras** | ESP32 camera proxy (live stream, last frame, replay) |
| **AI Assistant** | LLM chat (Ollama), policy engine status, decisions |
| **Settings** | broker config, OPC UA settings, system parameters |
| **Backend** | MQTT listener status, service health, log viewer |

The flow polls the OPC UA server (`cnc_opcua`, `opc.tcp://127.0.0.1:4840`) for
telemetry and writes TSN config as a `String`-typed OPC UA variant. Camera proxy
forwards to ESP32 MJPEG streams. See
[htsn-nodered/PORT_PLAN.md](htsn-nodered/PORT_PLAN.md) for the full node inventory.

### Wired TSN endpoint + RPi CNC

The wired, deterministic half of the network:

- **RPi CNC — `rpi-tsn/`** (built against [open62541](https://open62541.org/) v1.5):
  - `cnc_opcua` — the **OPC UA server** (`opc.tcp://<ip>:4840`); the wire for the
    wired data plane. The STM32 writes telemetry nodes and monitors `cmd_*` variables
    here; the GUI/SCADA and AI talk OPC UA to it too.
  - `tsn_opcua_link` — a poller: one persistent OPC UA client that dumps the current
    node set + `cmd_*` state to a JSON file the GUI reads (no OPC UA dependency in the
    web stack).
  - `htsn_opcua_cli` — a small CLI to read/write nodes for manual testing.
  - `ptpd.conf` — the **PTP v2 grandmaster** profile (domain 0, priority1 11,
    software 1-step). Started by `run.sh` on the wired NIC when `HTSN_TSN=1`.
- **STM32 endpoint — `stm32-tsn/`** ([Zephyr](https://docs.zephyrproject.org/),
  Nucleo-F767ZI):
  - **PTP slave** (`tsn/ptp.c`) — locks to the RPi grandmaster over the same wired NIC.
  - **OPC UA client** — writes telemetry and monitors `cmd_*` on the RPi server
    (the wired equivalent of the ESP32's MQTT command execution).
  - **Actuators** — ultrasonic **sonar**, I2C **OLED display**, and a **buzzer / servo**
    (PWM), driven by `cmd_*` and used to make TSN effects observable.
  - Static L3: endpoint `192.168.1.20`, RPi GM `192.168.1.10` (no DHCP).

See [OPC UA data plane + PTP](#opc-ua-data-plane--ptp) for the protocol and
[stm32-tsn/README.md](stm32-tsn/README.md) for the firmware (build/flash with `west`).

### Edge AI services

Three small Python services (`rpi-ai/`) that run on the Raspberry Pi next to the GUI
(one systemd unit each: `htsn-ai`, `htsn-policy`, `htsn-llm`):

| Service | Script | What it does |
|---------|--------|--------------|
| `htsn-ai` | `vision_service.py` | **YOLOv4-tiny** (OpenCV DNN) person/object detection on the ESP32-CAM MJPEG stream. On detection: triggers the CAM to record its own microSD clip (motion event on `tsn/sensors/event`), publishes live detection counters (`ai_detect` / `ai_person`) as sensors, keeps a rolling thumbnail + short clip |
| `htsn-policy` | `policy_engine.py` | **Autonomous TSN decisions** — watches the network and reconfigures it through the GUI action API (provenance **AI**): R1 person + PIR motion → raise QoS + open TAS gate; R2 gPTP grandmaster offset too high → switch grandmaster; R3 E2E latency too high → reserve an 802.1Qcc stream. Thresholds + cooldowns in `policy.json` |
| `htsn-llm` | `llm_bridge.py` | **Local LLM bridge** (HTTP :8081) in front of **Ollama** (default `qwen2.5:1.5b`, runs on the Pi CPU). Turns chat into strict-JSON action proposals, validates them against an **allowlist with clamped params** and executes them via the GUI API (provenance **LLM**). The LLM never touches MQTT/network directly |

All AI activity lands in the **AI Decisions** table (audit trail with source, action,
params, reason) and the **Devices** page flags devices the AI is currently configuring.
Rollback for any AI change: **Config Versions** page. See [AI on the edge](#ai-on-the-edge).

### Firmware agents

- **`esp32-agent/`** — the reference ESP32 (ESP-IDF v5.x) agent:
  - **zero-touch provisioning** — SoftAP `HTSN-Setup-<id>` + portal
    (http://192.168.4.1/), NVS storage, auto re-provision as fallback,
  - **MQTT command execution** — `apply` (JSON snapshot), `qos`, `vlan`, `timesync`,
    `tas`, `stream`, `preemption`, `status`, `wifi`, `fx`, `actor`, `ping`, `identify`,
    `ota` (CRC-verified), `reset`, `reboot`, `factory`,
  - **TSN config persisted to NVS** and restored on reboot,
  - **software gPTP** over UDP multicast (`224.0.1.129`, best-effort, sub-ms on WiFi),
  - **sensors** — BME280 (bit-bang I2C), TEMT6000 light, HC-S501 PIR, ultrasonic sonar,
    **WiFiVision** (device-free motion from RSSI noise + optional WiFi CSI variance —
    no extra hardware, events on the same sink as the PIR); 1 s heartbeat +
    history + sparklines in the GUI,
  - **relay actor** — GPIO16 relay pulse on motion (auto-detected sensor/relay role),
  - **OTA** — A/B slots, **device-side CRC32 verification** of the downloaded image
    before reboot, automatic rollback on bad boot (`shared/htsn_ota`),
  - **LWT last-will**, **SNTP** time, **LED** status (provisioning = fast blink,
     connecting = blink, online = solid), **factory reset** (BOOT 3 s),
- **`esp32-cam/`** — ESP32-CAM node streaming MJPEG to the Devices page, motion-driven
  **microSD clip recording** (triggered by the vision service or any PIR/WiFiVision
  event), provisioned with the same shared portal, OTA-capable (CRC-verified).

See [esp32-agent/README.md](esp32-agent/README.md) for the full firmware protocol and
wiring tables.

---

## Raspberry Pi edge deployment

The reference deployment runs **everything on one RPi 5** (the "edge node"):
broker, CNC core, web GUI, edge AI, the wired TSN CNC (OPC UA + PTP), Ollama,
auto-update and backup — with the ESP32 nodes as pure wireless clients and the STM32
as the wired TSN endpoint.

| What | Where |
|------|-------|
| mosquitto (MQTT, auth) | `mosquitto.service` |
| C11 CNC core (headless) | `htsn-cli.service` |
| Web GUI (Basic auth, :8000) | `htsn-webgui.service` |
| **Wired TSN CNC** (OPC UA :4840 + poller) | `run.sh` with `HTSN_TSN=1` (`rpi-tsn/`) |
| **PTP grandmaster** (ptpd, PREEMPT_RT) | `run.sh` with `HTSN_TSN=1` (`rpi-tsn/ptpd.conf`) |
| Vision / Policy / LLM bridge | `htsn-ai`, `htsn-policy`, `htsn-llm` |
| Local LLM runtime | `ollama.service` (qwen2.5:1.5b, CPU) |
| Auto-update (git pull → rebuild → restart) | `htsn-update.timer` (30 min) |
| Backup (hot DB copies + configs, 14 d) | `htsn-backup.timer` (daily) |
| Stable remote address (Tailscale) | `tailscaled.service` |
| Multi-WiFi failover (NetworkManager, by priority) | `wlan0` |

All secrets live in **`/etc/htsn/env`** (root-only, 0600) — the systemd units never
contain credentials. The unit files are in [`rpi-ai/systemd/`](rpi-ai/systemd/) and the
step-by-step setup (services, env file, Ollama, Tailscale, multi-WiFi,
update/backup) is in **[docs/EDGE.md](docs/EDGE.md)**. The PTP grandmaster wants a
**PREEMPT_RT** kernel for tight jitter — provision it with
`sudo bash deploy/enable_preempt_rt.sh` (adds the `rpi-*-rt` kernel, sets
`kernel=` in `config.txt`, applies PTP net tuning; stock kernel kept for rollback).

Highlights:
- **Auto-update** — every 30 min: `git pull` → C core rebuild → sync `rpi-ai/*.py`
  → restart only the services that changed. Failures keep the old code and log to
  `/home/htsn/htsn-ai/update.log`.
- **Backup** — daily hot SQLite DB copies plus key configs into
   `/home/htsn/backups/<stamp>/`, 14-day retention.
- **Reachable from anywhere** — Tailscale gives the Pi a stable name/IP
  (`http://rpi:8000`, `ssh htsn@rpi`) independent of which WiFi it sits on.
- **Multi-WiFi** — NetworkManager profiles with priorities (site hotspot first,
  fallbacks after); the Pi re-joins the best available network automatically.

---

## AI on the edge

Three AI paths, all **local** (no cloud), all **audited**:

1. **LLM assistant (chat).** The AI Assistant page proxies to the LLM bridge
   (`HTSN_LLM_URL`, default `http://127.0.0.1:8081`) → Ollama. The model replies with
   strict JSON: either an **action** (e.g. `save_qos`, `save_vlan`, `save_tas`,
   `save_stream`, `deploy_stream`, `ping_device`, `exec_all`, …) or a **guide**
   (step-by-step how-to using the real GUI pages). Actions are validated against an
   allowlist with clamped numeric params and known-device checks; unknown devices,
   out-of-range values and non-allowlisted actions are refused. Execution goes through
   the normal GUI action API with `source="llm"`, so it is indistinguishable from (and
   rolled back like) any manual change.
   On a Pi CPU a chat answer takes ~30 s with the 1.5 B model — the UI shows a
   "thinking" state.
2. **Policy engine (autonomous).** No chat involved: the policy engine watches MQTT
   telemetry + DB state and applies rules with cooldowns — person + PIR → raise QoS /
   open TAS gate (R1); grandmaster offset too high → swap grandmaster to the
   best-offset node (R2); E2E latency too high → reserve an 802.1Qcc stream (R3).
   Config: `policy.json` (thresholds, cooldowns, on/off per rule).
3. **Vision (perception).** YOLOv4-tiny on the ESP32-CAM stream. Detections become
   (a) sensor values in the GUI (Sensors page), (b) a clip-recording trigger on the
   CAM, and (c) the input the policy engine's R1 rule consumes.

Every change from 1 and 2 is recorded in **AI Decisions** (time, source AI/LLM/user,
device, action, params, reason) and shown live on the Devices page (devices get an
**AI** badge while being configured).

---

## Firmware & OTA

The firmware manager lives **per device** on the Devices page:

- **Upload** — `.bin` / `.img` / `.hex`; the server validates the type, computes the
  **CRC32** and derives a version from the filename (`…_v1.2.3.bin`), tags the device
  kind (sensor / cam) and stores it in `build/fw/` + the `firmware` table.
- **Flash / OTA** — publishes `{"url": "http://<host>:8000/fw/<file>", "size": N,
  "crc32": "…"}` on `tsn/cmd/<id>/ota`. The command is only offered when the stored
  firmware's kind matches the device kind.
- **On the device** — the image is downloaded to the inactive A/B partition
  (`esp_https_ota`), then the device **re-reads the partition and verifies the CRC32**
  against the upload-time value: mismatch → the partition is marked invalid, the
  previous app stays active and the GUI never reboots into bad firmware. Match →
   reboot; a bad new app is rolled back automatically by the bootloader on next boot.
   The new version is reported via `tsn/discover` / `tsn/status` and shown per device.

The firmware version constant for agent builds lives in
`shared/htsn_version/htsn_version.h` (`HTSN_FW_VERSION`).

---

## Provisioning & onboarding

**Flash it. Power it. It connects itself.**

- On first boot (no WiFi credentials) a node starts a **SoftAP `HTSN-Setup-<id>`**
  serving the config portal at **http://192.168.4.1/** — enter WiFi SSID / password and
  the MQTT broker; the agent saves them to NVS, reboots, joins your network and
  announces itself on MQTT.
- Each board advertises a **unique SSID** (`HTSN-Setup-<device-id>`) so multiple boards
  are distinguishable in setup mode.
- If a node loses its network it gives up after a few failed reconnects and
  **automatically restarts the SoftAP + portal** for over-the-air re-provisioning.
- WiFi can be changed later from the web GUI (`wifi` command on `tsn/cmd/<id>/wifi`).

> **Client isolation:** from a phone/mac hotspot, client isolation can block the node.
> Prefer a normal router WiFi.

For the step-by-step setup (broker, provisioning, connecting in the GUI) see
[esp32-agent/README.md](esp32-agent/README.md#wiring-to-the-web-gui-real-mode).

---

## MQTT / FXMQTT protocol

MQTT is the **single channel for the wireless plane**. The controller publishes
commands to `tsn/cmd/<id>/<command>` and subscribes to the status/discovery/ack/LWT/
sensor feeds.

| Topic (pattern)        | Direction | Purpose |
|------------------------|-----------|---------|
| `tsn/cmd/<id>/apply`   | out | full JSON snapshot (preferred) |
| `tsn/cmd/<id>/{qos,vlan,timesync,tas,stream,preemption,status,wifi,fx,ping,identify,ota,reset,actor,reboot,factory}` | out | per-feature commands |
| `tsn/ack/<id>`         | in  | reply `{"id","ok"[, "ip"]}` |
| `tsn/status`           | in  | heartbeat / status JSON (rssi, fw, ip) |
| `tsn/discover`         | in  | on-connect announcement (`ip`, `fw`, `kind`) |
| `tsn/lwt/<id>`         | in  | retained last-will → node marked offline |
| `tsn/sensors`, `tsn/sensors/<id>/...`, `tsn/sensors/event` | in | telemetry / history / motion events |
| `tsn/ptp`              | in  | gPTP reports |
| `tsn/fx/cmd/<id>`      | out | FX / C2C field-exchange commands |
| `tsn/fx/data`, `tsn/fx/<id>` | in/out | FX data feed (motion events, field-server samples) |
| `tsn/cmd/<id>/stream` + `tsn/fx/cmd/<id>` | out | 802.1Qcc stream reservation |

Command payload notes:

- `apply` — one JSON snapshot (priority, traffic class, VLAN, preemption, timesync,
  TAS cycle + GCL) — the preferred path used by *Execute settings on controller*.
- `ota` — `{"url": "...", "size": N, "crc32": "<hex>"}`; `crc32` triggers the
  device-side image verification (see [Firmware & OTA](#firmware--ota)).
- `wifi` — `{"ssid": "...", "pass": "..."}` with **optional** `pass` (omitted → the
  agent keeps its stored password).

> **Terminology:** the C plugin uses `tsn/discovery` (a legacy alias); everything else
> uses `tsn/discover`.

---

## OPC UA data plane + PTP

The wired plane uses **OPC UA** for data/control and **PTP** for time:

- **Server** — `cnc_opcua` (open62541) on `opc.tcp://<ip>:4840`. Node/variable IDs are
  defined once in `rpi-tsn/htsn_opcua_ids.h` and shared by the server, the poller, the
  CLI and the STM32 client.
- **Endpoint (client)** — the STM32 (`stm32-tsn/`) writes telemetry nodes (sonar, PIR,
  ambient, PTP offset) and monitors `cmd_*` variables (display, buzzer, servo, sonar
  sweep). This is the wired counterpart of the ESP32's MQTT command execution.
- **Bridge to the GUI** — `tsn_opcua_link` keeps one OPC UA session open and writes the
  current node set + `cmd_*` state to a JSON file (`/tmp/htsn_tsn_opcua.json`) that the
  GUI's **TSN Endpoint** page reads; GUI actions set `cmd_*` through the same server.
- **Time** — `ptpd` on the RPi is the **PTP v2 grandmaster** (domain 0, priority1 11,
  software 1-step, `rpi-tsn/ptpd.conf`); the STM32 is a **PTP slave** and reports its
  offset. Run the RPi on a **PREEMPT_RT** kernel (`deploy/enable_preempt_rt.sh`) for
  single-digit-µs scheduler latency, and wire the endpoint to the RPi's wired NIC
  (GM `192.168.1.10`, endpoint `192.168.1.20`).

```bash
# on the RPi (built against open62541 v1.5):
cd rpi-tsn && make PFX=$HTSN_O62541_PFX      # -> cnc_opcua, tsn_opcua_link, htsn_opcua_cli
# grandmaster + server + poller (also done by: HTSN_TSN=1 ./run.sh):
sudo ptpd -i eth0 -f rpi-tsn/ptpd.conf
./cnc_opcua        # opc.tcp://<ip>:4840
./tsn_opcua_link   # -> /tmp/htsn_tsn_opcua.json
./htsn_opcua_cli read 11100 30001             # e.g. read the sonar node
```

> **Why not just OPC UA everywhere?** OPC UA is a heavier, connection-based protocol
> aimed at the wired, low-churn segment; the wireless, many-to-one, chatty segment stays
> on MQTT/FXMQTT. The GUI bridges both so operators and the AI get one consistent view.

---

## Security

### Provisioning (plaintext by default)

The provisioning portal is intentional **plain HTTP on an open SoftAP** (no TLS, no AP
password): the WiFi password is transmitted in cleartext from your phone/PC to the
board. This is the standard zero-touch trade-off, but it means:

- anyone on the `HTSN-Setup-<id>` SoftAP can read the credentials being entered;
- the portal is only reachable from that SoftAP, so the exposure window is the few
  minutes you spend provisioning.

For sensitive deployments, either keep provisioning physically supervised or:

- **Lock the SoftAP behind WPA2-PSK:** store a password in NVS (namespace `htsn`, key
  `ap_pass`, min 8 chars); the compile-time fallback is `PROV_AP_PASS_DEFAULT` in
  `shared/htsn_prov/htsn_prov.c`. After provisioning the SoftAP is gone and normal
  operation only uses MQTT.
- **Secure the MQTT channel:** broker username/password + TLS are supported on both the
  agent and the GUI (see below).

> **WiFi password hygiene:** the node **never reports its saved WiFi password back** and
> `tsn/cmd/<id>/wifi` is accepted with an *optional* `pass` field — when the password is
> omitted, the agent keeps whatever is stored in NVS. So re-pointing an already-provisioned
> node to a new SSID does not re-send the secret over plaintext MQTT.

### MQTT authentication & TLS

Two independent configuration paths — the **agent** (NVS) and the **web GUI** (env):

| Component | Auth / TLS configuration |
|-----------|--------------------------|
| ESP32 agent / CAM | NVS keys in namespace `htsn`: `muser`, `mpass`, `mtls` (1=on), `mtls_ca` (PEM), `minsec` (1 = skip verify, dev only) |
| Web GUI | env: `HTSN_USER`, `HTSN_PASS`, `HTSN_TLS_CA`, `HTSN_TLS_CERT`, `HTSN_TLS_KEY`, `HTSN_TLS_INSECURE=1` (dev) |
| Broker | run mosquitto with authentication and/or TLS listeners on the machine |

The **OPC UA** wired plane can additionally be secured with OPC UA user tokens /
X.509 security policies on `cnc_opcua` (open62541 supports both); on the LAN
segment it runs by default on the trusted `192.168.1.0/24`.

### Web GUI access

The web GUI supports optional HTTP Basic auth via `HTSN_WEB_USER`/`HTSN_WEB_PASS`
(compared in constant time). There is no built-in HTTPS — terminate TLS in a reverse
proxy (nginx/caddy). On the Pi deployment all credentials are kept in
`/etc/htsn/env` (0600, root-only); the systemd units and the repo never contain them.

### AI safety

- The **LLM can only propose allowlisted actions** with clamped parameters; it has no
  direct network/MQTT access — every execution goes through the normal, audited GUI
  action API (`source="llm"`).
- The **policy engine** changes are rate-limited by per-rule cooldowns and fully
  visible in the AI Decisions audit trail; rollback via Config Versions.
- Edge AI is loopback-only: the LLM bridge listens on `127.0.0.1:8081` and Ollama on
  `127.0.0.1:11434` — neither is exposed.

---

## Build & test

```bash
# configure + build (C core: CLI, tests, agent)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

# run C tests (includes the host JSON parser test via ctest)
./build/htsn-tests
(cd build && ctest --output-on-failure)

# headless controller
./build/htsn-cli --headless --db ./config.db

# host firmware agent
./build/tsn-node-agent --id node-01 --platform linux --mqtt-host broker.local

# wired TSN CNC (RPi; needs open62541 v1.5 with -DUA_ENABLE_SUBSCRIBERS=ON)
cd rpi-tsn && make PFX=$HTSN_O62541_PFX

# STM32 TSN endpoint (Zephyr; needs the Zephyr SDK + nrfjprog/ST-LINK or J-Link)
west build -b nucleo_f767zi stm32-tsn && west flash

# Python tests + lint
python3 -m unittest discover -s tests

# package
cpack -G TGZ          # or: cmake --build build --target package
```

**Build options:** `-DBUILD_GUI=ON|OFF` (install the Python web GUI, default ON),
`-DBUILD_PLUGINS=ON|OFF` (build the sample MQTT discovery plugin, default ON).

**CI** (GitHub Actions) runs, on every push/PR:
- host build + `htsn-tests` + packaging,
- Python lint (ruff) + `unittest`,
- **AddressSanitizer/UBSan** build + tests (blocking),
- **cppcheck** static analysis (blocking),
- ESP-IDF build of both firmwares (`esp32-agent`, `esp32-cam`).

See [docs/BUILD.md](docs/BUILD.md) for no-root (local install) and packaging details
and [docs/EDGE.md](docs/EDGE.md) for the Raspberry Pi service deployment.

---

## Project layout

```
src/                  C11 control-plane core
  app/                application bootstrap, entry points, tests
  common/             logging, string utils, errors
  mvc/                model + event bus (GUI controller/view removed)
  db/                 SQLite schema + CRUD repositories
  device/             device model + manager
  discovery/          discovery framework
  qos|vlan|timesync|tas|sensors/   domain services
  stream/             IEEE 802.1Qcc stream reservation (talker/listener)
  mqtt|fxmqtt/        MQTT + OPC UA FX over MQTT
  radio/              WMM/802.11e mapping (802.1P -> AC)
  domain/             per-cell TSN domains
  config_version/     config snapshots + diff/rollback
  telemetry|trace/    telemetry + live communication monitor
   agent/              host firmware agent (Linux/RPi adapter)
   plugin/             loadable protocol plugins (.so)
esp32-agent/          ESP-IDF ESP32 firmware agent (reference, wireless/MQTT)
esp32-cam/            ESP-IDF ESP32-CAM firmware (MJPEG stream node, clips)
stm32-tsn/            Zephyr STM32 wired TSN endpoint (PTP slave + OPC UA client)
rpi-tsn/              RPi wired TSN CNC (OPC UA server, poller, CLI, ptpd.conf)
shared/               shared ESP-IDF components (htsn_prov, htsn_ota, htsn_version)
rpi-ai/               Raspberry Pi edge services
  vision_service.py   YOLOv4-tiny detection on the ESP32-CAM stream (htsn-ai)
  policy_engine.py    autonomous TSN rules R1..R3 (htsn-policy)
  llm_bridge.py       Ollama -> allowlisted GUI actions (htsn-llm)
  update.sh           auto-update (git pull -> rebuild -> sync -> restart)
  backup.sh           daily hot DB + config backups
  systemd/            unit + timer files for the Pi deployment
deploy/               enable_preempt_rt.sh (RPi PREEMPT_RT + PTP tuning)
docs/                 ARCHITECTURE, BUILD, EDGE (Pi deployment)
htsn-nodered/         Node-RED dashboard (flow-production.json, PORT_PLAN.md, deploy.sh)
tests/                Python unit + HTTP smoke tests
launcher/             desktop launcher + autostart
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/BUILD.md](docs/BUILD.md) and
[docs/EDGE.md](docs/EDGE.md).

---

## Requirements

| What          | Version   | Purpose                        |
|---------------|-----------|--------------------------------|
| SQLite3       | >= 3.30   | SQLite database                |
| libmosquitto  | >= 2.0 dev| MQTT/FX client                 |
| CMake         | >= 3.16   | Build system                   |
| GCC/Clang     | C11       | Compiler                       |
| Node.js       | >= 20     | Node-RED dashboard             |
| OpenCV + numpy| any       | Edge AI vision (Pi only)       |
| Ollama        | any       | Local LLM (Pi only, optional)  |
| open62541     | v1.5      | Wired TSN CNC (RPi only)       |
| Zephyr SDK    | any       | STM32 TSN endpoint (optional)  |

Install on Debian/Ubuntu:

```bash
sudo apt install build-essential cmake libsqlite3-dev libmosquitto-dev \
  python3 python3-pip
python3 -m pip install paho-mqtt
```

If you have no root, build the dependencies locally (`$HOME/local`) and point
CMake/pkg-config at them — see [docs/BUILD.md](docs/BUILD.md).

---

## FAQ / notes

- **Monitor Pause** keeps buffering new frames so pressing Start resumes where you left
  off.
- **The GUI restarts itself** — a watchdog re-checks the web GUI health every 4 s and
  restarts it if it wedges (`/tmp/htsn_mon.log`).
- **LLM latency** — the assistant runs a 1.5 B model on the Pi CPU; expect ~30 s per
  answer. Use short, concrete requests ("raise esp32-cam priority to 6") for the
  action path, and open-ended questions ("how do I configure this from scratch?")
  for the guide path.
 - **OTA integrity** — firmware images carry a CRC32; the device verifies it after
  download and refuses to boot unverified images.
- **CI** (GitHub Actions) builds, tests (incl. ASan/UBSan + cppcheck) and packages on
  every push.
- **`tsn/discovery` vs `tsn/discover`** — a few legacy topics still use the old name;
  both are understood.
- **Wired vs wireless** — the STM32 endpoint and the RPi grandmaster use the wired
  `192.168.1.0/24` (OPC UA + PTP); the ESP32 nodes join your WiFi (MQTT). The GUI
  unifies both; nothing is hard-wired to a single link type.

---

## License

MIT
