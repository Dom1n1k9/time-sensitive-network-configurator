"""OPC UA bridge for the wired RPi<->STM32 TSN endpoint.

The C poller (rpi-tsn/tsn_opcua_link) holds one OPC UA client connection to the
CNC server and atomically writes the endpoint's telemetry to a JSON file every
~1 s. This module:
  - reads that file (no Python OPC UA dependency) for /api/data;
  - mirrors the endpoint into the device table so it shows in Devices/topology;
  - exposes send_cmd() which shells out to the one-shot htsn_opcua_cli for writes.

The ESP32 (wireless) side uses MQTT and does not touch this module.
"""
import json
import os
import subprocess
import time

from . import state
from .db import add_event, connect

ENDPOINT_ID = "stm32-tsn-01"


def read_endpoint():
    """Latest endpoint telemetry dict, or None if not (yet) available.

    A snapshot older than OFFLINE_AFTER seconds is still returned but flagged
    online=False so the UI can show the endpoint as stale/offline.
    """
    try:
        with open(state.TSN_OPCUA_OUT, "r", encoding="utf-8") as f:
            d = json.load(f)
    except Exception:
        return None
    if not isinstance(d, dict) or not d.get("ok"):
        return None
    ts = int(d.get("ts") or 0)
    d["online"] = bool(ts) and (int(time.time()) - ts) <= state.OFFLINE_AFTER
    return d


def send_cmd(name, value):
    """Send a command to the endpoint via the one-shot CLI (htsn_opcua_cli write)."""
    cli = state.TSN_OPCUA_CLI
    if not os.path.isfile(cli) or not os.access(cli, os.X_OK):
        return {"ok": False, "msg": "htsn_opcua_cli not found - build rpi-tsn/ on the RPi"}
    env = dict(os.environ)
    env["HTSN_OPCUA_URL"] = state.TSN_OPCUA_URL
    try:
        p = subprocess.run([cli, "write", str(name), str(value)],
                           capture_output=True, text=True, timeout=20, env=env)
    except Exception as ex:
        return {"ok": False, "msg": "command failed: %s" % ex}
    out = (p.stdout or "").strip().splitlines()
    out = out[-1] if out else ""
    ok = p.returncode == 0 and out.startswith("ok")
    add_event("tsn", ENDPOINT_ID, "cmd %s=%s %s" % (name, value, "OK" if ok else "FAIL"))
    state.WS_NOTIFY.set()
    return {"ok": ok, "msg": out or ("ok" if ok else "command failed")}


def endpoint_listener_loop():
    """Mirror the endpoint into the device table and flag online/offline changes."""
    last = None
    while not state.LISTENER_STOP.is_set():
        ep = read_endpoint()
        online = bool(ep and ep.get("online"))
        now = int(time.time())
        con = connect()
        try:
            con.execute("INSERT OR IGNORE INTO devices (id, name, kind) VALUES (?,?,?)",
                        (ENDPOINT_ID, "STM32 TSN endpoint", 6))
            con.execute("UPDATE devices SET status=?, last_seen=? WHERE id=?",
                        (0 if online else 1, now if online else 0, ENDPOINT_ID))
            con.commit()
        except Exception:
            pass
        finally:
            con.close()
        state.WS_NOTIFY.set()
        if online != last:
            add_event("tsn", ENDPOINT_ID,
                      "endpoint online (OPC UA)" if online else "endpoint offline")
            last = online
        state.LISTENER_STOP.wait(1.5)
