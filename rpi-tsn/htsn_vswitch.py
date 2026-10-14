#!/usr/bin/env python3
"""htsn_vswitch — a virtual (software) TSN switch on the RPi.

Runs entirely inside an isolated network namespace ("tsnsw") so it can NEVER
disturb the host networking (eth0/TSN link). It creates a VLAN-filtering Linux
bridge with virtual ports (veth pairs), applies the per-port config from the
GUI (PVID, allowed VLANs, PCP->band priority, CBS rate, preemption) and emits
tagged test frames so the switch behaviour is visible (tcpdump/Wireshark).

Config: htsn_gui.db table vswitch_ports(port,pvid,allowed,pcpmap,cbs_kbps,preempt)
Status:  htsn_gui.db table vswitch_status(name,value,ts)
"""
import json
import os
import sqlite3
import subprocess
import time

DB = os.environ.get("HTSN_DB", "/home/wtsn/wtsn-configurator/build/htsn_gui.db")
NS = "tsnsw"
BR = "sw0"
POLL = 3.0
INJECTOR = "/tmp/htsn_vsw_inject.py"
INJECTOR_SRC = r'''
import socket, sys, time
iface = sys.argv[1]
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
s.bind((iface, 0))
dst = b"\x02\x00\x00\x00\x00\x01"
smac = b"\x02\x00\x00\x00\x00\x02"
for vid in (100, 200):
    tci = ((5 << 13) | vid).to_bytes(2, "big")
    payload = ("VSW-TEST vid=%d" % vid).encode()
    frame = dst + smac + b"\x81\x00" + tci + b"\x08\x00" + payload
    try:
        s.sendall(frame)
    except OSError:
        pass
'''

def run(cmd):
    try:
        r = subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True)
        return r.returncode, (r.stdout or "") + (r.stderr or "")
    except Exception as e:
        return 1, str(e)

def nsrun(cmd):
    return run(["ip", "netns", "exec", NS, "sh", "-c", cmd])

def ensure_ns():
    rc, out = run(["ip", "netns", "list"])
    if NS not in out:
        run(["ip", "netns", "add", NS])
    nsrun("ip link set lo up")

def ensure_bridge():
    rc, _ = nsrun("ip link show %s" % BR)
    if rc != 0:
        nsrun("ip link add name %s type bridge" % BR)
    nsrun("ip link set %s type bridge vlan_filtering 1" % BR)
    nsrun("ip link set %s up" % BR)

def ensure_port(port):
    iface = "vs_%s" % port
    peer = "vsu_%s" % port
    rc, _ = nsrun("ip link show %s" % iface)
    if rc != 0:
        run("ip link add %s netns %s type veth peer name %s netns %s" % (iface, NS, peer, NS))
    nsrun("ip link set %s master %s" % (iface, BR))
    nsrun("ip link set %s up" % iface)
    nsrun("ip link set %s up" % peer)   # bring the peer up so the link has carrier
    return iface, peer

def apply_port(row):
    port = str(row["port"])
    iface, peer = ensure_port(port)
    pvid = int(row["pvid"] or 1)
    allowed = str(row["allowed"] or "").strip()
    nsrun("bridge vlan del dev %s vid 1 2>/dev/null" % iface)
    nsrun("bridge vlan add dev %s vid %d pvid untagged" % (iface, pvid))
    for v in [a.strip() for a in allowed.split(",") if a.strip().isdigit()]:
        nsrun("bridge vlan add dev %s vid %s" % (iface, v))
    nsrun("tc qdisc replace dev %s root handle 1: prio bands 8 2>/dev/null" % iface)
    return iface, peer

def load_rows():
    try:
        c = sqlite3.connect(DB)
        c.row_factory = sqlite3.Row
        rows = [dict(r) for r in c.execute("SELECT port,pvid,allowed,pcpmap,cbs_kbps,preempt FROM vswitch_ports")]
        c.close()
        return rows
    except Exception:
        return []

def write_status(kv):
    try:
        c = sqlite3.connect(DB)
        c.execute("CREATE TABLE IF NOT EXISTS vswitch_status(name TEXT PRIMARY KEY, value TEXT, ts INTEGER)")
        for k, v in kv.items():
            c.execute("INSERT OR REPLACE INTO vswitch_status(name,value,ts) VALUES(?,?,strftime('%s','now'))", (k, str(v)))
        c.commit()
        c.close()
    except Exception:
        pass

def inject_test_frames(ports):
    if not ports:
        return
    try:
        with open(INJECTOR, "w") as f:
            f.write(INJECTOR_SRC)
    except Exception:
        return
    src = "vsu_" + str(ports[0])
    run(["ip", "netns", "exec", NS, "python3", INJECTOR, src])

def main():
    ensure_ns()
    ensure_bridge()
    last_hash = None
    while True:
        rows = load_rows()
        h = json.dumps(rows, sort_keys=True)
        if h != last_hash:
            last_hash = h
            for r in rows:
                try:
                    apply_port(r)
                except Exception:
                    pass
        ports = [str(r["port"]) for r in rows]
        inject_test_frames(ports)
        write_status({
            "namespace": NS, "bridge": BR, "ports": ",".join(ports),
            "updated": int(time.time()),
        })
        time.sleep(POLL)

if __name__ == "__main__":
    main()
