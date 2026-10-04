"""Shared mutable state for the web GUI process (guarded by the locks here)."""
import os
import threading
from collections import deque

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_DIR = os.path.join(BASE, "build")
os.makedirs(DB_DIR, exist_ok=True)
DB_REAL = os.environ.get("HTSN_DB", os.path.join(DB_DIR, "htsn_gui.db"))
DB_SIM = os.path.join(DB_DIR, "htsn_sim.db")
FW_DIR = os.environ.get("HTSN_FW_DIR", os.path.join(DB_DIR, "fw"))
os.makedirs(FW_DIR, exist_ok=True)
PORT = int(os.environ.get("HTSN_PORT", "8000"))

TABLES = ["devices", "device_tsn_features", "qos_configs", "vlan_groups",
          "vlan_members", "tas_schedules", "gcl_entries", "timesync_status",
          "sensors", "preemption_configs", "tsn_streams", "tsn_stream_members",
          "settings", "domains", "config_versions", "recordings", "sonar_sweeps",
          "ai_decisions"]

# The GUI is real-mode only. There is no simulation mode: telemetry comes from
# real nodes (ESP32 over MQTT, the STM32 TSN endpoint over OPC UA). The "mode"
# field is kept as "real" for API/back-compat.
MODE = {"mode": "real"}

# --- Wired STM32 TSN endpoint (OPC UA) bridge ---
# The C poller (rpi-tsn/tsn_opcua_link) writes endpoint telemetry to a JSON
# file; the GUI reads it (no Python OPC UA dependency). Commands shell out to
# the one-shot rpi-tsn/htsn_opcua_cli.
TSN_OPCUA_OUT = os.environ.get("HTSN_OPCUA_OUT", "/tmp/htsn_tsn_opcua.json")
TSN_OPCUA_URL = os.environ.get("HTSN_OPCUA_URL", "opc.tcp://127.0.0.1:4840")
TSN_OPCUA_CLI = os.environ.get("HTSN_OPCUA_CLI",
                               os.path.join(BASE, "rpi-tsn", "htsn_opcua_cli"))
EVENTS = deque(maxlen=400)
EVENT_LOCK = threading.Lock()
LISTENER_STOP = threading.Event()
WS_NOTIFY = threading.Event()   # set whenever state changes; the WS hub picks it up
RECENT_ACKS = {}
ACK_LOCK = threading.Lock()
ACK_TTL = 15.0   # seconds an ack counts as "fresh" before it is pruned
# Last known MQTT broker reachability, maintained by the background listener loop
# so request handlers never block on a TCP connect just to answer /api/data.
BROKER = {"ok": False, "checked_at": 0.0}
BROKER_LOCK = threading.Lock()
PING_OUT = {}    # {device_id: sent_epoch_seconds} for RTT latency measurement
MQTT_LOCK = threading.Lock()   # guards the cached REAL_MQTT client in mqtt_link
SIM_USER_DEVICES = set()
SIM_USER_DEVICES_LOCK = threading.Lock()
OFFLINE_AFTER = 20
