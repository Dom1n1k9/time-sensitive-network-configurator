#!/usr/bin/env python3
"""htsn_ptp_slave — PTPv2 slave helper on the RPi for Wireshark demos.

Sends Delay_Req once per second so the ptp4l grandmaster answers with
Delay_Resp, and replies to the grandmaster's P_Delay_Req with P_Delay_Resp.
Together with the GM's Sync / Follow_Up / Announce this puts (almost) all
PTPv2 message types on the wire at the same time.

Note: Delay_Resp is sent by the GM back to this host's own IP, so it is
delivered over loopback and does NOT appear in a wire capture. To see
Delay_Resp on the wire a second physical host (e.g. the STM32) must act as
PTP slave.
"""
import socket
import sqlite3
import struct
import time

IFACE = "eth0"
MC_GROUP = "224.0.1.129"
PTP_PORT = 319
DB = "/home/wtsn/wtsn-configurator/build/htsn_gui.db"

# Unique 48-bit clockIdentity — must differ from the ptp4l GM identity
# derived from eth0's MAC.
CLOCK_ID = b"\x98\xfe\x54\xff\xfe\x77"
PORT_NUM = 1
PTP_EPOCH_OFFSET = 2208988800  # seconds between 1900 (PTP) and 1970 (unix)


def ptp_now():
    ns = time.time_ns()
    sec = ns // 10**9 + PTP_EPOCH_OFFSET
    return sec, ns % 10**9


def current_cfg():
    """Return (domain_number, delay_req_interval_ms) from the active row."""
    try:
        c = sqlite3.connect(DB, timeout=2)
        r = c.execute(
            "SELECT domain_number, delay_req_interval_ms "
            "FROM sync_configs ORDER BY id LIMIT 1"
        ).fetchone()
        c.close()
        if r:
            dom = int(r[0] or 50) & 0x7F
            try:
                dr = max(100, min(600000, int(r[1] or 1000)))
            except (TypeError, ValueError):
                dr = 1000
            return dom, dr
    except Exception:
        pass
    return 50, 1000


def build(msgtype, domain, seq, flags=0, origin=None):
    sec, ns = origin if origin else ptp_now()
    hdr = struct.pack("!BBBBHH", 0, 2, msgtype, flags, domain, 0)
    hdr += CLOCK_ID
    hdr += struct.pack("!HQ", 0, PORT_NUM)      # portIdentity
    hdr += struct.pack("!HII", sec >> 32, sec & 0xFFFFFFFF, ns)
    hdr += struct.pack("!HQ", 0, PORT_NUM)      # originPortIdentity
    hdr += struct.pack("!HBB", seq & 0xFFFF, 0, 0)
    hdr += b"\x00\x00\x00\x00"                  # zero TLV (end)
    return hdr


def parse_time(payload):
    sec = (struct.unpack("!H", payload[24:26])[0] << 32) | \
          struct.unpack("!I", payload[26:30])[0]
    return sec, struct.unpack("!I", payload[30:34])[0]


def main():
    iface_ip = socket.gethostbyname(IFACE)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except (AttributeError, OSError):
        pass
    s.bind(("0.0.0.0", PTP_PORT))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                 socket.inet_aton(iface_ip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 socket.inet_aton(MC_GROUP) + socket.inet_aton(iface_ip))
    s.setblocking(False)
    seq = 1
    print("htsn_ptp_slave: up on %s (%s)" % (IFACE, iface_ip), flush=True)
    while True:
        dom, dr_ms = current_cfg()
        period = max(0.1, dr_ms / 1000.0)
        t0 = time.time()
        try:
            s.sendto(build(2, dom, seq), (MC_GROUP, PTP_PORT))
        except OSError as e:
            print("slave send err: %s" % e, flush=True)
        seq += 1
        while time.time() - t0 < period:
            try:
                data, addr = s.recvfrom(4096)
            except (BlockingIOError, TimeoutError):
                time.sleep(0.05)
                continue
            except OSError:
                time.sleep(0.05)
                continue
            if len(data) < 48 or data[1] != 2:
                continue
            if data[2] == 3:  # P_Delay_Req from the GM -> answer multicast
                try:
                    s.sendto(build(4, int.from_bytes(data[4:6], "big"),
                                   seq, flags=0x01,
                                   origin=parse_time(data)),
                             (MC_GROUP, PTP_PORT))
                    seq += 1
                except OSError as e:
                    print("slave pdelay err: %s" % e, flush=True)


if __name__ == "__main__":
    main()
