/* tsn_opcua_bridge — bidirectional wired TSN data plane on the RPi (CNC).
 *
 * The STM32 endpoint is a light UDP peer; this process is the only thing that
 * knows both sides and shuttles frames:
 *
 *   STM32 --UDP :4001 (telemetry frames)-->  bridge  --OPC UA write-->  cnc_opcua
 *   cnc_opcua cmd_* nodes (changed)  --OPC UA read-->  bridge  --UDP-->  STM32 :4000
 *
 * The endpoint's address is *learned* from the source of the first telemetry
 * datagram (no fixed endpoint IP needed). The read-only poller
 * (tsn_opcua_link) still reads the telemetry nodes this bridge writes, so the
 * GUI's telemetry view is unchanged. The GUI's one-shot `htsn_opcua_cli write`
 * lands on the server; this bridge forwards the matching UDP frame.
 *
 * Env:
 *   HTSN_OPCUA_URL      OPC UA server url   (default opc.tcp://127.0.0.1:4840)
 *   HTSN_TSN_TELEM_PORT UDP telemetry port  (default 4001)
 */
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
#include "htsn_opcua_ids.h"
#include "htsn_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile UA_Boolean running = UA_TRUE;
static void on_signal(int s) { (void)s; running = UA_FALSE; }

static const char *env_or(const char *k, const char *d) {
    const char *v = getenv(k);
    return (v && *v) ? v : d;
}

/* ---- learned endpoint address + UDP ---- */
static int      udp_fd = -1;
static struct sockaddr_in ep;        /* STM32 source (learned) */
static UA_Boolean ep_known = UA_FALSE;

/* ---- tiny JSON field extractors (the on-wire telemetry is small JSON) ---- */
static const char *jfind(const char *buf, const char *key) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(buf, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
static long long jint(const char *buf, const char *key, long long def) {
    const char *p = jfind(buf, key);
    return p ? strtoll(p, NULL, 10) : def;
}
static int jbool(const char *buf, const char *key, int def) {
    const char *p = jfind(buf, key);
    if (!p) return def;
    return (*p == 't' || *p == '1' || *p == 'T' || *p == 'y') ? 1 : 0;
}
static int jintarr(const char *buf, const char *key, long long *out, int maxn) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(buf, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++;
    int n = 0;
    while (n < maxn) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ']') break;
        char *end; long long v = strtoll(p, &end, 10);
        if (end == p) break;
        out[n++] = v;
        p = end;
        while (*p == ' ' || *p == ',') p++;
    }
    return n;
}

/* ---- OPC UA helpers ---- */
static UA_Client *g_c = NULL;
static UA_UInt16  g_ns = HTSN_OPCUA_NS;

static void w_scalar(UA_UInt32 id, int type, const void *val) {
    if (!g_c) return;
    UA_Client_writeValueAttribute_scalar(g_c, UA_NODEID_NUMERIC(g_ns, id), val,
                                         &UA_TYPES[type]);
}
static void w_int16arr(UA_UInt32 id, const int16_t *a, size_t n) {
    if (!g_c) return;
    UA_Variant v; UA_Variant_init(&v);
    v.arrayLength = n;
    v.data = (void *)a;
    v.type = &UA_TYPES[UA_TYPES_INT16];
    UA_Client_writeValueAttribute(g_c, UA_NODEID_NUMERIC(g_ns, id), &v);
}

/* ---- telemetry handlers (STM32 frame -> OPC UA node writes) ---- */
static void on_telemetry(uint8_t kind, const uint8_t *pl, size_t len) {
    char jb[HTSN_FRAME_MAXPLEN + 1];
    switch (kind) {
    case HTSN_KIND_TELEM_HELLO:
        if (len < sizeof(jb)) { memcpy(jb, pl, len); jb[len] = 0;
            fprintf(stderr, "bridge: endpoint hello %s\n", jb); }
        break;
    case HTSN_KIND_TELEM_SWEEP: {
        if (len < 4) break;
        int32_t sid = (int32_t)(pl[0] | pl[1] << 8 | pl[2] << 16 | (uint32_t)pl[3] << 24);
        static int16_t sweep[2048];
        size_t n = (len - 4) / 2;
        if (n > 2048) n = 2048;
        for (size_t i = 0; i < n; i++)
            sweep[i] = (int16_t)((uint16_t)pl[4 + i * 2] | ((uint16_t)pl[5 + i * 2] << 8));
        w_int16arr(HTSN_ID_SONAR_SWEEP, sweep, n);
        int32_t v = sid; w_scalar(HTSN_ID_SONAR_SWEEP_ID, UA_TYPES_INT32, &v);
        break;
    }
    case HTSN_KIND_TELEM_BUTTONS: {
        if (len < 1) break;
        uint8_t m = pl[0];
        UA_Boolean b;
        b = (m & 1) ? UA_TRUE : UA_FALSE; w_scalar(HTSN_ID_BTN1, UA_TYPES_BOOLEAN, &b);
        b = (m & 2) ? UA_TRUE : UA_FALSE; w_scalar(HTSN_ID_BTN2, UA_TYPES_BOOLEAN, &b);
        b = (m & 4) ? UA_TRUE : UA_FALSE; w_scalar(HTSN_ID_BTN3, UA_TYPES_BOOLEAN, &b);
        b = (m & 8) ? UA_TRUE : UA_FALSE; w_scalar(HTSN_ID_BTN4, UA_TYPES_BOOLEAN, &b);
        break;
    }
    case HTSN_KIND_TELEM_ACTUATOR: {
        if (len < sizeof(jb)) { memcpy(jb, pl, len); jb[len] = 0;
            int16_t angle = (int16_t)jint(jb, "angle", 0);
            uint16_t hz = (uint16_t)jint(jb, "hz", 0);
            uint16_t ms = (uint16_t)jint(jb, "ms", 0);
            UA_Boolean on = jbool(jb, "on", 0) ? UA_TRUE : UA_FALSE;
            w_scalar(HTSN_ID_SERVO_ANGLE, UA_TYPES_INT16, &angle);
            w_scalar(HTSN_ID_BUZZER_HZ, UA_TYPES_UINT16, &hz);
            w_scalar(HTSN_ID_BUZZER_MS, UA_TYPES_UINT16, &ms);
            w_scalar(HTSN_ID_RELAY_ON, UA_TYPES_BOOLEAN, &on);
        }
        break;
    }
    case HTSN_KIND_TELEM_PTP: {
        if (len < sizeof(jb)) { memcpy(jb, pl, len); jb[len] = 0;
            int64_t off = jint(jb, "offset_ns", 0);
            uint8_t st = (uint8_t)jint(jb, "state", 0);
            UA_Boolean locked = jbool(jb, "locked", 0) ? UA_TRUE : UA_FALSE;
            w_scalar(HTSN_ID_PTP_OFFSET_NS, UA_TYPES_INT64, &off);
            w_scalar(HTSN_ID_PTP_STATE, UA_TYPES_BYTE, &st);
            w_scalar(HTSN_ID_PTP_LOCKED, UA_TYPES_BOOLEAN, &locked);
        }
        break;
    }
    case HTSN_KIND_TELEM_TSN_APPLIED: {
        if (len < sizeof(htsn_tsn_cfg_t)) break;
        htsn_tsn_cfg_t cfg;
        memcpy(&cfg, pl, sizeof(cfg));
        w_scalar(HTSN_ID_TSN_APP_VLAN, UA_TYPES_INT16, &cfg.vlan_id);
        w_scalar(HTSN_ID_TSN_APP_PRIO, UA_TYPES_BYTE, &cfg.priority);
        w_scalar(HTSN_ID_TSN_APP_PREEMPT, UA_TYPES_BYTE, &cfg.preemption);
        w_scalar(HTSN_ID_TSN_APP_TIMESYNC, UA_TYPES_BYTE, &cfg.timesync_mode);
        w_scalar(HTSN_ID_TSN_APP_STROLE, UA_TYPES_BYTE, &cfg.stream_role);
        w_scalar(HTSN_ID_TSN_APP_STVLAN, UA_TYPES_INT16, &cfg.stream_vlan_id);
        int64_t cyc = cfg.tas_cycle_ns;
        w_scalar(HTSN_ID_TSN_APP_TASCYC, UA_TYPES_INT64, &cyc);
        uint32_t feat = 0;
        size_t gcl_bytes = (size_t)cfg.gcl_count * HTSN_TSN_GCL_ENTRY;
        if (len >= sizeof(cfg) + gcl_bytes + 4)
            memcpy(&feat, pl + sizeof(cfg) + gcl_bytes, 4);
        int32_t fv = (int32_t)feat;
        w_scalar(HTSN_ID_TSN_FEATURES, UA_TYPES_INT32, &fv);
        break;
    }
    default:
        break;
    }
}

/* ---- command forwarding (OPC UA node change -> STM32 UDP frame) ---- */
static int send_frame(uint8_t type, uint8_t kind, const uint8_t *pl, uint16_t len) {
    if (!ep_known) return -1;
    uint8_t frame[HTSN_FRAME_MAX];
    int flen = htsn_frame_pack(frame, type, kind, pl, len);
    if (flen < 0) return -1;
    return (int)sendto(udp_fd, frame, (size_t)flen, 0,
                       (struct sockaddr *)&ep, sizeof(ep));
}

static int send_cmd_kind(uint8_t kind, const uint8_t *pl, uint16_t len) {
    return send_frame(HTSN_TYPE_CMD, kind, pl, len);
}

static char last_tsn[HTSN_FRAME_MAXPLEN + 1] = {0};

static void push_tsn_config(const char *json) {
    htsn_tsn_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.priority        = (uint8_t)jint(json, "priority", 0);
    cfg.traffic_class   = (uint8_t)jint(json, "traffic_class", 0);
    cfg.vlan_id         = (uint16_t)jint(json, "vlan_id", 0);
    cfg.preemption      = (uint8_t)jint(json, "preemption", 0);
    cfg.timesync_mode   = (uint8_t)jint(json, "timesync_mode", 0);
    cfg.stream_role     = (uint8_t)jint(json, "st_role", 0);
    cfg.stream_vlan_id  = (uint16_t)jint(json, "st_vlan", 0);
    cfg.stream_priority = (uint8_t)jint(json, "st_prio", 0);
    cfg.tas_cycle_ns    = jint(json, "tas_cycle_ns", 0);
    long long gs[HTSN_TSN_MAX_GCL], dur[HTSN_TSN_MAX_GCL];
    int ngs = jintarr(json, "gcl_gs", gs, HTSN_TSN_MAX_GCL);
    int ndur = jintarr(json, "gcl_dur", dur, HTSN_TSN_MAX_GCL);
    cfg.gcl_count = (uint16_t)(ngs < ndur ? ngs : ndur);

    uint8_t pl[HTSN_FRAME_MAXPLEN];
    memcpy(pl, &cfg, sizeof(cfg));
    uint16_t off = (uint16_t)sizeof(cfg);
    for (int i = 0; i < cfg.gcl_count && off + HTSN_TSN_GCL_ENTRY <= HTSN_FRAME_MAXPLEN; i++) {
        pl[off++] = (uint8_t)gs[i];
        for (int b = 0; b < 8; b++)
            pl[off++] = (uint8_t)((uint64_t)dur[i] >> (8 * b));
    }
    if (send_cmd_kind(HTSN_KIND_CMD_TSN_CFG, pl, off) >= 0) {
        snprintf(last_tsn, sizeof(last_tsn), "%s", json);
        fprintf(stderr, "bridge: TSN config -> endpoint (vlan=%d prio=%d role=%d cyc=%lld gcl=%d)\n",
                cfg.vlan_id, cfg.priority, cfg.stream_role, (long long)cfg.tas_cycle_ns, cfg.gcl_count);
    }
}

/* Track last-sent actuator values so we only forward on change. */
static int16_t last_servo = 0;
static UA_Boolean last_relay = UA_FALSE;
static int16_t last_beep = 0;
static UA_Boolean primed = UA_FALSE;

static UA_StatusCode rd_scalar(UA_UInt32 id, int type, void *out) {
    UA_Variant v; UA_Variant_init(&v);
    UA_StatusCode rv = UA_Client_readValueAttribute(g_c, UA_NODEID_NUMERIC(g_ns, id), &v);
    if (rv == UA_STATUSCODE_GOOD && v.data && v.type == &UA_TYPES[type])
        memcpy(out, v.data, UA_TYPES[type].memSize);
    UA_Variant_clear(&v);
    return rv;
}

static void poll_commands(void) {
    /* TSN config (String) — forward whenever the GUI wrote a new snapshot. */
    UA_Variant v; UA_Variant_init(&v);
    if (UA_Client_readValueAttribute(g_c, UA_NODEID_NUMERIC(g_ns, HTSN_ID_CMD_TSN_CONFIG), &v)
        == UA_STATUSCODE_GOOD && v.data && v.type == &UA_TYPES[UA_TYPES_STRING]) {
        UA_String *us = (UA_String *)v.data;
        char s[HTSN_FRAME_MAXPLEN + 1];
        size_t n = us->length < sizeof(s) - 1 ? us->length : sizeof(s) - 1;
        memcpy(s, us->data, n); s[n] = 0;
        if (s[0] && strcmp(s, last_tsn) != 0)
            push_tsn_config(s);
    }
    UA_Variant_clear(&v);

    /* Actuator level commands — forward on change (skip the startup baseline). */
    int16_t a; UA_Boolean b; int16_t ms;
    if (rd_scalar(HTSN_ID_CMD_SERVO_ANGLE, UA_TYPES_INT16, &a) == UA_STATUSCODE_GOOD) {
        if (primed && a != last_servo) {
            uint8_t pl[2] = { (uint8_t)(a & 0xFF), (uint8_t)((a >> 8) & 0xFF) };
            send_cmd_kind(HTSN_KIND_CMD_SERVO, pl, 2);
        }
        last_servo = a;
    }
    if (rd_scalar(HTSN_ID_CMD_RELAY_ON, UA_TYPES_BOOLEAN, &b) == UA_STATUSCODE_GOOD) {
        if (primed && b != last_relay) {
            uint8_t pl[1] = { b ? 1 : 0 };
            send_cmd_kind(HTSN_KIND_CMD_RELAY, pl, 1);
        }
        last_relay = b;
    }
    if (rd_scalar(HTSN_ID_CMD_BEEP_MS, UA_TYPES_INT16, &ms) == UA_STATUSCODE_GOOD) {
        if (primed && ms != last_beep) {
            uint8_t pl[2] = { (uint8_t)(ms & 0xFF), (uint8_t)((ms >> 8) & 0xFF) };
            send_cmd_kind(HTSN_KIND_CMD_BEEP, pl, 2);
        }
        last_beep = ms;
    }
    UA_Boolean trig, reb, offv = UA_FALSE;
    if (rd_scalar(HTSN_ID_CMD_SONAR_TRIG, UA_TYPES_BOOLEAN, &trig) == UA_STATUSCODE_GOOD && trig) {
        send_cmd_kind(HTSN_KIND_CMD_SONAR, NULL, 0);
        w_scalar(HTSN_ID_CMD_SONAR_TRIG, UA_TYPES_BOOLEAN, &offv); /* re-arm */
    }
    if (rd_scalar(HTSN_ID_CMD_REBOOT, UA_TYPES_BOOLEAN, &reb) == UA_STATUSCODE_GOOD && reb) {
        send_cmd_kind(HTSN_KIND_CMD_REBOOT, NULL, 0);
        w_scalar(HTSN_ID_CMD_REBOOT, UA_TYPES_BOOLEAN, &offv);
    }
    primed = UA_TRUE;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    const char *url = env_or("HTSN_OPCUA_URL", "opc.tcp://127.0.0.1:4840");
    int telem_port = (int)atol(env_or("HTSN_TSN_TELEM_PORT", "4001"));

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("socket"); return 1; }
    int one = 1; setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in bnd; memset(&bnd, 0, sizeof(bnd));
    bnd.sin_family = AF_INET;
    bnd.sin_port = htons((uint16_t)telem_port);
    bnd.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp_fd, (struct sockaddr *)&bnd, sizeof(bnd)) < 0) {
        perror("bind telemetry port"); return 1;
    }
    fprintf(stderr, "bridge: listening UDP :%d for endpoint telemetry\n", telem_port);

    while (running) {
        if (!g_c) {
            g_c = UA_Client_new();
            if (UA_Client_connect(g_c, url) != UA_STATUSCODE_GOOD) {
                fprintf(stderr, "bridge: not connected to %s (retrying)\n", url);
                UA_Client_delete(g_c); g_c = NULL;
                for (int i = 0; i < 10 && running; i++) usleep(100000);
                continue;
            }
            fprintf(stderr, "bridge: connected to %s\n", url);
        }
        UA_Client_run_iterate(g_c, 0);

        fd_set rfds; FD_ZERO(&rfds); FD_SET(udp_fd, &rfds);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;
        int sr = select(udp_fd + 1, &rfds, NULL, NULL, &tv);
        if (sr > 0 && FD_ISSET(udp_fd, &rfds)) {
            uint8_t buf[HTSN_FRAME_MAX];
            struct sockaddr_in src; socklen_t slen = sizeof(src);
            ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
            if (n > 0) {
                if (!ep_known) { ep = src; ep_known = UA_TRUE;
                    char ip[32]; inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
                    fprintf(stderr, "bridge: learned endpoint %s:%d\n", ip, ntohs(src.sin_port)); }
                uint8_t type, kind, pl[HTSN_FRAME_MAXPLEN];
                size_t plen = 0;
                if (htsn_frame_unpack(buf, (size_t)n, &type, &kind, pl, &plen) == 0) {
                    if (type == HTSN_TYPE_TELEM) on_telemetry(kind, pl, plen);
                }
            }
        }
        if (ep_known) poll_commands();
    }
    if (g_c) { UA_Client_disconnect(g_c); UA_Client_delete(g_c); }
    close(udp_fd);
    return 0;
}
