#!/usr/bin/env python3
"""htsn_ptp_sync — apply SyncConfigs 802.1AS-Rev to the RPi grandmaster (ptp4l).

The SyncConfigs GUI card configures the endpoint (STM32 slave) via OPC UA, but
the *Grandmaster* here is the RPi running ptp4l. This root helper reads the
active SyncConfigs row from the DB and (re)writes /etc/htsn/ptpd.conf with the
matching ptp4l options, then restarts htsn-ptp when they change — so the
configured 802.1AS-Rev parameters are what actually appear in the PTPv2
Announce/Sync frames on the wire (Wireshark).

sync_configs column -> ptp4l option mapping:
  domain_number          -> domainNumber
  gm_priority            -> priority1 (and priority2)
  clock_class            -> clockClass   (GM clock quality, AS-Rev: 6)
  clock_accuracy         -> clockAccuracy (AS-Rev: 192 = 0xC0)
  sync_period_ms         -> logSyncInterval = round(log2(period_seconds))
  announce_interval_ms   -> logAnnounceInterval
  delay_req_interval_ms  -> logMinDelayReqInterval
  peer_delay / P2P       -> delay_mechanism P2P|E2E (linuxptp >= 4.x)
                           peerDelayReqSupported true (linuxptp 3.x)
  (always)               -> asCapable true (genuine 802.1AS on the wire)
"""
import math
import os
import re
import sqlite3
import subprocess
import time

DB = os.environ.get("HTSN_DB", "/home/wtsn/wtsn-configurator/build/htsn_gui.db")
CONF = os.environ.get("HTSN_PTPD_CONF", "/etc/htsn/ptpd.conf")
SVC = os.environ.get("HTSN_PTP_SVC", "htsn-ptp")
POLL = 5.0


def ptp4l_major():
    """Major version of the installed ptp4l (3 or 4+)."""
    try:
        out = subprocess.run(["/usr/sbin/ptp4l", "-v"],
                             capture_output=True, text=True, timeout=5)
        m = re.search(r"(\d+)\.(\d+)", out.stdout + out.stderr)
        if m:
            return int(m.group(1))
    except Exception:
        pass
    return 3


def log2ms(ms, default):
    try:
        ms = int(ms)
    except (TypeError, ValueError):
        ms = default
    if ms < 1:
        ms = default
    return max(-7, min(7, int(round(math.log2(ms / 1000.0)))))


def build_conf():
    try:
        c = sqlite3.connect(DB)
        c.row_factory = sqlite3.Row
        r = c.execute(
            "SELECT domain_number, gm_priority, sync_period_ms, "
            "announce_interval_ms, delay_req_interval_ms, "
            "peer_delay, delay_mechanism, clock_class, clock_accuracy "
            "FROM sync_configs ORDER BY id LIMIT 1").fetchone()
        c.close()
    except Exception:
        return None
    if not r:
        return None
    dom = int(r["domain_number"] or 0) & 0x7F
    p1 = int(r["gm_priority"] if r["gm_priority"] is not None else 128) & 0xFF
    cc = int(r["clock_class"] if r["clock_class"] is not None else 6)
    cc = max(5, min(254, cc))
    ca = int(r["clock_accuracy"] if r["clock_accuracy"] is not None else 192) & 0xFF
    lsi = log2ms(r["sync_period_ms"], 1000)
    lai = log2ms(r["announce_interval_ms"], 2000)
    ldr = log2ms(r["delay_req_interval_ms"], 1000)
    p2p = (int(r["peer_delay"] or 0) == 1) or \
        (str(r["delay_mechanism"] or "").strip().upper() == "P2P")
    # delay_mechanism is accepted by both linuxptp 3.x and 4.x
    conf = ("[global]\n"
            "domainNumber %d\n"
            "priority1 %d\n"
            "priority2 %d\n"
            "clockClass %d\n"
            "clockAccuracy %d\n"
            "asCapable true\n") % (dom, p1, p1, cc, ca)
    conf += "delay_mechanism %s\n" % ("P2P" if p2p else "E2E")
    conf += ("logSyncInterval %d\n"
             "logAnnounceInterval %d\n"
             "logMinDelayReqInterval %d\n") % (lsi, lai, ldr)
    return conf


def main():
    last = None
    while True:
        conf = build_conf()
        if conf is not None and conf != last:
            try:
                os.makedirs(os.path.dirname(CONF), exist_ok=True)
                with open(CONF, "w") as f:
                    f.write(conf)
                last = conf
                subprocess.run(["systemctl", "restart", SVC],
                               capture_output=True)
                print("ptpd.conf updated + %s restarted:\n%s" % (SVC, conf),
                      flush=True)
            except Exception as e:
                print("htsn_ptp_sync error: %s" % e, flush=True)
        time.sleep(POLL)


if __name__ == "__main__":
    main()
