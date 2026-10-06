# Node-RED Port Plan

Plan to port all Python GUI (`htsn_webgui/`) features into Node-RED flows,
making Node-RED the single GUI for the H-TSN control plane.

## Current state

| Phase | Feature | Design | Deployed | Tested |
|-------|---------|--------|----------|--------|
| 0 | TSN config form + live telemetry (27 nodes) | Done | Done | Done |
| 1 | Device management (list, add, ping, domains, versions) | Done | Pending | Pending |
| 2 | TSN config pages (QoS, VLAN, TAS, Preemption, Streams, Timesync, Deploy) | Done | Pending | Pending |
| 3 | Monitoring (events, metrics, sensors, recordings) | Done | Pending | Pending |
| 4 | TSN Endpoint OPC UA commands + Architecture topology | Done | Pending | Pending |
| 5 | Firmware OTA (list, upload, flash) + Camera proxy | Done | Pending | Pending |
| 6 | AI Assistant (LLM chat, decisions) + Settings + Export | Done | Pending | Pending |
| 7 | MQTT backend listeners (ack, status, discover, lwt, sensors, ptp, fx, sonar, recordings) | Done | Pending | Pending |

**All 7 phases designed** (~200 nodes across 7 flow files). Deployment requires:
1. RPi reachable (currently offline)
2. `node-red-contrib-sqlite` installed: `cd ~/htsn-nodered && npm install node-red-contrib-sqlite`
3. Run `./deploy.sh all` to merge + deploy all phases
4. Test each tab in the Node-RED dashboard (`http://<rpi>:1880/`)

**Known issues to fix during deployment:**
- Phase 1: `exec` node for snapshot may need quoting fixes
- Phase 2: TAS GCL entries need a second SQL insert after delete (currently only deletes)
- Phase 2: Stream members need insert after delete (currently only deletes)
- Phase 4: Architecture SVG uses `ui_template` with inline JS — verify rendering
- Phase 5: Firmware upload uses `http in` with `upload:true` — verify multipart handling
- All: SQLite node property names may differ from `node-red-contrib-sqlite` API

## Port phases

### Phase 1 — Device management (highest user value)

Port the **Devices** page. This is the primary operational interface.

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 1.1 | Device list (table with id, name, status, IP, USB, domain, FW, TSN features) | `devices.py:save_devices`, DB `devices` | Inject → HTTP in (or SQLite via `node-red-contrib-sqlite`) → ui_table | ~8 |
| 1.2 | Add device form (id, name, ip, usb, domain) | `save_devices` | ui_form → function (build payload) → SQLite insert/upsert | ~4 |
| 1.3 | Delete device | `save_devices {delete:[id]}` | ui_button → function → SQLite delete + MQTT `tsn/cmd/<id>/reset` | ~4 |
| 1.4 | Ping device | `ping_device` | ui_button → function → HTTP request to cam `/last.jpg` OR MQTT `tsn/cmd/<id>/ping` → delay (wait ack) → ui_text (RTT) | ~8 |
| 1.5 | Assign domain | `assign_domain` | ui_dropdown → function → SQLite update | ~3 |
| 1.6 | Domain CRUD | `save_domain`, `delete_domain` | ui_form + ui_button → SQLite | ~6 |
| 1.7 | Config versions: save/list/diff/rollback | `create_version`, `list_versions`, `diff_versions`, `rollback_version` | ui_button → function (snapshot DB tables to JSON) → SQLite `config_versions`; rollback = DELETE+INSERT replay | ~12 |
| 1.8 | Restore from backup file | `restore_backup` | ui_file → function (parse JSON, replay to DB) | ~4 |

**Dependencies:** `node-red-contrib-sqlite` (or `node-red-node-sqlite`) for DB access.
Node-RED has no built-in SQLite node; this is the key new dependency.

**Estimated effort:** ~2-3 days (49 nodes + DB layer)

---

### Phase 2 — TSN configuration pages (QoS, VLAN, TAS, Preemption, Streams, Timesync)

Port the per-standard configuration tabs. These are the "configure TSN" workflows.

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 2.1 | QoS: save/delete per device | `qos.py` | ui_form (device, prio, TC, bandwidth, latency) → SQLite | ~6 |
| 2.2 | Preemption: save/delete | `qos.py:save_preemption` | ui_form (device, mode, eMAC, pMAC) → SQLite | ~5 |
| 2.3 | VLAN groups: save/delete + members | `vlan.py` | ui_form (group, vlan_id) + ui_checkbox_group (members) → SQLite | ~8 |
| 2.4 | TAS/GCL: save/delete | `tas.py` | ui_form (name, cycle, target, GCL text) → SQLite (parse GCL text to gcl_entries) | ~6 |
| 2.5 | Streams: save/delete/deploy/deploy-all | `streams.py` | ui_form (name, talker, listeners, vlan) → SQLite; deploy → MQTT publish | ~10 |
| 2.6 | Timesync: GM/slave config | `timesync.py` | ui_form (GM select, slave multi-select) → SQLite | ~5 |
| 2.7 | FXMQTT: field server config + live data | `fxmqtt.py` | ui_form (type, node, broker) → SQLite; live: MQTT in `tsn/fx/#` → ui_table | ~8 |
| 2.8 | **Execute settings on controller** (main deploy) | `misc.py:exec_all` | ui_button → function (build per-device JSON snapshot from DB) → MQTT `tsn/cmd/<id>/apply` + OPC UA write for wired endpoint → delay → MQTT in `tsn/ack/#` → ui_text (status) | ~12 |
| 2.9 | Apply wired TSN config to endpoint | `misc.py:apply_wired_tsn` | Already done in Phase 0 (TSN config form) | 0 |

**Estimated effort:** ~3-4 days (60 nodes)

---

### Phase 3 — Monitoring (Monitor, Metrics, Sensors, Recordings)

Port the observability pages.

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 3.1 | Monitor: live event table | `misc.py:clear_events`, `D.events` | MQTT in (multiple topics) → function (parse) → ui_table; clear button | ~8 |
| 3.2 | Metrics: E2E latency + gPTP offset charts | `misc.py:metrics`, `clear_metrics` | Inject (poll) → SQLite (latency_log, timesync_reports) → function (aggregate) → ui_chart | ~10 |
| 3.3 | Sensors: live values + history sparklines | `misc.py:get_history` | MQTT in `tsn/sensors/#` → function → ui_gauge/ui_chart per sensor; history: SQLite `sensor_history` | ~15 |
| 3.4 | Recordings: clip list + playback | `/clip/<id>/<file>`, `/cam/<ip>/replay.mjpeg` | HTTP in (or function listing files) → ui_template (video/img tags) | ~8 |

**Estimated effort:** ~2-3 days (41 nodes)

---

### Phase 4 — TSN Endpoint commands + Architecture

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 4.1 | TSN Endpoint: servo/relay/beep/sonar/reboot commands | `tsn.py:tsn_cmd` | ui_slider/ui_button → OpcUa-Client write `cmd_*` nodes | ~10 |
| 4.2 | Architecture: live topology diagram | `topology.py` | Inject (poll) → SQLite (devices, streams, etc.) → function (build graph) → ui_template (D3/cytoscape or SVG) | ~8 |

**Estimated effort:** ~1-2 days (18 nodes)

---

### Phase 5 — Firmware OTA + Camera proxy

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 5.1 | Firmware: upload/list | `/api/firmware` GET/POST | ui_file → function (CRC32, version parse, file write) → SQLite | ~8 |
| 5.2 | Firmware: flash/OTA per device | `devices.py:start_ota` | ui_button → function → MQTT `tsn/cmd/<id>/ota` | ~4 |
| 5.3 | Camera: live stream proxy | `/cam/<ip>/stream` | ui_template (iframe/img with `/cam/<ip>/stream` URL) — Node-RED can't proxy, keep a small HTTP proxy OR direct from browser to cam | ~4 |
| 5.4 | Camera: ping/last.jpg | `/cam/<ip>/ping`, `/cam/<ip>/last.jpg` | ui_button → HTTP request → ui_text (RTT) / ui_template (img) | ~6 |

**Estimated effort:** ~1-2 days (22 nodes)

Note: Camera proxy (`/cam/<ip>/...`) is hard to replicate in Node-RED.
Options: (a) keep a minimal Python/Node HTTP proxy sidecar, (b) browser talks
directly to the cam IP (CORS may block), (c) use Node-RED's `http in`/`http response`
nodes as a pass-through proxy.

---

### Phase 6 — AI Assistant + Settings

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 6.1 | LLM chat | `misc.py:llm_chat` | ui_form (chat input) → HTTP request to LLM bridge (`:8081/chat`) → function (parse JSON action/guide) → ui_text + optional action execution | ~10 |
| 6.2 | AI Decisions: list/clear | `misc.py:clear_decisions` | Inject → SQLite `ai_decisions` → ui_table; clear button | ~5 |
| 6.3 | Settings: broker config, export/restore | `timesync.py:set_server`, `restore_backup` | ui_form → SQLite `settings`; export: function → `msg.payload = json` → ui_button (download) | ~6 |

**Estimated effort:** ~1 day (21 nodes)

---

### Phase 7 — MQTT backend (invisible infrastructure)

The Python GUI runs a persistent MQTT listener that parses incoming messages and
writes to SQLite (devices, sensors, sonar_sweeps, recordings, timesync_reports,
latency_log, RECENT_ACKS). In Node-RED this becomes:

| # | Feature | Source | Node-RED approach | Est. nodes |
|---|---------|--------|-------------------|------------|
| 7.1 | MQTT in: `tsn/ack/#` | `mqtt_link.py:parse_listener_msg` | mqtt in → function (parse) → SQLite update | ~4 |
| 7.2 | MQTT in: `tsn/status` | same | mqtt in → function → SQLite `devices` update | ~3 |
| 7.3 | MQTT in: `tsn/discover` | same | mqtt in → function → SQLite `devices` upsert | ~3 |
| 7.4 | MQTT in: `tsn/lwt/#` | same | mqtt in → function → SQLite `devices.status=0` | ~3 |
| 7.5 | MQTT in: `tsn/sensors/#` | same | mqtt in → function → SQLite `sensor_history` + global (for live widgets) | ~4 |
| 7.6 | MQTT in: `tsn/ptp` | same | mqtt in → function → SQLite `timesync_reports` | ~3 |
| 7.7 | MQTT in: `tsn/fx/#` | same | mqtt in → function → SQLite `fx_data` + global (for live FX table) | ~4 |
| 7.8 | MQTT in: `tsn/sonar` | same | mqtt in → function → SQLite `sonar_sweeps` + global | ~3 |
| 7.9 | MQTT in: `tsn/cam/recordings` | same | mqtt in → function → SQLite `recordings` | ~3 |

**Estimated effort:** ~1 day (26 nodes)

---

## Total estimate

| Phase | Features | Nodes | Effort |
|-------|----------|-------|--------|
| 0 (done) | TSN config + telemetry | 27 | — |
| 1 | Device management | ~49 | 2-3 days |
| 2 | TSN config pages | ~60 | 3-4 days |
| 3 | Monitoring | ~41 | 2-3 days |
| 4 | Endpoint + Architecture | ~18 | 1-2 days |
| 5 | Firmware + Camera | ~22 | 1-2 days |
| 6 | AI + Settings | ~21 | 1 day |
| 7 | MQTT backend | ~26 | 1 day |
| **Total** | **~50 actions** | **~284** | **11-16 days** |

## Key technical decisions

1. **SQLite access:** Node-RED needs `node-red-contrib-sqlite` (or similar).
   The Python GUI uses a single SQLite DB with 24 tables. All DB access in the
   port goes through this node.

2. **Camera proxy:** The `/cam/<ip>/...` HTTP proxy is the hardest feature to
   replicate. Options:
   - (a) Keep a minimal Python/Node proxy sidecar (50 lines)
   - (b) Browser talks directly to cam IPs (CORS may block)
   - (c) Node-RED `http in` + `http response` as pass-through
   Recommendation: (a) — smallest footprint, easiest to maintain.

3. **LLM bridge:** The LLM bridge (`:8081`) is a separate HTTP service. Node-RED
   just needs an `http request` node pointing at it. No port needed.

4. **WebSocket push:** The Python GUI pushes `{"t":"refresh"}` to connected
   browsers via WebSocket. Node-RED Dashboard handles its own live updates —
   no equivalent needed.

5. **OPC UA commands:** The `tsn_cmd` action (servo/relay/beep/sonar/reboot)
   writes to `cmd_*` OPC UA nodes. Node-RED's OpcUa-Client handles this directly
   (no CLI shell-out needed — unlike the Python GUI which uses `subprocess`).

6. **Flow organization:** Use one Node-RED tab per Python GUI page:
   - Tab "HTSN" (existing) — live telemetry
   - Tab "TSN Config" (existing) — TSN config form
   - Tab "Devices" — device management
   - Tab "QoS & VLAN" — 802.1Q config
   - Tab "TAS & Streams" — 802.1Qbv/Qcc
   - Tab "Monitor" — events + metrics + sensors
   - Tab "TSN Endpoint" — wired endpoint commands
   - Tab "Firmware" — OTA management
   - Tab "AI" — LLM chat + decisions
   - Tab "Backend" — MQTT listeners (invisible)

## Sequencing

Port in order: 1 → 2 → 7 → 3 → 4 → 5 → 6

Rationale:
- Phase 1 (devices) is the primary operational interface — users need it first
- Phase 2 (TSN config) completes the configuration workflow
- Phase 7 (MQTT backend) makes the live data flow work for all later pages
- Phases 3-6 are observability + specialized features, lower urgency

Each phase is independently deployable and testable. The Python GUI stays
running in parallel until the final phase is complete, then `wtsn-webgui.service`
is disabled and removed.
