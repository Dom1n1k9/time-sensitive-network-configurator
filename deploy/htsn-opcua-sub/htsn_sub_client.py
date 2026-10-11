#!/usr/bin/env python3
"""htsn-opcua-sub — OPC UA subscription client.

Keeps a live OPC UA session subscription against the local cnc_opcua server so a
continuous Binary-Protocol publish exchange (PublishRequest -> PublishResponse)
flows on the loopback interface (lo:4840). This makes the OPC UA session
data-plane (subscribe/publish, not just Read/Write) visible in Wireshark.

Runs as the systemd unit htsn-opcua-sub.service (User=wtsn).
"""
import asyncio
import logging
import os

from asyncua import Client
from asyncua import ua

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("htsn-opcua-sub")

URL = os.environ.get("HTSN_OPCUA_URL", "opc.tcp://127.0.0.1:4840")
INTERVAL_MS = 200        # publishing interval
KEEPALIVE_COUNT = 10     # keepalive publish every ~2s even when no data changes
LIFETIME_COUNT = 3000
MAX_PER_PUBLISH = 100
# Endpoint-written telemetry nodes; any change triggers a publish. last_seen
# updates each telemetry frame, so a live endpoint yields a fast publish stream.
NODES = [
    "ns=1;i=14",  # last_seen
    "ns=1;i=12",  # ptp_state
    "ns=1;i=3",   # servo_angle
    "ns=1;i=2",   # sonar_sweep_id
]


def params():
    p = ua.CreateSubscriptionParameters()
    p.RequestedPublishingInterval = INTERVAL_MS
    p.RequestedLifetimeCount = LIFETIME_COUNT
    p.RequestedMaxKeepAliveCount = KEEPALIVE_COUNT
    p.MaxNotificationsPerPublish = MAX_PER_PUBLISH
    p.PublishingEnabled = True
    p.Priority = 0
    return p


async def run_once():
    client = Client(URL)
    await client.connect()
    try:
        sub = await client.create_subscription(params())
        async with sub:
            for nid in NODES:
                await sub.subscribe_data_change(client.get_node(nid))
            log.info("subscription up: interval=%dms keepalive=%dx -> PublishReq/Resp on lo:4840",
                     INTERVAL_MS, KEEPALIVE_COUNT)
            n = 0
            async for _ in sub:
                n += 1
                if n % 200 == 1:
                    log.info("notification batches: %d", n)
    finally:
        await client.disconnect()


async def main():
    while True:
        try:
            await run_once()
        except Exception as e:  # noqa: BLE001 - keep the service alive
            log.warning("subscription down (%s: %s); reconnect in 3s", type(e).__name__, e)
            await asyncio.sleep(3)


if __name__ == "__main__":
    asyncio.run(main())
