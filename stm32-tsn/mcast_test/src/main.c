/* Minimal isolated multicast RX test.
 *
 * One UDP socket bound to 319, joined to 224.0.1.129, nothing else. Reports
 * the receive count + IPv4/UDP stats every second over UDP to the RPi at
 * 192.168.1.10:4001 (the serial console is dead on this board).
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/ethernet.h>

#include <string.h>
#include <stdio.h>

#define MCAST 0xE0000181u

int main(void)
{
	struct net_if *iface = NULL;

	for (int i = 0; i < 100 && iface == NULL; i++) {
		iface = net_if_get_default();
		if (iface == NULL) {
			k_sleep(K_MSEC(100));
		}
	}
	if (iface == NULL) {
		return 0;
	}

	/* Mimic eth_proto.c: a raw AF_PACKET (VLAN) socket bound to the iface
	 * with our source MAC. */
	int raw = zsock_socket(NET_AF_PACKET, NET_SOCK_DGRAM,
	                       net_htons(NET_ETH_PTYPE_VLAN));

	if (raw >= 0) {
		struct net_sockaddr_ll ll = {0};
		struct net_linkaddr *la = net_if_get_link_addr(iface);

		ll.sll_family = NET_AF_PACKET;
		ll.sll_ifindex = net_if_get_by_iface(iface);
		ll.sll_halen = 6;
		if (la != NULL && la->len == 6) {
			memcpy(ll.sll_addr, la->addr, 6);
		}
		if (zsock_bind(raw, (const struct net_sockaddr *)&ll, sizeof(ll)) < 0) {
			zsock_close(raw);
			raw = -1;
		}
	}

	int s = zsock_socket(AF_INET, SOCK_DGRAM, 0);
	int jr = -999;
	if (s >= 0) {
		struct sockaddr_in b;

		memset(&b, 0, sizeof(b));
		b.sin_family = AF_INET;
		b.sin_port = htons(319);
		b.sin_addr.s_addr = htonl(INADDR_ANY);
		zsock_bind(s, (struct sockaddr *)&b, sizeof(b));

		struct net_ip_mreqn m;

		memset(&m, 0, sizeof(m));
		m.imr_multiaddr.s_addr = htonl(MCAST);
		m.imr_address.s_addr = htonl(INADDR_ANY);
		m.imr_ifindex = 0;
		jr = zsock_setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m));
	}

	int r = zsock_socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dst;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = htons(4001);
	net_addr_pton(AF_INET, "192.168.1.10", &dst.sin_addr);

	int count = 0;
	uint8_t buf[128];
	char msg[160];

	for (;;) {
		struct sockaddr_in src;
		socklen_t sl;
		int got = 0;

		for (;;) {
			sl = sizeof(src);
			int n = zsock_recvfrom(s, buf, sizeof(buf), ZSOCK_MSG_DONTWAIT,
			                       (struct sockaddr *)&src, &sl);

			if (n <= 0) {
				break;
			}
			count++;
			got++;
		}

		struct net_if *nd = net_if_get_default();
		struct net_in_addr md;
		struct net_if_mcast_addr *ma;
		unsigned long ir = 0, id = 0, ur = 0, ud = 0;

		memset(&md, 0, sizeof(md));
		md.s_addr = htonl(MCAST);
		ma = (nd != NULL) ? net_if_ipv4_maddr_lookup(&md, &nd) : NULL;
		if (nd != NULL) {
			ir = (unsigned long)nd->stats.ipv4.recv;
			id = (unsigned long)nd->stats.ipv4.drop;
			ur = (unsigned long)nd->stats.udp.recv;
			ud = (unsigned long)nd->stats.udp.drop;
		}

		snprintf(msg, sizeof(msg),
		         "MCTEST jr=%d join=%d got=%d count=%d ip4rx=%lu ip4drop=%lu udprx=%lu udpdrop=%lu\n",
		         jr,
		         (ma != NULL && net_if_ipv4_maddr_is_joined(ma)) ? 1 : 0,
		         got, count, ir, id, ur, ud);

		if (r >= 0) {
			zsock_sendto(r, msg, strlen(msg), 0,
			             (struct sockaddr *)&dst, sizeof(dst));
		}

		k_sleep(K_MSEC(1000));
	}

	return 0;
}
