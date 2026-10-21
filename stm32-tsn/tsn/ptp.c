/* htsn-tsn gPTP / 802.1AS slave — Zephyr (POSIX sockets, DWT timebase).
 *
 * Speaks the IEEE 1588-2019 (802.1AS-Rev) wire format that the RPi grandmaster
 * (ptp4l, linuxptp 3.x) emits:
 *   [0]  tsmt: transportSpecific(4) | messageType(4)
 *   [1]  ver:  minorVersionPTP(4)   | versionPTP(4)
 *   [2]  messageLength(16)
 *   [4]  domainNumber(8)
 *   [6]  flagField(16)
 *   [8]  correction(64)
 *   [16] reserved2(32)
 *   [20] clockIdentity(48)
 *   [26] portNumber(16)
 *   [28] sequenceId(16)
 *   [30] control(8)
 *   [31] logMessageInterval(8)
 *   [32] body — for Sync/Follow_Up/Delay_Resp the originTimestamp lives here (8B:
 *        4B seconds + 4B nanoseconds, big-endian).
 *
 * The RPi is the grandmaster (domain 50); this endpoint is the SyncClient. It
 * joins the PTP multicast group (224.0.1.129) to receive the GM's
 * Sync/Follow_Up/Announce, multicasts a Delay_Req on the event port, and steers
 * the DWT timebase toward the GM with offset = ((T1 + T2) - (T3 + T4)) / 2.
 */
#include "ptp.h"
#include "htsn_clock.h"
#include "htsn_config.h"
#include "htsn_port.h"
#include "htsn_net.h"

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/netinet/in.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>

#include <string.h>
#include <stdint.h>
#include <errno.h>

/* PTP v2 UDP ports + the standard PTP multicast group 224.0.1.129. */
#define PTP_EVENT_PORT   319
#define PTP_GENERAL_PORT 320
#define PTP_MCAST_ADDR   0xE0000181u   /* 224.0.1.129, host order */

/* 1588-2019 message types (low nibble of the tsmt byte). */
#define PTP_SYNC                 0x0
#define PTP_DELAY_REQ            0x1
#define PTP_PDELAY_REQ           0x2
#define PTP_PDELAY_RESP          0x3
#define PTP_FOLLOW_UP            0x8
#define PTP_DELAY_RESP           0x9
#define PTP_PDELAY_RESP_FOLLOW_UP 0xA
#define PTP_ANNOUNCE             0xB

#define PTP_HDR_LEN       32u          /* 1588-2019 header length */
#define PTP_BODY_TIME_OFF 32u          /* originTimestamp / preciseOriginTimestamp */

static int      g_ev_sock  = -1;   /* event port 319 */
static int      g_gen_sock = -1;   /* general port 320 */
static uint8_t  g_domain   = HTSN_PTP_DOMAIN;   /* PTP domainNumber */
static volatile int g_domain_pinned = 0;
static uint64_t g_t1;             /* GM Sync/Follow_Up origin time (T1) */
static uint64_t g_t2;             /* GM Delay_Resp origin time (T4)     */
static uint64_t g_s2;             /* local Sync receive time (T2)       */
static uint64_t g_s3;             /* local Delay_Req send time (T3)     */
static volatile int have_t1 = 0;
static volatile int have_s2 = 0;      /* local Sync-receive timestamp valid */
static volatile int have_s3 = 0;      /* local Delay_Req-send timestamp valid */
static uint16_t g_sync_seq = 0;       /* seq of the Sync we timestamped */
static uint16_t g_dr_seq = 0;         /* seq of the Delay_Req we timestamped */
static uint16_t g_dr_next = 0;        /* next Delay_Req sequence id */
static volatile int g_use_e2e = 0;    /* 4-timestamp path active */
static volatile int g_off_valid = 0;  /* g_off_ns holds a fresh measurement */
static volatile int64_t g_off_ns = 0; /* last measured (slave - master) */
static struct sockaddr_in g_gm;   /* GM's source address (from Sync) */
static volatile int g_gm_known = 0;
static volatile int g_joined = 0; /* multicast join confirmed on both sockets */

/* ---- 1588-2019 header accessors ---- */
static uint8_t ptp_type(const uint8_t *b)    { return b[0] & 0x0f; }
static uint8_t ptp_version(const uint8_t *b) { return b[1] & 0x0f; }
static uint8_t ptp_domain(const uint8_t *b)  { return b[4]; }
static uint16_t ptp_seq(const uint8_t *b)    { return (uint16_t)((b[28] << 8) | b[29]); }

/* 8-byte PTP timestamp (4B seconds + 4B nanoseconds, big-endian) at `off`. */
static uint64_t ptp_ts64(const uint8_t *b, size_t off)
{
	uint32_t sec = ((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16) |
	              ((uint32_t)b[off + 2] << 8) | (uint32_t)b[off + 3];
	uint32_t ns  = ((uint32_t)b[off + 4] << 24) | ((uint32_t)b[off + 5] << 16) |
	              ((uint32_t)b[off + 6] << 8) | (uint32_t)b[off + 7];
	return (uint64_t)sec * 1000000000ULL + (uint64_t)ns;
}

/* The primary timestamp (T1/T2) is the body originTimestamp at [32..39]. */
static uint64_t ptp_body_time_ns(const uint8_t *b)
{
	return ptp_ts64(b, PTP_BODY_TIME_OFF);
}

/* Apply a measured correction `corr` = (master - slave): positive means our
 * clock is behind the GM. The first correction is a full epoch alignment;
 * afterwards only bounded fine steering. Records the measured error. */
static void ptp_correct(int64_t corr)
{
	/* Once converged (last offset small), ignore wildly-out-of-range samples
	 * (measurement glitches from the coarse DWT timestamp) instead of
	 * re-aligning on them. Before convergence we must accept them to align. */
	if (g_off_valid && g_off_ns < 100000000LL && g_off_ns > -100000000LL &&
	    (corr > 10000000000LL || corr < -10000000000LL)) {
		return;
	}

	g_off_ns = -corr;          /* measured slave-minus-master error */
	g_off_valid = 1;

	/* Coarse/epoch scale: apply in full. Idempotent — moves our clock onto
	 * the GM, so the next measurement is ~0. */
	if (corr > 1000000000LL || corr < -1000000000LL) {
		htsn_clock_apply_offset(corr);
		return;
	}

	/* Fine scale: bounded steps with a dead-band. */
	if (corr > 100000000LL)       corr = 100000000LL;
	else if (corr < -100000000LL) corr = -100000000LL;
	if (corr > 1000 || corr < -1000) {
		htsn_clock_apply_offset(corr);
	}
}

static uint64_t raw_ns(void) { return htsn_ptp_time_ns(); }

/* Join the PTP multicast group on `sock` (the default interface). Zephyr's
 * IP_ADD_MEMBERSHIP expects struct net_ip_mreqn, not the POSIX struct ip_mreq;
 * with the wrong size the kernel returns EINVAL. If the interface is still
 * down the join is deferred by the net stack and applied when it comes up.
 * Returns 1 on success, 0 on failure. */
static int join_ptp_multicast(int sock)
{
	struct net_ip_mreqn mreqn;
	memset(&mreqn, 0, sizeof(mreqn));
	mreqn.imr_multiaddr.s_addr = htonl(PTP_MCAST_ADDR);
	mreqn.imr_address.s_addr   = htonl(INADDR_ANY);
	mreqn.imr_ifindex          = 0;
	return (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn,
	                   sizeof(mreqn)) == 0) ? 1 : 0;
}

/* Send one Delay_Req (event, multicast 224.0.1.129:319); returns T4 (now).
 * T4 (our send time) is stamped into the originTimestamp field. */
static uint64_t send_delay_req(void)
{
	uint8_t m[44];   /* hdr(32) + originTimestamp(8) + TLV terminator(4) */
	memset(m, 0, sizeof(m));
	m[0]  = PTP_DELAY_REQ;             /* tsmt: transportSpecific=0 | type=1 */
	m[1]  = 2;                         /* ver:  versionPTP=2 */
	m[2]  = (uint8_t)(44u >> 8);       /* messageLength hi */
	m[3]  = (uint8_t)(44u & 0xFF);     /* messageLength lo */
	m[4]  = g_domain;                  /* domainNumber */
	uint16_t seq = g_dr_next++;        /* sequenceId (echoed in Delay_Resp) */
	m[28] = (uint8_t)(seq >> 8);
	m[29] = (uint8_t)(seq & 0xFF);
	g_dr_seq = seq;
	uint64_t now = htsn_ptp_time_ns();
	uint32_t sec = (uint32_t)(now / 1000000000ULL);
	uint32_t ns  = (uint32_t)(now % 1000000000ULL);
	m[32] = (uint8_t)(sec >> 24); m[33] = (uint8_t)(sec >> 16);
	m[34] = (uint8_t)(sec >> 8);  m[35] = (uint8_t)sec;
	m[36] = (uint8_t)(ns >> 24);  m[37] = (uint8_t)(ns >> 16);
	m[38] = (uint8_t)(ns >> 8);   m[39] = (uint8_t)ns;
	/* m[40..43] = 0 (TLV terminator) */

	uint64_t s4 = raw_ns();
	if (g_ev_sock >= 0) {
		struct sockaddr_in dst;
		memset(&dst, 0, sizeof(dst));
		dst.sin_family = AF_INET;
		dst.sin_port = htons(PTP_EVENT_PORT);
		dst.sin_addr.s_addr = htonl(PTP_MCAST_ADDR);
		sendto(g_ev_sock, m, sizeof(m), 0, (struct sockaddr *)&dst, sizeof(dst));
	}
	return s4;
}

static void handle_packet(const uint8_t *buf, int n, int is_event,
                          const struct sockaddr_in *src)
{
	(void)is_event;
	if (n < (int)PTP_HDR_LEN) return;
	if (ptp_version(buf) != 2) return;
	uint8_t d = ptp_domain(buf);
	if (g_domain_pinned) {
		if (d != g_domain) return;
	} else if (ptp_type(buf) == PTP_ANNOUNCE) {
		g_domain = d; g_domain_pinned = 1;
	} else if (d != g_domain) {
		return;
	}

	switch (ptp_type(buf)) {
	case PTP_ANNOUNCE:
		if (!g_gm_known) { memcpy(&g_gm, src, sizeof(g_gm)); g_gm_known = 1; }
		break;

	case PTP_SYNC: {
		g_sync_seq = ptp_seq(buf);
		g_s2 = raw_ns();                    /* T2: local Sync receive time */
		have_s2 = 1;
		have_t1 = 0;                        /* start a fresh sync round */
		if (!g_gm_known) { memcpy(&g_gm, src, sizeof(g_gm)); g_gm_known = 1; }
		{
			uint64_t t1 = ptp_body_time_ns(buf);

			if (t1 > 0) {               /* 1-step GM: T1 in the Sync body */
				g_t1 = t1;
				have_t1 = 1;
				if (!g_use_e2e) {
					ptp_correct((int64_t)g_t1 - (int64_t)g_s2);
				}
			}
		}
		break;
	}

	case PTP_FOLLOW_UP:
		/* Only pair with the Sync it belongs to (matching sequenceId). */
		if (have_s2 && ptp_seq(buf) == g_sync_seq) {
			g_t1 = ptp_body_time_ns(buf);   /* 2-step GM: T1 here */
			have_t1 = 1;
			if (!g_use_e2e) {
				/* One-way (T1 vs T2): converges even without a
				 * working Delay_Resp leg. */
				ptp_correct((int64_t)g_t1 - (int64_t)g_s2);
			}
		}
		break;

	case PTP_DELAY_RESP:
		/* Pair with the Delay_Req it answers (matching sequenceId). */
		if (have_s3 && ptp_seq(buf) == g_dr_seq) {
			g_t2 = ptp_body_time_ns(buf);   /* T4: GM saw our Delay_Req */
			have_s3 = 0;
			if (have_s2 && have_t1) {
				/* Full E2E round: offset = ((t1+t4)-(t2+t3))/2 */
				ptp_correct(((((int64_t)g_t1) + (int64_t)g_t2) -
				             ((int64_t)g_s2 + (int64_t)g_s3)) / 2);
				g_use_e2e = 1;
			}
		}
		break;

	default:
		break;
	}
}

static void ptp_thread_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	/* Join the PTP multicast group so the GM's multicast Sync/Follow_Up is
	 * accepted at L3 (needed for the clock lock). Requires CONFIG_NET_IPV4_IGMP
	 * (without it net_ipv4_igmp_join is a stub returning -ENOSYS). Re-issue a
	 * few times while the interface settles. The Delay_Req path below does not
	 * depend on the join, so a failed join is non-fatal: we still show up on the
	 * wire and the GM still answers with a Delay_Resp. */
	for (int tries = 0; tries < 6 && !g_joined; tries++) {
		if (g_ev_sock >= 0 && g_gen_sock >= 0 &&
		    join_ptp_multicast(g_ev_sock) && join_ptp_multicast(g_gen_sock)) {
			g_joined = 1;
			break;
		}
		k_sleep(K_MSEC(500));
	}
	if (g_joined)
		LOGI("ptp: multicast 224.0.1.129 joined\n");
	else
		LOGW("ptp: multicast join failed; Delay_Req fire-and-forget\n");
	for (;;) {
		static int pub_tick = 0;
		{
			uint8_t buf[128];
			struct sockaddr_in src;
			socklen_t sl;
			if (g_ev_sock >= 0) {
				sl = sizeof(src);
				int n = recvfrom(g_ev_sock, buf, sizeof(buf), MSG_DONTWAIT,
				                 (struct sockaddr *)&src, &sl);
				if (n > 0) handle_packet(buf, n, 1, &src);
			}
			if (g_gen_sock >= 0) {
				sl = sizeof(src);
				int n = recvfrom(g_gen_sock, buf, sizeof(buf), MSG_DONTWAIT,
				                 (struct sockaddr *)&src, &sl);
				if (n > 0) handle_packet(buf, n, 0, &src);
			}
		}
		/* Periodic E2E exchange: one Delay_Req per loop iteration. Sent even
		 * before the GM's multicast Sync is seen; the GM replies with a
		 * Delay_Resp, so both directions appear on the wire. */
		g_s3 = send_delay_req();
		have_s3 = 1;

		/* Report sync status (~1 Hz) via the HTSN PTP telemetry kind. */
		if (++pub_tick >= 20) {
			pub_tick = 0;
			char js[64];
			int jn = snprintf(js, sizeof(js),
			                  "{\"offset_ns\":%lld,\"locked\":%d}",
			                  (long long)htsn_ptp_offset_ns(),
			                  htsn_ptp_is_locked() ? 1 : 0);
			if (jn > 0) {
				htsn_net_publish_telemetry("ptp", js);
			}
		}
		k_sleep(K_MSEC(50));
	}
}

K_THREAD_STACK_DEFINE(ptp_stack, 8192);
static struct k_thread ptp_thread;

int htsn_ptp_init(void)
{
	g_ev_sock  = socket(AF_INET, SOCK_DGRAM, 0);
	g_gen_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_ev_sock < 0 || g_gen_sock < 0) {
		LOGE("ptp: socket() failed\n");
		return -1;
	}
	struct sockaddr_in b;
	memset(&b, 0, sizeof(b));
	b.sin_family = AF_INET;
	b.sin_port = htons(PTP_EVENT_PORT);
	b.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(g_ev_sock, (struct sockaddr *)&b, sizeof(b));
	b.sin_port = htons(PTP_GENERAL_PORT);
	bind(g_gen_sock, (struct sockaddr *)&b, sizeof(b));
	{
		/* Don't loop our own multicast Delay_Req back into RX. */
		int loop = 0;
		setsockopt(g_ev_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
		setsockopt(g_gen_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
	}

	/* Receive the GM's multicast Sync/Follow_Up/Announce. */
	join_ptp_multicast(g_ev_sock);
	join_ptp_multicast(g_gen_sock);

	/* Prio 4: above the net TX (~7), flow (6) and cmd (5) threads so PTP
	 * timestamp reads / Delay_Req sends are prompt, but below the RX thread
	 * (prio 2) and heartbeat (3) which must run first. */
	k_thread_create(&ptp_thread, ptp_stack, K_THREAD_STACK_SIZEOF(ptp_stack),
	                (k_thread_entry_t)ptp_thread_fn, NULL, NULL, NULL,
	                4, 0, K_NO_WAIT);
	LOGI("ptp: 802.1AS slave up (domain %u, GM=%s)\n", g_domain, CNC_IP);
	return 0;
}

void htsn_ptp_set_domain(uint8_t domain)
{
	g_domain = domain;
	g_domain_pinned = 1;
	LOGI("ptp: domain pinned to %u\n", domain);
}

bool htsn_ptp_is_locked(void)
{
	/* Locked = we have a fresh offset measurement within 1 ms of the GM. */
	return g_gm_known && g_off_valid &&
	       g_off_ns < 1000000LL && g_off_ns > -1000000LL;
}

int64_t htsn_ptp_offset_ns(void)
{
	return g_off_ns;
}
