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
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/netinet/in.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int  g_sock = -1;
static struct sockaddr_in g_cnc;   /* CNC destination (telemetry out) */

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

static void send_frame(uint8_t kind, const uint8_t *payload, uint16_t len)
{
	uint8_t frame[HTSN_FRAME_MAX];
	int flen = htsn_frame_pack(frame, HTSN_TYPE_TELEM, kind, payload, len);
	if (flen > 0 && g_sock >= 0)
		sendto(g_sock, frame, (size_t)flen, 0, (struct sockaddr *)&g_cnc, sizeof(g_cnc));
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

/* What this firmware actually enforces. 802.1Q VLAN/PCP tagging is a documented
 * TODO; TAS/Qbv, 802.1Qbb preemption and stream reservation also need the switch.
 * gPTP (slave to the RPi grandmaster) is the only TSN feature applied here. */
static uint32_t tsn_features(void)
{
	return HTSN_TSN_F_PTP;
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
	memcpy(g_tsn_payload, p, len);
	g_tsn_payload_len = len;
	g_tsn_valid = 1;

	htsn_tsn_cfg_t cfg;
	memcpy(&cfg, p, sizeof(cfg));
	/* Enforcement points for VLAN/PCP/TAS go here once the Zephyr net-if tagging
	 * TODO is closed; for now the config is stored and acknowledged. */
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
