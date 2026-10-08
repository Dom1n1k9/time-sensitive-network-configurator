/* htsn-tsn transport (htsn_net.c impl): deterministic UDP to the RPi/CNC.
 *
 * Uses the Zephyr **POSIX socket** API (BSD-style, thread-safe) rather than lwIP
 * or the raw net API, so the publish functions can safely sendto() from any
 * thread while the command thread blocks in recvfrom(). The endpoint binds one
 * socket on STM32_CMD_PORT (4000) for incoming commands and sends telemetry to
 * the CNC's CNC_TELEM_PORT (4001). See htsn_frame.h for the wire format.
 */
#include "htsn_net.h"
#include "htsn_frame.h"
#include "htsn_config.h"
#include "htsn_port.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_vlan.h>
#include <zephyr/net/net_if.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/netinet/in.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int  g_sock = -1;
static struct sockaddr_in g_cnc;   /* CNC destination (telemetry out) */

/* ---- 802.1Q egress tagging (raw AF_PACKET path) ----
 * When a TSN config with vlan_id>0 is applied, telemetry goes out as an
 * 802.1Q-tagged frame: Zephyr builds the 14-byte Ethernet header (type 0x8100)
 * and we supply [TCI][0x0800][IPv4][UDP][htsn frame]. This makes the configured
 * VLAN ID + PCP physically visible on the wire (e.g. in Wireshark). */
static int      g_raw_sock = -1;
static int      g_eth_if   = 0;
static uint8_t  g_src_mac[6];
static uint8_t  g_dst_mac[6];
static uint16_t g_vlan_id  = 0;    /* 0 = untagged (normal UDP socket) */
static uint8_t  g_pcp      = 0;
static uint8_t  g_preempt  = 0;    /* 802.3br: >0 = emit a preemption demo */

static int mac_parse(const char *s, uint8_t *out)
{
	unsigned a, b, c, d, e, f;
	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f) != 6)
		return -1;
	out[0] = (uint8_t)a; out[1] = (uint8_t)b; out[2] = (uint8_t)c;
	out[3] = (uint8_t)d; out[4] = (uint8_t)e; out[5] = (uint8_t)f;
	return 0;
}

/* IPv4 header checksum (ones' complement of 16-bit sums). */
static uint16_t ipv4_hdr_sum(const uint8_t *h, size_t len)
{
	uint32_t s = 0;
	for (size_t i = 0; i + 1 < len; i += 2)
		s += (uint16_t)((h[i] << 8) | h[i + 1]);
	if (len & 1)
		s += (uint16_t)(h[len - 1] << 8);
	while (s >> 16) s = (s & 0xffff) + (s >> 16);
	return (uint16_t)(~s & 0xffff);
}

/* Build [TCI][0x0800][IPv4][UDP][frame] into `out`; returns byte length. */
static int build_tagged_pcp(uint8_t *out, const uint8_t *frame, uint16_t flen, uint8_t pcp)
{
	uint16_t udp_len = 8 + flen;
	uint16_t ip_len  = 20 + udp_len;

	/* 802.1Q TCI (2B, big-endian) + EtherType IPv4 (2B). */
	uint16_t tci = ((uint16_t)(pcp & 0x07) << 13) | (g_vlan_id & 0x0fff);
	out[0] = (uint8_t)(tci >> 8);
	out[1] = (uint8_t)(tci & 0xff);
	out[2] = 0x08; out[3] = 0x00;

	/* IPv4 header (20B). */
	uint8_t *ip = &out[4];
	memset(ip, 0, 20);
	ip[0] = 0x45;                                   /* v4, IHL=5 */
	ip[2] = (uint8_t)(ip_len >> 8);                 /* total len (big-endian) */
	ip[3] = (uint8_t)(ip_len & 0xff);
	ip[8] = 64;                                     /* TTL */
	ip[9] = 17;                                     /* UDP */
	uint8_t s4[4], d4[4];
	unsigned a, b, c, e;
	sscanf("192.168.1.20", "%u.%u.%u.%u", &a, &b, &c, &e);
	s4[0]=(uint8_t)a; s4[1]=(uint8_t)b; s4[2]=(uint8_t)c; s4[3]=(uint8_t)e;
	sscanf(CNC_TAG_IP, "%u.%u.%u.%u", &a, &b, &c, &e);
	d4[0]=(uint8_t)a; d4[1]=(uint8_t)b; d4[2]=(uint8_t)c; d4[3]=(uint8_t)e;
	memcpy(&ip[12], s4, 4);
	memcpy(&ip[16], d4, 4);
	uint16_t cs = ipv4_hdr_sum(ip, 20);
	ip[10] = (uint8_t)(cs >> 8); ip[11] = (uint8_t)(cs & 0xff);

	/* UDP header (8B) — checksum 0 (valid over IPv4). */
	uint8_t *udp = &out[24];
	memset(udp, 0, 8);
	udp[0] = (uint8_t)(STM32_CMD_PORT >> 8); udp[1] = (uint8_t)(STM32_CMD_PORT & 0xff);
	udp[2] = (uint8_t)(CNC_TELEM_PORT >> 8); udp[3] = (uint8_t)(CNC_TELEM_PORT & 0xff);
	udp[4] = (uint8_t)(udp_len >> 8);         udp[5] = (uint8_t)(udp_len & 0xff);

	memcpy(&out[32], frame, flen);
	return 32 + flen;
}

static int build_tagged(uint8_t *out, const uint8_t *frame, uint16_t flen)
{
	return build_tagged_pcp(out, frame, flen, g_pcp);
}

/* ---- 802.3br frame-preemption demo (simulated; the STM32F7 MAC has no HW
 * preemption). Emits a 3-frame sequence on the tagged egress so the preemption
 * is visible on the wire / in Wireshark:
 *   1) a preemptible (low-PCP) frame aborted mid-flight, marked with the 802.3br
 *      Last Fragment Marker (a trailing 0xFFFF),
 *   2) a preempting (PCP 7) urgent frame that interrupted it,
 *   3) the full preemptible frame retransmitted now that the line is clear.
 * Runs from the system workqueue (thread context) while g_preempt && vlan are on. */
static void send_preempt_seq(void)
{
	if (g_raw_sock < 0 || g_vlan_id == 0) return;

	static uint8_t fbuf[48], full[80], part[80], ubuf[80];
	static uint32_t seq = 0;
	seq++;

	/* Frame 1/3: the "preemptible" low-priority frame. The payload is a readable
	 * marker so the preemption is unambiguous in Wireshark's data field. */
	char pl[24];
	int plen = snprintf(pl, sizeof(pl), "PREEMPTIBLE %04u", (unsigned)(seq & 0xffff));
	int flen = htsn_frame_pack(fbuf, HTSN_TYPE_TELEM, HTSN_KIND_TELEM_PREEMPT,
	                    (const uint8_t *)pl, (uint16_t)plen);
	if (flen <= 0) return;
	int full_len = build_tagged_pcp(full, fbuf, (uint16_t)flen, g_pcp);
	if (full_len <= 0) return;

	struct net_sockaddr_ll ll = {0};
	ll.sll_family   = NET_AF_PACKET;
	ll.sll_protocol = net_htons(NET_ETH_PTYPE_VLAN);
	ll.sll_ifindex  = g_eth_if;
	ll.sll_halen    = 6;
	memcpy(ll.sll_addr, g_dst_mac, 6);

	/* 1) Aborted partial frame: cut off part of the HTSN payload, then append the
	 *    2-byte 0xFFFF Last Fragment Marker (this is what a receiver uses to tell
	 *    the frame was preempted and must be discarded). */
	int cut = full_len - 8;
	if (cut < 32) cut = 32;
	if (cut + 2 > (int)sizeof(part)) cut = (int)sizeof(part) - 2;
	memcpy(part, full, cut);
	part[cut] = 0xFF; part[cut + 1] = 0xFF;   /* 802.3br LFM */
	zsock_sendto(g_raw_sock, part, (size_t)(cut + 2), 0,
	             (const struct net_sockaddr *)&ll, sizeof(ll));

	/* 2) The preempting urgent frame at the highest priority (PCP 7), readable. */
	int uflen = htsn_frame_pack(fbuf, HTSN_TYPE_TELEM, HTSN_KIND_TELEM_PREEMPT_URG,
	                    (const uint8_t *)"PREEMPTING!", 11);
	if (uflen > 0) {
		int ulen = build_tagged_pcp(ubuf, fbuf, (uint16_t)uflen, 7);
		if (ulen > 0)
			zsock_sendto(g_raw_sock, ubuf, (size_t)ulen, 0,
			             (const struct net_sockaddr *)&ll, sizeof(ll));
	}

	/* 3) Retransmit the full preemptible frame now that the line is clear. */
	zsock_sendto(g_raw_sock, full, (size_t)full_len, 0,
	             (const struct net_sockaddr *)&ll, sizeof(ll));
}

static struct k_work_delayable g_preempt_work;
static void preempt_work_fn(struct k_work *w)
{
	(void)w;
	if (g_preempt && g_vlan_id > 0)
		send_preempt_seq();
	k_work_reschedule(&g_preempt_work, K_MSEC(2000));
}

/* ---- CRC16-CCITT (init 0xFFFF, poly 0x1021, no reflect, no xorout) ---- */
uint16_t htsn_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}

/* ---- frame codec (HDR = 5: magic + type + kind + len(2, LE)) ---- */
int htsn_frame_pack(uint8_t *out, uint8_t type, uint8_t kind,
                    const uint8_t *payload, uint16_t len)
{
	if (len > HTSN_FRAME_MAXPLEN) return -1;
	size_t n = HTSN_FRAME_HDR + len;
	out[0] = HTSN_FRAME_MAGIC;
	out[1] = type;
	out[2] = kind;
	out[3] = (uint8_t)(len & 0xFF);
	out[4] = (uint8_t)((len >> 8) & 0xFF);
	if (len && payload) memcpy(&out[HTSN_FRAME_HDR], payload, len);
	uint16_t crc = htsn_crc16(out, n);
	out[n]     = (uint8_t)(crc & 0xFF);
	out[n + 1] = (uint8_t)(crc >> 8);
	return (int)(n + HTSN_FRAME_CRC);
}

int htsn_frame_unpack(const uint8_t *in, size_t in_len, uint8_t *type, uint8_t *kind,
                      uint8_t *payload, size_t *payload_len)
{
	if (in_len < HTSN_FRAME_HDR + HTSN_FRAME_CRC) return -1;
	if (in[0] != HTSN_FRAME_MAGIC) return -1;
	uint16_t len = (uint16_t)in[3] | ((uint16_t)in[4] << 8);
	if (len > HTSN_FRAME_MAXPLEN) return -1;
	if (in_len < (size_t)HTSN_FRAME_HDR + len + HTSN_FRAME_CRC) return -1;
	uint16_t crc = htsn_crc16(in, HTSN_FRAME_HDR + len);
	uint16_t got = (uint16_t)in[HTSN_FRAME_HDR + len] | ((uint16_t)in[HTSN_FRAME_HDR + len + 1] << 8);
	if (crc != got) return -1;
	*type = in[1];
	*kind = in[2];
	*payload_len = len;
	if (len && payload) memcpy(payload, &in[HTSN_FRAME_HDR], len);
	return 0;
}

/* ---- small helpers ---- */
static void set_cnc_addr(struct sockaddr_in *d)
{
	unsigned int a = 0, b = 0, c = 0, e = 0;
	sscanf(CNC_IP, "%u.%u.%u.%u", &a, &b, &c, &e);
	d->sin_family = AF_INET;
	d->sin_port = htons(CNC_TELEM_PORT);
	d->sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | e);
}

static void send_frame_tagged(uint8_t *frame, int flen)
{
	uint8_t buf[HTSN_FRAME_MAX + 32];
	int blen = build_tagged(buf, frame, (uint16_t)flen);
	if (blen <= 0 || g_raw_sock < 0) return;
	struct net_sockaddr_ll ll = {0};
	ll.sll_family   = NET_AF_PACKET;
	ll.sll_protocol = net_htons(NET_ETH_PTYPE_VLAN);
	ll.sll_ifindex  = g_eth_if;
	ll.sll_halen    = 6;
	memcpy(ll.sll_addr, g_dst_mac, 6);
	zsock_sendto(g_raw_sock, buf, (size_t)blen, 0,
	              (const struct net_sockaddr *)&ll, sizeof(ll));
}

static void send_frame(uint8_t kind, const uint8_t *payload, uint16_t len)
{
	uint8_t frame[HTSN_FRAME_MAX];
	int flen = htsn_frame_pack(frame, HTSN_TYPE_TELEM, kind, payload, len);
	if (flen <= 0) return;
	if (g_vlan_id > 0 && g_raw_sock >= 0) {
		send_frame_tagged(frame, flen);        /* 802.1Q-tagged egress */
	} else if (g_sock >= 0) {
		sendto(g_sock, frame, (size_t)flen, 0, (struct sockaddr *)&g_cnc, sizeof(g_cnc));
	}
}

/* ---- publish (thread-safe: each call builds + sends one datagram) ---- */
void htsn_net_publish_sweep(int32_t id, const int16_t *sweep, int n)
{
	uint8_t pl[HTSN_FRAME_MAXPLEN];
	int o = 0;
	pl[o++] = (uint8_t)(id & 0xFF); pl[o++] = (uint8_t)((id >> 8) & 0xFF);
	pl[o++] = (uint8_t)((id >> 16) & 0xFF); pl[o++] = (uint8_t)((id >> 24) & 0xFF);
	for (int i = 0; i < n && o + 1 < HTSN_FRAME_MAXPLEN; i++) {
		pl[o++] = (uint8_t)(sweep[i] & 0xFF);
		pl[o++] = (uint8_t)((sweep[i] >> 8) & 0xFF);
	}
	send_frame(HTSN_KIND_TELEM_SWEEP, pl, (uint16_t)o);
}

void htsn_net_publish_buttons(int k1, int k2, int k3, int k4)
{
	uint8_t mask = (uint8_t)((k1 & 1) | (k2 & 1) << 1 | (k3 & 1) << 2 | (k4 & 1) << 3);
	send_frame(HTSN_KIND_TELEM_BUTTONS, &mask, 1);
}

void htsn_net_publish_telemetry(const char *kind, const char *payload)
{
	uint8_t k = HTSN_KIND_TELEM_ACTUATOR;
	if (kind && (strcmp(kind, "sonar") == 0))        k = HTSN_KIND_TELEM_SWEEP;
	else if (kind && (strcmp(kind, "buttons") == 0)) k = HTSN_KIND_TELEM_BUTTONS;
	else if (kind && (strcmp(kind, "ptp") == 0))     k = HTSN_KIND_TELEM_PTP;
	send_frame(k, (const uint8_t *)payload, (uint16_t)strlen(payload));
}

/* ---- command thread: blocking recvfrom -> decode -> dispatch ---- */
static htsn_cmd_cb g_cmd_cb = NULL;
static void       *g_cmd_ud = NULL;

void htsn_net_set_cmd_cb(htsn_cmd_cb cb, void *ud) { g_cmd_cb = cb; g_cmd_ud = ud; }

/* ---- TSN config (network-layer op): store + report applied state ----
 * Handled here rather than via the verb callback: the payload is the binary
 * htsn_tsn_cfg_t + GCL array, and the acknowledgement is a telemetry frame. */
static uint8_t  g_tsn_payload[HTSN_FRAME_MAXPLEN]; /* cfg + gcl[] (verbatim) */
static uint16_t g_tsn_payload_len = 0;
static int      g_tsn_valid = 0;

/* What this firmware enforces. gPTP (slave to the RPi grandmaster) is always on.
 * 802.1Q VLAN/PCP tagging of telemetry egress is applied live once a config with
 * vlan_id>0 arrives (raw AF_PACKET path). TAS/Qbv, 802.1Qbb preemption and stream
 * reservation still need the switch. */
static uint32_t tsn_features(void)
{
	uint32_t f = HTSN_TSN_F_PTP;
	if (g_vlan_id > 0) f |= (HTSN_TSN_F_VLAN | HTSN_TSN_F_PCP);
	if (g_preempt && g_vlan_id > 0) f |= HTSN_TSN_F_PREEMPT;
	return f;
}

static void tsn_report_applied(void)
{
	uint8_t pl[HTSN_FRAME_MAXPLEN];
	if (g_tsn_payload_len + 4 > HTSN_FRAME_MAXPLEN)
		g_tsn_payload_len = HTSN_FRAME_MAXPLEN - 4;
	memcpy(pl, g_tsn_payload, g_tsn_payload_len);
	uint32_t f = tsn_features();
	pl[g_tsn_payload_len + 0] = (uint8_t)(f & 0xFF);
	pl[g_tsn_payload_len + 1] = (uint8_t)((f >> 8) & 0xFF);
	pl[g_tsn_payload_len + 2] = (uint8_t)((f >> 16) & 0xFF);
	pl[g_tsn_payload_len + 3] = (uint8_t)((f >> 24) & 0xFF);
	send_frame(HTSN_KIND_TELEM_TSN_APPLIED, pl, (uint16_t)(g_tsn_payload_len + 4));
}

static void tsn_handle_cfg(const uint8_t *p, uint16_t len)
{
	if (len < (uint16_t)sizeof(htsn_tsn_cfg_t) || len > HTSN_FRAME_MAXPLEN - 4) {
		LOGW("tsn cfg: bad len %u\n", (unsigned)len);
		return;
	}
	/* The bridge re-pushes the config on a keepalive (so it survives a reboot);
	 * ignore re-application when it is byte-for-byte unchanged. */
	if (g_tsn_valid && g_tsn_payload_len == len && memcmp(g_tsn_payload, p, len) == 0)
		return;
	memcpy(g_tsn_payload, p, len);
	g_tsn_payload_len = len;
	g_tsn_valid = 1;

	htsn_tsn_cfg_t cfg;
	memcpy(&cfg, p, sizeof(cfg));
	/* Enforce 802.1Q egress tagging: from now on telemetry leaves as a
	 * VID/PCP-tagged frame (raw AF_PACKET). vid==0 reverts to plain UDP. */
	g_vlan_id = cfg.vlan_id;
	g_pcp     = cfg.priority;
	g_preempt = cfg.preemption;
	LOGI("tsn cfg: vid=%u pcp=%u preempt=%u sync=%u role=%u stvid=%u stcp=%u cyc=%lld gcl=%u\n",
	     cfg.vlan_id, cfg.priority, cfg.preemption, cfg.timesync_mode,
	     cfg.stream_role, cfg.stream_vlan_id, cfg.stream_priority,
	     (long long)cfg.tas_cycle_ns, cfg.gcl_count);
	tsn_report_applied();
}

int htsn_net_tsn_cfg(htsn_tsn_cfg_t *out)
{
	if (!g_tsn_valid || !out) return -1;
	memcpy(out, &g_tsn_payload, sizeof(*out));
	return 0;
}

K_THREAD_STACK_DEFINE(cmd_stack, 4096);
static struct k_thread cmd_thread;

static void cmd_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	for (;;) {
		uint8_t buf[HTSN_FRAME_MAX];
		struct sockaddr_in src;
		socklen_t slen = sizeof(src);
		int n = recvfrom(g_sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
		if (n <= 0) { k_msleep(10); continue; }

		uint8_t type, kind, pl[HTSN_FRAME_MAXPLEN];
		size_t plen = 0;
		if (htsn_frame_unpack(buf, (size_t)n, &type, &kind, pl, &plen) != 0) continue;
		if (type != HTSN_TYPE_CMD) continue;

		/* TSN config is a network-layer op handled here (not via the verb callback). */
		if (kind == HTSN_KIND_CMD_TSN_CFG) {
			tsn_handle_cfg(pl, (uint16_t)plen);
			continue;
		}

		char cmd[16];
		char arg[HTSN_FRAME_MAXPLEN + 1];
		memset(arg, 0, sizeof(arg));
		const uint8_t *p = pl;
		uint16_t len = (uint16_t)plen;
		switch (kind) {
			case HTSN_KIND_CMD_SERVO: {
				int16_t a = (len >= 2) ? (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)) : 0;
				snprintf(cmd, sizeof(cmd), "servo"); snprintf(arg, sizeof(arg), "%d", (int)a);
				break;
			}
			case HTSN_KIND_CMD_SONAR:   snprintf(cmd, sizeof(cmd), "sonar");   break;
			case HTSN_KIND_CMD_RELAY: {
				int on = (len >= 1) ? p[0] : 0;
				snprintf(cmd, sizeof(cmd), "relay"); snprintf(arg, sizeof(arg), "%d", on);
				break;
			}
			case HTSN_KIND_CMD_BEEP: {
				int16_t ms = (len >= 2) ? (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)) : 0;
				snprintf(cmd, sizeof(cmd), "beep"); snprintf(arg, sizeof(arg), "%d", (int)ms);
				break;
			}
			case HTSN_KIND_CMD_HUD:
				snprintf(cmd, sizeof(cmd), "hud");
				if (len) memcpy(arg, pl, len);
				break;
			case HTSN_KIND_CMD_REBOOT: snprintf(cmd, sizeof(cmd), "reboot"); break;
			default: continue;
		}
		if (g_cmd_cb) g_cmd_cb(cmd, arg, g_cmd_ud);
	}
}

/* ---- init ---- */
int htsn_net_init(void)
{
	g_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_sock < 0) { LOGE("socket() failed\n"); return -1; }
	struct sockaddr_in b;
	memset(&b, 0, sizeof(b));
	b.sin_family = AF_INET;
	b.sin_port = htons(STM32_CMD_PORT);
	b.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(g_sock, (struct sockaddr *)&b, sizeof(b)) < 0)
		LOGE("bind %d failed\n", STM32_CMD_PORT);
	set_cnc_addr(&g_cnc);

	/* Raw AF_PACKET socket for 802.1Q-tagged egress (used only once a TSN
	 * config with vlan_id>0 is applied). Bind it to the Ethernet interface
	 * with our source MAC; the destination (CNC) MAC is set per-send. */
	struct net_if *iface = net_if_get_default();
	if (iface) {
		g_eth_if = net_if_get_by_iface(iface);
		struct net_linkaddr *ll = net_if_get_link_addr(iface);
		if (ll && ll->len == 6)
			memcpy(g_src_mac, ll->addr, 6);
	}
	if (mac_parse(CNC_MAC, g_dst_mac) != 0)
		LOGW("CNC_MAC parse failed; tagged egress disabled\n");
	g_raw_sock = zsock_socket(NET_AF_PACKET, NET_SOCK_DGRAM,
	                          net_htons(NET_ETH_PTYPE_VLAN));
	if (g_raw_sock >= 0) {
		struct net_sockaddr_ll ll = {0};
		ll.sll_family  = NET_AF_PACKET;
		ll.sll_ifindex = g_eth_if;
		ll.sll_halen   = 6;
		memcpy(ll.sll_addr, g_src_mac, 6);
		if (zsock_bind(g_raw_sock, (const struct net_sockaddr *)&ll, sizeof(ll)) < 0) {
			LOGW("raw socket bind failed; tagged egress disabled\n");
			zsock_close(g_raw_sock);
			g_raw_sock = -1;
		}
	} else {
		LOGW("raw AF_PACKET socket failed; tagged egress disabled\n");
	}

	/* 802.3br preemption demo: periodic, active only once a config with
	 * preemption>0 and vlan>0 is applied. */
	k_work_init_delayable(&g_preempt_work, preempt_work_fn);
	k_work_reschedule(&g_preempt_work, K_MSEC(2000));

	/* Announce to the CNC. */
	char hello[HTSN_FRAME_MAXPLEN];
	snprintf(hello, sizeof(hello), "{\"node\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\"}",
	         HTSN_NODE_ID, HTSN_MAC);
	send_frame(HTSN_KIND_TELEM_HELLO, (uint8_t *)hello, (uint16_t)strlen(hello));

	k_thread_create(&cmd_thread, cmd_stack, K_THREAD_STACK_SIZEOF(cmd_stack),
	                cmd_task_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	LOGI("net up: cmd=:%d -> %s:%d\n", STM32_CMD_PORT, CNC_IP, CNC_TELEM_PORT);
	return 0;
}
