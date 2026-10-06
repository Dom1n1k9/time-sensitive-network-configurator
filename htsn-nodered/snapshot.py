#!/usr/bin/env python3
"""Dump all HTSN config tables as JSON. Called by Node-RED exec node."""
import json, sqlite3, sys

DB = "/home/wtsn/wtsn-configurator/build/htsn_gui.db"
TABLES = ["devices","qos_configs","preemption_configs","vlan_groups",
          "vlan_members","tas_schedules","gcl_entries","timesync_status",
          "tsn_streams","tsn_stream_members","settings"]

def main():
    con = sqlite3.connect(DB, timeout=5)
    con.row_factory = sqlite3.Row
    out = {}
    for t in TABLES:
        try:
            out[t] = [dict(r) for r in con.execute("SELECT * FROM " + t)]
        except Exception:
            out[t] = []
    con.close()
    print(json.dumps(out, sort_keys=True))

if __name__ == "__main__":
    main()
