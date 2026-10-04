/* htsn-tsn time sync: PTP v2 (IEEE 1588-2008) slave over UDP (Zephyr sockets).
 *
 * The RPi/CNC runs ptpd as the grandmaster on the same NIC. This endpoint is a
 * boundary-clock TIME-SOURCE CLIENT: it receives SYNC/FOLLOW_UP (2-step GM) or a
 * 1-step SYNC, replies with DELAY_REQ, receives DELAY_RESP, and steers a local
 * high-resolution clock (DWT, mapped by SystemCoreClock) to track the GM.
 * htsn_ptp_time_ns() returns a network-referenced ns time base shared with the
 * CNC.
 *
 * Clock model: local ns = raw_ns() (DWT, since boot) + C, where C is the
 * boot->PTP-epoch mapping. On each completed exchange we take the symmetric-
 * delay estimate  C = ((T4 + T1) - (S2 + S3)) / 2  and slew (small) or step
 * (large) toward it. The exact sign/rate is validated against ptpd on-target.
 *
 * Upgrade path (out of scope here): 802.1AS-2020 link-layer gPTP (0x88F7) + the
 * F767 MAC hardware timestamp for sub-us accuracy under a TSN switch.
 */
#include "ptp.h"

#include "htsn_config.h"
#include "htsn_port.h"
#include "htsn_net.h"

#include <zephyr/kernel.h>
#include <zephyr/cmsis.h>      /* DWT, SystemCoreClock */
#include <zephyr/net/socket.h> /* POSIX socket()/bind()/sendto()/recvfrom() */
#include <sys/socket.h>
#include <netinet/in.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PTP_EVENT_PORT   319
#define PTP_GENERAL_PORT 320

enum {
	PTP_SYNC = 0x0, PTP_DELAY_REQ = 0x1, PTP_FOLLOW_UP = 0x8,
	PTP_DELAY_RESP = 0x9, PTP_ANNOUNCE = 0xB
};

#define PTP_HDR_LEN 44   /* a DELAY_REQ carries no body beyond the header */

/* ---- local 64-bit ns clock (DWT, mapped by SystemCoreClock) ---- */
static volatile uint64_t g_c_ns = 0;     /* boot -> PTP-epoch mapping */
static volatile bool     g_locked = false;
static volatile int64_t  g_last_offset_ns = 0;

/* timing-exchange state */
static volatile bool     g_have_s2 = false;
static volatile uint64_t g_s2;
static volatile bool     g_have_t1 = false;
static volatile uint64_t g_t1;
static volatile uint64_t g_s3;
static volatile uint8_t  g_domain = 0;

static int     g_ev_sock = -1;
static int     g_ge_sock = -1;
static struct sockaddr_in g_gm;         /* GM learned from an event-port source */
static bool    g_gm_known = false;

static uint64_t raw_ns(void)
{
	static uint64_t base = 0;
	static uint32_t base_cycles = 0;
	uint32_t c = DWT->CYCCNT;
	if (c < base_cycles) { base += (0xFFFFFFFFu - base_cycles) + 1u; base_cycles = 0; }
	uint64_t cycles = base + (c - base_cycles);
	return (cycles * 1000000000ULL) / SystemCoreClock;
}

uint64_t htsn_ptp_time_ns(void) { return raw_ns() + g_c_ns; }
bool     htsn_ptp_is_locked(void) { return g_locked; }
int64_t  htsn_ptp_offset_ns(void) { return g_last_offset_ns; }

/* ---- PTP v2 accessors (RFC 5905) over the UDP payload ---- */
static uint8_t  ptp_type(const uint8_t *b)      { return b[1]; }
static uint8_t  ptp_version(const uint8_t *b)   { return b[2]; }
static uint8_t  ptp_domain(const uint8_t *b)    { return b[10]; }

/* Header currentMessageTimestamp: 48-bit seconds (hi16 @12 + lo32 @14) + 32-bit ns @18. */
static uint64_t ptp_hdr_time_ns(const uint8_t *b)
{
	uint64_t sec = ((uint64_t)((uint16_t)(b[12] << 8 | b[13])) << 32)
	             | ((uint32_t)b[14] << 24 | (uint32_t)b[15] << 16 | (uint32_t)b[16] << 8 | b[17]);
	uint32_t ns  = (uint32_t)b[18] << 24 | (uint32_t)b[19] << 16 | (uint32_t)b[20] << 8 | b[21];
	return sec * 1000000000ULL + ns;
}

/* Body 64-bit ns timestamp (originTimestamp @40, preciseOriginTimestamp @40). */
static uint64_t ptp_ts64(const uint8_t *b, int off)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v = (v << 8) | b[off + i];
	return v;
}

static void ptp_write_hdr_time(uint8_t *b, uint64_t ns)
{
	uint64_t sec = ns / 1000000000ULL;
	uint32_t fns = (uint32_t)(ns % 1000000000ULL);
	b[12] = (uint8_t)(sec >> 40); b[13] = (uint8_t)(sec >> 32);      /* secondsFieldHi (16b) */
	b[14] = (uint8_t)(sec >> 24); b[15] = (uint8_t)(sec >> 16);      /* secondsFieldLo (32b) */
	b[16] = (uint8_t)(sec >> 8);  b[17] = (uint8_t)(sec);
	b[18] = (uint8_t)(fns >> 24); b[19] = (uint8_t)(fns >> 16);
	b[20] = (uint8_t)(fns >> 8);  b[21] = (uint8_t)(fns);
}

static void steer(uint64_t c_new)
{
	int64_t delta = (int64_t)(c_new - g_c_ns);
	if (delta > 50000000LL || delta < -50000000LL) {        /* > 50 ms: step */
		g_c_ns = c_new;
		g_last_offset_ns = delta;
	} else if (delta != 0) {                                /* slew ~20% */
		g_c_ns = g_c_ns + delta / 5;
		g_last_offset_ns = delta / 5;
	}
	g_locked = true;
}

/* Build + send a DELAY_REQ to the GM's event port. Returns local raw send time. */
static uint64_t send_delay_req(void)
{
	uint8_t m[PTP_HDR_LEN];
	memset(m, 0, sizeof(m));
	m[1] = PTP_DELAY_REQ;
	m[2] = 2;                 /* versionPTP */
	m[4] = (uint8_t)((PTP_HDR_LEN >> 24) & 0xFF);   /* messageLength: 32-bit big-endian */
	m[5] = (uint8_t)((PTP_HDR_LEN >> 16) & 0xFF);
	m[6] = (uint8_t)((PTP_HDR_LEN >> 8) & 0xFF);
	m[7] = (uint8_t)(PTP_HDR_LEN & 0xFF);
	m[10] = g_domain;
	ptp_write_hdr_time(m, htsn_ptp_time_ns());   /* O = current PTP time */
	uint64_t s3 = raw_ns();
	if (g_ev_sock >= 0 && g_gm_known)
		sendto(g_ev_sock, m, PTP_HDR_LEN, 0, (struct sockaddr *)&g_gm, sizeof(g_gm));
	return s3;
}

static void handle_packet(const uint8_t *buf, int len)
{
	if (len < 32) return;
	if (ptp_version(buf) != 2) return;

	switch (ptp_type(buf)) {
		case PTP_SYNC: {
			g_s2 = raw_ns();
			g_have_s2 = true;
			uint64_t o = ptp_ts64(buf, 40);
			if (o > 0) { g_t1 = o; g_have_t1 = true; }   /* 1-step GM */
			break;
		}
		case PTP_FOLLOW_UP: {
			g_t1 = ptp_ts64(buf, 40);
			g_have_t1 = true;
			break;
		}
		case PTP_ANNOUNCE:
			g_domain = ptp_domain(buf);
			break;
		case PTP_DELAY_RESP: {
			uint64_t t4 = ptp_hdr_time_ns(buf);
			if (g_have_t1 && g_have_s2)
				steer(((t4 + g_t1) - (g_s2 + g_s3)) / 2);
			g_have_t1 = g_have_s2 = false;   /* reset for the next exchange */
			break;
		}
		default: break;
	}
}

static void set_rcv_timeout(int sock)
{
	struct timeval tv = { 0, 20000 };   /* 20 ms */
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static void bind_port(int sock, uint16_t port)
{
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(sock, (struct sockaddr *)&a, sizeof(a));
}

K_THREAD_STACK_DEFINE(ptp_stack, 4096);
static struct k_thread ptp_thread;

static void ptp_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	g_ev_sock = socket(AF_INET, SOCK_DGRAM, 0);
	g_ge_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_ev_sock >= 0) { bind_port(g_ev_sock, PTP_EVENT_PORT);   set_rcv_timeout(g_ev_sock); }
	if (g_ge_sock >= 0) { bind_port(g_ge_sock, PTP_GENERAL_PORT); set_rcv_timeout(g_ge_sock); }

	static uint32_t iters = 0;
	for (;;) {
		uint8_t buf[128];
		if (g_ev_sock >= 0) {
			struct sockaddr_in src;
			socklen_t slen = sizeof(src);
			int n = recvfrom(g_ev_sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
			if (n > 0) {
				if (!g_gm_known) { g_gm = src; g_gm_known = true; }
				handle_packet(buf, n);
			}
			if (g_have_t1 && g_have_s2 && g_gm_known) {
				g_s3 = send_delay_req();
				g_have_t1 = false;          /* wait for DELAY_RESP to finish the exchange */
			}
		}
		if (g_ge_sock >= 0) {
			struct sockaddr_in src;
			socklen_t slen = sizeof(src);
			int n = recvfrom(g_ge_sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
			if (n > 0)
				handle_packet(buf, n);
		}

		/* ~1 Hz PTP status so the CNC can re-publish it to tsn/ptp (GUI metrics). */
		if (++iters >= 1000) {
			iters = 0;
			char st[72];
			snprintf(st, sizeof(st), "{\"offset_ns\":%lld,\"jitter_ns\":0,\"state\":%d}",
			         (long long)htsn_ptp_offset_ns(), htsn_ptp_is_locked() ? 0 : 2);
			htsn_net_publish_telemetry("ptp", st);
		}
		k_msleep(1);
	}
}

int htsn_ptp_init(void)
{
	g_c_ns = 0;
	k_thread_create(&ptp_thread, ptp_stack, K_THREAD_STACK_SIZEOF(ptp_stack),
	                ptp_task_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	LOGI("ptp slave up (v2/UDP 319/320, DWT clock)\n");
	return 0;
}
