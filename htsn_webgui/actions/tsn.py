"""Commands to the wired STM32 TSN endpoint (OPC UA).

The GUI sends {node: "cmd_*", value: ...}; we shell out to the one-shot
htsn_opcua_cli which writes the command node on the CNC server. The endpoint
(STM32, an OPC UA client) monitors those cmd_* nodes and acts.
"""
from ..opcua_link import send_cmd

ALLOWED = ("cmd_servo_angle", "cmd_relay_on", "cmd_beep_ms",
           "cmd_sonar_trigger", "cmd_reboot")


def _tsn_cmd(con, body):
    node = str(body.get("node") or "")
    if node not in ALLOWED:
        return {"ok": False, "msg": "unknown endpoint command '%s'" % node}
    value = body.get("value")
    if value is None:
        return {"ok": False, "msg": "missing value"}
    return send_cmd(node, value)


HANDLERS = {"tsn_cmd": _tsn_cmd}
